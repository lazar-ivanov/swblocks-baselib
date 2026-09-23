# `initiateClose( )` cancels but never shuts down — the design for both drivers

**Status:** design, 2026-09-23. **Nothing implemented. No build was run and nothing under `src/`
was touched.** This is the artifact that must be agreed before code is written, per the review
loop.

**Scope:** one defect with two halves, in two drivers.
`Http1ConnectionTaskT::initiateClose( )` and `Http2ConnectionTaskT::initiateClose( )` both cancel
the socket and nothing else, and `cancel( )` cannot reap a composed `asio::async_write` that is
between two of its internal steps. **(a)** is what `initiateClose( )` must do instead. **(b)** is
how the error that the fixed teardown then produces on that write is classified, without which (a)
converts a hang into a failure.

**Base.** Written against **`s6r2` @ `b100c2a`**, not against `lazari2` @ `38ed037`. S6R.2 is
implemented and not merged, and the two artifacts this design turns on live only there: the
`m_isWriteInFlight` barrier of H01, which is what makes a deliberate close with a write outstanding
reachable at all, and the red test. The *defect* is present at `38ed037` as well — `initiateClose( )`
is byte-identical on both tips — but the fix as designed reads `m_isWriteInFlight` and therefore
sequences **after** S6R.2.

**What this design is not.** It is not a restatement of the finding — `lane2.md`'s "S6R.2 follow-up"
section and the brief carry that. It is the intended change, the reason each shape was chosen over
the alternatives that were considered and rejected, and what else each shape moves.

**Provenance — read this before weighing any claim below.**

*Read at the source by the author, in the versions named:* `MultiOperationTask.h` in full;
`Http1ConnectionTask.h` in full at both tips; `Http2ConnectionTask.h`'s read loop, write pump,
close paths, timers and task overrides; `TcpBaseTasks.h`'s `shutdownSocket( )`,
`isExpectedSocketException( )`, `cancelTask( )` and the two `onTaskStoppedNothrow( )`;
`TcpSslBaseTasks.h`'s `scheduleTaskFinishContinuation( )`, `onShutdownCompleted( )`,
`isExpectedException( )` and `isShutdownNeeded( )`; `TcpStrandedStreams.h` and
`TcpSslStrandedStreams.h`'s `cancelTask( )` and `shutdownSocketOnStrand( )`; `NetUtils.h`'s three
predicates at both tips; `TaskBase.h`'s handler macros, `scheduleNothrow( )` and `isFailed( )`;
`Http1Codec.h`'s `parseEof( )` at `b100c2a`; `TestHttp1DriverWriteBarrier.h` in full;
`notes/plans/issues/multioperation-deliberate-close-fails-task-record.md`; and
**`boost/asio/impl/write.hpp` at Boost 1.84.0**, the tree at
`/home/lazar/dev/github/boost/boost_1_84_0`.

*Taken from the lane's report and NOT reproduced here, because this design ran no builds:* every
measured number — the `Broken pipe [system:32 at reactive_socket_send_op.hpp:136]` exception text,
the bimodal 5 ms / 30000 ms outcome, "6 of 8 still running after 500 ms, every one freed in 0 ms by
`requestCancel( )`", and "5 of 10 debug runs red". §12 says which of these the design depends on and
which it does not.

*One version caveat that a reviewer must weigh:* the Boost tree read here is **1.84.0**; the dist
this project builds against is **1.90.0** (s6r1 §11b.2 established that). `write_op` has had the
shape below since long before 1.84 and the design does not depend on a detail that moved, but the
exact file was not read in the version that ships.

---

## 1. The defect, established by reading

**`Http1ConnectionTaskT::initiateClose( )` (`Http1ConnectionTask.h:1468-1490` at `b100c2a`)** calls
`base_type::getSocket( ).cancel( ec )` under `isChannelOpen( )`, then `cancelIdleTimer( )`. Nothing
else. **`Http2ConnectionTaskT::initiateClose( )` (`Http2ConnectionTask.h:2550-2559`, identical at
both tips)** calls `cancelTimers( )`, then `getSocket( ).cancel( ec )` under `isSocketCreated( )`.
Nothing else.

**Why a cancel is not enough, verified in asio's own source rather than inferred.**
`boost/asio/impl/write.hpp:327-360` — `write_op::operator( )` is a resumable switch. It calls
`stream_.async_write_some( buffers_.prepare( max_size ), *this )` and **returns**; the next entry
is the `default:` label, reached when that elementary operation's handler runs, and the loop then
issues the next `async_write_some`. Between those two points the composed operation has **no
elementary operation registered with the reactor at all**. `socket.cancel( )` reaps what is
registered; at that instant there is nothing of this write to reap, the loop arms its next step
afterwards, and `initiateClose( )` is called **exactly once per run**
(`MultiOperationTask.h:371-376`, guarded by `m_closeInitiated`) so no second cancel is coming.

Under a stranded policy that window is not a hairline: the intermediate handler is dispatched
through the strand, so it sits in the strand's queue behind whatever handler is running — and the
handler that reaches `initiateClose( )` is itself running on that strand. The interleaving the red
test engineers on purpose is exactly this one.

