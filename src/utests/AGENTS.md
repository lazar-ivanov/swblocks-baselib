# Unit Test Modules

Rules and reference for `src/utests/`. The root [AGENTS.md](../../AGENTS.md) links here; read this
before adding a test case or a test module.

---

## The rule that matters most

**Every test module is a single translation unit, and it must not grow without a bound.**

One `<Module>Main.cpp` defines `UTF_TEST_MODULE`, includes `<utests/baselib/UtfMain.h>`, then
`#include`s every `Test*.h` in its directory. Adding a test case adds to that one TU. Nothing about
the build system caps it.

This has bitten the project once already, and expensively. `utf_baselib_messaging` reached a
**112.7MB** x86 debug object, at which point the 32-bit `clang-cl` host ran out of address space and
died with `0xC000001D` — no diagnostic, just a crash. Two x86 build combinations were unbuildable
until the module was split four ways. See
[the deferral record](../../notes/plans/issues/x86-clang-cl-host-and-test-module-size-deferral.md).

### When you add a test case

1. **Check the module has room.** `scripts/utests/utf_objsize.py` prints every object with its
   headroom. If the module you were going to use is at or near the 40MB target, do not add to it.
2. **Prefer a module comfortably under target.** A case can be added to any module whose fixtures and
   helpers suit it; it does not have to go in the largest one.
3. **Otherwise create a numbered sibling** — see below. This is cheap and is the intended answer, not
   a last resort.

### Size policy

| Tier | Value | Meaning |
|---|---:|---|
| Target | **40 MB** | Aim here. An object above it is reported and needs a recorded reason. |
| Ceiling | **75 MB** | Hard fail on `win-x86-*-debug`. |
| TU floor | **~21 MB** | What an empty test module costs. Subtract it to see your actual content. |

Measured on `x86` `debug`, where the address-space limit is real. The floor is why splitting has a
cost: **every new module pays ~21MB again**, so split when a module is genuinely large, not
reflexively.

### The gate enforces this for you

Every test module prints its headroom as it links, and the build **fails** if an object exceeds the
ceiling on an enforcing platform:

```
Linking utf_baselib_io...
  utf_baselib_io                  72.3 / 75 MB  [##########]  96%  over 40 MB target
```

- `make utests-sizes` prints the whole table, tightest module first, without building.
- Limits live in [object-size-limits.json](object-size-limits.json), per platform pattern.
- **Today only `win-x86-*-debug` enforces.** `win-x86-*-release` reports, everything else is silent —
  see [the rollout plan](../../notes/plans/test-module-size-gate-plan.md). So a module can be over
  budget on a platform you are not building, and you will not hear about it until someone builds it.
- `BL_SKIP_SIZE_GATE=1` silences it locally while experimenting.

**A ceiling breach deletes the linked binary.** The build is configured `.DELETE_ON_ERROR`, so a
failing gate takes the `.exe` with it and you cannot run that module's tests until you are under the
ceiling again — or until you set `BL_SKIP_SIZE_GATE=1` for the run. That is a side effect of where
the check is hooked, not a deliberate punishment.

**Raising a ceiling is not the fix.** Split the module. Changing
[object-size-limits.json](object-size-limits.json) is a deliberate commit of its own, argued in
review, and for `win-x86` it means arguing against a measured toolchain crash.

---

## Creating a new test module

**The build system needs no change.** `projects/make/common.mk` discovers modules by directory
wildcard (`$(wildcard $(SRCDIR)/utests/utf*)`) and globs `*.cpp` inside each. Creating the directory
is sufficient for `make <module>`, `make test_<module>`, `utests`, `testutf`, `install` and `help`.

**Use the numbering scheme.** A module that outgrows itself splits into numbered siblings:
`utf_baselib_messaging` → `utf_baselib_messaging2`, `utf_baselib_messaging3`, and so on. The base
name keeps its original number-free form.

Checklist:

