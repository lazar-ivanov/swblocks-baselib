# D3 — a cap on the HTTP/1.1 body bytes a sink has not taken — design (CS-2)

**Date:** 2026-09-27. **Status: revision 1, AGREED 2026-09-27 - see the Agreement line at the end.**
Written by the CS-2 lane for the review the workflow requires before D3 is implemented
([`../parallel-implementation-workflow.md`](../parallel-implementation-workflow.md) §4.1). Nothing
was built or run for the mechanism; the two type sizes in §6 were measured with a compile-only probe.

**Revised 2026-09-27**, once, after fable's round-1 review and the maintainer's first decision round
of this run: §6 as the maintainer decided it; the `applyData( )` guard the maintainer folded into
D3 (§2, §3, §8); and the review's findings F1-F7 and proposals P2-P10, in its wording where it gave
wording. Round 2's Low findings R2-F1 to R2-F3 were folded in with the agreement line, in its
wording.

**What is decided, and is not reopened here** — [`astra-second-review-decisions.md`](astra-second-review-decisions.md)
§3, D3: *fail, in the request task, with a default of 64 MiB. A new cap beside `maxResponseBodySize`
in `HttpClientRequestConfig`, on body bytes received and not yet taken: counted as each data event is
posted, and released as the sink takes bytes or the buffered path appends them. Past it the stream is
cancelled and the request fails with `BufferTooSmallException`, as the buffered cap already does, and
nothing further is queued for that request.* And by the maintainer, 2026-09-27, in this run's first
decision round: **(2)** *bytes plus a derived allowance - each queued block is charged
`sizeof( Event ) + sizeof( DataBlock )` plus an allocator constant on top of its payload; the knob
keeps its name and its unit, bytes*; **(3)** *a guard in `applyData( )` drops a block once the
request has failed, and releases its charge*. This note settles the mechanism for all three.

**Anchors.** `file:line` at `7d21df3`, which is this lane's branch point, together with the function
name, because D4 (implemented first in this lane) moves lines in the same file. Unqualified line
numbers are `src/include/baselib/httpclient/HttpClientRequestTask.h`. **VERIFIED** means read at the
cited source; **INFERRED** means it follows from verified facts and was not observed.

---

## 1. The knob

- **`maxOutstandingResponseBodySize`**, a `cpp::ScalarTypeIniter< std::size_t >` beside
  `maxResponseBodySize` (`:84`), defaulted in the constructor from a new
  `DEFAULT_MAX_OUTSTANDING_RESPONSE_BODY_SIZE = 64U * 1024U * 1024U` beside `:88`. *Outstanding* is
  astra R02's word, the decision's own test wording, and `FlowControlWindow`'s word for received and
  not consumed - the same quantity one layer down. Its unit is bytes.
- **What it bounds:** what one request task holds of the body and has not handed on — the blocks
  posted to its mailbox and not yet applied, plus those applied to `m_pendingDownload` (`:317`) and
  not yet taken — **charged as each block's untaken payload plus a fixed per-block allowance** for
  the memory the block costs beyond its payload (§6).
- **It reaches the session with no new plumbing.** `ClientSessionConfig::requestConfig`
  (`ClientSession.h:72`) is copied into every hop by `startHop( )` (`ClientSession.h:1130-1136`).
  VERIFIED.
- **The knob's comment and the failure message say four things** (the review's P2): what is
  charged, which is payload plus the per-block allowance; that there is **no "off" value**, as
  `maxResponseBodySize` has none - a caller who wants none sets `SIZE_MAX`, and `0` fails the first block of any body,
  exactly as `maxResponseBodySize = 0` does; that it bounds what is **outstanding and not the body**;
  and that on the buffered path it is the backstop behind `maxResponseBodySize`, whose message
  differs, so a caller can meet this one on a body under the total cap. The comparison is written
  `charge > cap - count`, which cannot overflow because `count <= cap` holds by construction (§3).
