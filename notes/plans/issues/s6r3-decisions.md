# S6R.3 — the three decisions, with their costs

**Status:** decision document, 2026-09-22. **Nothing implemented; nothing under `src/` was touched
and nothing was built.** These three are separated from the rest of the astra remediation because
none is a bug fix: H06 reverses a behaviour a test pins, H08 needs a rule the design never wrote,
and H11 is conformance for its own sake. Each needs an answer before a lane can start.

**Scope:** H06, H08, H11 from `astra-review-verification-record.md` §5.

**Provenance, because the recurring failure here is a right conclusion on a wrong premise.**
Everything in §1–§4 below was read at the source in this worktree by the author, including the RFC
text in §4, which was fetched rather than recalled. Three things are carried from the verification
record and are marked where they appear: H07's `failWith` guard, N1's 64 MB h1 body limit, and the
astra review's own wording. §6 lists what the record got wrong.

---

## 0. H06 and H08 are one concept, and saying so is most of the answer

The concept is **"what has the caller's sink actually seen, and is that the whole body"**. Today
nothing in the client holds that. The request task mirrors the *stream* onto the sink — the stream
closed, so `onComplete( )` fires — and neither the task nor the session ever compares what the sink
received against what arrived.

That single omission produces both findings, from opposite ends of the same accounting:

- **H06** is the bytes the sink did **not** take. They sit in `m_pendingDownload`
  (`HttpClientRequestTask.h:317`), `applyClosed( )` never looks at them, and the request is a
  success.
- **H08** is the bytes the sink **did** take. Nothing records that they escaped, so the session's
  retry rule cannot consult it and a second attempt appends to a prefix the caller already has.

So they take one mechanism, and §3 gives it. Read §1 and §2 first; the trade only makes sense once
both halves are on the page.

---

## 1. H06 — a partially consumed body is truncated and reported as success

### The decision

**At stream close with bytes still outstanding to the sink, is the request a success?** Today it is.
Saying it is not reverses the assertion in
`HttpClientRequestTask_StreamingSinkCreditsOnlyWhatItTookTests`.

### What the code does now

`applyData( )` (`:876`) pushes each block onto `m_pendingDownload` and defers `offerToSink( )`.
`offerToSink( )` (`:952`) offers the front block **once**: on a partial take it advances
`offset1( )` and `break`s out of its own loop. It credits only what was taken, and only while the
stream is open (`:987`).

`applyClosed( )` (`:1114`) sets `m_isStreamClosed`, cancels every timer, and — with no reference to
`m_pendingDownload` at all — defers `sink -> onComplete( )` (`:1137`), then calls `answerOnClosed( )`,
which on a clean close runs `completeResponse( )` and sets `m_isCompletionPending` (`:1272`). The
queue is destroyed with the task.

Traced through the pinning test: the sink takes 3 of `"abcdefgh"`, then 3 of the remaining
`"defgh"`; at close `m_pendingDownload` holds `"gh"` and `"ij"`. **Four bytes are dropped, the sink
is told `onComplete( )` — "the body is complete" (`ClientTypes.h:330`) — and the task succeeds.**

### What the test actually encodes, which is not what it looks like

This matters more than the rest of the section, because the record's framing ("the current behaviour
is pinned by a test, so fixing it overturns a decision") invites the reader to assume a decision was
taken about byte loss. **No such decision is recorded anywhere, and three independent sources say
the decision taken was a different one.**

1. **The test's own comment** (`TestHttpClientRequestTask.h:1385-1395`) states its subject in
   capitals: *"THE NUMBER IS THE ASSERTION … a task which credited what it was OFFERED would report
   eight and a task which credits what was TAKEN reports three per round - and since
   over-acknowledging fails the whole connection rather than the stream, the difference is not
   cosmetic. The remainder is re-offered before anything newer, which is what keeps the streamed
   body in order."* Credit accounting and re-offer ordering. Byte loss is not mentioned.

2. **The commit that wrote it**, `0c27dc3` (S5.1, 2026-09-19), says the same and no more: *"only what
   the sink actually consumed is credited, because over-acknowledging fails the whole connection
   rather than the stream, and the remainder is re-offered before anything newer so a streamed body
   stays in order."* Nothing in that message, and nothing in design §5.3, says a remainder
   outstanding at close may be dropped, or that such a request still succeeds.

