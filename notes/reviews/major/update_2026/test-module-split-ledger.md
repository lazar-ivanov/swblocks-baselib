# Test Module Split: Ledger

**This file is the resume point.** A session with no memory of any earlier session reads
[test-module-split-plan.md](test-module-split-plan.md), then this file, then continues.

Companion to the plan; see its §9 for the rules this ledger operates under. In short:

1. Every handback is **one commit that updates the code and its ledger row together**, so `git log`
   is the recovery record and this file can never drift from the tree.
2. **Every commit leaves the tree green.** Step A and Step B are each atomic — a module is never
   left half-split. Recovery from a dead session is `git checkout -- src/utests/<module>*` and redo
   that step, never a partial merge.
3. **Verify before trusting.** Run `scripts/utests/check_split.sh` and reconcile against this file
   before taking the next row. A row marked `in-progress` by a session that is no longer running is
   suspect: re-verify, then reset it to `todo` or promote it to `done`.

---

## Cold-start protocol

```
1  read  notes/reviews/major/update_2026/test-module-split-plan.md
2  read  notes/reviews/major/update_2026/test-module-split-ledger.md      (this file)
3  run   scripts/utests/check_split.sh                 does the tree match the ledger?
4  run   scripts/utests/utf_objsize.py                 which modules are already under ceiling?
5  take  the first `todo` row in plan order; commit code + ledger together
```

**Interpreter note.** The tooling is stdlib-only and `check_split.sh` resolves an interpreter on its
own. On this Windows box the repo `.venv` is **broken** — it points at a scratch directory from an
old session that no longer exists — so the script falls back to the devenv7 dist interpreter at
`~/swblocks/dist-devenv7-*/python/*/default/python.exe`. Set `UTF_PYTHON` to override.

---

## Status

Legend: `todo` · `in-progress` · `done` · `blocked` · `n/a`

### Step 0 — baseline, tooling and measurement

| # | Item | Status | Commit | Notes |
|---|---|---|---|---|
| 0.0 | Plan + ledger committed under `update_2026/` | **done** | `36ec522` | this file and the plan beside it |
| 0.1 | `utf_inventory.py` + invariants C1–C7 | **done** | `36ec522` | 772 cases, 115 helper blocks, 19 modules |
| 0.1a | `selftest_inventory.py` — prove each invariant fires | **done** | `36ec522` | 11 corruptions, all caught |
| 0.2 | `utf_objsize.py` + ceiling gate | **done** | `36ec522` | gates `win-x86-*-debug` only |
| 0.3 | `utf_runlog.py` + runtime comparator | **done** | `36ec522` | `--list_content`, Entering/Leaving, SKIPPED, assertion counts |
| 0.4 | `check_split.sh` — the single gate | **done** | `36ec522` | tiers skip cleanly when inputs are absent |
| 0.5 | Baseline: source inventory | **done** | `36ec522` | `baseline/inventory.json` |
| 0.6 | Baseline: object sizes | **done** | `36ec522` | `baseline/objsize.json` — captured **before** any tree deletion |
| 0.7 | Confirm `--report_level=detailed` emits per-case assertion counts | **done** | — | verified on `utf_baselib_utils`: 51 / 22 / 9 |
| 0.8 | Baseline: runtime pass 1 | **done** | _this commit_ | 741 registered, 741 ran, all passed, 645,156 assertions; promoted to `runlog.json` |
| 0.9 | Baseline: runtime pass 2 | **done** | _this commit_ | independent second run, same 741/741 |
| 0.10 | Derive `baseline/nondeterministic.json` | **done** | _this commit_ | **10 of 741** cases have unstable counts |
| 0.11 | Promote pass 1 to `baseline/runlog.json` | **done** | _this commit_ | `runlog.json` **is** pass 1; re-derive 0.10 from it and `runlog-pass2.json` |
| 0.12 | Per-header object-weight probe | **todo** | — | **the expensive one** — see below |
| 0.13 | Probe `utf_baselib_apps` with `UtfBaseLibCommon.h` narrowed | **todo** | — | prices the adjunct lever, plan §1.1 |

### Step 1 — pilot

