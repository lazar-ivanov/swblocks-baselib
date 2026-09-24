# H23 — one knob bounds two budgets that multiply: deferral record

**Date:** 2026-09-24. **Status:** deferred by the maintainer, **and the documentation is deliberately
the answer rather than a placeholder for one.** The ~20-line code option was considered and declined
with reasons, which is why this is a decision record and not a to-do.

Astra's **H23**, and before it **L6 finding 10**. The full argument lives where a reader will meet
the problem — `ConnectionPool.h`'s `maxRetriesPerRequest` comment — and this record exists so the
*decision* is not re-derived from the code each time.

---

## 1. The finding

`maxRetriesPerRequest` is one number, and it bounds **two counters that are renewed independently of
one another**:

- **the pool's** `Waiter::attempts`, which counts *establishment* failures and is refused by
  `examineKey( )`'s `attempts > maxRetriesPerRequest`;
- **the session's** `SessionRequestTaskT::m_attempts`, which counts *replays* and is **reset per
  redirect hop**, refused by `chkRequestMayBeReplayed( )`.

They compose multiplicatively. At the default of 3 the worst case is **(N+1)² = 16 establishments**
for one logical request.

## 2. Why the documentation is the answer

**The chain deadline already bounds both halves — in time, which is the dimension that matters.** The
session computes one deadline for the request and stamps the remainder on every hop, and the pool's
waiter deadline reads the same value. So the 16 establishments cannot outlive the caller's own
budget, whatever they cost in round trips. What H23 describes is a **cost** that is bounded, not a
**runaway** that is not.

**And the obvious fix is the wrong one.** Tightening this knob tightens *both* halves at once, which
is precisely the coupling the finding is about — it would make the symptom smaller while making the
conflation worse. The real fix is either one shared logical attempt budget, or establishment and
replay given **separately named limits**. Both are interface changes to `ConnectionPoolPolicy`, and
neither is justified by a cost that is already time-bounded.

**So the code option was declined, not postponed.** A ~20-line change that tightens the wrong thing
is worse than a comment that names the right thing.

## 3. What is in place

`ConnectionPool.h:234-259`, opening `ONE KNOB, TWO BUDGETS, AND THEY MULTIPLY (L6 finding 10, astra
H23)`. It names both counters **at the source**, derives the 16, and carries a paragraph headed
`WHY IT IS A NOTE AND NOT A FIX` giving §2's argument.

The same comment block also carries the **separate** warning that `maxRetriesPerRequest = 0` disables
HTTP/1.1 through a session — that is **H21**, a different finding with a different disposition, and
it is being fixed rather than deferred. Do not read the two as one.

## 4. What would reverse this

- **A measured cost.** If the round-trip cost of the multiplied budget shows up against a real origin
  — a slow-establishing pool burning the deadline on retries the caller did not intend — the note
  stops being sufficient and the two-named-limits shape becomes the fix.
- **Any other reason to change `ConnectionPoolPolicy`'s interface.** The interface change is the
  expensive part; if something else pays for it, splitting the budgets rides along nearly free.

## 5. What this record does not establish

- **Whether 16 is ever reached in practice.** Derived from the two bounds, never observed. No case
  arranges it and no origin here has produced it.
- **Whether the deadline bound holds under every clock and timeout combination.** The argument is
  read from the code that stamps the remainder on each hop, not measured across the combinations a
  caller could configure.
- **What the separately-named limits should be called or default to.** Deliberately not designed —
  designing it is most of the work, and §4 says when to spend it.
