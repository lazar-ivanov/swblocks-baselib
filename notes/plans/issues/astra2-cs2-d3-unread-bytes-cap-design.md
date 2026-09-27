# D3 — a cap on the HTTP/1.1 body bytes a sink has not taken — design (CS-2)

**Date:** 2026-09-27. **Status: first draft, NOT agreed. Not coded.** Written by the CS-2 lane for the
review the workflow requires before D3 is implemented
([`../parallel-implementation-workflow.md`](../parallel-implementation-workflow.md) §4.1). Nothing
was built or run for it.

**What is decided, and is not reopened here** — [`astra-second-review-decisions.md`](astra-second-review-decisions.md)
§3, D3: *fail, in the request task, with a default of 64 MiB. A new cap beside `maxResponseBodySize`
in `HttpClientRequestConfig`, on body bytes received and not yet taken: counted as each data event is
posted, and released as the sink takes bytes or the buffered path appends them. Past it the stream is
cancelled and the request fails with `BufferTooSmallException`, as the buffered cap already does, and
nothing further is queued for that request.* This note settles the mechanism, and names the one point
where it goes beyond the decision's letter (§6).

**Anchors.** `file:line` at `7d21df3`, which is this lane's branch point, together with the function
name, because D4 (implemented first in this lane) moves lines in the same file. Unqualified line
numbers are `src/include/baselib/httpclient/HttpClientRequestTask.h`. **VERIFIED** means read at the
cited source; **INFERRED** means it follows from verified facts and was not observed.

---

## 1. The knob

- **`maxOutstandingResponseBodySize`**, a `cpp::ScalarTypeIniter< std::size_t >` beside
  `maxResponseBodySize` (`:84`), defaulted in the constructor from a new
  `DEFAULT_MAX_OUTSTANDING_RESPONSE_BODY_SIZE = 64U * 1024U * 1024U` beside `:88`. *Outstanding* is
  astra R02's word and the decision's own test wording. The name is the review's to change.
- **What it bounds:** the body bytes one request task holds and has not handed on — posted to its
  mailbox and not yet applied, plus applied to `m_pendingDownload` (`:317`) and not yet taken.
- **It reaches the session with no new plumbing.** `ClientSessionConfig::requestConfig`
  (`ClientSession.h:72`) is copied into every hop by `startHop( )` (`ClientSession.h:1130-1136`).
  VERIFIED.
- **No "off" value**, as `maxResponseBodySize` has none; a caller who wants none sets `SIZE_MAX`. The
  comparison is written `bytes > cap - count`, which cannot overflow because `count <= cap` holds
  by construction (§3).
- **Both paths.** On the sink path it bounds the backlog. On the buffered path the apply phase
  appends a block as soon as it applies it (`applyData( )`, `:943-948`), so the cap bounds only the
  mailbox there; `maxResponseBodySize` bounds the total and fires first while the drain keeps up.
  On that path the new cap is the backstop for a drain which has fallen behind — the drain runs on
  `ThreadPoolId::GeneralPurpose` (`post( )`, `:404-406`), not on the thread which reads.

## 2. Where the count rises and falls, and under which lock

| | Where | Thread | Held |
|---|---|---|---|
| **Rises** | `post( )`, for a `Data` event, in the critical section which already does the `push_back` (`:381-383`) | the driver's strand: `onData( )` (`:1898-1911`) is called from `deliverBodyChunk( )` (`Http1ConnectionTask.h:1067-1089`) and `onDataEvent( )` (`Http2ConnectionTask.h:1400-1420`) | `m_mailboxLock` |
| **Falls: the sink took bytes** | `offerToSink( )` (`:969-1012`), per block, once `onData( )` has returned, by what it returned (clamped to what was offered, `:979-982`) | the drain, deferred phase | nothing held; takes `m_mailboxLock` for the decrement |
| **Falls: the buffered path appended** | `applyData( )`, after the append (`:943-946`) | the drain, apply phase | the task lock (`applyEvents( )`, `:464`); takes `m_mailboxLock` |
| **Falls: the buffered cap refused the block** | `applyData( )`'s cap branch (`:915-941`), whose block is dropped | the drain, apply phase | the same |