**The library states the rule this misses, verbatim, at `TcpBaseTasks.h:303-306`:**
*"shutdown() will prevent new read/write requests and cancel() will stop existing such requests"*.
`shutdown( )` is a property of the socket, not an entry in the reactor's table, so it poisons the
step the composed loop has not issued yet. That is the whole of the fix.

**Production consequence.** A server that answers early and stops reading — a 413 or a 401, which
nginx does — leaves an HTTP/1.1 connection task with a write nothing will wake, until the peer's own
timeout or TCP gives up.

**What the accounting then does, read at `MultiOperationTask.h:322-387`.** The task cannot take its
terminal path while that write is outstanding: `takeTerminalNoLock( )` (`:147-157`) requires
`0U == m_pendingOperations`. So this is a hang and not a slow failure.

---

## 2. (a) — shut the send side down where, and only where, a cancel cannot reach

**Change, in both drivers, inside the existing `isChannelOpen( )` / `isSocketCreated( )` guard and
before the existing `cancel( )`:**

```cpp
if( m_isWriteInFlight )
{
    TcpSocketCommonBase::m_wasSocketShutdownForcefully = true;

    base_type::getSocket().shutdown( asio::ip::tcp::socket::shutdown_send, ec );
}
```

The existing `cancel( ec )` stays, unconditionally and after it. `cancelIdleTimer( )` (h1) and
`cancelTimers( )` (h2) stay exactly where they are. The error code is discarded, as today: a
teardown call that fails must not cost the task its terminal path, and `applyDecision( )`
(`MultiOperationTask.h:170-190`) already logs and discards a throw out of `initiateClose( )` for the
same reason.

### 2.1 Why the send side only, and not `shutdownSocket( )`

**The obvious shape is to reuse what the driver already owns** —
`Http1ConnectionTaskT::shutdownOnStreamExecutor( )` (`:1575-1588`), which `cancelTask( )` posts, and
whose body is `TcpSocketCommonBase::shutdownSocket( getSocket( ), true /* force */ )` =
`linger( false, 0 )` + `shutdown( shutdown_both )` + `cancel( )` (`TcpBaseTasks.h:241-334`). The h2
sibling is the policy's `shutdownSocketOnStrand( )` (`TcpStrandedStreams.h:112-122`), protected and
therefore reachable. **That shape is rejected, and the reason is a correctness hazard rather than a
preference.**

`shutdown_both` is `SHUT_RD` as well. `SHUT_RD` makes **our own socket** report **`eof`** to a read
that is armed on it. `net::isCleanEndOfStreamErrorCode( )` — the predicate S6R.2's N2 added at
`NetUtils.h:387-421` — is `asio::error::eof == ec` **on every platform**, and the h1 read path uses
it, through `isCleanEndOfStream( )` (`:1050-1053`), to decide whether `parseEof( )` may declare a
**close-delimited** response COMPLETE (`onPeerClosed( ):1071-1095`). That predicate's own comment
says what is at stake: *"Declaring it complete would hand the caller a truncated response reported
as a success, and the caller would have no way to tell."*

So `shutdown_both` would manufacture, locally, the one code the read path is built to trust as the
peer's orderly close. **It is reachable.** `initiateClose( )` runs with a live parser, a live sink
and the read still armed on every path where the close came from a **first error raised somewhere
other than the read handler** — `onWriteCompleted( )`'s `CHK_EC` at `:789`, `onStartRequest( )`'s
initiating `catch` at `:724-743`, `chkArmIdleTimer( )`'s `catch` at `:1408-1418`. A request whose
response is close-delimited and half-received, whose write then fails, would on that path be
reported to the sink as a **success with a truncated body**.

`shutdown_send` manufactures nothing on the read side. The armed read is reaped by the `cancel( )`
that follows, with `operation_aborted`, exactly as it is today.

### 2.2 Why it is gated on `m_isWriteInFlight`

An ungated shutdown is *also* correct for the defect and is one branch shorter. It is rejected
because it would change behaviour on every path that works today, for nothing:

- **It would cost every deliberate close its TLS `close_notify`.** For a TLS-policy driver
  `scheduleTaskFinishContinuation( )` (`TcpSslBaseTasks.h:470-517`) runs `beginProtocolShutdown( )`
  at task finish whenever `m_isCloseStreamOnTaskFinish && isShutdownNeeded( ) &&
  ! m_wasSocketShutdownForcefully && ! m_scheduledForShutdown`. Both drivers set
  `isCloseStreamOnTaskFinish( true )` in their constructors (`Http1ConnectionTask.h:282`,
  `Http2ConnectionTask.h:440`), and `isShutdownNeeded( )` (`TcpSslBaseTasks.h:667-675`) delegates to
  the **stream wrapper**, not to a per-task flag — so an already-handshaken stream handed over by
  `attachStream( )` reports true. That continuation goes out today on h1's idle close and on h2's
  GOAWAY drain, and a send side that is shut cannot carry it.
