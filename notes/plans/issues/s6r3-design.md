# S6R.3 — design for the five that needed a decision first

**Status:** design, 2026-09-22. **Nothing implemented; nothing under `src/` was touched and nothing
was built** — another lane was compiling and the machine has two cores.

**Scope:** H06, H08, H09, H10 and H04a from `astra-review-verification-record.md`. H11 is settled
and is not designed here — see §5.

**What this design is not.** It is not a restatement of the findings — read the verification record
for those, and `s6r3-decisions.md` for the trade behind H06 and H08. It is the *intended change* for
each, the reason that shape was chosen over the alternatives, the boundaries it rests on, what could
go wrong, and the test with whether it can be shown red-before-green-after.

---

## 0. Provenance — what was read, and what was taken from a record

An independent reviewer will read this against the source and will find the difference, so it is
stated first rather than buried. Line numbers are this worktree's at `c90f84e`.

**The base moved while this was being written, and the numbers were re-checked against where it
moved to.** S6R.1's nine fixes merged into `lazari2` as `3bcf21e` during the session. Every claim
below holds unchanged on that tip; only line numbers shift, and only in the four files S6R.1 touched.
The anchors, so either base is checkable: `ConnectionPool.h` — `refreshEntry( )` 1206→1217,
`runActions( )`'s schedules loop 1756→1767, `startConnection( )`'s disposed arm 1835→1846,
`disposeInternal( )`'s task collection 2078→2089 (and `m_isDisposed` at 808 and `resolveDriver( )`'s
`if( attempt.driver )` at 926 do not move); `Session.h` — `queueHeaderBlock( )` 3606→3645,
`forceCloseStream( )` 3453→3492, the SETTINGS ACK 2291→2330, `setDynamicTableCapacity( )` 2474→2513,
`m_peerMaxFrameSize = value` 2529→2568 (and `produce( )`'s control-queue line at 864 and
`raiseConnectionError( )`'s clear at 1530 do not move); `ClientSession.h` — `chkPrepareRetry( )`
1151→1181, `chkPrepareNextHop( )` 1290→1320, the driver lambda 1730→1760, the sink hand-off
1115→1145. **`HttpClientRequestTask.h`, `Http2ConnectionTask.h`, `ClientConnectionTaskBase.h`,
`HpackEncoder.h`, `TaskBase.h` and `ExecutionQueueImpl.h` are untouched by that merge**, so every
§1, §2 and §4.1 citation is exact on both. Two facts §4.2 depends on were re-read on the new tip and
still hold: `m_isDisposed` is still a `cpp::ScalarTypeIniter< bool >`, and `disposeInternal( )` still
collects only `entry -> attempt.task` for its cancel sweep. H03a's `forceFlushNoThrow( true )` is now
in the tree, which is the state §4.2 assumes.

**Two more files moved and this section as first written did not say so** (2026-09-22 review):
`TestSession.h` and `TestClientSession.h` gained cases in the S6R.1 merge, so on the tip the peer
`SETTINGS_MAX_FRAME_SIZE = 1024` case is at `TestSession.h:3558-3577`, not `:3227-3245`;
`markDraining( )` in `handleGoAway( )` is at `Session.h:2388`, not `:2349`; and the decoder
registrations §2 cites are at `TestClientSession.h:1766` and `:1811`. Corrected where they are used.

**Re-read for the §4.1 re-gate (2026-09-23, `lazari2` @ `9583c31`):** the pool's `Entry`,
`refreshEntry( )`, `findDispatchable( )`, `canStartConnection( )`, `examineKey( )`, `examineAll( )`,
`runActions( )`, `startConnection( )`, `forgetConnection( )`, `chkCancelEntry( )` and
`onMaintenance( )`, whole; the base's `onProtocolNegotiated( )`, `continueAfterConnected( )` and
`connection( )`; the driver's `onProtocolNegotiated( )`, `onTaskStoppedNothrow( )`,
`publishState( )`, `state( )` and its five `Closed` publications; `TaskBase::notifyReadyImpl( )`,
`getState( )` and `exception( )`; every `ConnectionAttempt` builder in the tree (four) and
`StubConnectionTaskT` whole. Line numbers in §4.1's re-gate, §4.3 and §12 are that tip's; the
factory lambda §4 cites at `ClientSession.h:1730` is at `:1790` there.

**And the decisions document this design leans on is not on the branch this design is on.**
`s6r3-decisions.md` exists only on branch `s6r3-decisions`, at `c4b2f87`; on `lazari2` every
citation of it below resolves to nothing. Either it lands ahead of this design, or this design must
carry what it takes from it — §1 takes A2, B2 and the H07 dependency; §5 takes the whole of H11.
Found by the review; which of the two is the maintainer's call.

**Read at the source by the author, whole functions from their signatures to their ends:**

- the request task's streaming path — `applyData( )`, `offerToSink( )`, `applyClosed( )`,
  `outcomeOnClosed( )`, `answerOnClosed( )`, `failWith( )`, `applyEvents( )`, `runDeferred( )`,
  `cancelStream( )`, `releaseConnection( )`, and the public accessor block at `:1800-1845`;
- the session — `continuationTask( )`, `absorbResponse( )`, `decodeBody( )`, `storeCookies( )`,
  `chkPrepareRetry( )`, **`chkPrepareNextHop( )` from its first line**, `startHop( )`,
  `chkRemainingBudget( )`, and the driver-resolver lambda at `ClientSession.h:1730`;
- `ExecutionQueueImpl::onReady( )` including the lock it takes and the `continuationTask( )` call
  inside it; `TaskBase`'s `notifyReadyImpl( )`, `scheduleNothrow( )`, `requestCancelInternal( )`,
  `scheduleEvenIfAlreadyCanceled( )` and the member declarations of `m_state`/`m_cancelRequested`;
- the h2 engine — `queueHeaderBlock( )`, `produce( )`, `wantsWrite( )`, `submitRequest( )`,
  `submitHeaders( )`, `handleSettings( )`, `applyPeerSettings( )`, `raiseConnectionError( )`,
  `forceCloseStream( )`, `dropQueuedHeaderBlocks( )`, `closeStreamsAbove( )`, `closeEveryStream( )`,
  `sendableBytes( )`, `bodyBytesWanted( )`, and the `QueuedHeaderBlock` declaration and its comment;
- `HpackEncoderT` in full — the constructor, `setDynamicTableCapacity( )`, `encode( )` and
  `emitSizeUpdate( )`;
- the h2 driver — `pumpWrites( )`, `onRead( )`, `onHeaderBlocksProduced( )`, `applyCancel( )`,
  `onProtocolNegotiated( )`, `publishState( )`, `onTaskStoppedNothrow( )`, and every
  `applyCommands( )`/`pumpWrites( )` call site;
- `ClientConnectionTaskBase` — `continueAfterConnected( )`, `onProtocolNegotiated( )`,
  `connection( )`, and every occurrence of `m_connection` (four: one declaration, one write, one
  `.get( )` check on the writing thread, one getter);
- the pool — `resolveDriver( )`, `refreshEntry( )`, `effectiveMaxConnectionsPerKey( )`,
  `examineKey( )`, `runActions( )`, `startConnection( )`'s disposed arm, `disposeInternal( )` in
  full, and the `m_isDisposed` declaration;
- the tests — `HttpClientRequestTask_StreamingSinkCreditsOnlyWhatItTookTests` in full,
  `Session_GoAwayDropsQueuedHeaderBlockTests` in full, the connection-error sibling that precedes
  it, the two `frameTypes( )` ordering cases, the `hpackEncoderTableCapacity( )` cases, and the
  `settle( )` helper;
- **RFC 9113 fetched, not recalled**: §6.5.3's acknowledgement sentence, §4.3.1's decoder-side MUST
  and §4.2's FRAME_SIZE_ERROR sentence are quoted verbatim in §3 from that fetch. The fetch
  **confirms** the decisions document's §4 quotation of the §4.3.1 MUST word for word.

**Taken from a record and NOT re-derived here:** S6R.2's H07 shape (read from `s6r2-design.md`, not
from an implementation, because none exists); the L5 and L6 findings' own history; the TSan round's
32-run negative and its stated reason. Each is marked where it is used.

**One premise of the author's own was formed and then refuted by reading, and it is recorded in §1
rather than deleted**, because the deletion is what hides the lesson.

---

## 1. H06 and H08 — one mechanism: what the sink has seen, and whether that is the whole body

The decisions document argues the shape and the maintainer accepted it: **A2 for H06, B2 for H08,
and the frozen `BodySink` contract left alone.** This section is the change, not the argument.

### 1.1 The change, in five pieces

| Piece | Where | Serves |
|---|---|---|
| A drain at close, in the deferred phase, with a no-progress stop | `applyClosed( )`'s deferred slot, replacing the unconditional `onComplete( )` push at `:1137` | H06 |
| `onComplete( )` only when the queue drained on a **clean** close | same site | H06 + H08 |
| Fail the request when it did not drain | same, through the existing `deferredException` channel | H06 |
| `m_sinkDelivered` counter, incremented in `offerToSink( )` by what was **taken** | `:952-990` | H08 |
| `sinkDelivered( )` accessor, and one clause in `chkPrepareRetry( )` | `HttpClientRequestTask.h:1837`, `ClientSession.h:1151` | H08 |

The new deferred action replaces the lambda at `:1137`; `applyClosed( )`'s other two deferred
pushes — `releaseConnectionSlot( )` and `releaseConnection( )`'s empty lambda — keep their order
behind it.

**The drain is the existing `offerToSink( )` called repeatedly**, not a second copy of it.
`offerToSink( )` already loops while blocks are taken whole and `break`s on a partial take, so one
call does not drain a sink that takes less than a block. The new action calls it until a whole pass
moves nothing. *One precision (2026-09-22 review):* `offerToSink( )` computes `consumed` and returns
nothing (`:952-990`), so the drain has no way to see a pass that moved nothing. It must return the
count it credited from — a private signature change — or the drain compares the front block's
offset and the queue's length across the pass. Either is fine; a lane must not add a second loop.

**The verdict is a throw, not a new field.** If the queue is non-empty when progress stops, the
action throws a truncation exception. `runDeferred( )` catches `std::exception&` and keeps the
first, and `applyEvents( )` already calls `failWith( deferredException, false )` under the lock
afterwards (`:501`, after `cancelStream( reset )` at `:499`). So A2's failure limb costs **no new
plumbing at all** — it reuses the exact channel H07 is being built to make work. `cancelStream( )`
on that path is a no-op here, because `releaseConnection( )` has already reset `m_connection` under
the same lock.

### 1.2 Sequencing against H07, stated as the dependency it is