- **`post( )` is where the count rises because it is where the bytes enter.** Counting in
  `applyData( )` instead would miss the mailbox, which is unbounded over HTTP/1.1 while the drain is
  behind — the decision's *"one that covers the mailbox too"*. VERIFIED: `onData( )` is a post and
  nothing else.
- **No new lock order.** The mailbox lock is a leaf (`:294-297`) and stays one: the decrement calls
  nothing while holding it. Taking it under the task lock is an edge the task already has —
  `TaskBase::scheduleNothrow( )` holds the task lock (`TaskBase.h:1167`) across `scheduleTask( )`
  (`TaskBase.h:1208`), which takes `m_mailboxLock` (`:1842`) and then posts (`:1847`). VERIFIED.
- **The count and a latch are mailbox state**, declared with `m_mailbox` (`:299-301`) and never
  touched without `m_mailboxLock`. A lock and not an atomic, because the check, the latch and the
  `push_back` must be one step, so that "counted" and "queued" can never disagree.
- **The decrement is per block and after the call**, never across it: a sink is the caller's code
  and must not run under any lock of ours (the class comment, `:141-149`). That is also exactly
  where D4 records a block's accepted bytes, so the two land in the same place. A block the sink
  threw on was not taken and is not released: it is still held.

## 3. From an overflow in `post( )` to a cancelled stream and a failed request

**`post( )` can decide and cannot act.** It is `NOEXCEPT` (`:374-409`), runs on the driver's strand,
and holds the mailbox lock. It may not cancel the stream or fail the request itself: the sink
methods are *"a post and nothing else"* (`:1859-1862`, rule L2), nothing may be called under the
mailbox lock (`:294-297`), and the completion state is the drain's, under the task lock
(`:303-306`). VERIFIED.

**So `post( )` classifies a `Data` event, in its existing critical section:**

1. no block — queued as today, counting nothing;
2. the latch already set — **dropped**: not queued, not counted, no drain scheduled;
3. `bytes > cap - count` — the latch is set and the event becomes a **marker**: its kind becomes a
   new `EventKind::Overflow`, its block is released, and it is queued and a drain scheduled exactly
   as every post is;
4. otherwise `count += bytes`, queued as today.

Nothing here can throw that cannot already throw today: the `push_back` (a `bad_alloc` reaching
`BL_NOEXCEPT_END`, unchanged).

**The drain applies the marker in order** — a new case in `applyEvent( )`'s switch (`:563-605`) —
as `applyOverflow( )`:

- **if `m_isCompleted || m_isCompletionPending`, it returns.** The earlier verdict stands, and every
  path which set one has already reset the stream or never had one: `applyStopped( )` (`:1429`),
  the buffered cap (`:923`), a failed deferred action (`:508`), a close (`answerOnClosed( )`, after
  the stream ended), and `applyAcquired( )`'s refusals, which open no stream. VERIFIED per path.
  `applyStopped( )` opens the same way (`:1422-1425`);
- **otherwise `cancelStream( deferred )`** (`:1675-1699`) **and `failWith( )`** with a
  `BufferTooSmallException` — the buffered cap's type (`:925-938`), as decided — reading *"The HTTP
  response body bytes received and not yet taken exceeded the maximum of N bytes"*.

`applyEvents( )` then does what it does for every failure: the deferred cancel runs off the lock
(`:485`), the completion is taken under it (`:513-520`), and `notifyReady( )` is called with it
released (`:532-535`). VERIFIED.

**What the pool is told** follows from the close the cancel produces:

- **HTTP/1.1** has no stream reset. `cancel( )` posts `onCancelStream( )`
  (`Http1ConnectionTask.h:2408-2440`), which calls `finishStream( code, false, false )`
  (`:2516-2539`); an unusable finish publishes `Draining` under the state lock before `onClosed( )`
  (`publishStreamEnd( )`, `Http1ConnectionTask.h:1753-1774`), so `outcomeOnClosed( )` reads
  `ConnectionUnusable` (`:1195-1196`) and the pool retires the connection — as a cancel and the
  buffered cap already do. VERIFIED. A cancel arriving after the message completed on the wire is
  ignored by the handle check (`Http1ConnectionTask.h:2418`), and the clean close behind the marker
  keeps the connection. VERIFIED.