- **It would cost the armed read its `operation_aborted`** on those same paths, for the reason in
  §2.1, in exchange for nothing — there is no write for the shutdown to wake.

`m_isWriteInFlight` is exactly the predicate "a composed write is outstanding, so `cancel( )` may not
be able to reach it". Both drivers carry it under that name: h1 at `:227`, set at `:707` before
`async_write`, cleared at `:760` at the top of the handler and at `:737` in the initiating catch; h2
at `:372`, set at `:1622`, cleared at `:1665`. Both are touched only on the stream's executor. **With
the gate, the change is provably inert on every path where the connection ends correctly today**,
which is the strongest form the argument can take.

**The one route where h1's `initiateClose( )` is NOT on the stream's executor**, and why reading the
flag there is still safe: `scheduleTask( ):1496` → `scheduleRead( ):813` → its `catch` →
`onOperationCompleted( )`, which `TaskBase::scheduleNothrow( )` reaches **under the task lock**
(`TaskBase.h:1160-1208` takes `BL_MUTEX_GUARD( m_lock )` and then calls `scheduleTask( eq )`). On
that route no write has ever been issued, so `m_isWriteInFlight` is false, the new branch does not
run, and the behaviour is today's. *(That this route also calls `notifyReady( )` under the task lock,
which `MultiOperationTask.h:62-67` says must never happen, is a pre-existing defect of the mix-in's
contract; it is recorded in §13 and is not this change's to fix. It is named here because a reader
checking "is `initiateClose( )` always on the strand?" will find it and must not conclude the new
code is unsafe there.)*

### 2.3 Why `m_wasSocketShutdownForcefully` is set, and set first

It is not decoration and it is not an invention: it is the idiom this library already states.
`TcpSslStrandedStreams.h:70-76` says why `cancelTask( )` sets it **synchronously** rather than on the
strand — *"`isShutdownNeeded( )` and `scheduleTaskFinishContinuation` both read it to decide whether
a TLS shutdown is still owed. Setting it on the strand would let the terminal path read it stale and
start an `async_shutdown` on a stream whose socket is about to be shut down from under it."* A send
side we have just shut is exactly that stream. Setting the flag makes `isShutdownNeeded( )` return
false at its first line and the doomed `close_notify` is never attempted.

**Without it the attempt is made and fails, and on Windows that failure is not classified as
expected.** `onShutdownCompleted( )` (`TcpSslBaseTasks.h:593-646`) rethrows through
`BL_TASKS_HANDLER_CHK_EC( ec )` when the code is neither `eof` nor `isExpectedException( )`. On POSIX
the failure is `broken_pipe`, which `isExpectedSocketException( )` lists (`TcpBaseTasks.h:188-200`)
and which is consulted because `m_isHandshakeCompleted` is false on a task that never performed the
handshake (`TcpSslBaseTasks.h:439-462`) — so the task survives. On Windows a send after
`shutdown( SD_SEND )` is `WSAESHUTDOWN` **10058**, and that value is in **neither** of that
function's two lists. So the ungated, unflagged variant would fail a cleanly-closing TLS connection
on Windows only. The flag removes the question instead of betting on an error-code list — which is
also what `NetUtils.h:333-337` instructs.

**Its other reader is unaffected.** `TcpSocketCommonBaseT::onTaskStoppedNothrow( )`
(`TcpBaseTasks.h:111-145`) converts a task to `operation_aborted` only when
`isCanceled( ) && m_wasSocketShutdownForcefully`. `isCanceled( )` is false on a deliberate close and
on an error close; on an external cancel `cancelTask( )` sets the flag anyway
(`Http1ConnectionTask.h:1558-1572`, `TcpStrandedStreams.h:205-232`,
`TcpSslStrandedStreams.h:207-234`). No public consumer reads `wasSocketShutdownForcefully( )`:
grepped over `src/`, the only writers are those three `cancelTask( )`s and the only readers are the
two sites named above.

### 2.4 Three further shapes considered and rejected

1. **Cancel twice — post a second `cancel( )` to the strand.** The re-armed step is registered by
   then, so one extra cancel would usually reap it. Rejected: "usually" is the whole objection. The
   composed loop arms once per `write_some`, so a peer that takes the buffer a few bytes at a time
   needs arbitrarily many posts, and the ordering the argument rests on exists only under a stranded
   policy. It trades a deterministic fix for a probabilistic one.
2. **`socket.close( )` instead of `shutdown( )`.** It reaps everything registered and would, one
   hopes, make `(b)` unnecessary. Rejected twice over: the composed write's *next* step is issued on
   a closed descriptor and completes `bad_descriptor`, which is neither `operation_aborted` nor
   excused by anything — so `(b)` is still needed, with a worse code — and `isChannelOpen( )` then
   answers false for the rest of the teardown, which several call sites read.
