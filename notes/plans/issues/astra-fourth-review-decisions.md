# Astra's fourth review — the decision, and how its change-set lands

**Date:** 2026-09-29. **Status:** D1 taken by the maintainer on 2026-09-29: shape (a″), in which the
connection publishes the value itself. CS-9 is in lane 1, design note first.

**The review:** [`http2-l0-l6-fourth-review-2026-09-28.md`](../http2-l0-l6-fourth-review-2026-09-28.md),
of `045e889`.
- It confirms T01 and T02 fixed.
- It establishes no regression from CS-7 or CS-8.
- It finds one P2, U01, which is pre-existing.

**The plan:** [`http2-implementation-plan.md`](../http2-implementation-plan.md), "Astra's fourth
review": change-set CS-9.

**Where the evidence lives:** the orchestrator's evidence directory, `http2-l0-state/logs/`, a sibling
of the checkout, kept outside the repository on purpose. This round's logs are under `logs/astra4/`.

**Checked at the source by the orchestrator before planning, and at every site by the one-off
review** (`astra4/reviews/plan-fable-r1.md`: agree with changes, all taken):
- `completeResponse( )` copies `m_connection -> negotiated( )` unconditionally
  (`HttpClientRequestTask.h:1906`). That is the race.
- `isRequestUnsuitableForConnection( )` (`:884`) breaks the rule too, but is ordered today by the
  refusal's own chain.
  - An HTTP/2 refusal needs `m_isSubmitClosed`.
  - Only `closeSubmissions( )` sets that flag, and every caller of it has already published
    `Draining` or `Closed`, after the write.
  - The request reads the flag under `m_commandsLock`.

  The helper makes the rule mechanical there as well.
- `m_negotiated` is written at most once per task, in `continueAfterConnected( )`
  (`ClientConnectionTaskBase.h:656`). That is the handshake handler's continuation, and the only
  restart of establishment is gated on the handshake not having completed.
- `Ready` is published once, after the write.
- Every other publication is either a strand handler reached only after negotiation, or the terminal,
  which runs under the lock the write holds.

So any state other than `Connecting` implies a final value.
- The driver's class comment states the rule: read `state( )` first, and never read `negotiated( )`
  while it is `Connecting`.
- The pool obeys the rule (`ConnectionPool.h:1131`). The interface's own documentation never states
  it.
- The HTTP/1.1 driver starts `Ready`, with a constant value.

## 1. The decision

### D1 — how the request task reads a connection's negotiated value

- **What it is.**
  - A request can ride an HTTP/2 connection which is still completing its TLS handshake. This is
    preface riding, and it is on by default.
  - If the request fails first — the caller cancels it, its total or response-headers deadline
    expires, or its submit throws — the request task builds its response and copies the connection's
    negotiated protocol and ALPN identifier. The total deadline is on by default, at 30 minutes; the
    headers deadline is off by default.
  - The handshake may be writing that same value on another thread at that moment. It holds a
    `std::string`, so this is a data race: undefined behaviour.
  - The HTTP/2 driver documents the rule which prevents it — check `state( )` first, and never read
    while it is `Connecting` — and the pool follows it. The request task does not, at either of its
    two readers.
- **What happens if it is not done.** A legitimate cancel or deadline during connection establishment
  runs an unsynchronized copy of a string. Nothing is known to corrupt or crash today, but it is
  undefined behaviour on a path any caller can take: a request cancelled, or timed out by a deadline
  it set, while the connection it rides is still establishing. That holds over TLS, and over
  cleartext HTTP/2 by prior knowledge.
