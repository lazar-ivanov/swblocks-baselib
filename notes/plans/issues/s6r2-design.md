# S6R.2 — design for the ten correctness fixes

**Status:** design, 2026-09-22. **Nothing implemented. Nothing under `src/` was touched and nothing
was built** — another lane was compiling on a two-core machine while this was written. This is the
artifact that must be agreed before code is written, per the review loop. **Reviewed 2026-09-22 —
see §17. Not agreed as first written: it left two of the three obligations S6R.1's implementation
review recorded for this slice undischarged, left one arm of N2 undecided, and put its HTTP/1.1
cases into a module already over the size target. The corrections are written in place below, each
dated, and §17 says which are findings and which are proposals.**

**Scope:** the ten findings grouped as R2 in `astra-review-verification-record.md` — H01, H07, H05,
H12, H15, H16, H18, H03b, N1, N2 — plus the settling of **N3**, the open question §5a of that record
leaves for this slice.

**What this design is not.** It is not a restatement of the findings; read the verification record
for those. It is the *intended change* for each, the reason that shape was chosen over the
alternatives, the boundaries it rests on, and what could go wrong.

---

## 0. Provenance — what was read, and what was taken on trust

An independent reviewer will read this against the source and will find the difference, so it is
stated first rather than buried.

**Read at the source by the author, in this worktree, whole functions from their signatures:**

- the entire HTTP/1.1 driver request path — `onStartRequest( )`, `onWriteCompleted( )`,
  `onBytesRead( )`, `onPeerClosed( )`, `onReadCompleted( )`, `finishStream( )`, `closeConnection( )`,
  `deriveIsReusable( )`, `serializeRequestHead( )`, `submit( )`, `cancel( )`, `onCancelStream( )`,
  `onTaskStoppedNothrow( )`, `scheduleTask( )`, `initiateClose( )`, and the class comment
  (`Http1ConnectionTask.h:40-120`);
- `MultiOperationTaskT`'s accounting — `beginOperation( )`, `beginClose( )`, `isClosing( )`,
  `takeTerminalNoLock( )`, `onOperationCompleted( )`, `applyDecision( )`;
- the request task's drain — `onDrain( )`, `applyEvents( )`, `runDeferred( )`, `applyEvent( )`,
  `applyHeaders( )`, `applyData( )`, `applyClosed( )`, `outcomeOnClosed( )`, `answerOnClosed( )`,
  `applyStopped( )`, `failWith( )`, `completeResponse( )`, and all ten `failWith( )` call sites;
- the retry rule — `chkRequestMayBeReplayed( )`, `isIdempotentMethod( )`, `RetryContext`, and
  `ConnectionPoolPolicy`'s constructor and defaults;
- the pool's maintenance timer — the members, the constructor's `deadline_timer` creation,
  `armMaintenance( )`, `onMaintenance( )`, `runActions( )`, `disposeInternal( )`;
- the h2 engine — `Session`'s constructor, `encoderTableSize( )`, `applyPeerSettings( )`'s
  `SETTINGS_HEADER_TABLE_SIZE` arm, `applyAcknowledgedSettings( )`, `createStreamContext( )`,
  `applyLocalInitialWindowSize( )`, `feed( )`, `handleFrame( )`, `queueOpeningFrames( )`,
  `applyLocalSettings( )`, `submitRequest( )`, `deliverHeaderBlock( )`, `SessionLimits`;
- `ReceiveFlowControlWindowT` in full, and `HpackEncoderT`'s constructor, `setDynamicTableCapacity( )`
  and the size-update emission in `encode( )`;
- the h2 driver — `pumpWrites( )`, `onWrite( )`, `isPeerClosed( )`, `onRead( )`, `onPeerClosed( )`,
  `closeAllStreamsUnwrittenRetryable( )`, `toSessionRequest( )`;
- the h1 codec — `Http1ResponseLimits`, `parseEof( )`, `needsEof( )`, `isComplete( )`,
  `fileInterimAndRestart( )`, `makeBackend( )`, and the Beast backend's `putEof( )` / `isDone( )`;
- `ClientSession`'s redirect rewrite, and `http::HeaderList`'s field-name rule;
- **two artifacts outside this repository, in the exact versions the build uses**
  (`/home/lazar/swblocks/dist-devenv7-ub24-gcc1520-clang2010-a64/boost/1.90.0/source-linux`):
  Beast's `basic_parser::put_eof( )` and asio's `write_op` constructor. Both were opened because a
  justification below would have been false if they behaved otherwise.

**Taken from the verification record or from astra, and NOT re-derived here:** nothing load-bearing.
Every claim below that carries a file and a line was read. Where a claim is an inference rather than
a reading — a platform behaviour, a library contract, a probability — it says so in the sentence.

**Where this design contradicts or extends the verification record, §13 lists it.**

---

## 1. H01 — the write-completion barrier

**Defect, with the mechanism stated more exactly than the finding does.** `onStartRequest( )` stores
the serialized head in `m_requestHead` and the body reference in `m_requestBody`
(`Http1ConnectionTask.h:623-624`), builds a `std::vector< asio::const_buffer >` of views into them
(`:626`, `:631-635`) and hands it to `asio::async_write( )` (`:666`). `finishStream( )` clears both
(`:1110-1111`) and republishes the connection as `Ready` (`:1100-1106`) without any regard for
whether that write has completed.

The *vector* is not the problem: asio's `write_op` copies the buffer sequence into the operation
(`asio/impl/write.hpp:317-327`, `buffers_( buffers )`), so a local sequence is correct. What must
survive is the memory the `const_buffer`s point at, and that is what `finishStream( )` releases.

**This is not a cross-thread data race, and the distinction is what makes it testable.** The class
comment narrows this driver's correctness to the stranded policies and says so in as many words
(`:71-81`): every handler and every post runs on the socket's executor, which is the strand. A
composed `async_write` returns to that executor between its internal `async_write_some` calls, so
the read handler — and `finishStream( )` with it — can run **in the middle of one write**, on one
thread, deterministically. astra's "memory safety and wire corruption are possible" is right; the
verification record repeats it without the mechanism, and the mechanism is what buys a red test.

Three faces, in descending order of certainty:

1. **A second overlapping `async_write` on the same stream.** `finishStream( )` clears `m_handle`
   and sets `m_state = Ready`, and `submit( )` gates on exactly those two (`:1501-1507`). So the
   pool may hand this connection a new request while the first write is pending; `onStartRequest( )`
   then assigns `m_requestHead = serializeRequestHead( ... )` — which may reallocate the string
   under a pending `const_buffer` — and issues a second composed write over a stream that already
   has one. asio forbids overlapping writes on one stream; under a TLS policy it corrupts the
   record stream outright.
2. **A dropped body reference.** `m_requestBody.reset( )` (`:1111`) and
   `m_request = ClientRequest( )` (`:1097`) drop both of the driver's references to the caller's
   `DataBlock`. Whether that is the last reference is the caller's business, which is precisely why
   it is not something this driver may assume — astra says the same. When it is, the pending write
   reads freed memory.
3. **A mutated head.** `std::string::clear( )` keeps the allocation but writes the terminator at
   index 0 on the standard libraries this builds with — *inferred from those implementations, not
   read* — so the first octet of a head still going out can become NUL. Listed last because it is
   the only face that depends on an implementation detail; faces 1 and 2 do not.

**Change — one flag, in the vocabulary this library already uses, and three consequences of it.**

    bool m_isWriteInFlight = false;     // beside m_requestMayHaveBeenSent, which S6R.1 renames

(a) **Set it immediately before `async_write` and clear it in the handler.** This is verbatim the
    HTTP/2 driver's shape: `pumpWrites( )` sets `m_isWriteInFlight = true` before its `async_write`
    (`Http2ConnectionTask.h:1622-1634`) and `onWrite( )` clears it as its first act after the prolog
    (`:1665`). The h1 driver's `async_write` is inside a `try`, so the `catch` clears it too, before
    the existing `onOperationCompleted( )` call (`Http1ConnectionTask.h:677-685`).

    *Placement, added by the 2026-09-22 review.* In `onWriteCompleted( )` the clear is the first
    statement after `BL_TASKS_HANDLER_BEGIN( )` and **before** `BL_TASKS_HANDLER_CHK_EC( ec )` —
    where the h2 driver's `onWrite( )` has it (`:1665`, ahead of its `if( ec )`). `CHK_EC` throws to
    the epilog (`TaskBase.h:139-158`), so anything placed after it never runs for a write that
    failed. The two clears of (c) and the exact answer of §1a go in the same place, for the same
    reason: a write that fails with zero octets transferred is exactly the case that must read
    `m_requestMayHaveBeenSent = false` on its way to `onTaskStoppedNothrow( )`, and a write that
    fails after transferring some must still let go of the caller's `DataBlock` rather than hold it
    until the pool forgets the driver.

(b) **A write still in flight makes the connection non-reusable.** `finishStream( )`'s verdict
    becomes

        const bool isReusable =
            isConnectionUsable && ! base_type::isClosing() && ! m_isWriteInFlight;

    (one line, at `:1084`). Everything downstream already follows: `! isReusable` takes
    `closeConnection( )` (`:1123`), which is `beginClose( )`, and the read handler's own
    `BL_TASKS_HANDLER_END_MULTIOP( )` epilog then reaches `onOperationCompleted( )` with
    `m_closing` true and `m_closeInitiated` false, which is the one call that runs `initiateClose( )`
    (`MultiOperationTask.h:369-384`) — and `initiateClose( )` cancels the socket
    (`Http1ConnectionTask.h:1285-1307`), which wakes the pending write. **The barrier therefore does
    not hang: the same handler that trips it is the one that frees the write.** That chain was read
    end to end, because a barrier which waits for a handler nothing will wake is a deadlock and not
    a fix.