3. **Per-operation cancellation.** Verified as real, not assumed: `write_op` derives from
   `base_from_cancellation_state< WriteHandler >` with `enable_partial_cancellation( )` and tests
   `this -> cancelled( )` between steps (`impl/write.hpp:288-296, 349-353`). Binding a cancellation
   slot to the write handler and emitting `terminal` from `initiateClose( )` would complete the
   write with `operation_aborted`, which `MultiOperationTaskT` **already excuses** — so this shape
   would need no `(b)` at all, and it is the shape to revisit if the Boost floor ever rises.
   Rejected now: the state comes from the handler's *associated* cancellation slot, so it means
   changing how every write is issued in both drivers and wrapping `cpp::bind`'s handler in
   `asio::bind_cancellation_slot` while preserving the executor association; and the codebase still
   carries version guards down to `BOOST_VERSION >= 106600` (`TcpSslBaseTasks.h:244`) and
   `>= 1072` throughout, while per-operation cancellation arrived in 1.78. It would ship two
   mechanisms, and the fallback mechanism would be this design.

---

## 3. (b) — the accounting, read rather than assumed

`MultiOperationTaskT::onOperationCompleted( )` (`MultiOperationTask.h:322-387`) records the **first**
error and only the first. The exemption is at `:357-360`:

```cpp
const bool isSelfInflictedAbort =
    m_closingDeliberate &&
    ! base_type::isCanceled() &&
    isOperationAborted( eptr );
```

Three facts follow, each of which the design depends on:

1. **Only `operation_aborted` is excused.** `isOperationAborted( )` (`:138-141`) is
   `asio::error::operation_aborted == eh::errorCodeFromExceptionPtr( eptr )` — an equality, not a
   family. `broken_pipe` is a genuine error to this accounting, and so is every other spelling a
   locally-shut send side produces.
2. **Only a *deliberate* close excuses it.** `m_closingDeliberate` is set by `beginClose( )`
   (`:239-245`) and by nothing else. The record
   `notes/plans/issues/multioperation-deliberate-close-fails-task-record.md` is **CLOSED**; the flag
   is the maintainer's chosen shape 1 and it is in the tree as read.
3. **An external `cancelTask( )` is deliberately NOT excused** (`:313-320`), through
   `! base_type::isCanceled( )`. Whatever `(b)` does must preserve that.

**And one invariant that is not written down anywhere and that `(b)` rests on:** `m_closing` is true
with `m_firstError` still null **if and only if** the close was deliberate. `m_closing` has exactly
two writers — `beginClose( )`, which sets `m_closingDeliberate` with it, and the `if( eptr &&
! m_firstError )` block at `:344-368`, which assigns `m_firstError` and `m_closing` **inside the same
critical section**. `scheduleNothrow( )` (`:389-420`) clears both per run. So there is no state in
which the task is closing, has no first error, and is not closing deliberately.

**What (a) therefore produces without (b).** The shutdown fires, the re-armed `async_write_some`
fails, `onWriteCompleted( )` reaches `BL_TASKS_HANDLER_CHK_EC( ec )` at `:789`, and the epilog
records that code as the task's first error, because it is not `operation_aborted`. The task
completes `isFailed( )` — `TaskBase::isFailed( )` is `m_state >= PendingCompletion && m_hasException`
(`TaskBase.h:1109-1112`), so the "expected exception" classification does **not** save it; expected
only changes how loudly it is logged. **A deliberate close would end FAILED instead of hanging, and
the pool would count a clean shutdown as a failed connection.** That is not a fix.

---

## 4. (b) — the change: a write that fails while the task is already closing is not the task's error

**h1 — `onWriteCompleted( )` (`:745-798`), which classifies nothing today.** One predicate before
the prolog, and the existing `CHK_EC` becomes conditional:

```cpp
    /*
     * CLASSIFIED BEFORE THE PROLOG, the same way onReadCompleted( ) classifies an end of stream
     */

    const bool isOurOwnTeardown = ec && base_type::isClosing();

    BL_TASKS_HANDLER_BEGIN()

    ... m_isWriteInFlight, the zero-octet answer and the two storage clears, unchanged ...

    if( ! isOurOwnTeardown )
    {
        BL_TASKS_HANDLER_CHK_EC( ec );
    }

    BL_TASKS_HANDLER_CHK_CANCEL_IMPL()

    BL_TASKS_HANDLER_END_MULTIOP()
```

**h2 — `onWrite( )` (`:1638-1715`), which already classifies one thing.** One arm ahead of the
existing `isPeerClosed( )` arm:

```cpp
    if( ec )
    {
        if( base_type::isClosing() )
        {
            /* our own teardown - nothing to report and nothing to do */
        }
        else if( isPeerClosed( ec ) )
        {
            onPeerClosed();
        }
        else
        {
            BL_TASKS_HANDLER_CHK_EC( ec );
        }
    }
```

The new arm must come **first**, and it must do nothing rather than call `onPeerClosed( )`: on the
peer-close door `onPeerClosed( )` has already run from `onRead( )` and would otherwise republish
state and re-close streams a second time.

### 4.1 Why the predicate is the task's state and not the error's code