`applyClosed( )` calls `answerOnClosed( )` **under the lock, before any deferred action runs**, and
on a clean close that runs `completeResponse( )` and sets `m_isCompletionPending = true`
(`:1268-1272`). `failWith( )` returns at its first line when `m_isCompleted || m_isCompletionPending`
(`:1395-1398`). **Both links read at the source.** So today a deferred action cannot turn a pending
success into a failure, and A2's verdict is reached in exactly that phase.

**H07 (S6R.2) must land first.** Its designed shape — two guards in place of one, so that a
*published* completion wins over everything, an *earlier real failure* wins over a later one, and a
*pending success* loses to any failure — is precisely what A2 needs and nothing more. *(H07's shape
is taken from `s6r2-design.md` §2; it is designed, not implemented, so this is a dependency on a
sibling design, not on code in the tree.)*

If the maintainer wants H06 first, A2 must build H07's guards itself and S6R.2 inherits them. That
is strictly worse: the same edit, made by the change-set that does not own it, and then two
change-sets touching `failWith( )`.

### 1.3 Which close drains, and which does not

**Drain and `onComplete( )` only when the close is the answer: `RequestOutcome::Completed ==
m_outcome` and `! m_isCompletionPending && ! m_isCompleted`.** On any other outcome, or when the
caller has already been answered, the sink is told nothing at all.

*Corrected by the 2026-09-22 review.* This rule first read *"Drain and `onComplete( )` only when
`m_outcome == RequestOutcome::Completed`"*, and that condition alone does not deliver the decisions
document's table row for a timeout or a cancel. `Expired` and `Cancelled` are mailbox events
(`:241`, `:591-593`) applied by `applyStopped( )` (`:1303-1345`), which fails the request —
`m_isCompletionPending` with an exception — and never touches `m_outcome`. A clean `Closed` in the
**same batch** behind it — the timer fires as the last DATA lands, and both are posted before the
drain runs — reaches `applyClosed( )` with no error code, so `outcomeOnClosed( )` returns
`Completed` (`:1098-1102`), `m_outcome` takes it (`:1131`), and under the first wording the drain
would run and the sink would be told `onComplete( )` while the caller holds a `TimeoutException`.
`answerOnClosed( )` already guards its own answer with exactly `m_isCompleted ||
m_isCompletionPending` (`:1213`); the drain takes the same guard. The reversed batch,
`[ Closed, Expired ]`, needs nothing: `applyStopped( )` returns at its first line (`:1308`) and the
pending success stands.

Two consequences, both intended:

1. The **ALPN-bounce path stops lying.** The bounce closes the stream with `connection_aborted` and
   `isRetryable = true`, so `outcomeOnClosed( )` (`:1098-1112`) returns `Failed` — not `Completed` —
   and no terminal callback precedes the retry. This is the default path on every first request to
   an origin that does not speak h2, and B1 removes it without a knob.
2. A sink gets **no terminal callback on a failure**. That is the deliberate cost the decisions
   document names: a terminal callback whatever happened is `onComplete( outcome )`, a
   `ClientTypes.h` change, and is deferred with B4.

### 1.4 A premise the author formed and reading refuted — recorded, not deleted

Before reading `chkPrepareNextHop( )` the author concluded from two call sites — `startHop( )` hands
`m_bodySink` to every hop (`ClientSession.h:1115`), and `applyData( )` pushes to the sink with no
test of the response status — that a **redirected** streamed download would concatenate every hop's
entity body into the caller's sink and deliver one `onComplete( )` per hop, and that B2 therefore
fixed only half of its own headline.

**It is false.** `chkPrepareNextHop( )` **begins** with `if( m_bodySink ) return false;`, under the
comment "a sink and a redirect cannot both be honoured" (`ClientSession.h:1290-1299`). A streamed
request never follows a redirect; the 3xx is returned to the caller as the response. So a sink
session has exactly one kind of multi-hop chain — the **retry** — which is H08 and nothing else.
The decisions document is right and the objection does not exist.

This is the same failure mode §4 of the S6R.1 design records for H14: two call sites enumerated, the
function not opened at its signature. It cost nothing here only because it was checked before it was
written down.

### 1.5 What breaks, precisely — and the brief's wording is one line too generous

`HttpClientRequestTask_StreamingSinkCreditsOnlyWhatItTookTests` is the only case in the tree that
installs a `BodySink` on a request task. *(Verified independently of the decisions document: the only
`BodySink` implementer in the tests is `StubBodySink` in `TestClientContracts.h:155`; its two uses
are the contract test at `:950`, which drives the sink with no request task, and this case at
`TestHttpClientRequestTask.h:1412`; `TestClientSession.h:216` has a defaulted parameter no case
passes.)*

The brief and the decisions document both say the pinning test's **two** final lines move. Traced
through the fixture, **exactly one assertion changes under A2**:

- `UTF_REQUIRE_EQUAL( sink -> received(), "abcdef" )` → `"abcdefghij"`. **Changes.**
- `requireSucceeded( task )` — **unchanged.** A2 succeeds here, because this sink takes three bytes
  per call and the remainder is two blocks of two — `"gh"` at offset 6 of the first block, and
  `"ij"` — each taken whole, so **one** `offerToSink( )` pass of two `onData( )` calls drains it.
  (An earlier wording said "two more offers", which is right of the calls and wrong of the pass.)
  Only A1 would have turned this case into a failure, and A1 was not taken.
- `UTF_REQUIRE( sink -> isComplete( ) )` — **unchanged**; the queue drains, so `onComplete( )` fires.
- `UTF_REQUIRE_EQUAL( connection -> consumedTotal( ), 6U )` — **unchanged**, and this is the credit
  rule the case exists for. The drained tail credits nothing: `offerToSink( )`'s credit is gated on
  `! m_isStreamClosed` (`:987`) and `applyClosed( )` sets that flag under the lock before any
  deferred action runs.
- `UTF_REQUIRE( ! taskImpl -> response( ).body( ) )` — unchanged.
- The two in-flight `waitForConsumedTotal( 3U )` / `( 6U )` — unchanged.

So the honest statement is: **one assertion moves, and every credit assertion stays.** "Two lines"
came from §1 of the decisions document naming `"abcdef"` *and* `requireSucceeded` as the fixture's
observed output — which is true of what they *are*, and not of what A2 *changes*.

### 1.6 A hazard the fix carries in, which nothing else in this change-set does

`applyClosed( )` calls `cancelAllTimers( )` (`:1127`) **before** the deferred phase. So the drain
runs with the idle timer and the total timer already dead. Its work is bounded by
`m_pendingDownload` and by how much the sink takes per call. This paragraph first said the h1 body
was *"bounded only by Beast's `body_limit` (N1's 64 MB)"*, so the worst case was *"64 million
`onData( )` calls"*. **S6R.2 §9 removes that bound**: N1 defaults `Http1ResponseLimits::maxBodySize`
to no limit precisely because the codec cannot see whether a sink is installed, and h2 never had
one. So after S6R.2 a streamed body over either protocol is bounded by nothing but the peer and the
total timeout — which `applyClosed( )` has just cancelled — and a sink taking one byte per call
makes one `onData( )` call per byte of whatever arrived, in one uncancellable, undeadlined phase.
(Corrected by the 2026-09-22 review; the correction makes the hazard larger, not smaller.)

This is **not new work** — it is the work the contract already implies, compressed into one phase
with no deadline over it. It is recorded rather than fixed because a bound would have to choose a
number, and any number silently truncates a body that was about to arrive, which is the defect being
closed. It is the same shape as H09's, arriving from the other direction, and §2 says so.

### 1.7 Where H08's clause goes, and why not in the shared rule

`chkPrepareRetry( )` gets its own early refusal, above `chkRequestMayBeReplayed( )` and therefore
above the `rewind( )`. Not a field on `RetryContext`: `chkRequestMayBeReplayed( )` is deliberately
one rule for both halves of the retry, and the pool's half never has a sink — a field one of two
callers must always leave false reads as a bug to the next reader.

**The accessor is safe to read where the session reads it.** `sinkDelivered( )` sits beside
`isRetryable( )` and `outcome( )`, which the session reads off a completed hop from
`continuationTask( )` with no task lock. The last write to `m_sinkDelivered` happens in the deferred
phase, and `applyEvents( )` calls `notifyReady( )` only after that phase and after the locked
section that follows it (`:525`), so the completion edge orders the write before any read.

**Blast radius on the default configuration is nil**, and this is checkable rather than asserted:
the ALPN bounce delivers zero bytes, so `sinkDelivered( ) == 0` and the retry proceeds exactly as
today. The only refused retry is one after a genuine mid-body loss with
`retryIdempotentOnConnectionLoss` on, which is off by default.

### 1.8 Tests

- **H06, red-before-green-after and deterministic.** The pinning case above, with its one assertion
  moved. Red before is the current `"abcdef"`.
- **H06's failure limb**, which nothing today can produce: a sink that takes three bytes per call
  and then **stops taking anything** once the stream has closed. Assert the task fails and that the
  sink was **not** told `onComplete( )`. This case is the one that cannot pass before H07, and that
  is the point of it — it is the pin that proves the dependency was honoured rather than assumed.
- **H08's terminal-callback rule**: a session-level sink over the ALPN fallback, asserting **one**
  `onComplete( )` after the body and not two. Red before: two.
- **H08's retry refusal**: a mid-body connection loss with `retryIdempotentOnConnectionLoss` on,
  asserting the request fails rather than appending to the prefix the sink already has. Red before:
  the body is appended.
- The change-set also owes the two things the decisions document says are owed regardless: the
  `BodySink` half of `body-source-readiness-deferral.md` (L5 nit (e), never done — *verified: that
  file contains no occurrence of the string `BodySink`*), and the amendment to `offerToSink( )`'s
  comment, which states an h2 property as though it held on h1.

---

## 2. H09 — caller code and decoding under the execution-queue lock

**Defect, confirmed by reading both ends.** `ExecutionQueueImpl::onReady( )` takes
`BL_MUTEX_GUARD( m_lock )` and calls `task -> continuationTask( )` inside it (`:480`, `:499`).
`SessionRequestTaskT::continuationTask( )` then takes the wrapper's `m_lock` and runs
`absorbResponse( )` — `storeCookies( )` and `decodeBody( )`, the latter a registered
`ContentDecoder` over up to 64 MB — then `chkPrepareRetry( )` and `chkPrepareNextHop( )`, each of
which may call the caller's `BodySource::rewind( )`, then `startHop( )`. `os::mutex` is not
recursive, and `startHop( )`'s own comment says so where it explains why it assigns
`m_wrappedTask` directly.