| Module | Step | Status | Commit | Obj before | Obj after | Gate | Notes |
|---|---|---|---|---|---|---|---|
| `utf_baselib_security` | A | **n/a** | — | — | — | — | headers already fine-grained; pure Step B |
| `utf_baselib_security` | B | **done** | _this commit_ | 65.0 | **22.7** | green | `TestHashUtils`, `TestHmacSha256`, `TestRsaSignVerify`; **no data dir**, no lock |
| `utf_baselib_security2` | B | **done** | _this commit_ | — | **38.2** | green | crypto + PEM; 11 `.pem` fixtures; no lock |
| `utf_baselib_security3` | B | **done** | _this commit_ | — | **52.4** | tier2 over | auth cache + service; 8 JSON/txt; holds the lock; see below |

**Result: 65.0 MB in one module becomes 22.7 / 38.2 / 52.4 across three.** Max down 19%, two of three
under the 40 MB ceiling, and the machine-global lock confined to one module so the other two run free
of it. Verification: tier 1 green (772 cases, C1–C7), tier 3 green — **51 cases before in one module,
51 after across three, identical outcomes and assertion counts**.

`utf_baselib_security3` remains above the ceiling and **cannot be brought under it by moving files**.
A fourth module was tried and reverted; see the measurements below.

**A 3-way split was not enough.** The first cut gave 22.7 / 38.2 / **52.4** — the third still over the
40 MB ceiling. Throwaway probe modules settled why, and overturned the working hypothesis:

| Probe | Object | Marginal over the 21.4 MB floor |
|---|---:|---:|
| `UtfMain.h` only (`utf_baselib_setprio`, 223 lines) | 21.4 MB | — |
| `UtfMain.h` + `HttpServerHelpers.h` + one trivial case | 21.9 MB | **0.5 MB** |
| `TestAuthorizationServiceRest.h` alone | 37.7 MB | **16.3 MB** |

**The 40 MB ceiling is not reachable for every module, and this one proves it.** The full probe set,
x86 vc143 debug:

| probe | object | marginal over the 21.4 MB floor |
|---|---:|---:|
| floor — `UtfMain.h` only (`utf_baselib_setprio`, 223 lines) | 21.4 MB | — |
| + **including** `HttpServerHelpers.h` | 21.9 MB | 0.5 MB |
| + **including** `TestAuthorizationCacheImplUtils.h` | 21.9 MB | 0.5 MB |
| **using** it — `TestAuthorizationCacheImpl.h` alone | 47.3 MB | **25.9 MB** |
| **using** it — `TestAuthorizationCacheRestImpl.h` alone | 49.4 MB | **28.0 MB** |
| `TestAuthorizationServiceRest.h` alone | 37.7 MB | 16.3 MB |

Including a template helper costs nothing; **instantiating** it costs ~26 MB. Both auth cache headers
independently force the same `AuthorizationCache` instantiations, which is why either alone is 47-49
MB while both together are only 52. A 40 MB ceiling allows ~19 MB of marginal content, so **any
module containing even one authorization-cache test case is over the ceiling no matter how the files
are arranged.**

That is a hard limit on what a file-move split can deliver, and it needs a decision rather than more
splitting. The options are to reduce the instantiation weight itself (explicit instantiation in a
translation unit, `extern template`, or less template depth in the cache stack), to raise the ceiling,
or to record these modules as accepted exceptions. **The plan's §10 item 0.12 and its 40 MB target
both need amending in light of this.**

**Marginal object costs are NOT additive — they overlap, heavily.** Splitting
`TestAuthorizationServiceRest.h` (16.3 MB standalone) out of `security3` reduced `security3` by
**0.4 MB**, from 52.4 to 52.0. Nearly all of that header's weight is template instantiation it shares
with `TestAuthorizationCacheRestImpl.h` — both drive `AuthorizationServiceRest` and the same REST and
data-model templates — so removing the header does not remove the instantiations, which the remaining
header still requires.

**This changes how item 0.12 must be run.** Probing each header in its own module measures an *upper
bound on that header's standalone cost*, not its marginal contribution to a group, and the two differ
by more than an order of magnitude here (16.3 MB vs 0.4 MB). A split assignment cannot be
bin-packed from isolated probe results. The measurement that actually predicts the outcome is the
**leave-one-out delta**: build the module with a header removed and take the difference. Isolated
probes remain useful for ranking candidates cheaply, but the chosen grouping must be built and
measured before it is believed.

