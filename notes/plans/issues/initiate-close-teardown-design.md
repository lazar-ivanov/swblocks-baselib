# `initiateClose( )` cancels but never shuts down — the design for both drivers

**Status:** design, 2026-09-23. **Nothing implemented. No build was run and nothing under `src/`
was touched.** This is the artifact that must be agreed before code is written, per the review
loop. **Agreed 2026-09-23 — see §14 for by whom, on what, and for the corrections applied in place
before agreement.** **Corrected again 2026-09-23 after `bb53bdd` landed on `lazari2` - see the
dated blocks in §2.1, §2.2, §5, §11 and the addendum to §14. The design stands; §2.1 is moot.
`TcpBaseTasks.h` citations below at or after `:322` are `+32` at `bb53bdd`.**

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

*Resolved 2026-09-23 by the review:* `write_op` was read in the shipping tree, **Boost 1.90.0** at
`/home/lazar/swblocks/dist-devenv7-ub24-gcc1520-clang2010-a64/boost/1.90.0/source-linux/boost/asio/impl/write.hpp`.
The class is at `:312-392`; the `async_write_some( ) ... return; default:` pair is at `:363-366` and
the `cancelled( )` test between steps at `:373-377` — the same shape, so the mechanism §1 rests on
holds in the version that ships. The 1.84.0 line numbers below are kept as written, with the 1.90.0
ones beside them where they are load-bearing.

---

## 1. The defect, established by reading

**`Http1ConnectionTaskT::initiateClose( )` (`Http1ConnectionTask.h:1468-1490` at `b100c2a`)** calls
`base_type::getSocket( ).cancel( ec )` under `isChannelOpen( )`, then `cancelIdleTimer( )`. Nothing
else. **`Http2ConnectionTaskT::initiateClose( )` (`Http2ConnectionTask.h:2550-2559`, identical at
both tips)** calls `cancelTimers( )`, then `getSocket( ).cancel( ec )` under `isSocketCreated( )`.
Nothing else.

**Why a cancel is not enough, verified in asio's own source rather than inferred.**
`boost/asio/impl/write.hpp:327-360` (1.90.0: `:351-384`) — `write_op::operator( )` is a resumable switch. It calls
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
**close-delimited** response COMPLETE (`onPeerClosed( ):1071-1125`). That predicate's own comment
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

**SUPERSEDED 2026-09-23 - `bb53bdd` on `lazari2` changed the helper, and the rejection above no
longer has an object.** The quoted body - *"`linger( false, 0 )` + `shutdown( shutdown_both )` +
`cancel( )`"* - is now `linger( false, 0 )` (on the `force` path only) + **`shutdown( shutdown_send )`**
+ `cancel( )` (`TcpBaseTasks.h:241-368`, the shutdown at `:357`). The commit's reason is the mirror
image of this section's: shutting down the receive side made every task's teardown abortive on
Windows and destroyed data the task had just sent - measured through `PeerCloseErrorCodes_*` on
win-x64 and win-x86, `shutdown_both` -> 10054/10053 and 0 of 16384 bytes, `shutdown_send` -> `eof`
and all 16384 - and those two cases now assert the graceful teardown on every platform. So the
hazard this section describes was real of the helper as it stood, is the reason `bb53bdd` must not
be reverted, and is pinned by a test; it is no longer a reason to write the line by hand.

Consequences for (a), each checked against the source at `bb53bdd`:

- **The `shutdown( )` line may be the helper.** `TcpSocketCommonBase::shutdownSocket(
  base_type::getSocket( ) )` is `shutdown_send` + `cancel( )` exactly, and with `force` a
  `linger( false, 0 )` that sets the default and changes nothing. Calling it couples
  `initiateClose( )` to the guarantee the two control cases pin, which a bespoke line would not be.
  h1 may equally call its own `shutdownOnStreamExecutor( )` (`:1575-1588`; every route into
  `initiateClose( )` is on the strand but §2.2's, where the gate is false) and h2 the policy's
  `shutdownSocketOnStrand( )`; the static helper is the one name both drivers share and assumes
  nothing about the policy. The existing `cancel( )` after the gate may stay - a second cancel
  against an empty reactor table is a no-op - or move to an `else`; either is one reviewable diff.
- **The flag line stays, and it is not made redundant.** `shutdownSocket( )` is static and touches
  no task state; `m_wasSocketShutdownForcefully` is set by every *caller* that means it, beside the
  call - `TcpBaseTasks.h:599-601`, `TcpSslBaseTasks.h:398`, `TcpStrandedStreams.h:224`,
  `TcpSslStrandedStreams.h:226`, `Http1ConnectionTask.h:1565`. §2.3's reasons for setting it first
  are untouched. (A brief that reads the helper as "already setting that flag" has the idiom
  backwards.)
- **The gate stays** - §2.2's first reason; see the correction there.
- **(b) is unchanged.** A send side shut is still `broken_pipe` on POSIX and `WSAESHUTDOWN` on
  Windows to the pending write's next step, and `MultiOperationTask.h:357-360` excuses only
  `operation_aborted`. Nothing in `bb53bdd` touches the accounting or the write handlers.
- **The lane's red is unaffected.** `ScriptedPeer::closeSocket( )` is the peer's own
  `shutdown_both` + `close( )` (`s6r2:Http1DriverTestUtils.h:628-629`), not `shutdownSocket( )`,
  and `runBlockedUpload( )` parks the peer until the answer is taken, so the driver's teardown is
  still the only waker under test. The `broken_pipe` §9.3 cites is the peer's `close( )` with an
  unread upload - RFC 2525 section 2.17, both platforms - and is not this mechanism.

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
  *Withdrawn 2026-09-23:* this bullet was about `shutdown_both`. With `shutdown_send` - the
  helper's behaviour since `bb53bdd`, and (a)'s in either spelling - the armed read is reaped by the
  `cancel( )` with `operation_aborted` on every path, gated or not. The gate now rests on the first
  bullet alone, which `bb53bdd` does not touch: `notifyReady( )` consults
  `scheduleTaskFinishContinuation( )` and runs the TLS shutdown *before* the policy's
  `onTaskStoppedNothrow( )` reaches `shutdownSocket( )`, so a send side shut in `initiateClose( )`
  would precede the `close_notify` on every deliberate close, and only the gate keeps it to the
  closes where a write is outstanding.

`m_isWriteInFlight` is exactly the predicate "a composed write is outstanding, so `cancel( )` may not
be able to reach it". Both drivers carry it under that name: h1 at `:227`, set at `:707` before
`async_write`, cleared at `:760` at the top of the handler and at `:737` in the initiating catch; h2
at `:372`, set at `:1622`, cleared at `:1665`. Both are touched only on the stream's executor. **With
the gate, the change is provably inert on every path where the connection ends correctly today**,
which is the strongest form the argument can take.

*Precision, 2026-09-23 review.* Inert wherever `m_isWriteInFlight` is false when `initiateClose( )`
runs — which is every path with no write outstanding. The flag is also true for a write that has
completed at the socket but whose handler has not yet run (h1 clears it at `:760`, h2 at `:1665`,
both inside the handler), and a read handler that trips H01's barrier in that window ends the
connection correctly today and after this change; there the branch fires, and the only observable
difference is §7.1's — no `close_notify` on that close. "Every path where the connection ends
correctly today" therefore means "every path with no write outstanding", and §7.1 is the whole of
the rest.

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

**CORRECTED 2026-09-23 — the POSIX sentence above is true of h1 only, and "on Windows only"
understates the case.** h2 performs its own handshake, so on the h2 task `m_isHandshakeCompleted` is
true (set at `TcpSslBaseTasks.h:577`); the `! m_isHandshakeCompleted` arm of `isExpectedException( )`
(`:451`) is then skipped, `isExpectedSslException( )` admits SSL-category codes only (`:761-771`),
and the base it falls to (`TcpBaseTasks.h:1472-1491`) admits only a cancelled task's
`operation_aborted`. So the ungated, unflagged variant would fail a cleanly closing TLS **h2**
connection on POSIX as well. The flag is what makes the question unreachable on both drivers and
both platforms, which is a stronger reason for it than the one first written. *Also verified, in
the shipping tree:* Boost 1.90.0's Win32 condition table
(`boost/system/detail/system_category_condition_win32.hpp:64`) maps only `ERROR_BROKEN_PIPE` to
`broken_pipe`, so `eh::isErrorCondition( errc::broken_pipe, ec )` does not admit 10058 either; and
`asio::error::shut_down` (`asio/error.hpp:171`) is the portable name of that code but is
`ESHUTDOWN` on POSIX, where the code actually produced is `EPIPE` — so it is no substitute for
§4.1's state predicate.

**Its other reader is unaffected.** `TcpSocketCommonBaseT::onTaskStoppedNothrow( )`
(`TcpBaseTasks.h:111-145`) converts a task to `operation_aborted` only when
`isCanceled( ) && m_wasSocketShutdownForcefully`. `isCanceled( )` is false on a deliberate close and
on an error close; on an external cancel `cancelTask( )` sets the flag anyway
(`Http1ConnectionTask.h:1558-1572`, `TcpStrandedStreams.h:205-232`,
`TcpSslStrandedStreams.h:207-234`). No production consumer reads `wasSocketShutdownForcefully( )`.
*Count corrected 2026-09-23:* grepped over `src/`, the writers are **five**, every one a
`cancelTask( )` — the plain and TLS base policies (`TcpBaseTasks.h:569`, `TcpSslBaseTasks.h:398`),
the two stranded policies (`TcpStrandedStreams.h:224`, `TcpSslStrandedStreams.h:226`) and h1's
(`Http1ConnectionTask.h:1565`) — and the readers are the three sites named above
(`TcpBaseTasks.h:123`, `TcpSslBaseTasks.h:476` and `:669`) plus the accessor at
`TcpBaseTasks.h:399-401`, whose one reader is a test probe
(`utf_baselib_h2client/TestTcpStrandedStreams.h:1165`) asserting it after a cancel. The conclusion
is unchanged: nothing reads the flag to mean anything but "a TLS shutdown is not owed" and "a
cancelled task reports `operation_aborted`".

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
   would need no `(b)` at all, and it is the shape to revisit — the correction below says the Boost floor is already there.
   Rejected now: the state comes from the handler's *associated* cancellation slot, so it means
   changing how every write is issued in both drivers and wrapping `cpp::bind`'s handler in
   `asio::bind_cancellation_slot` while preserving the executor association; and it would ship a
   second mechanism into two drivers that no code in `src/` uses today (grepped 2026-09-23: no
   `cancellation_signal`, `cancellation_slot` or `bind_cancellation_slot` anywhere in the tree).

   **CORRECTED 2026-09-23 — the version argument first written here was false and is withdrawn.**
   It said the tree "still carries version guards down to `BOOST_VERSION >= 106600`
   (`TcpSslBaseTasks.h:244`) and `>= 1072` throughout, while per-operation cancellation arrived in
   1.78". The guards exist, but they are compatibility shims for an overridden `BOOSTDIR`, not a
   floor: every supported default configuration is at or above 1.84 —
   `OSImplPlatformCommon.h:2162-2163` states "devenv6 pins Boost 1.84.0 and devenv7 pins 1.90.0"
   for every supported build, and the makefiles' legacy filter (`scripts/devenv7/AGENTS.md:398`)
   names no environment that overrides it. So the floor is already high enough, and the rejection
   rests on the two grounds that remain: the change to how every write is issued, and the second
   mechanism. *Verified in the shipping tree, for the record this deferral needs:* `ssl::stream`'s
   `async_read_some( )` and `async_write_some( )` support `terminal` and `partial` cancellation
   (`boost/asio/ssl/stream.hpp:490-496, 547-553`; the composed `io_op` tests `cancelled( )` between
   its steps, `ssl/detail/io.hpp:274-291`), and a registered socket step connects
   `reactor_op_cancellation` to the slot (`detail/reactive_socket_service_base.hpp:238-243`) — so a
   `terminal` emit reaches the registered step, the between-steps window and the composed TLS read
   alike, and would close §13's TLS-read item as well as this defect. Two disciplines come with it:
   `cancellation_signal::emit( )` is not thread-safe and must run on the strand (every
   `initiateClose( )` route does, except h1's `scheduleRead( )` catch of §2.2, where no slot is
   connected yet), and the slot must be re-bound per operation. Its generic home is a seam in the
   mix-in or the stream policies, not two drivers' issue sites — which is the reason it is deferred
   now, and the reason it is the shape to revisit. §14 lays the choice out.

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

*Observation, 2026-09-23 review — not a defect; recorded so the implementation is not "corrected"
by symmetry.* `onWrite( )`'s error branch does not run `BL_TASKS_HANDLER_CHK_CANCEL_IMPL( )` today;
it sits in the success branch (`:1691`), and the new arm keeps that shape. An external cancel that
lands on a task already closing through door 2 — the read already ended, only the write left —
therefore has its `operation_aborted` swallowed by the new arm at the handler and is reported by
`TcpSocketCommonBaseT::onTaskStoppedNothrow( )`'s conversion instead (`TcpBaseTasks.h:123`; the
flag it needs is set synchronously by the policy's `cancelTask( )`, `TcpStrandedStreams.h:224` and
`TcpSslStrandedStreams.h:226`, which h2's own `cancelTask( )` reaches at `:2596`). That is exactly
what the existing `onPeerClosed( )` arm relies on for the same case, so §4.3's "the contract is
kept at the handler" is an h1 statement; in h2 the second net carries it, before and after this
change. `onPeerClosed( )` has no re-entry guard (`:1557-1572`), which is why the arm must do
nothing rather than call it.

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
`chkFinishClose( ):2253-2297` — the **only** call to `beginClose( )` on the graceful path — is called
from `pumpWrites( )` and from nowhere else (two call sites, `:1600` and `:1613`, both inside it;
verified 2026-09-23). So:

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
*Withdrawn 2026-09-23:* `shutdownSocketOnStrand( )` is `shutdown_send` since `bb53bdd`
(`TcpStrandedStreams.h:112-122` -> `shutdownSocket( ..., true )`), so h2 may call the static helper
inside its own `isSocketCreated( )` guard exactly as h1 does - see §2.1's correction. The two guards
stay as they are.

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
  `utf_baselib_httpclient3/TestHttp1ConnectionTask.h:96` (an explicit instantiation — it compiles
  the TLS driver and exchanges nothing over it); whichever module has a TLS h1 exchange must run
  green, and if none does, that gap is to be reported rather than papered over.
  **Gap established 2026-09-23: none does.** The only other module naming the TLS stranded policy
  is `utf_baselib_httpclient5/TestClientSessionTls.h`, whose peer prefers `h2` in both of its cases
  (`:294-295`, `:391-392`; `:351` asserts the negotiation), so no case in the suite closes an h1
  connection over TLS today. *PROPOSAL:* add one case to `utf_baselib_httpclient5`, whose
  `TlsPeerT` (`:45-100`) already takes a server preference through
  `CryptoBase::setAlpnServerPreference( )` — set it to `http/1.1`, run a keep-alive GET through the
  session, let the idle close end it, and assert the negotiated protocol and a clean end. That
  exercises §2.3's `close_notify` continuation on the path where no write is outstanding, which is
  the control this gate needs, subject to the module's headroom (`make utests-sizes`). The
  write-outstanding TLS path itself stays inference until an h1 barrier case exists over TLS; that
  is a second case and is not required for this gate.
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
   *2026-09-23:* attacked and stood (§14.1), then made moot by `bb53bdd` - the helper no longer
   does `shutdown_both`. The premise remains true of `shutdown_both` and is why that change stands.
2. **§3's invariant** — `m_closing && ! m_firstError` ⟺ deliberate close. Falsify it by finding a
   third writer of `m_closing`.
3. **§5's claim that h2's graceful door is protected.** It turns on `chkFinishClose( )` having
   exactly one caller, `pumpWrites( )`, and on `pumpWrites( )`'s early return. Falsify it by finding
   a second caller.
4. **§2.2's claim that `m_isWriteInFlight` is safe to read in `initiateClose( )`.** It turns on the
   enumeration of `onOperationCompleted( )`'s callers and on the one off-strand route among them.

**Agreed 2026-09-23** — each of the four was attacked at the source and stands; see §14 for what was
opened, what was corrected in place before agreement, and the one choice left to the maintainer.

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

**SCHEDULED 2026-09-23, by the maintainer: §10's h1-over-TLS control is its own change-set, taken
after S6R.3.** It is not folded into S6R.3, because it is independent of all four of those slices
and mixing it into a core-path change-set would blur what each is gated on.

What it buys, stated so the next reader does not have to re-derive it: **no case in the suite runs an
exchange over HTTP/1.1 on TLS.** The h1 driver is explicitly instantiated over the TLS policy, so it
compiles, but every TLS session case asserts `negotiatedAlpn( ) == "h2"` and therefore ends up on the
h2 driver. So nothing would notice if `close_notify` stopped going out on an h1 TLS connection —
which makes §2.3's reasoning about the send-side shutdown and the `close_notify` an inference on
both drivers rather than a tested fact. The write-in-flight gate makes the question *unreachable*,
not answered.

Two cases, in order: a session case in `utf_baselib_httpclient5` with the server's ALPN preference
set to `http/1.1`, a keep-alive GET and an idle close — the `close_notify` control; then a second
with a write outstanding, which is the one that exercises the gated path. Check the module's size
first; `httpclient5` was last measured well under target but S6R.2 added to it.


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
- **h2's `scheduleRead( )` has no accounting guard** (`Http2ConnectionTask.h:1449-1466`): it calls
  `beginOperation( )` and then `async_read_some( )` with no `catch` completing the operation, where
  h1's read (`:813-838`), write and timer each carry one. A throw out of the initiator would leave
  the count one high and the task unable to take its terminal path. Noticed 2026-09-23 while
  verifying §11.4; pre-existing, narrow, untouched here.
- **SOCKS5, H04a, and everything else on the astra owed list** — unrelated.

## 14. Design review, 2026-09-23

**Reviewer: Claude Fable 5.1, on `teardown-close` @ `9a7d5ec`, read against the source at `s6r2` @
`b100c2a` in the lane2 worktree, which is the base this design names.** Opened at the signature and
read to the end: `MultiOperationTask.h` whole; h1's `onStartRequest( )`, `onWriteCompleted( )`,
`scheduleRead( )`, `onBytesRead( )`, `isCleanEndOfStream( )`, `onPeerClosed( )`,
`onReadCompleted( )`, `finishStream( )`, `closeConnection( )`, `chkArmIdleTimer( )`,
`cancelIdleTimer( )`, `onIdleDeadline( )`, `initiateClose( )`, `scheduleTask( )`, `cancelTask( )`,
`shutdownOnStreamExecutor( )`, `onTaskStoppedNothrow( )`, `onCancelStream( )` and the class
comment; h2's `onConnectionErrorEvent( )`, `scheduleRead( )`, `isPeerClosed( )`, `onRead( )`,
`onPeerClosed( )`, `pumpWrites( )`, `onWrite( )`, `cancelTimers( )`, `armDrainDeadline( )`,
`onDrainDeadline( )`, `closeGracefully( )`, `chkFinishClose( )`, the session start,
`initiateClose( )`, `cancelTask( )` and `onTaskStoppedNothrow( )`; `TcpBaseTasks.h`'s
`onTaskStoppedNothrow( )`, `isExpectedSocketException( )`, `shutdownSocket( )`, the base
`cancelTask( )` and `isExpectedException( )`; `TcpSslBaseTasks.h`'s `cancelTask( )`,
`isExpectedException( )`, `scheduleTaskFinishContinuation( )`, `onShutdownCompleted( )`,
`isShutdownNeeded( )`, `isExpectedSslException( )` and `attachStream( )`; both stranded policies
from their class comments to their ends; `NetUtils.h`'s three predicates and the rule above them;
`TaskBase.h`'s handler macros, `isFailed( )` and `scheduleNothrow( )`; `Http1Codec.h`'s
`parseEof( )`; `TestHttp1DriverWriteBarrier.h` whole; the corrected sections of `s6r2-design.md` in
context; and `write_op` in **both** Boost trees — 1.84.0 as the design cites it and 1.90.0 as the
dist ships it — with `ssl/detail/io.hpp`, `ssl/stream.hpp`, `reactive_socket_service_base.hpp` and
Boost.System's Win32 condition table in 1.90.0. Nothing was built or run.

**Verdict: agreed, with the corrections written in place above, each dated 2026-09-23.** Both
halves stand as designed — `shutdown_send` gated on `m_isWriteInFlight` with the flag set first, and
the state predicate `ec && isClosing( )` guarding `CHK_EC` with `CHK_CANCEL_IMPL` outside it — and
so does "one rule, four edits, two files, one change-set". One premise in a *rejected* alternative
was false (§2.4.3's Boost floor) and is withdrawn; the rejection stands on its remaining grounds,
and whether it should is the one choice this review leaves to the maintainer, laid out at the end.
One claim in §2.3 was true of h1 only and is now stronger. One acceptance item named a run that
does not exist (§10's TLS h1 exchange) and now says so, with a proposal for the case that would.

**The four premises of §11, each attacked separately from its conclusion:**

1. **§2.1 stands.** `isCleanEndOfStreamErrorCode( )` is `asio::error::eof == ec` on every platform
   (`NetUtils.h:418-421`); h1's `isCleanEndOfStream( )` (`:1050-1053`) admits it; `onPeerClosed( )`
   (`:1071-1125`) reaches `parseEof( )` only through that predicate; and `parseEof( )`
   (`Http1Codec.h:662-697`) sets `m_isComplete` at `:696` when the backend is done after
   `putEof( )` at `:685` — which is exactly a half-received close-delimited body. The read is armed
   for the whole life of the connection (`scheduleTask( ):1517`, and `:793-794` in the write
   handler's own words), and the parser is live from `onStartRequest( ):615`, before the write is
   issued — so a first error out of `onWriteCompleted( )`, `onStartRequest( )`'s catch or
   `chkArmIdleTimer( )`'s catch reaches `initiateClose( )` with both. `SHUT_RD` on our own socket is
   `eof` to that read. Falsification attempted and failed.
2. **§3's invariant stands, in the direction (b) needs.** `m_closing` is private and has three
   writers: `beginClose( ):243` together with `m_closingDeliberate`; `onOperationCompleted( ):367`,
   inside `if( eptr && ! m_firstError )` and after `m_firstError = eptr` at `:364`, under the one
   guard taken at `:335`; and `scheduleNothrow( ):409`, clearing both with `m_firstError` at
   `:413`. Nothing else in the tree can touch it. So `m_closing && ! m_firstError` ⟹ deliberate.
   The converse is neither needed nor true — a deliberate close can record a first error
   afterwards — and §4.2's two cases are exactly the partition the arm relies on: a closing task
   either has no first error, in which case the close was deliberate and the failed write is ours,
   or has one, in which case `:344` would have discarded this error anyway. Neither `cancelTask( )`
   nor `requestCancel( )` writes `m_closing`, which is what keeps the external cancel on the path
   §4.3 describes: with `isClosing( )` false the write's `operation_aborted` reaches `CHK_EC` and is
   recorded as not self-inflicted; with it true, `CHK_CANCEL_IMPL` records it; and either way
   `TcpSocketCommonBaseT::onTaskStoppedNothrow( )` (`TcpBaseTasks.h:123`) re-imposes it. The
   window between the pre-prolog predicate and `CHK_EC` is closed by `m_closing` being monotonic
   within a run and every writer of it running on the strand, except the one route of §2.2, which
   precedes any write.
3. **§5 stands.** `chkFinishClose( )` is called at `:1600` and `:1613`, both inside `pumpWrites( )`,
   which returns at `:1593` while `m_isWriteInFlight` is true; `beginClose( )` has exactly two
   callers in h2, `:1571` and `:2267`. `onPeerClosed( )` (`:1557-1572`) arms no deadline, and
   `cancelTimers( )` (`:2092-2122`) cancels all five timers including `m_drainTimer`, so door 2 is
   unbounded as written. h2's `isPeerClosed( )` (`:1477-1502`) is `isPeerClosedErrorCode( ) ||
   isStreamTruncationError( )`, and `isPeerClosedErrorCode( )` (`NetUtils.h:382`) is
   `isOrderlyPeerCloseErrorCode( ) || connection_reset` over `eof` and the two Windows spellings
   (`:353-371`): no `broken_pipe` on any platform, so (a)'s locally produced code would reach
   `CHK_EC` on door 2 without the arm, and the arm must precede the `isPeerClosed( )` one because
   `onPeerClosed( )` has no re-entry guard.
4. **§2.2 stands.** h1's `m_isWriteInFlight` is written at `:707`, `:737` and `:760` and nowhere
   else, and there is one `async_write` (`:713`); h2's at `:1622` and `:1665`, one `async_write`
   (`:1626`). Every `onOperationCompleted( )` reached in h1 is a strand handler's epilog except
   `scheduleRead( )`'s catch (`:836`), which `scheduleNothrow( )` reaches under the task lock
   (`TaskBase.h:1167`, `:1208`) before `onStartRequest( )` has even been posted (`:1517` precedes
   `:1521-1526`), so the flag is false there. h2 has no explicit `onOperationCompleted( )` call at
   all — every route is an epilog, on the strand, as its own comment at `:2082-2085` says. Both flags
   are read on the strand.

**Verified and standing as written, against the source:**

- **§1**: `initiateClose( )` at h1 `:1468-1490` and h2 `:2550-2559` do what the design says and
  nothing else; `m_closeInitiated` at `:371-376`; `takeTerminalNoLock( )` at `:147-157`; the
  `TcpBaseTasks.h:304-305` comment verbatim. **`write_op` in 1.90.0** has the shape §1 describes
  (`:351-384`) — the provenance caveat is resolved, not tolerated.
- **§2.3's chain**: `scheduleTaskFinishContinuation( )`'s four-way condition (`:475-479`);
  `isShutdownNeeded( )` reading the flag at its first line (`:669`) and otherwise delegating to
  the wrapper, which `attachStream( )` moves whole (`:712-716`); `onShutdownCompleted( )` reaching
  `CHK_EC` (`:640`) only when there is no original exception — the deliberate-close case; the
  Windows list at `TcpBaseTasks.h:211-226` with no 10058; `broken_pipe` in the POSIX list (`:194`).
- **§3**: the exemption at `:357-360` is an equality on `operation_aborted` and requires
  `! isCanceled( )`; `isCanceled( )` is an atomic (`TaskBase.h:544`, `:1087`); `isFailed( )` is
  `m_state >= PendingCompletion && m_hasException` (`:1109-1112`); the handler macros put
  `onOperationCompleted( )` outside the task lock (`:107-118`, `:279-283`), and `CHK_EC` reaches the
  epilog by `break` (`:139-157`), so `if( ! isOurOwnTeardown ) { CHK_EC }` behaves as §4 says.
- **§4.3**: `closeConnection( )` → `beginClose( )` at `:1331-1347`; `isClosing( )` (`:258-263`)
  takes the leaf lock only; every handler body holds the task lock (`BL_TASKS_HANDLER_BEGIN( )`),
  so the established nesting is task → leaf and never leaf → task, and the pre-prolog placement
  mirrors `onReadCompleted( ):1152-1153`.
- **§7.3, §7.4**: h1's `onTaskStoppedNothrow( )` (`:1602-1647`) delivers `onClosed( )` with the
  first error's code and `! m_requestMayHaveBeenSent`; the zero-octet clear is at `:762-777`.
- **§8.1, §8.2**: the two `chkOrFail( )` at `TestHttp1DriverWriteBarrier.h:402-411`, in that order;
  `UNAIDED_END_IN_MILLISECONDS = 5000` (`:89`); the sibling asserts `closed` and the refused handle
  only (`:434-438`); the comments at `:159-189` and `:383-400` already explain why it is red and are
  the only edit owed there.
- **§9.3**: `s6r2-design.md` §1(b) and §17 say what the design quotes, and the corrections are
  right in substance — the cancel does not wake a composed write between steps, and a write woken
  by the peer's RST completes `broken_pipe` (the test's own comment at `:167-175` records the
  measurement), which `:357-360` does not excuse. One wording precision applied there: §1(b) has one
  bold sentence, not two; the clause before it is the other false claim.
- **§2.4.2**: `isChannelOpen( )` (`TcpBaseTasks.h:470`) is read by `initiateClose( )`,
  `cancelTask( )`, `shutdownOnStreamExecutor( )` and `chkArmIdleTimer( )` in h1 alone.

**Corrections applied in place before agreement, each dated:** §2.3's POSIX claim (h1 only; h2
fails on POSIX too without the flag); §2.3's writer and reader count of the flag; §2.4.3's Boost
floor (withdrawn; the rejection rests on its two remaining grounds); §10's instantiation line (`:96`,
not `:1296`) and the TLS gap; the provenance caveat (resolved in 1.90.0); the `s6r2-design.md`
wording.

**Precisions applied in place, none changing a decision:** §1's 1.90.0 line numbers; §2.1's
`onPeerClosed( )` span; §2.2's "provably inert" narrowed to "wherever no write is outstanding",
with the completed-but-unhandled write named; §4's h2 observation on `CHK_CANCEL_IMPL`; §5's two
call sites; §13's h2 `scheduleRead( )` note.

**Proposals, marked where they sit:** the TLS h1 case in `utf_baselib_httpclient5` (§10).

**The one choice left to the maintainer, with the tradeoff laid out rather than decided here.**
§2.4.3's per-operation cancellation was rejected on three grounds, and one of them — the Boost
floor — was false: the floor is 1.84.0, the shipping tree is 1.90.0, and both `write_op` and
`ssl::stream` honour a `terminal` emit in it. That shape would fix the hang with **no** (b), no
`SHUT_WR`, no flag, no lost `close_notify` and no Windows spelling to reason about, because it
completes the write with the one code the accounting already excuses; bound to the read handlers
too, it would also close §13's TLS composed-read item. Against it: it introduces a facility nothing
in `src/` uses, with a discipline of its own (emit on the strand, re-bind per operation), and its
right home is a generic seam rather than two drivers' issue sites — a larger design than this
defect fix. **The recommendation is to implement this design as written**: it is the smaller and
more local change, it follows the teardown idiom the library already states at
`TcpBaseTasks.h:304-305`, and its two behaviour deltas (§7.1, §4) are each argued from an invariant
this review verified — and to carry per-operation cancellation as the deferral §2.4.3 now records,
with the floor question closed. If the maintainer prefers the other shape, that is a different
design and needs its own gate; it is not a correction to this one.

**Addendum 2026-09-23, after `bb53bdd` landed on `lazari2`.** The recommendation above stands and
is stronger. §2.1 - the design's one bespoke element and the first attack point of §11 - is off its
critical path: (a) is now "call the library's own teardown one step earlier, where a cancel cannot
reach the write, gated so a deliberate TLS close keeps its `close_notify`, with the flag set first
as every `cancelTask( )` sets it". (b) is unchanged, because `shutdown_send` still breaks the
pending write with a code the accounting does not excuse. In lines nothing shrinks - four per
driver for (a), the predicate and the arm for (b) - but the argument a reviewer must hold is
shorter by its most delicate section, and the teardown `initiateClose( )` performs is now the one
`PeerCloseErrorCodes_*` guards on every platform. Option 2's case is untouched by `bb53bdd`: it
still buys "no (b), no flag, no lost `close_notify`, and §13's TLS-read item" at the price of a
facility nothing in `src/` uses and a seam of its own. The balance therefore moves toward Option 1.
Two things `bb53bdd` does not change and this addendum does not claim: nothing was run here, and
§13's TLS composed-read item - an ending observed while we are already tearing down is ours, not
the peer's - is still owed its one-line gate on `! isClosing( )`, whichever option is taken.

**Unchanged from §12:** nothing was run here, so every number is the lane's; the h2 red's
reproducibility; Windows.

---

## 15. Implementation, 2026-09-23

**Implemented on `teardown-impl` at `849e767`, in the lane1 worktree, clang debug only.** The status
line at the top of this document and §11's "Not agreed" are the status at design time and are left
as written, in the house pattern of `s6r1-design.md` §11b.

**Base.** The design names `s6r2` @ `b100c2a` and it had to: h1's `m_isWriteInFlight` exists only
there, so the h1 half of (a) cannot be written on `lazari2` at all. But the design's own corrections
turn on `bb53bdd`, which is on `lazari2` and not on `s6r2`. So the branch is `lazari2` @ `efff61f`
with `s6r2` merged into it (`c2af9d2`), one conflict, in `utf_baselib_httpclient/notes.txt`, where
both sides append test recipes to a file whose first line says each slice appends to it; resolved as
the union. `TcpBaseTasks.h` is untouched by that merge, so `bb53bdd`'s `shutdownSocket( )` stands as
written. **The fix therefore carries S6R.2 with it, which is a sequencing fact the orchestrator has
to decide about and not one this lane could settle.**

### 15.1 The form (a) took, and one reason this design does not give

§2.1's superseded block and §14's addendum both point at the helper, and the helper is what landed:
`TcpSocketCommonBase::shutdownSocket( base_type::getSocket( ) )`, without `force`, inside each
driver's existing guard and before its existing `cancel( )`, gated on `m_isWriteInFlight`, with
`TcpSocketCommonBase::m_wasSocketShutdownForcefully = true` on the line before it — the idiom of
`TcpStrandedStreams.h:224` and `Http1ConnectionTask.h:1565`. The existing `cancel( )` stays
unconditional and after, so the ungated path is byte-identical to today.

**The reason the design does not state, found by reading `shutdownSocket( )` at `bb53bdd` rather
than taking §2.1's quotation of it:** the helper opens with `if( ! socket.is_open( ) ) return;`. §5
says h2's looser `isSocketCreated( )` guard is safe because "`shutdown( )` on a created-but-closed
socket returns `bad_descriptor` and is discarded like every other code here". That is true of the
bespoke line §2 first proposed and **false of the helper** — `checkSocketError( )`'s
`bad_file_descriptor` arm is `BL_RIP_MSG`, a fatal RIP, not a discard. The `is_open( )` guard makes
the question unreachable, so the helper is strictly safer here than the line it replaced, and §5's
sentence should be read as an argument about the rejected shape.

### 15.2 (b), and the invariant re-derived

Both halves landed as designed: `const bool isOurOwnTeardown = ec && base_type::isClosing( );`
before h1's prolog with `if( ! isOurOwnTeardown ) { CHK_EC }` and `CHK_CANCEL_IMPL( )` outside it,
and one arm placed first in h2's `onWrite( )` that does nothing.

§3's invariant was re-derived from the source rather than read back, because (b) is built on it and
§11 names it as the second thing to attack. `m_closing` is **private** and `grep` over all of `src/`
finds exactly three writers, all in `MultiOperationTask.h`: `beginClose( ):243`, which sets
`m_closingDeliberate` with it; `onOperationCompleted( ):367`, inside `if( eptr && ! m_firstError )`
and after `m_firstError = eptr` at `:364`, in the same critical section; and
`scheduleNothrow( ):409`, which clears it with `m_closingDeliberate` and `m_firstError`. So
`m_closing && ! m_firstError` can only have come from `beginClose( )`. **The invariant stands.**

### 15.3 The (a)-only intermediate, which §8.1 called the only direct evidence for (b)

Measured, a64 clang debug, ten runs of each state, `Http1Driver_WriteInFlightRefusesReuseTests`:

| driver | assertion one, "a write nothing woke" | assertion two, "did not end clean" | wall |
|---|---|---|---|
| cancel only | **2 of 10 red** | not reached | 5314 ms on the red runs |
| (a) alone | 0 of 10 red | **7 of 10 red**, every one `Broken pipe [system:32 at reactive_socket_send_op.hpp:136]` | 45-55 ms on every run |
| (a) + (b) | 0 of 12 | 0 of 12 | 27-50 ms |

The timing column is the second half of the evidence and was not anticipated here: with (a) alone
the red runs no longer take the 5 s bound at all. (a) converted the hang into a prompt failure, and
(b) removed the failure — which is §3's sentence, measured.

**§12's "every number is the lane's" is now partly discharged.** The `Broken pipe` text and the
bimodal 5 ms / 30000 ms outcome were reproduced here independently. The 2-of-10 red rate is lower
than the lane's 4-5 of 10; the case is a genuine race and a rate is not a premise, which §12 already
said.

### 15.4 The h2 case, and what §8.3 got wrong about it

**§8.3's door is right and its lever is wrong.** The case is
`H2Driver_PeerHalfClosesWithAWriteInFlightTests`, through door 2 exactly as §5 and §8.3 require: a
peer that never reads, then `shutdown( shutdown_send )`. §12's open question — "whether the peer's
receive buffer can be made small enough" — is **settled: yes**, with two corrections.

1. **§8.3 says "a large DATA upload leaves `async_write` outstanding", and the write is not large.**
   `Session::produce( )` places **one DATA frame per write** — `bodyBytesWanted( )` never offers
   more than `SETTINGS_MAX_FRAME_SIZE` — so the writes are 16384 + 9 octets each and not one
   window-sized buffer. Measured: `16467, 16393, 16393`. The body's size is therefore irrelevant
   beyond exceeding the window; what has to be small is the pipe.
2. **The receive buffer must be shrunk on the ACCEPTOR, not on the accepted socket.** An accepted
   socket inherits the listening socket's buffer sizes and the receive window is advertised during
   the handshake, so a shrink applied after `accept( )` clamps the buffer but arrives after the peer
   has been told it may send more. Measured: with the post-accept set alone the driver got three
   16.4 KB frames away before blocking; with the acceptor-level set it blocks on the first. The
   driver's own send buffer is shrunk too, from `onWriteScheduled( )`, which runs on the strand
   immediately before each `async_write`.

The rendezvous is `onWriteScheduled( )` and not a sleep: it is called on the strand just before
`async_write` is issued, and the read completion carrying the peer's FIN cannot be dispatched until
that strand handler returns — so by the time `onRead( )` runs the composed write is in flight, by
the strand's ordering rather than the scheduler's.

Same three states, ten runs each: unfixed **5 of 10 red** on assertion one; (a) alone **3 of 10
red** on assertion two, every one `Broken pipe`, every run under 45 ms; (a) + (b) **0 of 12**.

**It landed in a new module, `utf_baselib_h2client6`.** `utf_baselib_h2client2` measured **38.1 MB**
a64 clang debug with its fourteen cases — at `src/utests/AGENTS.md`'s 40 MB target, and the size at
which `utf_baselib_h2client` itself was split (37.1 MB). The new module is 33.1 MB and
`utf_baselib_httpclient7` is unchanged at 30.0 MB. The second reason is not size: the peer is a raw
socket that refuses to read, and `Http2TestServerT` reads its socket continuously — a peer that
reads is a second waker for the write under test. `RawFrameScriptPeer` was considered and does not
fit either: its script is fixed at construction and this case has to half-close on a rendezvous the
driver raises.

### 15.5 What was NOT done, and is still owed

- **§10's TLS run.** The gap §10 established is not closed. No case in the suite closes an h1
  connection over TLS, and §10's proposal — one keep-alive GET through `utf_baselib_httpclient5`'s
  `TlsPeerT` with the ALPN server preference set to `http/1.1` — was **not** implemented here. So
  §2.3's `close_notify` reasoning remains inference on both drivers, and the flag is what makes the
  question unreachable rather than answered. This is the largest thing this change-set does not have.
- **§10's release and whole-suite gate**, which are the orchestrator's by the worktree split.
- **Windows**, per §10 and §12.
- **§13's list**, untouched: h1's missing peer-close arm, the composed TLS read that can slip a
  cancel the same way a composed write does, `scheduleNothrow( )`'s task-lock call, and h2's
  `scheduleRead( )` accounting guard. The new module is the natural home for the first two, and its
  own header says so.

### 15.6 The test gate

`scripts/utests/check_split.sh` tier 1 is RED against `notes/reviews/major/update_2026/baseline`,
which predates these modules entirely — every S6R.2 case reads as ADDED there. Re-run against the
merge commit `c2af9d2`, so that the only delta is this change, it reports four items and each is
intended: the new h2 case ADDED (a new case is unjudgeable by a differential, by construction); two
`C2 case BODY CHANGED` for the h1 barrier cases, one of which gained §8.2's assertion and both of
which gained the comment corrections §8.1 and §9.1 owe; and one `C6 helper member LOST`, which is
the same comment edit — C6 identifies members by text hash, and the block is present at the same
line in both manifests. Tier 2 needs an x86 debug tree and tier 3 a current baseline; neither exists
in this lane. Assertion counts, which is tier 3's substance for the cases touched:
`Http1Driver_WriteInFlightRefusesReuseTests` 7, unchanged, and its sibling 6, which is +1 for §8.2.

## 16. Implementation review, 2026-09-23 - the second gate

**Reviewer: Claude Fable 5.1, on `lazari2` in the main worktree, reading the staged merge of
`teardown-impl` @ `96071bb` (`849e767` is the fix, `c2af9d2` the S6R.2 merge).** Opened at the
signature and read to the end, at the working tree: h1's `onStartRequest( )`, `onWriteCompleted( )`,
`isCleanEndOfStream( )`, `onPeerClosed( )`, `onReadCompleted( )`, `finishStream( )`,
`closeConnection( )`, `chkArmIdleTimer( )`, `cancelIdleTimer( )`, `onIdleDeadline( )`,
`initiateClose( )`, `scheduleTask( )`, `cancelTask( )`, `shutdownOnStreamExecutor( )` and
`onTaskStoppedNothrow( )`; h2's `scheduleRead( )`, `isPeerClosed( )`, `onRead( )`,
`onPeerClosed( )`, `onWriteScheduled( )`, `pumpWrites( )`, `onWrite( )`, `cancelTimers( )`,
`armDrainDeadline( )`, `onDrainDeadline( )`, `closeGracefully( )`, `chkFinishClose( )`, the session
start, `initiateClose( )`, `cancelTask( )` and `onTaskStoppedNothrow( )`; `MultiOperationTask.h`
whole; `TcpBaseTasks.h`'s `shutdownSocket( )` with its `checkSocketError( )` lambda,
`isExpectedSocketException( )`, both `onTaskStoppedNothrow( )`, both `cancelTask( )`,
`isChannelOpen( )` and `isSocketCreated( )`; `TcpSslBaseTasks.h`'s `cancelTask( )`,
`isExpectedException( )`, `scheduleTaskFinishContinuation( )`, `onTaskStoppedNothrow( )`,
`beginProtocolShutdown( )`, `onShutdownCompleted( )`, `isShutdownNeeded( )`, `attachStream( )`,
`getSocket( )` and the SSL `isChannelOpen( )` / `isSocketCreated( )`; both stranded policies'
`cancelTask( )` and `shutdownSocketOnStrand( )`; `TaskBase.h`'s handler macros, `notifyReady( )`,
`requestCancelInternal( )`, `isCanceled( )`, `isFailed( )` and `scheduleNothrow( )`; `NetUtils.h`'s
three predicates; `Session.h`'s `wantsWrite( )`, `produce( )`, `bodyBytesWanted( )` and
`writeOneDataFrame( )`; `ClientConnectionTaskBase.h`'s connect deadline; `HttpClientRequestTask.h`'s
`connectionFailureCause( )` and `answerOnClosed( )`; `TestHttp1DriverWriteBarrier.h` and
`TestHttp2DriverWriteBarrier.h` whole; `Http1DriverTestUtils.h`'s `runExchange( )`; the new
module's `Main.cpp`, `notes.txt` and marker; `utf_baselib_httpclient7/notes.txt`; the
`s6r2-design.md` correction diff; `src/utests/AGENTS.md`; and, outside the repo, the lane's own
journal (`http2-l0-state/lane1.md`, the 2026-09-23 section) and `teardown-validate.sh`. Nothing
was built or run. The one measurement made independently here is the object sizes, read off the
lane worktree's build tree (below).

**Verdict: the implementation is correct and matches the design as gated. Agreed.** Both halves
landed in the shape §14 agreed to, on both drivers; the invariant (b) rests on stands when
re-derived from the source rather than read back; the three design errors the lane reports are
real and its corrections are right, with one wording precision; the (a)-only table is a sound
demonstration that (b) is load-bearing, with the statistical caveat stated below; the h2 case is on
door 2 and could not have been built on door 1; the new module was necessary; the `httpclient3` red
is pre-existing and its mechanism is confirmed, though it is better described than the lane
describes it; the TLS gap is a gap and is acceptable to land on a gate, for the reason given at
item 7. Four things neither the design nor the lane noticed are at item 8; one of them is a stale
"known red" note that will mislead the next reader and should be corrected before or with the
merge. None of the four is a defect in the code.

### 16.1 (a) matches what the design left

`Http1ConnectionTask.h:1556-1567` and `Http2ConnectionTask.h:2599-2610`: inside each driver's
existing guard (`isChannelOpen( )` / `isSocketCreated( )`, unchanged), before the existing
unconditional `cancel( ec )` (`:1566`, `:2609`, unchanged), gated on `m_isWriteInFlight` (`:1558`,
`:2601`), the flag on the line before the call (`:1560`, `:2603`), and
`TcpSocketCommonBase::shutdownSocket( base_type::getSocket( ) )` with no `force` (`:1562`, `:2605`).
h1's `cancelIdleTimer( )` (`:1577`) and h2's `cancelTimers( )` (`:2575`) are where they were.

**No-`force` is right, read at `TcpBaseTasks.h:241-368`:** the only thing `force` adds is the
`linger( false, 0 )` block at `:303-321`, and it is the default; the rest of the function -
`shutdown( shutdown_send, ec )` at `:357` and `cancel( ec )` at `:364`, each through
`checkSocketError( )` - runs either way. Passing `force` would add one `setsockopt( )` that changes
nothing. The cleartext `TcpSocketAsyncBase::onTaskStoppedNothrow( )` (`:628-643`) and its TLS twin
(`TcpSslBaseTasks.h:520-546`) already call the helper this same way, so `initiateClose( )` now
performs the library's own task-finish teardown one step earlier - which is §14's addendum in one
sentence.

**The flag's placement is right, and it is not load-bearing within the call.** `shutdownSocket( )`
is `static` and reads no task state, so nothing between `:1560` and `:1562` can observe the flag;
its readers - `isShutdownNeeded( )`'s first line (`TcpSslBaseTasks.h:669`),
`scheduleTaskFinishContinuation( )` (`:476`) and `TcpSocketCommonBaseT::onTaskStoppedNothrow( )`
(`TcpBaseTasks.h:123`) - all run on the terminal path, which cannot be taken while the write is
still pending. "Set first" therefore mirrors the idiom of the stranded policies
(`TcpStrandedStreams.h:224`, `TcpSslStrandedStreams.h:226`, h1's own `cancelTask( )` at `:1653`),
where it IS load-bearing because the shutdown is posted; here everything is synchronous and either
order is correct. Recorded so nobody "fixes" the order in either direction.

**The gated path's second `cancel( )`** (the helper's at `:364`, then the driver's own) is a no-op
against an empty reactor table - the design allowed either shape and this is the one-line diff.

### 16.2 (b) matches, in both drivers, and the invariant re-derived

h1 `onWriteCompleted( ):745-832`: `const bool isOurOwnTeardown = ec && base_type::isClosing( );` at
`:779`, before `BL_TASKS_HANDLER_BEGIN( )` at `:781`; `if( ! isOurOwnTeardown ) { CHK_EC }` at
`:820-823`; `CHK_CANCEL_IMPL( )` at `:825`, outside the guard. The flag clear, the zero-octet
answer and the two storage clears (`:791-818`) are untouched and stay ahead of the guard.
h2 `onWrite( ):1638-1745`: the new arm is the FIRST arm of `if( ec )` (`:1686-1691`), its body is a
comment, and the existing `isPeerClosed( )` arm follows it as `else if`.

**The invariant, from the source and not from §15.2.** `MultiOperationTask.h` is 429 lines and was
read whole. `m_closing` is declared under the `private:` label at `:90` (members `:112-119`); the
class has no `friend`; a derived class cannot write it. Its writers are three: `beginClose( ):243`,
which sets `m_closingDeliberate` in the same guard at `:244`; `onOperationCompleted( ):367`, which
is inside `if( eptr && ! m_firstError )` (`:344`) and inside `if( ! isSelfInflictedAbort )`
(`:362`), two lines after `m_firstError = eptr` at `:364`, under the one `BL_MUTEX_GUARD` taken at
`:335`; and `scheduleNothrow( ):409`, which clears it together with `m_firstError` at `:413` under
the guard at `:406`. So `m_closing && ! m_firstError` can only have been produced by
`beginClose( )`, and `isClosing( )` (`:258-263`) reads `m_closing` under the same leaf lock. **The
invariant stands in the direction (b) needs**, and `isClosing( )` true partitions exactly as §4.2
says: no first error - deliberate; a first error - `:344` would have discarded this one.

**One precision to §4.2 and §4.3, which neither the design nor the lane states.** The invariant
gives *deliberate*, not *self-inflicted*. On a deliberately closing task the failed write's code is
almost always the one our own `shutdown_send` produced - but a genuine transport error that
completes the write in the same instant (the peer's RST racing our teardown) is swallowed by the
same arm, where before this change it was recorded as the task's first error and the task
completed FAILED. `MultiOperationTask.h:310-311` states the mix-in's contract as *"a genuine I/O
failure which arrives while the task is closing is still the task's error"*, and the two write
handlers now narrow it. It is acceptable, and it is stated here so §4.3's *"no genuine error is lost
that was previously reported"* is read with the qualifier it needs: on every deliberate-close path
the sink has already been answered - h1's `finishStream( )` releases it before `closeConnection( )`,
h2's `onPeerClosed( )` and `chkFinishClose( )` close every stream before `beginClose( )` - so
`connectionFailureCause( )` (`HttpClientRequestTask.h:1186`) has no request task left to chain the
lost error into, and the only observable is the connection task's own `isFailed( )`, on a connection
being discarded, for a code (`connection_reset`, `broken_pipe`) that `isExpectedSocketException( )`
already classed as expected. The h2 driver already ends gracefully on a peer close seen by the write
side; this makes h1 consistent with it in the one window where both are true.

**The window between `:779` and `:823` is closed**, as §14.2 argued: `m_closing` is monotonic
within a run, and every writer runs on the strand except `scheduleRead( )`'s catch, which precedes
any write. `isClosing( )` at `:779` runs outside the task lock and takes the leaf lock only;
`isClosing( )` at h2 `:1686` runs inside the handler body, task lock then leaf lock, the established
order.

### 16.3 The three design errors, each verified

1. **`shutdownSocket( )` and `bad_file_descriptor`.** True, with a wording precision.
   `TcpBaseTasks.h:248` is `if( ! socket.is_open( ) ) return;`; `checkSocketError( )` at `:253-297`
   routes `bad_file_descriptor` and `WSAEBADF` to `BL_RIP_MSG` at `:285`. So §5's sentence -
   *"`shutdown( )` on a created-but-closed socket returns `bad_descriptor` and is discarded like every
   other code here"* - was an argument about the bespoke line and does not transfer to the helper.
   **But "strictly safer" (§15.1, and the lane's journal) overstates it.** The bespoke line discarded
   `ec` unconditionally and had no fatal arm at all; the helper HAS a fatal arm and is safe only
   because `is_open( )` makes it unreachable - asio's `is_open( )` is "the native handle is not
   invalid", and a handle can be bad while open only if something closed the descriptor behind
   asio's back, which nothing here does. The correct statement is: *the helper is not less safe than
   the line it replaced, and it is pinned by the two `PeerCloseErrorCodes_*` control cases, which the
   line was not.* The commit message's own sentence on this ("h2's looser `isSocketCreated( )` guard
   would otherwise have reached `checkSocketError( )`") conflates the two shapes - a bespoke line
   never reaches that lambda - and is history; §15.1 is the accurate version. Also read: h2's
   trailing `cancel( ec )` at `:2609` stays under `isSocketCreated( )` and discards its code, exactly
   as today, so a created-but-closed socket on that path is the same no-op it was.
2. **One DATA frame per write.** True, and the reason is the pull contract rather than
   `produce( )`'s loop. `produce( ):913-947` loops `writeOneDataFrame( )` until
   `firstSendableStreamId( )` is the connection, so it WOULD place several frames if several were
   pending; what bounds it is `bodyBytesWanted( ):1067-1099`, whose `room` is
   `min( m_peerMaxFrameSize, windows ) - pendingBody.size( )`, and `provideBody( )`'s `BL_CHK` at
   `:1142-1145` refuses more. So at most one frame's worth is ever pending per stream between two
   pumps, and one `produce( )` places one DATA frame per stream. The measured `16467, 16393, 16393`
   is `16384 + 9` with 74 octets of something ahead of the first. §8.3's "a large DATA upload leaves
   `async_write` outstanding" therefore rested on the wrong quantity, and the lane's correction -
   the PIPE is what must be small - is the right one.
3. **`SO_RCVBUF` on the acceptor.** True in mechanism on Linux - an accepted socket inherits the
   listener's buffer sizes, the initial window is chosen from the listener's `sk_rcvbuf` at SYN-ACK
   time, and setting the option after `accept( )` clamps the buffer but not the window already
   advertised - and the lane measured it (three frames away versus none). The test sets both
   (`TestHttp2DriverWriteBarrier.h:230-239` and `:360-365`), which is the right belt and braces.

### 16.4 The intermediate evidence is sound, with one statistical caveat

The wall-clock column is the corroboration the lane claims, and it is the discriminator the exception
text cannot be: on Linux the peer's RST and our own `shutdown_send` both surface as `EPIPE` from the
next `send( )`, so `Broken pipe [system:32 at reactive_socket_send_op.hpp:136]` alone cannot say
which waker produced it. A red run at 5314 ms is the 5 s bound plus the harness's release - the
peer's RST freeing a write nothing else woke; a red run at 45-55 ms with the peer still parked can
only be the driver's own teardown having reached the write. So the (a)-alone row says exactly what
§8.1 wanted said: (a) converted the hang into a prompt failure, and (b) removed the failure.

The (a)-alone 7 of 10 exceeding the cancel-only 2 of 10 is not a contradiction and is worth
explaining once: a registered send step is reaped by `cancel( )` with `operation_aborted` (excused),
but after `shutdown( SHUT_WR )` the kernel reports the socket writable at once, so a registered step
woken by the reactor thread between the helper's `shutdown( )` at `:357` and its `cancel( )` at
`:364` completes `EPIPE` instead. (a) therefore poisons the between-steps window AND races the
cancel for the registered one, and the `EPIPE` count is expected to be at or above the hang count.
The h2 table's 3 of 10 against 5 of 10 runs the other way and is sample noise at ten runs; the
design's §12 already said a rate is not a premise.

**The caveat.** Assertion two's evidence is conclusive - 7 of 10 red to 0 of 12 green cannot be
chance. Assertion one's h1 evidence is thin on its own - 2 of 10 to 0 of 10 is consistent with luck
about one run in nine - and rests on the mechanism read in `write_op`, on the wall-clock signature,
and on the h2 case's 5 of 10 to 0 of 12, which is not thin. §10 asked for ten runs a side and got
them; the orchestrator's release gate, which runs the case again on two more toolchains, is the
right place to thicken it, and no further lane run is asked for.

### 16.5 The h2 case is on door 2, and the module was necessary

**Door 2, by construction of the peer and by the strand.** `HalfClosingPeer` accepts, never reads,
and on request does `shutdown( shutdown_send )` (`:374`) - FIN with the receive queue untouched, so
the driver's `onRead( )` gets `eof` → `isPeerClosed( )` → `onPeerClosed( ):1557-1572` →
`beginClose( ):1571` → the read handler's own epilog runs `initiateClose( )`. The write is in flight
when it does: `BlockedWriteProbeT::onWriteScheduled( )` (`:499-534`) signals from inside
`pumpWrites( )` at `:1620`, on the strand, before `async_write( )` at `:1626`; the case then asks for
the half-close (`:660`) and waits for it (`:662`), and the read completion carrying the FIN cannot
be dispatched until the handler that is issuing the write returns. `taskEndedUnaided` (`:671`) is
read with the peer still holding its end; `release( )` is at `:680`. The two assertions are §8.3's,
in §8.1's order (`:711-722`), behind a precondition that the blocked write really was a DATA frame
(`:706-709`), which is the right thing to assert rather than assume.

**It could not have been built on door 1.** `chkFinishClose( )` is called at `:1600` and `:1613`
and nowhere else, both inside `pumpWrites( )` and both after its return at `:1593` while
`m_isWriteInFlight` is true; `beginClose( )` has exactly two h2 callers, `:1571` and `:2290`. So a
graceful close with a write outstanding never reaches `beginClose( )` at all - it waits in
`pumpWrites( )` for a write the peer controls, bounded only by `armDrainDeadline( )`, whose expiry
cancels the task (`onDrainDeadline( ):2199-2232`). A case on that door would be green against the
unfixed driver and would be testing the drain deadline. Door 3 would have served too, but door 2 is
the one that turns a clean peer close into a FAILED connection without (b), which is the one worth
pinning.

**Necessary, not convenient.** The sizes were read off the lane worktree's own build tree rather
than taken from the record: `utf_baselib_h2client2` is 39,937,968 bytes (38.1 MiB) a64 clang debug,
`utf_baselib_h2client6` 34,677,624 (33.1 MiB), `utf_baselib_httpclient7` 31,466,952 (30.0 MiB),
all three matching §15.4 to the tenth. `src/utests/AGENTS.md` says of a module at or near the 40 MB
target: *"do not add to it"*. Its "prefer a module comfortably under target" alternative was checked
against the lane journals' own figures: `h2client3` 39.7 MB and `h2client5` 37.6 MB are at the same
wall; `h2client4` has room at ~26 MB but its `Main.cpp:52` says *"No other case here may touch the
driver"*. So no sibling with room and suitable fixtures existed, and the fixture argument - a raw
peer that refuses to read, which `Http2TestServerT` must not become - stands on its own as well.
The 21 MB TU floor is the cost and it was paid knowingly.

### 16.6 The `httpclient3` red: pre-existing, mechanism confirmed, and it is not a "flake"

The lane's mechanism is right and is confirmed by reading, not by its numbers.
`runExchange( ):1266` reads `result.stateAfterResponse = driver -> state( )` at `:1349`
immediately after `sink -> waitForClosed( )` at `:1342`, with the peer parked. `waitForClosed( )`
is satisfied by `finishStream( )`'s `onClosed`, and `finishStream( )` decides the state it publishes
at `:1300-1301`: `isReusable = isConnectionUsable && ! isClosing( ) && ! m_isWriteInFlight`. The
request's write completed speculatively inside `async_write_some( )` on the strand and its handler
was posted, but under a multi-threaded `io_context` the read completion carrying the peer's answer
can be enqueued on the strand ahead of it when the peer answers within that window - a window a
concurrent compile widens from microseconds to hundreds of milliseconds. Then `onReadCompleted( )`
runs first, the parser completes, `finishStream( )` sees `m_isWriteInFlight` still true, publishes
`Draining`, and the case's `Ready` assertion is red. The write handler then runs with `ec` = success
(the write had completed), `initiateClose( )` fires the new branch with nothing left to poison, and
the task ends clean on both sides of the fix - which is why the lane sees the same assertion and the
same task-succeeded on both, 2 of 20 with and 3 of 20 without.

**Two sharpenings.** First, this is the exact window §2.2's *Precision, 2026-09-23 review* names -
*"the flag is also true for a write that has completed at the socket but whose handler has not yet
run"* - so the design predicted it and did not know it was already visible. Second, it is not test
flakiness and it is not S6R.2 "flakiness"; it is a **benign, pre-existing H01 defect**: a spurious
refusal of reuse for a connection whose write has in fact completed, costing the pool a connection
and nothing else - the refusal is the safe direction, since `m_isWriteInFlight` is also the storage
guard for the buffers `async_write( )` was given. The case is right to be red on it. The shape of the
fix, for the record and for its own change-set: `finishStream( )` defers the verdict when the write
is still in flight and the connection is otherwise reusable, and `onWriteCompleted( )` publishes
`Ready` and arms the idle timer on success where the stream has already ended. It goes on §13's list
below as S6R.2's, and `teardown-validate.sh` already carries the right instruction to the release
gate: a red on that one assertion in `utf_baselib_httpclient3` is this defect and not this
change-set's.

### 16.7 The TLS gap: a gap, and acceptable to land on a gate

What §10's TLS run would have measured, and what it would not. The ungated path of both
`initiateClose( )` overrides is byte-identical to today, so a TLS control case with no write
outstanding (§10's `http/1.1` ALPN proposal) is a regression guard on unchanged code, not a
verification of the new lines. The new lines run under TLS only on the gated path - a close with a
write outstanding - and there they do two things: skip the `close_notify` (§7.1, by the flag at
`isShutdownNeeded( ):669` and `scheduleTaskFinishContinuation( ):476`, both read here), and produce a
write error that surfaces through `ssl::stream` and is classified by the task's state and not its
code (§4.1). Neither has been exercised on either driver. That is the gap, and §10 said to report it
rather than paper over it; it is reported.

**Why it is acceptable to land:** the risk on the gated TLS path is bounded to a `close_notify` not
being sent on a connection whose send side we have just shut - which could not have carried it -
and the flag makes the attempt unreachable rather than betting on `isExpectedException( )`'s two
lists, as §2.3's corrected reasoning requires. The classification of the surfaced error is the task's
state, which is the same on every policy. And the whole-suite gate does exercise h2's `initiateClose( )`
under TLS on the ungated path through `utf_baselib_httpclient5`'s two `ClientSessionTls_*` cases,
whose peer prefers `h2` - the one TLS control that exists today, for the driver whose edit is
identical.

**What is owed, in order:** §10's h1 TLS control case, as its own test-only change-set - small, and
the natural companion of §13's h1 peer-close arm; then the write-outstanding TLS case for h1, which
`TestHttp1DriverWriteBarrier.h`'s harness can take by instantiating the driver over
`TcpSslSocketAsyncStrandedBase` against a TLS peer that stops reading - the sibling module the h2
case's `Main.cpp` already names as the home for the composed-read item. Neither blocks this gate.

### 16.8 What neither the design nor the lane noticed

1. **`utf_baselib_httpclient7/notes.txt:6-13` still says the reuse case is "KNOWN RED, AND ON
   PURPOSE"** - "fails about half of its runs on 'the barrier left a write nothing woke'" - and
   names the very mechanism this change removes. It was written at `b100c2a` and the fix commit did
   not touch it. It is now false, and a stale "known red" note is the one kind of comment that
   causes a real red to be ignored. **Owed before or with the merge**: replace the block with the
   sentence that the case is green since `849e767` and is the regression guard for both halves. One
   file, documentation only.
2. **`TestHttp2DriverWriteBarrier.h` carries §8.3's wrong lever in its own text.** Its header comment
   at `:59-60` ("the driver's second write is the whole window's worth of DATA"), the enum comment
   at `:82-84`, the rendezvous comment at `:639-641` and both failure messages at `:655` and `:708`
   say "a window's worth", while `BIG_WRITE_THRESHOLD`'s comment at `:100-108` says, correctly, one
   frame per write and 16.4 KB. The assertion itself is right (`>= 8 KB`). Owed a comment pass so
   the file does not contradict itself in the place a failing run will print.
3. **The precision to §4.2/§4.3 at 16.2** - the arm swallows a genuine concurrent error on a
   deliberately closing task, narrowing the mix-in's stated contract for the two write handlers -
   acceptable, and now written down.
4. **The `httpclient3` red is a benign H01 defect and not flakiness** (16.6), with the fix shape
   recorded. It is S6R.2's, it predates this change, and it is owed its own change-set; until then
   every release gate will carry its 10-15%.

Also checked and found clean, so they are not rediscovered: `m_wasSocketShutdownForcefully` is
`protected` (`TcpBaseTasks.h:69`) and the spelling `TcpSocketCommonBase::m_wasSocketShutdownForcefully`
is the one h1's `cancelTask( )` already uses; an external `cancelTask( )` landing on a task whose
flag this change set is converted to `operation_aborted` by `onTaskStoppedNothrow( ):123` exactly
as a task whose flag the cancel set, so nothing changes for the cancelled case; h1's `onIdleDeadline( )`
(`:1491-1513`) re-checks `activeHandle( )`, so the idle close cannot reach the new branch with a
request's write outstanding; h2's connect deadline is not an accounted operation and is disarmed in
`onTaskStoppedNothrow( )` (`ClientConnectionTaskBase.h:644`), so the error branch of `onWrite( )` not
clearing `m_isPrefaceWritePending` - pre-existing - cannot hold the terminal path; the sibling case's
assertion count is 6 (`+1`, `:456`) and the reuse case's 7, by reading; the `s6r2-design.md`
corrections name what landed and nothing more; and the design's status line and §11's "Not agreed"
are left as written in the house pattern, as §15 says.

### 16.9 Additions to §13, owed and out of scope here

- **H01's spurious reuse refusal in the completed-but-unhandled write window** (16.6), S6R.2's; the
  fix shape is stated there. It is what makes `utf_baselib_httpclient3` red one run in eight under
  load.
- **The stale `httpclient7/notes.txt` block** (16.8.1) - to be corrected with or before the merge.
- **The h2 test's self-contradicting lever wording** (16.8.2) - comments only.
- **§10's two TLS cases** (16.7), the control first.

**Sequencing, for the orchestrator and not this review:** merging `teardown-impl` merges S6R.2,
which was reviewed and agreed on its own gate contingent on three `src/` fixes the maintainer applied;
nothing here re-reviews it, and the flake at 16.6 is the one S6R.2 fact this review adds to that
record. The release pass (`teardown-validate.sh`) and the whole-suite gate are the orchestrator's,
and `Http1Driver_WriteInFlightRefusesReuseTests` and `H2Driver_PeerHalfClosesWithAWriteInFlightTests`
are the two cases whose green there is the fix's, per §10.
