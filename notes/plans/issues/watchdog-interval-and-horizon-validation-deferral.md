# `Watchdog` Interval and Horizon Validation: Deferral Record

This document records two validation gaps in
[`src/include/baselib/core/Watchdog.h`](../../../src/include/baselib/core/Watchdog.h) which were
deliberately **not** fixed, so that their absence was read as a decision rather than an oversight.

**Status: CLOSED, both fixed 2026-09-08.**

## Resolution (2026-09-08)

Fixed as item 8.2 of
`notes/reviews/major/update_2026/whole-library-cxx-test-enhancement-outstanding-issues-plan.md`.
Both gaps are now closed with one `BL_CHK` each:

- the constructor rejects a negative `checkingInterval` before the `std::uint64_t` cast can turn
  it into a huge interval which silently disables the watchdog;
- `expiringMonitors( horizon )` rejects a negative horizon, which used to wrap the same way and
  report every monitor as expiring.

`TestWatchdogArgumentValidation` in `src/utests/utf_baselib/TestWatchdog.h` gained a negative case
for each. The accepted domain now matches what the documentation always stated, so no caller which
was passing a valid value is affected.

The rest of this document is the original analysis and is kept for the record.

---

**Origin:** `notes/reviews/major/update_2026/whole-library-cxx-test-enhancement-plan.md`, task T362, which states that both *"are deliberately not
covered here because fixing them is a production change ... Record both, do not fix them from a
test."*

---

## Decision

**Date:** 2026-09-08
**Status:** Both **deferred**; neither is covered by a test, because a test could only assert the
current, wrong behaviour

| # | Item | Disposition |
|---|---|---|
| 1 | The constructor rejects a **zero** checking interval but accepts a **negative** one | **Deferred** |
| 2 | `expiringMonitors( horizon )` does not validate its horizon at all | **Deferred** |

What T362 **does** cover, in `TestWatchdogArgumentValidation`
(`src/utests/utf_baselib/TestWatchdog.h`), is the two guards that do exist: the zero-interval
rejection in the constructor, and `extendExpiration`'s negative-extension rejection **plus** its
ordering - it validates before it calls `setupMonitor`, so a rejected call must not consume a monitor
slot.

---

## Item 1 - a negative checking interval is accepted

```
m_checkingIntervalInMicros( static_cast< std::uint64_t >( m_checkingInterval.total_microseconds() ) )
```

`BL_CHK( 0U, m_checkingIntervalInMicros, ... )` rejects only the value zero. A negative
`time_duration` yields a negative `total_microseconds()`, and the cast to `std::uint64_t` turns it
into an astronomically large positive value - non-zero, so the check passes.

**Consequence.** `timeDurationInCheckingIntervals( ... )` then divides by that huge value and returns
1 for essentially any extension, and `run( ... )` sleeps for a negative `time_duration` on every
iteration. The watchdog does not crash; it silently stops being a watchdog.

**Why deferred.** The fix is a one-line production change -
`BL_CHK( true, checkingInterval.is_negative() || 0 == checkingInterval.total_microseconds(), ... )`
or equivalent - but it is a **production** change, and T362 is a test task. It also changes the
constructor's accepted domain, which is public API. It belongs in a change whose subject is the
`Watchdog`, reviewed as such.

---

## Item 2 - `expiringMonitors` does not validate its horizon

`expiringMonitors( horizon )` passes `horizon` straight to `timeDurationInCheckingIntervals( ... )`
with no check. A negative horizon becomes an astronomically large interval count by the same
`std::uint64_t` cast, and every monitor whose expiration is anything below that - i.e. every extended
monitor - is reported as expiring.

**Consequence.** A caller that computes a horizon by subtraction and gets a negative result is told
that everything is about to expire. In a real deployment that is the input to a termination
decision.

**Why deferred.** Same reason as Item 1: it is a production change to a public method's accepted
domain. It is also the smaller of the two - the horizon is caller-computed, whereas the checking
interval is configuration.

---

## Why neither is covered by a test

A test written today could only assert the current behaviour - that a negative interval is accepted
and that a negative horizon reports everything. Pinning that would make the eventual fix *fail* a
test, which inverts the purpose of the suite. When either is fixed, the fix's own commit should carry
the assertion that the negative input is now rejected.

---

## Conditions to revisit

- Any change to `Watchdog.h` for any other reason - fixing both is a few lines and the review
  context is already open.
- A second production consumer of `Watchdog` appears. Today the accepted domain is policed by the
  one caller that exists; a second one makes the guards load-bearing.
- Either input becomes computed rather than literal at a call site, which is when a negative value
  stops being hypothetical.
