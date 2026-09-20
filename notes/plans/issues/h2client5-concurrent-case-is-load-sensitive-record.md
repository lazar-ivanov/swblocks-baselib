# The concurrent-request case is load-sensitive and is NOT yet trustworthy

**Found:** 2026-09-20, by the orchestrator's release validation of the case the L6 second pass asked
for. **Status:** OPEN. **The review item "no concurrent-request case" is therefore NOT closed** — a
case exists, but it does not yet hold under the conditions a gate runs in.

## What happened

`H2Pool_TwoRequestsInFlightOnOneConnectionTests` failed **once**, under `clang2010 release`, during
the eight-module validation pass — i.e. while the machine was building other modules concurrently.
The same binary then passed **5 of 5** re-runs idle, and `gcc1520 release` passed **3 of 3**.

**Eight idle passes are not evidence.** This project has already been bitten by exactly that
inference: an earlier flake passed 12 of 12 idle and reproduced 1 in 40 under load, and the lane
that fixed it established that *a concurrent compile stalls one thread for hundreds of milliseconds,
where even CPU load presses evenly on all of them*. A validation pass building two toolchains is
that shape.

## The failure, and why the value matters

    lhs value: 1
    rhs value: 2
    critical check EQUAL( pool -> dispatchCapacity( connection ), 2U ) has failed

It failed at **the discriminating assertion** — the one the case exists for, where capacity reading
2 while the first request is still in flight is the only evidence that the band inference ran.

And it read **1, not 3**. That is the opposite of the loose-bound hazard the authoring lane
recorded: 1 means the peer's limit was **never learned at all** and the pool was still in the
one-slot-until-known regime when the assertion ran. The log also shows only 2 of 3 acquires
answered.

## What this does and does not establish

- **It does not establish a pool defect.** The most likely reading is that under load the peer's
  SETTINGS, the driver's republish on `SettingsReceived`, and the pool's maintenance tick interleave
  differently, so the reading the band test needs is taken later than the assertion.
- **It does establish that the case is not yet a reliable witness.** A case whose discriminating
  assertion depends on when a 10–250 ms maintenance tick lands, relative to a 2 s opening delay,
  cannot be trusted to mean what it claims — and a gate runs precisely under the load that broke it.

## What whoever picks this up should do

1. **Reproduce under load, not idle.** Run it while a compile is in flight; that is the shape which
   broke it, and idle re-runs will mislead.
2. **Then decide which it is.** If the pool genuinely fails to learn the limit under that
   interleaving, that is an L5 defect the case has just found and is worth more than the case. If it
   is the case racing the tick, the fix is a rendezvous on the learning event rather than on the
   request count — the same lesson as `awaitWindowStall`, `awaitStreamClosed` and `requireRecorded`:
   **wait for the thing the assertion is about.** The case currently waits for two requests to have
   arrived at the peer, which is not the same event as the pool having learned the limit.
3. **Do not weaken the assertion to make it pass.** Capacity reading 2 with the first request still
   in flight is the whole point; an assertion of `>= 1` would pass against a pool that never learns
   anything.

## Related

`notes/plans/issues/http2-l6-review-record.md` — the second pass's "what I would not ship" list,
item 2, which this case was written to close and does not yet close.
