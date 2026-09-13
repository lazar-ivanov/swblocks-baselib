# Splitting the Oversized Test Modules

> **On approval, this document is committed to
> `notes/reviews/major/update_2026/test-module-split-plan.md`** and becomes the tracked source of
> truth, alongside the ledger described in §9.
>
> **Web copy (for hand-off to another machine):**
> https://claude.ai/code/artifact/efe14d04-e4a3-419c-9454-477401a64254
> It is a convenience copy for reading; the committed file and ledger remain authoritative, and the
> Artifact is refreshed at each checkpoint rather than edited in place.

## Context

Every test module in `src/utests/` is built as a **single translation unit**: one `<Module>Main.cpp`
that defines `UTF_TEST_MODULE`, includes `<utests/baselib/UtfMain.h>`, then `#include`s every
`Test*.h` in its directory. Nothing caps the growth of that TU. Three consequences:

1. **x86 32-bit toolchains cannot build it.** `clang-cl` 16.0.5 as a 32-bit host crashes
   (`0xC000001D`, out of ~2 GB address space) on `UtfBaselibMessagingMain.cpp`, whose x86 debug
   object is **112.7 MB**. Recorded in
   [x86-clang-cl-host-and-test-module-size-deferral.md](../../../plans/issues/x86-clang-cl-host-and-test-module-size-deferral.md),
   items 3/4/5. The 2026-09-10 fix swapped in a 64-bit clang-cl host — headroom, not a cure.
2. **Compilation is slow and unparallelizable.** ~19 long single-file compiles dominate every build.
   Under gcc `-O2` the largest TU peaks near **3.7 GB resident**, which is why [AGENTS.md](../../../../AGENTS.md)
   and [scripts/devenv7/AGENTS.md](../../../../scripts/devenv7/AGENTS.md) both mandate `-j1`.
3. **Test execution cannot be parallelized** beyond the current 18 binaries.

The deferral's own reopen trigger — "any test module's x86 debug object approaches ~100 MB" — **is
already tripped**: messaging is at 112.7 MB (vc143) / 110.3 MB (ccl16) in the tree right now.

**Intended outcome.** Split the oversized modules into numbered siblings so **no x86 debug object
exceeds 40 MB**, then close deferral items 3, 4 (restore `-Zi` for x86 ccl16 release) and 5
(optionally restore the 32-bit host).

---

## 1. The measurement that drives everything

Object size tracks **template instantiation weight, not source lines**. Measured from the
`bld/win-x86-*-debug` trees currently on disk:

- **There is a ~21 MB fixed floor per TU.** `utf_baselib_setprio` is 223 lines of test source and
  still yields a **21.4 MB** object — `UtfMain.h` + baselib + the header-only Boost.Test runner
  alone. **Every new module pays this again.**
- **Lines are actively misleading.** `utf_baselib_apps` is 1,578 lines → **77.2 MB** (49 MB/kline);
  `utf_baselib` is 23,472 lines → 55.3 MB (2.4 MB/kline). A split ranked by lines would rank
  `utf_baselib_apps` 14th of 19 and never touch it.

| Module | vc143 | ccl16 | marginal | headers | TUs @ 40 MB | partitions by whole file? |
|---|---:|---:|---:|---:|---:|---|
| `utf_baselib_messaging` | 112.7 | 110.3 | ~92 | 3 | **5** | needs Step A |
| `utf_baselib_rest` | 78.7 | 78.1 | ~58 | 1 | **4** | needs Step A (trivial) |
| `utf_baselib_io` | 77.4 | 79.4 | ~56 | 1 | **4** | needs Step A (hardest) |
| `utf_baselib_apps` | 77.2 | 77.0 | ~56 | 3 | **4** | **yes, trivially** |
| `utf_baselib_tasks` | 67.7 | 67.9 | ~47 | 8 | **3** | **no — see §5.2** |
| `utf_baselib_security` | 65.0 | 63.9 | ~44 | 9 | **3** | **yes — the pilot** |
| `utf_baselib_blobtransfer` | 56.3 | 57.4 | ~35 | 1 | **2–3** | needs Step A (clean) |
| `utf_baselib_http` | 55.8 | 56.5 | ~35 | 6 | **2–3** | **yes** |
| `utf_baselib` | 55.3 | 55.9 | ~34 | 19 (+2nd TU) | **2–3** | **yes, 2 sticky clusters** |
| `utf_baselib_data` | 49.8 | 47.0 | ~29 | 7 | **2** | **yes, header by header** |