- **Both paths.** On the sink path it bounds the backlog. On the buffered path the apply phase
  appends a block as soon as it applies it (`applyData( )`, `:943-948`), so the cap bounds only the
  mailbox there; `maxResponseBodySize` bounds the total and fires first while the drain keeps up.
  On that path the new cap is the backstop for a drain which has fallen behind — the drain runs on
  `ThreadPoolId::GeneralPurpose` (`post( )`, `:404-406`), not on the thread which reads.

## 2. Where the count rises and falls, and under which lock

| | Where | By | Thread | Held |
|---|---|---|---|---|
| **Rises** | `post( )`, for a `Data` event carrying a block, in the critical section which already does the `push_back` (`:381-383`) | the block's payload, `size( ) - offset1( )`, plus the allowance | the driver's strand: `onData( )` (`:1898-1911`) is called from `deliverBodyChunk( )` (`Http1ConnectionTask.h:1067-1089`) and `onDataEvent( )` (`Http2ConnectionTask.h:1400-1420`) | `m_mailboxLock` |
| **Falls (a): the sink took bytes** | `offerToSink( )` (`:969-1012`), per block, once `onData( )` has returned | what it returned, clamped to what was offered (`:979-982`); **and the allowance when the block is popped** - a partly taken block keeps its allowance until it is | the drain, deferred phase | nothing held; takes `m_mailboxLock` for the decrement |
| **Falls (b): the buffered path appended** | `applyData( )`, after the append (`:943-946`) | payload plus allowance | the drain, apply phase | the task lock (`applyEvents( )`, `:464`); takes `m_mailboxLock` |
| **Falls (c): the buffered cap refused the block** | `applyData( )`'s cap branch (`:915-941`), whose block is dropped - **the release first in that branch and unconditional** (the review's P4) | payload plus allowance | the same | the same |
| **Falls (d): the request had already failed** | a new guard at the TOP of `applyData( )`, before `armIdleTimer( )` - the maintainer's third decision | payload plus allowance, released only when the event carries a block (arm 1 charged none); the block is dropped, and nothing is offered, appended or credited | the same | the same |

- **(d) is the fourth site and the only new behaviour besides the cap.** Its test is "the
  completion is already decided" - `m_isCompleted || m_isCompletionPending` - and **while `Data` can
  still arrive, a decided completion can only be a failure.** The one pending SUCCESS is
  `answerOnClosed( )`'s (`:1386`), set on a clean `Closed`, and `Closed` is always the last event
  (`ClientConnection.h:140-143`) — and, for `Data` in particular, structurally on both drivers:
  HTTP/1.1 retires the sink and the handle under `m_stateLock` before `onClosed( )`
  (`Http1ConnectionTask.h:1753-1764`) and delivers body only through `tryGetActiveStream( )`
  (`:1517-1530`); HTTP/2 erases the stream before `onClosed( )` (`Http2ConnectionTask.h:1155-1200`)
  and `onDataEvent( )` returns without a sink (`:1400-1407`). VERIFIED — so no `Data` is ever
  applied behind a pending success, and every
  block (d) drops belongs to a request whose caller already holds its failure. That is a timeout or
  a cancel from `applyStopped( )`, which today still offers such blocks to the caller's sink (the
  review's §7 item 2), a sink which threw, a failed upload read, or the buffered cap. The overflow
  is not among them: what follows its marker is dropped at `post( )` by the latch and never applied.
  The guard says this at the source.
- **With the guard, the count's invariant is exact** (the review's §3.1, restated for the charge):
  `count` equals the sum, over the blocks in the mailbox and in `m_pendingDownload`, of each block's
  untaken payload plus its allowance. Nothing is released that was not charged - the crossing block
  and everything after the latch are never charged (§3) - so the count cannot go negative.
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
  where D4 records a block's accepted bytes (`b1adabf`), so the two land in the same place. A block
  the sink threw on was not taken and is not released: it is still held, and D4 offers it nothing
  more; what arrives after the throw's failure is released by (d).

## 3. From an overflow in `post( )` to a cancelled stream and a failed request

**`post( )` can decide and cannot act.** It is `NOEXCEPT` (`:374-409`), runs on the driver's strand,
and holds the mailbox lock. It may not cancel the stream or fail the request itself: the sink
methods are *"a post and nothing else"* (`:1859-1862`, rule L2), nothing may be called under the
mailbox lock (`:294-297`), and the completion state is the drain's, under the task lock
(`:303-306`). VERIFIED.

