# Whole-library C++ review (Fable 5.1): plan for the remaining findings

**Date:** 2026-09-06
**Source:** `notes/reviews/major/update_2026/whole-library-cxx-review-fable51.md` (62 rows; per-item detail, design intent and perf impact live there and are not repeated here) and the decision extract `whole-library-cxx-review-fable51-decisions.md`.
**Baseline:** working tree at `2480954` (`lazari2`). Line numbers in the review are exact for every file except `OSImplUNIX.h`, `TcpBlockTransferClient.h`, `ConversationProcessingBaseImpl.h` and `transfer/*.h`; current numbers for those are given below.

## Context

The review found 62 rows. Six are fixed (M-3 option b, R-5, T-2, T-3, O-3, O-4, commits `351f5a0`..`2480954`) plus the close-on-exec sweep from O-10 (Linux). The user took the remaining decisions on 2026-09-06 (below). This plan implements every other row as the review recommends, in dependency order, with the design corrections a source spot-check produced (N-2 mechanism, R-4 fallback, A-12 reachability, R-1/R-2 destructor paths). Nothing is committed by the implementer; the user commits between stages as before.

## Excluded (already decided; do not re-open)

| Item | Disposition |
|---|---|
| M-3 | option (b) done; mutual TLS deferred (`notes/plans/issues/broker-outbound-peer-identity-deferral.md`) |
| M-4 (+ the `sourcePeerId` fill-only "M-15" note in M-3) | **decided 2026-09-06: defer with the M-3 mutual-TLS work**; record only, no code |
| R-5, T-2, T-3, O-3, O-4 | done (decisions file "Decided" entries) |
| O-10 close-on-exec sweep | done on Linux (`process-spawn-fd-sweep-perf-deferral.md`); Darwin stays deferred there |
| A-12 | **decided 2026-09-06: TSan suppression + record**, no code |
| W-1..W-4 | record + handoff instructions (standing rule); the two instruction files the deferral record cites are absent from this checkout and were never committed, so the handoff is a fresh file |
| devenv7 scripts, JSON, TLS policy, ExecutionQueue items | excluded by the review's own "Already decided" list |

## Decisions taken 2026-09-06 (bind the design below)

1. **M-4**: deferred with M-3 mutual TLS.
2. **N-2 (+M-8)**: implement. Inactivity timers default **60 s**: receive and send on `HttpServer` only, TLS handshake and shutdown on every TLS server. Connection cap default = **min( 4096, soft fd limit / 2 (UNIX), 0.8 × physical RAM / worst-case per-connection footprint )**, effective value logged; an explicit value wins; 0 = unbounded. Blob server: cap on outstanding allocated blocks, **no** idle timer (auto-push connections idle up to the 30 s heartbeat).
3. **S-2**: escape behind `escapeTemplateVariables` (default true) + release note.
4. **R-4**: forbid and `BL_RT_ASSERT` on re-entry; document.
5. **A-12**: suppression + record.
6. **S-3**: hard cap **without LRU**: erase stale on encounter; at the cap sweep once, if still full do not cache (one-time log); future timestamps = stale; protected virtual clock hook.
7. **A-8 / R-7**: fix with atomics.

## Summary table (residual rows)

Ratings as in the review unless a decision changed the shape. Units = focused `make -k -j1 <module>` (toolchain × variant) + the run.

