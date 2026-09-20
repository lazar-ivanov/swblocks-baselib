# What the HTTP/2 client work learned about its own verification

**Written:** 2026-09-20, at the close of L6, the end of the authorized scope. **Status:** RECORD —
no code change is owed by it. The defects it generalises from are each fixed or recorded separately;
this is the reasoning behind the rules added to `src/utests/AGENTS.md`, so those rules are not bare
assertions.

**Why it exists at all.** These lessons lived only in the session's own state directory, which is
deliberately outside the repository and gets discarded, and in one agent's memory. The maintainer
asked for them where the next round can find them.

---

## 1. Stubs agreeing with a contract is not components agreeing with each other

Layers L3 through L5 were built by parallel lanes, each against the published S2.6 contract, each
with its own stub. Every module was green throughout. **Every finding of the L5 review, and the
largest of L6, existed because the pool, the request task and a real driver had never been composed
in any test.**

What the stubs could not show:

- a refused `submit()` leaking a pool slot **permanently** — the pool holds `Entry::slotsInUse`, and
  an entry is forgotten only at zero, so a leaked slot pinned the entry and, for h2 where a key has
  one connection, cost a permanent unit of capacity;
- the pool latching its dispatch limit from a reading taken **before the peer's SETTINGS**;
- the draining reserve chosen, wired, defaulted and pinned — and never *published*, so it bought
  nothing;
- an entry erased while its attempt task was still live;
- and a request task probe that published state **before** answering its sinks while asserting in a
  comment that this was "the order a driver produces too". The real driver does the opposite on two
  routes. That is L6 finding 16, and the assertion in the comment is exactly what the driver broke.

**The one that should sting.** S6.1's first end-to-end case found that *every first request over a
fallback connection failed*: the pool dispatches a key's first request onto the `Connecting`
placeholder so its HEADERS ride the preface, and over a fallback that placeholder is the HTTP/2 task,
which hands the stream to the HTTP/1.1 driver and completes — answering the rider with
`connection_aborted`, correctly retryable, and nothing replayed it because the dispatched half of the
retry had no owner.

**Design §5.4 had already named that exact case** as one the missing half would fail. It was written
down, and it still passed three green modules and two full review passes, because nothing composed
the three components until S6.1 did.

**So:** when a slice is the first to compose real components, write that case before building out the
surface, and read a failure there as the exercise working.

---

## 2. The measuring instrument was wrong three times, and each defect hid the next

`scripts/utests/utf_runlog.py`, which the G1 gate depends on:

1. `REPORT_CASE_RE` demanded the word `has`. Boost writes **five** verdicts and three avoid it —
   `has passed | has failed | was aborted | was skipped | has timed out`. An aborted case fell
   through to a default of `passed`. Measured in this project's logs at the time: 1428 `has passed`
   against 6 `was aborted`.
2. `compare()` was **differential in every branch that could see a failure**, so anything absent from
   the baseline side was compared against nothing. Every layer of this feature added a module, so a
   new module's failures were structurally invisible.
3. `NO_ERRORS_RE` and `FAILURE_RE` were `^`-anchored, and **Boost colours its confirmation report** —
   7 bare against 201 ANSI-prefixed `*** No errors detected`.

Fixing only 1 would not have caught the failure that exposed this. Fixing 2 without 3 would have been
inert on saved logs. All three are fixed (`acba555`, `3b52677`).

**What it cost:** the L4 gate **reported clean while `utf_baselib_h2client2` had exited 201.** Three
holes lined up — no exit code was read, the module was new so every case registered as `NEWLY RUNS`
with nothing to differ from, and the parser scored all nine cases `passed`. It was caught only by a
raw-evidence check run alongside the comparison, and the underlying failure was a real flake.

**So:** never let one instrument be the only witness, and ask of any comparison *what it can never
report*.

---

## 3. Running finds what reading does not

The independent reviewer recorded three occasions where it read code, judged it sound, and running
proved otherwise — its own count, in its own record. Among them a 1-in-8 race in a TLS case it had
read and passed, and the timer race below, where its verification of the idle-timer split accounted
for two of `cancelTimers()`'s three callers.

Every defect of consequence in L4 through L6 came from executing:

| Found by running | What it was |
|---|---|
| S4.1's first build | `TcpTunnelStageT` over a TLS policy **had never compiled** — nothing instantiated it |
| S4.4's first run | a window stall scripted as a duration when it is an event; an opening delay undone by the first thing the client said |
| S4.3's tests | a read that would **hang** the task rather than fail it |
| S6.1's end-to-end case | the fallback rider, above |
| L6's closing TSan | a real cross-thread race in the h2 driver's timer cancellation |

**So:** a template nothing instantiates is not compiled; a negative control that makes the failure
certain is worth more than any number of green runs; and validation that was never run is not a
clean result.

---

## What this record does not claim

None of this says review was not worth it — the review found the security defect in L6 (a cleartext
session following a redirect to `https` and sending Secure cookies in the clear), the permanently
leaked slot, and the ownerless retry half. The point is narrower: **review and execution find
different classes of defect, and this feature needed both.** Where they disagreed, running was right
every time.