**So `post( )` classifies a `Data` event, in its existing critical section**, where `charge` is the
block's payload plus the allowance:

1. no block — queued as today, counting nothing;
2. the latch already set — **dropped**: not queued, not counted, no drain scheduled;
3. `charge > cap - count` — the latch is set and the event becomes a **marker**: its kind becomes a
   new `EventKind::Overflow`, its block is moved out of the event before the `push_back` and dropped
   after the critical section — nothing is released under the mailbox lock, which keeps `:294-297`
   true — and the marker is queued and a drain scheduled exactly as every post is;
4. otherwise `count += charge`, queued as today.

Nothing here can throw that cannot already throw today: the `push_back` (a `bad_alloc` reaching
`BL_NOEXCEPT_END`, unchanged). `count`, the latch and their invariant `count <= cap` are declared
beside `m_mailbox` with a comment naming the lock. (The events dropped by step 2 already die in
`onData( )`'s frame, off the lock, because `post( )` never takes them.)

**The drain applies the marker in order** — a new case in `applyEvent( )`'s switch (`:563-605`); it
has to be there, because the switch's `default:` shares `Start`'s arm (`:569-571`) and a marker which
fell into it would re-run the start, arming the total timer and acquiring a second connection — as
`applyOverflow( )`, which neither releases anything (the marker carries no charged block) nor arms
the idle timer:

- **if `m_isCompleted || m_isCompletionPending`, it returns.** The earlier verdict stands, and every
  path which set one has already reset the stream or never had one: `applyStopped( )` (`:1429`),
  the buffered cap (`:923`), a failed deferred action (`:508`), a close (`answerOnClosed( )`, after
  the stream ended), and `applyAcquired( )`'s refusals, which open no stream. VERIFIED per path.
  `applyStopped( )` opens the same way (`:1422-1425`);
- **otherwise `cancelStream( deferred )`** (`:1675-1699`) **and `failWith( )`** with a
  `BufferTooSmallException` — the buffered cap's type (`:925-938`), as decided — whose message says
  what was charged, along the lines of *"The HTTP response body received and not yet taken exceeded
  the maximum of N bytes, counting each held block's payload and an allowance of A bytes for its
  memory"*.

`applyEvents( )` then does what it does for every failure: the deferred cancel runs off the lock
(`:485`), after the `offerToSink( )` actions queued ahead of it in the same batch, the completion is
taken under the lock (`:513-520`), and `notifyReady( )` is called with it released (`:532-535`).
VERIFIED.

**The cancel is as prompt as the drain** (the review's P6): a marker posted while the drain is inside
a sink callback is applied when that callback returns, and until then the driver reads and `post( )`
discards. A sink which blocks for a minute costs a minute of discarded reads.

**What the pool is told** follows from the close the cancel produces:

- **HTTP/1.1** has no stream reset. `cancel( )` posts `onCancelStream( )`
  (`Http1ConnectionTask.h:2408-2440`); `onCancelStream( )` (`Http1ConnectionTask.h:2516-2539`) calls
  `finishStream( code, false, false )` (`:1549`); an unusable finish publishes `Draining` under the
  state lock before `onClosed( )`
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
With `retryIdempotentOnConnectionLoss` on, an idempotent request that overflowed and lost its
connection in one batch can be replayed, onto a sink which took nothing; the knob's contract covers
it and the buffered cap has the same shape (the review's P7). It cannot duplicate bytes, and D4
(`b1adabf`) adds that a sink which threw is never replayed onto. That the two caps share this shape
is recorded as one Low item on the owed list, not changed here.

## 4. What is dropped, and why that is not a silent truncation

**Dropped:** the block which would have crossed the cap, and every `Data` event posted after it — at
`post( )`, so they are never queued, counted, offered, or credited.

**No read is paused** (the review's P5): the drivers are untouched, the HTTP/1.1 read re-arms as
today, and what it reads after the latch is built into a block and dropped at `post( )` until the
cancel lands on the strand.

**Not dropped:**

- **the blocks queued ahead of the marker.** They are applied in order before it; on the sink path
  they are offered in that batch's deferred phase, before the completion is notified. The sink gets
  the prefix which arrived before the cap was crossed, in order, and nothing after it;
- **every other event kind** — `Start`, `Acquired`, `Headers`, `Trailers`, `BodyWanted`, `Closed`,
  `Expired`, `Cancelled` — queued as today. `Closed` has to be: it gives the pool its slot back
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
outstanding to the connection window (`Session.h:3550-3562`). VERIFIED. The same holds for what the
guard (d) drops: every failure it can follow has already reset the stream (§3), so those bytes are
squared up at the same reap.

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

So bytes delivered and not taken never exceed the stream's initial receive window: every byte posted
was received and every byte consumed was taken first, so **the task's untaken bytes <= the session's
outstanding <= the window** - VERIFIED by derivation from the four facts above (the review re-derived
it the same way). The credit reaches the session after the task's release, through the driver's
mailbox, which can only make what the peer is permitted smaller. The allowance changes none of this:
it is this task's own accounting and is never credited to anyone.

**That window is small today.** It is 65,535 until our SETTINGS are acknowledged (`Session.h:507-508`,
`Globals.h:232`), and afterwards the profile's `SETTINGS_INITIAL_WINDOW_SIZE`, up to 2^31 − 1
(`Session.h:2831-2841`, `:2856-2866`). Nothing under `src/include` sets that setting — a grep finds it
only in `utf_baselib_h2core`'s own cases — so today every HTTP/2 stream's backlog is under 64 KiB, and
the 64 MiB default cannot fire over HTTP/2. VERIFIED.

**It fires over HTTP/2 only when a profile advertises a window above the configured cap**, or a
caller configures a cap below their window, and then it behaves exactly as over HTTP/1.1:
`RST_STREAM( CANCEL )`, the request failed with `BufferTooSmallException`, the connection kept. The
cap never branches on the protocol, which is design 5.3's rule for this task (`:116-123`).

## 6. The per-block allowance — decided by the maintainer, 2026-09-27

**Why payload alone does not bound memory.** A queued block also costs its mailbox `Event`
(`:254-270`: a `HeaderList`, an `error_code`, two counted pointers and an `exception_ptr`) while it
waits in the mailbox, and a `DataBlock` object with its own allocation for as long as it is held.
HTTP/1.1 makes one exact-size block per non-empty read (`deliverBodyChunk( )`,
`Http1ConnectionTask.h:1067-1089`, `createInstance( m_bodyChunk.size( ) )` at `:1079`), so **a peer
which delivers one byte per read produces one block per byte**, and a payload cap of 64 MiB would
hold up to 64 Mi blocks before it fired - over 10 GiB at the sizes below. Over HTTP/2 the same peer is
bounded by the window — at most 65,535 blocks per stream today — but each of those blocks is a
`DataBlock` of at least `defaultCapacity( )`, 1 MiB (`Http2ConnectionTask.h:1076-1080`,
`DataBlock.h:435-450`, `:493`), so the per-block cost there is the driver's and dwarfs any allowance
this task could charge. That is CS-3's file: the maintainer decided the same day that CS-3 fixes it,
`blockOf( )` asking for the payload's own size. This note does not claim it.

**Decided: each queued block is charged a fixed allowance on top of its payload**, a named constant
derived from the types, with the derivation in its comment — a public static of the task, since
`Event` is the task's own protected type and §8's cases compute their caps from it;
`HttpClientRequestConfig`'s comment names it:

- `sizeof( Event ) + sizeof( data::DataBlock )` - the mailbox entry and the block object. Measured
  with a compile-only probe at a64 clang debug: **104 + 64**. `data::DataBlock` is already the
  concrete `om::ObjectImpl`, so its `sizeof` is the whole object;
- plus an allocator constant of **64**: two heap allocations per block - the block object and its
  buffer - each paying the allocator's header and its rounding, up to 32 bytes on the 64-bit
  allocators this builds with (glibc's smallest chunk is 32 bytes).

**232 bytes a block on this platform**, and whatever the types measure on another, since the constant
is computed from them. It is **charged at the rise** and **released when the block leaves the queue**,
which for a partly taken block is when it is popped (§2). The knob keeps its name and its unit, bytes,
and its comment and the failure message say what is charged. A 64 KiB block pays 0.35% more, so a 64 MiB
cap fires at about 63.8 MiB of body; one-byte blocks reach it after 64 MiB / 233, about 288,000
blocks, so the cap bounds the task's memory and not only its payload. A fourth case pins it (§8).

## 7. What else changes

- **D4** is implemented first in this lane (`b1adabf`). It records each block's accepted bytes in
  `offerToSink( )` before the next callback, and that is where this release goes (§2).
- **The buffered cap** is unchanged in what it decides. Its refused block is now released, first
  and unconditionally in its branch (P4), so after that failure the backlog count stays honest; and
  every block behind it is released by (d).
- **Comments made false by D3**, corrected in a comment-only commit of their own, after the logic:
  `Event`'s *"a queue which is bounded by the stream window"* (`:248-252`); `offerToSink( )`'s
  HTTP/1.1 paragraph (`:960-967`); and `drainToSink( )`'s *"a streamed body is capped by nothing this
  library sets … a bound is deliberately not taken"* (`:1033-1043`). A cap on what is outstanding is
  not a cap on the body, and it fails rather than truncates. The HTTP/1.1 driver's `consumed( )`
  comment (`Http1ConnectionTask.h:2443-2451`) stays true and is CS-1's file.
- **The focused-module runs cited as evidence are from the lane tip, after this commit, because the
  object changes with the line numbers** (P9): baselib's macros bake `__LINE__` in.

## 8. Tests — each red deterministic

1. **The mailbox** (`utf_baselib_httpclient`, the module's probe connection). A sink parks its FIRST
   offer only at a rendezvous, so that every later post lands while the drain is inside it. The cap
   is stated in terms of the allowance, so that two blocks fit and a third crosses. The case delivers
   four blocks, the fourth after the crossing, then releases the sink and delivers the close.
   **Green:** the task fails with `BufferTooSmallException` naming the cap; the sink received exactly
   the first two blocks; the trace shows the cancel; `consumed( )` credited only those two; one
   release. **Red today:** the request succeeds with all four blocks. Deterministic, because every
   post happens while the drain is held inside the sink.
2. **A real HTTP/1.1 driver and a sink which takes nothing** (`utf_baselib_httpclient9`, the cleartext
   session, with `Http1DriverTestUtils.h`'s `ScriptedPeer`). The peer declares a body of 64 MiB and
   writes it from one reused 64 KiB buffer, so the test allocates nothing large. The cap is 256 KiB.
   **Green:** the task fails with `BufferTooSmallException` naming the cap, the sink took nothing, and
   the peer could not write the whole body — the client stopped taking bytes and closed. **Red today:**
   the whole body is buffered, and the request fails at the close with the *"did not take 67108864
   bytes"* `UnexpectedException` (`drainToSink( )`, `:1066-1072`) — the wrong exception, after exactly
   the large allocation the green run avoids. Deterministic: the red is the exception type, a pure
   function of the input. The short write rests on two things: the socket buffers being far smaller
   than 64 MiB (INFERRED; Linux's ceilings are a few MiB), and the client's socket being CLOSED, since
   only a RST frees a writer blocked on a zero window. The driver's teardown is `shutdownSocket( )` —
   shutdown_send and cancel, no close (`TcpBaseTasks.h:236-330`, `:628-643`) — and the socket closes
   when the retired driver task is destroyed. So the peer is declared before the session and the pool,
   so that their destruction precedes its join, and its write count is read after that scope ends;
   the script writes the reused buffer in its own loop and records how many writes succeeded, because
   `ScriptedPeer::send( )` discards the result. The exception and the sink's zero are asserted first,
   before teardown, so a larger buffer or a lingering socket can fail only the short-write assertion.
   (Reverses if the green log shows the peer's write returning within the case without this ordering
   — then the ordering is a comment, not a requirement.)
3. **The control** (`utf_baselib_httpclient9`). A sink which takes everything, a 1 MiB body, and a cap
   of 64 KiB. The peer writes 16 KiB pieces and sends each only after the sink's own tally shows the
   last one taken, so the backlog never exceeds one piece. The script's wait on that tally is bounded
   by the utility's `WAIT_TIMEOUT_IN_MILLISECONDS` and records a timeout as a failure, so a failed run
   cannot hang the peer thread and the destructor's join; and the sink signals after appending, so
   the tally and the take are one step (P8). **Green before and after**, with the whole body. It
   discriminates against a cap on the total, which would fail it — it is what *outstanding, not
   total* means.
4. **The allowance** (`utf_baselib_httpclient`, the mailbox probe of case 1). One-byte blocks,
   delivered while the sink is parked, cross the cap after cap / ( 1 + allowance ) of them - the
   case computes the number from the task's own constant. Deterministic for case 1's reason. Red
   today: nothing crosses, and every block is delivered.
5. **No delivery after a failure** (`utf_baselib_httpclient`, the probe connection) - the guard (d).
   The case cancels the request and waits for the probe's `cancel:42` record, which the deferred
   phase makes after the apply phase has already failed the request; only then does it deliver a
   `Data` block, and then the close. It waits for the close's release, which the drain applies after
   the block. **Green:** the sink was never offered the block, nothing was credited, and the request
   failed with the cancellation. **Red today:** the block is offered to the sink of a request whose
   caller already holds its failure. Deterministic: the block is posted after the failure is decided,
   and the assertion waits on an event ordered after it.

`utf_baselib_httpclient` takes cases 1, 4 and 5 provided its growth measures small, per the brief
(D4's two cases measured +1.8% there); cases 2 and 3 go to `utf_baselib_httpclient9` — whose one
peer type is `ScriptedPeer`, which D5's cases use too (F5); E2 needs only a listener. Each case added
to `utf_baselib_httpclient` carries a `notes.txt` recipe: that module's index declares itself complete
and C9 holds it to that. `utf_baselib_httpclient9` was created by `src/utests/AGENTS.md`'s checklist
for D5 (`0f3b0ba`) and its index is complete too. Tier 1 will report the new cases and helper
members; the journal explains each line (F6).

## 9. Risk and blast radius

Every response's data path passes `post( )` and the release. The request task serves both protocols
and the session, so every module which runs a request task is reached. Behaviour changes only past
the cap, and - for the guard (d) - only after a request has already failed, on both protocols. Below
the cap the change is one mailbox-lock acquisition per block taken or appended, and one comparison
and one addition per post.

## 10. Verified versus inferred

**Verified at the source:** where bytes enter and leave the task; which lock each site holds; the
existing task-to-mailbox lock edge; that `post( )` may not act; the path from the marker to the
completion; each driver's cancel and the outcome it produces; that `continuationTask( )` runs inside
the hop's drain, and what the hop's verdict reads then; the pending-failure guards on the sink's
terminal callback and on the success path; the one writer of a pending success, which the guard (d)
rests on; the connection-window credit on reap; HTTP/2's window enforcement and crediting; the
default window; and that no production profile sets one. The review verified every citation of the
first draft at the same commit.

**Measured:** `sizeof( Event )` 104 and `sizeof( data::DataBlock )` 64, at a64 clang debug, with a
compile-only probe of the header.

**Inferred:** that each driver honours "closed is last" in full (the contract says so; that no `Data`
follows a `Closed` is verified at both drivers); the allocator
constant, 64, which is an estimate of two allocations' headers and rounding and not a measurement;
and that a 64 MiB body cannot fit the socket buffers on every platform.

**To be shown when coded** (the review's §5 item 9): a grep that no `Data` producer other than
`onData( )` and no consumer of `m_pendingDownload` other than the ones §2 names exists at the tip,
since the count's invariant depends on it.

**Not checked:** Windows and macOS socket buffer ceilings; any caller which relies today on more than
64 MiB of HTTP/1.1 backlog.

---

## Agreement

**Agreement: 2026-09-27.** Fable's round 2 (`D3-design-r2.md`) re-checked this revision against round 1 and the maintainer's decisions and agrees; the orchestrator agrees. D3 is implemented to this note.