**`HttpServerHelpers.h` is effectively free**, because `UtfMain.h` already pulls
`baselib/http/SimpleHttpTask.h`, `tasks/AsioSslStreamWrapper.h`, `crypto/TrustedRoots.h` and the
thread pools — the http and tasks machinery is *inside* the floor. So the weight of these modules is
in the test headers themselves, not in a shared-helper tax, which is the good case: it splits by
moving files. `TestAuthorizationServiceRest.h` alone is 16.3 MB of the 31 MB `security3` carried,
so separating it is what lets both halves fit.

### Step 2 — fan out, serial mode

**Mode S is in force** (plan §8): one module at a time, one worktree, no lanes. Chosen deliberately
so each module can be evaluated for new surprises before the next begins — the pilot produced four,
and there is no reason to assume the harder modules produce none.

Against the **55MB** ceiling, `utf_baselib_data` at 49.8MB needs no work at all, and three modules
need only a single header moved out. Eight modules remain.

#### The per-module recipe

Settled by the pilot. Run it in this order for every module; do not skip step 5 to save time, it is
the step that costs nothing and catches the most.

```
1  confirm the module is built and note its object size
2  choose the grouping - by data fixtures, by lock usage, then by cohesion
      if the grouping is not obvious, probe LEAVE-ONE-OUT, never in isolation
3  git mv the headers; write <Module>NMain.cpp; split data/ (COPY shared files); split notes.txt
4  rm -rf bld/<plat>/utests/<family>*/*-data        the copy rule never prunes
5  scripts/utests/check_split.sh --tier1            MUST be green before building
6  mk -k -j1 <family...> ARCH=x86 TOOLCHAIN=vc143 VARIANT=debug
7  scripts/utests/utf_objsize.py --ceiling 55       if over, return to 2
8  utf_runlog.py --run --bld <tree> --only <family...> --capture t.json
   utf_runlog.py --against t.json --compare baseline/runlog.json
                 --nondet baseline/nondeterministic.json --family <prefix>
9  update this ledger's row and commit code + ledger in ONE commit
```

#### Order and status

Easiest first to bank the recipe; each phase only begins when the previous is green.

**Phase 2A — pure file moves, no prerequisites.** These need no header splitting at all.

| # | Module | Step | Status | Obj before | Obj after | Notes |
|---|---|---|---|---:|---:|---|
| 1 | `utf_baselib_http` | B | **done** | 55.8 | **54.9 / 25.0** | 2 modules; `http2` is lock-free. **Target not reachable** — see below |
| 2 | `utf_baselib` | B | **done** | 55.3 | **49.4 / 30.5** | 6 non-umbrella headers out; both under the ceiling with real margin |
| 3 | `utf_baselib_apps` | B | **done** | 77.2 | **45.8 / 65.4** | split on the application boundary. **apps2 is OVER the ceiling and unsplittable** — see below |

**`utf_baselib_http` result.** 55.8 MB becomes **54.9 + 25.0** across two modules. `utf_baselib_http2`
(TLS policy, peer verification, stream wrapper — 19 cases) is entirely free of the machine-global
lock and runs in parallel; `utf_baselib_http` keeps the three server-test headers which all reach
`HttpServerHelpers.h`. Tier 1 and tier 3 both green: 56 cases before, 56 after (37 + 19), identical
outcomes and assertion counts.

**The 40 MB target is not reachable here, and a third module was tried and reverted.** Splitting
`TestClientHttpTasks.h` (12 cases) out moved `utf_baselib_http` by **2.5 MB** (54.9 → 52.4) and
produced a **51.1 MB** object — leaving two modules near the ceiling instead of one, for no real
gain. The cause is the same as the security pilot's: including `HttpServerHelpers.h` costs 0.5 MB,
but standing up a server through it costs roughly **30 MB** of instantiation that every server case
in the module shares.

**This is now a confirmed pattern, not a one-off.** Two independent helper stacks — the authorization
cache and `HttpServerHelpers` — behave identically. Expect the same of `TestMessagingUtils.h`,
`TestBlobTransferUtils.h`, `TestTaskUtils.h` and `TestRestUtils.h`, which back the three largest
modules in the tree. **Measure each before planning its split**; the module count cannot be predicted
when most of the weight is shared. See
[../../../plans/issues/test-instantiation-weight-deferral.md](../../../plans/issues/test-instantiation-weight-deferral.md).

**Watch item:** `utf_baselib_http` sits 0.1 MB under the ceiling and is the first module that will go
red when anyone adds an HTTP test case.