**Consequences to accept up front.** A 40 MB ceiling leaves ~19 MB of test content per TU, so the
tree grows from 18 modules to roughly **35–40** and total object bytes roughly **double**. That is
correct for the goal — the 2 GB limit is *per-process peak*, not aggregate — but it costs disk and
link time, and each new module adds a test process contending on the machine-global lock (§6).

*A64 has no baseline: only `utf_baselib_cmdline` is built for a64 and it is 1.4× its x86 counterpart,
implying a64 messaging ≈ 155 MB. Measure one a64 module as an observation; the gate stays on x86
debug, where the deferral set it.*

### 1.1 An adjunct lever worth measuring before fixing the module count

`src/utests/include/utests/baselib/UtfBaseLibCommon.h:20-29` is an umbrella pulling
`baselib/{core,data,tasks,http,messaging}/PreCompiled.h` — entire subsystems — and **20 test headers
include it**: all three `utf_baselib_apps` headers, all ten `TestBaselibDefault*.h`, and five of seven
`utf_baselib_data` headers. (Despite the name there is no real PCH machinery anywhere — no `/Yc`,
`/Yu`, `-include` or `.gch` in any makefile. These are plain aggregate headers.)

The negative control supports the hypothesis: `utf_baselib_security` (65 MB) includes the umbrella in
**none** of its nine headers — its weight is OpenSSL and the REST/HTTP helpers instead.

If it holds, **narrowing those includes shrinks objects across many modules with no file movement at
all**, and could cut how many new modules are needed — the main cost of this plan. It is a *different
kind of change* (shared-header surgery across 20 headers), so per [AGENTS.md](../../../../AGENTS.md)'s "never mix
changes" rule it must not ride along. Step 0's probe measures it for free.

---

## 2. Decisions taken

| Question | Decision |
|---|---|
| Ceiling | **55 MB** per object, x86 debug, per the deferral record |
| Single-header modules | **Verbatim block relocation** — whole `UTF_AUTO_TEST_CASE` blocks cut unchanged; no test logic rewritten |
| How that is made safe | **Decomposed into Step A + Step B**, each with its own mechanical gate — §4. The central design decision. |
| Execution | **Two interchangeable modes, serial or 3-lane parallel**, switchable at any module boundary — §8 |
| Durability | **All state committed to the repo**; a session may die at any point and be resumed cold — §9 |
| Sequencing | **Pilot `utf_baselib_security`**, settle recipe and tooling, then fan out — two commits per module |

---

## 3. Why the build system needs no change

`projects/make/common.mk:268`:

```make
UTESTS := $(patsubst $(SRCDIR)/utests/%, %, $(wildcard $(SRCDIR)/utests/utf*))
```

Modules are discovered by **directory wildcard**; `common.mk:473` globs `*.cpp` inside each module
dir. Creating `src/utests/utf_baselib_security2/` with a `UtfBaselibSecurity2Main.cpp` is sufficient —
`make utf_baselib_security2`, `test_utf_baselib_security2`, `utests`, `testutf`, `install` and `help`
all pick it up with **zero makefile edits**. Binary name is the module with `_`→`-`
(`common.mk:475-478`); a `data/` subdir is auto-copied to `<binary-stem>-data` (`common.mk:497-507`).

**Each new module is a full module with its own runner.** The deferral frames the split as "one runner
TU plus case-only TUs", whose constraint *"exactly one TU may define `UTF_TEST_MODULE`"* is fiddly.
Separate **modules** avoid it entirely: own `Main.cpp`, own `#define UTF_TEST_MODULE`, own runner, own
binary, own log. Strictly simpler, and the only variant that also buys **test-execution** parallelism.

Two non-build files carry an explicit module list:

- `scripts/generate-eclipse-project-config.py:306-331` — explicit `projects` dict, already stale
  (missing `utf_baselib_apps`). **Convert it to the same directory glob in the pilot**, fixing the
  staleness and removing the one file every lane would otherwise conflict on.
- `settings/vscode/*/*/{tasks,launch}.json` — optional convenience entries.

`.gitignore`, `python-tests.mk` and `install.mk` need nothing.

---

## 4. The core design: two separately-provable steps

The risky operation — "cut test cases out of a 10,000-line header into a different binary" — must
never be one step. Split it:

> **Step A — split the header, same module, same TU.** Cut a block verbatim out of `TestIO.h` into a
> new sibling `TestIO2.h`, `#include`d from the *same* `UtfBaselibIoMain.cpp` immediately after it.
> **Nothing about the build changes**: same TU, same object, same binary, same test inventory.
>
> **Step B — move whole headers into a new module.** `git mv` the now-separate header into
> `src/utests/utf_baselib_io2/` with its own `Main.cpp`. This is exactly the "move files only, never
> split them" operation originally scoped.

