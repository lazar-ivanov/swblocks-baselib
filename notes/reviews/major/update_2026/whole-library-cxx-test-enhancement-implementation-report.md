# C++ test enhancement: implementation report

**Date:** 2026-09-08
**Plan implemented:** `whole-library-cxx-test-enhancement-plan.md` (same directory) - 383 tasks across 3 stages and 19 work packages.
**Result:** all **383/383** tasks delivered as **49 commits** on `lazari2`, `cb431f0` -> `7b4988e`. Nothing pushed.
**Companion:** `whole-library-cxx-test-enhancement-outstanding-issues.md` (same directory) holds the items still needing an owner decision; this document is the record of what was done.

## How the work was executed

Three isolated git worktrees (`swblocks-baselib-lane{1,2,3}`) on their own branches, each with its
own build tree, fed by a single orchestrator that owned all integration. Work was cut into chunks
grouped by the file each task writes; each chunk ran a full lifecycle - sync, baseline build, baseline
test, implement, build, test, compare, commit - then was rebased onto the integration branch and
fast-forwarded in **one at a time**, with a focused build and test in the integration worktree after
every single commit.

**Validation split.** Lane workers gated on **clang 20.1.0 / release**; the orchestrator independently
verified every integrated commit under **gcc 15.2.0 / debug**. Both toolchains compile
`-std=c++11 -Wall -Wpedantic -Wextra -Werror`. Two integration failures early in P1 showed that the
release-only lane gate is structurally blind to two classes of problem - compiler dialect differences,
and `BL_ASSERT`/`BL_VERIFY` which are compiled out under `NDEBUG` - so from that point every lane also
ran the gcc debug **test** target before handing back. That change found four further defects inside
the lanes rather than after integration.

---

## 1. Delivery against the plan

| Priority | Plan | Delivered | Commits |
|---|---|---|---|
| P0 | 21 | **21** | 13 |
| P1 | 214 | **214** | 27 |
| P2 | 127 | **127** | 7 |
| P3 | 21 | **21** | 2 |
| **Total** | **383** | **383** | **49** |

Delivery was reconciled against the plan's own priority totals, not against the working "wave" counts
used during scheduling (which were larger because they included lower-priority tasks pulled forward as
dependencies). Both counts were computed independently and agree.

**Dependency policy.** Where a task in the active priority group depended on a lower-priority task,
the dependency was pulled into the active group rather than left stranded. P0 therefore executed 27
tasks (21 P0 + 6 prerequisites), P1 executed 220 (214 + 6 more), and P2/P3 needed almost none, since
the earlier groups had already absorbed them.

## 2. Change volume

| | Files | Lines added | Lines removed |
|---|---|---|---|
| Tests (`src/utests`) | 95 | 58,287 | 3,910 |
| Production (`src/include`, `src/local`) | **10** | **116** | **11** |
| Notes (`notes/plans/issues`) | 6 | 428 | 0 |
| **Total** | **111** | **58,831** | **3,921** |

The production figure is the important one: **116 lines added and 11 removed across 10 files**, every
one of them a defect fix demanded by a failing test. Nine of those added lines are `#include`
directives for the self-containment fixes.

**31 new test files** were created, including an entirely new test module, `utf_baselib_apps`, which
the makefile's wildcard target discovery picked up with no makefile edit.

## 3. The two frozen headers

`TestBaselibDefault.h` and `TestTasks.h` were the plan's stated critical path - between them the
target of 125 of the 383 tasks - and were **not** to receive new cases or be restructured. That held:

| File | Before | After | New cases went to |
|---|---|---|---|
| `TestBaselibDefault.h` | 8,547 lines | 10,691 | `TestBaselibDefault2.h` .. `TestBaselibDefault10.h` (9 files, 5,201 lines) |
| `TestTasks.h` | 5,887 lines | 7,010 | `TestTasks2.h` .. `TestTasks8.h` (7 files, 7,308 lines) |

Every edit to the two originals is an addition **inside a pre-existing case**, or - in `TestTasks.h`
alone - the removal of a byte-for-byte duplicate helper class that task T006 promoted into the shared
`UtfConcurrent.h`. Neither file was refactored, split, reordered or reformatted. Sixteen numbered
sibling files were allocated centrally by the orchestrator so that no two lanes could ever claim the
same filename.

## 4. Suite growth and per-module timings