**`utf_baselib` result.** 55.3 MB becomes **49.4 + 30.5**. The six headers moved to `utf_baselib2`
— time zone, date/time validation, transaction, net utils and the two Boost.Asio ones, 39 cases —
are exactly those reaching *neither* `UtfBaseLibCommon.h` (the messaging/http/tasks/data umbrella)
nor the `examples/objmodel` fixtures. Tier 1 and tier 3 green: 218 cases before, 218 after.

**This is the first split where weight moved proportionately**, and it establishes the predictor for
the rest of the work:

| Module | Before | After | Shed | Moved headers share a helper stack with those left? |
|---|---:|---:|---:|---|
| `utf_baselib` | 55.3 | 49.4 | **5.9** | no |
| `utf_baselib_http` | 55.8 | 54.9 | 0.9 | yes — `HttpServerHelpers` |
| `utf_baselib_security3` | 52.4 | 52.0 | 0.4 | yes — authorization cache |

**Group by shared helper stack, not by size or line count.** Headers that share one do not separate;
headers that do not, do.

**`utf_baselib_apps` result, and the first module which cannot reach the ceiling.** 77.2 MB becomes
**45.8 + 65.4**, split on the application boundary: the two `bl-tool` headers stay, the one
`bl-messaging-http-gateway` header leaves. Tier 1 and tier 3 green, 13 cases before and after.

`utf_baselib_apps` at 45.8 MB is compliant. **`utf_baselib_apps2` at 65.4 MB is not, and no file
move can fix it: it is one module, one header, one test case.**
`MessagingApps_HttpGatewayTlsValidationTests` is 165 lines of test source which instantiates the
entire messaging HTTP gateway application, and that costs **44 MB**. The split was kept regardless,
because it moves the family maximum from 77.2 to 65.4 — an improvement, though not compliance.

**The plan's §1.1 adjunct lever is refuted and should not be revisited.** It hypothesised that
`UtfBaseLibCommon.h` — the umbrella pulling the whole messaging, http, tasks and data stack, included
by 20 test headers — was driving object sizes. Measured: **0.1 MB**. Every include probed so far sits
within 1.2 MB of the bare floor, while *using* what they declare costs 26 to 44 MB. Narrowing
includes is worthless here; only reducing instantiation weight helps.

**Phase 2B — Step A then Step B.** Single-header modules, so a header split must come first. Two
commits each: A is gated on near-binary-equivalence, B on the ceiling.

| # | Module | Step | Status | Obj before | Obj after | Notes |
|---|---|---|---|---:|---:|---|
| 4 | `utf_baselib_blobtransfer` | A | **done** | 56.3 | 56.27 (inert) | cut at line 282 — structural seam *and* lock boundary |
| 4 | `utf_baselib_blobtransfer` | B | **done** | — | **52.1 / 54.4** | both under the ceiling; single module was over |
| 5 | `utf_baselib_rest` | A | **BLOCKED** | 78.7 | — | **not attempted** — helper floor alone is 60.5 MB, above the ceiling |
| 5 | `utf_baselib_rest` | B | **BLOCKED** | — | — | blocked on the instantiation-weight deferral, item 2 |
| 6 | `utf_baselib_io` | A | **done** | 77.4 | 77.37 (inert) | cut at 6167; header now 6,166 + 1,348 lines |
| 6 | `utf_baselib_io` | B | **BLOCKED** | — | — | attempted: 72.3 + 56.9, **both over the ceiling**; reverted |

**Phase 2C — the hard two.** Do not start before 2A and 2B are green.

| # | Module | Step | Status | Obj before | Obj after | Notes |
|---|---|---|---|---:|---:|---|
| 7 | `utf_baselib_messaging` | A | **todo** | 112.7 | — | cut 4576-6914 (verbatim, zero fixups); **pin the cold-cache case**, plan §5.4 |
| 7 | `utf_baselib_messaging` | B | **todo** | — | — | likely needs 3+ modules; measure, do not predict |
| 8 | `utf_baselib_tasks` | hoist | **todo** | 67.7 | — | **prerequisite, own commit** — `TestTasks.h` fixtures to `src/utests/include/utests/baselib/` |
| 8 | `utf_baselib_tasks` | B | **todo** | — | — | only after the hoist |

| — | `utf_baselib_data` | — | **n/a** | 49.8 | 49.8 | already under the 55MB ceiling; no work |