**Astra's added point is true and I verified its two limbs separately.** The completed hop has
cancelled its timers (`applyClosed( )` `:1127`, and `failWith( )` `:1407`), and the chain budget is
`chkRemainingBudget( )` — a synchronous `m_deadline - now` evaluated **inside `startHop( )`**, which
runs *after* `absorbResponse( )`. And `m_cancelRequested` is tested after `absorbResponse( )` too.
So a long decode is neither interrupted by a timer nor observed by a cancel until it is over: **both
uncancellable and undeadlined**, and the two are separate facts, not one restated.

**This design takes the documented minimum, and the reason is reachability, not cost.**

`decodeBody( )` returns immediately unless `m_plan.state -> decoders.hasDecoder( )`
(`ClientSession.h:1248-1275`) — and **no decoder ships**. Every `registerDecoder( )` call in the tree
is under `src/utests/`: `TestClientSession.h:1766` and `:1811`, and the registry's own cases in
`TestContentDecoder.h`. (This sentence first said *"the only two registrations in the tree are in
`TestClientSession.h:1710` and `:1755`"*; those lines are the comments above the accept-encoding
cases, not calls, and the count was wrong. The conclusion is not. Corrected by the 2026-09-22
review.) So the CPU-heavy, undeadlined half is **latent**, exactly like H24 and H25, and becomes live
when a codec does.

What is live today is `BodySource::rewind( )`: caller code, under both locks, with a contract that
says nothing about blocking or re-entering. `storeCookies( )` and `startHop( )` are in-library and
bounded.

So:

- **Now, as documentation:** state at `BodySource::rewind( )` in `ClientTypes.h` and at
  `ContentDecoder` that both run under the scheduling lock of the execution queue the request was
  pushed to and under the session wrapper's lock, and must not block, must not submit to that queue,
  and must not re-enter the wrapper. This is what L6 finding 6 asked for and what closes the live
  half — the live half is a **contract gap**, and a contract gap is closed by writing the contract.
- **As a gate, not a deferral:** the structural move — response transformation into the hop task's
  deferred phase, which already exists and already runs off every lock — becomes a **prerequisite of
  the decoder programme**. *Where it is recorded, corrected by the 2026-09-22 review.* This line
  first said *"recorded beside H24 and H25 rather than in a deferral file nobody reads when shipping
  a codec"*. H24 and H25 are recorded nowhere but the verification record's staging list, so
  "beside them" named no place; and the decoder programme's record is
  `http-content-decoders-deferral.md`, which is not a file nobody reads — `ContentDecoder.h`'s file
  comment sends every codec author to it by name. That is where the gate goes: a "prerequisites
  before a decoder ships" section carrying this move, with H24 and H25 entered beside it (S6R.4's
  ledger hygiene). Shipping a codec without it puts unbounded CPU under a queue-wide mutex.

**Why not do the structural move here.** It changes the continuation protocol that
`RetryableWrapperTaskT` shares, which makes it a core-path change gating on the whole suite, for a
hazard whose expensive half no shipped configuration can reach. Doing it now would also collide with
H06/H08, which add state to the same `continuationTask( )` path in the same slice.

**Test.** None, and this says so rather than inventing one: a documentation change has nothing to
show red. The structural move, when it comes, is testable — a deliberately slow decoder with other
completions queued behind it — and that case belongs to the change that makes it pass.

---

## 3. H10 — a queued header block can follow a SETTINGS ACK with stale HPACK or frame limits

The most expensive item in the review. Both halves are answered below, and the shape taken is the
one astra names first, for reasons the alternatives make clear.

### 3.1 The defect, both halves, read end to end

`queueHeaderBlock( )` HPACK-encodes the block and frames it **at queue time**: `m_encoder.encode( )`
commits its dynamic-table transaction, the fragments are sized against `m_peerMaxFrameSize` as it
reads at that instant, and the finished bytes go onto `m_headerBlockQueue`.

`handleSettings( )`, for a peer's non-ACK SETTINGS, calls `applyPeerSettings( )` — which sets
`m_peerMaxFrameSize = value` immediately (`:2529`) and calls
`m_encoder.setDynamicTableCapacity( min( encoderTableSize( profile ), value ) )` (`:2474`) — and then
serializes the ACK into `m_controlQueue` (`:2291`).

`produce( )` emits **the control queue first, then the header block queue** (`:864-878`).

So an already-encoded block is written **after** the ACK of the SETTINGS it predates:

- **HPACK half.** `HpackEncoderT::setDynamicTableCapacity( )` only records a pending value; the
  table moves and the Dynamic Table Size Update is emitted inside the *next* `encode( )`
  (`:184-194`, `emitSizeUpdate( )` at `:352-365`). So the block on the wire after the ACK carries no
  size update. RFC 9113 §4.3.1, fetched: *"An endpoint MUST treat a field block that follows an
  acknowledgment of the reduction to the maximum dynamic table size as a connection error
  (Section 5.4.1) of type COMPRESSION_ERROR if it does not start with a conformant Dynamic Table
  Size Update instruction."*
- **MAX_FRAME_SIZE half.** The block's fragments were sized to the old, larger limit. RFC 9113 §4.2,
  fetched: *"An endpoint MUST send an error code of FRAME_SIZE_ERROR if a frame exceeds the size
  defined in SETTINGS_MAX_FRAME_SIZE…"* Reachable only for a reduction from above 16384 — a smaller
  value is refused outright by `applyPeerSettings( )`, which a case already pins
  (`TestSession.h:3558-3577` on the tip, `SETTINGS_MAX_FRAME_SIZE = 1024` → PROTOCOL_ERROR).

**Only header blocks are exposed, and that asymmetry is the finding.** DATA is framed inside
`produce( )` — `sendableBytes( )` caps by `m_peerMaxFrameSize` at that moment (`:3695-3698`) — and
every control frame is tiny. A header block is the one thing this engine commits to bytes before it
commits to the wire.

### 3.2 The window, and why L3 did not see it

Every `applyCommands( )` in the h2 driver is followed by a `pumpWrites( )` (call sites at `:526/528`,
`:1532/1538`, `:1713/1717`, `:2521/2530`), and `pumpWrites( )` returns early only on
`m_isWriteInFlight || isClosing( ) || ! m_session`. So a block sits in the queue **exactly when a
write is in flight**, and `onRead( )` feeds the peer's SETTINGS on the read path meanwhile
(`:1524`). One write in flight plus one inbound SETTINGS is the whole trigger.

L3 examined `encode( )` in isolation and cleared the encoder mirror, "including the smallest-then-
final rule". That verdict is correct about `encode( )`. What it never had in view is a block that
was *already encoded*. **No existing case puts one there:** every case that feeds a SETTINGS after a
`submitRequest( )` produces in between (checked across `TestSession.h`).

### 3.3 The shapes, and which one the ACK boundary settles

RFC 9113 §6.5.3, fetched: *"the recipient MUST immediately emit a SETTINGS frame with the ACK flag
set. Upon receiving a SETTINGS frame with the ACK flag set, the sender of the altered settings can
rely on the values from the oldest unacknowledged SETTINGS frame having been applied."*

Read the second sentence as the boundary it is: **before our ACK the peer may not rely on its new
values having been applied.** That makes "every block encoded before the SETTINGS leaves before the
ACK" a complete answer to *both* halves, not a trick that fixes one:

- the oversized HEADERS arrives while the old MAX_FRAME_SIZE is still the one the peer may rely on;
- at the moment of the ACK, §4.3.1's MUST is evaluated against our table as it then stands. If some
  block encoded in between carried the size update, the table is already at or below the new maximum
  and the MUST is **not armed**; if none did, `m_sizeUpdatePending` is still true and the next
  `encode( )` emits the update — which is the first field block after the ACK. Conformant either way.

**That argument covers the blocks encoded BEFORE the SETTINGS, and only those — and the review found
that the mechanism §3.5 first specified moved the others too.** A block encoded *after*
`applyPeerSettings( )` has run is sized to the new frame limit and, if the capacity moved, opens
with the size update. §4.3.1, in the sentence above the two this section quotes: *"Any change to the
maximum value set using SETTINGS_HEADER_TABLE_SIZE takes effect when the endpoint acknowledges
settings. The HPACK encoder at that endpoint can set the dynamic table to any size up to the maximum
value set by the decoder."* Until our ACK the change has not taken effect, so an update to a
*larger* size ahead of it is out of contract, and the decoders that matter enforce exactly that:
nghttp2 applies its own SETTINGS only in the ACK branch of
`nghttp2_session_on_settings_received( )` — `nghttp2_session_update_local_settings( )` is what
calls `nghttp2_hd_inflate_change_table_size( )` and assigns `local_settings.max_frame_size` —
and its inflater bounds an incoming size update by the setting so applied (`hd_inflate_read_len( )`
against `settings_hd_table_bufsize_max`, `NGHTTP2_ERR_HEADER_COMP` above it); Netty applies its
sent settings in `onSettingsAckRead( )` — *"a synchronization point between endpoints"*, its
comment says — and `HpackDecoder.setDynamicTableSize( )` throws when the update exceeds the
`maxDynamicTableSize` so applied; QUICHE's
`HpackDecoderState::OnDynamicTableSizeUpdate( )` reports an error whose name is the rule —
`kDynamicTableSizeUpdateIsAboveAcknowledgedSetting`. *(All three read from their sources by the
2026-09-22 review; nghttp2's session file fetched whole, the other two from the files named.)*
So a block that follows the SETTINGS must stay behind the ACK, as it does today, and one that
precedes it must go ahead; the ACK's place is *between* them, and that is what §3.5 now specifies.
Nor is the following block a corner: `onRead( )` runs `applyCommands( )` between `feed( )` and
`pumpWrites( )` (`Http2ConnectionTask.h:1524-1538`), so a submit waiting in the mailbox when a
SETTINGS arrives is encoded under the new values before anything is produced — with no write in
flight at all.

The same three decoders confirm the reading of the reduction above: nghttp2 enters its
expect-an-update state only when the setting is below `ctx.hd_table_bufsize_max`, the table's
current maximum as the peer's own updates left it; Netty sets `maxDynamicTableSizeChangeRequired`
only when the setting is below `encoderMaxDynamicTableSize`; QUICHE's
`require_dynamic_table_size_update_` compares the setting with the current size and limit. A block
that carried the update before the ACK disarms all three. What none of them tolerates is the
update arriving before the ACK when it *raises* the table — the case the first draft put on the wire.

**(A) Promote the queued blocks into `m_controlQueue` ahead of the ACK.** Five lines. **Rejected —
see §3.4.**

**(B) Defer the ACK past the blocks encoded before it.** ~20 lines. **Taken — see §3.5.**

**(C) Drop the stale blocks and re-encode them.** Rejected twice over. Astra's own note applies —
"Independently re-encoding one block can also desynchronize later blocks" — and it contradicts the
invariant `QueuedHeaderBlock` exists for: a block is dropped only for a stream that has been
**doomed**, which is why the struct carries a stream id at all. A drop with the stream still open
leaves the registry told HEADERS were sent and `localEndStreamQueued` set, for a block that never
goes out.

**(D) Structural: queue field lists, encode inside `produce( )`.** Rejected for this slice; see §3.7
for what it additionally buys and why it is worth recording rather than doing.

**(E) Emit the header block queue before the control queue.** Rejected outright: two existing cases
pin control-before-headers with a PING ACK (`TestSession.h:2400-2408` and `:2431-2444`), and
`applyCancel( )`'s held-back cancel is built on it — a RST_STREAM must not overtake the HEADERS of a
stream the peer has never heard of.

### 3.4 The collision, stated exactly — it is not a red test, and that is worse

Shape (A) is the cheap one, and the collision the verification record names is its.

`raiseConnectionError( )` clears `m_headerBlockQueue` (`:1530`) and `forceCloseStream( )` calls
`dropQueuedHeaderBlocks( streamId )` (`:3453`), so a block queued for a stream that a peer's GOAWAY
dooms never reaches the wire. `Session_GoAwayDropsQueuedHeaderBlockTests` pins it, and the reason is
in its comment: the caller has already been told that request is **retryable**, so bytes reaching
the origin afterwards turn a replay into a duplicate. `QueuedHeaderBlock`'s own comment says why the
identifier travels with the bytes — *"a bare buffer carries nothing to recognize it by"*.

`m_controlQueue` is a bare buffer (`:445`). Promoting a block into it **destroys the only handle
that makes the drop possible.** After (A), a block promoted by a SETTINGS and then doomed by a
GOAWAY is written anyway — from the control queue, ahead of whatever we queue after it — for a
request the caller has already been told is retryable. RFC 9113 §6.8 obliges the peer to ignore
it, and `forceCloseStream( )`'s own comment (`:3478-3490`) is exact about why the pin exists
regardless: *"the bytes of a request the caller has already been told is RETRYABLE would reach the
origin anyway, and a peer which acts on them turns the replay into a duplicate"*. (This sentence
first said the block was *"written behind our GOAWAY"* and that the request *"reaches the origin
twice"*; the first is not where (A) would put it, and the second is the peer's choice, not ours.
Corrected by the 2026-09-22 review.)

