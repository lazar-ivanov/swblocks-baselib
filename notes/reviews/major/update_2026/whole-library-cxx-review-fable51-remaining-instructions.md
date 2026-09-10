# Handoff: implementing the remaining whole-library review findings

**For:** a fresh Claude Code session (Opus 5) on the Linux devenv7 host, repository `swblocks-baselib`, branch `lazari2`.
**Written:** 2026-09-06, from the session that produced the plan.
**Goal:** implement every remaining row of the Fable 5.1 whole-library C++ review exactly as the plan prescribes, stage by stage, with proof, without committing.

Read these three documents first, in this order, before touching any source:

1. `notes/reviews/major/update_2026/whole-library-cxx-review-fable51-remaining-plan.md` — the plan: what is excluded, the seven binding decisions, the summary table (one row per item with recommendation, risk, blast radius, cost of testing, complexity, cost), the design notes where the plan corrects the review, the file-level ordering constraints, the stages, the verification rules.
2. `notes/reviews/major/update_2026/whole-library-cxx-review-fable51.md` — the review. For every item the "Detailed findings" section (from line 159) gives the exact lines, the failure mechanism, the author's design intent the fix must preserve, the runtime performance impact of the fix and the fix itself. Implement from that text; do not re-review the library.
3. `notes/reviews/major/update_2026/whole-library-cxx-review-fable51-decisions.md` — the decision extract with the "Decided" entries for the items already fixed. Its entry format is the one to reproduce for every item you finish.

Also read `AGENTS.md` at the repository root (it is loaded automatically) and `notes/plans/issues/process-spawn-fd-sweep-perf-deferral.md` before touching `OSImplUNIX.h` (it explains the child helper shape O-5 must keep).

---

## 1. What is already done (do not redo, do not re-review)

| Item | State | Where recorded |
|---|---|---|
| M-3 | option (b) fixed, mutual TLS deferred | `notes/plans/issues/broker-outbound-peer-identity-deferral.md` |
| R-5, T-2, T-3, O-3, O-4 | fixed with tests, commits `351f5a0`, `a4833d2` | decisions file "Decided" entries |
| O-10 close-on-exec sweep | fixed on Linux (`close_range`), Darwin deferred | `notes/plans/issues/process-spawn-fd-sweep-perf-deferral.md` and `-plan.md` |
| Windows verification of O-3/O-4 | done on a Windows host, commits `e02eb07`, `99804db` | `notes/plans/issues/windows-only-residual-findings-deferral.md` |

Everything the review lists under "Already decided: excluded from this plan" stays excluded.

## 2. Session rules (binding; restated from `AGENTS.md` and the user's standing instructions)