**Why this matters: Step A has an extraordinarily strong automatic gate.** The preprocessed TU is
unchanged apart from `#line` bookkeeping, so the object must be **size-identical within ~1%** and the
run identical case-for-case and assertion-for-assertion. An accidental edit, a dropped case, a helper
stranded on the wrong side of the cut — each breaks the build or moves that number. Step A is
provable to a degree no reviewer could match.

Step B then only ever moves whole files, so §7's checks apply directly, and the two failure modes
never compound. Each step is an independently committable, independently green state.

---

## 5. Feasibility, measured per module

None of the four single-header files contains any `UTF_AUTO_TEST_SUITE`, `UTF_GLOBAL_FIXTURE`,
`UTF_FIXTURE_TEST_CASE`, fixture struct or `#define`; **no anonymous namespace wraps a test case** in
any of them. Across all ten modules, **no macro is defined in one header and used in another.**

### 5.1 The four single-header modules

| File | cases | best cuts | groups (A) | groups (B, no hoist) | hardest blocker |
|---|---:|---|---:|---:|---|
| `TestRestDefault.h` | 15 | anywhere between cases | 5 | **5** | none — one anon ns (2983–3044) used only by the case at 3060 |
| `TestBlobTransferFilesystem.h` | 26 | **line 292**; 1531–1670 | 5 | 2–3 | anon ns N2 (293–411) + N3 (456–726) reach forward to cases at 1390/1447/1934 |
| `TestMessagingDefault.h` | 38 | **4576–6914 verbatim, zero fixups**; 9814–10161; 10654–10942 | 4–5 | 3 | `createProtocolMessage`/`context_t` stragglers at 7266/7521/10532/10546 |
| `TestIO.h` | 32 | 3493–3844; 6410–7442; 4069–5543 | 4 | 2 | **nested** anon ns 1718–2156 used at 5634/5690/5767/5911; `simpleConnectAndTransmitDataTest` (233) used at 6133/6141 |

Two results reverse the naive reading:

- **`TestRestDefault.h` is the easiest file in the tree, not the hardest.** Its only file-scope
  construct in 3,481 lines is one anonymous namespace used by the single case that follows it.
  Cuttable between any two cases, cross-TU, with no hoisting.
- **`TestBlobTransferFilesystem.h` cuts at line 292 in a way that is also perfect for §6.** That one
  boundary separates the 12 lock-holding `FilesPackagerInMemory*` cases (all 13 lock sites bar an
  outlier at 1661) from 13 entirely lock-free cases.

### 5.2 The six multi-header modules

| Module | cross-header coupling | `data/` | global fixture | verdict |
|---|---|---|---|---|
| `utf_baselib_data` | **none** | 1 file → `TestDataModelDefault.h` | none | **Best case. Partitions header by header.** |
| `utf_baselib_apps` | **none** | none | none | **Trivial.** Split bl-tool pair vs the gateway header (the heaviest). |
| `utf_baselib_http` | **none** (sharing is via `HttpServerHelpers.h`) | none | none | **Clean.** Split on the lock — see §6. |
| `utf_baselib_security` | **none** | 19 files, cleanly bipartite | none | **Clean. The pilot.** |
| `utf_baselib` | 1 edge: `getFalse()` (`TestBaselibDefault.h:365`) → `TestBaselibDefault3.h:311,329,347` | none | `UtfLoaderInit.h` ↔ 6 `TestObjModel.h` cases | **Clean, 2 sticky clusters.** |
| `utf_baselib_tasks` | **severe** | none | none | **Does not partition — see below.** |

**`utf_baselib_tasks` is the correction to make loudly.** `TestTasks.h` is the deliberate fixture base
for `TestTasks3/4/5/6/7.h` — `ExecutionQueueCompletionControl` (`:635`),
`createControlledCompletionTask` (`:941`), `ExecutionQueueNotificationTestContext` (`:900`),
`MonotonicCounterObservable*` (`:2806`/`:2906`/`:3058`), `waitUntilScheduled`/`waitUntilReturned`/
`completeNext` (`:672`/`:686`/`:700`) — with further lateral edges 5→6/7 and 6→7. The headers say so
in comments (`TestTasks4.h:60-67`, `TestTasks5.h:47-51`, `TestTasks6.h:47-51`, `TestTasks7.h:42-51`).

Only **`TestTasks2.h`** and **`TestTasks8.h`** are extractable as-is. Reaching 3 TUs requires hoisting
`TestTasks.h`'s fixture set into `src/utests/include/utests/baselib/` as its own commit — real work,
so **schedule `tasks` last**, after the recipe is proven elsewhere.