**And the suite would not catch it.** No case combines a peer SETTINGS with a GOAWAY over a queued
block — the GOAWAY cases feed no SETTINGS after `settle( )`, and `settle( )` produces after the
SETTINGS it feeds, so nothing is pending when the GOAWAY lands. **Both checked by reading the
helper and the cases.** So (A)'s cost is not a red test to fix; it is a silent regression of a
pinned safety property, in a configuration the suite does not construct. That is the same shape as
the false padding comment §4 of the S6R.1 design records — a true-looking invariant surviving
because the test used the other configuration — and it is the reason (A) is rejected rather than
repaired.

Shape (B) touches no such handle. Verified against the same cases: the GOAWAY case feeds no SETTINGS
so nothing is deferred and `wantsWrite( )`/`out.empty( )` are unchanged; the connection-error
sibling feeds a *SETTINGS ACK*, not a SETTINGS, so it queues no acknowledgement of ours. The
positional entry §3.5 now specifies touches none either: an entry of stream 0 is what
`dropQueuedHeaderBlocks( )` cannot match, and neither case's queue holds one.

**Nor can a driver-level case construct it** (checked by the 2026-09-22 review; the first draft
checked `TestSession.h` only). The test peer sends one SETTINGS in its life, its opening one —
`openingDelayInMilliseconds` gates its writes and nothing scripts a later SETTINGS
(`Http2TestServer.h`) — and the client-level cases that script a GOAWAY
(`TestHttp2ConnectionTask.h:1294`, `TestClientSession.h:705`, `TestHttp2TestPeer.h:766`) do not
delay it, while the two that delay it (`TestConnectionPoolConcurrency.h:465`,
`TestHttp2TestPeer.h:691`) send no GOAWAY.

### 3.5 The change taken — the SETTINGS acknowledgement leaves after the blocks that predate it, and before the blocks that follow it

**The mechanism was respecified by the 2026-09-22 review; the decision — defer the acknowledgement,
and only it — is unchanged.** As first written this section put the ACK in *"a second buffer,
`m_deferredControlQueue`, beside `m_controlQueue`"*, which `produce( )` appended *"after the header
block queue and before the DATA loop"*. That defers the ACK behind every block in the queue at
produce time — the ones encoded before the SETTINGS, which is the fix, **and the ones encoded after
it, which is a new defect**: such a block carries the size update the peer may only accept after the
ACK (§3.3), and the first draft's own "what it does not cover" paragraph said an increase was
*"outside this"*, which is true of an already-framed block and false of the one framed next. The
suite would not have seen it: the test peer's decoder is constructed at the larger of what it
advertises and 4096 and only ever *lowers* its ceiling, on the ACK (`advertisedHeaderTableSize( )`,
`Session.h:1220-1240`; `applyAcknowledgedSettings( )`, `:2610-2630`), so it accepts a size update up
to its advertised size at any time — and S6R.2's amended H12 case, which has the peer raise the table
to 16384 and asserts *"a size update in the produced bytes"*, would stay green with that update
written ahead of the ACK. The same silent shape §3.4 rejects (A) for.

**The ACK holds its place in the header block queue.** Two pieces, not five:

1. `handleSettings( )` serializes the ACK into the `frames` of a `QueuedHeaderBlock` whose
   `streamId` is `STREAM_ID_CONNECTION`, and pushes it onto `m_headerBlockQueue` — behind every
   block already there, ahead of every block queued after. `produce( )` writes the queue in order
   and is not touched; `wantsWrite( )` already tests the queue; `dropQueuedHeaderBlocks( )` matches
   a real stream id and so never an entry of stream 0; `raiseConnectionError( )`'s existing clear
   (`:1530`) drops it with the blocks, which is right — an acknowledgement owed on a connection we
   have just ended is not owed — and is the first draft's item 5 for free.
2. `checkControlQueueBound( )` measures `m_controlQueue.size( )` **plus the `frames` of every
   stream-0 entry in the header block queue**, so the existing bound on queued control bytes keeps
   its meaning and a peer cannot make us buffer acknowledgements without limit while a write is
   blocked. (`checkInboundFrameRate( )` already bounds inbound SETTINGS per second; that bounds the
   rate, not the total, so the byte bound is the one that matters.) A walk of a short deque, once
   per inbound SETTINGS, rather than a counter kept in step at three sites.

Plus one sentence on the `QueuedHeaderBlock` comment (`:416-426`), which explains that the
identifier travels with the block so a doomed block can be recognised: an entry of stream 0 is a
SETTINGS acknowledgement holding its place among the blocks, and this section is why.

**Only the SETTINGS ACK moves.** Every other control frame keeps its place ahead of the header
blocks, which is what §3.3(E) and `applyCancel( )` require. The four readers of
`m_headerBlockQueue` — `produce( )`, `wantsWrite( )`, `dropQueuedHeaderBlocks( )` and
`raiseConnectionError( )` — are the four named above; the driver's `onHeaderBlocksProduced( )` is
per produce, not per entry, and is unaffected.

**Why this is within "immediately".** We apply the peer's values at receipt, which is what §6.5.3
requires of the recipient; we delay only the acknowledgement, and only behind frames already
committed to bytes. Every implementation queues its ACK behind whatever its write buffer already
holds; this makes the boundary explicit instead of accidental. The delay is bounded by the in-flight
write, which the connection's own write path already bounds.

**What it does not cover, said plainly.** An *already-framed* block and a SETTINGS increase: the
increase never makes such a block oversized and never arms §4.3.1's MUST against it, so ordering
it ahead of the ACK changes nothing. (This paragraph first said an increase *"of either parameter
— is outside this"* without the qualifier; a block framed *after* the increase is very much inside
it, and the positional entry keeps that block behind the ACK exactly as today.) And the encoder's
own construction-time capacity is **H12's** (S6R.2), not this; the two touch the same object and
must not be written by the same lane at the same time.

### 3.6 What happens to S6R.1's H13 lines

**Nothing. They are not touched.**

H13 (`3f0c89f`, on branch `s6r1`) inserted `firstMaxFragment = maxFragment -
FrameCodec::prioritySize( priority )` into `queueHeaderBlock( )` and changed one `std::min` to use
it. Shape (B) changes `handleSettings( )`, `produce( )`, `wantsWrite( )`,
`checkControlQueueBound( )` and `raiseConnectionError( )` — and not one line of
`queueHeaderBlock( )`. H13's comment block and its two lines survive verbatim, and its case keeps
asserting exactly what it asserts.

This is a property of the shape — and of the shape as the review respecified it in §3.5, which
touches `handleSettings( )`, `checkControlQueueBound( )` and one comment — and it is one of the
reasons for it. Shape (D) would have moved
`queueHeaderBlock( )`'s whole body into `produce( )`, rewriting those lines within days of their
landing and putting a lane in the position of re-deriving H13's arithmetic in a new place. The
verification record's staging note — "H10 will restructure what S6R.1's H13 touched, so sequence
them" — assumed (D). Under (B) the sequencing constraint disappears.

### 3.7 A third hazard the reading found, which this change does not close

`queueHeaderBlock( )` calls `m_encoder.encode( )`, and `encode( )` **commits** its dynamic-table
transaction: the entries are in our table from that moment. If the block is then **dropped** — by
`dropQueuedHeaderBlocks( )` or by `raiseConnectionError( )`'s clear — our encoder holds entries the
peer's decoder never saw. Every later block that uses a dynamic index is then decoded against a
different table.

**It is latent, and the reachability argument is what makes it latent rather than live:**

- `raiseConnectionError( )`'s clear sets `m_isClosed` and nothing further is sent. Harmless.
- `forceCloseStream( )`'s drop is reached from a peer's GOAWAY, and `handleGoAway( )` calls
  `m_registry.markDraining( )` (`:2349`) **before** the doomed streams are closed. `submitRequest( )`
  refuses on `canOpenLocalStream( )` while draining (`:921-927`), so no new local stream — hence no
  new header block — can be opened on that connection.
- The only other door into `queueHeaderBlock( )` is `submitHeaders( )`, the role-neutral one. **No
  client path calls it**: its only callers in the tree are the test server (`Http2TestServer.h`,
  three sites) and one session case. A client sending trailers on a surviving stream would reach it,
  and this client does not send trailers.