(c) **The write's storage is released by the write's handler.** The two clears move out of
    `finishStream( )`'s unconditional block into `onWriteCompleted( )`, and stay in `finishStream( )`
    only for the paths where no write was ever issued — expressed as `if( ! m_isWriteInFlight )`.
    Nothing else in the file reads either member (grepped over `src/`: the only readers are
    `onStartRequest( )`'s own assignment and the two `asio::buffer` expressions), so releasing them
    as soon as the write settles is both safe and the earliest correct point.

    **Exactly two of the seven statements in that block move.** `m_requestHead.clear( )` (`:1110`)
    and `m_requestBody.reset( )` (`:1111`). `m_parser.reset( )`, `m_bodyChunk.clear( )`,
    `m_headersDelivered`, `m_requestMayHaveBeenSent` and `m_requestSaidClose` are response-side or
    per-stream state with no pending operation holding them, and they stay unconditional.

**Why the connection must be refused rather than drained.** astra's correction says "stop or drain
an early-aborted upload". Draining is wrong for a pooled client: the peer that stopped reading sets
the pace, so the drain is unbounded, and the request bytes still in our send buffer are bytes the
server has not consumed — put another request behind them and the server reads our next request line
as this request's body. Refusing reuse states that in one predicate, and it is the exact mirror of a
rule `deriveIsReusable( )` already applies in the other direction: a response that left bytes
unconsumed (`m_readValid != 0`, `:448`) makes the connection unusable "because they are either
unsolicited or a second message the server smuggled behind the first". H01 is the same sentence with
the arrow reversed.

**Why the flag and not per-write heap storage.** The alternative considered and rejected was a
per-write holder (head plus body reference) bound into the completion handler, so that the members
disappear and lifetime is structural. It is defensible, and it is *not what this library does*: the
h2 driver writes out of the member `m_writeBuffer` and protects it with `m_isWriteInFlight`, clearing
it only inside `pumpWrites( )` under that guard (`Http2ConnectionTask.h:1591-1605`). Copying a rule
this library already states beats inventing one — the same argument S6R.1 made for H02 — and the
holder buys nothing the flag does not, because face 1 (overlapping writes) needs the flag anyway.

**What could go wrong.**

- **Keep-alive reuse could regress into a coin toss.** It does not, and the reason is ordering, not
  luck. For a request whose bytes fit the socket send buffer, the write completion is queued on the
  strand the moment the kernel accepts them, and the response cannot arrive for at least one round
  trip; strand FIFO then runs `onWriteCompleted( )` first. The write is still pending at response
  time exactly when the body did *not* fit — which is the case where refusing reuse is correct.
- **A false negative exists and is accepted.** A server that answers successfully before draining a
  large body costs us a connection. That is a connection, not a correctness loss, and the
  alternative is face 1.
- **The `catch` clearing the flag rests on a premise this file already rests on.** If
  `async_write` could throw *after* initiating, the catch would clear a flag the handler still owes —
  but that same catch already calls `onOperationCompleted( )`, so under that hypothesis the
  accounting is already double-counting. The new line adds no premise; it shares one.
- **`onCancelStream( )` does not wake anything by itself.** It is a plain post, not an accounted
  handler, so its `finishStream( )` → `closeConnection( )` sets `m_closing` with no operation
  completing behind it; the socket is cancelled only when the pool's `requestCancel( )` → posted
  `shutdownOnStreamExecutor( )` lands (`:1375-1409`). That is pre-existing and unchanged by this
  design, stated so nobody attributes it to the barrier.
- **A pending write woken by the peer rather than by our cancel ends the task failed, not clean.**
  `onOperationCompleted( )` excuses only `operation_aborted`, and only while closing deliberately
  (`MultiOperationTask.h:357-366`); a write that the peer's RST completes with `broken_pipe` or
  `connection_reset` after the barrier tripped is recorded as the task's first error. The request
  was answered before that, and the pool retires a Draining connection whether its task ended clean
  or failed, so nothing is lost — but a test on this path must not `waitForSuccess( )` the driver
  task. Added by the 2026-09-22 review.

**Test — deterministic, red before and green after, and it does not rely on catching a fault.**

The harness is `TestHttp1ConnectionTask.h`'s scripted blocking TCP peer (`ScriptedPeer`, `:398-730`)
and the `PlainEstablisherImpl` / `PlainDriverImpl` pair (`:749-751`). The module is **not**
`utf_baselib_httpclient3` — see §14, corrected 2026-09-22 — and the peer's vocabulary has no
socket-option step today, so `receive_buffer_size` on the accepted socket is a step the lane adds.

*Case 1 — the barrier.* Script: accept, read only up to the blank line, write a complete
`413` with `Content-Length: 0` and no `Connection: close`, then stop reading and hold the socket
open. Submit a POST whose body cannot fit the peer's receive buffer plus our send buffer — made
deterministic by setting `SO_RCVBUF` small on the accepted socket (and, if needed, `SO_SNDBUF` on
the established stream before it is attached) rather than by choosing a large number and hoping.
Assert, after `onClosed`: `state( ) != Ready` and `freeStreamSlots( ) == 0`.
**Red today**: `deriveIsReusable( )` returns true for that response — complete, no `needsEof`, not
101, no close token, HTTP/1.1, nothing unconsumed — so `finishStream( )` publishes `Ready`.

*Case 2 — the overlap face, same script.* After `onClosed`, call `submit( )` again on the same
driver. **Red today**: it returns a valid handle and a second `async_write` goes out over the first.
**Green after**: `INVALID_STREAM_HANDLE`.

Neither case asserts on a crash, which is what makes them worth their green. An ASan run over the
module is worth doing on top and is not the evidence.

### 1a. What S6R.2 does to the lines S6R.1 touched

S6R.1's H02 has landed — `6fe658d`, merged at `3bcf21e` — and it put four things in this file, not
the three this paragraph first counted: `m_requestMayHaveBeenSent = true` before `beginOperation( )`
and the `async_write` `try` (`:687` on `lazari2`), under a 21-line comment whose last sentence reads
*"The exact answer - 'zero octets escaped, so this is safe to replay' - needs the write-completion
barrier of H01 and is S6R.2's"*; the kept-but-redundant `if( 0U != bytesTransferred )
m_requestMayHaveBeenSent = true;` in `onWriteCompleted( )` (`:725-733`), under a comment that says
it is redundant; the rename at the declaration (`:209`; `:212` on `lazari2`, now with a comment of
its own) and the three read sites (`:974`, `:986`, `:1457`; `+33` on `lazari2`) plus the reset in
`finishStream( )` (`:1114`; `:1147`). **This worktree's `src/` is the tree S6R.1 was designed
against, not the one it landed on**: every HTTP/1.1-driver line number in this document is the
lane's, and the lane that implements S6R.2 branches from `lazari2` at `a36b04e` or later. (Corrected
2026-09-22.)

**S6R.2 changes exactly one of those lines, adds one beside another, and leaves the rest alone.**

- **The set point stays.** `m_requestMayHaveBeenSent = true` stays immediately before `async_write`,
  for S6R.1's reason, and `m_isWriteInFlight = true` goes beside it.
- **The two flags deliberately diverge in the `catch`.** `m_isWriteInFlight` is cleared there;
  `m_requestMayHaveBeenSent` is **not**. They answer different questions — "is a handler owed" and
  "may bytes have escaped" — and a lane that folds them into one reintroduces H02 on the throwing
  initiator, which is the case S6R.1's justification is built on.
- **The redundant line in `onWriteCompleted( )` is replaced by the exact answer.**

      if( 0U == bytesTransferred )
      {
          m_requestMayHaveBeenSent = false;
      }

  This is the "zero bytes escaped, so retry is safe" that S6R.1 names and defers here. The rename
  is not undone and no member returns.
- **The three read sites are untouched.** The rename stands, and so does `finishStream( )`'s reset.
- **The three comments S6R.1 landed move with the code, or one of them becomes the sixth comment
  in this feature to outlive what it described.** The set-point comment's last sentence — *"is
  S6R.2's"* — becomes "is answered in `onWriteCompleted( )`"; the *"Redundant since…"* comment goes
  with the line it excused; and the member's comment gains its second half: *set before the write is
  issued, and cleared by a write which transferred nothing*. Added by the 2026-09-22 review.

**Why the exact answer is taken rather than left.** S6R.1's approximation is strictly conservative
against *today*, and that has a price this design measured at the source:
`chkRequestMayBeReplayed( )` returns from its `isRetryable` limb or falls to
`context.isConnectionLost && policy.retryIdempotentOnConnectionLoss && isIdempotentMethod( ... )`
(`ConnectionPool.h:436-455`), and **`retryIdempotentOnConnectionLoss` defaults to false** — the
constructor (`:297-310`) sets six fields in its body and three in its initializer list, and not that
one, and its own comment says "default off" (the count was "five" before the 2026-09-22 review).
So after S6R.1, a peer that resets an idle pooled connection before a single byte leaves takes the
request down with it, for GET as much as for POST, with no fallback limb to catch it. Today that
request is retried, because the flag is false. The exact answer restores exactly that case and
nothing else.

**Why `bytesTransferred == 0` is proof and not an approximation.** `async_write` reports the
cumulative total across its internal `write_some` calls; zero means no octet was ever handed to the
stream. Under the TLS policy it means no plaintext octet was encrypted into a record. Neither is a
statement about what the peer did with bytes it received — it is a statement that there were none.

**Where the answer is fully settled, and where the flag is what settles it.** `onTaskStoppedNothrow( )`
runs from `notifyReady( )`, which `applyDecision( )` reaches only when `takeTerminalNoLock( )` saw
`0U == m_pendingOperations` (`MultiOperationTask.h:147-157`, `:378-384`). Every begun operation has
therefore completed, so at that read site the write has always settled and the answer is exact.
`onPeerClosed( )` can run with the write still pending; there `m_isWriteInFlight` is true, the flag
is still true from the set point, and the answer is S6R.1's conservative one — which is correct,
because a pending write is not proof of anything.

**And the barrier is what makes the clear safe.** Clearing a flag back to false would be a hazard if
a second request could have started in between. Under (b) it cannot: the connection is not `Ready`
while `m_isWriteInFlight` is true.

---

## 2. H07 — a throwing terminal sink callback must not become a success

**Defect, read end to end.** `applyClosed( )` pushes `sink -> onComplete( )` onto the deferred list
and then calls `answerOnClosed( )` (`HttpClientRequestTask.h:1130-1145`), which on a clean close runs
`completeResponse( )` and sets `m_isCompletionPending = true` (`:1268-1272`). `applyEvents( )` then
runs the deferred list (`:475`), and `runDeferred( )` keeps the first exception (`:533-548`). Back
under the lock it calls `failWith( deferredException, false )` (`:501`) — and `failWith( )` returns
immediately when `m_isCompleted || m_isCompletionPending` (`:1395-1398`). The pending success then
publishes at `:504-511`. The caller is told the request succeeded and the exception is discarded.

**Change — two guards in place of one, in `failWith( )`.**

    if( m_isCompleted )                                       // published; too late
    {
        return;
    }

    if( m_isCompletionPending && m_completionException )      // an earlier real failure wins
    {
        return;
    }

    if( ! m_isCompletionPending )     // a pending completion already ran it
    {
        completeResponse();
    }

`cancelAllTimers( )` stays unconditional; it is idempotent. The `completeResponse( )` guard is not
cosmetic — a second call would rebuild `m_response.body( )` from `m_responseBody` all over again, and
`m_connection` has already been released by `releaseConnection( )` (`:1289-1300`) by then, so the
negotiated protocol it filled in would not be filled in twice but the body copy would.

**Why this shape.** The comments already promise it: "the FIRST failure is what the request is failed
with" (`:466-473`). What was missing is that a *pending success* is not a failure and must not
outrank one. The two guards say exactly that — published wins over everything, an earlier failure
wins over a later one, a pending success loses to any failure.

**What could go wrong, and why it does not.** The guard is widened for all ten `failWith( )`
callers, so each was opened. Eight of them (`:692`, `:744`, `:789`, `:803`, `:1243`, `:1255`,
`:1319`, `:1335`) are in phase one and already unreachable behind a caller-side
`if( m_isCompleted || m_isCompletionPending ) return;` — `applyAcquired( )` (`:676`),
`answerOnClosed( )` (`:1213`), `applyStopped( )` (`:1308`). The ninth is the phase-three site
(`:501`) this fix is for. The tenth is `applyData( )`'s response-body cap (`:916`), which has
**no** such guard:
it could now upgrade a pending success to a failure if a Data event were applied after a Closed one.
The sink contract makes `onClosed` last, so that ordering is a driver defect rather than a state this
change creates — and if it ever happened, failing is the better answer than silently appending past
the cap. Recorded rather than left to be discovered.

**Test.** Two cases in the request-task module, both pure and deterministic, no network: a sink whose
`onComplete( )` throws, and a sink whose final `onData( )` throws in the same batch as the close.
Assert the task is `isFailed( )` and that the exception is the sink's. **Red before**: the task
completes successfully today and the exception is swallowed.

**Do not assert that the stream was reset — corrected by the 2026-09-22 review.** This paragraph
used to end: *"Assert also that the stream was reset — `cancelStream( )` already runs on this path
(`:497`) and it must not regress."* In both cases the stream is already closed by the time the sink
throws: `applyClosed( )` marks it closed and calls `releaseConnection( )`, which resets
`m_connection` (`:1289-1296`), before the deferred phase runs the sink at all, and `cancelStream( )`
returns at once on a null connection (`:1531-1534`). There is nothing to reset and nothing on the
wire to observe; the `:497` call is for a sink that throws mid-stream, which is a different case.
Assert instead that `releaseStream( )` was still called exactly once, with `Completed` — the peer
did speak, the slot goes back, and the connection stays poolable — which is what a lane fixing this
by reordering `applyClosed( )` would break.

---

## 3. H05 — bound aggregate informational responses

**Defect, both halves read.** On h2, `deliverHeaderBlock( )` queues a `Headers` event for every block
and, for an informational one, records nothing on the stream context at all
(`Session.h:3155-3199`) — there is no count and no byte total anywhere on that path. On h1,
`fileInterimAndRestart( )` appends to `m_interimResponses` and then calls `makeBackend( )`, which
constructs a *fresh* backend with `m_limits.maxHeadersSize` (`Http1Codec.h`,
`fileInterimAndRestart( )` and `makeBackend( )`) — so every interim gets its own full 64 KB budget
and the per-message cap never accumulates. The request task is a third accumulation point:
`applyHeaders( )` pushes each interim onto `m_interimResponses` unconditionally
(`HttpClientRequestTask.h:845-856`).

**Change — bound it at the two parsers, and the third point is then bounded by construction.**

- `SessionLimits` gains two fields beside the nine it already has (`Session.h:88-128`), with
  defaults in `Globals` like every other one: `maxInterimResponsesPerStream` (proposed 8) and
  `maxInterimHeaderBytesPerStream` (proposed 64 KB — one block's worth in total, not per block).
- `StreamContext` gains the two counters; `deliverHeaderBlock( )` increments them on the
  informational arm, using the same decoded-list size the per-block cap already computes.
  *Precision, 2026-09-22 review:* that cap is applied inside `m_decoder.decode( )`, which is handed
  `m_localMaxHeaderListSize` (`Session.h:2030-2035`) and does not hand the measured size back.
  Unless the decode outcome carries it, measure the list in `deliverHeaderBlock( )` itself — the
  RFC 9113 §6.5.2 sum, name length plus value length plus 32 per field, if that is what the decoder
  counts; check rather than assume — and use the same measure on the h1 side, so the two limits
  name one number.
- On breach the **stream** is reset, not the connection. A peer flooding one stream costs that
  request; a connection error would cost every other request on the connection for one peer's
  behaviour.
- `Http1ResponseLimits` gains the same two, enforced in `fileInterimAndRestart( )` through the
  existing `fail( )`/`Http1CodecError` path, which the driver already turns into a finished stream
  and a closed connection. h1 has no stream reset — `cancel( )`'s comment says so
  (`Http1ConnectionTask.h:1532-1541`) — so closing is the only lever and is what astra names.
  *Precision, 2026-09-22 review:* `fileInterimAndRestart( )` takes no `error_code`
  (`Http1Codec.h:694-716`); its caller `parse( )` does (`:500-505`), so the check runs there, before
  the restart, or the function gains the parameter. And `Http1CodecError`'s own comment says every
  value is "a security property somebody will want to assert on directly", so the breach should be
  a new value — proposed `TooManyInterimResponses`, covering the count and the byte total alike —
  rather than a reuse of `HeadersTooLarge`, which names a different limit. Proposal, not finding.
- **Nothing is added to the request task.** With both parsers bounded, `m_interimResponses` is
  bounded by the same numbers. Adding a third cap would put the same rule in three places.

**Why the limit is a pair and not one number.** A count alone lets 8 blocks of 64 KB through; a byte
total alone lets thousands of tiny blocks through and each one costs a `std::vector` push and an
event. Both are cheap; the existing limits table already pairs them this way for header blocks.

**Boundary.** `103 Early Hints` is a real feature a real server sends, usually once and small. Eight
is above anything a compliant server does and below anything that matters as memory. The number is a
default on a `SessionLimits`/`Http1ResponseLimits` field, so a caller who disagrees can move it.

**Test.** h2: feed nine informational blocks on one stream and assert a RST_STREAM and that the
stream closed with an error, plus a control at eight that still succeeds. h1: the same against the
codec, asserting the `Http1CodecError`. **Red before**: both accept unboundedly today. The h2 case
needs the `settle( )` helper the session tests already have.

---

## 4. H12 — the encoder table must start at the protocol default

**Defect.** `Session`'s initializer list does `m_encoder( encoderTableSize( profile ) )`
(`Session.h:516`), and `encoderTableSize( )` returns the profile's value verbatim (`:1267-1277`).
`HpackEncoderT`'s constructor initializes the table to that and sets `m_sizeUpdatePending( false )`
(`HpackEncoder.h:108-118`). So a profile asking for more than 4096 indexes entries a default peer
has already evicted, with no dynamic table size update to tell it otherwise.

**Change — one expression.**

    m_encoder(
        std::min< std::size_t >(
            encoderTableSize( profile ),
            static_cast< std::size_t >( Globals::HEADER_TABLE_SIZE_DEFAULT )
            )
        )

**Why that is the whole fix.** `applyPeerSettings( )` already does the rest, and it already does it
correctly: its `SETTINGS_HEADER_TABLE_SIZE` arm calls
`m_encoder.setDynamicTableCapacity( std::min( encoderTableSize( m_profile ), value ) )`
(`Session.h:2471-2480`), and `setDynamicTableCapacity( )` sets `m_sizeUpdatePending`, which
`encode( )` emits at the start of the next block including the smallest-then-final rule
(`HpackEncoder.h:137-165`, `:186-196`). So after the constructor change the profile's larger capacity
is applied **exactly when the peer's SETTINGS permits it, and announced** — which is the RFC rule —
and if the peer never advertises the setting we stay at 4096, which is what RFC 9113 §6.5.2's initial
value requires. A peer that advertises exactly 4096 is the same case: `setDynamicTableCapacity( )`
returns without arming an update when the value equals the current capacity
(`HpackEncoder.h:147-155`), which is what lets the amended test's control at 4096 assert that *no*
size update appears in the produced bytes. (Added by the 2026-09-22 review.)

**The record's correction is confirmed and matters.** A *smaller* profile capacity is harmless and
needs no announcement: both tables insert the same entries in the same order, so the peer's table is
a superset of ours and every index we emit names the same entry in both. `std::min` leaves that case
untouched.

**What could go wrong — and here this design contradicts the verification record.** The record names
H06 as the one finding "pinned by a test". **H12 is pinned too**:
`src/utests/utf_baselib_h2core/TestSession.h:3254-3261` sets `hpackEncoderTableSize = 65536U` and
asserts `hpackEncoderTableCapacity( ) == 65536` immediately after construction. That assertion is the
defect, and the fix overturns it. The rest of that block still passes as written — the peer advertises
4096, `min` is 4096, the capacity is already 4096 — but its comment, "The new capacity is signalled at
the start of the next block", becomes **false**, because nothing changed and nothing is signalled.
That is precisely the shape S6R.1 recorded for H14: a test that stays green while its comment stops
being true. The change-set owes the amendment: assert 4096 after construction, and have the peer
advertise something *between* 4096 and the profile's value — 16384 — so the raise really is exercised
and the comment is true again.

**Test.** The amended block above is the test: `4096` after construction is red today, and the raise
to `min( 65536, 16384 )` with a size update in the produced bytes is green after.

---

## 5. H15 — recompute the automatic update threshold when the window moves

**Defect.** `ReceiveFlowControlWindowT`'s constructor derives `m_updateThreshold( initialSize / 2 )`
(`FlowControlWindow.h:470-478`). `applyInitialWindowSizeChange( )` forwards to the window and leaves
the threshold alone (`:534-540`), and `shouldSendWindowUpdate( )` is
`m_pendingCredit > 0 && m_pendingCredit >= m_updateThreshold` (`:598-600`). A stream opened before
our SETTINGS is acknowledged starts at 65535 with a threshold of 32767; the acknowledgement lowers
the window to the profile's value and leaves 32767 behind. With a 1024-octet window, 32767 octets of
pending credit can never accumulate, so no WINDOW_UPDATE is ever due and the stream stops for good.

**Narrower than it reads, and the narrowing was verified.** `createStreamContext( )` builds each
stream's receive window from `m_localInitialWindowSize` (`Session.h:1432-1446`), which
`applyLocalInitialWindowSize( )` has already updated by then (`:2664-2674`). So a stream opened
**after** the acknowledgement gets the right size *and* the right half-window threshold, for free.
The defect is confined to streams that were already open — the rider of design 5.5 — which is exactly
the case astra names. The connection window is untouched by this setting, per RFC 9113 §6.9.2, and
the code agrees.

**Change — two parts, and the second is the one that makes the class safe rather than merely fixed.**

(a) A `bool m_isThresholdExplicit` set by `setUpdateThreshold( )`. `applyInitialWindowSizeChange( )`
    recomputes `m_updateThreshold = newValue / 2` when it is false. The flag needs no plumbing:
    `setUpdateThreshold( )` has exactly two callers (`Session.h:536-540` and `:1452-1457`) and both
    are already gated on `profile.windowUpdateThreshold != 0U`, so "explicit" and "a profile set it"
    are the same predicate.

(b) A liveness floor in `shouldSendWindowUpdate( )`:

        return m_pendingCredit > 0 &&
            ( m_pendingCredit >= m_updateThreshold || m_window.size() <= 0 );

    An exhausted window with credit owed is a stall whatever the threshold is, and the threshold's
    only purpose — batching frames — is worthless once there is nothing left to batch *for*.

**Why (b) as well as (a).** (a) fixes the case the finding names. It does not fix a profile that sets
`windowUpdateThreshold` larger than the window it also asks for, which wedges identically and is a
caller mistake this library should not turn into a hang. (b) makes the wedge unreachable by
construction; (a) keeps the default policy honest. Neither is redundant.

**What could go wrong.** (b) trades frames for liveness: a small window with a consumer taking a few
octets at a time will emit an update per consumption once the window is empty. That is the only
behaviour that makes progress at all, and it fires only at zero.

**Test.** In `utf_baselib_h2core`: open a stream before the ACK, acknowledge a 1024-octet initial
window, feed and consume 1024 octets, and assert a WINDOW_UPDATE for that stream id in the produced
bytes. **Red before**: zero WINDOW_UPDATE frames. A second case sets an explicit threshold above the
window and asserts the same, which is red before *and* after (a) alone — it is what pins (b).

---

## 6. H16 — normalize protocol-specific fields at the h2 driver boundary

**Defect.** `toSessionRequest( )` copies `request.headers( )` unchanged
(`Http2ConnectionTask.h:2790-2811`); `submitRequest( )` checks only that method, scheme and path are
non-empty and then appends every field (`Session.h:906-957`). The h1 renderer by comparison refuses
`Transfer-Encoding`, supplies `Host` when absent and **sets or removes `Content-Length` from the body
that will actually be written** (`Http1ConnectionTask.h:475-514`).

**The internal trigger is real and was verified — and corrected by the 2026-09-22 review.** This
paragraph used to say *"`chkFollowRedirect( )` rewrites the method and drops the body for a 303"*.
There is no such function. `chkPrepareNextHop( )` (`ClientSession.h:1290`) drops the body when the
policy says so — `m_next.body( om::ObjPtrCopyable< data::DataBlock >( ) )` (`:1348-1351`, lane
numbering) — and touches the headers only to drop credentials on a cross-origin hop
(`:1339-1342`). The verdict is `RedirectPolicy::rewriteMethod( )`'s (`RedirectPolicy.h:314-347`),
which sets `dropBody` for a 303 on any method but GET and HEAD **and for a 301 or 302 on POST**, so
the trigger is three status codes and not one. A POST with an explicit `Content-Length` therefore
becomes a GET that still declares one, and the h2 driver sends END_STREAM with no DATA. RFC 9113
§8.1.1 makes that malformed. On h1 the same redirect is harmless, because `serializeRequestHead( )`
removes the field.

**Change — normalize in `toSessionRequest( )`, reject only what normalization would silently
misrepresent.** It is static and pure and runs on the caller's thread, which is where h1 does the
equivalent work, and it keeps the engine role-neutral for the test peer and any future server.

1. Remove the connection-specific fields of RFC 9113 §8.2.2: `connection`, `keep-alive`,
   `proxy-connection`, `transfer-encoding`, `upgrade`.
2. `te`: keep it only when its value is exactly `trailers`; otherwise remove it. §8.2.2 permits no
   other value.
3. `content-length`: remove it when the request has no body; set it from the block when the body is
   a `DataBlock`; leave the caller's value when the body is a `BodySource`, whose length this layer
   does not know and whose framing claim is therefore the caller's.
4. `host`: drop it when it matches the URL authority case-insensitively — §8.3.1 says a client
   generating HTTP/2 uses `:authority` — and **reject** when it disagrees, because dropping a
   disagreeing `Host` would silently change which origin the request claims.
   *Precision, 2026-09-22 review — proposal.* "Matches" is a comparison of authorities, not of
   strings. `:authority` is `request.url( ).authority( )`, which is the host plus `:port` only when
   the URL spells a port (`Uri.h:1229-1239`), and h1's `hostHeaderValue( )` is the same string
   (`Http1Codec.h:1091-1094`). A caller who writes `Host: example.com:443` against
   `https://example.com/` names the same origin and must not be rejected. Parse the caller's value
   as `uri-host [ ":" port ]` (RFC 9110 §7.2), fold the host's case, default a missing port from the
   scheme on both sides, and compare the pair.

**Why normalize rather than reject.** A `Connection: keep-alive` from a caller is legal
protocol-neutral input; rejecting it would make one `ClientRequest` succeed over h1 and fail over h2,
which is the difference this client exists to hide. §8.2.2 says an intermediary translating h1 to h2
MUST remove these fields, so removing is the specified behaviour and not a leniency.

**One arm astra implies that must NOT be written.** Pseudo-header injection through the caller's
header list is impossible: `http::HeaderList` validates every name as `1*tchar` and `':'` is not a
token character, which its own comment states and the validator enforces (`HeaderList.h:60-82`,
`:286-296`). A check for caller-supplied `:method` and friends would be dead code. Recorded so that a
lane does not add it.

**What could go wrong.** Item 4 is the only arm that can fail a request that works today. A caller
who sets `Host` deliberately to something other than the URL host is doing virtual-host routing that
h2 expresses through `:authority`; failing them loudly beats sending a request whose two authorities
disagree. Item 3's `BodySource` limb is a deliberate non-answer and is the one place this fix is
weaker than h1.

**Test.** Driver-level, pure: `toSessionRequest( )` is static. Assert each removal, the `te` split,
the `content-length` removal for a bodyless request, and the `Host` rejection. Then one end-to-end
case in the session tests: POST with `Content-Length` → 303 → assert the h2 request carries no
`content-length`. **Red before** on every one.

---

## 7. H18 — require a non-ACK SETTINGS as the peer's first frame

**Defect.** `handleFrame( )` dispatches on the frame type with no notion of a first frame
(`Session.h:1710-1795`), and `feed( )` consumes the client preface string only in the server role
(`:714-718`). A peer may ACK our settings and send response HEADERS without ever sending its own.

**Change.** A `bool m_peerPrefaceSettingsSeen` and a gate at the top of `handleFrame( )`, **before**
the `frame.streamErrorCode` arm: a first frame that is not SETTINGS, or is SETTINGS with the ACK flag
set, is a connection error `PROTOCOL_ERROR` (RFC 9113 §3.4); otherwise the flag is set and the
dispatch proceeds.

**Why before the stream-error arm.** That arm exists so that a malformed frame does not desynchronize
the connection. The preface rule outranks it: a peer whose first frame is a malformed HEADERS has
already broken the preface, and answering a stream error would leave the gate armed for the *next*
frame, judging the wrong one. Stated because the two orders give different behaviour and the choice
is not obvious.

**Boundaries checked at the source, because this rule can reject our own peers.**

- Our own sessions always send SETTINGS first: `queueOpeningFrames( )` calls `applyLocalSettings( )`,
  which serializes the SETTINGS frame into the control queue, **before** the optional connection
  WINDOW_UPDATE (`Session.h:1342-1405`). That holds in both roles, so the library's own test server
  (`Http2TestServer.h:1675-1684`) satisfies the client's new rule and vice versa.
- **The survey was redone per `Session` object, on `lazari2`, by the 2026-09-22 review**, because
  the bullet it replaces — *"fourteen of sixteen call `settle( )` … the two that do not —
  `Session_HpackDecoderCeilingTests` and `Session_LocalSettingsTakeEffectOnAckTests` — feed a
  SETTINGS frame directly"* — was wrong on both counts: `TestSession.h` has nineteen cases, and those
  two call `settle( )` as well, in other blocks. Per object: every session that is fed anything is
  fed a non-ACK SETTINGS first, through `settle( )` (`TestSession.h:609-619` — SETTINGS, then the
  ACK) or directly (`:1127`, `:1235`, `:1289`, `:3481`, `:3541`, `:3566`, `:3599`); the sessions at
  `:644`, `:686`, `:1103`, `:1377`, `:1404`, `:1457` and `:1472` are constructed and never fed; and
  the server-role session at `:749` is fed an HTTP/1.1 request line, which `consumePreface( )`
  refuses before any frame exists. Expected fallout in `utf_baselib_h2core`: **zero**, now as a fact
  about objects rather than cases — subject only to the compiler.
- **The driver modules are not exempt, and were checked too.** Every driver test speaks to one of
  two peers. `Http2TestServer` gates *all* of its writes behind `m_isWriteAllowed` until its opening
  SETTINGS is released (`Http2TestServer.h:814-821`, `:887`, `:1680-1684`), so a peer told to delay
  its opening sends nothing before it, which §3.4 allows. `RawFrameScriptPeer` writes whatever
  octets a script gives it, and its three scripts all `.send( )` an empty non-ACK SETTINGS first
  (`TestHttp2ConnectionTask.h:922-933`, `:1023-1033`; `TestHttp2TestPeer.h:932-947`). A future raw
  script that opens with anything else is refused by the client under test, which is the rule
  working: that peer's comment says it "sends what a conforming one cannot", and this is one more
  thing it can send.

**What could go wrong.** A peer that opens with an extension frame is now refused. §3.4 makes SETTINGS
the first frame the server sends, so that peer is wrong, but it is a real-world risk and it is the
reason this is a design decision rather than a mechanical fix.

**Test.** Four rejections — ACK, DATA, PING, HEADERS as the first frame — plus one control where a
legal SETTINGS is coalesced with following traffic in one `feedText( )`. All in `utf_baselib_h2core`,
all red before.

---

## 8. H03b — the pool's maintenance timer, where the h2 fix's shape is unavailable

**Defect, verified.** `armMaintenance( )` calls `expires_from_now( )` and `async_wait( )` on
`m_maintenanceTimer` (`ConnectionPool.h:1898-1914`) from `runActions( )` (`:1771-1774`), which runs
**outside** the pool lock by the leaf-lock rule. `disposeInternal( )` calls `cancel( )` on the same
timer (`:2096-2100`), also outside the lock. Two threads inside one `basic_deadline_timer` is the
documented "Shared objects: Unsafe", and it is the same byte the h2 driver's race was caught on.

**Two things make it narrower than the h2 case, and both were read.** The timer object is created
once in the constructor (`:845-851`) and **never reassigned**, so there is no use-after-free from a
re-arm destroying the object under a concurrent `cancel( )` — which was the worse half of the h2
hazard. And two concurrent `armMaintenance( )` calls are impossible: `m_isMaintenanceArmed` is set
and cleared only under the pool lock (`:1681-1683`, `:1942`, `:1975`), so at most one arm is ever
outstanding. The race is **arm against dispose-cancel**, and nothing else.

**And the outcome can be bounded rather than called "benign".** A lost `cancel( )` leaves the timer
armed; when it fires, `onMaintenance( )` takes the lock, sees `m_isDisposed`, clears the flag and
returns (`:1938-1945`). The handler holds `self_ref_t::acquireRef( this )`, so the cost is that the
pool's destruction is delayed by at most one interval —
`MAX_MAINTENANCE_INTERVAL_IN_MILLISECONDS = 250` (`:211`). Two hundred and fifty milliseconds, no
crash. The verification record's "real UB, benign outcome" is right; this is the number behind it.

**Why the h2 fix's shape is unavailable.** That race was closed by posting the off-strand caller to
the strand (`http2-driver-timer-cancel-cross-thread-race-record.md` §7). The pool has no strand: its
timer is constructed on `ThreadPoolDefault::getDefault( ThreadPoolId::GeneralPurpose ) -> aioService( )`
(`:845-850`), a bare io_service, and a post to an io_service is not a serialization. Giving the pool
a strand is possible and is **rejected**, for a reason the h2 record itself states in another form:
`disposeInternal( )` is also called from the destructor (`:852-859`), and a posted handler there
would bind `acquireRef( this )` on an object whose count has already reached zero. The h2 record's
own rule — "posting it on a termination path is wrong, because a handler posted there may never run"
— applies twice over here.

**Change — a dedicated leaf mutex, and a flag that makes disposal final.**

    mutable os::mutex   m_timerLock;      // leaf; nothing is called while it is held
    bool                m_isTimerDisposed = false;

`armMaintenance( )` takes it, returns at once if `m_isTimerDisposed`, and otherwise arms.
`disposeInternal( )` takes it, sets `m_isTimerDisposed = true` and cancels. Both call sites already
run with no other lock held, so the new mutex is a pure leaf and closes no cycle. The flag also
removes the post-dispose re-arm astra names as "work can also be scheduled after the disposal sweep",
which the mutex alone would not.

**Alternative rejected: take the pool lock around the timer calls.** The timer functions do not
re-enter the pool — asio never invokes a completion handler from inside the initiating call, and
`cancel( )` posts rather than calls — so it would not deadlock. It would still widen the documented
leaf lock to cover an asio service mutex for no benefit over a four-line lock of its own, and the
pool's lock discipline is the one comment in that file most likely to be read as a contract.

**Test.** None that is deterministic. Justified by construction, and corroborated — not proved — by
the TSan run S6R.1 already requires over the pool module with a racing `dispose( )`; the recipe is in
the timer-cancel record §5 and the mandatory positive control (`utf_baselib_basictask`, the known
report at `TestBaselibBasicTask.h:127`, exit 66) is in its §1. A clean run is not the proof.

---

## 9. N1 — the h1 body cap must not apply to a streamed response

**Defect, verified on both sides.** `Http1ResponseLimits::maxBodySize` defaults to
`DEFAULT_MAX_BODY_SIZE = 1ULL << 26` (`Http1Codec.h:212, 216-222`) and is handed to the Beast
backend, which sets it as `body_limit` (`detail/Http1CodecBeastImpl.h:179`), for **every** response —
the driver knows nothing about whether a `BodySink` is installed. The request task's own cap is on
the other side of an early return: `applyData( )` returns as soon as `m_bodySink` is set
(`HttpClientRequestTask.h:888-902`), so `maxResponseBodySize` governs the buffered path only. The
design's limits table scopes its 64 MB to "Response body, buffered mode"
(`notes/plans/http2-design.md:748`). The h2 engine has no equivalent cap at all. So a streamed
download above 64 MB fails on HTTP/1.1 and succeeds on HTTP/2, and `http1Limits` is per session
(`ClientSession.h:78`, `:1558`), never per request.

**Change — remove the cap from the layer that cannot see the buffering, not plumb the buffering down
to it.** `Http1ResponseLimits::maxBodySize` defaults to **no limit**, and the codec passes
`boost::none` to Beast's `body_limit( boost::optional< std::uint64_t > )`
(`basic_parser.hpp:299-302`) when it is unset. The field stays as a knob a session may still set.

*Representation — proposal by the 2026-09-22 review.* The field is a
`ScalarTypeIniter< std::uint64_t >` (`Http1Codec.h:216`) and "unset" has no spelling on it. The
smaller change keeps the type, defaults it to `std::numeric_limits< std::uint64_t >::max( )`, and
passes the value through unchanged: Beast's check is `n > *body_limit_`, which the maximum never
trips, so there is no `boost::none` branch and no sentinel with a second meaning. Zero as a sentinel
is the alternative, and zero is a value a caller could mean. Either shape satisfies this section;
the constructor's default is the change.

**Why that is the right layer, and why the obvious shape was rejected.** The first draft of this
section said the driver should tell the codec whether a sink is installed, "because the request
carries the sink decision". **That premise is false, and checking it is what killed the shape**:
`BodySink` is a constructor parameter of `HttpClientRequestTaskT` (`HttpClientRequestTask.h:292`,
`:347`) and of the session's request task (`ClientSession.h:959`). It is not on `ClientRequest`, it
is not on `ClientConnection::submit( )`, and it never reaches a driver at all — the driver delivers
every body chunk through `sink -> onData( )` and has no idea what the other end does with it.
Plumbing it down would mean a field on `ClientRequest` describing the *response*, or a change to the
frozen S2.6 contract.

None of that is needed, because the cap already exists at the layer that knows:
`HttpClientRequestConfig::maxResponseBodySize`, default `DEFAULT_MAX_RESPONSE_BODY_SIZE` = 64 MB,
whose own comment says "The cap on a buffered response body - design 4.6, 64 MB, matching
SimpleHttpTask.h:83" (`HttpClientRequestTask.h:76-89`). That is the *same number* and the *same
design citation* as the h1 codec's. The h1 cap is a duplicate of it applied one layer too low, and
removing it leaves the buffered path capped at 64 MB exactly as before and the streamed path
uncapped — which is precisely what h2 does and what the design's table says.

**What this removes, stated exactly.** A bound on the *transfer*, not on memory. Buffered memory
stays bounded by the request task's cap; streamed memory was never bounded by `body_limit` anyway,
since the driver hands each chunk on and clears `m_bodyChunk`. A caller using `Http1ResponseParser`
directly — the driver and the codec tests — loses the default bound and can set one.

**And a default that changes is a visible behaviour change**, so it is called out rather than
slipped in: a session that relied on `http1Limits.maxBodySize` to stop a runaway h1 transfer now
relies on `maxResponseBodySize` for the buffered case and on its own total timeout for the streamed
one.

**What could go wrong, and one thing that does not hold for h1.** An h1 peer may now stream without
end, bounded only by the request's total timeout (`ConnectionPoolPolicy::requestTimeout`, 30 minutes
by default). It would be comfortable to add "and the sink applies backpressure through `consumed( )`"
— **and that is false on h1**: this driver's `consumed( )` is deliberately a no-op, and its comment
says so in as many words, "Nothing to credit - HTTP/1.1 has no flow control window ... What
backpressure there is over HTTP/1.1 is TCP's own" (`Http1ConnectionTask.h:1580-1606`). A sink that
takes nothing therefore accumulates in the request task's `m_pendingDownload` over h1 while the read
loop keeps reading. That is pre-existing, it is H06's territory rather than N1's, and it is written
here so that this fix is not later credited with a bound it does not provide.

