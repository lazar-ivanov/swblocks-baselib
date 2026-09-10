# Timer pre-start cancel race and macOS `getpeername` EINVAL — review and plan

## Summary

- Two intermittent macOS test failures were traced by Opus 5 to production code. This note is an
  independent review of that analysis and of the proposed fixes, plus the plan to apply them.
- Both root-cause analyses are confirmed. The first is platform-independent lock ordering; the
  second is a macOS/BSD portability gap.
- The timer fix is right as proposed. The `remote_endpoint()` fix is right for `TcpBaseTasks.h`,
  but the NetUtils part should target `safeRemoteEndpointId`, not the retry loop, and one further
  ENOTCONN-only site was missed.
- The timer race can be regression-tested deterministically by cancelling a timer task before it
  is pushed onto a queue, so verification does not have to rely on repetition alone.
- These are the first production edits in this effort; everything before was test-only.
- Status: analysis complete, nothing implemented.

## Issue 1 — Timer task cancelled before it starts sleeps the full init delay (confirmed)

All in [TaskBase.h](src/include/baselib/tasks/TaskBase.h):

- `requestCancelInternal()` (line 1007) sets `m_cancelRequested`, then returns without calling
  `cancelTask()` when the state is not `Running`. It is idempotent, so a later call is a no-op.
- `scheduleNothrow()` (line 1141) resets the flag only on restart (`Created != m_state`), so a
  fresh task keeps a pre-start cancel. The generic abort at line 1177 is skipped because
  `TimerTaskBaseT::scheduleEvenIfAlreadyCanceled()` (line 1820) returns true.
- `TimerTaskBaseT::scheduleTask()` (line 1805) arms `async_wait` with `getInitDelay()` and nothing
  re-checks the flag afterwards.
- `SimpleTimerTaskT::run()` (line 1994) only observes `isCanceled()` when the wait fires.

`RetryableWrapperTaskT::continuationTask()` (line 2122) creates the retry sleep timer with
`initDelay == retryTimeout` and swaps it in at line 2198 under the wrapper lock. A cancel that lands
after that swap but before the queue re-schedules the wrapper reaches the timer in the `Created`
state and is silently lost until the sleep elapses.