- [ ] `src/utests/utf_baselib_<name><N>/Utf…<N>Main.cpp` — copy an existing one; change
      `#define UTF_TEST_MODULE` and the `#include`s.
- [ ] **`git mv` the headers** that move, so history follows them.
- [ ] **`data/` cannot be shared between modules.** `TestUtils::resolveDataFilePath` resolves
      `<exe-dir>/<exe-stem>-data/`, so a module referencing a data file needs **its own copy**. Group
      data-consuming headers to avoid duplication where you can.
- [ ] **Move the `notes.txt` `--run_test=` recipes** with their cases. A recipe must name a case in
      its own module, and a case that had a recipe must not end up without one (C8 and C9).
- [ ] **If the module's `notes.txt` opens with *"each slice appends the recipes for the cases it
      lands here"***, it is declaring itself a complete index and C9 holds it to that — every case
      in it needs a recipe, including every case you add. Fifteen modules say this today. Elsewhere
      `notes.txt` is a curated list and no case is obliged to appear; 481 of the 1075 cases have no
      recipe and are meant to have none. Dropping the declaration to escape the check is itself a
      C9 failure.
- [ ] Run `scripts/utests/check_split.sh`.

### Things that are not obvious

- **Never `#include` a test header across module directories.** Each module owns its headers. No
  invariant catches a cross-include; it silently duplicates cases into two binaries.
- **Never copy a helper into two headers of the same module.** That is an ODR violation; invariant C6
  catches it.
- **Anonymous namespaces have internal linkage.** A helper in one cannot be shared across a TU
  boundary without being moved into a named namespace first.
- **`using namespace bl;` at module scope is load-bearing** in some modules. Omitting it is a compile
  error, not a silent failure.
- **Watch for cold-start contracts.** `MessagingUtils_TokenTypeConcurrencyTests` deliberately
  exercises a process-global cache while it is still cold; any earlier case in the same process that
  warms it neuters the test **and it still passes**. Grep a header for such comments before splitting
  it.

---

## Verifying a change to the test tree

`scripts/utests/check_split.sh` runs the tiers below. Run it before handing back any change that
moves, adds or removes test cases.

| Tier | Tool | Checks |
|---|---|---|
| 1 | `utf_inventory.py --compare` | C1–C13: no case lost, added or edited; guard and namespace stacks unchanged; no duplicate names; helper members neither lost, invented nor duplicated; data files present, unchanged in content and still referenced; `notes.txt` recipes resolve, no case loses one, and a module declaring its index complete really is; every file on both sides keeps its `#include` list, bar the roster lines a relocation must edit; file-scope text — the fixtures, the column-0 statics, the `BL_IID_DECLARE`s, the behavioural `#define`s — neither lost nor invented; a helper member and a file-scope span each keep the `#if` stack they sit under; a helper member that stayed in its file keeps the namespace it sat in; and all of it over `src/utests/include/` too |
| 2 | `utf_objsize.py --ceiling 75` | No object over the ceiling |
| 3 | `utf_runlog.py --compare` | Registered set, executed set, pass/fail, skips, and **per-case assertion counts** — differentially, **only over the 17 modules the baseline covers**, and **only on the platform the baseline was captured on**; across a mismatch the comparison is refused, not attempted |

`selftest_inventory.py` proves tier 1 actually fails when it should; run it if you change the
extractor.

**A change that legitimately adds something refreshes the baseline, in a commit of its own.** Tier 1
is a relocation gate, so a new case, a new helper member, an added `#include`, an edited data file and
a new file-scope declaration are all reported — none of them is a relocation. The answer is
`utf_inventory.py --capture notes/reviews/major/update_2026/baseline/inventory.json` as a companion
commit, where the manifest diff shows exactly what is now blessed. Refreshing to silence a report you
cannot explain is the one way to make this gate worthless.