**Where this sits.** The verification record files N1 under "belongs with the sink-semantics work",
which is H06 and H08 in R3. It is independent of both: it changes which limit is applied, not what
partial consumption means. Doing it here is correct, and it should be said that it does **not**
pre-empt R3's decisions.

**Test.** Two cases, and they must be a pair. In the HTTP/1.1 driver module §14 names, with the
scripted peer, a response body above 64 MB is not something to transfer in a test — so the *driver*
case sets a small `http1Limits.maxBodySize` explicitly and asserts it is still honoured, which pins
that the knob survives — a pin the codec tests already carry twice (`TestHttp1Codec.h:1198`,
`:1227`, limits of 8 and 5 asserting `BodyTooLarge`), so the driver-level copy is optional and,
given §14's size note, better omitted (2026-09-22 review). The *agreement* case belongs one layer up, in the request-task module: with a body
sink installed, a response larger than `maxResponseBodySize` must complete over both protocols, and
with no sink it must fail with `BufferTooSmallException` over both. **Red before**: the h1 arm of the
sink case fails today with the codec's `body_limit` error while the h2 arm succeeds, which is the
disagreement stated as an assertion.

---

## 10. N2 — ask `net::` instead of comparing, and the predicate is a decision

**Breach, verified.** `onReadCompleted( )` classifies the end of the conversation as
`asio::error::eof == ec || base_type::isStreamTruncationError( ec )`
(`Http1ConnectionTask.h:1002-1003`). `windows-peer-close-error-codes-record.md` states the rule in
terms: do not compare an asio transport error code by hand; ask `net::isPeerClosedErrorCode( )` or
`net::isOrderlyPeerCloseErrorCode( )`. The h2 driver complies, in a function whose comment says why
(`Http2ConnectionTask.h:1477-1501`).