- **HTTP/2**: `RST_STREAM( CANCEL )` (`Http2ConnectionTask.h:736-770`); the connection stays `Ready`,
  the outcome is `Failed`, and the connection is kept. VERIFIED.

**No new retry path.** The session decides a retry in `continuationTask( )`, and the hop's
`notifyReady( )` reaches it synchronously, inside this same drain: `notifyReadyImpl( )` calls the
ready callback off the task lock (`TaskBase.h:730`); the queue binds that callback to
`onReadyObserver( )` directly (`ExecutionQueueImpl.h:694-701`); the session's wrapper forwards the
same callback to its hop (`ForwarderTaskBaseT::scheduleNothrow( )`, `TaskBase.h:389-396`); and
`onReady( )` calls `continuationTask( )` (`ExecutionQueueImpl.h:499`). The drain flag is still set
then (it is cleared only at `:441`), so the close which the overflow's own cancel produces is at most
queued, never applied, when `chkPrepareRetry( )` (`ClientSession.h:1172-1220`) reads the hop:
`m_outcome` is still the constructor's `Failed` (`:367`; written only by `applyAcquired( )`'s refusal
and throw branches and by `applyClosed( )`, `:1216`) and `m_isRetryable` is still false (`:792`,
`:1210`), so `chkRequestMayBeReplayed( )` refuses (`ConnectionPool.h:489-514`). VERIFIED by reading.
**The one exception** is a close already in the batch behind the marker — a peer close racing the
overflow. It is applied in the apply phase and can read `ConnectionUnusable`, and then the existing
rule applies unchanged: the knob, idempotence, and `sinkDelivered( )` (`ClientSession.h:1193-1196`).
A replay can then reach only a sink which took nothing, which cannot duplicate bytes; D4 adds that a
sink which threw is never replayed onto.

## 4. What is dropped, and why that is not a silent truncation

**Dropped:** the block which would have crossed the cap, and every `Data` event posted after it — at
`post( )`, so they are never queued, counted, offered, or credited.

**Not dropped:**

- **the blocks queued ahead of the marker.** They are applied in order before it; on the sink path
  they are offered in that batch's deferred phase, before the completion is notified. The sink gets
  the prefix which arrived before the cap was crossed, in order, and nothing after it;
- **every other event kind** — `Headers`, `Trailers`, `BodyWanted`, `Closed`, `Expired`,
  `Cancelled` — queued as today. `Closed` has to be: it gives the pool its slot back
  (`releaseConnectionSlot( )`, `:1254`) and lets go of the connection (`:1263`), which is the pairing
  rule (`:1701-1718`). *"Nothing further is queued"* is read as *no further body bytes*; this is the
  one place the note interprets the decision's words.

**It is not a truncation which passes for a success, because the marker is ahead of the close.** The
contract makes `onClosed( )` the last event (`ClientConnection.h:140-143`) and one strand makes both
posts, so the marker is queued ahead of it (VERIFIED as the contract, INFERRED of each driver). The
marker's `failWith( )` sets `m_isCompletionPending` with an exception, and after that:

- `applyClosed( )` queues no `drainToSink( )` (`:1237-1242`), and `drainToSink( )` is the only caller
  of `onComplete( )` (`:1075`) — so the sink is never told the body is complete;
- `answerOnClosed( )` returns at once (`:1327-1336`) — the success path is never reached;
- the caller's task fails with `BufferTooSmallException`.

The sink is left holding a prefix with no terminal callback, which is the existing contract for every
failed streamed request (`applyClosed( )`'s comment, `:1231-1235`, and the `onComplete( outcome )`
deferral it cites). **And the peer is told to stop**: `RST_STREAM` on HTTP/2, the connection closed
on HTTP/1.1.

