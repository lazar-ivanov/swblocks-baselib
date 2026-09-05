# F-07 residual — ExecutionQueue notification serialization: analysis, options, decision

## Context

`pr-review-deep-dive-residual-findings.md` classifies **F-07** as *"Mostly closed — residual is a
deliberate contract change"*. The residual is this: the deep-dive review asked for a **serialized
notification queue** so observer callbacks never overlap. The branch deliberately went the other
way and adopted **concurrent, unordered callbacks** as the public contract
([ExecutionQueueNotify.h:82-91](src/include/baselib/tasks/ExecutionQueueNotify.h#L82-L91)).

That is a **silent, runtime-only behavior change from `master`** — no compile error, no diagnostic.
Downstream observers written against `master` may now race. The findings doc names one open work
item: *"Worth confirming that [thread-safety annotation] sweep was exhaustive — that is the only
F-07 work I would still consider."*

**Confirmed for this decision: real downstream observers exist outside this repository.** That is
what makes the silent break worth spending days on, and it drives the two shape decisions below —
the escape hatch (Option B) and, critically, the fact that the new policy parameter is
**mandatory**, so downstream fails to compile and is forced to read the contract rather than
silently inheriting concurrent delivery.

This document answers three things you asked for: (1) whether the design is right at all, (2) what
each way of addressing it costs in size / complexity / risk / blast radius, and (3) the performance
impact.

---

## Part 1 — Two corrections to the findings doc

Both matter for the decision, so they are stated up front.

### 1a. `fb85810` did not change the behavior — `188fe03` did

The findings doc says *"`fb85810` removed the events lock"*, implying that commit is the behavior
change. Verified against git, the sequence is:

| Commit | What it actually did |
|---|---|
| `master` | `BL_MUTEX_GUARD( m_lockEvents )` at **function scope** of `onReady()`, wrapping the `m_lock` state transition **and** `padExecutingQueueNothrow()` **and** `m_cvReady.notify_all()` **and** both callback invocations. |
| `188fe03` "fix potential deadlock issue" (2025-11-17) | **This is the behavior change.** Narrowed `m_lockEvents` to exclude the callbacks. Callbacks became concurrent here. |
| `e60b3b3` | Added `m_activeWorkGeneration` / `m_lastPublishedGeneration` (F-07a). |
| `fb85810` | Deleted the by-then **vestigial** mutex + documented the contract. |
| `4f58080` | Doc-only, 120 insertions, **0 code lines**. |

So the contract change is ~9 months old on this branch and was motivated by a **real deadlock**, not
by a performance desire. That reframes the question: reverting is not "undoing a recent
simplification", it is "reintroducing a bug that was deliberately fixed".

### 1b. `master`'s guarantee was weaker than it looks — and the review asked for *more* than `master`

`master` held one `std::mutex` across the callbacks. That buys **mutual exclusion**. It does **not**
buy meaningful ordering: `std::mutex` acquisition order is unspecified, so among concurrently
completing tasks the delivery order on `master` was already arbitrary — not completion order, not
push order. Any downstream code that "relied on ordering" on `master` relied on nothing.

The review's ask (a *serialized notification queue* / FIFO dispatcher) would deliver **strictly more
than `master` ever did**. That is scope creep relative to the compatibility argument that motivates
it.

**Net: the only property actually lost versus `master` is mutual exclusion of `onEvent`.** That is
the entire blast radius, and it is the only thing worth considering restoring.

---

## Part 2 — The findings doc's one open work item is already closeable

*"Confirm that sweep was exhaustive."* Answered by full-repo inventory. Two facts:

**The `4f58080` annotations are not about observers at all.** It touched nine *files*, and every
annotation concerns *queue-state accessors* (`isEmpty()`/`size()`/`hasReady()`/`top()`/`pop()`)
being point-in-time snapshots — see [ExecutionQueue.h:61-88](src/include/baselib/tasks/ExecutionQueue.h#L61-L88).
It was never an observer thread-safety sweep. The observer contract came from `fb85810`
(`ExecutionQueueNotify.h`) and `055eb55` (the `maxReadyOrExecuting()`-under-lock warning).

**There are exactly five `ExecutionQueueNotify` implementations in the entire repo,** and there is
no `std::function`/lambda registration path — every observer must implement the interface, so the
inventory is closed by construction:

| # | Implementation | `onEvent` body | Concurrency-safe? |
|---|---|---|---|
| 1 | `ExecutionQueueNotifyBase` [:49](src/include/baselib/tasks/ExecutionQueueNotifyBase.h#L49) | `BL_UNUSED` — no-op | Trivially yes |
| 2 | `FanoutTasksObservableT` [:306](src/include/baselib/reactive/FanoutTasksObservable.h#L306) | asserts only | Trivially yes |
| 3 | `FilesPkgUnpkgBase` [:240](src/include/baselib/transfer/FilesPkgUnpkgBase.h#L240) | inherits #2's no-op; overrides only `maxReadyOrExecuting()` → constant | Trivially yes |
| 4 | `TcpServerBase` [:1743](src/include/baselib/tasks/TcpBaseTasks.h#L1743) | **the only substantive one** — mutates `m_activeEndpoints` | **Yes** — all four mutation sites (`:1766`, `:1822`, `:2148`, `:2160`) and both read sites (`:2193`, `:2209`) are under `TaskBase::m_lock`, taken at [:1764](src/include/baselib/tasks/TcpBaseTasks.h#L1764) |
| 5 | `ExecutionQueueNotificationRecorder` (test) [:857](src/utests/utf_baselib_tasks/TestTasks.h#L857) | guarded `push_back`, hook copied under lock and invoked outside | Yes, correctly |

Only two of seventeen production queue-creation sites register an observer at all:
[FanoutTasksObservable.h:102](src/include/baselib/reactive/FanoutTasksObservable.h#L102)
(`AllTasksCompleted` only, no-op body) and
[TcpBaseTasks.h:1875](src/include/baselib/tasks/TcpBaseTasks.h#L1875) (`AllEvents`, connection rate).

**Conclusion: the in-repo sweep is complete and no in-repo observer is unsafe.** The residual is
*entirely* a question about code that does not live in this repository.

---

## Part 3 — Is the design right? Would serialization be limiting?

You asked directly. My assessment: **the branch's choice is correct for this component**, with three
qualifications.

### Why concurrent delivery is the right default

1. **Layering.** `ExecutionQueue` is a *scheduler*, not an event bus. Serialization is a property an
   observer can add in three lines (`BL_MUTEX_GUARD` in `onEvent`) but **cannot remove** if the queue
   imposes it. Push the policy to the party that knows whether it needs it.
2. **It matches the substrate.** This is a Boost.Asio library. Asio's own model is exactly this:
   handlers run concurrently by default, serialization is opt-in via a strand. Imposing a
   queue-wide callback mutex would be idiomatically inconsistent with everything around it.
3. **`master`'s serialization was on the critical path of *scheduling*.** `m_lockEvents` wrapped
   `padExecutingQueueNothrow()`, which is what schedules the *next* tasks. So an arbitrary user
   callback was, on `master`, a serialization point for the queue's own forward progress. That is a
   genuine design defect, not a feature.
4. **It caused a real deadlock**, which is why `188fe03` exists: a callback that re-enters the queue
   and blocks (`flush`/`wait` park on `m_cvReady`) can only be woken by another task's `onReady()`,
   which needed `m_lockEvents`, held by the parked callback. Circular wait. The repo now has a
   regression test pinning this: `Tasks_ExecutionQueueNotificationBlockingReentryTest`
   ([TestTasks.h:1156](src/utests/utf_baselib_tasks/TestTasks.h#L1156)).
5. **The ordering nobody had.** Per Part 1b, `master`'s order was arbitrary. There is no "ordering
   contract" being broken.

### The valid rationale on the other side — steelmanning the review

These are real and should not be dismissed:

1. **The break is silent.** No compile error. A downstream observer with an unguarded counter or
   `std::vector` now has a data race with no diagnostic. This is the single strongest argument, and
   the branch has done nothing about it beyond header comments.
2. **Safe-by-default is a reasonable library posture.** `onEvent` is `NOEXCEPT` and virtual —
   ordinary users write these. Requiring every author to reason about up-to-32-way concurrency is a
   footgun.
3. **Retained-task event ordering is now genuinely unspecified.** With `OptionKeepAll` and a
   re-pushed task, the second `TaskReady` for the *same task* can be delivered before the first is
   processed. `ExecutionQueueNotify.h:84-86` documents this away, but it is a semantic hazard a
   user could plausibly not anticipate.
4. **The contract is internally inconsistent.** `onEvent` runs with **no** lock;
   `maxReadyOrExecuting()` runs **under `m_lock`**
   ([ExecutionQueueImpl.h:293](src/include/baselib/tasks/ExecutionQueueImpl.h#L293), reached from
   [:533](src/include/baselib/tasks/ExecutionQueueImpl.h#L533)). Two virtuals on one interface with
   opposite lock disciplines is confusing and, per CXX-07, is itself an open finding.

### My conclusion

Serializing **would** be limiting — but only for queues that have an observer, and the limit is
sharper than it first appears (Part 5): it converts the observer's callback duration into a hard,
core-count-independent throughput ceiling for that queue, and parks thread-pool workers, degrading
*unrelated* queues sharing the pool. That is a bad property to impose unconditionally on a
scheduler.

**But the compatibility break is real, silent, and currently has no escape hatch.** The right
resolution is not "serialize" or "don't serialize" — it is: keep concurrent delivery as the
default, and make the break *recoverable* and *disclosed*.

---

## Part 4 — Options

Blast radius is small for every option: `ExecutionQueue.h`, `ExecutionQueueImpl.h`,
`ExecutionQueueNotify.h`, `TestTasks.h`. Nothing outside `src/include/baselib/tasks/` needs to
change, because no in-repo observer needs a fix.

### Option A — Close it: disclose, don't code

Record the Part-2 inventory as the exhaustiveness evidence, add a **tracked** breaking-change /
migration entry (the contract currently lives only in header comments — a downstream integrator
recompiling will never see it), and fix the two inaccuracies in the findings doc.

- **Size:** doc only. ~1 new file in `notes/`, small edits to `pr-review-deep-dive-residual-findings.md`.
- **Cost:** ~0.25 day.
- **Complexity:** none.
- **Risk / blast radius:** zero. No code changes.
- **Performance:** zero.
- **Leaves open:** a downstream user who needs exclusion must write their own mutex, and must first
  discover that they need to.

### Option B — Option A + opt-in serialization, made **mandatory and loud** (**recommended**)

Add a **non-defaulted** policy parameter to `setNotifyCallback`, and one `m_lockNotify` mutex taken
*only* inside the two already-existing `if( onNotify )` branches.

**Decision taken: the default behavior stays concurrent (today's contract), but the parameter has no
default value.** Every call site — in-repo and downstream — must state its policy explicitly and
**fails to compile until it does**. That converts the silent runtime break into a loud compile-time
one, which is the whole point given real downstream observers exist.

Use a **named enum, not a bare bool.** A `true`/`false` at a call site is the opposite of loud, and
the repo's `false /* dontSchedule */` comment convention is not enough for a policy that carries
deadlock and thread-starvation ramifications. Nest it next to `EventId` for consistency
([ExecutionQueueNotify.h:41-51](src/include/baselib/tasks/ExecutionQueueNotify.h#L41-L51)):

```cpp
            enum NotifyDelivery
            {
                DeliveryConcurrent,             // today's contract: callbacks may overlap
                DeliverySerialized,             // mutually exclusive callbacks; read the hazards
            };
```

**Put the policy parameter *second*, before `eventsMask`**, so `eventsMask` keeps its existing
default and only one parameter becomes mandatory:

```cpp
virtual void setNotifyCallback(
    SAA_in    om::ObjPtr< om::Proxy >&&                     notifyCB,
    SAA_in    const ExecutionQueueNotify::NotifyDelivery    delivery,
    SAA_in    const unsigned                                eventsMask = ExecutionQueueNotify::AllEvents
    ) = 0;
```

This is deliberate and matters mechanically: a trailing non-defaulted parameter would have forced
removal of the `eventsMask` default too, churning every call site twice. With this order, both
legacy forms — `setNotifyCallback( p )` and `setNotifyCallback( p, AllEvents )` — fail to compile.
The second one fails because there is no implicit conversion from the anonymous `AllEvents` enum (or
from `int`) to `NotifyDelivery`, so there is no silent-acceptance hole.

- **Size:** ~40 added lines across 3 headers, 12 small hunks, no function rewritten, plus 5 call-site
  updates and ~4 tests.
- **Cost:** ~2.5 days (0.5 impl, 0.5 docs — the hazard text in Part 6 is the bulk, 1.0 tests,
  0.5 build/review), +0.75 if the microbenchmark from Part 5 is built.
- **Complexity:** low. One new member, one new 12-line helper, three argument additions in `onReady`.
- **Risk:** low. Changes a pure virtual signature, which breaks *implementers* — there is exactly one
  ([ExecutionQueueImpl.h:116](src/include/baselib/tasks/ExecutionQueueImpl.h#L116)) and the library
  is header-only, so there is no binary ABI to preserve. Downstream breakage is intentional,
  compile-time, and mechanically fixable in one line per call site.
- **In-repo call-site churn:** exactly five —
  [FanoutTasksObservable.h:102](src/include/baselib/reactive/FanoutTasksObservable.h#L102),
  [TcpBaseTasks.h:1875](src/include/baselib/tasks/TcpBaseTasks.h#L1875), and
  `TestTasks.h:927/1284/1340`. All five pass `DeliveryConcurrent`, preserving today's behavior
  exactly.
- **Blast radius at runtime:** none — behavior is bit-identical to today for every in-repo queue.
- **Performance:** **zero when off** (see Part 5). Master-equivalent-or-better when on.
- **Key design win:** serializing *only the invocation* — with the `m_lock` state transition and
  `m_cvReady.notify_all()` happening **before** `m_lockNotify` is taken — **breaks the `188fe03`
  deadlock cycle**. A blocked callback no longer prevents other completions from making progress
  and signalling the condition variable. This gives `master`'s observable property without
  `master`'s bug.
- **Two residual hazards, documented rather than fixed** — (i) a serialized callback that blocks
  waiting for *another notification* from the same queue still deadlocks; (ii) thread-pool worker
  starvation from N workers parked on `m_lockNotify`. These are **not footnotes**: they are the
  reason the mode is opt-in, and Part 6 specifies the header text verbatim.

**Do not** put the flag in the options enum. `setOptions` is an *absolute assignment*
([ExecutionQueueImpl.h:1094-1099](src/include/baselib/tasks/ExecutionQueueImpl.h#L1094-L1099)) and
real call sites do `eq -> setOptions( OptionKeepAll )` (e.g.
[TestAsyncV2.h:390](src/utests/utf_baselib_async/TestAsyncV2.h#L390)) — the serialization bit would
be silently cleared long after registration. Also, every existing bit is a *task-retention* policy
consumed by `keepTask()`; a delivery policy does not belong there.

### Option C — The review's original ask: a FIFO notification dispatcher

A dedicated dispatcher (strand / worker thread / serialized queue-of-one) delivering all events in
commit order.

- **Size:** a new subsystem. Notification queue, ownership, shutdown drain in `dispose()`,
  back-pressure policy.
- **Cost:** ~5–10 days.
- **Complexity:** high.
- **Risk:** high, and it introduces problems that do not exist today: unbounded notification queue
  growth under load; `dispose()`/lifetime must drain pending notifications; the happens-before
  relationship between "callback delivered" and "`pop()` observes the ready task" is broken because
  delivery is now asynchronous; added latency on every event.
- **Performance:** one `post()` + heap allocation per notification. Paradoxically *raises*
  completion throughput (the completion thread no longer runs the callback) while adding latency
  and memory pressure.
- **Value:** delivers FIFO ordering, which nobody had on `master` and no in-repo observer needs.
- **Assessment: not recommended.** This is the option the existing plan doc explicitly rejected
  ([execution-queue-concurrency-issues-plan.md:43](notes/plans/issues/execution-queue-concurrency-issues-plan.md#L43)),
  and that rejection is sound.

### Option D — Do nothing

- **Cost:** zero. **Risk:** the findings doc keeps two inaccurate claims, and the compat break stays
  undisclosed outside header comments.

### Adjacent item worth folding in

`getMaxReadyOrExecuting()` calling the user virtual under `m_lock` is CXX-07's residual (~0.5 day,
already on the findings doc's ranked list at #3). It is the *inconsistency* in the observer contract
noted in Part 3. If you take Option B, fixing this in the same workstream makes the whole
`ExecutionQueueNotify` contract coherent: **no user code runs under `m_lock`, ever.** That is a
better story than either fix alone.

---

## Part 5 — Performance impact

### When the option is OFF: provably zero

`getEventNotifyCB()`
([:300-324](src/include/baselib/tasks/ExecutionQueueImpl.h#L300-L324)) already returns an empty
`cpp::void_callback_noexcept_t` when `m_notifyCB` is null or the mask does not match, and `onReady`
already tests `if( onNotify )` at `:477` and `if( onNotifyAllCompleted )` at `:502`. The new mutex
goes **strictly inside those existing branches**, and the per-delivery flag is captured **under the
`m_lock` we already hold**, inside the `if( notifyCB )` block that already gates the `cpp::bind`.

Therefore:
- **No observer (15 of 17 production queues):** the branch is not taken. Zero added instructions.
- **Observer, option off:** one `bool` load from a member on the same cache line as `m_eventsMask`
  (already touched at `:307`), one register store, one correctly-predicted branch. No atomic, no
  fence, no second lock. This is noise next to the `cpp::bind`/`std::function` heap allocation
  already performed per event at `:314-319`.

### Current per-task lock cost, for scale

A minimal task lifecycle today costs **~2 `std::mutex` acquisitions**: one on push
([:660](src/include/baselib/tasks/ExecutionQueueImpl.h#L660)) and one on completion
([:378](src/include/baselib/tasks/ExecutionQueueImpl.h#L378)), plus a second on completion only when
the queue drains ([:484](src/include/baselib/tasks/ExecutionQueueImpl.h#L484)). `master` cost
**3** — `m_lockEvents` was paid unconditionally on every completion even though 15 of 17 queues have
no observer at all. `BL_MUTEX_GUARD` is `std::lock_guard<std::mutex>`
([BaseDefs.h:52](src/include/baselib/core/BaseDefs.h#L52),
[OSBoostImports.h:120](src/include/baselib/core/detail/OSBoostImports.h#L120)) — futex-backed,
order ~15-25ns uncontended, syscall when contended.

### When the option is ON: the structural cost

Let *C* = the `m_lock` completion section, *D* = mean `onEvent` duration, *P* = pool width
(`THREADS_COUNT_DEFAULT = 32`, [ThreadPoolImpl.h:449](src/include/baselib/core/ThreadPoolImpl.h#L449)).

- **Today:** per-queue completion throughput ≈ `min( P/(T+C+D), 1/C )`. The ceiling is `m_lock`,
  which already serializes *C*.
- **Serialized:** `min( P/(T+C+D), 1/C, 1/D )`. **The new `1/D` term is independent of *P*.** When
  *D* ≫ *C*, throughput collapses to `1/D` and adding cores does nothing. The Amdahl serial fraction
  of the completion path goes from `C/(C+D)` to `1`.
- **The second-order effect is the one that actually matters:** threads blocked on `m_lockNotify`
  are **thread-pool workers**, and `ThreadPoolDefault` is process-wide. A slow serialized observer
  on *one* queue removes workers from the shared pool and degrades **unrelated** queues. This is the
  strongest technical argument for keeping serialization opt-in.

### Where this would actually be felt

Hot paths, from the queue-usage survey:
- `AsyncExecutorImpl` — **three** queues per executor, one push+complete cycle per async op
  ([AsyncExecutorImpl.h:848-901](src/include/baselib/async/AsyncExecutorImpl.h#L848-L901),
  `:1034`, `:1085`). Highest frequency in the repo. **No observer registered** → unaffected.
- `ObservableBase` — one queue *per subscription*, one task *per event*
  ([:671](src/include/baselib/reactive/ObservableBase.h#L671), `:747`). **No observer** → unaffected.
- Transfer/packaging — one task per 1MB data block. **No observer** → unaffected.
- `TcpServerBase` — the one production queue with `AllEvents`. Rate = connection accept/teardown,
  not per-message. Serialization here would be immaterial.
- `FanoutTasksObservable` — `AllTasksCompleted` only, no-op body. Immaterial.

**So the in-repo performance impact of Option B, even if every existing observer opted in, is
approximately nil.** The cost is entirely a hazard for *future* or *downstream* observers with
expensive callbacks — which is precisely why it should be opt-in and documented, not default.

### The measurement gap — important

**No existing benchmark can detect any of this.** The closest harness, `executePerfTests`
([TestAsyncV2.h:378-440](src/utests/utf_baselib_async/TestAsyncV2.h#L378-L440), 40,960 tasks × 5
async calls), **never calls `setNotifyCallback`** — `m_notifyCB` is null, so both `if( onNotify )`
branches are dead code in that run. It measures the default path only and is structurally blind to
the option in both states. It is still the right **regression guard for requirement 1**: its numbers
must be unchanged.

A benchmark that could actually measure the serialization cost must:
1. register a real observer with `AllEvents` (otherwise it measures nothing);
2. use `OptionKeepNone` so every completion yields a `TaskDiscarded`, maximising event density;
3. use near-empty `SimpleTaskImpl` bodies so completion processing, not task work, dominates;
4. parameterise `onEvent` cost as a **busy-spin of D ns, not a sleep** — a sleep releases the pool
   thread and hides the starvation effect, which is the entire point;
5. sweep `{ no observer, concurrent, serialized } × D ∈ {0, 1µs, 10µs, 100µs} × P ∈ {1, 2, ncpus}`;
6. include a **second, unobserved queue on the same pool** and report its throughput — that is the
   effect the model predicts and the one users would actually hit.

Expected signature: concurrent scales ~linearly in *P*; serialized flattens at ≈ `1/D` regardless of
*P*; at *D* = 100µs the second queue shows measurable degradation only in the serialized run.

---

## Part 6 — Documentation requirements (the loud part)

The compile break forces downstream to *look* at `setNotifyCallback`. What they read there has to be
sufficient to make the right choice without reading the implementation. Three placements, each with
a different job.

### 6a. `ExecutionQueueNotify.h` — the `NotifyDelivery` enum: the decision guide

Placed directly above the enum, so it is the first thing anyone sees when the compiler points them
at the new parameter. Must cover, in this order:

1. **What changed and when.** Prior to this release, callbacks were mutually exclusive. They are not
   any more by default. State it as a behavior change, not as a feature description.
2. **`DeliveryConcurrent`** — the default contract, cross-referenced to the existing `onEvent` block
   at [:82-91](src/include/baselib/tasks/ExecutionQueueNotify.h#L82-L91). Observers **must** be
   thread-safe; up to thread-pool-width invocations may be in flight
   (`THREADS_COUNT_DEFAULT = 32`, [ThreadPoolImpl.h:449](src/include/baselib/core/ThreadPoolImpl.h#L449)),
   plus arbitrary external threads via `ExternalCompletionTaskIfT::markCompleted`
   ([TaskBase.h:1498](src/include/baselib/tasks/TaskBase.h#L1498)).
3. **`DeliverySerialized`** — what it does and does **not** buy. It buys mutual exclusion of
   `onEvent` **only**. It does **not** buy ordering (see 6b), does **not** cover
   `maxReadyOrExecuting()`, and is **not** a notification-drain barrier.
4. **The two hazards, under an explicit `WARNING:` heading** (below).
5. **The guidance sentence:** *"Choose `DeliverySerialized` only for short, non-blocking observers.
   If your callback can block, or can take more than a few microseconds, choose
   `DeliveryConcurrent` and synchronize inside your own observer."*

### 6b. The ordering trap — must be stated explicitly

Downstream reading "serialized" will assume FIFO. They will be wrong, and on `master` they were
already wrong. Say so plainly:

> `DeliverySerialized` guarantees **exclusion, not order.** Completing threads contend for an
> internal mutex whose acquisition order is unspecified, so delivery order between different task
> completions remains arbitrary — including between repeated execution attempts of the same retained
> task. This matches the behavior of releases prior to the concurrent-delivery change, which also
> provided no ordering guarantee. If you need ordering, sequence it inside your own observer.

### 6c. The two hazards — verbatim `WARNING:` text

Both belong on the enum **and** are cross-referenced from `ExecutionQueue::setNotifyCallback`.

> **WARNING (deadlock).** A `DeliverySerialized` callback must never block waiting for *another
> notification* from the same queue. Delivery of that other notification requires the same internal
> mutex, which your callback holds. This is a narrower restriction than the one that applied before
> the concurrent-delivery change — a serialized callback **may** safely call back into the queue
> (`push_back`, `wait`, `flush`, `dispose`) and **may** block waiting for another *task* to
> complete, because the queue's state transition and condition-variable signalling happen before the
> notification mutex is acquired. Only waiting on another *delivery* deadlocks.

> **WARNING (thread-pool starvation).** Notification callbacks run on thread-pool worker threads.
> Under `DeliverySerialized`, every concurrently completing task parks one worker on the
> notification mutex for the duration of the callback in front of it. `ThreadPoolDefault` is
> **process-wide** and shared with every other execution queue, so a slow serialized observer on one
> queue degrades throughput on **unrelated** queues, and can exhaust the pool. Per-queue notification
> throughput becomes `1 / callbackDuration` and does **not** improve with more cores.

### 6d. `ExecutionQueueImpl.h` — the lock-ordering invariant

The comment on `invokeNotifyCB()`, verbatim as given under **Required lock-ordering invariant**
below. This one is for maintainers, not consumers; it is what stops a future change from
reintroducing the `188fe03` cycle.

### 6e. The tracked breaking-change record

Header comments are invisible to someone who merely recompiles. The disclosure commit must put a
record where an integrator will actually find it (`notes/plans/issues/`, alongside the existing
`public-header-hygiene-deferral.md` risk-acceptance precedent), stating: the behavior change and the
commit that introduced it (`188fe03`, not `fb85810`); that it is silent at runtime; that the
mandatory parameter now surfaces it at compile time; the five-observer inventory as exhaustiveness
evidence; and the one-line migration (`DeliveryConcurrent` to keep current branch behavior,
`DeliverySerialized` to restore pre-change behavior).

---

## Recommendation

**Option B**, sequenced as independent commits per AGENTS.md ("one type of change per commit"):

0. **Persist this document** to
   `notes/plans/issues/execution-queue-notification-delivery-plan.md`, matching the naming of the
   existing `execution-queue-concurrency-issues-plan.md`. It supersedes that file's
   "do not implement F-07b" decision, which should gain a one-line forward reference.
1. **Doc/disclosure commit** (~0.25 day) — the observer inventory as exhaustiveness evidence, the
   tracked breaking-change record (6e), and the two findings-doc corrections from Part 1.
   *Worth doing regardless of what happens to the code.*
2. **Implementation commit** (~0.75 day) — `NotifyDelivery`, the mandatory parameter, `m_lockNotify`,
   the `invokeNotifyCB()` helper, the lock-ordering comment, and the five in-repo call sites updated
   to `DeliveryConcurrent`. Runtime-behavior-preserving for every existing call site, which is what
   makes it reviewable in isolation.
3. **Contract-documentation commit** (~0.5 day) — the Part 6a–6c header text. Kept separate from
   commit 2 per AGENTS.md's "never mix logic and comment/style changes"; commit 2 carries only the
   minimum doc needed to explain its own signature change.
4. **Test commit** (~1.0 day) — see Verification.
5. **CXX-07 fold-in** — **done, but not as originally scoped.** The hoist described in the findings
   doc is infeasible; the implemented fix samples the limit once at registration. See below.

### CXX-07: correction to the cost estimate, and what was actually done

`pr-review-deep-dive-residual-findings.md` ranks this as *"Hoist `getMaxReadyOrExecuting()` out of
`m_lock` — 0.5 day"*. Verified against the code, a hoist as described is **not achievable at that
cost, or in that shape**.

`getMaxReadyOrExecuting()` is called from `padExecutingQueueNothrow()`, and every one of its seven
call sites already holds `m_lock`. Six of them could pre-compute the limit before taking the lock.
The seventh cannot: `padExecutingQueueNothrow()` is called from inside the lambda at
`ExecutionQueueImpl.h:1076-1080`, which is used as the **`condition_variable::wait` predicate** in
`waitInternal()`. The predicate is re-evaluated by the condition variable with `m_lock` held on every
wake, so there is no point at which a caller can compute the observer's limit outside the lock ahead
of each evaluation.

The real options are therefore:

| | Approach | Cost | Consequence |
|---|---|---|---|
| **A** | Sample the limit once per queue *operation*, outside the lock, into a member read under it | ~2 days + a documented contract change | Fully removes user code from under `m_lock`. Changes *when* the limit is sampled: once per operation rather than once per scheduled task. Identical for any observer returning a constant — which is both in-repo implementations — but observable to a downstream observer returning a dynamic value. |
| **B** | Hoist only out of the inner `while` loop: one query per padding pass rather than one per scheduled task | ~0.5 day | Real win on the hot scheduling path — it removes a proxy-mutex acquisition (`tryAcquireRef`) plus a virtual call **per task scheduled**. But user code still runs under `m_lock`, so it does **not** close CXX-07. Same dynamic-limit caveat as A, in milder form. |
| **C** | Leave as-is | 0 | The hazard stays, already documented at `ExecutionQueueNotify.h:61-64`. |

Both A and B change the sampling semantics of `maxReadyOrExecuting()`, which is a public contract
change on top of the one this workstream already makes.

**The decisive finding is that every implementation returns a compile-time constant:**
`ExecutionQueueNotifyBase.h:44` returns `0`, `FanoutTasksObservable.h:294` returns
`MAX_READY_OR_EXECUTING_TASKS` (1024), `FilesPkgUnpkgBase.h:240` returns `MAX_OPEN_FILES_PENDING`
(256). The entire pull mechanism exists to deliver a constant, and pays a proxy-mutex acquisition
plus a virtual call *per scheduled task*, inside the queue's global lock, to do it. The tell is in
the same `if` statement: `m_maxExecuting` is a **pushed** limit set by `setThrottleLimit()`, while
`maxReadyOrExecuting()` is a **pulled** one. There is no design reason for the asymmetry — a limit
is configuration, and configuration should be pushed.

**Implemented: sample once at registration.** `setNotifyCallback()` resolves the observer and calls
`maxReadyOrExecuting()` *before* taking `m_lock`, storing the result in `m_maxReadyOrExecuting`
alongside `m_maxExecuting`; `getMaxReadyOrExecuting()` is deleted and the scheduling loop reads the
member. Consequences:

- **No user code runs under `m_lock` at any point.** The observer contract collapses to one rule.
- A proxy-lock acquisition and a virtual call leave the per-task scheduling path.
- **Zero changes to any observer class** — the polymorphism still works, `FilesPkgUnpkgBase` still
  overrides the constant.
- Contract change, documented at both `ExecutionQueueNotify::maxReadyOrExecuting()` and
  `setNotifyCallback()`: a dynamic limit is no longer re-queried, and a cached limit now survives
  observer disconnect (previously, disconnecting silently removed the throttle). In exchange the
  earlier "must not call back into the queue or you will deadlock" restriction is lifted.
- 6a's "`maxReadyOrExecuting()` is not covered by the notification mutex" caveat is kept, since it
  is still true and still worth stating.

This path had **no test coverage at all** — no test overrode `maxReadyOrExecuting()`. A new
`Tasks_ExecutionQueueThrottleFromObserverTest` now pins both halves: the limit is enforced
(2 executing, 3 pending, and retiring one admits exactly one more), and it is queried exactly once.

Total **~3.0 days**, or ~3.75 with the microbenchmark.

### Critical files

- [src/include/baselib/tasks/ExecutionQueueNotify.h](src/include/baselib/tasks/ExecutionQueueNotify.h) — `NotifyDelivery` enum next to `EventId` at `:41-51`; hazard/decision text per Part 6a–6c
- [src/include/baselib/tasks/ExecutionQueue.h](src/include/baselib/tasks/ExecutionQueue.h) — `setNotifyCallback` signature + doc at `:105-113`, cross-referencing the hazards
- [src/include/baselib/tasks/ExecutionQueueImpl.h](src/include/baselib/tasks/ExecutionQueueImpl.h) — `m_lockNotify` + `m_notifyDelivery` members after `:233`, ctor init at `:252`, `getEventNotifyCB` out-param at `:300`, new `invokeNotifyCB()` helper after `:324`, 4 sites in `onReady` (`:448`, `:454`, `:477`, `:495`, `:502`), `setNotifyCallback` at `:1101`
- [src/include/baselib/reactive/FanoutTasksObservable.h:102](src/include/baselib/reactive/FanoutTasksObservable.h#L102) and [src/include/baselib/tasks/TcpBaseTasks.h:1875](src/include/baselib/tasks/TcpBaseTasks.h#L1875) — one-line call-site updates to `DeliveryConcurrent`
- [src/utests/utf_baselib_tasks/TestTasks.h](src/utests/utf_baselib_tasks/TestTasks.h) — fixture parameterisation + 3 call-site updates + tests

### Required lock-ordering invariant (must appear as a comment)

> `m_lockNotify` is strictly **outer** to `m_lock`. It is acquired only in `invokeNotifyCB()`, called
> only from `onReady()` after every `m_lock` guard has been released. No path may acquire
> `m_lockNotify` while holding `m_lock` — in particular `getMaxReadyOrExecuting()` invokes a user
> virtual under `m_lock` and must never be covered by it. The reverse edge (`m_lockNotify → m_lock`)
> is intentional: an observer may legitimately re-enter the queue.

Release `m_lockNotify` **between** the two invocations rather than holding it across the `:484`
`m_lock` re-acquisition — this keeps the serialized region free of any blocking lock acquisition and
leaves the `e60b3b3` generation revalidation timing untouched.

Use a **non-recursive** mutex: same-thread reentrancy into `onReady` is provably impossible
(`onReady` is reached only via `TaskBase::notifyReadyImpl` → `cbReady()` at
[TaskBase.h:711](src/include/baselib/tasks/TaskBase.h#L711), scheduling failures are `post()`ed
never run inline, and nothing under `tasks/` drives the io_service on a caller thread), and
recursion would silently grant thread-exclusion while violating the logical exclusion being bought.

> **Premise corrected (2026-09-04, M-1 of the Fable 5.1 review):** same-thread re-entry *is*
> reachable — `ExternalCompletionTask::markCompleted()` runs `notifyReadyImpl` → `cbReady()` on the
> caller's thread, so a serialized callback which synchronously completes a sibling task of the same
> queue re-enters `invokeNotifyCB()` while holding `m_lockNotify`. The non-recursive mutex decision
> stands; the contract in `ExecutionQueueNotify.h` now forbids that pattern explicitly and
> `invokeNotifyCB()` detects it with a per-queue owner-thread check and `BL_RT_ASSERT`. See
> `pr-review-fable51-merge-gate-items4-8-plan.md`.

---

## Verification

Reuse the existing fixtures: `ExecutionQueueNotificationRecorder`
([TestTasks.h:750](src/utests/utf_baselib_tasks/TestTasks.h#L750)),
`ExecutionQueueNotificationTestContext` (`~:900`, add a ctor parameter defaulted to
`DeliveryConcurrent` so all six existing constructions are unchanged), `ExecutionQueueTestSignal`
(`:595`), `createControlledCompletionTask` (`:939`), `setHook` (`:818`), `waitForEventCount`
(`:833`).

**Call-site sweep first:** `grep -rn "setNotifyCallback" src/` must return exactly five call sites
plus the declaration and definition. If the build succeeds without touching all five, the parameter
was accidentally given a default value and the loudness requirement has been lost — treat a clean
build before the call-site edits as a failure, not a success.

Shared probe helper records `maxDepth` via `++/--concurrentDepth` around the hook body.

1. **Default stays concurrent** — `Tasks_ExecutionQueueNotificationConcurrentDeliveryTest`. A's hook
   signals then blocks; B's hook signalling *while A is still inside* is a positive, fast,
   deterministic proof. Assert `maxDepth == 2`.
2. **Opt-in truly serializes** — `Tasks_ExecutionQueueNotificationSerializedDeliveryTest`. (i) 64
   controlled tasks completed from `os::thread`s, `waitForEventCount( TaskDiscarded, 64 )`, assert
   `maxDepth == 1` (pure invariant, no timing gate); (ii) an A/B rendezvous asserting an
   enter/exit sequence counter shows A exits before B enters. The bounded wait shapes the schedule
   and is **never** the assertion.
3. **The `188fe03` deadlock is not reintroduced** — factor
   `Tasks_ExecutionQueueNotificationBlockingReentryTest`
   ([:1156-1260](src/utests/utf_baselib_tasks/TestTasks.h#L1156-L1260)) into a helper taking a
   `NotifyDelivery` and call it twice. It already has the right safety machinery: bounded deadline at
   `:1216` and detach-instead-of-join at `:1220-1242`, so a reintroduced deadlock **fails** rather
   than hangs. Under `DeliverySerialized` this is also the positive proof of the 6c deadlock
   carve-out: callback A blocks in `eq->wait( taskB )` at `:1189` while holding `m_lockNotify`, and
   both completions must still return.
4. **Disposal under opt-in** — re-run `Tasks_ExecutionQueueAllTasksCompletedDisposalCompatibilityTest`
   ([:1367](src/utests/utf_baselib_tasks/TestTasks.h#L1367)) serialized. This is the concrete proof
   that no `m_lock → m_lockNotify` edge exists, since it drives
   `padExecutingQueueNothrow` → `getMaxReadyOrExecuting` → user virtual under `m_lock` while another
   thread holds `m_lockNotify`.
5. **Explicit non-test:** `Tasks_ExecutionQueueAllTasksCompletedReentrantTests`
   ([:970](src/utests/utf_baselib_tasks/TestTasks.h#L970)) must **not** be re-run serialized — its
   schedule requires both callbacks inside the hook simultaneously, which opt-in makes impossible by
   definition. Add a comment so nobody "generalizes" it later.
6. **Zero-cost-when-off, verified by inspection, not measurement.** Confirm in review that
   `m_lockNotify` is acquired only inside `invokeNotifyCB()`, that `invokeNotifyCB()` is called only
   from within the pre-existing `if( onNotify )` / `if( onNotifyAllCompleted )` branches, and that the
   delivery-policy read happens inside the `if( notifyCB )` block of `getEventNotifyCB` that already
   gates the `cpp::bind`. Per Part 5, no benchmark in the repo can observe this path, so the argument
   has to be structural.
7. **Hazard (ii) is not unit-testable** — thread-pool starvation only shows up under load. It is
   covered by the Part 5 microbenchmark's second-unobserved-queue measurement, if that is built;
   otherwise it stands on the documented warning alone. Say so in the plan record rather than
   implying test coverage that does not exist.

Build and run:

```bash
make -k -j4 utf_baselib_tasks
make -k -j4 utf_baselib_tasks VARIANT=release
make -k -j4 utf_baselib_async     # executePerfTests numbers must be unchanged
```

Run the focused notification tests under `BL_CLANG_ENABLE_RA_TSAN=1`, as
`execution-queue-concurrency-issues-plan.md:75` already requires. Do not require the whole repo to
be TSan-clean — CXX-07 accessor races are separately tracked.