3. **The sibling contract test asserts the opposite for the same sink type.**
   `ClientContracts_BodySourceAndSinkStreamingTests` (`TestClientContracts.h:944-974`, same test
   module) drives `StubBodySink` in a `while` loop until the block is exhausted and requires
   **all five** bytes arrive — under a comment that reads *"a sink which consumes less than it was
   offered is NOT an error … and the remainder is offered again"*. The contract test loops; the
   request task does not.

**So `"abcdef"` and `requireSucceeded` at `:1449-1456` are the fixture's observed output, not a
decision.** The decision the test encodes — credit what was taken, re-offer in order — survives the
fix untouched: under any option below, the two pre-close rounds still credit 3 and then 6, and
`consumedTotal( ) == 6` still holds, because the drained tail is credited to nothing (the stream is
closed; `:987` suppresses it). Only the two final lines change.

That is the honest answer to "establish what decision it encodes": **it encodes the credit rule, and
the loss rode along.** This is a correction to a premise, not a reversal of a decision.

### The two halves, which cost very different amounts

The record prices H06 at "100+, contract change". That is the price of the *second* half only.

**(a) Truncation at close.** Deterministic, no timing, no flow control, entirely inside
`HttpClientRequestTask.h`. This is what the test exhibits and what astra's headline is about.

**(b) No readiness or resume signal.** A sink that returns zero and becomes ready later has no way
to say so. This is **already recorded** — `http2-l5-review-record.md:406-411`, nit (e) — which says
it *"belongs in `body-source-readiness-deferral.md` as the sibling it is"*. **It was never added
there**: that file names only `BodySource`. Closing (b) means a new method on `BodySink`, i.e. the
frozen `ClientTypes.h`. It is the expensive half and it is not needed to stop losing bytes.

### Options for half (a)

**A0 — leave it.** Cost zero. What it buys: nothing. What it costs: the client can hand a caller a
truncated body and call it a success, with no signal of any kind. Listed because it must be, not
because it is arguable.

**A1 — fail at close if anything is outstanding.** In `applyClosed( )`, before `answerOnClosed( )`,
test `m_pendingDownload.empty( )` and `failWith( )` a truncation exception if not. ~10 lines, apply
phase only, no sink call, no dependency on anything else. **What it costs:** it fails requests a
single further offer would have completed — including the pinning test's own scenario, where the
sink would have taken the last four bytes in two more rounds. It converts a contract the sink is
honouring (partial consumption) into a request failure.

**A2 — offer the remainder at close, then fail only if the sink still will not take it.** In the
deferred phase, loop: offer the front block; if a full pass consumed nothing and the queue is not
empty, stop. Then `onComplete( )` if the queue is empty, and fail the request if it is not.
~25–40 lines. This is the contract's letter — *"the remainder is offered again"* has a last
"again", and stream close is it.