| ID | Recommendation | Risk | Blast radius | Cost of testing | Complexity | Cost of implementation |
|---|---|---|---|---|---|---|
| A-1 | `m_timer->cancel()` in `runNow()`/`cancelTask()` | Low | all timer tasks | 4 units `utf_baselib_tasks` + count case | Trivial | 0.5 h |
| A-2 | `wakeUp()` under `m_lock`, share impl with `runNow()` | Low | timer tasks | same run | Trivial | 0.25 h |
| A-3 | decision/swap under `m_lock`; call `m_wrappedTask->exception(nullptr)` directly | Low-Med | `RetryableWrapperTask` | 4 units + cancel-during-retry stress | Low | 1.5 h |
| A-4 | document hook lock discipline; `TcpServerBase`: disconnect `m_notifyCB` first, `wait=false`, null-guard the cancel-before-start path | Low | all `TcpServerBase` | 4 units `utf_baselib_http` + throwing-subclass case | Low | 1.5 h |
| A-5 | state flip under `m_lock`, join outside; atomic `m_shuttingDown`; guard `m_lastException`; `>=` + `notify_all` | Low | thread pool | 4 units `utf_baselib_basictask` + resize/dispose stress | Low | 2 h |
| A-6 | `m_socket->cancel()` in `cancelTask()`/`onTimeout`; `m_done` flag | Low | ICMP pinger | 2 units `utf_baselib_tasks` | Low | 1 h |
| A-7 | `m_lastLoggedCounter( 0 )` | None | counters | compile + run | Trivial | 5 min |
| A-8 | `std::atomic<State>`, `m_hasException` via one private setter (5 sites), `.load()` at `SimpleHttpTask.h:304`; lands last in `TaskBase.h` | Low | all tasks | TSan `utf_baselib_tasks` | Low | 1 h |
| A-9 | set `isExpectedException` before the throw | None | log noise | existing run | Trivial | 5 min |
| A-10 | guard `createThreads` in ctor; swap the last two lines of `ThreadPool.h:135-138`; skip global dispose for shared pools | Low | pool init/shutdown | compile-proof | Trivial | 0.5 h |
| A-11 | seed into a local then move; fix the divisor + includes | None | UUID/random | compile-proof | Trivial | 0.25 h |
| A-12 | **record + suppression only** | None | build config | TSan run shows the entry suppressed | Trivial | 0.5 h |
| A-13 | `m_cancelPending` in `ExecutorTaskT`; terminating call after the callback | Medium | every `AsyncExecutor` user | 4 units `utf_baselib_async` + slot-leak case (needs a task-backed test state) | Medium | 3 h |
| A-14 | success flag in `ScanDirectoryTask`; `BL_CHK` watchdog interval/extension; reject `-` hosts + `--` | None | scan, watchdog, pinger | existing runs | Trivial | 0.75 h |
| N-1 | `remote_endpoint( ec )` + `"<unknown>"` in `scheduleStdErrorResponse` | None | `HttpServer*` | 4 units `utf_baselib_http` + RST case | Trivial | 0.5 h |
| N-2 (+M-8) | timers (HTTP receive/send; generic TLS handshake/shutdown), derived connection cap closing excess sockets in `processIncomingConnection`, blob block cap; new `os::` memory/fd-limit helpers | Low-Med | all servers | 6 units (`_http`, `_io`) + 5 cases | Moderate | 2 d |
| N-3 | drop `isSocketClosed`; back-off timer + re-arm on transient errors | Low | every acceptor | 4 units `utf_baselib_io` + `setrlimit` case | Low | 3 h |
| N-4 | lower-case header keys; reject differing `Content-Length`, `Transfer-Encoding` (400/501), SP/HTAB in names, CTL in values; allow empty values; cap/validate the URI | Low | HTTP server | 4 units + parser cases | Trivial | 3 h |
| N-5 | validate response header names/values; `iequals` collisions; deny framing headers; `iequals` in the bridge | Low | HTTP server, bridge | 4 units + 2 cases | Trivial | 2 h |
| N-6 | `streambuf` `max_size` (64 KiB); configurable max response size (64 MiB) | Low | HTTP clients | 4 units + 1 case | Trivial | 1 h |
| N-7 | fail on EOF with `Content-Length` mismatch; `lexical_cast` with sign rejection | Low | HTTP clients | same run | Trivial | 1 h |
| N-8 | `m_shuttingDown` under `m_lock` in both `continueAfterStoppedAccepting` branches; close stream in the handshake branch | Low | TLS servers | protected-`onEvent` subclass case | Trivial | 1 h |
| N-9 | validate 100..599 (else 502); numeric status line; 500/504 for `PROCESS` failures; strip dump/file/function from HTTP error bodies | Low | HTTP server | 4 units `utf_baselib_http` | Trivial | 2 h |
| N-10 | arm client timeout once + matching text; two guards; clear `m_activeEndpoints` at disconnect; null-safe location; empty header values | Low | HTTP client, `TcpServerBase` | existing runs | Trivial | 2 h |
| M-1 | `"rb+"` with `"wb+"` fallback; delete→dispose→reopen test | Low | SingleFile storage | 4 units `utf_baselib_data` + 1 case | Trivial | 1 h |
| M-2 | `BL_CHK( offset <= size )` before `setOffset1`; throwing `setOffset1`/`setSize`; guards in the two deserialisers | Low | all block receivers | 6 units (`_io`, `_messaging`) + negative wire case | Trivial | 2 h |
| M-5 | collect heartbeat targets, call after the guard; defer `m_notifyCallback` to after the guard | Low | broker outbound registry | 4 units `utf_baselib_io` + fake-queue deadlock case + TSan | Low | 3 h |
| M-6 | capacity override only on GET/load | Low | `TransferOnly` PUT | `utf_baselib_io` case | Trivial | 0.5 h |
| M-7 | `error_code` overloads → `throwChunkDoesNotExist()`; open first | Low | blob storage | 4 units `utf_baselib_data` + concurrent case | Low | 2 h |
| M-9 | copy `m_state` under `m_lock` in accessors; `m_targetsLock` in the rotating dispatcher | Low | messaging clients | 4 units `utf_baselib_messaging` + dispose-vs-poll | Trivial | 2 h |
| M-10 | erase `m_peersInfo` entry when both vectors empty | None | outbound registry | unit case | Trivial | 0.5 h |
| M-11 | five ≤5-line fixes (signed length, header wrap, `onStreamChanging` reset, `m_taskTerminated`, NOEXCEPT guard) | None | see review | existing runs + corrupted-file case | Trivial | 1.5 h |
| R-1 | flip `m_isDisposed` under `m_disposeLock`, flush outside; keep destructor path idempotent | Low | all REST contexts | 4 units `utf_baselib_rest` + stress | Low | 1 h |
| R-2 | same shape; re-lock for the base part; early return in the continuation | Low | proxy shutdown | 4 units `utf_baselib_messaging` | Low | 1.5 h |
| R-3 | `time::milliseconds( ... )` | None | gateway | 4 units `utf_baselib_rest` + cancel case | Trivial | 5 min |
| R-4 | **atomic invoking thread id per subscription; `BL_RT_ASSERT` on re-entry in `unsubscribe`/`dispose`; document** | Low | reactive | 4 units `utf_baselib_tasks` + abort case (death test) | Low | 1.5 h |
| R-6 | grace period for unregistered entries; cap `m_requestsInFlight` | Low | gateway | unit case | Low | 1 h |
| R-7 | null-check; flag-first `onCompleted`; atomic `m_isDisposed`; locked `taskCopy()` | None | reactive, bridge, async | existing runs | Trivial | 1 h |
| R-8 | per-instance id in the holder; tracked holders closed on destruction | Low | proxy storage | two-instance case | Medium | 3 h |
| T-1 | reject non-portable / `..` / absolute `relPath`, empty symlink target, via `fs::isPortablePath` | Low | unpack, metadata | 4 units `utf_baselib_blobtransfer` + negative cases | Trivial | 2 h |
| T-4 | `unsigned char` indexing; `srcLength >= 2` guard; `static_assert` table sizes | None | codecs | 2 units `utf_baselib` + 0x80-0xFF round trip | Trivial | 0.5 h |
| T-5 | lower-bound branch for signed sources | Low | ~12 call sites | 2 units + boundary table | Low | 1.5 h |
| T-6 | correct factory type; delete dead branch | None | error rehydration | 4 units `utf_baselib_data` + round trip | Trivial | 15 min |
| T-7 | skip `last_write_time` for `Symlink` entries | Low | packager | dangling-link case | Trivial | 0.5 h |
| T-8 | `SymlinkTargetPolicy` (allow / relative-only / contained), default allow; count ignored links | Low | unpack symlinks | 3 cases per policy | Low | 3 h |
| T-9 | the fourteen small utility fixes as listed | None | utilities | 2 units `utf_baselib` + cases | Trivial | 4 h |
| T-10 | vector properties; string-or-array `aud`; `zip` optional | Low | JOSE consumers | 4 units `utf_baselib_data` + RFC examples | Low | 2 h |
| O-1 | single-quote wrapping (`'` → `'\''`); remove dead uid/gid | Low | `createProcess( userName )` | 2 units + `$`/backtick/`*` cases (root needed for the live run) | Low | 1 h |
| O-2 | take `m_lock`; no-op when `m_pid == 0`; unlocked helper for internal callers | Low | `os::sendSignal` | 2 units + case | Trivial | 0.5 h |
| O-5 | `parentPid` in `ChildExecInfo` + `getppid()` re-check; EPIPE-tolerant deleter for the process stdin stream; document thread constraint and `SIGPIPE` | Low-Med | `createProcess` users | 2 units + dead-child stdin case | Low | 3 h |
| O-6 | capture `errno` after the call; `errno = 0` before `fread/fwrite`; `ftello` inside the macro | None | stream I/O | 2 units + closed-pipe case | Trivial | 1 h |
| O-7 | permissions parameter (0600 default); `EINTR` retry | Low | test lock | existing global-lock tests | Low | 1.5 h |
| O-8 | `try_lock` + `write(2)` fallback, then `fastAbort()` | Low | every RIP site | review + existing runs | Low | 1 h |
| O-9 | copy the hook out of the lock; drop `NOEXCEPT` from the six function-type typedefs | Low | hooks, typedefs | compile-proof all modules | Trivial | 1 h |
| O-10 | function-local logging statics; atomic level; `#else`/`>=`; `TimeZoneData` size check + `call_once`; `splitCommandLineArguments`; `getpw*_r` rc; device seek via `os::fseek/ftell` | Low | logging, OS layer | compile-proof + existing runs | Low | 3.5 h |
| S-1 | fall back to the presented token; erase stale on encounter | None | authorization | 4 units `utf_baselib_security` + no-rotation mock | Trivial | 0.5 h |
| S-2 | escaper hook on `resolve`; URI-encode path, JSON-escape body; reject CR/LF/NUL; `escapeTemplateVariables` flag | Low-Med | broker → auth service | 4 units + template cases | Low | 3 h |
| S-3 | **cap without LRU** + clock hook | Low | broker memory/availability | 4 units + cap and future-timestamp cases | Low | 2.5 h |
| S-4 | print `<set>`/`<empty>`; `--token-data-default-file` | None | two apps | manual | Trivial | 0.5 h |
| S-5 | `O_CREAT|O_EXCL|0600` helper; `bl-tool http` secure mode with cookies/headers | Low | CLI error path | 2 units `utf_baselib_cmdline` + mode-bit case | Low | 2 h |
| S-6 | mutex + copy-out on `g_trustedRoots`; late roots into `g_sslContext` or `BL_CHK` | None | crypto init | 4 units `utf_baselib_http` + case | Low | 1.5 h |
| S-7 | `set_password_callback` failing fast | None | server startup | encrypted-key case | Trivial | 0.5 h |
| S-8 | the listed small fixes (BIO checks, `toIntSize()`, cert arithmetic, registry mutex, JNI env/offset, base64resource, `Version::fromString`) | Low | crypto, loader, JNI, bl-tool | compile-proof + existing runs (`utf_baselib_jni` build) | Trivial-Low | 5 h |
| W-1..W-4 | **record + handoff file** | None | records | none | Trivial | 0.5 h |

