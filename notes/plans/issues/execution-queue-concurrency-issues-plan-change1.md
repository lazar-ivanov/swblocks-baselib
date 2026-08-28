# F-07a / Change 1: Truthful `AllTasksCompleted` generation validation

## Summary and contract

Change 1 repairs the meaning of `AllTasksCompleted`; it does not redesign scheduling or serialize callbacks.

After this change, an `AllTasksCompleted` notification means:

- At a defined validation point immediately before dispatch, there were no pending or executing tasks.
- No scheduled work had been admitted or rescheduled since that completion candidate was created.
- A completion candidate is consumed at most once for its active-work generation.
- A producer may enqueue work after validation, including before or during the callback. This is valid under the point-in-time contract, not an unresolved race.
- Retained or `dontSchedule=true` ready tasks continue to be excluded from the existing completion condition.
- Callbacks may overlap or reorder under the intentionally concurrent notification contract.

The public callback signature, event IDs, queue interface, and ABI remain unchanged.

## Implementation changes

### Work-generation state

In [ExecutionQueueImpl.h](/home/lazar/dev/github/swblocks-baselib/src/include/baselib/tasks/ExecutionQueueImpl.h:212):

- Include `<cstdint>` directly and add:
  - `std::uint64_t m_activeWorkGeneration`;
  - `std::uint64_t m_lastPublishedGeneration`.
- Initialize both to zero.
- Protect both exclusively with `m_lock`.
- The first scheduled admission advances the active generation to one, so zero can represent “no candidate” locally if useful.

Advance `m_activeWorkGeneration` exactly once after work successfully enters a new scheduled execution attempt:

- A newly added task enters pending state with `dontSchedule=false`.
- A retained ready task is moved back to pending.
- A non-self continuation is admitted through `pushInternalNoLock`.
- A self-continuation moves the executing task back to pending.

Do not advance it for:

- `dontSchedule=true`;
- prioritization or reordering within pending;
- pending-to-executing movement;
- keep/discard processing;
- pending or executing cancellation by itself;
- option, throttle, or observer changes.

Place the increment after the relevant structural transition has succeeded and before `padExecutingQueueNothrow()`. Do not increment in public push wrappers, avoiding double counting.

### Candidate creation in `onReady`

Refactor [onReady](/home/lazar/dev/github/swblocks-baselib/src/include/baselib/tasks/ExecutionQueueImpl.h:365) while preserving continuation, retention, scheduling, and condition-variable behavior:

1. Acquire `m_lock` for the task state transition and candidate creation.
2. In that `m_lock` scope:
   - perform the task state transition;
   - snapshot `TaskReady` or `TaskDiscarded` as today;
   - call `padExecutingQueueNothrow()` as today;
   - after padding, if pending and executing are empty, record a local all-complete candidate and its current generation.
3. Do not resolve or snapshot the `AllTasksCompleted` callback there.
4. Remove the current second `m_lock` scope that pre-captures `AllTasksCompleted`.
5. Release `m_lock` and invoke the task-event callback.

Use an explicit local candidate flag plus candidate generation rather than relying solely on a sentinel value.

### Candidate validation and publication

After the task-event callback returns:

1. If no candidate was created, return without further completion processing.
2. Acquire only `m_lock`.
3. Accept the candidate only when:
   - `m_pending` and `m_executing` are still empty;
   - `m_activeWorkGeneration` equals the candidate generation;
   - `m_lastPublishedGeneration` differs from the candidate generation.
4. When accepted:
   - set `m_lastPublishedGeneration` before dispatch;
   - resolve the current `AllTasksCompleted` observer and event mask with `getEventNotifyCB`;
   - mark the generation published even if the mask excludes the event or the notification proxy is disconnected.
5. Release `m_lock`.
6. Invoke the resolved callback, if any, with no internal queue mutex held.

Do not test `m_observerThis` during delivery validation:

- `m_observerThis` protects late task-to-queue completion calls; it is not the registered notification target.
- Queue disposal alone must not revoke a completion candidate already committed by `onReady`, preserving current and `master` behavior.
- Explicit disconnection of `m_notifyCB`, or replacement through `setNotifyCallback`, controls whether and where completion is delivered.

### Generation rather than an idle boolean

Use generations rather than a single “idle already published” flag because the candidate must remain associated with the work version that created it.

If A’s candidate is waiting, B is admitted and completes, and B’s task callback has not yet published its candidate, a boolean could allow A’s old path to publish. Generation comparison rejects A regardless of whether B has already published.

The generation remains the candidate identity used to reject stale or duplicate completion publication.

## Public documentation

Update [ExecutionQueueNotify.h](/home/lazar/dev/github/swblocks-baselib/src/include/baselib/tasks/ExecutionQueueNotify.h:64) to state:

- `AllTasksCompleted` is a point-in-time notification that pending and executing were empty during final validation.
- Scheduled work admitted since candidate creation invalidates that candidate.
- Work may be enqueued concurrently after validation.
- Ready/retained tasks do not prevent this event.
- Becoming empty solely because pending work is canceled or discarded by a flush does not itself originate this event; completion publication remains driven by task readiness processing.
- No internal queue mutex is held during `onEvent`.
- Callbacks from the same queue may overlap and delivery order is unspecified, including across repeated attempts of one retained task.
- Observers must be thread-safe, callback thread identity is unspecified, and `AllTasksCompleted` is not a notification-drain barrier.
- A task-event callback is snapshotted when its state transition commits; the all-complete observer is resolved at final delivery validation.