The brief asks for "the write-side equivalent" of the read path's classification, and the equivalent
is **not** the same kind of test. On the read side the transport is the only witness that the
conversation ended, so the code is the only evidence there is. On the write side we ourselves ended
it, and our own state is better evidence than any code — decisively so, because the codes diverge:
`broken_pipe` on POSIX, `WSAESHUTDOWN` on Windows, and whatever an `ssl::stream` surfaces on top of
either. `NetUtils.h:333-337` states the house rule for precisely this situation — *"do not compare
against ... in networking code ... if neither fits, add a third HERE with its reasoning rather than
open-coding the comparison at the call site"* — and the record behind it says this library has paid
for a platform-specific code comparison **three times**. A state predicate is right on Windows
without a Windows run; a code predicate cannot be known to be.

### 4.2 Why `isClosing( )` and not a new `isClosingDeliberate( )`

`isClosing( )` is already public on the mix-in (`:258-263`) and already read by both drivers.
It is true in two cases and the invariant of §3 makes both correct:

- *Closing deliberately.* This is the case the fix exists for: we shut the send side down, so the
  write's failure is self-inflicted and says nothing about the run.
- *Closing because something already failed.* Then `m_firstError` is already set, and
  `onOperationCompleted( )` would have **discarded** this error anyway at `:344`. Excusing it
  earlier changes nothing observable.

An `isClosingDeliberate( )` accessor would be additive API on the mix-in and is permitted to ride
with its feature, but it buys nothing here and adds a member function to landed gated core. It is
recorded as considered.

### 4.3 What this does not give up

**The external cancel is still reported.** `BL_TASKS_HANDLER_CHK_CANCEL_IMPL( )` stays **outside**
the guard. If the task has been cancelled it throws `operation_aborted`, the epilog finds
`base_type::isCanceled( )` true, `isSelfInflictedAbort` is false, and the task completes
`isFailed( )` with `operation_aborted` exactly as `MultiOperationTask.h:313-320` requires. There is a
second net underneath it — `TcpSocketCommonBaseT::onTaskStoppedNothrow( )` re-imposes
`operation_aborted` whenever `isCanceled( ) && m_wasSocketShutdownForcefully` — but the contract is
kept at the handler and does not lean on it.

**No genuine error is lost that was previously reported.** By §3's invariant the only errors the new
arm swallows are ones that were either self-inflicted or already second.

**The ordering holds without a race.** `m_closing` is set synchronously in the handler body that
decides to close — `closeConnection( )` → `beginClose( )` (h1 `:1331-1352`), `onPeerClosed( )` →
`beginClose( )` (h2 `:1571`), `chkFinishClose( )` → `beginClose( )` (h2 `:2267`) — and
`initiateClose( )` runs in that same handler's epilog, before the write handler can be dispatched.
Under the stranded policies, which is where the h1 class comment narrows its correctness claim
(`Http1ConnectionTask.h:71-83`), both handlers run on the one strand and cannot interleave.

**The lock order is unchanged.** `isClosing( )` takes the accounting's leaf lock only, and
`onOperationCompleted( )` releases that lock before `applyDecision( )` reaches `notifyReady( )` — so
the accounting lock is never held while the task lock is taken, in either order.

### 4.4 One arm deliberately NOT added to h1, and why it is not needed here

h2's `onWrite( )` has a second classification h1's `onWriteCompleted( )` still lacks: a write that
fails because **the peer** went away, while we are not closing, is an ordinary end there
(`isPeerClosed( ec )` → `onPeerClosed( )`) and a task failure here. h2's own comment at `:1648-1660`
says why that matters — *"which made the classification depend on which handler the ending happened
to reach first"* — and h1 has precisely that asymmetry today: the read path took
`net::isPeerClosedErrorCode( )` in S6R.2's N2 and the write path did not.

**It is a real defect and it is not this one.** It is pre-existing, `(a)` does not touch it, and
nothing in this change-set depends on it. Folding it in would mix a second fix into a gated core
change. It belongs on the owed list (§13) with the shape stated, so the next reader of
`onWriteCompleted( )` does not have to rediscover it.

---

## 5. The h2 driver — the same shape, and the exposure established in fact

The brief asks whether h2 has the same exposure *in fact*. It does, but through different doors than
h1, and one of its three doors is already protected. This is what reading the write pump gives:

**`pumpWrites( ):1591-1636` returns immediately while `m_isWriteInFlight` is true**, and
`chkFinishClose( ):2253-2290` — the **only** call to `beginClose( )` on the graceful path — is called
from `pumpWrites( )` and from nowhere else. So:

- **Door 1, the graceful close (GOAWAY drain): NOT exposed.** `beginClose( )` cannot be reached while
  a write is outstanding, by construction. And if the drain never completes because the peer stopped
  reading, `armDrainDeadline( ):2145-2174` — armed at both sites that set `m_isCloseWhenDrained`,
  `closeGracefully( ):2236-2239` and `onConnectionErrorEvent( ):1442` — expires and cancels the task,
  which reaches the policy's forced shutdown. The h2 author saw this hazard and bounded it; the
  comment at `:2126-2143` says so in as many words.