Total ≈ 11 engineering days on Linux; Windows rows ≈ 6.5 h on a Windows host (handoff).

## Design notes where the plan differs from or refines the review

- **N-2 mechanism.** (1) The handshake task is a bare `om::ObjectImpl< STREAM >` (`TcpBaseTasks.h:1526-1541`); put the handshake/shutdown timer in `TcpSslSocketAsyncBase::scheduleProtocolOperations` (both branches, `TcpSslBaseTasks.h:234-273`) and cancel it in `onTaskStoppedNothrow`, not in `handleWrite` (shutdown runs as the finish continuation while the task is still `Running`). (2) `setThrottleLimit` parks excess accepted sockets in Pending with no timer; the cap instead closes the stream in `processIncomingConnection` (`TcpBaseTasks.h:2085-2107`) when `m_eqConnections->size() >= cap`, logging through the existing server policy. (3) Receive/send timers follow `SimpleHttpTask::scheduleTimer/onTimer` (`SimpleHttpTask.h:239-317`: `acquireRef`, `m_lock`, check `Running`, `requestCancelInternal()`), re-armed per read/write, on `HttpServerReceiveRequestTask` and the send task in `HttpServer.h`. (4) Derived cap: new helpers `os::getPhysicalMemorySize()` (UNIX `sysconf( _SC_PHYS_PAGES ) * sysconf( _SC_PAGESIZE )`; Windows `GlobalMemoryStatusEx().ullTotalPhys`, listed for Windows verification in the handoff) and `os::getFileDescriptorSoftLimit()` (UNIX `getrlimit( RLIMIT_NOFILE )`; Windows 0 = not applicable); `TcpServerBase` takes `maxConnections` (default = derived), `connectionTimeout` (default 60 s) and a per-server `connectionMemoryFootprint` (HTTP: `Parser::g_maxHeadersSize + g_maxContentSize` + 64 KiB; blob/broker: `blockCapacity`); the effective cap is logged once at start. (5) M-8: `TcpBlockTransferServer` counts outstanding allocated blocks and answers `no_buffer_space` above a configurable cap; no idle timer.
- **R-4.** `std::atomic< std::thread::id > invokingThread` in `SubscriptionInfo` (idiom at `ExecutionQueueImpl.h:254`), set by the invoker tasks created at `ObservableBase.h:744` and the bound `onCompleted` tasks (`:561-567`, `:838-845`); `unsubscribeInternal( wait=true )` and `ObservableBase::dispose()` `BL_RT_ASSERT` when the current thread matches; header comment states the rule. No deferred-disposal machinery.
- **A-12.** The restructure would leave the same lock edge in `scheduleTask` (called under the task lock by `TaskBase::scheduleNothrow`) and `requestCancel`, so no code change. Add `projects/make/toolchain/tsan-suppressions.txt` (one `deadlock:` entry for the `AsyncExecutor` cycle) and append `suppressions=<path>` to the `TSAN_OPTIONS` export at `clang-analysis.mk:246`; write the record `notes/plans/issues/async-executor-tsan-inversion-record.md` (why the cycle cannot close: Ready and Executing are exclusive states of one `TaskInfo`).
- **A-13 before anything else in `AsyncExecutorImpl.h`.** Test needs a task-backed `AsyncOperationState` in `TestAsyncCommon.h` (none exists); `maxConcurrentTasks = 1`, cancel with the inner task latched, release, then a second operation must complete.
- **R-1 / R-2.** `disposeInternal` is also reached from the destructors unlocked and uses the flag as its idempotency guard; shape: flag under lock, flush unlocked, then the base part (R-2 re-locks for `base_type::disposeInternal(false)`).
- **S-3.** `AuthorizationCacheImpl.h:81-90` gains `m_maxEntries` (ctor parameter, default 10000); `updateInternal` (`:215-244`) erases stale on encounter, sweeps once at the cap and skips caching if still full (one-time `BL_LOG` at warning); `tryGetAuthorizedPrinciplal` (`:267-302`) erases stale entries and treats `timestamp > now` as stale instead of `BL_CHK`; `now()` becomes a protected virtual for the tests.
- **S-1** shares the stale-erase step: land S-1 first.
- **O-5.** `parentPid` captured near `OSImplUNIX.h:1621`, checked after `prctl` at `:1014` (`getppid() != parentPid` → `_exit`); the process stdin `FILE*` gets a deleter that flushes and tolerates `EPIPE` (`OSImplPlatformCommon.h:831-843` stays strict for other streams); `OS.h:690-699` documents the `SIGPIPE` requirement and the forking-thread binding.
- **M-5** current sites: `registerQueue` `TcpBlockTransferClient.h:1101-1170` (heartbeat at `:1145`), `continuationTask` `:1506-1778` (callbacks at `:1577`, `:1688`, `:1698`). Land M-11's `m_taskTerminated` (`:1605-1616`) first.
- **Other current line numbers (OSImplUNIX.h):** O-1 `:1556-1561`, `:1765-1800`; O-2 `:233-262`, `:1325`; O-7 `:2317-2410`; O-10 `splitCommandLineArguments` `:1365-1392`, `getpw*_r` `:2012-2130`, `ftello` `:1912`.