**What the breach costs today.** On Windows a peer close during a full-duplex transfer is reported as
`connection_aborted` and a close with unread data as `connection_reset`. Neither is `eof`, so
`isEndOfStream` is false, `BL_TASKS_HANDLER_CHK_EC( ec )` fires and the task **fails** a connection
the peer closed normally. That is the same ~1-in-8 Windows defect the h2 driver had before
`connection_aborted` was added to `net::`.

**And the fix is not a one-line swap, because the predicate chosen has a second consequence.** See
§11: whichever predicate `onReadCompleted( )` asks, everything it admits goes to `onPeerClosed( )`,
and `onPeerClosed( )` is what can **complete a close-delimited message**. Admitting the Windows reset
spellings therefore turns an aborted transfer into a successful short response for exactly the class
of message HTTP/1.1 cannot frame any other way.

**Change — two parts.**

(a) **Classification.** `onReadCompleted( )` asks
    `net::isPeerClosedErrorCode( ec ) || base_type::isStreamTruncationError( ec )` — the h2 driver's
    exact expression. The conversation is over however it ended, and failing the task is the wrong
    answer to a peer that went away.

(b) **Completion.** `onPeerClosed( )` may complete a `needsEof( )` message only when the byte stream
    ended *cleanly*. Neither existing predicate answers that question:
    `isOrderlyPeerCloseErrorCode( )` admits the reset spellings on Windows by design — the record
    says that is kept deliberately, for the handshake retry — and admitting them here is the
    opposite of what this call site needs. So this is the case the record anticipates: **add a third
    predicate in `core/NetUtils.h` with its reasoning**, rather than open-coding a comparison at the
    call site. Proposed: `net::isCleanEndOfStreamErrorCode( )`, admitting `eof` on both platforms,
    with the reasoning that a Windows reset means either that data was lost or that the connection
    was aborted, and a close-delimited body may not be declared complete on either.

    With (b), `onPeerClosed( )` refuses a `needsEof( )` message closed by a reset —
    `protocol_error`, the same answer a truncated Content-Length body already gets — while a message
    framed by Content-Length or chunking is unaffected, because Beast decides those.

    **The truncation arm — a gap the 2026-09-22 review found, resolved here as a proposal.** (a)
    keeps `base_type::isStreamTruncationError( ec )`, and (b) as first written said nothing about
    it. Today a TLS stream that ends without `close_notify` — `stream_truncated`, which is
    `isExpectedSslErrorCode( )` under the TLS policy (`TcpSslBaseTasks.h:342-345`) and never true
    under the plain one (`TcpBaseTasks.h:545-550`) — reaches `onPeerClosed( )` and *completes* a
    close-delimited message. A (b) that admitted `eof` alone would turn that into a failure for
    every close-delimited HTTPS response from a server that skips `close_notify`, which RFC 2818
    §2.2.2 would bless and which nothing in this design set out to change. So the completion
    predicate is `net::isCleanEndOfStreamErrorCode( ec ) || base_type::isStreamTruncationError( ec )`
    — the same two-part shape as (a) — on the reasoning the record already gives in its own table,
    where the truncation spellings sit in the row "Orderly close of a TLS stream". The strict
    alternative is recorded as not taken, not as wrong.

    **The error code for an unclean end.** This section said the answer is *"`protocol_error`, the
    same answer a truncated Content-Length body already gets"*. It is not: Beast's
    `partial_message` passes through `parseEof( )` unchanged — `classifyBackendError( )` records
    the codec reason and returns with `ec` as Beast set it (`Http1Codec.h:554-560`). More to the
    point, (a) makes a POSIX reset end the connection task *cleanly*, so `connectionFailureCause( )`
    in the request task (`HttpClientRequestTask.h:1186-1209`) finds no exception to chain and the
    caller would lose "connection reset by peer" from the diagnostic. Proposal: on an unclean end
    `onPeerClosed( )` does not call `parseEof( )` at all and finishes the stream with the
    transport's own code — `connection_reset` or `connection_aborted` — which is what the caller saw
    before and what the request's `errinfo_error_code` should keep saying. Retryability is
    unaffected either way: `outcomeOnClosed( )` reads `isConnectionLost` from the connection's
    published state and not from the code (`:1098-1111`), and `Draining` is published before
    `onClosed( )` on every path.