**A2 has a prerequisite and it is H07.** The drain cannot run in the apply phase: rule L4 and the
class comment (`:140-180`) forbid calling the caller's sink under the task lock, which is the whole
reason `offerToSink( )` is deferred today. So the verdict is reached in the deferred phase, by which
time `answerOnClosed( )` has already set `m_isCompletionPending`, and `failWith( )` returns at its
first line when that flag is set (`:1395-1398`). **A deferred action cannot currently turn a pending
success into a failure — which is exactly H07.** So either H07 (S6R.2) lands first and A2 uses its
mechanism, or A2 builds a second one. *(H07's guard was read at the source for this document; the
finding itself is the record's.)*

### What each option breaks

- `HttpClientRequestTask_StreamingSinkCreditsOnlyWhatItTookTests` — under A1 it becomes a failure
  case; under A2 the last two lines become `received( ) == "abcdefghij"` and the credit assertions
  are unchanged. **No other test in the tree installs a `BodySink`** (grepped: the only other
  users are `TestClientContracts.h`, which drives the sink directly with no request task, and a
  helper parameter in `TestClientSession.h:216` that no case ever passes).
- The design record. §5.3 does not contradict the fix — it is silent on an outstanding remainder at
  close. **The amendment is an addition, not a reversal**, which is a materially smaller act than
  "amend the design record first" implies.

### Recommendation

**Take A2, and sequence it after H07.** Reasons, in order of weight:

1. The behaviour being reversed was never decided. §1's three sources show the test pins the credit
   rule, which A2 leaves alone.
2. Silent truncation reported as success is the worst failure mode a client has. It cannot be
   detected by the caller: `body( )` is empty by design in streamed mode, so there is nothing to
   measure the sink's tally against.
3. A2 costs one more offer per request over A1 and fails strictly fewer requests. The only sink A2
   fails is one that will not accept its own body, which is a genuine error.
4. A2's dependency on H07 is a sequencing note, not a cost: H07 is already in S6R.2, ahead of this.

**Do not close half (b) here.** It is a change to `ClientTypes.h`, no caller in the tree needs it,
and the immediate hazard it addresses — an h2 stream stalling on a sink that never becomes ready —
already ends in a clean `TimeoutException` from the idle or total timer, which is the same reason
the `BodySource` sibling was deferred. **Instead, add the `BodySink` half to
`body-source-readiness-deferral.md`**, which L5 nit (e) asked for and which was not done.

### What becomes possible, and what does not

Afterwards a streamed body either arrives in full or the request fails, which is the precondition
for anything that decodes or checksums a streamed body (the decoder programme, H24/H25). It does
**not** give a sink a way to pause and resume — that stays half (b) — and it does not change h1's
lack of backpressure (§6).

---

## 2. H08 — a retry reuses a sink that has already been told the body was complete

### The decision

**May a transparent retry reuse a `BodySink`, and what does `onComplete( )` promise?** Today: yes,
and nothing.

### What the code does now

`applyClosed( )` defers `sink -> onComplete( )` (`:1137`) whenever a sink is installed, with **no
test of the outcome** — although `m_outcome = outcomeOnClosed( event )` is computed five lines above
it (`:1130`) and is exactly the value that would decide. The session then retries:
`continuationTask( )` (`ClientSession.h:1376`) sees
`m_hop -> exception( )`, calls `chkPrepareRetry( )` (`:1151`), and `startHop( )` (`:1085`) builds a
fresh `HttpClientRequestTaskImpl` handed **the same `m_bodySink`** (`:1115`).

`chkPrepareRetry( )` delegates to `chkRequestMayBeReplayed( )` (`ConnectionPool.h:430`), which tests
the attempt count, `request.isReplayable( )` — which is `nullptr == m_bodySource || canRewind( )`,
purely the **request** body — `context.isRetryable`, and the idempotent-on-connection-loss knob.
**Nothing anywhere asks whether response bytes have escaped.** *(All four links read at the source.)*

### What `onComplete( )` means today, enumerated

| Terminal situation | `onComplete( )`? | Is it true? |
|---|---|---|
| Full body delivered | yes | yes |
| Body truncated (H06) | yes | no — bytes were lost |
| Timeout or cancel | yes, on the later `Closed` that follows our own RST_STREAM | no — the caller was already failed |
| Retryable bounce, then a retry (H08) | yes, then more `onData( )` on the next hop | no, twice |
| Pool `acquire( )` failed, `submit( )` threw, `submit( )` refused | **no** | — |

It fires in four of five terminal situations and is true in one. The fifth does not fire at all.
This is the concrete shape of "the request task mirrors the stream onto the sink".

### The live path is the default one, not a corner

Design §5.4 (`http2-design.md:1027-1040`) records that the pool dispatches the first request of a key
onto the `Connecting` placeholder so its HEADERS ride the preface; over a connection that selects
`http/1.1`, the h2 task hands the stream to the h1 driver and answers the rider through
`closeSubmissions( )` → `failSubmission( )` → `sink -> onClosed( …, connection_aborted, isRetryable
= true )` (`Http2ConnectionTask.h:2302`, `:1088`). `isRetryable` is true, so
`chkRequestMayBeReplayed( )` returns from its `isRetryable` limb with the default policy and no knob
set. An h1 download with a sink is fully supported — the pinning test itself runs on
`HttpProtocol::Http11`.

**So on every first request to an origin that does not speak h2, a caller's sink is told
`onComplete( )` with zero bytes and is then given the whole body.** No option, no knob, no race.

The other face — a visible prefix, connection loss, then the retried body appended to it — needs
`retryIdempotentOnConnectionLoss`, which defaults off (`ConnectionPool.h:268`). It is the worse
corruption and the rarer configuration.

### Options

**B0 — leave it.** Zero cost, and the contract at `ClientTypes.h:330` is simply false. Not tenable
once it is written down.

**B1 — one rule in the request task: `onComplete( )` if and only if the full body arrived.** The
same edit A2 makes. Delete the unconditional push at `:1137`; the deferred drain calls
`onComplete( )` only on a clean close with an empty queue. This **eliminates the default path
above entirely** — the bounce is not a clean close, so no terminal callback precedes the retry —
and makes the callback's declared meaning true without changing a word of the contract. ~0 extra
lines over A2.

**B2 — B1 plus a retry clause.** Add `sinkDelivered( )` beside `isRetryable( )` and `outcome( )`
(`HttpClientRequestTask.h:1837-1845`, already the house idiom for "what the session reads off a
hop"), and one clause in `chkPrepareRetry( )`: refuse a retry when the hop delivered bytes to the
sink. ~5 lines in the session, ~4 in the task. This closes the visible-prefix face too.

  **The clause belongs in `chkPrepareRetry( )` and not in the shared predicate.**
  `chkRequestMayBeReplayed( )` is deliberately one rule for both halves of the retry
  (`ConnectionPool.h:396-412`), and the pool's half never has a sink — it does not know sinks exist.
  Putting the flag in `RetryContext` would add a field one of its two callers must always leave
  false, which reads as a bug to the next reader. The shared rule is about the *request*; "response
  bytes escaped" is about the *hop*, and only the session sees hops.

**B3 — a session-owned wrapper sink.** The session interposes its own `BodySink` between the caller
and each hop: forward `onData( )` and its return value, count the bytes, swallow the hop's
`onComplete( )`, and deliver exactly one at the end of the chain. ~40–60 lines, all in
`ClientSession.h`, nothing in the request task, nothing in the contract. Fixes H08 completely and
**does not touch H06** — the wrapper sees only what was delivered, and a truncated body is
indistinguishable from a short one to anything that does not know what arrived from the wire.

**B4 — change the frozen contract: let a sink advertise a reset.** Add `canReset( ) / reset( )` to
`BodySink` and permit a transparent retry after a visible prefix when the sink supports it. This is
the option the record names as the second shape. It changes every implementer of a published IID,
and the file's own header (`ClientTypes.h:46-50`) says such a change *"is negotiated rather than made
unilaterally"* — this document is that negotiation, so the freeze is not itself an objection. The
objection is that **it buys nothing today**: no sink in or out of the tree implements reset, and the
only case it unlocks is behind a knob that is off by default.

### Recommendation

**Take B2. Record B4 as a deferral; do not build it.**

B1 alone leaves the visible-prefix corruption standing behind a knob, and a knob that silently
corrupts is worse than one that refuses. B2's extra cost over B1 is under ten lines. B3 is a clean
piece of design and I would take it if H06 were not also on the table — but it duplicates, at the
session, state the request task must hold anyway for H06, and two places holding "what the sink has
seen" is the shape this finding is made of.

**Blast radius of B2 is close to nil.** The ALPN bounce delivers zero bytes, so `sinkDelivered( )`
is zero and the retry proceeds exactly as today — the default path does not change. The only
refused retry is one after a genuine mid-body loss with the non-default knob on. **No test in the
tree installs a sink at the session level**, so nothing goes red and the change-set owes its own
cases: a terminal-state-checking sink over the fallback (asserting a single `onComplete( )` after
the body, not two), and a mid-body loss with the knob on, asserting the request fails rather than
appending.

### What becomes possible, and what does not

`onComplete( )` becomes a callback a caller can build on: it means the body arrived. A caller who
wants a terminal callback *whatever* happened does not get one — that is `onComplete( outcome )`,
a contract change, and belongs in the same deferral as B4. Resumable streamed downloads across a
connection loss become the caller's problem, via `Range`, which is where they belong until a sink
can express a reset.

---

## 3. The one mechanism, and what it costs

| Piece | Where | Serves | Lines |
|---|---|---|---|
| Drain the remainder at close, with a no-progress stop | `HttpClientRequestTask.h`, deferred phase | H06 | ~20 |
| `onComplete( )` iff the queue drained on a clean close | same site, replacing `:1137` | H06 + H08 | ~5 |
| Fail the request when it did not | same, needs H07's mechanism | H06 | ~10 |
| `sinkDelivered( )` accessor | beside `isRetryable( )`, `:1837` | H08 | ~4 |
| One clause in `chkPrepareRetry( )` | `ClientSession.h:1151` | H08 | ~5 |

Roughly 45 lines across two files, one new counter and one new accessor, no change to
`ClientTypes.h`, and one prerequisite (H07). Everything above rests on a single piece of state the
request task does not have today: **how many response bytes the sink has taken, and how many are
still owed to it.**

**What the single mechanism does not cover, said plainly:** half (b) of H06 (sink readiness), a
terminal callback on failure, and retry after a visible prefix. All three are `ClientTypes.h`
changes, all three are deferrals, and none is needed to stop losing bytes or to stop lying about
completion.

---

## 4. H11 — the decoder accepts a missing mandatory HPACK size update

### The decision

**Do we want conformance here for its own sake?** There is nothing else in it: no safety
consequence, no interop break, no memory growth (the retained table stays bounded by its old
capacity), and — verified below — no way to reach it with anything this library ships.

### What the code does now

`setMaxDynamicTableSize( )` (`HpackDecoder.h:161`) lowers the ceiling on the peer's future size
updates and deliberately does not shrink the table. `decodeSizeUpdate( )` (`:538`) enforces two
things: the update must open the block (`:553`) and must not exceed what we advertised (`:566`).
It never *requires* one. The session drops the ceiling on the acknowledgement of our SETTINGS
(`Session.h:2580-2589`, contract 3).

### What the RFC actually says — fetched, not recalled

RFC 9113 §4.3.1 carries **both** duties, which is the detail the whole decision turns on:

> "Once an endpoint acknowledges a change to SETTINGS_HEADER_TABLE_SIZE that reduces the maximum
> below the current size of the dynamic table, its HPACK encoder MUST start the next field block
> with a Dynamic Table Size Update instruction…"

> "An endpoint MUST treat a field block that follows an acknowledgment of the reduction to the
> maximum dynamic table size as a connection error of type COMPRESSION_ERROR if it does not start
> with a conformant Dynamic Table Size Update instruction."

Two precisions the review's own wording loses:

1. **The decoder-side MUST is armed only by a reduction *below the current size of the dynamic
   table*.** "The reduction" in the second sentence refers back to the first. So astra's trigger —
   "advertise table size zero, receive the ACK, then receive a header block without the reduction
   instruction" — is a violation only if our decoder's table was non-empty at the moment of the ack.
   An implementation that required the update on any acknowledged reduction would reject conformant
   peers.
2. **RFC 7541 §4.2 is not the wrong citation, it is the incomplete one.** It states the encoder's
   duty, which is true and which the comment uses correctly. What the comment then does is take
   "a conforming peer has itself stopped naming what an eviction here would drop" — a sound answer
   to *is it safe not to evict?* — as if it answered *may we accept a block without the update?*,
   which is a different question with a different answer in a different document. **The
   justification is wrong in kind, not stale**: no fact changed under it; it argues the table's
   safety and concludes about the decoder's duty.

### Reachability, verified at the source

The SETTINGS we send are `m_profile.settings` plus `SETTINGS_ENABLE_PUSH = 0`
(`Session.h:1353-1391`). `advertisedHeaderTableSize( )` (`:1220`) falls back to
`HEADER_TABLE_SIZE_DEFAULT` = 4096 when the profile names none, and `applyAcknowledgedSettings( )`
iterates only the settings we actually sent — so **a default `Http2Profile` never calls
`setMaxDynamicTableSize( )` at all**, and the ceiling and the capacity both stay at 4096. There is
no reduction, so there is nothing to arm.

Stronger than "unreachable with the default profile": every browser fingerprint the design records
(`http2-design.md:1553-1554`) advertises `1:65536` — an **increase**, which the MUST never covers.
So the L7 profile work does not reach it either. Arming it needs a profile advertising a table size
below the live table size, which nothing in this tree constructs and no shipped fingerprint uses.

### What a fix would cost, and what it would break

~25–35 lines in `HpackDecoder.h` and one call site: record, at ack time, whether the new ceiling is
below `m_dynamicTable.size( )` (`HpackDynamicTable.h:460` already exposes it); require the next
block to open with a conformant update; clear the requirement when it does.

**It breaks nothing.** `Hpack_SizeUpdateTests` (`TestHpack.h:1825`) never calls
`setMaxDynamicTableSize( )` — grepped: no test in the tree does — and the session cases that
advertise a table size (`TestSession.h:1025`, `:3043`) either never decode a block after the ack or
decode one that does open with an update. That cuts both ways: **the current lenient behaviour is
unpinned, and so a fix would be too** until it ships its own cases.

### Recommendation

**Leave the behaviour. Rewrite the justification. Record it as a conformance deferral.**

Not because the code is right — it is lenient where an RFC says MUST error — but because every
reason to spend the lines is absent and one reason not to is present:

1. Unreachable with anything the library builds or ships, today or at L7.
2. No safety consequence: leniency here cannot grow memory, desynchronise the table, or mislead a
   caller. We accept something we could reject.
3. **Implementing a MUST-error introduces a false-positive risk where none exists.** The arming
   condition is subtle — below the *current table size*, at the moment of the *ack* — and a fix that
   gets it slightly wrong tears down live connections against conformant peers. Today the failure
   mode is "we were tolerant"; afterwards it is "we killed the connection". For a condition nothing
   can reach, that trade is negative.
4. It is a decoder-side MUST about a peer's encoder being sloppy in a way that harms nobody. No
   interop break exists in either direction.

**What is owed, and it is not optional:** the two doc comments at `HpackDecoder.h:120-132` and
`:138-160` — astra cites the second, but the RFC 7541 §4.2 argument opens in the first — must stop reading
as though the question were settled. It should cite RFC 9113 §4.3.1, quote the decoder-side MUST,
state that we deliberately do not enforce it, and state the arming condition — so that the next
reader finds a recorded choice rather than an argument that answers a different question. Plus a
short entry in `issues/` naming the reopen trigger: **the first profile that advertises
`SETTINGS_HEADER_TABLE_SIZE` below 4096, or below the live table size.**

Cost of the recommendation: one comment and one short record. Cost of the alternative: ~30 lines,
new cases, and a new way to fail a connection.

---

## 5. Sequencing, and what these touch elsewhere

- **H07 (S6R.2) is a prerequisite of H06's A2.** Without it a deferred action cannot fail a request
  whose success is already pending. If the maintainer prefers H06 first, it must build H07's
  mechanism, and then S6R.2 inherits it rather than the other way round. Taking S6R.2 first is free.
- **H06 and H08 are one change-set** under §3, by the project's rule that a change-set has one
  theme. Both are core-path changes to `HttpClientRequestTask.h` and gate on the whole suite.
- **N1 belongs with them.** The verification record already says so: the h1 driver's 64 MB body
  limit applies to streamed bodies too, so a streamed download over 64 MB fails on h1 and succeeds
  on h2. It is the same surface and the same reviewer's attention. *(N1's figures are the record's;
  `Http1Codec.h:212` and the `body_limit` call at `detail/Http1CodecBeastImpl.h:179` were confirmed
  here.)*
- **H11 touches nothing else.** It is not a prerequisite of H10 or H12, which concern the encoder.
- **The decoder programme (H24, H25)** wants H06 landed: decoding or checksumming a streamed body is
  only meaningful once "the body arrived in full" is a property the client enforces.
- **L6 finding 4a** interacts with H08 only in that both concern the fallback bounce. 4a removes the
  wasted attempt; H08 removes the false terminal callback on it. Neither needs the other.

---

## 6. Corrections to the verification record and the brief

Recorded in the same spirit as the record's own §2 — so that it is not treated as uniformly right.

- **H06's "no resume path" argument is HTTP/2-only.** The record and the brief say *"a sink that
  consumes nothing sends no credit, so no further DATA arrives"*. On HTTP/1.1 that is false in both
  limbs: `Http1ConnectionTaskT::consumed( )` (`Http1ConnectionTask.h:1587`) is a documented no-op —
  *"HTTP/1.1 has no flow control window"* — and `onReadCompleted( )` re-arms `scheduleRead( )`
  unconditionally (`:1020`). So over h1 a zero-consuming sink applies **no backpressure at all**;
  blocks accumulate in `m_pendingDownload`, bounded only by Beast's `body_limit` (N1's 64 MB), and
  the remainder *is* re-offered on every new chunk. The truncation-at-close defect is the same on
  both protocols; the stall is h2's and the unbounded-ish buffering is h1's.
  **The consequence for the decision:** `offerToSink( )`'s own comment (`:942-951`) — *"a sink which
  takes nothing closes the stream window and the server stops sending. That is the design 5.3 chain
  working"* — is true of h2 and false of h1, and the pinning test runs on `Http11` against a probe
  that records credit no real h1 driver would use. Worth an amendment whichever option is taken.