## Order and stages

File-level ordering constraints (from the spot-check): `TaskBase.h`: A-1/A-2, A-3, A-4 doc, A-9, then **A-8 last**. `TcpBaseTasks.h`: N-3 → N-8 → N-10, then A-4 and the N-2 cap (A-4's disconnect uses N-10's `m_activeEndpoints` clear). `HttpServer.h`: N-1 → N-9 → N-2 timers. `AsyncExecutorImpl.h`: A-13, then R-7 `taskCopy()`. `ObservableBase.h`: R-7 → R-4. `AuthorizationCacheImpl.h`: S-1 → S-3. `TcpBlockTransferClient.h`: M-11 → M-5; M-10 any time. Everything else independent.

Stages (each a logic-only, reviewable set; comment-only edits kept in separate hunks the user can split; nothing committed):

1. **Stage 3 — tasks, timers, pool:** A-1, A-2, A-3, A-4, A-5, A-6, A-7, A-9, A-10, A-11, A-14, then A-8. Modules: `utf_baselib_tasks`, `utf_baselib_basictask`, `utf_baselib`, `utf_baselib_http`.
2. **Stage 4 — async:** A-13, R-7 (async part), A-12 record + suppression. `utf_baselib_async` + TSan.
3. **Stage 5 — HTTP server and client:** N-1, N-3, N-4, N-5, N-6, N-7, N-8, N-9, N-10, then N-2 (+M-8) with the `os::` helpers. `utf_baselib_http`, `utf_baselib_io`.
4. **Stage 6 — block transfer and messaging:** M-1, M-2, M-5, M-6, M-7, M-9, M-10, M-11. `utf_baselib_io`, `utf_baselib_data`, `utf_baselib_messaging`.
5. **Stage 7 — REST, proxy, reactive:** R-1, R-2, R-3, R-4, R-6, R-7 (rest/reactive parts), R-8. `utf_baselib_rest`, `utf_baselib_messaging`, `utf_baselib_tasks`.
6. **Stage 8 — transfer and utilities:** T-1, T-4, T-5, T-6, T-7, T-8, T-9, T-10. `utf_baselib_blobtransfer`, `utf_baselib`, `utf_baselib_data`, `utf_baselib_tasks`.
7. **Stage 9 — OS layer:** O-1, O-2, O-5, O-6, O-7, O-8, O-9, O-10. `utf_baselib`; O-9's typedef change compile-proofed on every module.
8. **Stage 10 — security and apps:** S-1, S-2, S-3, S-4, S-5, S-6, S-7, S-8. `utf_baselib_security`, `utf_baselib_http`, `utf_baselib_cmdline`, `utf_baselib_jni` build; OpenSSL 1.1.1w build for S-6/S-7.
9. **Stage 11 — records:** M-4 deferral appended to `broker-outbound-peer-identity-deferral.md`; A-12 record; W-1..W-4 (+ the Windows half of the N-2 memory helper) appended to `windows-only-residual-findings-deferral.md` as items 10-13 with the new `notes/plans/issues/whole-library-windows-residuals-instructions.md` (structure per the existing handoff pattern: done list, session rules, per-item location/fix/verification, records to update); decisions file "Decided" entries for every row; release note for S-2 and the N-2 defaults.