`utf_baselib`'s two sticky clusters: `{TestObjModel.h, UtfLoaderInit.h, examples/objmodel/}` (the
global fixture registers `clsids::MyObjectImpl()` and `ObjModel_LoaderResetTests` at `:788` re-registers
it in a `BL_SCOPE_EXIT`), and `{TestBaselibDefault.h, TestBaselibDefault3.h}` (the `getFalse()` edge —
or copy the two-line helper). The other twelve headers are free.

### 5.3 Blockers and how each is handled

- **Forward-reaching anonymous namespaces.** `TestIO.h`'s **nested** anon ns at 1718–2156 (inside the
  3,100-line ns at 66–3212 — easy to miss) and `TestBlobTransferFilesystem.h`'s N2/N3. These block
  *Step B only*. Either keep dependent cases in one module, or hoist the namespace into the shared
  tree as its own commit. Never copy a helper into two headers of the same module — invariant **C6**.
- **`using namespace bl;` at module scope — verified load-bearing but narrow.** `UtfBaselibMain.cpp:28`
  sets it, and exactly **six cases in `TestBaselibDefault.h`** rely on it (`:5049`, `:5170`, `:6708`,
  `:8230`, `:8358`, `:8672`). All 18 other `utf_baselib` headers are independent of it. Whichever
  module takes `TestBaselibDefault.h` needs that line. Omitting it is a compile error, not a silent
  failure. `TestIO.h:68` has the same pattern inside its anon ns — any hoisted helper carries it.
- **Doc comments are part of the case.** Every case in these files is preceded by a substantive
  `/****…` block. **Cut at the blank line before the comment, not at the macro line.** The Tier 1
  extractor hashes the comment too, or the checks silently permit losing documentation.
- **Include order is load-bearing for Step A.** Header-only definitions with no forward declarations,
  so a new sibling must be `#include`d *after* the header owning the shared anon namespaces.
- **`data/` cannot be shared across modules.** `TestUtils::resolveDataFilePath` resolves
  `<exe-dir>/<exe-stem>-data/` (`TestUtils.h:110-121`) and `install.mk:32-36` installs it per module.
  Group data-consuming headers so duplication is unnecessary — invariant **C7**.

### 5.4 The one ordering contract automation cannot infer

`MessagingUtils_TokenTypeConcurrencyTests` (`TestMessagingDefault.h:3242`, comment at 3248–3255)
deliberately exercises a **cold** process-global cache: `createBrokerProtocolMessage()` lazily
initialises a static `g_tokenType` and *"the window is only open while the cache is cold."* Any earlier
case in the same process that warms it silently neuters the test — **and it still passes.**

**Handling:** pin it, with its comment, to the head of whichever module it lands in, and record why in
the tooling's allowlist. Giving it a module of its own would be safest — a fresh process is a cold
cache by construction. Treat it as the template: grep each remaining header for cold-start/first-use
contracts before splitting it.

---

## 6. Keeping the test suite fast

The machine-global lock (`MachineGlobalTestLock.h`, a `RobustNamedMutex` guarding the fixed port
`UtfArgsParser::PORT_DEFAULT = 28100`) serializes **across processes**, so more modules means more
contenders. Concentrate the lock users:

- **`utf_baselib_blobtransfer` — ideal.** All 13 lock sites at lines 21–257 (one outlier at 1661); the
  cleanest structural cut, line 292, *is* the lock boundary.
- **`utf_baselib_rest` — ideal.** Zero direct sites; the lock is taken inside `TestRestUtils.h`, so it
  is invisible to any split of the header.
- **`utf_baselib_http` — clean.** `TestHttpServer.h`, `TestClientHttpTasks.h` and
  `TestTlsHandshakeVerification.h` take it (mostly implicitly via `HttpServerHelpers.h:355`);
  `TestTlsProtocolPolicy.h`, `TestTlsPeerVerification.h` and `TestAsioSslStreamWrapper.h` are
  lock-free *and* drop the heaviest include — an ideal parallel-safe sibling.
- **`utf_baselib_security` — clean.** Only `TestAuthorizationCacheImpl.h` and
  `TestAuthorizationCacheRestImpl.h` take it, via `TestAuthorizationCacheImplUtils.h`.
- **`utf_baselib_messaging` / `utf_baselib_io` — scattered** (17 and 14 sites). Messaging's zero-fixup
  cut at 4576–6914 takes only 4 of them; io's two cleanest islands are entirely lock-free.