All figures below are **gcc 15.2.0 / debug**. "Before" was measured by checking the original commit
`cb431f0` out into a lane worktree and running the whole suite there; "after" is the final aggregate at
`7b4988e`. The time column is **Boost.Test's own in-module execution time** (its
`Leaving test module ...; testing time:` figure), not `make` wall time - the two runs had different
build states, so wall time would not be comparable, whereas this is.

| Module | Cases before | Cases after | Delta | Before (s) | After (s) | Runtime |
|---|---|---|---|---|---|---|
| `utf_baselib` | 154 | **227** | +73 | 38.0 | 44.6 | +17% |
| `utf_baselib_apps` | *new* | **13** | +13 | - | 0.001 | - |
| `utf_baselib_async` | 7 | **20** | +13 | 34.0 | 34.3 | ~0% |
| `utf_baselib_basictask` | 2 | **5** | +3 | 1.1 | 1.1 | ~0% |
| `utf_baselib_blobtransfer` | 15 | **23** | +8 | 104.0 | 58.4 | -44% |
| `utf_baselib_cmdline` | 5 | **17** | +12 | 0.003 | 0.011 | +294% |
| `utf_baselib_data` | 90 | **113** | +23 | 0.110 | 0.126 | +15% |
| `utf_baselib_http` | 34 | **56** | +22 | 53.2 | 55.6 | +5% |
| `utf_baselib_io` | 22 | **32** | +10 | 137.7 | 172.4 | +25% |
| `utf_baselib_jni` | 7 | **11** | +4 | 1.0 | 1.1 | +7% |
| `utf_baselib_loader` | 9 | **20** | +11 | 0.005 | 0.025 | +364% |
| `utf_baselib_messaging` | 28 | **46** | +18 | 204.8 | 275.6 | +35% |
| `utf_baselib_parsing` | 1 | **2** | +1 | 0.000 | 0.000 | +169% |
| `utf_baselib_rest` | 5 | **15** | +10 | 61.2 | 190.3 | +211% |
| `utf_baselib_security` | 32 | **51** | +19 | 77.1 | 24.1 | -69% |
| `utf_baselib_setprio` | 1 | **1** | +0 | 0.001 | 0.000 | -26% |
| `utf_baselib_tasks` | 58 | **101** | +43 | 108.8 | 124.0 | +14% |
| `utf_baselib_utils` | 1 | **3** | +2 | 0.001 | 0.005 | +446% |
| **17 pre-existing modules** | **471** | **743** | **+272** | **821** | **982** | **+20%** |
| **all 18 modules** | **471** | **756** | **+285** | | **982** | |

### Aggregate validation at each priority boundary

Every priority group ended with a full aggregate run over the union of modules it touched, from the
integration worktree, under gcc debug. All three were clean.

| Checkpoint | HEAD | Modules | Cases | Failures |
|---|---|---|---|---|
| P0 complete | `a91a693` | 11 | 465 | 0 |
| P1 complete | `66efd67` | 17 | 672 | 0 |
| **Final** | **`7b4988e`** | **17** | **755** | **0** |
| Final + `utf_baselib_setprio` | `7b4988e` | **18** | **756** | **0** |

The final aggregate script covered 17 modules; `utf_baselib_setprio` (1 case, unchanged in count by
task T336) was run separately at the same HEAD and is also clean. The complete tree is therefore
**756 cases over 18 modules, 0 failures**.

### Notable timing effects

Coverage grew **58%** (471 -> 743 cases over the pre-existing modules) for a **20%** increase in
execution time. Three effects account for the shape of the table.

- **Two modules got substantially faster despite gaining cases.** `utf_baselib_blobtransfer` fell
  **104s -> 58s (-44%)** while going 15 -> 23 cases, and `utf_baselib_security` fell
  **77s -> 24s (-69%)** while going 32 -> 51. The cause is task T004, which replaced a **5-second
  sleep** standing in for an acceptor-ready signal, at 8 call sites, with a bounded readiness poll.
  The worker measured that change as a genuine A/B - forcing the old sleep path back, re-running,
  then restoring - and recorded `utf_baselib_http` 53s -> 13s, `utf_baselib_blobtransfer`
  106s -> 42s, `utf_baselib_rest` 60s -> 46s at the time it landed. The saving compounds across every
  later run of every affected module, and it is why the overall increase is 20% rather than
  substantially more.