**An `inventory.json` conflict is resolved by re-capturing with the integrated tool, never by taking
one side.** Taking a side is how a stale manifest reaches the baseline: a lane branched before an
invariant landed re-captures with its own older tool, the manifest loses the keys that invariant
reads, and tier 1 goes on printing PASS with only a note to say it is now judging nothing. Merge or
rebase first, then `--capture` once, from the integrated tree. **No such downgrade has happened
here** — every baseline commit before `adc00c8` carries four top-level keys, so nothing was there to
drop — but `s6r3-1` carries its own baseline commit `b814ed9`, written with an older tool and never
merged, and a re-capture from it would write four keys over six. `--capture` therefore **refuses** to
overwrite a baseline whose keys are a strict superset of the ones it writes, and exits 4. It compares
**two levels** — the top-level keys, and the key set of each list's entries and of a module — because
a check's key is not always a top-level one: the `#if` stack below is a field on a *member*, so a
pre-guard tool writes the same six top-level keys. Two is as deep as it can go: below that the
manifest is keyed by data (`modules` by name, `data_files` by filename), so a deeper rule would
refuse the ordinary refresh that removes a module or a data file. That guard lives in the tool doing
the writing, so it can only stop an older tool built from that commit onward. The rule is what covers
the rest.

**C11 is not in force until that refresh happens.** No baseline captured before it carries
`file_members`, and a hard failure there would red the gate for everyone rather than for the change
that earned it, so every run prints which state it is in. The refresh that arms it is the ordinary
one above.

**A helper carried out from under its `#if` is a guard change, not a move — and C6 and C11 now say
so.** For thirteen invariants nothing read a preprocessor condition enclosing anything but a test
case: C11 dropped conditionals because "conditionals are C3's", and C3 speaks for cases only. 18
helper members and 11 file-scope spans sit under one, `Utf.h`'s `#if defined( UTF_TEST_MODULE )` —
the condition gating `main( )` — among them. Measured before this: `namedMutexSemaphoreKey( )` cut
out from under `#if ! defined( _WIN32 )` into a sibling header **with no guard**, on
`split_members( )`'s own extent, which is how every split here is cut, **passed tier 1 green** — a
relocation accident that silently changes what compiles on which platform. So the condition stack
joins C6's and C11's identity, and a text that survives under a different stack reports as a guard
change rather than as a loss. **The condition is compared as written**: `#if ! defined( X )`
re-spelled as `#ifndef X` reports, because a re-spelling is an edit and a normaliser sound enough to
be trusted would have to be an expression parser. **It is not in force until the baseline is
refreshed**, and every run prints which state it is in, exactly as C11 and C13 do. A split that
moves a guarded helper **with** its guard stays silent, which is the point — and for that to be true
C6's *duplication* half no longer asks about a member which is nothing but `#if`, `#else` and
`#endif`. Three members tree-wide have that shape; they declare nothing, so two copies are no ODR
risk, and they repeat across sibling headers on any ordinary split. C6's no-loss half and the guard
half still judge them.

**Tier 3 is the one that catches a case which still registers and still passes while silently doing
less work.** Do not skip it for a change that moves cases between modules.

**Tier 1 scans `src/utests/include/` as well as `src/utests/utf*/`.** That shared tree — 27 files,
15,373 lines, included by 181 of the 185 module files, and where `Utf.h`, `UtfMain.h` and the shared
fixtures live — used to be read by no invariant at all: editing a shared fixture there, or
redefining `UTF_AUTO_TEST_CASE` itself, passed tier 1 green. Its files are now judged by C6, C10,
C11 and C12 exactly as a module's are. It is **not** a module — it has no `data/` directory and can
have none — so C7, C8 and C9 never ask it anything. **C13 is not in force until the baseline is
refreshed**, and every run prints which state it is in, exactly as C11 does.