**What could go wrong, and what cannot be checked here.** The premise behind the third predicate's
Windows column is that an *orderly* server close after a close-delimited body arrives as `eof` on
Windows, because by then nothing of ours is left unread — the mechanism the record measured requires
unread data. That is an **inference about a platform this machine cannot run**. If it is wrong, (b)
turns ordinary Windows close-delimited responses into failures, which is worse than the defect.

**So N2's acceptance needs the Windows matrix, and this design says so rather than pretending
otherwise.** The record is explicit: *"A Linux-only run cannot catch a breach of this rule"*, and a
Windows run may need dozens of iterations because the mechanism is a race. What a Linux run **can**
check is the predicate itself, all of it: the third predicate admits the same set on both platforms,
so unlike the two existing ones it has no arm a Linux run cannot reach. Assert both platform arms of
every row anyway, the way `TlsHandshakeRetryClassifier_RetryableErrorSetTests` does, so the day a
platform arm is added the test already has a place for it. What a Linux run cannot check is the
*premise* above — that an orderly Windows close arrives as `eof` — and no unit test of the predicate
touches that. (Corrected by the 2026-09-22 review: the sentence this replaces called the POSIX reset
"the only arm that discriminates", which is true of the two existing predicates and not of this one.)

**Test.** A `net::` unit case for the third predicate asserting both arms of every code; and a driver
case against the scripted peer where the peer aborts mid-body on a close-delimited response,
asserting a failure rather than a short success. The second is red before **and** after (a) alone,
which is what pins (b) and is the only reason (a) and (b) ship together.