- **Risk, complexity, blast radius.**
  - **Reach:** every request over either protocol, at completion. The change stays inside
    `HttpClientRequestTask.h`, plus a comment on the `ClientConnection` interface. No `TaskBase`,
    pool or interface signature changes.
  - **The shapes:**
    - **(a) Obey the rule at each reader.** One helper reads `state( )` first. While `Connecting`, the
      response keeps the default `NegotiatedProtocol` — `Unknown`, with no identifier — which the type
      already defines as "not settled". Otherwise the helper copies the published value. Both readers
      use it, and the interface documents the rule.
      - What a caller sees: a request which failed before its connection finished negotiating
        reports an `Unknown` protocol and an empty ALPN. Today that read is a race, so it has no
        defined value to preserve.
    - **(a′) The HTTP/2 driver's getter enforces the rule.** `negotiated( )` loads the state. While it
      reads `Connecting`, it returns a function-local static default; otherwise it returns the
      published value.
      - Every reader, present and future, is safe, including those L7 and L8 will add.
      - What a caller sees is the same as with (a).
      - It is a behavioural change to a driver method, and the driver's header gates 15 modules
        where (a) gates 8. Its deterministic test is cheap: an unstarted driver's getter returns the
        default, not its own member.
    - **(a″) The connection publishes the value itself, and the getter reads the publication.** Put to
      the maintainer on 2026-09-29, when they asked what a future caller of the getter would expect.
      - `continueAfterConnected( )` writes the value once, and then sets an atomic flag. Both getters,
        the establishment base's and the HTTP/2 driver's, read the flag first. Before it is set they
        return a default `NegotiatedProtocol`, `Unknown` with no identifier. After it they return the
        value, which is never written again.
      - The getter is then safe from any thread at any time. It reports the protocol from the moment
        the handshake settles it, which is what the interface already promises: "Unknown with no
        identifier until ALPN has resolved".
      - Neither request-task reader needs a rule. A request which failed before the handshake
        completed reports `Unknown`. One which failed after it reports what was negotiated — `h2`, or
        `http/1.1` on the fallback — even before `Ready`.
      - **Reach:** one atomic store in every client connection's establishment, and one atomic load
        in every read of an HTTP/2 connection's value. `ClientConnectionTaskBase.h` is compiled by 21
        modules, and the gate covers them.
    - **(b) A separately published immutable snapshot.** The connection publishes its negotiated value
      through a synchronized or immutable holder, and the getter returns that. It is a redesign of the
      getter on the interface and in both drivers, for readers which need provisional metadata during
      establishment, and none does today.
    - **(c) Wait for negotiation before completing a failed request.** Rejected by Astra and by this
      plan: it would delay a cancel or a timeout for metadata which does not exist yet.
- **The undecided part.** Which shape: (a), (a′), both, or (b).
- **Recommendation: (a) and (a′) together.**
  - **(a) is the direct fix.** It has deterministic evidence: a probe which counts reads shows the
    request task never reads while `Connecting`.
  - **(a′) closes the class.** This is the rule's second breach: the pool's reader was H04, fixed at
    the reader by H04b, and the request's is U01. `AGENTS.md` makes a repeat a decision to take
    structurally, not a rule to document again. L7 and L8 will add readers.
  - **Together** they cost a few lines in the driver, one driver test, and a gate of 15 modules.
  - **The plan's one-off review recommended (a) alone,** reversing to (a′) "if a third reader appears
    which does not read `state( )` first". The orchestrator counts two already.
  - **Reverses to (a) alone** if the maintainer would rather not change a driver method or widen the
    gate for a rule the interface now documents. **To (b)** only if some caller must read safe
    provisional metadata during establishment, which none does.
- **Taken, 2026-09-29: (a″).** Before deciding, the maintainer asked three things: what a future
  caller of the getter would expect, whether "not until `Ready`" is part of its contract, and what the
  HTTP/2 standard says. The answers, checked at the source:
  - **The contract.** The interface promises "Unknown with no identifier until ALPN has resolved"
    (`ClientConnection.h:362`), and restricts no caller. The rule "never read while `Connecting`"
    appears only in the HTTP/2 driver's class comment. So the driver did not honour the interface
    as written.
  - **The standard.**
    - RFC 9113 §3.2 selects HTTP/2 over TLS by ALPN, in the TLS handshake, and RFC 7301 §3.2 makes
      the selected protocol definitive for the connection. For cleartext, §3.3 settles it by prior
      knowledge.
    - §3.4 lets the client send immediately after its own preface.
    - So the protocol is known from the end of the handshake. `Ready` is the driver's own state,
      published after it has built its session and issued its preface, and no RFC knows it.
    - The text is saved verbatim in the evidence directory, `logs/astra4/rfc/`.
  - **So (a′) was withdrawn.** It would have reported `Unknown` between the handshake and `Ready`,
    after the protocol was settled. That is safe, but wrong against both the interface and the
    standard, and wrong in the window where a reader checking the negotiation would look first.
  - **And (a) alone was not enough.** It leaves the getter unsafe during establishment, behind a rule
    the interface never states — the rule two readers have now missed.
  - **(a″) is the contract-preserving form of (a′).** Its mechanism has real content, so the lane
    writes a design note first, and an Opus reviewer agrees it before it is coded (workflow §4.1).
    The note re-verifies the single write, and settles where the flag lives and how the default is
    stored.
  - **It reverses** if the design note finds the value can be written more than once. A single
    publication cannot cover a second write, and the decision goes back to the maintainer.

## 2. The change-set

| Change-set | Finding | Lane | Starts |
|---|---|---|---|
| **CS-9** | U01 | lane 1, `astra4-cs9` | D1 taken 2026-09-29; the design note first |

The plan gives its files, tests and gate.

## 3. As it lands

*(To be written when CS-9 lands.)*