- **`utf_baselib_rest` is the one large regression: 61s -> 190s (+211%)** for 5 -> 15 cases. This is
  inherent to what was added. The gateway tasks (T186, T187, T188, T378, T379, T380, T383) each need
  a broker, and two of them hold a request open for a 3-second prune timeout. The plan anticipated
  this and told reviewers to push back on any proposal that starts an additional server; the
  implementations fold multiple probes into single server runs where possible, but a broker start per
  scenario is unavoidable for several of them. If full-suite wall time becomes a concern, this module
  is where to look first.
- **The other increases are proportionate or better.** `utf_baselib` +17% for +73 cases, `tasks` +14%
  for +43, `http` +5% for +22, `io` +25% for +10, `messaging` +35% for +18. The large *percentage*
  jumps on `cmdline`, `loader`, `parsing` and `utils` (+294% to +446%) are on modules whose absolute
  time is measured in **milliseconds** - `cmdline` went from 2.8ms to 11.0ms - and are not meaningful
  as ratios.
- **`utf_baselib_messaging` remains the longest suite** at ~276s and dominates any full-tree run.
- Individual chunk reports during the work sometimes showed large deltas that were **machine
  contention, not regression** - three lanes plus an integration verification share 4 cores. Every
  such delta was attributed per test case before being accepted; in one instance a module was
  measured as 12% *faster* immediately after gaining cases.

## 5. Production defects: 12 found, 11 fixed, 1 blocked

Every fix was required by a failing test, made as the smallest correct change, and kept under
regression. No production change was made speculatively.

| # | File | Defect | Evidence |
|---|---|---|---|
| 1 | `bl-tool/commands/ProcessFilesUtils.h` | `lines[ lines.size() ]` evaluated - a one-past-the-end `std::vector` read - on any file ending inside an open block comment | Reproduced: an extra trailing newline appeared in the output. Noted as **not** ASAN-detectable, since the slot lies inside the vector's own allocation; a byte-exact content assertion is the only reliable detector |
| 2 | `bl-tool/commands/ProcessFilesUtils.h` | `updateheadercomment` rewrote a file that is entirely a licence header to **zero bytes**, with exit code 0 and a log line claiming success | Reproduced against unmodified code before the fix |
| 3 | `bl-tool/commands/ProcessFilesUtils.h` | `removeCommentsWithMarkers` opened (and therefore truncated) the target **before** validating input, so a rejected source file was left truncated and partially rewritten. The rejection is legal C++ - code following `*/` on the same line | Reproduced: `UTF_REQUIRE_EQUAL( readAllBytes( path ), original )` failed |
| 4 | `bl-tool/commands/HttpRequest.h` | `saveResponse` drove its loop on `is.good()`, so a body ending in a newline wrote one extra blank line; an empty body was saved as `"\n"` | Reproduced on the exact byte comparison |
| 5 | `reactive/FanoutTasksObservable.h` | null `m_eqChildTasks` dereferenced inside `BL_ASSERT` on a shutdown path reachable when the observable fails before its first iteration - **aborted the debug build** | Reproduced: `make` Error 245, no Boost.Test summary. Instrumentation showed the line is hit 12 times across the suite and only the new case reports the pointer null |
| 6 | `core/detail/OSImplUNIX.h` + `OSImplWindows.h` | a **legal** `RedirectStdout\|RedirectStderr\|MergeStdoutAndStderr` + file-callback combination passed validation then tripped `BL_ASSERT( out )` | Reproduced: `SIGABRT`, exit 134 - the whole test binary |
| 7 | `core/detail/OSImplWindows.h` | `tryGetUserDomain` read `USERDNSDOMAIN` twice; the `USERDOMAIN` fall-back was dead code | Inspection only - Windows-only file, see the outstanding-issues document |
| 8 | `jni/JavaBridge.h` | `registerCallback` threw without clearing the pending Java exception raised by a failed `::RegisterNatives`, poisoning the thread - fatal under CheckJNI | The **first** run of the new test failed on unmodified code, which is itself the evidence |
| 9 | `httpserver/HttpServerPorts.h` | not self-contained (`no type named 'port_t' in namespace 'bl::os'`). Compiles today **only** because its single consumer includes `BaseServerPorts.h` two lines earlier | Negative control: fix reverted -> the new TU stopped compiling with the exact original diagnostic |
| 10 | `loader/Version.h` | not self-contained - undeclared `str::`, `utils::` | as above |
| 11 | `jni/JvmHelpers.h` | not self-contained - undeclared `fs::`, `os::`, `str::`, `MessageBuffer` | as above |
| 12 | `crypto/HmacSha256.h` | not self-contained - undeclared `BL_CHK_CRYPTO_API_NM`, `toIntSize` | as above |