---

## 11. N3 — settled: the success half does not happen, and fixing N2 naively would create it

The question §5a leaves open is whether a truncated body can be reported as success. Commit
`2b4c61b` measured the byte loss and its message *infers* the success half; the record says the
inference is not established and that `closeAllStreamsUnwrittenRetryable( )` argues against it.
**Settled by reading, end to end, both drivers and the request task.**

**On HTTP/2 it does not happen.** `onRead( )` sends a peer close to `onPeerClosed( )`
(`Http2ConnectionTask.h:1504-1520`), which calls
`closeAllStreamsUnwrittenRetryable( connection_aborted )` (`:1557-1571`). That walks every live
stream and calls `closeStream( handle, errorCode, isRetryable )` (`:1181-1197`), which delivers
`sink -> onClosed( handle, errorCode, isRetryable )` with a **non-empty** code (`:1140-1145`). In the
request task, `answerOnClosed( )` turns any non-empty code into `failWith( ... "The HTTP request
failed" )` (`HttpClientRequestTask.h:1243-1250`) and `outcomeOnClosed( )` never returns `Completed`
for one (`:1098-1111`). A stream that is no longer live cannot be truncated, because the only way it
left `m_streams` is the END_STREAM that the completeness check at `Session.h:2188` is gated on. **The
commit message's inference is wrong for the driver it names**, and the link the record suspected is
the reason. (*Precision, 2026-09-22 review:* the commit's sentence — "a response body can be short
while the task reports success" — is true of the h2 **connection** task, which `onPeerClosed( )` ends
through `beginClose( )` and which therefore completes clean; the record's §5a read "the task" as the
request, and it is that reading this section refutes.)

**On HTTP/1.1 it can happen, but only where the protocol cannot tell.** `onPeerClosed( )` calls
`parseEof( )` and then `finishStream( m_parser -> isComplete( ) ? eh::error_code( ) : protocol_error,
... )` (`Http1ConnectionTask.h:955-989`) — a success when the parser says complete.
`Http1ResponseParser::parseEof( )` delegates to Beast's `put_eof( )`, and that function was opened in
the exact Boost the build uses
(`.../boost/1.90.0/source-linux/boost/beast/http/impl/basic_parser.ipp`):

- first, `BOOST_ASSERT( got_some( ) )` — a parser that has seen **no octet** aborts a debug build
  here and, in release, falls through both guards below (`basic_parser.ipp:202-224`; `got_some( )`
  is `state_ != state::nothing_yet`, `basic_parser.hpp:166-171`). *This arm was missing from the
  list as first written, and it is the zero-octet shape S6R.1's implementation review handed to
  this slice — §11a. Added by the 2026-09-22 review;*
- still in `start_line` or `fields` → `partial_message`;
- `flagContentLength` or `flagChunked` set and not complete → `partial_message`;
- otherwise → `state_ = complete`, no error.

So a Content-Length or chunked body cut short becomes a **failure**, and a close-delimited body is
completed with whatever arrived. That is not a defect in this library: a close-delimited message is
*defined* to end when the connection closes, and HTTP/1.1 gives a client nothing to compare against.

**And today, on Windows, even that is unreachable — because of N2's breach.** The reset spellings
never satisfy `asio::error::eof == ec`, so `onReadCompleted( )` never calls `onPeerClosed( )` for
them; it fails the task instead. The 16 KB the commit measured as lost therefore surfaces to the
caller as a **failure**, not as a short success.

**The finding is prospective, and it is a constraint on N2.** Fix N2 with either existing predicate
and the Windows reset spellings start reaching `onPeerClosed( )` — where a close-delimited response
aborted mid-body is completed as a success, with, on that platform, none of the unread bytes
delivered at all. **N2's naive fix creates exactly the defect N3 asked about.** That is why §10 is two
parts and not one, and why its second part needs a third predicate rather than either existing one.

**What remains unestablished, stated as such.** Whether an orderly Windows close of a close-delimited
response really arrives as `eof` — §10's premise — is an inference. It cannot be settled here.

### 11a. The zero-octet close — the third HTTP/1.1 case, and S6R.1's obligation discharged here

Added by the 2026-09-22 review, because the design as first written did not carry it and S6R.1's
implementation review (`s6r1-design.md` §11b.3) says in terms that S6R.2's design "cannot start
without" it.

