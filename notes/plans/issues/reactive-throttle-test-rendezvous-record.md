# Tasks_ReactiveNotifyOnNextThrottleTests: A Missing Rendezvous

**Status:** **RESOLVED 2026-09-14.** Found by the 12-combo Windows matrix, reproduced under load, fixed and verified.

**Summary.** The test samples three counters but rendezvouses on only two of them. The third is read
while another thread may not yet have been scheduled to update it, so under load the assertion on it
fails. Everything about the throttle under test is correct; the gap is in the test's own
synchronisation.

---

## What was seen

One failure in twelve full-matrix combos:

```
win-a64-vc143-debug
src/utests/utf_baselib_tasks/TestTasks5.h(905): fatal error:
    in "Tasks_ReactiveNotifyOnNextThrottleTests":
    critical check EQUAL( blockedNextCountAtRejection, 1U ) has failed
        lhs value: 0
        rhs value: 1
```

**Classification, measured rather than assumed:**

| Condition | Runs | Result |
|---|---:|---|
| `make -j5 testutf`, the failing combo | 1 | **1 failure** |
| `make -j5 testutf`, the other eleven combos | 11 | 11 pass |
| The module alone, serially, idle machine | 6 | **6 pass** |

Load sensitive, and only under `-j5` on a two-core machine. It is not a defect in
`ThrottleProbeObservable` or in the throttle behaviour being exercised.

---

## Why it fails

`TestTasks5.h:833-912` sets up two subscribers on one throttled observable:

- `blockedObserver` — a `BlockingCountingObserverImpl` with `gateCallIndex = 1`, so it completes one
  `onNext` and then parks on a `TestSignal` gate
- `freeObserver` — a plain `CountingObserverImpl`

It then waits, samples, and asserts:

```cpp
const auto firstRejectionSeen  = probeImpl -> waitForFirstRejection();      // rendezvous 1
const auto freeObserverDrained = freeObserver -> waitForNextCount( 3U );    // rendezvous 2

const auto pushedAtRejection           = probeImpl -> pushedCount();
const auto freeNextCountAtRejection    = freeObserver -> nextCount();
const auto blockedNextCountAtRejection = blockedObserver -> nextCount();    // no rendezvous

UTF_REQUIRE_EQUAL( blockedNextCountAtRejection, 1U );
```

Both rendezvous points observe progress made by *other* threads: the producer reaching its throttle
limit, and the free observer draining the three admitted values. Neither implies the blocked
observer's thread has run far enough to increment its own counter from 0 to 1.

On a loaded machine it had not. The producer throttled, the free observer drained, and the blocked
observer was still waiting to be scheduled — so `nextCount()` read 0.

**The author was alert to exactly this class of bug.** `CountingObserverT` carries the comment that
`waitForNextCount()` is *"a bounded rendezvous which replaces a sleep"*, and the free observer is
synchronised with it. The blocked observer simply never got the same treatment.

---

## The fix

One line, using machinery that already exists. `BlockingCountingObserverT` derives from
`CountingObserverT` (`TestTasks5.h:197-198`), so it inherits the same bounded rendezvous:

```cpp
const auto blockedObserverArrived = blockedObserver -> waitForNextCount( 1U );
...
UTF_REQUIRE( blockedObserverArrived );
```

placed with the other two waits, before any counter is sampled. No sleep, no weakened assertion, and
the same failure mode still caught if the blocked observer genuinely never arrives — it would fail on
`blockedObserverArrived` with a clear cause instead of on a confusing count mismatch.

### Applied and verified

The failure as first seen was one occurrence in twelve matrix combos, which is far too rare to
verify a fix against - the case also passed six times out of six serially *before* any change. So it
was first made reproducible: run the single case in a loop against six busy loops on a two-core
machine, starving the blocked observer's thread. That turned a 1-in-12 flake into a measurable rate.

| | Pre-fix | Post-fix |
|---|---:|---:|
| Starved iterations | 150 | **450** |
| Failures | **4 (2.7%)** | **0** |
| Full module, serial | - | 3/3 pass |

Every pre-fix failure was the same assertion at `TestTasks5.h:905`. At the measured 2.7% rate, 450
clean runs would occur by chance roughly 0.02% of the time.

**What this evidence does not cover.** It establishes the race is gone under starvation on
`win-a64-vc143-debug`. It does not prove the case is race-free in general, and the fix was not
re-verified on the other eleven combos - a judgement call, since the change is a bounded wait built
from the test's own existing idiom rather than new machinery.

### The baseline acceptance, and why it was surgical

This is the **first deliberate change to test content** since `baseline/inventory.json` was captured,
so it is the first time C2 has fired for a good reason - `case BODY CHANGED`. The gate was right and
the baseline had to move.

It was moved **one field at a time, not re-captured**. A wholesale re-capture would re-anchor C1 to
C4 for all 772 cases and destroy the standing proof that the module split altered no test. Instead
the accept asserted, before writing anything:

- exactly one case's content differs, and it is this one
- the case name set is unchanged, at 772
- no helper member was lost
- the edited case's guard and namespace stacks are unchanged

The resulting diff is **one line** - a single `body_sha`. That is the shape any future intentional
test change should take, and it is the same "accept, deliberately and visibly" workflow the
[size gate plan](../test-module-size-gate-plan.md) proposes for object sizes.

The harness was deliberately throwaway - "start N busy loops, run `--run_test=<case>` M times, count
non-zero exits" - and is worth rebuilding from that description rather than preserving. The shape is
what matters, and it generalises to any of the candidates in the next section: **make a rare
concurrency failure frequent before trying to fix it**, or there is no way to tell a fix from luck.

---

## Worth checking for the same shape elsewhere

The pattern is specific and greppable: a bounded rendezvous on some observers or probes, followed by
sampling state owned by a thread nobody waited for. Any test that takes several `waitFor*` calls and
then reads more counters than it waited on is a candidate.

This one survived a long time because it only fails when the machine is oversubscribed — two cores
running five concurrent test modules. A CI machine with more cores may never show it.

---

## Conditions to revisit

- ~~**The fix lands.**~~ Done; this record is closed.
- **The same assertion fails again**, especially serially or on a machine that is not oversubscribed.
  That would mean the diagnosis here is wrong and the throttle itself is suspect.
- **Other tests start failing under `-j5`.** The matrix run that found this was the first time the
  suite had been driven at that concurrency on a two-core host; more of the same shape may exist.