The test at [TestTasks3.h:1025-1094](src/utests/utf_baselib_tasks/TestTasks3.h#L1025-L1094)
(30 s retry timeout, asserts `elapsed < retryTimeout`) is exactly that interleaving: its
verification callback signals from inside the wrapper lock, so the forwarded `requestCancel()`
always lands on the timer, and it lands before the timer starts unless the queue thread wins the
race. Observed failure rate about 1 in 20 on macOS; the ordering is pure C++ and Linux and Windows
are exposed too.

Production impact is broader than the retry wrapper: any timer task with a non-zero init delay that
is cancelled while still queued (for example during a shutdown that cancels a queue) completes only
after its full init delay.

### Why the proposed fix is sound

Fix: in `TimerTaskBaseT::scheduleTask()`, after `scheduleTimerInternal( getInitDelay() )`, re-check
the cancel flag and call `cancelTask()` if set.

- `scheduleTask()` runs under the timer's `m_lock` (held by `scheduleNothrow()`), and
  `requestCancel()` takes the same lock. A cancel is therefore either entirely before arming (flag
  set, seen by the new check) or entirely after (state `Running`, `cancelTask()` called directly).
  There is no third case.
- Cancelling a `deadline_timer` right after `async_wait` is normal asio usage. `onTimer()`
  (line 1770) already treats `operation_aborted` as the cancel path, calls `run()`, which returns
  `neg_infin`, and the task completes without an exception, same as a runtime cancel. Only timing
  changes; the observable completion semantics stay the same.
- `ObservableBaseT` ([ObservableBase.h:412-437](src/include/baselib/reactive/ObservableBase.h#L412-L437))
  never sets the cancel flag (its `requestCancel()` maps to `requestStop()`), so the new check is a
  no-op for reactive objects.
- No subclass of `TimerTaskBaseT` anywhere under `src/include` overrides `scheduleTask()`, so the
  fix cannot be bypassed.
- Agree with rejecting the alternatives: changing `requestCancelInternal()` touches every task
  type, and the wrapper cannot help because the cancel arrives after its lock is released.

Implementation detail: `m_cancelRequested` is private to `TaskBaseT` (line 525). The re-check must
use the public `isCanceled()` getter.

## Issue 2 — `remote_endpoint()` fails with EINVAL, not ENOTCONN, on macOS (confirmed, scope adjusted)

The observed log line `remote_endpoint: Invalid argument [system:22]` is the evidence: macOS/BSD
`getpeername` reports EINVAL for a socket whose connection has been torn down, where Linux reports
ENOTCONN. The guard in
[TcpBaseTasks.h:2497-2510](src/include/baselib/tasks/TcpBaseTasks.h#L2497-L2510) accepts only
`asio::error::not_connected`, so a normal early client disconnect propagates to
`BL_WARN_NOEXCEPT_END` at line 1994 and is logged as a multi-line warning.
[UtfMain.h:124](src/utests/include/utests/baselib/UtfMain.h#L124) turns every WARNING log line into
a Boost.Test error, which is why one event reads as "12 failures".

Differences from the proposal:

1. Widen `safeRemoteEndpointId`, not `safeRemoteEndpoint`. The retry loop in
   [NetUtils.h:68-117](src/include/baselib/core/NetUtils.h#L68-L117) exists to paper over a
   spurious ENOTCONN on a still-live socket (the RHEL5/6 note). Adding EINVAL there would make a
   genuinely dead connection burn 30 retries × 100 ms of blocking `os::sleep` before throwing, and
   `safeRemoteEndpointId` (line 133) would still rethrow. The gap that matters is at line 133:
   return the `<unknown_host_name>:<unknown_port>` placeholder for EINVAL as well, with no retry.
   Callers: `SimpleHttpTask.h:297`, `TcpBaseTasks.h:615`, `TcpSslBaseTasks.h:653`.
2. One identical site was missed:
   [TcpBlockTransferClient.h:2108-2124](src/include/baselib/messaging/TcpBlockTransferClient.h#L2108-L2124)
   (`createConnection()` on the block-transfer server) has the same ENOTCONN-only catch around
   `remote_endpoint()`. It runs right after `processConfiguredConnection()`, so the window is
   narrow, but it should be widened for consistency.

The `isErrorCondition` list at `TcpBaseTasks.h:189` covers socket I/O errors, not `getpeername`,
and should be left alone.

## Plan

Four production edits and one test. Each edit is logic-only, no reformatting.

1. [TaskBase.h](src/include/baselib/tasks/TaskBase.h) `TimerTaskBaseT::scheduleTask()` (~line 1817):
   after `scheduleTimerInternal( getInitDelay() )`, add `if( isCanceled() ) { cancelTask(); }` with
   a comment explaining that a pre-start cancel skipped `cancelTask()` because the state was not yet
   `Running`, and that `scheduleEvenIfAlreadyCanceled()` bypasses the generic abort.
2. [TcpBaseTasks.h:2499](src/include/baselib/tasks/TcpBaseTasks.h#L2499): accept
   `asio::error::invalid_argument` alongside `not_connected`, with a comment noting that macOS/BSD
   `getpeername` returns EINVAL for a disconnected socket.
3. [TcpBlockTransferClient.h:2114](src/include/baselib/messaging/TcpBlockTransferClient.h#L2114):
   same widening.
4. [NetUtils.h:133](src/include/baselib/core/NetUtils.h#L133) (`safeRemoteEndpointId` catch): same
   widening. Leave the retry loop at line 98 unchanged.
5. Regression test in [TestTasks3.h](src/utests/utf_baselib_tasks/TestTasks3.h), next to the case
   at line 1025: create a `SimpleTimerTask` with a 30 s init delay, call `requestCancel()` before
   `push_back`, flush, then assert elapsed is well under the delay, the callback never ran, and the
   task completed without an exception. This forces the `Created`-state cancel every run and fails
   on current code, so the fix is verified red/green rather than only statistically.

## Verification

- Build affected modules only, `-j1`, both `VARIANT=debug` and `VARIANT=release`:
  `utf_baselib_tasks`, `utf_baselib_messaging`, `utf_baselib` (NetUtils tests) and
  `utf_baselib_http` (uses `safeRemoteEndpointId`). This macOS host uses clang; Linux gcc/clang and
  Windows msvc/clang should be run when available, since issue 1 is platform-independent.
- Run the new deterministic test once on unfixed code (expect failure) and once on fixed code.
- Run the existing `TestTasks3.h:1025` case and the messaging suite in a loop (50 or more runs),
  since their original failure rate was about 5%.
- Run `Tasks_RetryableWrapperTaskCancelStressTests` and the full tasks/messaging suites, up to
  five test modules concurrently.
- Confirm no WARNING lines appear in the messaging suite output on macOS.