**Moving heavy helper bodies out of line is not a relocation, and tier 1 now says so.** Reduction
option 3 below rewrites a helper's declaration and writes its body into a new `…Impl.cpp` in the
shared tree. Replaying the real four-way `f992e2f` split reports eleven such lines — one added
`#include` in a shared header and ten helper members — where it used to report three. Each names a
real change to a file every module compiles; the answer is the companion refresh, where the manifest
diff shows exactly what is blessed.

**Tier 3's baseline covers 17 of the tree's 45 test binaries, and none of the http or h2 client
ones.** It is also a `win-x86-vc143-debug` capture (`91d5c2c`), so it is a statement about one
platform as well as about those modules: run against a Linux tree it reported 37 assertion-count
differences inside its *own* 17 modules which are nothing but Windows-versus-POSIX
(`BaseLib_OSJunctionsTests` 18 → 0, `BaseLib_OSRegistryValueTest` 8 → 0, Windows argv quoting
81 → 0). Every comparison therefore ends with a coverage statement naming the modules it could not
speak about, and `baseline/uncovered.json` gives the reason for each — read that before taking a
green tier 3 for coverage. The 17 client modules were measured for admission and **refused**;
`notes/plans/issues/tier3-client-modules-not-baselineable-record.md` has the numbers and what would
reverse it.

**That cross-platform comparison is now refused rather than attempted.** A capture records the
platform it was taken on — the name of the build tree it read, so pass `--bld` even with
`--parse-logs` — and tier 3 compares only when the baseline's platform matches the tree it is
handed. Otherwise it reports neither a PASS nor a FAIL but a **SKIP naming both platforms**, because
a FAIL would red the gate for everyone who is not on the baseline's platform and a PASS would claim
a check that never ran. **A baseline with no platform stamp is refused the same way and is not in
force until it is refreshed**, exactly as C11 and C13 are, and the summary line says which state it
is in. The answer to either is `utf_runlog.py --run --bld <tree> --capture …` on the platform you
need to gate, twice, because the unstable list is derived from a pair of passes.

**`baseline/nondeterministic.json` is stamped the same way, and it is the reason the refresh is two
captures.** It is `{ "__platform__": …, "unstable": [ … ] }`, stamped with the platform the two runs
it was derived from agree on, and a comparison refuses a list from anywhere else on the same path
with the same SKIP. It is the other input to a comparison and was the last one nothing checked:
every case it names is thereafter compared on outcome alone, so a list carried across a platform
excuses the wrong cases, silently and for as long as it stays committed. **A bare array is an
unstamped list** — which is what every list derived before the stamp existed is — and is refused
like an unstamped baseline, because unknown is not a match.

**`utf_runlog.py --nondeterministic <pass1> <pass2> --capture …` produces a starting point, not the
list: merge it into the one already there, and never let it replace one.** Two passes classify every
case that agrees with itself as deterministic, which is sound for a case that varies on most runs
and unsound for one whose variation is rare —
`notes/plans/issues/utf-runlog-nondeterministic-sampling-record.md` measures one misclassified about
a third of the time. So the committed list is a **curated superset**: of its eleven names ten
re-derive from the two committed passes and
`IO_SimpleConnectAndTransmitDataMessageDispatcherOutgoingTests` does not. That one was added by hand
at `77ef537`, which flagged it at 8199 assertions against a baseline of 8194 and then drew 8199 and
8194 from the *unchanged* binary — the two baseline passes had simply been unlucky in agreeing. A
name recorded from observation is evidence the pair does not carry, and a refresh that overwrites
the file drops it with nothing to show it happened. **Nothing in the file marks which names came
from observation rather than from a pair**, so read the history of any name you are about to remove.

**A differential comparison can only speak about things present on both sides.** Tiers 1 and 3
compare against a baseline, so anything *new* — a module, a case — is unjudgeable by construction,
and its failures are compared against nothing. Ask of any comparison you rely on: *what can this
never report?* Read the raw artifacts too — module exit codes, verdict lines, the `clean` and
`failures` fields — because a tool reporting "clean" is a claim, not a fact.

---

