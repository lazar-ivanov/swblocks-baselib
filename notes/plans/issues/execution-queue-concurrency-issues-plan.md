# F-07: Completion correctness with concurrent notifications

## Summary

- `ExecutionQueue` itself is not fundamentally broken: internal queue mutations remain protected by `m_lock`.
- The stale `AllTasksCompleted` defect is confirmed and deterministic, including the TCP handshake path that replaces a discarded handshake task with a real connection task.
- No current in-repo shutdown transition depends on this event; observed internal impact is primarily a false TCP “no connections” log.
- Repair the stale completion defect with generation validation while deliberately retaining the branch's concurrent notification delivery.
- Do not implement the previously proposed F-07b FIFO dispatcher. Repository inspection found no production observer that depends on queue-provided callback serialization or commit-order delivery.
- Preserve all public types, event IDs, and callback signatures, but explicitly document the concurrent notification contract.

## Change 1 — F-07a: Completion generation and delivery validation

Modify [ExecutionQueueImpl.h](/home/lazar/dev/github/swblocks-baselib/src/include/baselib/tasks/ExecutionQueueImpl.h:365) without redesigning notification dispatch:

- Add a 64-bit active-work generation and last-published generation, both protected by `m_lock`.
- Advance the generation whenever work successfully enters a new scheduled execution attempt:
  - new scheduled task;
  - retained ready task rescheduled;
  - non-self continuation admitted;
  - self-continuation rescheduled.
- Do not advance it for `dontSchedule=true`, prioritization, or pending-to-executing movement; these do not begin a new active-work admission.
- When `onReady` leaves pending and executing empty, capture only an all-complete candidate containing the current generation—not the callback.
- Invoke the task notification with no internal mutex held.
- Then, under `m_lock`, publish the candidate only if:
  - pending and executing are still empty;
  - the active generation still matches;
  - that generation has not already been published.
- Mark the generation published and resolve the current all-complete observer/mask under the lock, then invoke it after releasing the lock. Mark it published even when no observer is currently connected, preserving non-retroactive notification behavior.
- Retain the branch’s concurrent task-callback behavior.
- Preserve the existing improvement over `master`: no user `onEvent` callback runs under either queue mutex.

## Notification-delivery decision — do not implement F-07b

> **Superseded in part.** The rejection of the F-07b FIFO dispatcher stands. The decision below to
> leave concurrent delivery as the *only* available behavior does not: see
> `execution-queue-notification-delivery-plan.md`, which keeps concurrent delivery as the default but
> adds an opt-in `ExecutionQueueNotify::DeliverySerialized` mode and makes the choice a mandatory
> parameter of `setNotifyCallback`. The breaking-change record is in
> `execution-queue-notification-delivery-breaking-change.md`.

Adopt concurrent, unordered notification delivery as the intentional public contract:

- Callbacks from one queue may execute concurrently on different threads.
- Observers are responsible for synchronizing shared state and must be safe for concurrent invocation.
- No delivery order is guaranteed between callbacks produced by different task completions or by repeated execution attempts of the same retained task.
- `AllTasksCompleted` is a queue-state notification, not a notification-drain barrier; previously started callbacks may still be executing when it is delivered.
- Callback thread identity is unspecified.
- Continue to snapshot `TaskReady` and `TaskDiscarded` observers when their state transitions commit, and resolve `AllTasksCompleted` against the current observer during final validation.
- Do not add a FIFO, dispatcher, worker thread, strand, producer barrier, callback mutex, or callback-order sequence field.

After F-07a candidate creation moved into the same `m_lock` scope as its state transition, `m_lockEvents` no longer protects any state or callback behavior beyond `m_lock`. Remove the redundant member and acquisition rather than retain a vestigial mutex on every completion path.

## Public contract

Clarify in [ExecutionQueueNotify.h](/home/lazar/dev/github/swblocks-baselib/src/include/baselib/tasks/ExecutionQueueNotify.h:64):

- `AllTasksCompleted` means there were no pending or executing tasks at a linearization point immediately before dispatch.
- A producer may enqueue work after that point, including during the callback.
- No queue mutex is held while `onEvent` runs.
- Callbacks from the same queue may overlap, and observers must be thread-safe.
- Delivery order is unspecified across completion paths, including repeated attempts of one retained task.
- `AllTasksCompleted` does not imply that earlier notification callbacks have returned.
- Callback thread identity must not be relied upon.
- Retained ready tasks remain excluded from the existing all-complete condition.

## Test plan

Add deterministic C++11 gate/condition-variable tests to [TestTasks.h](/home/lazar/dev/github/swblocks-baselib/src/utests/utf_baselib_tasks/TestTasks.h), using timeouts only as failure ceilings:

- F-07a:
  - `TaskReady(A)` enqueues B; only B’s final generation publishes completion.
  - `TaskDiscarded(A)` enqueues B, covering the TCP-handshake pattern.
  - B is admitted and completes while A’s task callback is blocked; the old candidate is dropped and exactly one completion is published.
  - A task callback waits for B; both finish before a deadline, proving the `master` deadlock is not restored.
  - A task callback replaces or disconnects the observer; completion uses the current target.
  - Exercise retained-task rescheduling, self/non-self continuation, and `dontSchedule=true` admission.
  - Treat prioritization, padding, and cancellation generation accounting as source-review invariants; existing cancellation and option tests provide regression coverage.

- Verification:
  - Build and run focused tests, then the complete `utf_baselib_tasks` suite.
  - Run the focused notification tests with `BL_CLANG_ENABLE_RA_TSAN=1`.
  - Confirm `m_lockEvents` has no remaining declaration, acquisition, or documentation reference in `ExecutionQueueImpl`.
  - Do not require the entire repository to be TSan-clean in this chunk; known CXX-07 accessor races remain separately tracked.

## Assumptions and boundaries

- CXX-07 synchronization of `hasReady`, `hasExecuting`, `hasPending`, `size`, configuration, local-pool access, and `maxReadyOrExecuting()` is explicitly excluded.
- Serialized callback delivery on `master` was observable but undocumented. Omitting F-07b deliberately changes that behavior; external observers must follow the newly explicit concurrent-callback contract.
- No scheduler, task ownership, cancellation, continuation, or public ABI redesign is included.
