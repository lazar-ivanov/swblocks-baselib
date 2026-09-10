# AsyncExecutor Task Lock / Queue Lock Inversion: Record and Suppression

This document records why the ThreadSanitizer `lock-order-inversion` reports produced by
`AsyncExecutorImpl` are not a reachable deadlock, and why the decision was to suppress them
rather than to restructure the code.

**Origin:** finding **A-12** of the whole-library C++ review
(`notes/reviews/major/update_2026/whole-library-cxx-review-fable51.md`, section A-12; the
decision extract carries the same item). The report class itself is older: it is the
"255 reports / a separate decision" item in
`notes/plans/issues/pr-review-fable51-residual-findings-status.md`.

**Decision date:** 2026-09-06. **Status:** no code change; suppressed and recorded.

---

## What ThreadSanitizer reports

```
WARNING: ThreadSanitizer: lock-order-inversion (potential deadlock)
  Cycle in lock order graph: M0 => M1 => M0
```

The two edges are:

1. **Executor task lock → execution queue lock.**
   `AsyncExecutorImpl.h`, `ExecutorTaskT::scheduleAsyncCall` posts the completion callback
   (`postCompletionCallback` → `m_completionTasksQueue -> push_back`) while the caller holds
   `base_type::m_lock` (the executor task lock) - it is called from `onOperationTaskReady`,
   `requestCancel`, `requestNewAsyncCall` and `scheduleCall`, all of which run under that lock.
   `push_back` takes the queue lock and, through `padExecutingQueueNothrow`, the lock of the
   task it schedules.

2. **Completion task lock → executor task lock.**
   `TaskBase.h`, `SimpleTaskWithContinuation::onExecute` holds the lock of the completion
   `SimpleTask` while it invokes the bound callback, which is `ExecutorTaskT::onExecute`; that
   acquires `m_executeLock` and then the executor task lock.

## Why the cycle cannot close

The mutex at the far end of edge 1 belongs to the task the queue has just moved from **Ready**
into the pending/executing queue. A task in Ready has already returned from its `onExecute`
and therefore released its own lock.

The mutex at the near end of edge 2 is held only while that same task is **Executing**.

One `TaskInfo` cannot be Ready and Executing at the same time, so for any single mutex instance
the two edges are never held simultaneously and no thread can wait on the other. ThreadSanitizer
reports the cycle because the completion tasks are pooled and reused - the completion queue is
created with `ExecutionQueue::OptionKeepAll` - so the tool observes the two orders across
*historical* acquisitions of the same mutex object rather than across two live acquisitions.

## Why the code was not restructured

The review's suggested fix was to split `scheduleAsyncCall` into an accounting part under the
lock and a post part after the guard, in `onOperationTaskReady` and `requestNewAsyncCall`. A
source check while planning the work showed this does **not** remove the edge: `scheduleAsyncCall`
is also reached from `scheduleCall` (called under the task lock from `TaskBase::scheduleNothrow`,
which itself holds the task lock) and from `requestCancel`, where moving the post out of the
critical section would either reintroduce the same edge or change the cancellation ordering the
executor depends on. The restructure would therefore cost the same critical sections and still
leave the report.

The decision (2026-09-06, with the user) was therefore: **suppression plus this record**, no code
change.

## The suppression

`projects/make/toolchain/tsan-suppressions.txt`:

```
deadlock:bl::detail::AsyncExecutorImplT*::ExecutorTaskT*::scheduleAsyncCall
```

It is wired in through the `TSAN_OPTIONS` export in `projects/make/toolchain/clang-analysis.mk`
(the TSAN section), so any target built and run through the makefiles with
`BL_CLANG_ENABLE_RA_TSAN=1` picks it up. When a test binary is launched by hand, set

```
TSAN_OPTIONS=second_deadlock_stack=1:suppressions=<repo>/projects/make/toolchain/tsan-suppressions.txt
```

The suppression is deliberately anchored on `scheduleAsyncCall`, the only function that creates
edge 1, so an inversion introduced anywhere else - including elsewhere in `AsyncExecutorImpl` -
is still reported.

## When to revisit

- If `scheduleAsyncCall` ever posts to a queue whose tasks can be Ready and Executing at once
  (for example if the completion queue stops being a plain execution queue of one-shot tasks),
  the argument above no longer holds and the suppression must be removed.
- If the executor's completion tasks stop being pooled (`OptionKeepAll` dropped), the reports
  should disappear on their own; the suppression should then be removed rather than left in place.
- A real deadlock in this area would show as a hang, not as a TSan report; the suppression hides
  the report, not the hang.

## A second, unrelated report class seen during verification (not suppressed)

`utf_baselib_tasks` under TSan reports 217 lock-order-inversions without the suppression and 78
with it. The remaining 78 are **not** the `AsyncExecutor` cycle: they are
`ExternalCompletionTaskIfT::onExecute` (holding its own task lock) → `ExecutionQueue::push_back`
→ `padExecutingQueueNothrow` → the lock of the task being scheduled - structurally the same
"task lock held across a queue push" pattern, at a different site.

This class is **pre-existing** (`ExternalCompletionTask` was not touched by the whole-library
review work), is **not** covered by any row of the review, and is deliberately **not** suppressed:
the suppression above is anchored on `scheduleAsyncCall` precisely so that other sites keep being
reported. Whether it can close has not been analysed. It is recorded here so the next person
running TSan on `utf_baselib_tasks` knows the expected residue and does not mistake it for the
`AsyncExecutor` item.

## Verification

`make -k -j1 utf_baselib_async TOOLCHAIN=clang2010 VARIANT=debug BL_CLANG_ENABLE_RA_TSAN=1
BL_CLANG_ENABLE_RA_FORCE_O1=1` and the module run: with the suppression in place the
`AsyncExecutor` inversion reports are gone and no other report takes their place. The same holds
for `utf_baselib_tasks`, which produced 217 reports of this class before the suppression.