## Writing a test that is worth its green

These four rules were each paid for. Ignoring one costs a defect that survives review.

**Compose the real components early.** A stub faithful to a published interface is not evidence that
two components agree. Stubs encode assumptions the real thing may not honour — one probe published
its state before answering its sinks and asserted in a comment that this was "the order a driver
produces too"; the real driver did the opposite. A whole layer of defects has been found by the
first case that put real components together, including one the design had *already named* as a
consequence and which still passed three green modules and two review passes. If a slice composes
things for the first time, write that case **before** building out the surface, and treat a failure
there as the exercise working.

**A negative control is the evidence; the run count is not.** Show the case failing against the
unfixed code. Where a harness refuses a degradation, build the discrimination into the assertion
instead. Sixty green runs after a 1-in-16 flake happen by luck about 2% of the time — what earns a
fix is a control that makes the failure *certain*, shown red before and green after.

**Reproduce a flake under load, not idle.** A concurrent compile stalls one thread for hundreds of
milliseconds; CPU, memory and I/O churn press evenly on all of them and do not reproduce the same
races. One flake here survived 240 idle runs under synthetic load and reproduced during an ordinary
parallel build.

**A clean TSan result means nothing without a positive control.** "Clean" and "not instrumented" look
identical. Build a module with a known report in the same instrumented tree and show it fires — and
never claim "no ThreadSanitizer line" from a build that was not instrumented.

**Wait for the thing the assertion is about.** A wait whose predicate differs from the assertion's is
a flake waiting: a wait for "two records" was satisfied by the peer's own *connected* record, so a
case asserted on a frame before it existed. Never poll or sleep before an assertion — a poll loop
before an assertion is a missing rendezvous, and the fix belongs in the harness, not the case.

---

## Reducing a module that is already too large

In order of preference.

1. **Move whole headers to a numbered sibling.** Cheapest and safest; only files move.
2. **Split a header, then move it** — as two separate commits. Step A cuts a block verbatim into a
   sibling header included from the *same* `Main.cpp`: the preprocessed TU is unchanged, so the
   object size must not move (this is a strong, mechanical gate — a real split measured +0.003%).
   Step B then moves the whole file, which is case 1.
3. **Move heavy helper bodies out of line.** For a `T< E = void >` helper in
   `src/utests/include/utests/baselib/`, move big member bodies into a sibling `…Impl.cpp` with an
   explicit instantiation, and give each consuming module a thin forwarding `.cpp` that
   `#include`s it. Keep the fake template — that idiom is what keeps baselib header-only.

**Know when (3) pays.** It *relocates* weight, it does not reduce it. It pays only when several
modules would each otherwise instantiate the same thing: it worked for `TestMessagingUtils` across
four modules, and would do nothing for a helper only one module uses.

**Splitting cannot fix everything.** A single case cannot straddle two executables, so one heavy
case sets a hard floor. `utf_baselib_messaging3` is 68.35MB of *one* inline helper,
`messageProcessingRoundTrip`. Reducing that needs
[instantiation weight work inside baselib](../../notes/plans/issues/test-instantiation-weight-deferral.md),
not more splitting.

**Do not predict a grouping from isolated per-header measurements** — they were off by 40× once.
Build the grouping and measure it, or use leave-one-out. What decides splittability is how much
every case instantiates *in common*, measured as `before − (a + b)`.

---

## The x86 release caveat

**Object size does not govern `x86` + `ccl16` + `release`.** That path is limited by peak memory in
the *optimizer*, and it is the one combination that still cannot afford full `-Zi` — see
`BL_MINIMAL_DEBUG_INFO` in `projects/make/toolchain/msvc-default.mk`. Its release object is
*smaller* than its debug object, so passing the 75MB ceiling tells you nothing about it. The only
check is whether that combination builds.

x86 targets are built by the x86-hosted tools on every host, so those ~2GB limits are real on every
machine, not just 32-bit ones.