- **A fourth door the first draft did not name, checked by the 2026-09-22 review and closed: a
  *local* cancel.** `resetStream( )` (`:1122`) ends in `sendRstStream( )`, which clears the
  stream's `pendingBody` and never its block (`:1595-1626`), and the driver's `applyCancel( )` holds
  a cancel back until `onHeaderBlocksProduced( )` says the block has gone into a write
  (`Http2ConnectionTask.h:689-720`, `:733-760`) — so a cancelled request's block is written, not
  dropped. Counted rather than assumed: `forceCloseStream( )` has exactly two callers,
  `closeStreamsAbove( )` and `closeEveryStream( )` (`:3424`, `:3446`), and
  `m_headerBlockQueue.clear( )` exactly one (`:1530`). And in the server role — the test peer —
  `closeStreamsAbove( )` dooms only locally-initiated streams (`:3410-3413`), so a client's GOAWAY
  drops nothing there and the peer's encoder cannot desynchronise this way either.

So: **unreachable today, by two independent properties, and one shipped feature away from being
live.** It is recorded here and not fixed, because the fix is shape (D) and (D) is not taken — which
is precisely the argument for (D) when the day comes.

### 3.8 Tests — both halves, both deterministic, both red before

The engine is a pure function of `feed( )` and `produce( )` and has no write pump, so the window
that needs a blocked write in the driver needs **nothing at all** here: "queued and not yet
produced" is simply "submitted without an intervening `produce( )`". Both cases go in
`utf_baselib_h2core`, beside the SETTINGS cases; adding to that module is subject to the size rule
in `src/utests/AGENTS.md` and a sibling module is the answer if it is near the target.

- **HPACK half.** `settle( )`; submit one request and produce it, so the encoder's dynamic table is
  non-empty — which is what arms §4.3.1's MUST, and the reason this step is not decoration. Submit a
  second request and **do not** produce. Feed a peer SETTINGS with `SETTINGS_HEADER_TABLE_SIZE = 0`.
  Produce, and assert on `frameTypes( )` that the HEADERS frame precedes the SETTINGS ACK. **Red
  before:** the ACK is first.
- **MAX_FRAME_SIZE half.** `settle( )` with a peer `SETTINGS_MAX_FRAME_SIZE` of 32768; submit a
  request whose header block exceeds 16384 so its first fragment is larger than the default; do not
  produce. Feed `SETTINGS_MAX_FRAME_SIZE = 16384`. Produce, and assert both that the oversized
  HEADERS precedes the ACK and that its length is what the old limit allowed. **Red before:** the
  ACK precedes a frame the peer may then refuse.
- **A control that must stay green**, and it is the one that would catch a careless (E): the
  existing PING-before-HEADERS ordering cases. They are not modified; they are named here so that a
  lane knows they are load-bearing for this change.
- **The control that separates the two mechanisms, and it is owed** (2026-09-22 review):
  `settle( )` on a profile whose `hpackEncoderTableSize` exceeds 4096 (H12's amended case has the
  shape); feed a peer SETTINGS raising `SETTINGS_HEADER_TABLE_SIZE` to 16384; **then** submit, then
  produce. Assert the SETTINGS ACK precedes that HEADERS. Green today, green under the positional
  entry, **red under the second buffer this section first specified** — which is why it is owed: it
  is the pin that stops the next reader from re-deriving that shape. It does not wait for H12: the
  order assertion holds whatever the encoder pends — before H12 the profile's 65536 makes the
  peer's 16384 a reduction it pends, after H12 a raise it pends, and with the default profile it
  pends nothing — so the case is written with this change-set, and only the substance assertion
  proposed below wants H12's raise to be the update it looks for.