- **This document is the explicit instruction to implement.** `AGENTS.md` defaults to research over implementation; the user has approved the plan, so implement it. Anything not covered by the plan or the review is still "research and ask", not "do".
- **Edit tool only for existing files.** Never rewrite a file; every hunk must be intentional and explainable in a diff review. The Write tool only creates new files (new records, the handoff file for Windows).
- **Never commit.** No `git add`, `git commit`, `git push`, no stash. The user commits between stages. Leave the tree with all changes uncommitted when you stop.
- **Logic and style never mix.** A comment-only or documentation-only edit (A-4's hook documentation, O-5's `SIGPIPE` note, R-4's contract comment) goes in its own hunk so the user can commit it separately; do not reformat, reorder includes or rename while fixing.
- **No unrequested features.** Implement what the review row says. Where the review offers alternatives, the plan names the one to use. Do not add "nice to have" parameters, logging or refactors.
- **C++11 only.** No `std::make_unique`, `std::optional`, `shared_mutex`, generic lambdas, structured bindings, `if constexpr`. Follow the file's idioms (`BL_MUTEX_GUARD`, `BL_CHK`, `om::ObjPtr`, `cpp::ScalarTypeIniter`, `BL_NOEXCEPT_BEGIN/END`, `SAA_in`/`SAA_inout` annotations, the column-aligned member layout).
- **Builds:** focused builds of the affected test modules only; `-j1` whenever more than one module is built; never a full parallel build of the repository. Up to 5 test modules may run concurrently. Both toolchains (`TOOLCHAIN=clang2010`, the default, and `TOOLCHAIN=gcc1520`) and both variants (`VARIANT=debug`, `VARIANT=release`).
- **Python:** only `.venv/bin/python` / `.venv/bin/pytest` if any Python is needed (none is expected in this work).
- **Out of bounds:** no edits to `scripts/devenv7/**` (record defects in `scripts/devenv7/docs/supply-chain-verification-deferral.md` instead); no edits to `src/include/baselib/core/detail/OSImplWindows.h` or any other Windows-only code that cannot be compiled here, except the Windows half of the new `os::` helpers in N-2, which is explicitly listed for Windows verification in the handoff you write in stage 11.
- **When in doubt, ask.** Use `AskUserQuestion` for anything where two readings lead to materially different code. Do not guess and do not silently narrow the scope. If a fix turns out to need more than the review estimated, say so and ask before widening it.
- **Stop at the end of every stage** and report (section 7). Do not start the next stage until the user has reviewed and committed.

## 3. The seven binding decisions (2026-09-06)

1. **M-4 (+ the `sourcePeerId` fill-only note in M-3):** deferred with the M-3 mutual-TLS work. Record only, no code (stage 11).
2. **N-2 (+M-8):** implement. Inactivity timers default **60 s**: receive and send timers on `HttpServer` only; TLS handshake and shutdown timers on every TLS server. Connection cap default = **min( 4096, soft descriptor limit / 2 on UNIX, 0.8 × physical RAM / worst-case per-connection footprint )**, effective value logged once; an explicit value wins; 0 = unbounded. Blob server: a cap on outstanding allocated blocks answering `no_buffer_space`; **no** idle timer there (auto-push connections legitimately idle up to the 30 s heartbeat, `TcpBlockTransferClient.h:1340`).
3. **S-2:** escape template variables behind a config flag `escapeTemplateVariables` defaulting to true; percent-encode for the URL path template, JSON-escape for the JSON body; reject CR/LF/NUL in tokens up front; release note.
4. **R-4:** forbid unsubscribe/dispose from inside an observer callback: record the invoking thread id per subscription, `BL_RT_ASSERT` with a message on re-entry in `unsubscribe` and in `ObservableBase::dispose()`, document the rule in the header. No deferred-disposal machinery.
5. **A-12:** no code change. TSan suppression + record.
6. **S-3:** hard cap **without LRU**: erase stale entries on encounter; at the cap sweep once and, if still full, do not cache the new principal (authorization still succeeds; one-time warning log); a future timestamp is treated as stale, not as an error; a protected virtual clock accessor so the tests can inject time.
7. **A-8 / R-7:** fix with atomics (`std::atomic< State > m_state`, an atomic `m_hasException` mirror set through one private setter at the five `m_exception` assignment sites in `TaskBase.h` (lines 581, 702, 1098, 1131, 1236), `.load()` at the streaming site `SimpleHttpTask.h:304`, `std::atomic< bool > m_isDisposed` in the bridge, a locked `taskCopy()` accessor in `AsyncExecutorImpl.h`).

## 4. Host facts and invocations

- Ubuntu 24, arm64, 2 cores, soft and hard descriptor limit 1048576 (`char` is unsigned here: T-4's signed-`char` bug does not reproduce on this host; the test must use explicit `unsigned char`/`char` casts or the `static_assert` plus a table-driven check).
- Build one module: `make -k -j1 utf_baselib_tasks TOOLCHAIN=gcc1520 VARIANT=debug` (toolchain names are exactly `gcc1520` and `clang2010`). `make -k -j1 test_<module> ...` builds and runs the module.
- Binaries: `bld/ub24-a64-<toolchain>-<variant>/utests/<module>/utf-baselib-<suffix>` (hyphenated, e.g. `utf-baselib-tasks`, `utf-baselib-messaging`; the core one is `utf-baselib`). Single case: `<binary> --run_test=<CaseName> --catch_system_errors=no`.
- `utf_baselib` is a single translation unit and takes several minutes per build; batch its items (stages 3, 8, 9) before building.
- Disk: 47 GB, about 7.4 GB free with all four `bld/ub24-a64-*` trees present. If a build fails for space, remove the variant trees you are not using (`rm -rf bld/ub24-a64-<toolchain>-<variant>`); `make clean` does not work in this project.
- TSan: `make -k -j1 <module> BL_CLANG_ENABLE_RA_TSAN=1 BL_CLANG_ENABLE_RA_FORCE_O1=1` (clang only; `projects/make/toolchain/clang-analysis.mk`, which exports `TSAN_OPTIONS=second_deadlock_stack=1`). Build TSan objects into a separate tree or remove objects before and after, since the flags change the object files.
- OpenSSL 1.1.1w build (stage 10 only, S-6/S-7): `BL_USE_OPENSSL_1X=1`, objects removed before and after.
- Killing a stuck test: `pkill -x utf-baselib-tasks` (never `pkill -f` with a path: it matches the invoking shell).
- Test helpers: `bl::fs::TmpDir`, `utest::TestUtils`, `test::MachineGlobalTestLock` (O-7 users), the `CancelType` fault-injection hooks in `src/utests/include/utests/baselib/TestBlobTransferUtils.h` (T-items), `TestAsyncCommon.h` (A-13 needs a new task-backed `AsyncOperationState` there), the mock authorization service in `src/utests/utf_baselib_security/TestAuthorizationCacheImpl.h` (S-1 needs a variant returning a principal with a null token).
- A `BL_RT_ASSERT` aborts the process. The R-4 case must therefore run the re-entrant observer in a child process (spawn the test binary itself with `--run_test=<helper case>` through `os::createProcess` and assert a non-zero exit) rather than in the test process.
- Merge base for the final diff check: `18cc4b1` (`git merge-base master lazari2`).

## 5. Per-stage workflow

For each stage:

1. Re-read the review's detailed finding for every item in the stage and the plan's design note for it. Note the review's line numbers are exact except in `OSImplUNIX.h`, `TcpBlockTransferClient.h`, `ConversationProcessingBaseImpl.h` and `transfer/*.h`; the plan's "Design notes" section gives current numbers for O-1, O-2, O-5, O-7, O-10, M-5, M-10, M-11.
2. For every item the plan's verification section names as "shown failing before the fix", write the regression case first, build once, run it, and keep the failing output (exact assertion text or the hang under a watchdog) for the report. If a case cannot be made to fail deterministically before the fix (N-8 interleaving, the O-5 `getppid` race), say so and use the deterministic alternative the plan gives (protected-`onEvent` subclass for N-8; none for the `getppid` race, which is documented rather than tested).
3. Implement the items in the file-level order the plan gives (`TaskBase.h`: A-1/A-2, A-3, A-4 doc, A-9, then A-8 last; `TcpBaseTasks.h`: N-3 → N-8 → N-10, then A-4 and the N-2 cap; `HttpServer.h`: N-1 → N-9 → N-2; `AsyncExecutorImpl.h`: A-13 then R-7; `ObservableBase.h`: R-7 → R-4; `AuthorizationCacheImpl.h`: S-1 → S-3; `TcpBlockTransferClient.h`: M-11 → M-5, M-10 any time).
4. Build the stage's modules, `-j1`, for `clang2010` and `gcc1520`, `debug` and `release` (4 units; 6 where the plan says so, i.e. the second module of the pair as well). Run the full module test binary for each unit, not only the new cases; up to 5 module runs concurrently.
5. Run TSan where the plan lists it (stages 3, 4, 6: `utf_baselib_tasks`, `utf_baselib_async`, `utf_baselib_io`).
6. `git diff --check 18cc4b1 -- src` must be clean (no trailing whitespace, no CRLF).
7. Add the "Decided" entry for every finished item to the decisions file (section 6), in the same shape as the existing R-5/T-3/O-3/O-4 entries: date, option taken, what was implemented (names of the new functions/members), the tests added and that they failed before the fix, any deviation from the review text, downstream notes (wire/source compatibility, defaults changed).
8. Stop and report (section 7).

## 6. Stages

Each stage is one reviewable, logic-only set. Modules named are the ones to build and run.

| Stage | Items | Modules | Notes |
|---|---|---|---|
| 3 tasks, timers, pool | A-1, A-2, A-3, A-4, A-5, A-6, A-7, A-9, A-10, A-11, A-14, then A-8 | `utf_baselib_tasks`, `utf_baselib_basictask`, `utf_baselib`, `utf_baselib_http` (A-4) | A-6's live ICMP run needs root; build and run what does not. A-4: also null-guard the cancel-before-start path in `TcpServerBase::onTaskStoppedNothrow` (queues can be null there). TSan on `utf_baselib_tasks` after A-8. |
| 4 async | A-13, R-7 (async `taskCopy()`), A-12 record + suppression | `utf_baselib_async` | A-13 test: `maxConcurrentTasks = 1`, task-backed operation state, cancel with the inner task latched, release, second operation must complete. A-12: new `projects/make/toolchain/tsan-suppressions.txt` with one `deadlock:` entry, `suppressions=<path>` appended to the `TSAN_OPTIONS` export in `clang-analysis.mk:246`, record `notes/plans/issues/async-executor-tsan-inversion-record.md` (why the cycle cannot close: a `TaskInfo` cannot be Ready and Executing at once). TSan on `utf_baselib_async` shows the cycle suppressed and nothing else from `AsyncExecutor`. |
| 5 HTTP server and client | N-1, N-3, N-4, N-5, N-6, N-7, N-8, N-9, N-10, then N-2 (+M-8) | `utf_baselib_http`, `utf_baselib_io` | N-2 per decision 2 and the plan's design note: handshake/shutdown timer in `TcpSslSocketAsyncBase::scheduleProtocolOperations` cancelled in `onTaskStoppedNothrow`; cap closes the stream in `processIncomingConnection`; new `os::getPhysicalMemorySize()` and `os::getFileDescriptorSoftLimit()` in `OS.h` with UNIX implementations (Windows half written but flagged for Windows verification); `TcpServerBase` parameters `maxConnections`, `connectionTimeout`, `connectionMemoryFootprint`. Cases: half request, no ClientHello, no close_notify, cap + 1 sockets, idle PUTs beyond the block cap, derived-cap arithmetic with injected values, 0 = unbounded. |
| 6 block transfer and messaging | M-1, M-2, M-5, M-6, M-7, M-9, M-10, M-11 | `utf_baselib_io`, `utf_baselib_data`, `utf_baselib_messaging` | M-5 deadlock case with a fake `queue_t` under a watchdog, then TSan on `utf_baselib_io`. |
| 7 REST, proxy, reactive | R-1, R-2, R-3, R-4, R-6, R-7 (rest/reactive parts), R-8 | `utf_baselib_rest`, `utf_baselib_messaging`, `utf_baselib_tasks` | R-4 per decision 4; child-process death test. R-1/R-2: keep the unlocked destructor path idempotent (flag under lock, flush outside, R-2 re-locks for the base part). |
| 8 transfer and utilities | T-1, T-4, T-5, T-6, T-7, T-8, T-9, T-10 | `utf_baselib_blobtransfer`, `utf_baselib`, `utf_baselib_data`, `utf_baselib_tasks` | T-8 policy default is "allow" (behaviour unchanged). T-9 is fourteen small fixes; one hunk each, all listed in the review. |
| 9 OS layer | O-1, O-2, O-5, O-6, O-7, O-8, O-9, O-10 | `utf_baselib`; O-9 compile-proofed on every module | O-5 keeps the async-signal-safe child helper shape (`parentPid` in `ChildExecInfo`, `getppid()` check after `prctl`, `_exit` on mismatch); EPIPE-tolerant deleter only for the process stdin stream. O-7: permissions parameter defaulting to 0600 and `EINTR` retry; no `flock` rewrite. O-10 minus the close-on-exec sweep (done). |
| 10 security and apps | S-1, S-2, S-3, S-4, S-5, S-6, S-7, S-8 | `utf_baselib_security`, `utf_baselib_http`, `utf_baselib_cmdline`, `utf_baselib_jni` (build) | S-1 before S-3. OpenSSL 1.1.1w build of `utf_baselib_http` and `utf_baselib_security` for S-6/S-7. S-8's manifest class-id array leak is Info: no change. |
| 11 records | see section 8 | none | |

## 7. What to report at the end of each stage

The user reads only the final message of your turn; make it self-contained:

- A table with one row per item: what was changed (function/member names), the test added, whether it was shown failing before the fix (with the failing assertion text or "hang, killed by watchdog"), and any deviation from the review text with the reason.
- The build and run matrix actually executed (module × toolchain × variant, TSan runs, the 1.1.1w build), with every failure quoted verbatim. Do not summarise a red run as green; if a pre-existing case fails independently of your change, say so and show that it fails on the untouched baseline too.
- Anything found on the way that is out of scope (a new defect, a slow test, a Windows-only concern): do not fix it; record it in the appropriate deferral record or propose a new one, and list it.
- The `git status --short` and `git diff --stat` output, and the statement that nothing was committed.

Then stop. The user commits and tells you to continue.

## 8. Records to update (stage 11, plus the per-stage "Decided" entries)

- `notes/reviews/major/update_2026/whole-library-cxx-review-fable51-decisions.md`: a "Decided" entry per item (done per stage), including the record-only outcomes for M-4, A-12 and W-1..W-4.
- `notes/plans/issues/broker-outbound-peer-identity-deferral.md`: append M-4 (and the `sourcePeerId` fill-only note) as deferred with the mutual-TLS item, with the revisit conditions already listed there.
- `notes/plans/issues/async-executor-tsan-inversion-record.md`: new (stage 4).
- `notes/plans/issues/windows-only-residual-findings-deferral.md`: append W-1, W-2, W-3, W-4 and the Windows half of the N-2 `os::` helpers as items 10-14 in the existing table shape (finding, location, what is wrong, fix when picked up, how to validate).
- `notes/plans/issues/whole-library-windows-residuals-instructions.md`: new handoff for a Windows session, structured like this file: what is done, session rules (Edit-only, never commit, `-j1`, batch-file delayed expansion rules from `AGENTS.md`, LF endings), per-item location/fix/verification (W-1 argv round-trip scaffold at `TestBaselibDefault.h:1577`; W-4 two threads spawning concurrently with redirects; the N-2 helper: `GlobalMemoryStatusEx` value sanity and the derived cap on Windows), and the records to update afterwards. Note that the two instruction files the deferral record already cites are absent from this checkout.
- Release note (the repository's usual place; if none exists, a section in the decisions file): S-2 escaping default, N-2 timeout and cap defaults, O-9 typedef change, T-10 accessor type change, N-9 status-line and error-body changes.

## 9. Pitfalls learned in the previous stages

- The review's exact line numbers are a strength: quote them in the "Decided" entries, but re-check them in the four files that changed.
- `TestBaselibDefault.h` is one huge file; new process tests go in the `#if !defined( _WIN32 )` blocks next to the O-3/O-4 cases.
- Spawn-heavy tests are cheap now (2 ms per spawn); a slow process test points at its own waits, not at `createProcess`.
- Timing-based cases (A-1's callback count, N-2's timeouts, the detached-release bound) must leave margin for a 2-core host under 5 concurrent module runs; use generous windows and assert on order or counts rather than exact durations.
- `BL_RT_ASSERT` and `BL_RIP_MSG` abort; never trigger them in the test process.
- Keep every comment you add in the file's existing comment style (block comments, no trailing periods on single lines, column alignment).
- Never `pkill -f`; never `make clean`; never a full parallel build.
