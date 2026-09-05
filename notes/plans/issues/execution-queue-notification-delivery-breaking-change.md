# ExecutionQueue Notification Delivery: Breaking-Change Record

This document records a **breaking behavior change** to the `ExecutionQueueNotify` observer contract
that is invisible at compile time on the branch as originally written, the migration required of
downstream consumers, and the evidence that no in-repository observer is affected.

**Finding:** F-07 — "Completion correctness with concurrent notifications" (High),
`notes/plans/issues/pr-review-deep-dive-residual-findings.md:206-217`

**Analysis and decision:** `notes/plans/issues/execution-queue-notification-delivery-plan.md`

---

## The change

**Date introduced:** 2025-11-17, commit `188fe03` ("fix potential deadlock issue")
**Date surfaced at compile time:** this cycle
**Status:** Accepted as the intended contract; made loud rather than reverted

Prior to `188fe03`, `ExecutionQueueImpl::onReady()` held `m_lockEvents` at **function scope**. That
guard wrapped the `m_lock` state transition, `padExecutingQueueNothrow()`, `m_cvReady.notify_all()`
**and both observer callback invocations**. Observer callbacks for a single queue were therefore
mutually exclusive.

`188fe03` narrowed that guard to exclude the callbacks. **Observer callbacks for a single queue may
now execute concurrently on different threads.**

Two later commits are frequently mistaken for the source of the change and are not:

| Commit | What it actually did |
|---|---|
| `e60b3b3` | Added `m_activeWorkGeneration` / `m_lastPublishedGeneration` (the F-07a stale-`AllTasksCompleted` fix). |
| `fb85810` | Deleted the by-then **vestigial** `m_lockEvents` member and documented the concurrent contract. No behavior change. |
| `4f58080` | Documentation only — 120 insertions, **zero** executable lines. Its annotations concern *queue-state accessor* snapshot semantics (`size()`, `hasReady()`, `top()`, `pop()`), **not** observer thread safety. |

`notes/plans/issues/pr-review-deep-dive-residual-findings.md` attributes the change to `fb85810` and
describes `4f58080` as annotating "nine observers". Both are corrected there.

---

## Why it was not reverted

Restoring the previous behavior would restore a **real deadlock**, which is why `188fe03` exists: an
observer callback that re-enters the queue and blocks (`flush` / `wait` park on `m_cvReady` under
`m_lock`) can only be woken by another task's `onReady()`, which needed `m_lockEvents` — held by the
parked callback. Circular wait.

Two further points weigh against reversion:

- The previous behavior serialized `padExecutingQueueNothrow()`, i.e. an arbitrary user callback sat
  on the critical path of the queue's own **scheduling**.
- It never provided ordering. A single `std::mutex` has unspecified acquisition order, so delivery
  order between concurrently completing tasks was already arbitrary. Code that appeared to rely on
  ordering relied on nothing.

The property actually lost is **mutual exclusion of `onEvent`**, and nothing more.

---

## Why it is nonetheless a problem worth spending on

The break is **silent**. There is no compile error and no runtime diagnostic. A downstream observer
holding an unguarded counter, `std::vector`, or map now has a data race that will not reproduce
under light load.

Real downstream observers exist outside this repository.

---

## The resolution

Concurrent delivery is retained as the default, and the policy is made an **explicit, mandatory**
parameter of `ExecutionQueue::setNotifyCallback`:

```cpp
virtual void setNotifyCallback(
    SAA_in    om::ObjPtr< om::Proxy >&&                     notifyCB,
    SAA_in    const ExecutionQueueNotify::NotifyDelivery    delivery,
    SAA_in    const unsigned                                eventsMask = ExecutionQueueNotify::AllEvents
    ) = 0;
```

The parameter has **no default value**, so every existing call site fails to compile until it states
its policy. That converts a silent runtime break into a loud compile-time one. It is placed *before*
`eventsMask` so that `eventsMask` keeps its default and only one parameter becomes mandatory; both
legacy forms — `setNotifyCallback( p )` and `setNotifyCallback( p, AllEvents )` — fail to compile,
the latter because there is no implicit conversion from the anonymous `AllEvents` enumeration (or
from `int`) to `NotifyDelivery`.

`ExecutionQueueNotify::DeliverySerialized` restores mutual exclusion **without** restoring the
`188fe03` deadlock, because only the callback invocation is serialized: the queue's state transition
and `m_cvReady.notify_all()` both complete *before* the notification mutex is acquired, so a blocked
callback no longer prevents other completions from making progress and signalling waiters.

---

## Migration

One line per call site.

| Situation | Change |
|---|---|
| Observer is already thread-safe, or the callback body is trivial | Pass `ExecutionQueueNotify::DeliveryConcurrent`. Behavior is unchanged from the current branch. |
| Observer was written against the pre-`188fe03` contract and has not been audited | Pass `ExecutionQueueNotify::DeliverySerialized`. Behavior matches releases prior to `188fe03`. |