#### Standing cautions

- **Never predict a grouping from isolated per-header measurements.** They were off by 40x in the
  pilot. Leave-one-out, or build the grouping and measure it.
- **Do not cross-include a header between modules.** No invariant catches it (see the C5 note below).
- **Delete the build data directories** for the family before tier 3, every time.
- **Re-verify on ccl16** at the end of each phase; the pilot only rebuilt vc143, so the ccl16 tree
  still holds pre-split `utf_baselib_security` objects.

### Step 3 — close the deferral

| # | Item | Status | Commit | Notes |
|---|---|---|---|---|
| 3.1 | Every x86 debug object under 40 MB | **todo** | — | `utf_objsize.py --ceiling 40` exits 0 |
| 3.2 | Restore `-Zi` for x86 ccl16 release | **todo** | — | deferral item 4 |
| 3.3 | Retest the 32-bit clang-cl host | **todo** | — | deferral item 5 |
| 3.4 | Update deferral record + `msvc-default.mk` + devenv7 AGENTS.md | **todo** | — | |
| 3.5 | Full 12-combo Windows matrix | **todo** | — | |

---

## Measured baseline

Captured 2026-09-12 from the build trees then on disk, **before** any of them is deleted. These are
the numbers every later comparison is made against.

**Source inventory** — 772 test cases, 115 helper blocks, 19 module directories, all case names
globally unique. Note 772 is the count in *source*; a single platform builds fewer (the x86 Windows
run executes 741) because some cases sit inside platform guards. Comparisons are always
before-vs-after on the same platform, so this is not a discrepancy.

**Runtime, `win-x86-vc143-debug`** — two independent full passes, 17 modules (`utf_baselib_plugin`
is a shared library and has no binary). Both: **741 registered, 741 ran, every case passed**, 36
skipped, **645,156 assertions**. Registered equals ran in every module, and no case was missing an
assertion count — the parser covered the whole tree.

Only **10 of 741** cases have assertion counts that disagree between the two passes, all
cancel/reactive/timing-sensitive: `AsyncCB_CancelTests`, `AsyncV2_CancelTests`,
`BaseLib_Base64UrlTests`, `BlobTransfer_FilesPackagerInMemoryCancelUploadTests`,
`Tasks_ExternalCompletionTaskTests`, `Tasks_ReactiveDisconnectObservableTests`,
`Tasks_ReactiveDisconnectObserverTests`, `Tasks_ReactiveTestsWithException`,
`TestDataChunkStorageFilesystemMultiFiles`, `TestDataChunkStorageFilesystemSingleFile`.

So **731 cases carry a full assertion-count comparison**. Note the two heaviest cases,
`AsyncCB_SmallPerfTests` (368,642 assertions) and `BaseLib_SortedVectorHelperTests` (160,602), are
*stable* and therefore fully compared.

**Object sizes, x86 debug** — 20 objects over the 40 MB ceiling across the two toolchains:

| Module | vc143 | ccl16 |
|---|---:|---:|
| `utf_baselib_messaging` | 112.7 | 110.3 |
| `utf_baselib_rest` | 78.7 | 78.1 |
| `utf_baselib_io` | 77.4 | 79.4 |
| `utf_baselib_apps` | 77.2 | 77.0 |
| `utf_baselib_tasks` | 67.7 | 67.9 |
| `utf_baselib_security` | 65.0 | 63.9 |
| `utf_baselib_blobtransfer` | 56.3 | 57.4 |
| `utf_baselib_http` | 55.8 | 56.5 |
| `utf_baselib` | 55.3 | 55.9 |
| `utf_baselib_data` | 49.8 | 47.0 |

The ~21 MB per-TU floor is visible in `utf_baselib_setprio` at 21.4 MB for 223 lines of source.

---

## Notes for whoever picks this up

- **C3 deliberately ignores include guards.** A first cut counted them, and 77 of the 113 "guarded"
  cases turned out to be sitting under an `#ifndef __UTEST_*_H_` include guard rather than any real
  conditional. That would have fired C3 every time a case moved into a new header written without
  one — a false alarm that erodes trust in the gate faster than any missed defect. `is_include_guard`
  in `utf_inventory.py` recognises the `#ifndef FOO` / `#define FOO` shape and drops that frame.
  **Do not "fix" this back.** After the filter, C3 covers 37 cases under genuine conditionals:
  28 platform guards, 6 library-version guards, 1 devenv-version guard.