Do not document or imply permanent quiescence, callback serialization, queue closure, or a producer barrier.

## Test plan

Add a thread-safe notification recorder and deterministic synchronization helpers to [TestTasks.h](/home/lazar/dev/github/swblocks-baselib/src/utests/utf_baselib_tasks/TestTasks.h):

- Derive the recorder from the existing notification base and connect it through `ProxyImpl`.
- Record event ID and task under the recorder’s own mutex.
- Release the recorder mutex before executing test hooks, because hooks may call queue operations.
- Store controllable completion callbacks in scheduling order so multiple outstanding callbacks cannot overwrite one another.
- Avoid test assertions from completion threads; record outcomes and assert from the test thread.
- Use condition variables and controllable completion tasks. Sleeps must not determine correctness; timeouts are failure ceilings only.
- Use `os::chrono` durations with `os::condition_variable`, and use `cpp::function` for the recorder hook.
- If the blocking re-entry failure path must detach a completion thread to avoid hanging the test process, keep every captured object under shared ownership and document why detachment is safe and necessary.
- Where necessary, use a controllable task whose completion method returns only after its queue `onReady` path returns.

Add these focused cases:

1. **Re-entrant `TaskReady`**
   - Configure `KeepAll`.
   - A’s `TaskReady` callback schedules B.
   - Hold B active long enough to prove A’s candidate does not publish.
   - Complete B and assert exactly one final `AllTasksCompleted`.

2. **Re-entrant `TaskDiscarded`**
   - Configure `KeepNone`.
   - A’s `TaskDiscarded` callback schedules B, reproducing the TCP-handshake promotion pattern.
   - Assert no stale completion for A and exactly one completion after B.

3. **Obsolete candidate after completed intervening work**
   - Block A inside its task-event callback.
   - Admit and complete B, then block B inside its task-event callback.
   - Release A and wait until A’s complete `onReady` path has returned.
   - Assert no all-complete event: A’s generation must be rejected even though the queue is empty again.
   - Release B and assert exactly one all-complete event.
   - This specifically distinguishes generation validation from an idle-published boolean.

4. **Blocking re-entry without the `master` deadlock**
   - From A’s task-event callback, wait for B using a queue operation.
   - Complete B from another thread.
   - Assert both operations return before the deadline.
   - Verify by source inspection that every callback is dispatched without a queue mutex; the timed re-entry test exercises the deadlock-sensitive behavior rather than claiming to observe every mutex directly.

5. **Current observer selection**
   - Replace the notification target from A’s task callback; assert the new target receives all-complete and the old target does not.
   - Disconnect the notification proxy from A’s task callback; assert no all-complete call occurs, while the generation is still consumed.

6. **Disposal compatibility**
   - Dispose the queue from A’s task callback without disconnecting the notification proxy.
   - Assert the already committed, still-valid completion candidate is delivered to the connected target.
   - This locks in the distinction between queue disposal and notification-proxy disconnection.

7. **Admission-site coverage**
   - Reschedule a retained ready task and observe a later completion generation.
   - Exercise a self-continuation and assert completion only after the final execution attempt.
   - Exercise a non-self continuation and assert no intermediate completion between the original and continuation tasks.
   - From a task-event callback, add a new task with `dontSchedule=true`; assert the ready-only task neither invalidates the candidate nor prevents exactly one all-complete notification.
   - Review prioritization, padding, and cancellation paths to verify they do not increment the generation. These exact counter values are internal review invariants rather than focused behavioral cases.

Do not add FIFO-order, dispatcher-thread, or maximum-callback-concurrency assertions. Existing cancellation and general option tests remain the regression coverage for cancellation behavior.

## Verification and acceptance

- Build the focused target with `make -k -j1 utf_baselib_tasks`.
- Run the new focused test cases, then the complete `test_utf_baselib_tasks` target.
- Run the complete repository suite with `make -k -j6 test` after the focused task suite passes.
- Run the focused tests under `BL_CLANG_ENABLE_RA_TSAN=1` when using the repository’s supported Clang/devenv configuration. If unavailable on the current platform, record the unsupported/skipped run rather than treating the whole repository as TSan-clean.
- Confirm the helper code remains C++11-only, uses `os::chrono` rather than naming `std::chrono` directly, and does not depend on a direct `<chrono>` include.
- Confirm the controllable completion helper consumes callbacks in scheduling order without overwriting an outstanding callback.
- Review every scheduled-admission path to confirm exactly one generation increment.
- Confirm `getEventNotifyCB(AllTasksCompleted, ...)` is called only during final validation, never before the task callback.
- Confirm every `onEvent` invocation occurs without `m_lock`.
- Confirm no public signature, event value, scheduler behavior, task ownership, or cancellation policy changes.

Acceptance requires all new deterministic tests and the existing task suite to pass, with no stale or duplicate completion for an invalidated generation.

## Boundaries and estimate

- Callback serialization/FIFO is intentionally not planned; concurrent, unordered notification delivery is the documented contract.
- CXX-07 public accessor/configuration races and `maxReadyOrExecuting()` under-lock callback behavior remain separate.
- No changes are required in `TcpBaseTasks.h` or `FanoutTasksObservable.h`; `ExecutionQueue.h` points callback registrants to the notification contract.
- Estimated Change 1 size: approximately 40–80 production/documentation lines touched, with a smaller net increase, plus approximately 900–1,000 test/support lines touched for deterministic concurrency helpers and focused cases.
- Estimated effort: 1–2 engineering days plus cross-platform and sanitizer stabilization.