**Flow control is not disturbed by the drop.** On HTTP/1.1 `consumed( )` is a no-op
(`Http1ConnectionTask.h:2453-2461`). On HTTP/2 the dropped bytes are never credited, which is right
for a stream being reset — the buffered cap's own reason (`:917-921`) — and the connection window
does not leak: when the reset stream is reaped, `emitStreamClosed( )` credits whatever it still has
outstanding to the connection window (`Session.h:3550-3562`). VERIFIED.

## 5. Over HTTP/2

**The stream window bounds the same bytes first.**

- The session refuses a DATA frame larger than the stream's receive window with
  `FLOW_CONTROL_ERROR` (`Session.h:2302-2312`). VERIFIED.
- The window grows only by what we grant, and we grant only credit the consumer has returned:
  `onConsumed( )` moves bytes from outstanding to pending credit, and only `takeWindowUpdate( )` —
  sent from that credit — grows the window (`FlowControlWindow.h:605-624`, `:659-670`,
  `Session::consumed( )` `Session.h:1206-1224`). VERIFIED.
- The request task calls `consumed( )` only for bytes the sink took (`offerToSink( )`, `:1006-1009`)
  or the buffered path appended (`reportConsumed( )`, `:948`, `:1647-1673`). VERIFIED.
- Padding is received and credited at once and never delivered (`Session.h:1996-2001`). VERIFIED.

So bytes delivered and not taken never exceed the stream's initial receive window — INFERRED from the
four facts above. The credit reaches the session after the task's release, through the driver's
mailbox, which can only make what the peer is permitted smaller.

**That window is small today.** It is 65,535 until our SETTINGS are acknowledged (`Session.h:507-508`,
`Globals.h:232`), and afterwards the profile's `SETTINGS_INITIAL_WINDOW_SIZE`, up to 2^31 − 1
(`Session.h:2831-2841`, `:2856-2866`). Nothing under `src/include` sets that setting — a grep finds it
only in `utf_baselib_h2core`'s own cases — so today every HTTP/2 stream's backlog is under 64 KiB, and
the 64 MiB default cannot fire over HTTP/2. VERIFIED.

**It fires over HTTP/2 only when a profile advertises a window above the configured cap**, or a
caller configures a cap below their window, and then it behaves exactly as over HTTP/1.1:
`RST_STREAM( CANCEL )`, the request failed with `BufferTooSmallException`, the connection kept. The
cap never branches on the protocol, which is design 5.3's rule for this task (`:116-123`).

## 6. What a cap on payload does not bound — the one open choice

**The count is payload, and memory is not only payload.** A queued block also costs its mailbox
`Event` (`:254-270`: a `HeaderList`, an `error_code`, three counted pointers and an `exception_ptr`)
while it waits in the mailbox, and a `DataBlock` object with its own allocation for as long as it is
held. HTTP/1.1 makes one block per non-empty read (`deliverBodyChunk( )`,
`Http1ConnectionTask.h:1067-1089`), so **a peer which delivers one byte per read produces one block
per byte**, and a 64 MiB payload cap would hold up to 64 Mi blocks. The overhead per block was not
measured; if it is of the order of a few hundred bytes, which the structure suggests, such a backlog
is two orders of magnitude larger than its payload. INFERRED. Over HTTP/2 the same peer is bounded by
the window — at most 65,535 blocks per stream today.

**Proposal: charge each queued block a fixed allowance on top of its payload** — a named constant,
say 256 — at the rise and at the release (for a partially taken block the allowance goes when the
block leaves the queue). A 64 KiB block pays 0.4% more; one-byte blocks reach a 64 MiB cap after
about 261,000 of them, so the cap bounds the task's memory and not only its payload. The knob's
comment and the failure message say what is charged. **Reverses if** the review holds that the
decision fixed the quantity as payload bytes. The trickle case is then recorded as a risk D3 carries,
in the decision record, beside D3.

## 7. What else changes

- **D4** is implemented first in this lane. It records each block's accepted bytes in
  `offerToSink( )` before the next callback, and that is where this release goes (§2).
- **The buffered cap** is unchanged. Its refused block is now released, so after that failure the
  backlog count stays honest.