- **`utf_baselib`, `_data`, `_apps`** — effectively lock-free (`TestBaselibDefault5.h:689` uses a
  *named* lock, so it does not serialize against the default; `TestWatchdog.h:19` includes the header
  but never constructs one).

**Record suite wall-clock before and after.** Current shape: 756 cases / ~982 s, dominated by
messaging (~276 s), rest (~190 s), io (~172 s), tasks (~124 s). If total serialized time rises, the
grouping is wrong.

---

## 7. Automated, self-validating verification

The failure mode — a case that silently stops being registered — **looks exactly like success**. It
must not rely on review. New tooling in `scripts/utests/` (Python 3, via the repo `.venv`).

### Tier 1 — source equivalence (no build, ~1 s, every commit)

`utf_inventory.py` extracts every `UTF_AUTO_TEST_CASE( Name )` / `UTF_FIXTURE_TEST_CASE( Name, Fixture )`
block — from the macro line at column 0 to the next line that is exactly `}`, **plus the preceding
`/****…` doc comment**. It emits per case: name, module, file, fixture, `sha256` of the normalized body
*and* of the doc comment, the enclosing `#if` guard stack, and the enclosing namespace stack. It also
hashes the **residue** (everything outside case bodies, chunked at top-level boundaries) and records
each module's referenced data filenames against its actual `data/` contents.

**This is parseable without a C++ parser**: all 773 case macros sit at column 0 with braces at column
0, so extraction is unambiguous — no brace matching, no string/comment handling. Assert that as a
precondition.

`--compare baseline.json current.json` asserts:

| | Invariant | Catches |
|---|---|---|
| **C1** | case-name set identical | a case lost or accidentally added |
| **C2** | `sha256(body)` and `sha256(doc comment)` unchanged | any edit to test logic, or a dropped doc comment |
| **C3** | enclosing `#if` guard stack unchanged | a case silently moving inside/outside a platform guard |
| **C4** | enclosing namespace stack unchanged | a case moving into/out of an anonymous namespace |
| **C5** | no case name appears twice tree-wide | collision; keeps `--run_test=` recipes unambiguous |
| **C6** | every baseline helper **member** still present somewhere; no member twice within one module under the same namespace | a lost helper; an ODR violation from duplicating a helper |
| **C7** | every referenced data filename exists in that module's `data/`; duplicates byte-identical | a moved case that cannot find its fixture data |

**C2 + C3 + C4 are the core claim:** the text of every test, *and the compilation context that text
sees*, is unchanged. That is what makes verbatim relocation provable.

C3 is load-bearing, not theoretical: nine headers mix platform guards with cases, notably
`TestBaselibDefault5.h` (9 guards / 17 cases) and `TestBaselibDefault.h` (4 guards / **120 cases**).

C5 starts from a verified fact: **all 773 test-case names are globally unique today.**

C6 is checked **per member, not per helper block**. A header split partitions a block — some helpers
leave with the cases that use them, the rest stay — and a whole-block hash cannot tell that from a
deletion. Loss is compared on member text alone, so hoisting a helper into a different namespace
reads as a move; duplication is compared on text *and* enclosing namespace, because the same text
under two namespaces is no ODR risk (`utf_baselib_async` keeps five such pairs deliberately).

### Tier 1A — the Step A gate: near-binary-equivalence

The strongest check available, applying to every header split. `check_split.sh --step-a <module>`
asserts against the object built immediately before the split:

- object size differs by **< 1%** (debug info records file names and line numbers, so not bit-identical, but it cannot move materially);
- `--list_content` on the linked binary is **identical**;
- the full run is identical case-for-case, pass/fail and assertion-for-assertion.

**A green Step A gate means the split is textually and semantically inert** — the claim that would
otherwise have to be taken on trust from a reviewer reading a 10,000-line diff.

### Tier 2 — build and the acceptance gate

`utf_objsize.py` walks `bld/win-x86-*/utests/**/*.obj` and **exits non-zero if any object exceeds the
ceiling** (default 40 MB). The deferral's acceptance criterion, mechanized.

Plus `git ls-files --eol src/utests | grep -v 'i/lf'` must be empty. `core.autocrlf=true` with no
`.gitattributes`, and `git diff --check` does **not** catch a whole-file line-ending rewrite — a real
hazard when creating dozens of new files.

### Tier 3 — runtime equivalence

`utf_runlog.py`:

- `--collect` runs `<binary> --list_content` for the **registered**-case set. *(Verified working: one
  bare name per line, exit 0, no tests run. The `--help` path in `Utf.h:151-158` prints an empty list —
  the master suite is not populated at that point — so `--list_content` is the one to use.)*