- **H06 is priced as one thing and is two.** "100+, contract change" is half (b). Half (a) — the
  truncation the finding is named for — is ~25–40 lines and needs no contract change.

- **"Amend the design record first" overstates it.** Design §5.3 does not state the behaviour being
  reversed; it is silent on it. The amendment is an addition.

- **The record's "explicitly accepting loss" (carried from astra) is not supported.** §1 above gives
  the three sources. The test pins credit accounting; the loss is a by-product of the fixture.

- **L5 nit (e) is owed and was not done.** It directed the `BodySink` readiness gap into
  `body-source-readiness-deferral.md`; that file names only `BodySource`.

- **H08 is broader than "a retry reuses a sink".** `onComplete( )` is also delivered after a timeout
  or a cancel that the caller has already been told about, and is **not** delivered at all when the
  pool fails, `submit( )` throws, or `submit( )` is refused. The table in §2 is the full shape.

- **H11's arming condition is narrower than astra states.** Astra's trigger omits "below the current
  size of the dynamic table"; see §4. A fix written to astra's wording would reject conformant peers.

- **H11's citation is incomplete rather than wrong.** RFC 7541 §4.2 does state the encoder's duty
  and the comment uses it correctly; the defect is that it answers a question about the table and
  concludes about the decoder. RFC 9113 §4.3.1 carries *both* MUSTs, which is worth knowing before
  anyone rewrites the comment to cite "the other RFC".