- **Verified preconditions.** Zero cases sit inside any namespace, confirming that no anonymous
  namespace wraps a test case; 156 cases carry a doc comment that C2 now hashes; and the column-0
  namespace detection reproduces the hand analysis exactly (`TestIO.h` 4 blocks, `TestMessagingDefault.h`
  5 including both `utest` blocks, `TestRestDefault.h` 1, `TestBlobTransferFilesystem.h` 7).
- **Test headers are not self-contained: they lean on the include closure of whatever was included
  before them.** Moving `TestTimeZoneData.h` out of `utf_baselib` failed to compile on
  `utest::measureRuntime`, which lives in `utests/baselib/TestUtils.h`. The header never included it
  — it worked only because `TestBaselibDefault.h` came first in `UtfBaselibMain.cpp` and pulled it
  transitively through `UtfBaseLibCommon.h`. The repo already knows about this weakness; the comment
  at the top of `utf_baselib/TestPublicHeaderInstantiation.cpp` describes exactly this property.

  **A static independence check cannot find these.** Looking for sibling `#include`s and shared
  symbols — which is what was done before the move, and which passed — does not see a declaration
  arriving transitively. **The build step in the recipe is what catches them, which is why it comes
  before tier 3 and must not be skipped.**

  The fix is to add the missing include to the moved header. That is safe for the gate: C2 hashes
  case bodies and doc comments, not include lines, so the proof of faithful relocation still holds.
  It also leaves the header self-contained rather than carrying the trap to the next move.
- **C5 is file-based, so cross-including a header between modules evades every invariant.** A module
  whose `Main.cpp` does `#include "../other_module/TestFoo.h"` registers that header's cases in a
  second binary, but the file is scanned once so no duplicate name is seen; tier 3 compares unions,
  so it does not see it either. **Step B must move files, never cross-include them.** Verified
  accidentally: a probe module written that way tripped no invariant.
- **The build's test-data copy never prunes, so a clean bld is required to trust a split.**
  `projects/make/common.mk:503-508` copies `src/utests/<module>/data/*` into
  `bld/<plat>/utests/<module>/<binary-stem>-data/` with `cp -r` and never removes anything, and the
  whole rule is skipped when the source `data/` directory does not exist. So after moving data files
  out of a module, the old files remain in that module's build data directory and its tests keep
  passing against them. **A missing data file would look green locally and only fail on a clean
  build** — exactly the failure mode this work exists to prevent. Delete
  `bld/<plat>/utests/<module>/*-data` for every module a split touches before running tier 3.
- **`utf_baselib_plugin` is a shared library, not a test executable.** It has no `Main.cpp`, reports
  0 cases, and `utf_runlog.py` skips it because no matching `.exe` exists. That is correct, not a
  gap.
- **Assertion counts need `--report_level=detailed`, which `make test_*` does not pass.** The make
  harness uses the `UTF_FLAGS` set in `common.mk`, so logs it produces carry no per-case counts.
  `utf_runlog.py --run` executes the binaries directly with the right flags and is the reliable path
  for a full-strength tier 3. Comparing against make-produced logs still works and still checks the
  executed set, outcomes and skips — it just prints how many cases it compared *without* counts, so
  a weaker check never passes for a strong one.
- **Two false-alarm sources were found and fixed while validating the comparator**, both of the kind
  that would have got the gate switched off: a snapshot parsed from logs has no registered set, which
  once reported all 741 cases as "registration lost"; and a missing assertion count compared against a
  real one reported every case as changed. Both now require *both* sides to have measured the thing
  before comparing it.
- **The comparator is validated against two real runs, not a synthetic mutation.** Pass 2 vs pass 1
  reports exactly the 10 unstable cases without the filter, and passes clean with it — so the gate is
  known not to cry wolf on an unchanged tree.
- **Item 0.12 is the expensive one.** It builds one throwaway module per test header — roughly 50
  compiles at 1–4 minutes each. It is embarrassingly parallel across lanes and its output is what
  decides each module's actual split, so it gates Step 2 but not Step 1.
- **Do not delete `bld/win-x86-vc143-debug` or `bld/win-x86-ccl16-debug` until 0.8–0.11 are done.**
  They are the only built trees, and the runtime baseline needs their binaries.