## Verification

Rules: `AGENTS.md` (focused builds, `-j1` for more than one module, ≤ 5 test modules concurrently, both toolchains, both variants). Invocation on this host: `make -k -j1 <module> TOOLCHAIN={gcc1520|clang2010} VARIANT={debug|release}`; binaries at `bld/ub24-a64-<toolchain>-<variant>/utests/<module>/utf-baselib-<suffix>`; single case with `--run_test=<Case> --catch_system_errors=no`.

- Per stage: 4 units of each touched module (6 where the review says so) and the new cases; the cases that prove a defect are run once before the fix and shown failing (A-1 count, A-13 slot leak, N-1 latency, N-2 half-request / no-ClientHello / no-close_notify / cap+1 / idle PUT, N-3 `setrlimit`, M-1 reopen after delete, M-5 fake-queue deadlock, R-1/R-2 stress, R-4 re-entry abort, S-1 no-rotation, S-3 cap and future timestamp, T-1 `..`, T-4 0x80-0xFF, O-1 metacharacters, O-2 reaped handle, O-5 dead-child stdin, O-6 closed pipe).
- TSan (`BL_CLANG_ENABLE_RA_TSAN=1 BL_CLANG_ENABLE_RA_FORCE_O1=1`) on `utf_baselib_tasks`, `utf_baselib_async`, `utf_baselib_io` after stages 3, 4 and 6: no `AsyncExecutor` report outside the suppression, no M-5 inversion, no A-8/R-7 reports.
- OpenSSL 1.1.1w (`BL_USE_OPENSSL_1X=1`, objects removed before and after) for `utf_baselib_http` and `utf_baselib_security` in stage 10.
- N-2 defaults: a unit case asserts the derived cap equals the minimum of the three terms with injected limit/memory values, and that 0 restores unbounded.
- `git diff --check <merge-base> -- src` clean; every hunk explainable; no style-only edits mixed in; nothing committed.