- **Door 2, the peer close: EXPOSED.** `onRead( )` gets `eof`/reset with a write outstanding →
  `onPeerClosed( ):1567-1572` → `cancelTimers( )` and `beginClose( )` → the epilog runs
  `initiateClose( )` → cancel only → the composed write is not woken. **No timer is left to bound
  it**: `initiateClose( )` has just cancelled them all and this door never arms the drain deadline.
  A peer that half-closes its write side while our send buffer is full — FIN on the read, and still
  not reading — wedges the task exactly as h1's does.
- **Door 3, any first error while a write is outstanding: EXPOSED, in both drivers.** A read that
  fails with a non-peer-close code, a throw out of `drainSessionEvents( )`, h1's `onBytesRead( )`
  refusing an over-long field line — each records a first error in its epilog, which calls
  `initiateClose( )`, which cannot reach the write. Also unbounded, and also unfixed today.

So the h2 driver needs **both** halves, and needs them for doors 2 and 3. On door 3 the `(b)` arm is
inert, because the first error is already recorded; it is door 2 that would otherwise turn a peer's
close into a FAILED connection.

**Two shape differences to respect when the edit is made.** h2's guard is `isSocketCreated( )` and
h1's is `isChannelOpen( )` — keep each as it is, since `shutdown( )` on a created-but-closed socket
returns `bad_descriptor` and is discarded like every other code here. And h2 has no
`shutdownOnStreamExecutor( )` of its own; the policy's `shutdownSocketOnStrand( )` is reachable but
is the rejected `shutdown_both`, so h2 gets the same two lines written out, not a call.

---

## 6. Does one fix serve both?

**One rule, four edits, two files, one change-set.**

The *rule* is single and is worth stating once for both drivers: *a teardown must shut the send side
down where a cancel cannot reach the write, and a write that fails while the task is already closing
is not the task's error.*

It cannot be hoisted anywhere. `MultiOperationTaskT` owns the accounting but has no socket —
`initiateClose( )` is virtual there precisely because only the task knows what it has in flight. The
stream policies own the socket but do not know the task is closing. So the same two lines are written
in each driver's own `initiateClose( )`, and the classification in each driver's own write handler —
one new predicate in h1, which classifies nothing, and one new arm in h2, which already classifies
peer close.

**They ship together.** Per driver, `(a)` without `(b)` converts a hang into a failure; `(b)` without
`(a)` is dead code. Across drivers, splitting would leave the same defect open in one of them with
the fix's own commit message explaining how to fix it.

**Gating.** Both `initiateClose( )` overrides and both write handlers are existing core code paths on
every HTTP connection this library makes, so this is its own tested change-set gated on the entire
suite, not a rider on anything. It sequences after S6R.2, which it reads `m_isWriteInFlight` from.

---

## 7. What else (a) moves, stated rather than discovered later

1. **A TLS `close_notify` is not sent on a close where a write was outstanding.** The peer sees a
   truncation instead. Every predicate in this library already treats that as an orderly end —
   `STREAM::isStreamTruncationError( )`, and `net::isCleanEndOfStreamErrorCode( )`'s own comment
   naming RFC 2818 §2.2.2 — and RFC 8446 §6.1 permits closing without waiting for the peer's
   `close_notify`. On every path where no write is outstanding, §2.2's gate means the continuation
   runs exactly as it does today.
2. **On the gated path the pending read's wake code does not change.** The `cancel( )` still runs,
   unconditionally and after the shutdown, and `SHUT_WR` does not make a socket readable. This is the
   whole point of §2.1 and it is what keeps `onPeerClosed( )` out of the teardown.
3. **The write's own outcome changes from "never" to an error code the handler now excuses.** The
   sink is unaffected: on H01's path `finishStream( )` has already delivered `onClosed` before the
   close begins; on the error-close path the first error is already recorded and
   `onTaskStoppedNothrow( )` (`:1602`) still delivers with it.
4. **`m_requestMayHaveBeenSent` may now be cleared by a teardown write that transferred zero
   octets** (`:762-777`), which is correct — zero octets is the one case in which "safe to replay" is
   proven rather than assumed — and moot on H01's path, where `finishStream( )` has already cleared
   it.

---

## 8. Tests

### 8.1 The existing red needs no new assertion, and must not be weakened

`Http1Driver_WriteInFlightRefusesReuseTests`
(`s6r2:src/utests/utf_baselib_httpclient7/TestHttp1DriverWriteBarrier.h`) already asserts exactly the
two things this design is accountable for, in the right order and separately:

- `chkOrFail( result.taskEndedUnaided, "the barrier left a write nothing woke ..." )` — **this is
  (a)**. It is a bounded wait (`UNAIDED_END_IN_MILLISECONDS = 5000`) taken **with the peer still
  parked**, so the driver's own teardown is the only possible waker. Green after (a).
- `chkOrFail( ! result.taskFailed, "the driver task did not end clean: " + result.taskFailure )` —
  **this is (b)**, and it is what makes (a)-alone visible as a failure rather than a pass. Green only
  after (b), and its message carries the reason, which is how a regression will name itself.