- Parses `bld/<plat>/utflogs/*.log` for per-case `Entering`/`Leaving`, pass/fail, the
  `SKIPPED CASES (n):` block (`Utf.h:199-216`), and — with `--report_level=detailed` — **assertions
  passed/total per case**.
- Normalizes away ISO timestamps, absolute paths, source line numbers, `testing time: Nus` and the
  Starting/Completed banners, all of which change legitimately when code moves.
- `--compare` requires the **union across all modules** to match on registered set, executed set,
  pass/fail, SKIPPED set, and assertion count per case.

The assertion-count check catches what static analysis cannot: a case that still registers, still
passes, but silently does less work because it lost a side effect inherited from a sibling case.

**Non-determinism handled honestly:** run the baseline **twice**; any case whose assertion count
differs between the two runs goes to `nondeterministic.json` and is thereafter compared on pass/fail
only. Without this, retry loops and perf cases produce false alarms and the gate gets ignored.

### Gate wiring

`scripts/utests/check_split.sh` runs Tier 1 always, Tier 1A on request, Tier 2 when a build tree
exists, Tier 3 when logs exist. Run before every handback and after every integrated commit. Red
blocks the merge.

---

## 8. Execution: serial or parallel, interchangeable

Both modes consume the same ledger (§9) and the same gates, so **the mode is a runtime choice, not a
design commitment** — switch at any module boundary, or run serially for a while and fan out later.

### Mode S — serial (default; lowest resource use)

One worktree, one module at a time, in the §10 order. Per module: build baseline → Step A → gate →
commit → Step B → gate → commit → update ledger. Needs ~2 GB of build tree and no coordination. This
is the mode to use when resources are tight, when a single agent is doing the work, or when the run
must be paced across many short sessions.

### Mode P — 3 lanes plus an orchestrator

The pattern recorded in
[whole-library-cxx-test-enhancement-implementation-report.md](whole-library-cxx-test-enhancement-implementation-report.md)
(lines 8-23): three worktrees `swblocks-baselib-lane{1,2,3}` on their own branches, each with its own
build tree, plus an orchestrator owning all integration, fast-forwarding handbacks **one at a time**
and rebuilding after each.

Every module split is confined to `src/utests/<module>*/`, so lanes never touch the same file —
provided the two shared files are handled once in the pilot (`scripts/utests/*`, read-only after; and
`generate-eclipse-project-config.py`, converted to a glob). The deferral record is updated once, by
the orchestrator, at the end.

Suggested assignment, balanced by marginal object bytes:

| Lane | Modules | marginal MB |
|---|---|---:|
| 1 | `utf_baselib_messaging`, `utf_baselib_data` | ~121 |
| 2 | `utf_baselib_rest`, `utf_baselib_io`, `utf_baselib_blobtransfer` | ~149 |
| 3 | `utf_baselib_apps`, `utf_baselib_http`, `utf_baselib`, `utf_baselib_tasks` | ~172 |

**Disk budget.** 17 GB free with 4.9 GB already in `bld`. After capturing the baseline, delete the
existing `bld` → ~22 GB. Four worktrees × 158 MB source ≈ 0.6 GB; a focused tree is ~460 MB for two
modules, so a lane stays near 1–2 GB per combo. **Standing rule:** once a module family passes its
gate, archive `bld/<plat>/utflogs` plus the object-size manifest, then delete
`bld/<plat>/utests/<that family>`.

**CPU.** 2 cores. `-j1` per build is mandatory; practical concurrency is ~2 building lanes with the
third editing or validating. Tests may run up to 5 modules concurrently, but port-using modules
serialize on the lock regardless.

**Validation split.** Lanes gate on `ARCH=x86 TOOLCHAIN=vc143 VARIANT=debug` — where the ceiling is
measured. The orchestrator independently verifies each integrated commit on `ccl16` debug, plus
`release` at each checkpoint. The prior refactor found a single-variant lane gate structurally blind
to dialect differences and to `BL_ASSERT`/`BL_VERIFY` compiled out under `NDEBUG`, so **lanes must run
the test target too, not only build.**

---

## 9. Durable state and cold resumption

The work spans far more than one session. **No state may live in a conversation.** Everything needed
to resume lives in the repo and is committed.

### The ledger

`notes/reviews/major/update_2026/test-module-split-ledger.md` — one row per (module, step), the single
place a resuming session looks:

| module | step | status | commit | obj before | obj after | gate | owner | date | notes |
|---|---|---|---|---|---|---|---|---|---|
| `utf_baselib_security` | A | n/a | — | — | — | — | — | — | headers already fine-grained |
| `utf_baselib_security` | B | **done** | `abc1234` | 65.0 | 24/22/21 | green | lane0 | 2026-09-12 | 3-way, zero data duplication |
| `utf_baselib_messaging` | A | **in-progress** | — | 112.7 | — | — | lane1 | — | cut 4576–6914 first |

Statuses: `todo` / `in-progress` / `done` / `blocked` / `n/a`.

### Rules that make resumption safe

1. **Every handback is one commit that updates code *and* its ledger row together.** `git log` is
   then the recovery record; the ledger can never drift from the tree.
2. **Every commit leaves the tree green.** Step A and Step B are each atomic. Never commit a module
   half-split. If a session dies mid-edit, the recovery is `git checkout -- src/utests/<module>*` and
   redo that step — never a partial merge.
3. **Baselines are committed**, not regenerated: `notes/reviews/major/update_2026/baseline/`
   holds `inventory.json`, `objsize.json`, `runlog.json`, `nondeterministic.json`. Every comparison
   everywhere is against these files, so lanes and sessions never drift against each other.
4. **The tree is self-describing.** `check_split.sh` plus `utf_objsize.py` report actual state from
   the tree alone. A resuming session runs them first and reconciles against the ledger before
   trusting it.
5. **`in-progress` older than the current session is suspect.** Verify with the tools, then reset the
   row to `todo` or promote it to `done`.

### The cold-start protocol

A fresh session, with no context beyond the repo:

```
1. Read notes/reviews/major/update_2026/test-module-split-plan.md   (this document)
2. Read ...-ledger.md
3. Run scripts/utests/check_split.sh          -> confirms the tree matches the ledger
4. Run scripts/utests/utf_objsize.py          -> which modules are already under ceiling
5. Pick the first `todo` row in §10 order; do exactly that step; commit code + ledger together
```

No conversation history is required at any point. That is the acceptance test for this section: if a
step needs something only a previous session knew, it belongs in the ledger notes column.

### Web copy

This plan is published as an Artifact so it can be read and picked up from another machine. The
Artifact is a **convenience copy for reading and hand-off**; the committed file and ledger remain
authoritative, and the Artifact is refreshed at each checkpoint rather than edited in place.

---

## 10. Work plan

### Step 0 — baseline, tooling and measurement

0. **Commit this plan** to `notes/reviews/major/update_2026/test-module-split-plan.md` and create the
   empty ledger beside it, so every later step has somewhere to record itself from the first commit
   onward.
1. Capture and **commit** all four baseline artifacts (§9) from the current tree. Capture object sizes
   from the **existing** `bld/win-x86-*-debug` trees **before deleting them** — they are the ground
   truth for the ceiling.
2. Land `scripts/utests/{utf_inventory,utf_runlog,utf_objsize}.py` + `check_split.sh` and the empty
   ledger. Validate Tier 1 by comparing the baseline against itself (clean) and against a
   deliberately corrupted copy (must fail on each of C1–C7 individually).
3. **Per-header object-weight probe.** Because size does not track lines, the split assignment must
   come from measurement. For each header in the ten modules, create a throwaway module dir whose
   `Main.cpp` includes only that header, build `x86/vc143/debug`, record the object size; marginal
   weight = size − 21 MB. Discard the probe dirs. Also probe `utf_baselib_apps` once with
   `UtfBaseLibCommon.h` narrowed, to price the §1.1 lever. Record all of it in the ledger.
4. Confirm `--report_level=detailed` emits per-case assertion counts on `utf_baselib_utils` before
   Tier 3 depends on it; fall back to counting `--log_level=all` assertion lines if not.

### Step 1 — pilot: `utf_baselib_security`

Chosen because it exercises every mechanical hazard at the lowest risk: 9 headers, none above 23% of
the module, **no cross-header coupling**, and five data-consuming headers that force the `data/`
question to be solved properly. Pure Step B — no header splitting at all.

| module | headers | `data/` |
|---|---|---|
| `utf_baselib_security` | `TestHashUtils.h`, `TestHmacSha256.h`, `TestRsaSignVerify.h` | — |
| `utf_baselib_security2` | `TestCryptoUtils.h`, `TestPemKeyFormats.h`, `TestBignumBase64Url.h` | the 11 `test-*.pem` |
| `utf_baselib_security3` | `TestAuthorizationCacheImpl.h`, `TestAuthorizationCacheRestImpl.h`, `TestAuthorizationServiceRest.h` | `*.json`, `response_*.txt` |