- *Proposal (2026-09-22 review):* both HPACK cases should also assert the substance, not only the
  order — that the block written ahead of the ACK does not open with a size update (its first
  octet's top three bits are not `001`) and that the first block written after the ACK does. Order
  is what §6.5.3 settles; the update is what §4.3.1 demands; a case that pins only the first can go
  green on a mechanism that gets the second wrong.
- The change-set owes its `notes.txt` recipes and a manifest refresh, as every case-adding
  change-set here does.

---

## 4. H04a — publishing the driver pointer

**Defect, read end to end.** `ClientConnectionTaskBase::onProtocolNegotiated( )` writes the plain
`om::ObjPtr< ClientConnection > m_connection` on the connection's own thread
(`ClientConnectionTaskBase.h:581`); `connection( )` returns a **reference to that member** (`:671`);
the pool's `attempt.driver` lambda (`ClientSession.h:1730`) does `om::copy( held -> connection( ) )`,
which reads the pointer and takes a reference on the object; `resolveDriver( )` calls it
(`ConnectionPool.h:926-932`) from `refreshEntry( )`, **before any load of a state the writer set
afterwards** — the driver poll is the first thing `refreshEntry( )` does. The member has exactly one
write (grepped: four occurrences, one declaration, one write, one `.get( )` check on the writing
thread, one getter). The pool's mutex does not help: the writer never takes it.

**Recorded three times, and the third is not a refutation.** L5 finding 7; L6; and a TSan round of
32 runs in which the read demonstrably executed — the case logs `ALPN selected ''` twice and could
not have completed had the read returned nothing — and was **not reported**. That record states its
own reason and marks it inferred: the write and the read are in practice ordered through the
execution queue's and the io_context's mutexes, which is real synchronisation TSan honours and which
the code does not document. An incidental edge is not a designed one.

### 4.1 The change — re-gated 2026-09-23, after the gate as agreed crashed the pool's own module

**The gate this section agreed on 2026-09-22 was implemented exactly as written and crashed
`utf_baselib_h2client4` with SIGSEGV** (lane 3, branch `s6r3-3`, 2026-09-23, entering
`H2Pool_FallbackDriverIsPreferredTests`): an unbounded `startConnection( ) → runActions( ) →
startConnection( )` recursion (`ConnectionPool.h:1942` → `:1860` → `:1942` …) until the stack
overflowed. Reverted, the module is 15 cases green. The mechanism was verified link by link against
`lazari2` @ `9583c31` on 2026-09-23 — every function below opened at its signature and read to its
end — and it is not a stub artefact: the stub only holds still a window the real fallback opens for
microseconds. What this section said, and why each part was wrong, is quoted below the gate now
taken, so that the next reader gets the answer before the history.

**The gate taken.** One reading of the task connection's state per examine, and both decisions that
depend on it — whether the driver may be polled, and whether the entry is retired — made from that
one reading:

    ConnectionState taskState = ConnectionState::Connecting;
    bool mayPollDriver = false;

    if( entry -> taskConnection )
    {
        taskState = entry -> taskConnection -> state();               /* the ONE load */
        mayPollDriver = ( ConnectionState::Closed == taskState );
    }
    else if( entry -> attempt.task )
    {
        mayPollDriver = entry -> attempt.task -> getState() >= tasks::Task::PendingCompletion;
    }

    if( ! entry -> driverConnection && mayPollDriver )
    {
        /* the existing resolveDriver( ) block, unchanged ( :1299-1322 ) */
    }

    const auto& connection = entry -> current();

    if( connection )
    {
        const auto state = connection.get() == entry -> taskConnection.get() ?
            taskState : connection -> state();

        /* the existing Ready and Draining-or-Closed arms, unchanged ( :1331-1350 ) */
    }

Three properties, each read at the source:

1. **It is a valid acquire for the `m_connection` write.** `publishState( )`
   (`Http2ConnectionTask.h:2364`) stores `m_connectionState` with the default order and `state( )`
   (`:2822`) loads it the same way. On the fallback the write (`ClientConnectionTaskBase.h:581`,
   inside `continueAfterConnected( )` `:625`, on the strand) is sequenced before the task's
   completion, and `Closed` is stored from that completion: `notifyReadyImpl( )` (`TaskBase.h:604`)
   calls the driver's `onTaskStoppedNothrow( )` (`Http2ConnectionTask.h:2651`), which publishes at
   `:2700`. On the driver's four other `Closed` routes — `onPeerClosed( )` `:1569`, the PING
   deadline `:1974`, the drain deadline `:2226`, `chkFinishClose( )` `:2288` — the task took the h2
   branch of `onProtocolNegotiated( )` (`:2479-2490`), which never writes `m_connection`; a
   pre-handshake stop reaches `:2700` with the member never written. So a load that returns `Closed`
   orders the pool's `attempt.driver( )` read (`ClientSession.h:1790` → `connection( )` `:669`)
   after the only write there is.
2. **It cannot retire the entry on the reading that should have adopted the driver — by
   construction, not by timing.** The retire arm reads `taskState`, the value the poll was decided
   on. If it is `Closed`, the poll ran first in the same call and `current( )` is the driver; if it
   is not, nothing is retired, and the next examine reads `Closed` and adopts. Two loads of the same
   atomic in one examine — which the agreed gate had (`getState( )`, then `state( )`), and which
   today's unconditional poll has too (`m_connection`, then `state( )`) — can straddle the writer's
   `Closed` store if the examining thread is held between them; one load cannot. `Draining` needs
   no arm: `publishState( )` is monotone (`:2364-2385`) and `Ready` is published at `:2534` alone,
   on the h2 branch, so a `Draining` task connection never carries a driver.
3. **It costs nothing against today on the fallback.** The rider's `onClosed( )` is posted from
   `closeAllStreamsUnwrittenRetryable( )` at `:2708`, *after* `:2700`, so the `releaseStream( )` its
   request task makes reads `Closed` and adopts in that examine — the examine that adopts today.
   Without a rider the adopting examine is the next maintenance tick, as today; the write and the
   `Closed` store are microseconds apart, so it is the same tick.

**The `else` arm is the agreed gate, kept for the one attempt shape that needs it — and that shape is
in the tree, not hypothetical.** `taskConnection` is null when the attempt's task is not itself a
`ClientConnection`, and `TestHttpClientRequestTask.h:1314-1341` builds exactly that: an attempt whose
task is a bare `SimpleTaskImpl` (`:1331`) and whose accessor returns a connection the case holds,
used by two cases (`:2408`, `:2501`). The lane's re-gate argument said `taskConnection` "is set on
every path that can produce a fallback driver"; that is true of production — `ClientSession.h:1790`
and `TestConnectionPoolConcurrency.h:360` are the only other `attempt.driver` assignments, both on an
h2 task — and false of the tree, and L5 finding 7's shape alone, which the lane proposed, would
never open for those two cases: their entries would sit until the establishment bound. For that shape
the task-state gate is safe precisely because `current( )` (`:752`) is null until the driver is
adopted, so the retire arm cannot fire; and it is ordered because `PendingCompletion`
(`TaskBase.h:726`) is stored after `onTaskStoppedNothrow( )` returns (`:604`), after anything the
task wrote. It costs those two cases one tick where today the first examine adopts — 10 ms when the
placeholder armed the tick (`:1774-1775`), up to 250 ms if it was already running. A `Created` task
is still excluded, for the reason the next paragraph gives.

**Corrected by the 2026-09-22 review: the gate as first written was `tasks::Task::Running != …
getState( )`, and that admits `Created`.** A `Created` attempt task is not a theoretical state at
this site: `startConnection( )` assigns `entry -> attempt` and calls `examineAll( )` **under the
pool lock** (`ConnectionPool.h:1852-1867`), before `runActions( )` pushes the task (`:1871`) — so
the very examine that creates the entry polls it, and so can any other thread's examine that takes
the lock between the creator's release and its push. For the creator that read is ordered before
the write by its own later push; for the other thread it is not — a read unordered against a write
that has not happened yet, which is the same formal race H04a exists to remove, one state earlier.
"Finished" excludes it and costs nothing: no driver can exist before the task has run.

**What this section said on 2026-09-22, and why it was wrong — quoted, because the deletion is what
hides the lesson.** The gate was

    if( ! entry -> driverConnection && entry -> attempt.task &&
        entry -> attempt.task -> getState() >= tasks::Task::PendingCompletion )

with the argument *"`m_connection` is written before the task can finish, so the load is an acquire
on a store that happens-after the write, and the pointer read is ordered."* True, and not enough. The
section asked one question of the gate — is the store it opens on after the write? — and never the
second: what does the reader do while the gate is shut? While it is shut, `current( )` (`:752`) falls
through to the **task** connection, and the store that opens the gate is not the first store the task
makes on its way out. `onTaskStoppedNothrow( )` publishes `Closed` at `Http2ConnectionTask.h:2700`;
`PendingCompletion` is stored only when it returns (`TaskBase.h:604` → `:726`). Between the two,
`refreshEntry( )` reads `Closed` from the task (`:1325-1329`), its retire arm fires (`:1342-1350`),
and `examineKey( )` forgets the entry (`:1597-1609`) — driver never adopted, waiter served by a new
connection. In the real driver that window is the tail of one locked function and any examine can
land in it; the rider's own `releaseStream( )` is posted from inside it (`:2708`). Its losing outcome
is a wasted TLS connection and a full second establishment, on the default path to every origin that
does not speak h2 — a race, not a certainty, and one nobody would have traced back to this line. The
stub (`TestConnectionPool.h:801`, `:354`) holds the task at `Closed` and `Running` for the case's
life, so under the agreed gate the window never closes, every replacement entry is born into it, and
the pool's own response to an uncharged retire is the recursion —
`pool-uncharged-retire-recursion-record.md`.

*"**Why the task's state and not the connection's.** `Http2ConnectionTaskT::onTaskStoppedNothrow( )`
publishes `ConnectionState::Closed` at `:2648` — also after the write, also atomic — and L5 finding 7
proposed exactly that. It is correct, and it is narrower: it needs `entry -> taskConnection`, which
exists only when the attempt's task is itself a `ClientConnection`."* Reversed on the axis that
mattered. `Closed` is published **before** `PendingCompletion` and is the very store that arms the
retire branch, so a gate on it opens the poll at the instant the retire arm can first fire, and the
poll runs first (`:1297` precedes `:1325`). The section compared the two stores against the write and
never against the retire branch. Where it was right — the connection's state is not available for
every attempt — the answer was an arm per shape, not the later of the two stores for both.

*"**What it costs.** At worst one maintenance tick of adoption latency, over a window of two
returning stack frames on one thread."* The tick is not a unit of 10 ms: the interval doubles on
every tick that changes nothing (`:2050-2058`), from 10 ms to 250 ms (`:210-211`), and an
establishment lasts long enough for it to have climbed, so "one tick" is up to a quarter of a second
— and the sentence names the cost in the *lucky* branch of the window. In the other branch the cost
was the retire above.

*"H04a has no deterministic test and says so"* (§4.3 and §7, as first written). False, and it is the
claim the crash fell out of. `StubFactory::operator( )( )` (`TestConnectionPool.h:783-853`) builds
the fallback as a task that is `Closed` from birth (`:801`) with the driver beside it (`:829`), and
`StubConnectionTaskT` stays `Running` until the case ends (`:354`) — which is exactly "driver
written, `Closed` published, task not finished", held still. A gate on "finished" can never open for
it, and the case that pins the fallback preference (`:1507`) can never pass under it. That was
answerable by reading the fixture the feature is pinned by; neither the author nor the reviewer
opened it, and both accepted "no deterministic test" instead of checking it against the module.

**What it must not disturb.** `refreshEntry( )`'s last branch already tests `attempt.task ->
getState( )` for a *failed* attempt, and L5 finding 7 separately records that this branch tests the
wrong object — it is correct today only because `Http1ConnectionTaskT` reads `Ready` from
construction. That is a different finding, it is not fixed here, and the new gate must be written so
that it does not look like it fixed it.

**H04b is already landed** (`b707fc2`, branch `s6r1`): `effectiveMaxConnectionsPerKey( )` tests
`entry -> isReady` first. This closes the other half named in the same finding.

### 4.2 Does one admission mechanism serve H04a and H03a's residual? — **One site. Two mechanisms.**

The S6R.1 design says the residual's delay "is closed by S6R.3's admission protocol, with H04a".
That promise is answered here rather than repeated, and the answer is narrower than the promise.

**They do meet at one site, which is the genuinely useful half of the answer.** `refreshEntry( )`
resolves the fallback driver (H04a's unsynchronised read) and pushes it onto `actions.schedules`;
`runActions( )` then pushes it onto `m_eqConnections` **outside the pool lock** (`:1756-1759`) —
which is H03a's residual face 2. One adoption path, two findings on it. And the gate H03a's residual
wants **already exists at the sibling site**: `startConnection( )` has
`else if( m_isDisposed ) { actions.cancels.push_back( attempt.task ); }` (`:1835`). `refreshEntry( )`
has no such arm.

**But the mechanisms are not one, and three things say so:**

1. **A disposed gate under the pool lock narrows the window and does not close it.** `runActions( )`
   executes a batch collected earlier; the flag can be set between collection and execution.
2. **The check cannot simply move to the push.** `m_isDisposed` is a `cpp::ScalarTypeIniter< bool >`
   (`:808`), so reading it outside the lock is itself a race; and taking the pool lock around
   `m_eqConnections -> push_back( )` would order pool lock → queue lock, which is the edge rule L4
   exists to forbid and which the queue's own observer callback could close into a cycle.
3. **Closing it properly is a reservation with a drain** — admit under the lock, release the lock,
   push, and have `disposeInternal( )` wait for admissions in flight before it flushes. That is a
   change to the pool's disposal protocol, it gates on the whole suite, and it is not two lines
   beside a read gate.

**A one-line alternative that buys most of what the residual costs, verified rather than assumed.**
The residual matters because of its *duration*: a driver pushed during the flush's wait is not
cancelled by the sweep and then runs to its own idle lifetime — 300 s by default, unbounded if a
caller disables it. `disposeInternal( )` collects `entry -> attempt.task` into the list it
`requestCancel( )`s (`:2074-2080`) and **does not collect `entry -> driverConnection`**. Adding it
means any driver the pool knows about is cancel-requested before the flush; and
`TaskBase::scheduleNothrow( )` throws `operation_aborted` at `:1196` for a task already
cancel-requested, with `scheduleEvenIfAlreadyCanceled( )` returning true only for timer tasks
(grepped: one override in the whole tree). So such a driver completes **at once** instead of idling.
The pattern is already in the tree, which is the strongest thing that can be said for a one-liner:
`chkCancelEntry( )` (`:1112-1135`) cancels a `driverConnection` task from `refreshEntry( )`'s
establishment-timeout arm, possibly in the same batch that schedules it; no driver overrides
`requestCancel( )`; and `requestCancelInternal( )` on a `Created` task marks the flag and returns
(`TaskBase.h:1026-1042`). *(Verified by the 2026-09-22 review, link by link.)*

That is a real reduction of the residual and it is **not** H04a. It belongs with whatever change-set
owns the pool's disposal, and it is recorded here so it is not lost between two slices.

**One obligation that IS H04a's, because the sentence names it** (2026-09-22 review): the comment
above the flush at `ConnectionPool.h:2146-2152` ends *"That window is pre-existing ( today's
reset( ) reaches the same wait ) and is closed by the admission protocol of S6R.3, with H04a"*.
After this section that sentence is false in the tree, and H04a's change-set is the one editing
`ConnectionPool.h`; it owes the amendment — the window is *narrowed in duration* by the one-line
cancel above when that lands, and closed only by a reservation with a drain, which nothing has
taken. `s6r1-design.md` §2 carried the same sentence and is corrected in place (2026-09-22).

**So the answer to the question as asked: no, one mechanism does not serve both.** H04a needs an
acquire on the writer's publishing store. H03a's residual needs an admission gate with a drain. They
share a site, a file and a shape of argument, and nothing else. Saying otherwise would be the third
time in this project that a right conclusion was published on a wrong premise.

### 4.3 Test

**Corrected 2026-09-23.** This section said *"None that is deterministic, and that is stated rather
than papered over. The change is justified by construction"*, and §7 repeated it. Wrong, and the
crash is what the wrong claim cost: the module that pins the fallback preference is a deterministic
test of any gate on the driver poll, because its stub holds the fallback's three stores still —
driver written, `Closed` published, task never finished (§4.1). What the change-set ships:

- **The existing `H2Pool_FallbackDriverIsPreferredTests`** (`TestConnectionPool.h:1507`),
  unmodified and named here as load-bearing: red under the agreed gate — a crash, or with the bound
  of `pool-uncharged-retire-recursion-record.md` in place a failed answer — and green under §4.1's.
- **A pin of H04a's own purpose, red before.** A stub task that is `Connecting` and `Running` with
  the accessor already returning a `Ready` driver (one knob on `StubFactory`: the fallback task's
  initial state), and an **unreplayable** request, so that nothing rides the preface. Assert that no
  answer arrives while the task stays `Connecting` — a bounded negative wait, with its positive half
  in the same case, as `H2Pool_ConnectingTakesOnlyReplayableTests` does it — then
  `setState( Closed )` on the task and assert the waiter is answered with the driver. Red before:
  today's unconditional poll hands the driver out at the first examine. It is the one deterministic
  statement of "the pointer is not read before the acquire" available without TSan.
- **The `else` arm.** An attempt whose task is a plain `SimpleTaskImpl` — the shape of
  `TestHttpClientRequestTask.h:1314` — with an accessor returning a `Ready` stub; assert the driver
  is adopted once the task has completed. Green before and after; it pins that the arm exists.
- Where they go is subject to the size rule in `src/utests/AGENTS.md`: `utf_baselib_h2client4`, or
  a numbered sibling if it is near the target.

The TSan paragraph below stands: it corroborates the race half and does not prove it.

A TSan run over `utf_baselib_httpclient4` is worth doing and is **not** the proof — the same 32-run
round already failed to report the unfixed read, for a reason that record gives. A clean run
corroborates; the construction argument carries it. The recipe is in
`http2-driver-timer-cancel-cross-thread-race-record.md` §5, and the mandatory positive control
(`utf_baselib_basictask`, the known report at `TestBaselibBasicTask.h:127`, exit 66) is in that
record's §1 and closing tables, not in §5.

---

## 5. H11 — closed by the maintainer, and moved

**Decided, not dropped.** No code: the behaviour stays, the justification is rewritten to cite
RFC 9113 §4.3.1 rather than RFC 7541 §4.2, to say plainly that the leniency is chosen, and to state
the arming condition; plus a short record naming the reopen trigger — the first profile advertising
`SETTINGS_HEADER_TABLE_SIZE` below 4096, or below the live table size. It is a comment rewrite and
belongs with S6R.4's documentation work. The argument is in `s6r3-decisions.md` §4 and is not
repeated here.

**It landed there, and nothing is owed** (confirmed against the source, 2026-09-23). Commit
`b1224b3`, "http2: the decoder's HPACK leniency is a choice, not a consequence", inside the S6R.4
merge `38ed037`; the rewrite is the doc comment above `HpackDecoderT::setMaxDynamicTableSize( )` in
`HpackDecoder.h`. It carries all four things this section asks for — RFC 9113 §4.3.1 in place of
RFC 7541 §4.2, the decoder-side MUST quoted, the leniency stated as a chosen conformance departure
rather than a consequence of not evicting, and the arming condition written as the table's CURRENT
size rather than the previous maximum. One deviation from this section, and it is an improvement:
the reopen trigger went into that same comment, under "WHEN TO REOPEN IT", instead of into a
separate `issues/` record — so the trigger sits where the next reader of the behaviour will be, not
in a file they would have to know to open. The behaviour is unchanged, as decided.

*(One more thing this section's §0 flagged is no longer true: `s6r3-decisions.md` IS on `lazari2`,
at `c4b2f87`, so every citation of it below resolves.)*

---

## 6. These are four change-sets, not one

The S6R.1 design could argue nine fixes into one change-set because they touched disjoint functions
and none was a core-path rewrite. That argument does not transfer.

- **H06 + H08** — one change-set, one theme ("what the sink has seen"), core path in
  `HttpClientRequestTask.h` and `ClientSession.h`. **Gates on the whole suite. Blocked on H07.**
- **H10** — its own change-set, core path in `Session.h`. Independent of the other three. **Must not
  run concurrently with S6R.2's H12**, which writes the same encoder's construction.
- **H04a** — its own change-set, core path in `ConnectionPool.h`: §4.1's two-arm gate, the one-load
  refresh, and the cases of §4.3. *Amended 2026-09-23:* this said "two lines and a comment", of the
  gate that crashed. It could ride with the pool-disposal work of §4.2 if that is taken; it must not
  ride with H06/H08, whose theme is different. **It lands after the retry-accounting bound of
  `pool-uncharged-retire-recursion-record.md`**, its own change-set, which is what makes a wrong gate
  on the driver poll a red case rather than a stack overflow.
- **H09** — documentation only. It may ride with S6R.4.

**One interaction across them, and it is not a conflict.** H06's drain adds uncancellable,
undeadlined work to the request task's deferred phase; H09's structural move, when it comes, adds
more of the same kind to a *different* phase. Neither blocks the other; whoever writes the H09 fix
should read §1.6 first, because the two hazards are the same hazard.

---

## 7. Acceptance

- Focused modules, clang debug, in the lane; **one module at a time**.
- Then clang **and** gcc release plus the whole-suite G1 gate, by the orchestrator. Every one of
  these is a core-path change, so the whole suite is the gate, not a convenience.
- Every fix that claims a test above must ship that test, shown **red before and green after**.
  Three of them can be: H06's drain, H06's failure limb (after H07), H08's two cases, and both of
  H10's — plus H10's third case, which is a control, green on both sides by design, and owed for the
  reason §3.8 gives. Nothing here may rest on "the suite still passes".
- **H04a ships the cases §4.3 names** — the existing fallback-preference case as a named control,
  the Connecting-then-Closed pin (red before), and the `else`-arm pin — and `utf_baselib_httpclient`
  is in its focused set, because the `else` arm exists for two of that module's cases. *Corrected
  2026-09-23:* this line said *"H04a has no deterministic test and says so. It is justified by
  construction"*; the module that pins the poll was that test, and the agreed gate crashed it. The
  TSan run still corroborates the race half and does not prove it.
- **H09 ships no test**, because a documentation change has none to ship.
- H06/H08 and H10 each owe `notes.txt` recipes and a manifest refresh.

---

## 8. Where this contradicts or extends the two records it was built on

Recorded in the same spirit as the verification record's own §2 and the decisions document's §6.

- **The pinning test moves one assertion, not two** (§1.5). `requireSucceeded( )` is unchanged under
  A2; it would have changed under A1. Both the brief and the decisions document say two.
- **The verification record's "its cheap fix collides with an existing GOAWAY test" is true of one
  cheap shape and not of the other** (§3.4). Promotion into the control queue collides — and worse
  than the wording suggests, because the collision is a silent regression the suite cannot construct,
  not a red case. Deferring the acknowledgement does not collide, and that was checked against the
  cases and the `settle( )` helper rather than assumed.
- **"H10 will restructure what S6R.1's H13 touched, so sequence them"** (implementation plan
  `:1592`, verification record §6) assumed the structural shape. Under the shape taken, H10 does not
  touch `queueHeaderBlock( )` at all (§3.6).
- **The S6R.1 design's "the delay is closed by S6R.3's admission protocol, with H04a" is not
  delivered as written** (§4.2). One site serves both; two mechanisms are needed; H04a is the smaller
  one and does not shorten the residual. A one-line reduction of the residual's *duration* is
  identified and verified instead, and it is not part of H04a.
- **H09's price depends on a reachability nobody stated**: the decode half is latent because no
  decoder ships (two test registrations, no production one), which is why the documented minimum is
  the right answer now and a gate on the decoder programme is the right answer later. The record
  rates H09 "structural" without that distinction.
- **A third HPACK hazard exists and is in no record** (§3.7): dropping an already-encoded block
  leaves the encoder's dynamic table holding entries the peer never saw. Latent behind two
  independent properties, and closed for free only by the structural shape.
- **One premise of this design's own author was refuted by reading** (§1.4), and the refutation is
  recorded rather than deleted.
- **§3.5's first mechanism was wrong and the review respecified it** (2026-09-22): a second buffer
  appended after the whole header block queue also moved the blocks encoded *after* the SETTINGS
  ahead of the ACK, which puts a size-update increase on the wire before the change has taken
  effect — a COMPRESSION_ERROR against nghttp2, Netty and QUICHE — and which the suite's own peer
  cannot see. The ACK now holds its place inside the header block queue. The decision stands.
- **§4.1's gate as first written admitted `Created`** (2026-09-22), a state `startConnection( )`
  exposes under the pool lock; corrected to "finished".
- **§4.1's gate as corrected on 2026-09-22 was itself wrong, and crashed** (2026-09-23). "Finished"
  is stored *after* the `Closed` that retires the entry, so while the gate was shut the retire arm
  read the task's `Closed` with the driver unadopted; the stub, `Closed` and `Running` for the case's
  life, made that window permanent, and the pool re-established without charge until the stack
  overflowed. Re-gated on one reading of the task connection's state, with the agreed gate kept only
  for an attempt whose task is not a `ClientConnection` (§4.1). The rejected alternative had the
  ordering right and the availability wrong; the correction takes each where it holds. The unbounded
  recursion is a pool hazard in its own right, live today in its asynchronous form —
  `pool-uncharged-retire-recursion-record.md`.
- **§1.3's gate as first written told the sink `onComplete( )` in the `[ Expired, Closed ]` batch**
  (2026-09-22), the very row the decisions document's table promised to close; corrected to "the
  close is the answer".

---

## 9. What this design does not establish

Nothing here was built or run. Every claim is from reading, except the three RFC 9113 sentences,
which were fetched. In particular: H10's trigger is derived from the driver's write-in-flight guard
and the engine's queue, not observed against a peer; H04a's race is certain from the source and its
consequence is probabilistic; H06's ALPN-bounce path is the decisions document's trace, re-read here
at `outcomeOnClosed( )` and `applyClosed( )` but not exercised. No case in the tree installs a sink
at the session level, so **nothing in the suite would have caught H08** — which is the argument for
the cases §1.8 owes.

---

## 10. Deliberately out of scope

Half (b) of H06 (sink readiness), `onComplete( outcome )`, and B4's reset-capable sink — all
`ClientTypes.h` changes, all deferrals. H09's structural move, recorded as a gate on the decoder
programme. H10's shape (D), recorded in §3.7 with what it alone buys. The pool's
reserve-and-drain admission protocol and the one-line cancel of a known driver at disposal, §4.2.
H11, §5. H12 (S6R.2) writes the same encoder H10 reads and is sequenced against it, not merged with
it.

---

## 11. Design review, 2026-09-22

**Reviewer: Claude Fable 5.1, on `lazari2` @ `1b077d6`, read against the source at that tip.**
Every function §0 lists was opened at its signature and read to its end: the engine's queueing,
settings, GOAWAY, drop, reset and bound paths; the encoder whole; the request task's apply,
deferred, close, fail and stop paths; the session's continuation chain; the pool's refresh, start,
cancel and dispose paths; `TaskBase`'s schedule, cancel and notify paths; the queue's `onReady( )`;
the driver's cancel, produce and write pump; the base's `m_connection`. The four cited cases, the
`settle( )` helper, the stub sink, the test peer's configuration surface, S6R.2's H07, H12 and N1
sections and the decisions document (from its branch) were read. RFC 9113 §4.3.1 and §6.5.3 were
fetched again, and the three decoders §3.3 names were read from their sources — nghttp2's session
file whole, Netty's and QUICHE's decoders from the files named there. Nothing was built or run; a
lane was compiling.

**Verdict: not agreed as first written; agreed with the corrections written in place above, each
dated 2026-09-22.** Every *decision* stands — A2 and B2, the documented minimum with a gate, the
deferred acknowledgement, the finished-task gate, one site and two mechanisms, H11 as the
maintainer closed it. Three *specifications* did not, and one of them is the item the brief called
the most expensive in the review:

1. **H10's mechanism reordered blocks it should not have** (§3.3, §3.5). The §6.5.3 argument is
   right for every block encoded before the SETTINGS — the frame-size half because the peer enforces
   its new limit from its ACK, the HPACK half because the peer measures the reduction against the
   table as it stands when the ACK arrives, and all three decoders read do exactly that. But a
   second buffer appended after the whole queue also put the blocks encoded *after* the SETTINGS
   ahead of the ACK, and those carry the size update §4.3.1 says takes effect only when we
   acknowledge: a COMPRESSION_ERROR against nghttp2, Netty and QUICHE on the raise H12 is being built
   to follow, reachable with no write in flight, and invisible to the suite because the test peer's
   decoder never lowers its ceiling before the ACK. The correction keeps the decision and moves the
   ACK into the header block queue at its own position — two pieces instead of five, one comment,
   one more case. §3.6 still holds.
2. **H04a's gate admitted `Created`** (§4.1), a state `startConnection( )` exposes to
   `refreshEntry( )` under the pool lock before the push. Corrected to "finished". *2026-09-23: that
   correction stood on the same blind spot as the gate it corrected, and the corrected gate crashed
   the pool's own module — §12. "The finished-task gate" in the verdict above does not stand.*
3. **H06's gate told the sink `onComplete( )` in the `[ Expired, Closed ]` batch** (§1.3), the row
   the decisions document's table promised to close. Corrected to "the close is the answer".

**Verified and standing as written, against the source:**

- **The promotion rejection (§3.4).** `m_controlQueue` is a bare `wire_buffer_t` (`:445`),
  `dropQueuedHeaderBlocks( )` matches by stream id (`:3460-3474`), and a promoted block is
  unrecognisable. And no case constructs the configuration: `Session_GoAwayDropsQueuedHeaderBlockTests`
  feeds no SETTINGS after `settle( )`, `settle( )` produces after the SETTINGS it feeds
  (`TestSession.h:609-625`), the sibling before it feeds an ACK, the test peer sends one SETTINGS in
  its life, and no client-level case both delays it and scripts a GOAWAY. So (A)'s cost is a silent
  regression, as §3.4 says — and so would the first §3.5 have been, which is why the correction was
  made in place rather than sent back.
- **The third HPACK hazard (§3.7)** is latent behind the two properties named, and behind a third
  the design did not name — a local cancel never drops a block — with `forceCloseStream( )`'s two
  callers and the queue's one clear counted, and the server role checked.
- **H09's reachability**: `decodeBody( )`'s three early returns, no `registerDecoder( )` outside
  `src/utests/`, `rewind( )` at `:1201` and `:1389` under both locks, the budget read inside
  `startHop( )` after `absorbResponse( )`, and `m_cancelRequested` tested after it too. The
  documented minimum with a gate is the right call; the gate now has a home.
- **A2's failure limb needs no plumbing** under H07 as S6R.2 §2 designs it: a pending success
  carries no `m_completionException`, so neither of H07's guards protects it and `:501` fails it.
  `releaseConnectionSlot( )` is queued behind the drain with `Completed` captured (`:1586`), which is
  what S6R.2's corrected H07 test asserts, so the two designs agree on the slot; and a truncation
  failure is not retried, because the hop's outcome stays `Completed` and its `isRetryable( )`
  false, before H08's clause is even reached.
- **One site, two mechanisms (§4.2)**, and the one-line cancel's chain link by link, with the
  pattern found already in `chkCancelEntry( )`. S6R.1's §2 is corrected in place; the in-tree
  comment is an obligation on H04a's change-set.
- §1.4, §1.5 (with one precision), §1.7, §1.8's grep claims (the readiness deferral has no
  `BodySink` in it), §3.1, §3.2, §3.6, and L5 finding 7's wrong-object branch as §4.1 describes it.

**Precisions applied in place, none changing a decision:** `offerToSink( )` returns nothing (§1.1);
one pass, two calls (§1.5); N1 removes the h1 bound §1.6 leaned on (§1.6); the decoder-registration
citation (§2); three anchors that moved (§0, §3.1); the promotion wording (§3.4).

**Proposals, marked where they sit:** substance assertions on H10's HPACK cases (§3.8); the
implementation plan's S6R.3 row, which still said H10 restructures H13's lines, amended alongside
this review (`http2-implementation-plan.md:1592`).

**Findings for the maintainer that are not this design's to fix:** `s6r3-decisions.md` is not on
`lazari2` (§0); `ConnectionPool.h:2146-2152` promises what §4.2 withdraws, and a lane must amend it
(§4.2).

**What this review does not claim.** Nothing was compiled. Whether a stream-0 entry disturbs any
reader of `m_headerBlockQueue` the reviewer did not find is the lane's to confirm — the four readers
named in §3.5 are the four found. The `[ Expired, Closed ]` batch of §1.3 is derived from the
mailbox's shape, not observed. And the three decoders were read for the two rules §3.3 cites and
nothing else.

**Agreement.** With the in-place corrections above taken as part of the design, the reviewer agrees
that S6R.3's four change-sets may be implemented against it, in the order and with the gates §6 and
§7 give — H06/H08 after H07 lands, H10 never beside H12.

---

## 12. Re-gate of §4.1, 2026-09-23 — after the agreed gate crashed

**Reviewer: Claude Fable 5.1 — the same reader who agreed §4.1 on 2026-09-22 and made its `Created`
correction — on `lazari2` @ `9583c31`, at the maintainer's request after lane 3 reported the crash.**
Read whole, from signature to end: `refreshEntry( )`, `findDispatchable( )`, `canStartConnection( )`,
`examineKey( )`, `examineAll( )`, `runActions( )`, `startConnection( )`, `forgetConnection( )`,
`chkCancelEntry( )`, `onMaintenance( )` and `Entry` in `ConnectionPool.h`; `onProtocolNegotiated( )`,
`continueAfterConnected( )`, `connection( )` and the `m_connection` declaration in
`ClientConnectionTaskBase.h`; the driver's `onProtocolNegotiated( )`, `onTaskStoppedNothrow( )`,
`publishState( )`, `state( )` and all five `Closed` publications; `TaskBase::notifyReadyImpl( )`,
`getState( )` and `exception( )`; the four `ConnectionAttempt` builders in the tree and
`StubConnectionTaskT` whole; lane 3's journal entry for the crash, outside the repo. Nothing was
built — two lanes were compiling — and nothing needed to be for the diagnosis: the lane's evidence is
a backtrace and a green re-run, and every link of the mechanism is a synchronous line of code.

**Verdict on the lane's diagnosis: right in every link, overstated in one magnitude, and wrong in
one claim that would have sent the fix into a second failure.**

- Every link holds — `current( )`'s fall-through, the `Closed`-before-`PendingCompletion` order, the
  retire arm, the forget, the uncharged re-establishment, the recursion. §4.1 carries them with line
  numbers.
- *"On the default path for every origin that does not speak h2"* names the path, not the frequency.
  In the real driver the shut-gate window is microseconds under the task lock; the tick lands in it
  rarely, and the rider's release, though posted from inside it, usually arrives after it has closed.
  The losing outcome is real and bad — a wasted TLS connection and a second establishment — and it is
  a race. The stub makes it certain, which is why the crash was deterministic.
- *"`taskConnection` is set on every path that can produce a fallback driver"* is true of production
  and false of the tree: `TestHttpClientRequestTask.h:1314` is a driver-bearing attempt on a
  `SimpleTaskImpl`, used by two cases. L5 finding 7's shape alone, which the lane proposed as the
  fix, would leave those entries unadopted until the establishment bound. The gate taken has an arm
  for each shape (§4.1).
- Both understatements verified: the tick is up to 250 ms; the recursion is unbounded whenever an
  entry retires without ever having been usable and without the retire being charged, which is
  `refreshEntry( )`'s `Draining`/`Closed` arm exactly, and it is latent as a crash only because no
  real driver is born `Closed` — while its asynchronous form, a reconnect loop against an origin that
  GOAWAYs at birth, is live. It has its own record, with a bound and a test, and the bound is
  sequenced before this gate on purpose.

**A third shape, and it is the one taken.** Neither the agreed gate nor L5 finding 7's is the answer:
the first opens on the later store; the second on the right store but for one attempt shape only;
and both read the task connection's state a second time for the retire decision, which is a straddle
(§4.1, property 2 — today's unconditional poll has the same straddle, at nanosecond width). One load,
both decisions from it, and the task-state gate only where there is no task connection to read.

**What would have caught this at the gate — the gate is what failed, not the lane.**

1. **Read the gate against the fixture that pins the feature.** `StubFactory` builds the fallback as
   `Closed` + `Running` + driver (`TestConnectionPool.h:801`, `:829`, `:354`). One read of it answers
   "does this gate ever open for the case that pins the poll?" — no. The design said "no
   deterministic test" and the review accepted the sentence instead of opening the module. A change
   to a poll owes a reading of every fixture that drives the poll, and "there is no test" is a claim
   about the tree, to be checked against the tree.
2. **Ask the second question of every gate.** An acquire argument answers "is the load after the
   write?". A gate also has a shut state, and the reader does something in it. Here it read the same
   object's *other* state and acted on it. "What does the reader do while the gate is shut, and which
   of the writer's stores does it see first?" was never asked — not by the author, and not by the
   reviewer, whose `Created` correction moved the gate *later* by the same method and so widened the
   shut interval without looking at it.
3. **A correction that narrows a gate is a change that needs the same review as the gate.** The
   2026-09-22 correction was made in place and agreed in the same pass; it was treated as a
   precision. It was a specification change, and it was wrong in the direction it moved.
4. **A rejected alternative that differs in ordering must be compared on ordering.** §4.1 rejected
   L5 finding 7 on availability and generality and wrote "also after the write" of both stores — true
   and beside the point. The two stores differ in their order relative to the retire branch, and one
   sentence comparing them there would have been the whole finding.
5. **Compile the fifteen cases.** The design and the review both record "nothing built; a lane was
   compiling". The module the gate crashed builds in minutes and would have crashed at the design
   stage. Keeping builds off a contended machine is right for a design pass; for a two-line change to
   a polled core path whose pinning module is small, one focused build costs less than this section.

**What this re-gate does not claim.** The corrected gate has not been compiled or run. Property 1's
acquire argument and property 2's one-load argument are from the source; the cases in §4.3 are
specified, not written. The frequency of the real-driver race was reasoned from the window's width
and the paths that can examine during it, not measured.

**Agreement.** §4.1 as re-gated, sequenced after the recursion bound's change-set, may be implemented
— with the cases of §4.3 shown red before and green after where that section says they can be, and
with `utf_baselib_httpclient` in the focused set, because its two pool-composed cases are the ones
the `else` arm exists for.