**The defect, verified at the source.** `onPeerClosed( )` returns only for a null parser, and the
parser exists from `onStartRequest( )` until `finishStream( )`. A peer that reads the request and
closes without writing one octet — a server dropping a request it will not serve, or the stale
keep-alive race H01 and H02 exist for — reaches `parseEof( )` → `putEof( )` → `put_eof( )` on a
parser in `state::nothing_yet`. `putEof( )` guards only on `is_done( )`
(`Http1CodecBeastImpl.h:208-216`). In a debug build the assert aborts the module; in release
`put_eof( )` sets `state_ = complete`, `parseEof( )` sees `isDone( )` with `m_statusCode` still 0
and `isInterimStatus( 0 )` false and marks the message complete (`Http1Codec.h:563-566`),
`deliverHeaders( )` finds `isHeaderComplete( )` true — Beast's `is_header_done( )` is
`state_ > fields`, and `complete` is — and delivers `onHeaders( handle, 0, {} )`, and
`finishStream( )` is reached with an empty code. **The caller receives a status-0, header-less
success.** It is not close-delimited framing and it is not a Windows matter: it happens on `eof`,
on every platform, and it is the case §11's "only where the protocol cannot tell" was short by.

**The same defect after an interim.** `fileInterimAndRestart( )` discards the backend and makes a
fresh one (`:694-716`), which has also seen no octet, and resets `m_statusCode` to 0. A peer that
sends `103 Early Hints` and then closes takes exactly the path above — an abort in debug, a
status-0 success in release — although the parser as a whole has seen a whole message.

**Change — refuse in `parseEof( )`, on the current backend.** The backend gains `gotSome( )`,
forwarding Beast's public `got_some( )`; `parseEof( )` asks it before `putEof( )` and, when it is
false, refuses through the existing `fail( )` path and returns. Because the check is on the
*current* backend, the interim case is covered by the same line. `deliverHeaders( )` then finds
`isHeaderComplete( )` false on `nothing_yet` and delivers nothing; `onPeerClosed( )` finishes the
stream as a failure carrying `! m_requestMayHaveBeenSent` — after §1a, exact once the write has
settled and conservative while it is pending — with `isConnectionUsable = false`. A retry is
therefore the pool's decision under its policy, as for every other "the connection died" answer,
which is what S6R.1's review said the flag's name and placement were load-bearing for.

*Which `Http1CodecError` — proposal.* A new value, proposed `NoResponse` — "the connection closed
before a single octet of the status line arrived" — rather than `MalformedMessage`, whose comment
covers "a truncated message at end of stream" and would let a zero-octet close and a cut-short
status line assert alike. Either is a failure; the value is what a test can name.

**What could go wrong.** Nothing on the existing paths: a parser that has seen an octet answers
exactly as before, and a completed message never reaches `onPeerClosed( )` with a parser at all,
because `onBytesRead( )` resets it on completion. The one visible change is that a close after a
`1xx` with no final response now fails instead of delivering a status-0 success — a response with
no final status line is not a response.

**Test — the one that could not exist before this change.** In the HTTP/1.1 driver module §14
names: the scripted peer reads the request and closes without writing; assert that `onClosed`
carries a non-empty code, that no `onHeaders` was delivered, and `isRetryable == false` — the write
completed before the close, so the request is not provably unsent. A second case sends `103 Early
Hints` and closes. Both **abort a debug module before the change** — the red is the process dying,
not an assertion — so they land in the same commit as the conversion or after it, never before,
and the run recipe says so. S6R.1's H02 probe keeps its partial status line, and its comment's
"also the realistic shape" now has a sibling that states the other one.

---

## 12. Why these are one change-set, and where the seams are

They touch disjoint functions across ten files — `Http1ConnectionTask.h`, `Http1Codec.h`,
`detail/Http1CodecBeastImpl.h`, `HttpClientRequestTask.h`, `ConnectionPool.h`, `http2/Session.h`,
`http2/FlowControlWindow.h`, `http2/HpackEncoder.h` (reader only), `http2/Http2ConnectionTask.h`,
`http2/Globals.h`, plus `core/NetUtils.h` for N2's third predicate. Three interactions exist and
none is a conflict:

- **H01 and S6R.1's H02** share four lines in `onStartRequest( )` and `onWriteCompleted( )`. §1a
  states line by line what changes. H01 must land **after** H02, not beside it.
- **N2 and N3** are one decision and must land together; §10(a) without §10(b) is a regression, which
  §11 demonstrates.
- **H15's two parts** both touch `shouldSendWindowUpdate( )`'s neighbourhood and are one edit.

**One shared invariant, checked.** H05, H15 and S6R.1's H14 all touch per-stream receive state.
H05 adds two counters to `StreamContext` and increments them on the informational arm of
`deliverHeaderBlock( )`; H15 adds a flag inside `ReceiveFlowControlWindowT` and changes one predicate
there; H14 calls the two flush helpers at the post-registry DATA site. No two write the same field,
and H15's predicate change makes H14's threshold-gated flushes strictly more likely to emit, which is
the direction both fixes want.

**Three hazards recorded here rather than discovered during implementation:** H12 overturns a live
assertion and falsifies its comment (§4); H18 can reject our own peers if the opening-frame order
were ever changed, which is why §7 names the two functions that keep it true; and H01's barrier
depends on the read handler's own epilog to wake the write it refuses to wait for (§1(b)).

**What this change-set must also do — two amendments S6R.1's implementation review handed to it.**
The zero-octet conversion of §11a; and the in-tree comment at `Session.h:1890-1896` (`lazari2`),
which says *"if this frame carried END_STREAM, canSend( WINDOW_UPDATE ) fails and the stream flush
emits nothing"* and is true only when the END_STREAM **closes** the stream — in half-closed (remote)
`canSend( )` returns true (`StreamStateMachine.h:359-361`) and a legal, useless WINDOW_UPDATE goes
out. The review's wording is the amendment: *"and if this frame carried END_STREAM and closed the
stream, canSend( WINDOW_UPDATE ) fails and the stream flush emits nothing; on a stream whose local
half is still open it emits a legal WINDOW_UPDATE the peer will ignore"*. H15 changes the predicate
those flushes are gated on, which is why the comment rides here. (This paragraph replaces *"It must
not touch `judgeDataFrame( )` or `queueHeaderBlock( )`, which S6R.1 is editing for H17 and H13"* —
S6R.1 landed at `3bcf21e` before this design was reviewed, and this change-set lands on top of it.
Updated 2026-09-22.)

---

## 13. Where this design corrects or extends the verification record

Recorded so that the record is not treated as uniformly right, which is the standard it set for
astra.

- **H12 is pinned by a test, and the record names only H06 as pinned.**
  `TestSession.h:3254-3261` asserts the defect. §4.
- **H01's mechanism is single-threaded, not a cross-thread race.** The record repeats astra's
  framing. Under the stranded policies — which the class comment says are the only policies this
  driver is correct over — the interleaving is one strand's, which is what makes a deterministic red
  test possible at all. §1.
- **N3's success half is established, and it is established as prospective.** The record says the
  success half is not established and asks for it to be settled. It is: it does not happen on h2, it
  happens on h1 only for close-delimited messages, and today the N2 breach blocks it on Windows —
  so **fixing N2 would create it**. That connection is not in the record. §11.
- **H03b's "benign outcome" has a number: 250 ms of delayed destruction**, bounded by
  `MAX_MAINTENANCE_INTERVAL_IN_MILLISECONDS`. And the pool's timer, unlike the h2 driver's, is never
  reassigned, so the use-after-free half of the h2 hazard is absent here. §8.
- **H15 is confined to streams opened before the acknowledgement.** Streams created afterwards get a
  correct threshold for free, because `createStreamContext( )` reads the already-updated
  `m_localInitialWindowSize`. The record does not narrow it. §5.
- **One arm of H16 cannot be written.** Pseudo-header injection through a caller's `HeaderList` is
  impossible by that class's own name validation. §6.
- **S6R.1's H02 has a behavioural cost the record does not price.** With
  `retryIdempotentOnConnectionLoss` defaulting to false, the conservative rule removes the automatic
  retry of a reset idle pooled connection for every method. §1a.
- **N3 has a third HTTP/1.1 case, and it is neither close-delimited nor Windows-only.** A close
  before any response octet — or after a 1xx with no final response — is a status-0 success in
  release and an abort in debug, on `eof`, on every platform. Neither the record nor this design as
  first written carried it; S6R.1's implementation review found it, and §11a is where it is fixed.
  Added 2026-09-22.

---

## 14. Acceptance

- Focused modules, clang debug, in the lane; **one module at a time**. The modules this touches:
  `utf_baselib_h2core` (H05 h2 half, H12, H15, H16 engine half, H18); `utf_baselib_h2client2`
  (H16's driver half — `toSessionRequest( )` is `Http2ConnectionTaskT`'s static); `utf_baselib_http2`
  (N2's `net::` unit case, beside `TestPeerCloseErrorCodes.h`); `utf_baselib_httpclient` (H07, the
  request-task module); `utf_baselib_h2client4` (H03b, the pool module); and for the HTTP/1.1 driver
  cases **a new `utf_baselib_httpclient7`, not `utf_baselib_httpclient3`**.
- **The size rule — corrected 2026-09-22.** This list used to send H01, N1 and N2 to
  `utf_baselib_httpclient3`. That module was 40.3 MB on a64 debug after S6R.1 (`6fe658d`'s own
  message), which is over the 40 MB target, and `src/utests/AGENTS.md` is explicit: *"If the module
  you were going to use is at or near the 40MB target, do not add to it … create a numbered
  sibling."* This design puts six cases there — H01's two, N1's (optional), N2's, and §11a's two —
  so the sibling is not optional. The sibling reaches `ScriptedPeer` and the driver typedefs by
  moving them into a shared header under `src/utests/include/utests/baselib/` (proposed
  `Http1DriverTestUtils.h`, beside `Http2DriverTestUtils.h`), never by including
  `utf_baselib_httpclient3`'s header across the module boundary — the rule in that file — and
  `utf_baselib_httpclient3`'s `notes.txt` recipes stay where their cases are. `utf_baselib_h2core`
  was 36.9 MB on the gcc a64 debug object recorded in `l1-gcc-toolchain-coverage-record.md` before
  S6R.1 added three cases to it; this design adds ten. The lane reads the headroom line as the
  module links **before** adding, and if it is at or near target the new cases go to
  `utf_baselib_h2core2` under the same checklist; `utf_inventory.py --compare` is run either way.