The four assertions above them — `closed`, `status == 413`, `state != Ready`, `freeSlots == 0` — are
H01's and stay as they are. **Nothing in this case should change.** A case that already fails for the
right reason and will pass for the right reason is the ideal red, and the temptation to "update" it
should be resisted; the only edit owed is to its comments, which currently explain why it is red
(§9).

**The negative control is free and must be recorded:** with (a) alone, assertion one goes green and
assertion two goes red with `Broken pipe` in the message. The implementing lane should run that
intermediate state once and record it, because it is the only direct evidence that (b) is
load-bearing rather than defensive.

### 8.2 One assertion to add to its sibling

`Http1Driver_WriteInFlightRefusesASecondRequestTests` shares `runBlockedUpload( )` and therefore
shares the 5-second bound, but asserts only the refused handle — so when the driver wedges it stalls
for the full bound and stays green. Add `UTF_REQUIRE( result.taskEndedUnaided )` to it. One line; it
turns a silent five-second stall into a failure.

### 8.3 The h2 side needs its own case, and it is not the obvious one

The h2 red **cannot** be built on a graceful close: §5 shows `chkFinishClose( )` cannot fire with a
write in flight. It must use **door 2** — a peer that stops reading and then half-closes:

- shrink the peer's receive buffer, let it read only the preface and the first HEADERS, then stop
  reading, so a large DATA upload leaves `async_write` outstanding;
- the peer then `shutdown( shutdown_send )`s its own socket, which puts FIN on the wire without
  taking what is still queued;
- our `onRead( )` gets the FIN → `onPeerClosed( )` → `beginClose( )` → `initiateClose( )`, with the
  write still outstanding and every timer just cancelled.

Red before: the task does not end within a bound taken while the peer still holds its end. Green
after (a). And the second assertion, `! isFailed( )`, is red with (a) alone and green with (b), the
same two-assertion shape as h1's. The existing h2 peer harness is the place to build it; whether the
receive-buffer lever reproduces reliably against the HTTP/2 flow-control windows is the one thing
this design cannot settle without running it (§12).

---

## 9. Comments and records that assert something false, and are owed the correction

1. **`finishStream( )`'s "AND IT DOES NOT HANG"** (`b100c2a:Http1ConnectionTask.h:1246-1251`):
   *"the epilog of the very handler that got here then reaches onOperationCompleted( ) ... which
   cancels the socket and so wakes the pending write. The handler that trips the barrier is the one
   that frees it."* False in about three runs in four, on the lane's measurement — 6 of 8, not
   reproduced here. **Amend with the fix** — the mechanism the
   sentence describes is right only once `initiateClose( )` shuts the send side down, so the
   corrected wording should name the shutdown rather than the cancel, and should say why a cancel
   alone is not enough.
2. **H01's commit message** (`bf115f2`): *"which cancels the socket and completes the write it
   refused to wait for"*. History; not to be rewritten. Correct it where a reader will look — which
   is items 1 and 3, and the new change-set's own commit message, which should name `bf115f2` as the
   claim it corrects.
3. **`notes/plans/issues/s6r2-design.md` carries the same false claim TWICE, and the second time
   under a heading that says it was verified at the source.** This is the place a reader will look
   and the brief's "two comments" undercounts it:
   - **§1(b)** — *"`initiateClose( )` cancels the socket ..., which wakes the pending write. **The
     barrier therefore does not hang** ... That chain was read end to end, because a barrier which
     waits for a handler nothing will wake is a deadlock and not a fix."* The chain *was* read end to
     end; what was never established is that `cancel( )` reaches a composed write.
   - **§17**, under the heading *"Verified and standing as written, against the source"* — *"H01's
     barrier cannot hang on any accounted path ... `initiateClose( )` → `getSocket( ).cancel( )`; the
     woken write completes `operation_aborted`, is excused as self-inflicted, and takes the
     terminal."* Both the wake and the code are wrong: the write is usually not woken at all, and on
     the occasions it is woken — by the peer's RST rather than by us — the code is `broken_pipe`,
     which §3 shows is not excused.

   **This design corrects both in place, dated, in the house pattern** (`s6r1-design.md` §2 and §4
   do the same), rather than leaving a "verified at the source" claim standing that is known false.
   The correction is documentation only and is part of this commit.

**How the error was made, since that is the instructive part and this project has recorded the same
shape twice before.** Both readers followed the *control flow* to its end — handler → epilog →
`onOperationCompleted( )` → `initiateClose( )` → `cancel( )` — and stopped at the library call,
taking "cancel wakes the pending operations" as a given. The premise that needed opening was not in
this repository at all: it was `write_op`'s resumable loop in asio. Following a chain to its end
means following it past the last line one owns.

---

## 10. Acceptance

- Focused modules, clang debug, in the lane, **one module at a time**:
  `utf_baselib_httpclient7` (the two barrier cases), `utf_baselib_httpclient3` (the seven h1 driver
  cases, including keep-alive reuse — the control that says the new branch does not fire on an
  ordinary exchange), and the h2 client modules the new case lands in.