- **Comments made false by D3**, corrected in a comment-only commit of their own, after the logic:
  `Event`'s *"a queue which is bounded by the stream window"* (`:248-252`); `offerToSink( )`'s
  HTTP/1.1 paragraph (`:960-967`); and `drainToSink( )`'s *"a streamed body is capped by nothing this
  library sets … a bound is deliberately not taken"* (`:1033-1043`). A cap on what is outstanding is
  not a cap on the body, and it fails rather than truncates. The HTTP/1.1 driver's `consumed( )`
  comment (`Http1ConnectionTask.h:2443-2451`) stays true and is CS-1's file.

## 8. Tests — each red deterministic

1. **The mailbox** (`utf_baselib_httpclient`, a new test file, the module's probe connection). A sink
   holds its first offer at a rendezvous, so that every later post lands while the drain is inside
   it. The cap is set so that two blocks fit and a third crosses. The case delivers four blocks, the
   fourth after the crossing, then releases the sink and delivers the close. **Green:** the task fails
   with `BufferTooSmallException` naming the cap; the sink received exactly the first two blocks; the
   trace shows the cancel; `consumed( )` credited only those two; one release. **Red today:** the
   request succeeds with all four blocks. Deterministic, because every post happens while the drain is
   held inside the sink.
2. **A real HTTP/1.1 driver and a sink which takes nothing** (`utf_baselib_httpclient9`, the cleartext
   session, with `Http1DriverTestUtils.h`'s `ScriptedPeer`). The peer declares a body of 64 MiB and
   writes it from one reused 64 KiB buffer, so the test allocates nothing large. The cap is 256 KiB.
   **Green:** the task fails with `BufferTooSmallException` naming the cap, the sink took nothing, and
   the peer could not write the whole body — the client stopped taking bytes and closed. **Red today:**
   the whole body is buffered, and the request fails at the close with the *"did not take 67108864
   bytes"* `UnexpectedException` (`drainToSink( )`, `:1066-1072`) — the wrong exception, after exactly
   the large allocation the green run avoids. Deterministic: the red is the exception type, a pure
   function of the input. The short write rests on the socket buffers being far smaller than 64 MiB
   (INFERRED; Linux's ceilings are a few MiB). The case asserts the exception first, so a larger buffer
   could fail only the second assertion.
3. **The control** (`utf_baselib_httpclient9`). A sink which takes everything, a 1 MiB body, and a cap
   of 64 KiB. The peer writes 16 KiB pieces and sends each only after the sink's own tally shows the
   last one taken, so the backlog never exceeds one piece. **Green before and after**, with the whole
   body. It discriminates against a cap on the total, which would fail it — it is what *outstanding,
   not total* means.

`utf_baselib_httpclient` takes case 1 only if its growth measures small, per the brief; cases 2 and 3
go to `utf_baselib_httpclient9` with D5 and E2, one peer type in the module. If the allowance (§6) is
taken, case 1 states its cap in terms of the allowance, and a fourth case pins it: one-byte blocks
cross the cap after cap / (1 + allowance) blocks.

## 9. Risk and blast radius

Every response's data path passes `post( )` and the release. The request task serves both protocols
and the session, so every module which runs a request task is reached. Behaviour changes only past
the cap. Below it the change is one mailbox-lock acquisition per block taken or appended, and one
comparison per post.

## 10. Verified versus inferred

**Verified at the source:** where bytes enter and leave the task; which lock each site holds; the
existing task-to-mailbox lock edge; that `post( )` may not act; the path from the marker to the
completion; each driver's cancel and the outcome it produces; that `continuationTask( )` runs inside
the hop's drain, and what the hop's verdict reads then; the pending-failure guards on the sink's
terminal callback and on the success path; the connection-window credit on reap; HTTP/2's window
enforcement and crediting; the default window; and that no production profile sets one.

**Inferred:** that each driver honours "closed is last" (the contract says so); that bytes delivered
and not taken are bounded by the HTTP/2 window (from four verified facts); the size of the per-block
overhead (not measured); and that a 64 MiB body cannot fit the socket buffers on every platform.

**Not checked:** Windows and macOS socket buffer ceilings; any caller which relies today on more than
64 MiB of HTTP/1.1 backlog.

---

## Agreement

*(The dated agreement line goes here once the review is closed. D3 is not coded before it.)*