**Blocked - D-01:** cancellation does not stop `RetryableWrapperTask`'s retry loop; measured at 5
factory calls and 8.01s of retry sleeps after `requestCancel()`. The fix changes cancellation
semantics in `ForwarderTaskBase` and `SimpleTimerTaskT`. See the outstanding-issues document.

*Update (2026-09-08): D-01 is no longer blocked.* It was fixed as item 1 of
`whole-library-cxx-test-enhancement-outstanding-issues-plan.md`, and not the way this paragraph
predicted: neither `ForwarderTaskBase` nor `SimpleTimerTaskT` was changed, because both serve their
other users correctly. The cancel latch lives in `RetryableWrapperTaskT` itself — the only class
which knows that the operation is the whole retry sequence rather than the task currently wrapped.
`Tasks_RetryableWrapperTaskCancelTests` now measures 1 factory call and completes in about a
millisecond instead of 8.01 s.

*Update (2026-09-08): a further thirteen defects were fixed* under items 3, 7 and 8 of that plan —
the redaction gaps, the eleven "pinned as current, not endorsed" behaviours which were fixed rather
than kept, and the two remaining filed deferrals. They are catalogued in section 14 of
`notes/plans/issues/devenv7-breaking-changes-release-notes.md`.

Four of the twelve are in `bl-tool`, and three of those **destroy or corrupt user files**.

## 6. What the audit actually revealed

The plan's premise was that coverage was missing. The more consequential finding is that a
significant amount of coverage was **present but inert**.

### 6.1 Tests that structurally could not fail - 10 repaired

| # | Test | Why it could not fail |
|---|---|---|
| 1 | `TestFsUtils::compareFolders` / `compareFileContents` | returned agreement whenever either side was missing or not a directory; compared at most the first 1024 bytes and the loop never reached index 0 |
| 2 | `Tasks_ScanDirectoryTaskTests` | contained **no `UTF_REQUIRE` at all** |
| 3 | `FilesPackagerUnit` metadata | compared the pipeline against itself; replaced by an independent `recursive_directory_iterator` walk |
| 4 | `isLocalUserOnWindows` | the helper carried the **same copy-paste bug** as the code under test |
| 5 | filesystem-metadata read-back (T121) | an unfalsifiable disjunction over a single shared box |
| 6 | T028's ordering assertion | **impossible and vacuous** as the plan specified it |
| 7 | a `UTF_CHECK_THROW` in the async-RPC error path | could not fail; replaced by four discriminating sub-cases |
| 8 | `ExecutionTimer` move-cancel (T237) | a name-based count could not see the moved-from timer's line, because its name had been moved out. Found by the worker mutation-testing **its own** implementation of the plan's assertion list |
| 9 | the HTTP client redirect block (T294) | every assertion sat inside a host guard that is **off by default** |
| 10 | async cancel-path assertions (T363) | were `BL_ASSERT`, compiled out under `NDEBUG` - the cancel tests verified nothing in release builds |

### 6.2 Cases that silently no-op'd while reporting as passed

Task T011 converted the suite's silent skip gates. Roughly **40 cases** returned before their first
assertion when run without `--is-client` / `--is-server` / `--path`, and Boost.Test reported every one
of them as **passed**. They now emit `SKIPPED: <case>: <reason>` and appear in an end-of-run block.

At the final tree **35** such cases report as skipped - `data` 17, `messaging` 6, `io` 4,
`blobtransfer` 3, `rest` 2, and one each in `parsing`, `security` and `tasks`. The count is one lower
than the 36 measured when T011 landed because a later task (T138) removed one gate and made that case
actually run.

The practical consequence: CI's green count had been overstating real coverage by roughly forty cases,
with nothing in any log or report saying which ones did not run.

### 6.3 Falsifiability was proven, not assumed

Where it was cheap, workers mutated production code, confirmed the intended assertion failed, reverted,
and reported which mutation each assertion caught. One chunk alone applied **19 mutations** and
verified the production tree byte-identical afterwards. Two assertions were proven by **compile-time**
mutation - removing `using base_type::resolve;` and `using base_type::async_resolve;` each produce a
compile error pointing at the new case, which no runtime assertion could catch.

Equally important, workers stated plainly which assertions they could **not** mutation-test and why -
for example a case whose regression manifests as a **process hang**, which exercising would have hung
the suite.

## 7. Defects in the plan document itself

Six specification errors were found by attempting the work; they are listed in full in the
outstanding-issues document.