This grouping achieves **zero data duplication** (the PEM consumers are all in module 2, the JSON/txt
consumers all in module 3) *and* isolates both lock users into module 3 *and* follows instantiation
weight (OpenSSL vs REST/HTTP helpers). Confirm against the Step 0 probe before committing.

Deliverables: the three modules, a green gate, the eclipse-config glob conversion, and a short written
recipe (file moves, `Main.cpp` template, `data/` handling, what the gate checks) that later work
follows verbatim.

### Step 2 — fan out, two commits per module

Order — easiest first to bank the recipe, hardest last:

1. `utf_baselib_apps` — B only, trivial; isolates the gateway header (the heaviest)
2. `utf_baselib_data` — B only, header by header; `serialized_object.json` follows `TestDataModelDefault.h`
3. `utf_baselib_http` — B only; split on the lock (§6)
4. `utf_baselib` — B only; keep the two sticky clusters (§5.2); carry `using namespace bl;`
5. `utf_baselib_rest` — A (trivial) then B
6. `utf_baselib_blobtransfer` — A at line 292 then B
7. `utf_baselib_messaging` — A at 4576–6914 (zero fixups) then B; pin the cold-cache case (§5.4)
8. `utf_baselib_io` — A at 3493–3844 first, then B; hardest cuts last
9. `utf_baselib_tasks` — **last**; needs the fixture hoist (§5.2) as its own prior commit

**2A gate:** Tier 1 + **Tier 1A near-binary-equivalence**. Object size must *not* move — that is the
point. **2B gate:** Tier 1 + Tier 2 (40 MB ceiling) + Tier 3.

### Step 3 — close the deferral

1. `utf_objsize.py` reports every x86 debug object under 40 MB.
2. Delete the `-gline-tables-only` special case for `BL_USE_CLANG_CL` + `ARCH=x86` + `VARIANT=release`
   in `projects/make/toolchain/msvc-default.mk:325-344`; rebuild that combo (item 4).
3. Test the 32-bit host without a makefile edit (item 5):
   `make -k -j1 utests ARCH=x86 TOOLCHAIN=ccl16 VARIANT=debug CLANG_CL_DIR='<dist>/…/VC/Tools/Llvm/bin'`
4. Update the deferral record's Decision table, the `CLANG_CL_DIR` comment in `msvc-default.mk`, and
   the toolchain notes in `scripts/devenv7/AGENTS.md`.
5. Run the full 12-combo Windows matrix once.

---

## 11. Verification checklist

| | Check | How |
|---|---|---|
| 0 | Every header split is textually inert | Tier 1A: object size within 1%, identical `--list_content`, identical run |
| 1 | No test lost, added or altered | `utf_inventory.py --compare` C1–C7 green |
| 2 | Every case still registers | `--list_content` union == baseline |
| 3 | Every case still runs and passes | `utf_runlog.py --compare`: executed set, pass/fail, SKIPPED set |
| 4 | No case silently weakened | per-case assertion counts equal (modulo `nondeterministic.json`) |
| 5 | Ceiling met | `utf_objsize.py --ceiling 40` exits 0 on x86 vc143 **and** ccl16 debug |
| 6 | No line-ending damage | `git ls-files --eol src/utests` all `i/lf` |
| 7 | Suite not slower | `testutf` wall clock and per-module times vs the ~982 s baseline |
| 8 | Matrix green | full 12-combo Windows build/test run |
| 9 | Debug info restored | x86 ccl16 release builds with `-Zi`, no special case left |
| 10 | Resumable | a cold session can follow §9 and continue with no conversation history |

---

## 12. Files touched

**Created**, per split module: `src/utests/utf_baselib_<name><N>/Utf…<N>Main.cpp`, the relocated
`Test*.h` (via `git mv` where a whole file moves, so history follows), and `data/` where needed.

**Modified:** the original `Main.cpp` (drop moved `#include`s), its `notes.txt` (the `--run_test=`
recipes move with their cases), `scripts/generate-eclipse-project-config.py` (once, pilot), optionally
`settings/vscode/*/*/{tasks,launch}.json`, and at the end `projects/make/toolchain/msvc-default.mk`,
the deferral record, and `scripts/devenv7/AGENTS.md`.

**New:** `scripts/utests/{utf_inventory,utf_runlog,utf_objsize}.py`, `check_split.sh`;
`notes/reviews/major/update_2026/test-module-split-{plan,ledger}.md` and `baseline/*.json`.

**Not modified:** `projects/make/common.mk`, `install.mk`, `python-tests.mk`, `.gitignore` — the
directory wildcard handles new modules.