---

## 7. What this document does not establish

Nothing here was built or run — the machine was carrying another lane's compile. Every claim is from
reading, and the line numbers are this worktree's at commit `4aeced3`. In particular: the ALPN-bounce
path in §2 is traced through four functions and a design record, not observed; no case in the tree
installs a sink at the session level, so **nothing in the suite would have caught it**, which is
itself the argument for the cases §2 owes.

---

## 8. The three answers, once the trade above has been read

- **H06 — fix it, with option A2, sequenced after H07.** The behaviour being reversed was never
  decided; the test pins the credit rule and the loss rode along, and the credit rule survives the
  fix. ~35 lines. Contingent on H07 landing first, or on building H07's mechanism here.
- **H08 — fix it, with option B2, and leave the `BodySink` contract alone.** One rule
  (`onComplete( )` iff the body arrived) plus one accessor and one clause in `chkPrepareRetry( )`.
  It removes a contract violation that fires today on **every** first request to an h2-less origin,
  and its blast radius on the default configuration is nil. Record the reset-capable sink (B4) and
  `onComplete( outcome )` as deferrals.
- **H11 — leave it, and rewrite the justification.** Unreachable with anything the library builds or
  ships, no safety consequence, no interop break, and enforcing the MUST would add a way to kill a
  connection where today there is none. What is owed is a comment that cites RFC 9113 §4.3.1 and
  says the leniency is deliberate, plus a record naming the reopen trigger. **No code change is
  recommended, and none should be manufactured.**

Two things are owed regardless of which way any of these three goes: the `BodySink` half of
`body-source-readiness-deferral.md` (L5 nit (e), never done), and the amendment to
`offerToSink( )`'s comment, which states an h2 property as though it held on both protocols.