`DeliverySerialized` carries two documented hazards — a narrower deadlock restriction, and
thread-pool worker starvation. Both are stated in full on the enumeration in
`src/include/baselib/tasks/ExecutionQueueNotify.h`; read them before selecting it. The short form is:
choose it only for short, non-blocking observers.

---

## Exhaustiveness evidence: no in-repository observer is affected

`ExecutionQueueNotify` has no `std::function` or lambda registration path — every observer must
implement the interface — so the inventory below is closed by construction. All of `src/` was
searched, including `src/apps`, `src/local`, `src/utests` and `src/versioning`.

| # | Implementation | `onEvent` body | Safe under concurrent invocation? |
|---|---|---|---|
| 1 | `ExecutionQueueNotifyBase`, `ExecutionQueueNotifyBase.h:49` | `BL_UNUSED` — no-op | Trivially yes |
| 2 | `FanoutTasksObservableT`, `FanoutTasksObservable.h:306` | asserts only | Trivially yes |
| 3 | `FilesPkgUnpkgBase`, `FilesPkgUnpkgBase.h:240` | inherits #2's no-op; overrides only `maxReadyOrExecuting()`, which returns a constant | Trivially yes |
| 4 | `TcpServerBase`, `TcpBaseTasks.h:1743` | the only substantive one — mutates `m_activeEndpoints` | **Yes.** All four mutation sites (`:1766`, `:1822`, `:2148`, `:2160`) and both read sites (`:2193`, `:2209`) are under `TaskBase::m_lock`, acquired at `:1764` |
| 5 | `ExecutionQueueNotificationRecorder` (test), `TestTasks.h:857` | guarded `push_back`; hook copied under the lock and invoked outside it | Yes |

Only two of seventeen production queue-creation sites register an observer at all:
`FanoutTasksObservable.h:102` (`AllTasksCompleted` only, no-op body) and `TcpBaseTasks.h:1875`
(`AllEvents`, connection rate). Both pass `DeliveryConcurrent`, preserving current behavior exactly.

**The in-repository sweep is complete. The residual exposure is entirely external.**

---

## Second breaking change in the same workstream: `maxReadyOrExecuting()` sampling

`ExecutionQueueNotify::maxReadyOrExecuting()` used to be invoked **while `m_lock` was held**, from
the scheduling path, **once per scheduled task**. It is now sampled **exactly once**, inside
`setNotifyCallback()`, before the lock is taken, and the value is cached for the lifetime of that
registration.

This closes CXX-07: no *observer* code runs under `m_lock` any more. (Correction, 2026-09-05: three
other kinds of user code still do, by design, and are documented at their contracts rather than
here: `Task::scheduleTask()` overrides, invoked from `padExecutingQueueNothrow()` under `m_lock`,
continuation callbacks set with `setContinuationCallback()`, invoked from `onReady()` under
`m_lock` (both see `TaskBase.h`), and the `scanQueue()` callback (see `ExecutionQueue.h`). None
of them may call back into the owning queue.)

**Why it was safe to change:** every implementation in the tree returns a compile-time constant —
`0` (`ExecutionQueueNotifyBase.h:44`), `1024` (`FanoutTasksObservable.h:294`), `256`
(`FilesPkgUnpkgBase.h:240`). The pull mechanism existed to deliver a constant while paying a proxy
mutex acquisition plus a virtual call per scheduled task, inside the queue's global lock. Its twin
throttle in the very same condition, `m_maxExecuting`, has always been *pushed* through
`setThrottleLimit()`; there was no design reason for the asymmetry.

**What downstream must know:**

| | Before | Now |
|---|---|---|
| Sampling | Once per scheduled task | Once per `setNotifyCallback()` |
| A changing return value | Took effect on the next scheduling decision | **Never takes effect**; re-register to install a new limit |
| Observer proxy disconnected | Throttle silently removed (fell back to "no limit") | Cached limit **stays in effect** |
| Calling back into the queue from it | Deadlock — the queue lock was held | Safe; no queue lock is held |

An observer needing a genuinely dynamic limit must now call `setNotifyCallback()` again when the
limit changes. No in-tree observer does, and the previous contract forbade doing anything expensive
or re-entrant there in any case.

This change is **not** surfaced by a compile error — it is a silent semantic change for a dynamic
implementation. It is bundled here deliberately: the mandatory `delivery` parameter already forces
every downstream call site to re-read `setNotifyCallback()`, so this is the cheapest moment to make
it. Both halves are documented at `ExecutionQueueNotify::maxReadyOrExecuting()` and at
`ExecutionQueue::setNotifyCallback()`.

The `DeliverySerialized` documentation still notes that the notification mutex does not cover
`maxReadyOrExecuting()`, which remains true and worth stating.

**Test coverage:** this path previously had **none** — no test overrode `maxReadyOrExecuting()`.
`Tasks_ExecutionQueueThrottleFromObserverTest` now covers both halves of the contract.