- Then clang **and** gcc release plus the whole-suite gate, by the orchestrator. Both edited
  functions are on every HTTP connection this library makes.
- **`Http1Driver_WriteInFlightRefusesReuseTests` must be shown red before and green after**, and the
  **intermediate (a)-only state recorded** (§8.1). Nothing here may rest on "the suite still passes".
- **Re-run the red at least ten times on each side.** The lane measured this case at 4-5 red in 10
  before the fix; a single green run is what let the defect through the first time and is not
  evidence.
- **A TLS run is required and is not optional**, because §2.3's `close_notify` reasoning is the one
  part of this design that is pure inference about a path no case in `utf_baselib_httpclient7`
  exercises. The h1 driver is instantiated over `TcpSslSocketAsyncStrandedBase` at
  `utf_baselib_httpclient3/TestHttp1ConnectionTask.h:1296`; whichever module has a TLS h1 exchange
  must run green, and if none does, that gap is to be reported rather than papered over.
- **Windows is owed and cannot be provided here.** §2.3's `WSAESHUTDOWN` reasoning is why the flag is
  set; the flag makes the question unreachable rather than answering it, which is the point, but the
  matrix run is what confirms no other Windows spelling leaks out of the new arm.

## 11. Readiness

**Not agreed. This is the text a second reader is asked to open against the source.** The gate is
that two readers agree, and one reader got the original claim wrong twice in the same document
(§9.3).

What a reviewer should attack first, in order of how much rests on it:

1. **§2.1's rejection of `shutdown_both`.** It turns on `SHUT_RD` making our own socket report `eof`
   to an armed read, and on that `eof` reaching `parseEof( )` with a live parser. Falsify it by
   finding that no path reaches `initiateClose( )` with the read armed and the parser mid-message.
2. **§3's invariant** — `m_closing && ! m_firstError` ⟺ deliberate close. Falsify it by finding a
   third writer of `m_closing`.
3. **§5's claim that h2's graceful door is protected.** It turns on `chkFinishClose( )` having
   exactly one caller, `pumpWrites( )`, and on `pumpWrites( )`'s early return. Falsify it by finding
   a second caller.
4. **§2.2's claim that `m_isWriteInFlight` is safe to read in `initiateClose( )`.** It turns on the
   enumeration of `onOperationCompleted( )`'s callers and on the one off-strand route among them.

## 12. What could not be settled here, and what it would take

- **The exact error code a locally shut-down pending write reports**, on Windows and under TLS. The
  design is built not to need it — §4.1 — but §2.3's Windows claim was derived from the documented
  meaning of `WSAESHUTDOWN`, not measured.
- **Whether the h2 red of §8.3 reproduces reliably.** HTTP/2's own flow control bounds how much DATA
  can be outstanding, so whether the peer's receive buffer can be made small enough to leave a write
  stuck needs a run.
- **Every number in the lane's report.** The design depends on exactly one qualitative fact from it —
  that `cancel( )` alone left the write outstanding while `shutdown( ) + cancel( )` freed it — and
  that fact is independently established by reading `write_op` (§1) and `shutdownSocket( )`
  (`TcpBaseTasks.h:303-334`). The timings, the run counts and the exception text are corroboration,
  not premises.
- **Whether the fix changes how long an ordinary keep-alive close takes.** §2.2's gate means it
  should not fire at all on one, and `utf_baselib_httpclient3`'s reuse cases are the control, but no
  timing was measured.

## 13. Owed, and deliberately out of scope

- **h1's write path still has no peer-close arm** (§4.4). Shape: the same `isPeerClosed( )` question
  h2 asks at `:1648-1660`, placed after the new `isClosing( )` arm, routing to `onPeerClosed( ec )`
  and `closeConnection( )`. Its own change-set.
- **A composed TLS read can slip a cancel the same way a composed write does.**
  `ssl::stream::async_read_some` is itself composed, so on a TLS connection the armed read can
  re-arm past `initiateClose( )`'s `cancel( )` and then observe the peer's own close — reaching
  `onPeerClosed( )` with a live parser and possibly framing a truncated close-delimited response as a
  success. **Pre-existing; neither created nor closed by this change**, which is why §2.1 refuses to
  add a cleartext route to it. The one-line shape that closes it is to gate `onPeerClosed( )` on
  `! base_type::isClosing( )` — an ending that arrives while we are already tearing down is ours, not
  the peer's — which is the read-side twin of §4's sentence. Not folded in: it changes a path that is
  correct in the common case, and it deserves its own red.
- **`TaskBase::scheduleNothrow( )` calls `scheduleTask( )` under the task lock**, so h1's
  `scheduleRead( )` catch can reach `notifyReady( )` with the task lock held, which
  `MultiOperationTask.h:62-67` forbids. Pre-existing, narrow (it needs `async_read_some` to throw),
  and untouched here.
- **SOCKS5, H04a, and everything else on the astra owed list** — unrelated.