*Correction (2026-09-08): there are eleven, not six.* Five more were identified while resolving the
outstanding issues, and all eleven are now carried in section 0 of the plan document itself as an
**erratum** — the specifications there are deliberately left unedited, so the plan stays the
historical record. The five added are:

- **T041** — a non-void `utils::tryCatchLog` without a callback trips the `BL_ASSERT` in
  `core/Utils.h`, so that combination of the specified matrix cannot be executed;
- **T193** — the `/authorize?t=opaque-token-value` shape is unattainable, because `-` is
  percent-encoded by the URL-path escaper;
- **T334** — the first additionally bundled trusted root is an **X.509 v1** certificate, which the
  task's v3 expectations do not describe;
- **T376** — the expected placeholder literal belongs to the `dm::Payload` streaming operator, while
  the trace site the task names streams `AsyncRpcPayload`;
- **T181** — the "present token on the acknowledgment path" guard the task asks to cover does not
  exist in the production code.

The two most serious of the original six:

- **T006 step 4 / T010 step 4 deadlock the test binary.** Wrapping the nine exception macros so the
  whole `UTF_*` vocabulary shares `test::UtfGlobals::g_lock` self-deadlocks, because `UtfMain.h:116`'s
  `utfLineLogger` takes that same non-recursive lock for *every* log line. Making the lock recursive
  does not help - a worker thread logging while the main thread waits on tasks deadlocks across
  threads. Both steps were dropped, with the reasoning recorded so they are not re-attempted.
- **T028's assertion is impossible and vacuous.** Re-specified against the real production contract.

The plan's **line numbers are stale throughout**; cases must be located by name.

## 8. Process record

Four anomalies are worth recording because they shaped how the rest of the work was validated.

- **A false-green integration (mine).** A `git merge --ff-only` failed - the branch was no longer a
  fast-forward - but the merge and its verification ran as one backgrounded command and only the
  verification log was read. With the commit's sources absent, `make` had nothing to rebuild and
  re-ran a **pre-session binary**, reporting `EXIT=0` and "No errors detected" for four modules. It
  was caught by **case-count arithmetic**, not by the exit status: the run reported 28 and 7 cases
  where the lane had measured 29 and 8, and the two "missing" cases were exactly the ones that commit
  added. Integration was subsequently moved behind a script that aborts loudly if the fast-forward
  fails, **refuses to verify a tree it did not integrate**, and flags a tested binary older than the
  sources it includes. That guard then caught two further real failures.
- **A C++14 construct reached main.** Default arguments on **lambda parameters** are a C++14 extension:
  clang accepts them, gcc rejects them with `-Werror=c++14-extensions`, so `utf_baselib_data` did not
  build at all under gcc. Fixed, and every worker brief thereafter carried an explicit trap list plus
  a gcc compile-only guard.
- **A `BL_ASSERT` precondition violation reached main.** `Utils.h:406` asserts
  `is_same<void,RETURN> || cbOnError` at the *top* of `tryCatchLogImpl`, so a non-void `tryCatchLog`
  without a callback aborts in debug even on the success path. Invisible to a release-only gate. Main
  was reset to green, the chunk returned to its lane, and assert-heavy modules were required to run
  gcc debug themselves thereafter.
- **Three chunk mis-assignments (mine).** Chunk membership was initially derived from task-ID ranges,
  but the plan groups tasks by package and IDs are not contiguous within one. Two workers correctly
  refused out-of-module tasks; a third substituted the real tasks for the module it was given, which
  another lane was already doing, producing one duplicated chunk that was resolved at rebase by taking
  the already-integrated version. Remaining chunks were grouped from each task's declared **file**.

## 9. Repository state

- **Integration branch:** `lazari2`, at `7b4988e`, 49 commits ahead of `cb431f0`.
- **Nothing pushed.** `origin/lazari2` remains at `cb431f0`.
- **Aggregate validation at final HEAD:** 18 modules, **755 cases, 0 failures**, gcc 15.2.0 debug.
- Every commit message begins `improving test coverage: P<n> ` and carries the
  `Co-Authored-By: Claude Opus 5` trailer.
- Six deferral/decision documents were filed in `notes/plans/issues/`. *(2026-09-08: three of the
  six are now closed — see section 8.1 of the outstanding-issues document for the current state of
  all six and of the three other records the resolution work touched.)*
- Three lane worktrees (`swblocks-baselib-lane{1,2,3}`) are clean and retained; they hold no unique
  commits and can be removed with `git worktree remove`.