- Then clang **and** gcc release plus the whole-suite G1 gate, by the orchestrator.
- Every fix that claims a test above must ship it, shown **red before and green after**. Nothing here
  may rest on "the suite still passes".
- **H03b has no deterministic test** and is justified by construction; the TSan run S6R.1 already
  requires covers it, with its positive control, and a clean run is corroboration and not proof.
- **H01 is a core-path change to the terminal path of every HTTP/1.1 connection**, and AGENTS.md
  gates such a change on the whole suite as its own tested change-set. It should be its own commit
  within this set even if the rest ride together.
- **N2 cannot be accepted on this machine.** Its Linux arm checks only that a POSIX reset is refused
  by the clean-end predicate; the rule it breaches is one a Linux-only run structurally cannot check.
  It needs the Windows matrix, repeated, because the mechanism is a race.
- **§11a's two cases abort a debug module before the conversion**, so their red is the process
  dying and not an assertion; they are committed with or after the conversion, never before, and
  the run recipe records that. (Added 2026-09-22.)

## 15. What could not be settled here

1. **N2's Windows behaviour** — §10 and §11. Needs the matrix.
2. **H12's amended test** — the raise from 4096 to 16384 must be shown in the produced bytes, which
   needs a build.
3. **H18's test survey** — now per `Session` object and per driver peer (§7, 2026-09-22); a compiler
   is still what turns "expected fallout: zero" into a fact.
4. **H01's buffer sizes** — the test's determinism rests on `SO_RCVBUF`/`SO_SNDBUF` being settable on
   those sockets in that harness. That was reasoned about, not tried.
5. **Whether H01's forced non-reuse ever fires on an ordinary keep-alive exchange.** The ordering
   argument in §1 says it cannot; only a run of `Http1Driver_RequestResponseAndKeepAliveReuseTests`
   after the change turns that argument into evidence, and it is the first thing to look at if that
   case becomes flaky.
6. **The sizes of `utf_baselib_h2core` and the new `utf_baselib_httpclient7` once the cases are in**
   — §14; only the link line answers it. (Added 2026-09-22.)
7. **Whether the decode outcome carries the decoded-list size H05 wants** — §3; a compiler question.
   (Added 2026-09-22.)

## 16. Deliberately out of scope

H06, H08, H11, H09, H10, H04a (S6R.3, decisions first); H19, H20, H23, H29 (S6R.4); H24, H25
(deferred to the decoder programme); H21, H22 (on L6's owed list). H02, H03a, H13, H14, H17, H26,
H04b, H27, H28 are S6R.1's and landed at `3bcf21e`.

---

## 17. Design review, 2026-09-22

**Reviewer: Claude Fable 5.1, on `s6r2-design` @ `48f4739` in this worktree, read against
`lazari2` @ `a36b04e` — the tree this change-set will actually land on, which is thirty-three lines
longer in `Http1ConnectionTask.h` than the one every citation above was taken from.** Every function
named in §1, §2, §8, §10 and §11 was opened at its signature and read to its end on `lazari2`; the h2
engine, the receive window, the encoder, the pool's timer and retry rule, the codec's EOF path,
`MultiOperationTaskT`'s accounting, the handler macros, `HeaderList`'s name rule, the redirect
policy, the request task's `cancelStream( )` and `outcomeOnClosed( )`, the h2 test server's write
gate and the three raw-frame scripts were read for the claim each section rests on; Beast's
`put_eof( )`, `got_some( )` and `body_limit( )` and asio's `write_op` were opened in the dist the
build uses. Nothing was built or run.

**Verdict: not agreed as first written; agreed with the corrections written in place above, each
dated 2026-09-22.** The reasoning the design carries is sound on every item; what it lacked was
completeness, on three counts:

1. **Two of S6R.1's three recorded obligations were undischarged** (`s6r1-design.md` §11b.3,
   `a36b04e`). The zero-octet close — a virgin parser reaching `put_eof( )`, an abort in debug and a
   status-0 success in release — was absent, and §11's own summary of `put_eof( )` omitted its first
   line, `BOOST_ASSERT( got_some( ) )`, which is the arm that carries it. The `canSend( WINDOW_UPDATE )`
   tree comment was absent too. §11a and §12 now carry both; §13 records the third h1 case. The miss
   has the shape S6R.1's had — an enumeration of arms read past the line above them — and the
   reviewer notes that the caller's own check of `put_eof( )` stopped at the same two arms.
2. **N2(b) was silent on the truncation arm.** (a) keeps `isStreamTruncationError( )`; a (b) that
   admitted `eof` alone would have failed every close-delimited HTTPS response from a server that
   skips `close_notify`, which today succeeds. §10 now decides it, as a proposal, in favour of
   today's behaviour, and separately corrects the code an unclean end should carry, because (a)
   makes a POSIX reset end the task cleanly and the diagnostic chain would otherwise lose it.
3. **The module-size rule was not applied.** `utf_baselib_httpclient3` was over the 40 MB target
   before this design added six cases to it. §14 names the sibling.

**Verified and standing as written, against the source:**

- **H01's barrier cannot hang on any accounted path.** Every `finishStream( )` that can see
  `m_isWriteInFlight == true` runs inside the read handler — `onBytesRead( )` on completion or parse
  error, `onPeerClosed( )` — and `closeConnection( )` → `beginClose( )` there is followed by that
  handler's own `BL_TASKS_HANDLER_END_MULTIOP( )` → `onOperationCompleted( )`, which finds
  `m_closing && ! m_closeInitiated` (`MultiOperationTask.h:371`) and runs `initiateClose( )` →
  `getSocket( ).cancel( )`; the woken write completes `operation_aborted`, is excused as
  self-inflicted, and takes the terminal. A write whose completion was already queued when the
  barrier tripped completes clean and takes the terminal the same way. `onCancelStream( )` is the
  one unaccounted caller and §1 says so; `onStartRequest( )`'s two early returns cannot see the flag
  set, because (b) keeps a connection with a write in flight out of `Ready`, and `submit( )` gates on
  `Ready` and an empty handle under the same lock `finishStream( )` publishes under.
  `takeTerminalNoLock( )` requires `0 == m_pendingOperations` (`:147-157`), so §1a's "fully settled
  at `onTaskStoppedNothrow( )`" holds. asio's `write_op` copies the sequence (`write.hpp:317-327`),
  so the vector is not the hazard and the members are. The only readers of the two members are the
  ones §1 names; a full read of the driver found no other.
- **§1a's divergence is real.** The catch clears `m_isWriteInFlight` on the premise the catch already
  rests on — no handler is owed — and leaves `m_requestMayHaveBeenSent` on S6R.1's — a throw is not
  provably unwritten. One flag serving both would answer the second question with the first's
  premise and replay a POST from the throwing initiator. `chkRequestMayBeReplayed( )` falls to
  `isConnectionLost && retryIdempotentOnConnectionLoss && isIdempotentMethod( )`
  (`ConnectionPool.h:430-455`) and the policy constructor leaves the knob false (`:297-310`), as
  §1a says.
- **N3's settlement is right, and so is the constraint it puts on N2.** On h2 a peer close reaches
  every live stream as `onClosed( handle, connection_aborted, isRetryable )` through
  `closeStream( )` (`Http2ConnectionTask.h:1108-1149`, `:1181-1198`, `:1557-1572`),
  `answerOnClosed( )` fails any non-empty code (`HttpClientRequestTask.h:1211-1273`), and a stream
  that is not live completed on END_STREAM with its content-length closed out (`Session.h:3252-3277`).
  On h1 `put_eof( )` completes only a close-delimited message, verified in Boost 1.90. Both existing
  `net::` predicates admit the Windows reset spellings (`NetUtils.h:353-385`), so either would route a
  mid-body reset into that completion; the third predicate is necessary, not stylistic.
  `outcomeOnClosed( )` reads `isConnectionLost` from the connection's state and not from the code, so
  N2's choice of code changes diagnostics and nothing else.
- **H12 is pinned and the design handles it.** `TestSession.h:3588-3595` on `lazari2` asserts 65536
  at construction; the block's comment becomes false as §4 says; the amendment is right, and its
  control at 4096 rests on `setDynamicTableCapacity( )` refusing an update for an equal value, now
  stated in §4.
- **H03b, H07, H15 and H18 are as written.** The pool's timer is created once and never reassigned;
  `m_isMaintenanceArmed` is written only under the pool lock (`:1692-1694`, `:1953`, `:1986`); the
  race is arm against dispose-cancel and nothing else. `failWith( )`'s guard, its ten call sites and
  their eight caller-side guards are as counted. The window's threshold is set only in the
  constructor and by the two profile-gated callers. `handleFrame( )` opens with the stream-error arm
  the gate must precede, and every peer this library's tests speak to opens with SETTINGS.

**Precisions applied in place, none of them changing a decision:** where the clears go in
`onWriteCompleted( )` and that a write woken by the peer ends the task failed (§1); the landed
reality of S6R.1's lines, the three comments that must move, six fields not five (§1a); H07's test
must not assert a reset that cannot happen (§2); `fileInterimAndRestart( )` takes no `error_code`
and the decoder does not hand its measure back (§3); the equal-capacity no-op (§4); the redirect
function's real name, the 301/302 POST trigger and the authority comparison (§6); the H18 survey
redone per object and per peer, and the test server's real line (§7); N1's representation and its
existing pins (§9); the predicate's arms versus its premise (§10); the commit sentence's two readings
(§11); the stale S6R.1 sentence (§12); modules and sizes (§14, §15).

**Proposals, marked as such where they sit:** `TooManyInterimResponses` and `NoResponse` as new
codec error values (§3, §11a); the truncation arm and the transport code on an unclean end (§10);
the maximum-value default for `maxBodySize` and dropping the redundant driver-level pin (§9); the
authority-pair comparison for `Host` (§6); a shared `Http1DriverTestUtils.h` for the new sibling
(§14).

**What this review does not claim.** Nothing was compiled: the sizes in §14, the H18 fallout, and
whether the decode outcome carries the list size are the compiler's. N2's Windows premise is
exactly as unsettled as §10 says, and nothing here narrows it.

**Agreement.** With the in-place corrections above taken as part of the design, the reviewer agrees
that S6R.2 may be implemented against it — on `lazari2` at `a36b04e` or later, in the modules §14
now names, with H01 as its own commit and §11a's cases never ahead of their conversion.
