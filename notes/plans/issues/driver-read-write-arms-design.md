# The three unscheduled driver arms — design

**Date:** 2026-09-23. **Status:** **AGREED 2026-09-23 — see §10.** Reviewed (§9), corrections applied
in place and each dated, and both shape choices made by the maintainer: **#7 takes shape (ii)** and
**#12 takes the driver-local A4**.

| | State, 2026-09-23 |
|---|---|
| **A2** (#7) | **implemented** `a2-write-peer-close` @ `04bcb6f`, **reviewed §12 and accepted** — no conditions; owed items are §12's list, none blocking |
| **A3** (#13) | **implemented** `a3-h2-accounting` @ `372a397`, **reviewed §11 — agreed with two conditions**, both returned to the lane: three code comments carrying false premises, and the `postCommand( )` residual, which the maintainer decided is **fixed now, not deferred** |
| **A1** (#8) | **A1-cleartext (face 3) implemented** `a1-cleartext` @ `6632469`, **reviewed §13 and accepted** — one comment condition at the merge (§13.3), the Windows narrowing **deferred to the matrix** (§13.3); **A1-tls (faces 1–2) implemented** `a1-tls` @ `db30f53`, **reviewed §15 and accepted** — no condition at the merge; face 1's gate lands as defence-in-depth by the maintainer's decision, its control and its trigger recorded (§15.5). Re-specified in §12.5 after A2 showed the recorded gate worked only by accident; maintainer agreed 2026-09-23 — including that the author's own "write records, read consults" was necessary and **not sufficient**, §12.5's step 3 being what makes it deterministic |
| **A4** (#12) | agreed, **not started** |

Change-set **A** of the four items that
[`astra-remediation-owed-work.md`](astra-remediation-owed-work.md) records as *unscheduled* — owed a
slot that was never created. Three defects, all pre-existing, all in the two HTTP drivers' read and
write paths, each landing as **its own change-set with its own red**. The fourth unscheduled item,
`TaskBase::scheduleNothrow( )`, is deliberately **not** here; §4 says why.

Every line number below was re-derived on `lazari2` at `a723cee`. S6R3-COMMON's warning applies:
they drift, the anchors are the function names.

---

## 1. Why these three and not the other one

The owed list's four unscheduled items were proposed as two change-sets, **A** = the two
classification arms (#7, #8) and **B** = the two accounting items (#12, #13), on the reasoning that
each pair shares a reviewer's context. **Reading them refuted that pairing.**

#13 is h2's `scheduleRead( )` missing the `try` / `catch` that h1's identical function carries, with
a comment above h1's explaining exactly why it is there. It is the same subsystem, the same two
drivers and the same kind of narrow pre-existing gap as #7 and #8, and h1's function is the worked
example. Nothing justified holding it in a second change-set.

#12 is not an accounting gap at all once its chain is followed to the end — it is a **lock-scope
defect in the task core**, and §4 establishes that. *Review 2026-09-23: the chain holds, the
framing does not — §4 shows the lock scope is deliberate and that the core already supplies the
off-lock route, so #12 may come back into this design as a driver-local **A4** (§6).*

---

## 2. #7 — h1's write path has no peer-close arm

### 2.1 The defect

h2's `onWrite( )` classifies a write that failed because the peer went away, while we are not
closing, as an ordinary ending: `isPeerClosed( ec )` → `onPeerClosed( )`. h1's `onWriteCompleted( )`
has no such arm, so the same event is a **task failure** there. h2's own comment says what that
costs — *"which made the classification depend on which handler the ending happened to reach
first"* — and h1 has precisely that asymmetry today, because S6R.2's N2 took
`net::isPeerClosedErrorCode( )` on the read path and left the write path with no classification
at all: `onWriteCompleted( )` (`:745-832`) has nothing between the teardown arm and `CHK_EC`.
Nothing there is hand-rolled; it is absent. *(Wording corrected 2026-09-23.)*

Established in [`initiate-close-teardown-design.md`](initiate-close-teardown-design.md) §4.4, which
also states why it was not folded into the teardown change-set: *"It is a real defect and it is not
this one."* That was right about that change-set and was never a reason not to schedule it.

### 2.2 The shape

§13 states it: the same `isPeerClosed( )` question h2 asks, placed **after** the teardown fix's new
`isClosing( )` arm, routing to `onPeerClosed( ec )` and `closeConnection( )`. **The review of
2026-09-23 found that line under-specified on two counts — the predicate and the shape — and both
are corrected here, in place.**

**Which predicate — corrected 2026-09-23.** Both predicates in `NetUtils.h` are read-side: eof,
`connection_reset`, and the two Windows spellings (`:353-371`, `:382-385`); no `broken_pipe` on
any platform, which teardown §14 premise 3 also states. The one write-side spelling this suite has
**measured** is `broken_pipe` — `TestHttp1DriverWriteBarrier.h:167-175`: the peer's `close( )`
with the upload unread puts a RST on the wire and the blocked write completes `system:32`. So the
arm as recorded would not fire on the measured code, and §2.3's red would stay red after the fix.
Under the rule (AGENTS.md; `NetUtils.h:333-337`) the answer is a **third predicate in
`NetUtils.h`**, say `isPeerClosedOnWriteErrorCode( )` — `isPeerClosedErrorCode( ec ) ||
broken_pipe == ec`, with its Windows spelling measured by the matrix and not assumed
(`WSAECONNRESET` and `WSAECONNABORTED` already map to the two codes the read-side predicate admits
there; `WSAESHUTDOWN` is ours and stays out). Additive API, so it rides with A2. Ask
`base_type::isStreamTruncationError( )` beside it exactly as the read path does at `:1187-1188`,
because this class is instantiated over TLS. The orderly variant stays wrong for the reason the
draft gave: the question is whether the conversation is over, not whether a retry is worth it.
**Never hand-compare the code.**

*Consequence, for the owed list and not for A2:* h2's `onWrite( )` (`:1638`) asks
`isPeerClosed( )` (`:1477-1502`), which is the read-side pair, so h2's write arm has the same hole —
a peer close that reaches the write first as `broken_pipe` goes to `CHK_EC` and fails the task.
`H2Driver_PeerHalfClosesWithAWriteInFlightTests` does not see it because its peer holds its end and
the read notices first (door 2); a peer that RSTs would.

**Corrected 2026-09-24 — this paragraph was wrong in three ways when it was written.** It was written
from h1's perspective by an author who had not read h2's structure for it, and its line numbers came
from a pre-A3 tree.

1. **"a peer that RSTs would" — it would not.** That case comes out **green**. `broken_pipe` needs a
   FIN *before* the RST (`CLOSE_WAIT` → `EPIPE`); a bare reset gives `connection_reset`, which h2
   already admits.
2. **The arm fired in the ordinary case — this point is itself corrected 2026-09-24 by the 13a/13e
   implementation review, §16.3, by reading at the source and not by measurement.** As first written
   it said `perform_io( )` performs the **write's** syscall first but **posts the read's handler
   first**, so that with both ops registered the read handler ran first, closed, and the arm could
   fire **only where no read is armed**. That is not this reactor. In Boost 1.90 `epoll_reactor::run( )`
   queues the `descriptor_state` itself; `perform_io( )` runs later, from
   `descriptor_state::do_complete( )`, which **invokes the write's completion in place** — the socket
   is built on `make_strand( )`, so that is `strand::execute( )`, which enqueues the handler and runs
   the strand's invoker inline, the inner `io_context` executor being `blocking.possibly` and the
   reactor thread `can_dispatch( )` — while the read's completion has just been **posted** to the
   scheduler queue by `perform_io_cleanup_on_block_exit`. So with both ops registered the **write
   handler ordinarily runs first**, with `isClosing( )` false, and the arm fired on every peer ending
   that reached a pending write: as 13a whenever a FIN preceded the reset, as 13e whenever the answer
   was queued. The read-first order is a narrow race — a woken pool thread reaching the strand's mutex
   before the reactor thread does — and not the rule. The two no-read-armed windows, `onRead( )`'s
   pump-then-arm pair and `onProtocolNegotiated( )`'s, are real and are what the two committed cases
   arrange; they are where the write is *alone*, not the only places it is *first*.
3. **The cited line numbers were never right.** `:1638` is `onRead( )`'s epilog and `:1477-1502` is a
   different function's comment. The anchors are the function names, as §0 says.

**And the reading found a second defect, live today** — see
[`astra-remediation-owed-work.md`](astra-remediation-owed-work.md) item **13e**. It is why the shape
matters and not only the predicate.

**Which shape — the question §13's one line does not settle.** Two shapes, and they differ in
what the caller sees.

- **(i) the recorded one:** `onPeerClosed( ec ); closeConnection( );` from the write handler.
  `onPeerClosed( )`'s own contract (`:1090-1105`) says its argument is *how the byte stream ended*,
  and a write's code can never satisfy `isCleanEndOfStream( )` (`:1085-1088`), so (i) always takes
  the unclean branch: `finishStream( writeEc, ... )`, `m_parser.reset( )` (`:1326`), then
  `closeConnection( )`. The read — armed for the life of the connection (`:1598-1602`) — then hands
  whatever the reactor still holds to `onBytesRead( )` with no parser (`:990-1000`), which treats it
  as unsolicited. An early response, a 413 answered from the head while the upload is still going —
  the one case where a server closes mid-upload on purpose — **loses its status line whenever the
  write's failure handler runs before the read that carries it.** The task no longer fails, but the
  stream's verdict still depends on which handler ran first, which is the complaint restated one
  level down.
- **(ii) excuse the write and do nothing:** `if( ! isOurOwnTeardown && ! isPeerClosedOnWrite ) {
  CHK_EC( ec ); }` — no `onPeerClosed( )`, no `closeConnection( )`, `CHK_CANCEL_IMPL( )` (`:825`)
  untouched. The read observes the same ending with the **read-side** code — reset or eof on POSIX,
  the measured spellings on Windows — and `onPeerClosed( )` classifies it once, from the only side
  that can (`:1106-1160`); `finishStream( )` then reaches `closeConnection( )` itself (`:1354`).
  Order no longer matters at all. `:828-830`'s own sentence — *"the read loop has been armed since
  the task was scheduled and the response will arrive on it"* — is (ii) applied to the failure case.

**Recommended: (ii).** Left to the maintainer, because it departs from the shape teardown §4.4 and
§13 recorded and from h2's, where `onPeerClosed( )` takes no code and pre-empting the read costs
nothing. What (ii) rests on and the red must confirm on both platforms: a peer close that failed
the write is always reported to the pending read as well, as one of the codes the read-side
predicate admits.

**Order — and the reason is now concrete, not a coincidence.** The `isClosing( )` arm runs first
because the write-side predicate admits `broken_pipe`, and `broken_pipe` is what our own
`shutdown_send` produces on POSIX — measured 7 of 10 in the *shutdown without the classification*
row at `:189-193`. So on the barrier case itself the two questions answer *yes* together **every
run**, and a peer-close arm placed first would classify our own teardown as the peer's: harmless
under (ii), `onPeerClosed( )` plus `closeConnection( )` on our own teardown under (i). The predicate
is evaluated before the prolog (`:779`), and the new arm sits inside `if( ! isOurOwnTeardown )`
(`:820-823`).

### 2.3 The red

*Amended 2026-09-23.* A peer that reads the request head, answers it, and closes while our upload
is still outstanding, on cleartext h1 — `runBlockedUpload( )`'s parked peer
(`TestHttp1DriverWriteBarrier.h`) with a close in place of the park; `runPeerCloseExchange( )`'s
`resetInstead` arm (`TestHttp1DriverPeerClose.h:96-135`) already shows the wire shape. **What the
close looks like is not a choice:** a peer that closes with our upload unread puts a RST on the wire
on every platform (RFC 2525 §2.17; `:167-175`). *Corrected 2026-09-23 by the implementation review,
§12.3 — which code each side gets is decided by an **exchange**, not by timing:* the kernel holds one
pending error and the first syscall to reach it takes it (`sock_error( )`). When the read's `recv( )`
is first, the read-side verdict is *reset, failed, not retryable*, as in
`Http1Driver_PeerResetsMidCloseDelimitedBodyTests`, and the write's code is `broken_pipe`; when the
write's `send( )` is first — which it is whenever both ops are registered, because the reactor
performs the write op first — the write's code is `connection_reset` and **the read is handed plain
`eof`**. Deterministic given the syscall order, measured both ways. The predicate admits both write
codes or the case flakes on the code rather than the race; what the read's `eof` then means is
§12.5's finding. The case asserts three
things: the task did **not** fail, the connection is not reusable, and — the assertion that
discriminates (ii) from (i) — the response head reached the sink before the reset was reported. It
must be shown red against the unfixed tree, where the task fails with the write's code whenever the
write handler runs first; making that ordering certain is the harness's job, and a run count is not
the evidence.

### 2.4 What this must not disturb

`utf_baselib_httpclient7`'s two write-barrier cases. Their four assertions are H01's and teardown
§8.1 says *"Nothing in this case should change."* **Corrected 2026-09-23:** the barrier case's
write ends with exactly the code the write-side predicate admits — `broken_pipe`, from our own
`shutdown_send` (`:189-193`) — so what keeps the new arm off it is the **arm order**, not the code:
`isOurOwnTeardown` is true there (`:779`) and the arm sits inside `if( ! isOurOwnTeardown )`. **To
be confirmed by the lane, not assumed** — this is the exact shape that sank H01's recorded fix, and
an arm that drifted ahead of the teardown arm would fail these cases on POSIX every run.

---

## 3. #8 — a composed TLS read can slip a cancel

### 3.1 The defect

`ssl::stream::async_read_some` is itself composed, so on a TLS connection the armed read can re-arm
past `initiateClose( )`'s `cancel( )` and then observe the peer's own close — reaching
`onPeerClosed( )` with a live parser, and **possibly framing a truncated close-delimited response as
a success.**

This is the read-side twin of the write defect the teardown change-set proved real and fixed. It is
**pre-existing**: neither created nor closed by that change, which is why §2.1 of that design refuses
to add a cleartext route to it.

*Precision 2026-09-23, the premise checked in the shipping Boost.* The composed read is `io_op` in
`boost/asio/ssl/detail/io.hpp` (1.90.0, `:146-200`): on `want_input_and_retry` it re-arms
`next_layer_.async_read_some( )` (`:179`) and returns. Between the transport read completing in
the reactor and that intermediate handler running on the strand, nothing of this read is
registered, and a `cancel( )` issued in that window — `initiateClose( )` runs in a strand handler's
epilog — reaps nothing. The same shape as `write_op`, which teardown §1 established. On cleartext
the same *ordering* exists — a read already completed with the peer's eof when the epilog cancels
reaches `onReadCompleted( eof )` too — but there the close is the peer's genuine one and not a
reaction to our teardown, which is why the teardown design is right to call the truncation route
TLS's. The gate of §3.2 covers both.

### 3.2 The shape

§13 gives it in one line: gate `onPeerClosed( )` on `! base_type::isClosing( )`. An ending that
arrives while we are already tearing down is **ours**, not the peer's — the read-side twin of §4's
sentence in the teardown design.

*Precisions 2026-09-23, three, and one thing the gate does not close.*

1. **The shape must swallow, not check.** `if( isEndOfStream ) { if( ! base_type::isClosing( ) ) {
   onPeerClosed( ec ); closeConnection( ); } }` — an ending observed while closing completes the
   read's operation with no error, through the epilog, the twin of the write arm's *do nothing*.
   Routing it to `CHK_EC( )` instead would make `eof` the first error of a task closing
   **deliberately** and fail the idle close.
2. **It is a live-parser change only.** `onPeerClosed( )` returns at once without a parser
   (`:1108`) and `finishStream( )` resets it (`:1326`), so every deliberate close reaches
   `beginClose( )` with the parser gone. The gate changes behaviour only where a first error was
   raised outside the read handler with a response in flight — `onWriteCompleted( )`'s `CHK_EC`,
   `onStartRequest( )`'s catch (`:724`), `chkArmIdleTimer( )`'s catch (`:1455`): teardown §2.1's
   list. With A2 in shape (ii), a peer-noticed write failure leaves that list.
3. **`isClosing( )` does not cover an external cancel, so the recorded gate does not either.**
   Neither `cancelTask( )` nor `requestCancel( )` writes `m_closing` (teardown §14 premise 2,
   re-verified: its writers are `beginClose( )`, `onOperationCompleted( )` and
   `scheduleNothrow( )`), and the read handler's `CHK_CANCEL_IMPL( )` sits in the `else` branch
   *after* the arm (`:1201`). A cancelled TLS connection whose composed read re-arms past the posted
   shutdown's cancel, and then sees the peer close in answer to the send side we just shut, reaches
   `onPeerClosed( )` with a live parser exactly as §3.1 describes. Whether the gate is
   `! isClosing( )` alone, `! isClosing( ) && ! isCanceled( )`, or the cancel check moved ahead of
   the arm, is the lane's to decide with the case in hand — and the record must say which, because
   the first is what §13 wrote down and it is not enough on its own.

*Corrected 2026-09-24 by the implementation review of A1-tls — §15.2.* Point 2's list is right as a
list and its conclusion is stale. `chkArmIdleTimer( )`'s catch runs with no parser — it is guarded on
no active handle, and the parser's lifetime lies inside the handle's. `onStartRequest( )`'s initiating
catch runs with a **live** parser, created above the write, but it clears `m_isWriteInFlight` before
completing the operation, so `initiateClose( )` shuts nothing down there, and no request octet has
left, so no response can be in flight: it cannot produce the symptom. `onWriteCompleted( )` fails the
task, with A2 in shape (ii), only on the `operation_aborted` of a cancel's reaping, on its own
`CHK_CANCEL_IMPL( )`, and on Windows on `WSAESHUTDOWN` — every one behind an external cancel. So in
this tree `isClosing( )` is true at the observation with a live parser only when `isCanceled( )` is
true as well, and the gate of this section has no red the cancel check does not also fix; it lands
as defence-in-depth on §15.5's terms. Point 3's decision is taken: `CHK_CANCEL_IMPL( )` **narrowed to
a live parser, ahead of the arm** — the other two candidates are ruled out in §15.3.

**What the gate does not close, recorded so it is not mistaken for closed.** The same cancel-miss
window has a second face. If the transport read that completed inside the window carried a partial
TLS record, the engine wants more (`io.hpp:156`), the op re-arms, and a peer that then goes silent
is never answered: `shutdown_send` cannot wake a read, `initiateClose( )` runs once, and the task
hangs — the read-side twin of the write hang, in both drivers. Per-operation cancellation (teardown
§2.4.3, §14's Option 2) is the shape that closes it; it is the deferral recorded there, not this
change-set, and it is why §3.3's ranking is by the misclassification and not by a hang this
change-set does not touch.

*Re-specified 2026-09-23 by the implementation review of A2 — §12.5. The gate above closes two of
three faces of the symptom; the third, a reset consumed by the write's `send( )`, leaves the read a
plain `eof` the gate cannot see, and A2 removed the accident that had been covering it. A1 is
proposed split into A1-cleartext (face 3, red in hand, no TLS needed) and A1-tls (faces 1-2, after
the fixture).*

### 3.3 Why this one is ranked first of the three

It is the only one of the three that can turn a **truncated response into a reported success**. #7
misclassifies an ending; #13 can hang a task. This one can hand the caller a body that is not the
body. It is also the same defect class the teardown work has already proved bites in this tree.

### 3.4 The red, and the thing that makes it awkward

The case needs a TLS connection, a close-delimited response, a cancel, and a peer close arriving in
the window — and **no case in the suite runs an HTTP/1.1 exchange over TLS through the h1 driver.**
*Verified 2026-09-23:* `httpclient3:96` is a compile-only instantiation; every TLS case in
`httpclient5` asserts `h2` (`TestClientSessionTls.h:351`); `h2client3`'s server-preference case
negotiates `http/1.1` into a recording fallback factory and issues no request
(`TestHttp2ConnectionTaskTls.h:241-287`); the contract tests use stubs; and `utf_baselib_http`'s
HTTPS cases go through `SimpleHttpTask`, not this driver. The `tls-h1-control` lane exists — a
worktree on that branch at `a723cee`, nothing landed — and is what supplies the fixture. **This
change-set is therefore sequenced after that lane's control case.** Stated as the dependency it is,
rather than discovered later.

Whether the window can be made *certain* rather than probable is the lane's first question, and the
answer decides whether this lands at all: §"A negative control is the evidence" in
`src/utests/AGENTS.md` governs, and a fix without a red does not land.

---

## 4. #12 — why it is NOT in this change-set

The record says *"`TaskBase::scheduleNothrow( )` calls `scheduleTask( )` under the task lock, so h1's
`scheduleRead( )` catch can reach `notifyReady( )` with the task lock held, which
`MultiOperationTask.h:62-67` forbids."* **The chain was followed to the end and every link holds:**

1. `TaskBase::scheduleNothrow( )` takes `BL_MUTEX_GUARD( m_lock )` and calls `scheduleTask( eq )`
   **under it**.
2. h1's `scheduleTask( )` override calls `scheduleRead( )` **directly** — not posted. Its own comment
   says why: *"The read is armed FIRST and unconditionally."*
3. `scheduleRead( )`'s `catch` calls `base_type::onOperationCompleted( std::current_exception(),
   false )` **inline**.
4. `onOperationCompleted( )` is the one function `MultiOperationTask.h:62-67` says must run outside
   the lock, because it is where the terminal `notifyReady( )` is reached — and
   `BL_TASKS_HANDLER_END_MULTIOP( )` exists precisely to place it outside the
   `BL_MUTEX_GUARD( TaskBase::m_lock )` that `BL_TASKS_HANDLER_BEGIN( )` opens.

So on that path the pending count reaches **zero** — nothing else is outstanding when the first read
is being armed — and `notifyReady( )` runs under `m_lock`.

*Verified 2026-09-23 at the source, link by link:* the guard at `TaskBase.h:1167` and
`scheduleTask( eq )` at `:1208` inside its `try`; h1 `:1584` reaching `scheduleRead( )` at `:1605`
with no `beginOperation( )` before it; `:852` and the catch at `:869-871`; `takeTerminalNoLock( )`
(`:147-157`) true with `m_closing` just set and the count back at zero; `applyDecision( )` `:194`
→ `notifyReadyImpl( )`, which re-acquires `m_lock` at `TaskBase.h:564`. `os::mutex` is Boost's
plain mutex (`OSBoostImports.h:120-121`; `MultiOperationTask.h:99` says in words that it is not
recursive). *(Corrected 2026-09-23, §14.3: `os::mutex` is `std::mutex` — `OSBoostImports.h:95` reads
`using std::mutex;`, and the red backtrace's frame #4 is `std::__1::mutex::lock( )`. Non-recursive
either way; the conclusion stands.)* The cost is therefore a self-deadlock on the scheduling thread — while the execution
queue's own lock is held too, since `ExecutionQueueImpl.h:649` says `scheduleNothrow( )` is called
under it.

**The clean fix is to move `scheduleTask( eq )` out of the guard in `TaskBase::scheduleNothrow( )`.**
That is a lock-scope change in the task core, which **every** task in baselib goes through —
messaging, blobtransfer, io, all of it. Under the project's rule that core code changes land as their
own change-set gated on the entire suite, it cannot ride on a driver fix. The two local alternatives
are both worse: posting `scheduleRead( )` from h1's `scheduleTask( )` changes the documented
*armed FIRST and unconditionally* ordering, and deferring the catch needs a flag.

**Reopened 2026-09-23 — the lock scope is deliberate, and the core already has the route.** Two
facts the paragraph above did not weigh.

1. **The guard serializes scheduling against cancellation.** `requestCancel( )` takes `m_lock`
   (`:1241`) and `requestCancelInternal( )` (`:1026-1058`) calls `cancelTask( )` only when
   `m_state` is `Running`, which `scheduleNothrow( )` sets at `:1194` inside the same guard, after
   testing `m_cancelRequested` at `:1196`. With `scheduleTask( )` outside it a cancel can land
   between `Running` and the first operation being armed: `cancelTask( )` finds nothing, and the
   operations armed a moment later are never cancelled — a **lost cancel** in every task whose
   `cancelTask( )` is a synchronous `socket.cancel( )`. "Clean" was asserted from the deadlock
   alone.
2. **The base already answers a `scheduleTask( )` that throws.** Its catch (`:1210-1232`) posts
   `notifyReadyImpl( )` to the pool, off every lock; h2 names the sibling route in its own words
   (`Http2ConnectionTask.h:2685-2687`). So #12 is not the core catching where it should not — it
   is h1's `scheduleRead( )` catching on the one path where the base wants the throw.

The driver-local fix therefore needs **no flag**: arm without a catch on the schedule path and let
the throw propagate to `scheduleNothrow( )`'s catch; keep the catch for the in-handler re-arm
(`:1205`), where it is right — two functions, or one whose caller chooses. The count is left at one
on that route; nothing reads it after completion (`pendingOperations( )` has three readers, all in
`utf_baselib_tasks2`) and `scheduleNothrow( )` resets it (`MultiOperationTask.h:408`). *Armed FIRST
and unconditionally* is untouched. **This puts #12 back within reach of this design as a fourth,
driver-local change-set — A4 in §6 — proposed, not decided.** The core change stays available, and
stays its own whole-suite gate if the maintainer prefers it; it simply has a cost this draft did
not name.

### 4.1 One thing checked that cuts the other way, and is recorded so it is not rediscovered

h1's **other** call site — `scheduleRead( )` from inside `onReadCompleted( )`'s handler body — also
reaches that inline `onOperationCompleted( )` under the task lock, because a handler body runs inside
`BL_TASKS_HANDLER_BEGIN( )`'s guard. **It is nevertheless not the same defect:** the completing
operation is still outstanding there until `END_MULTIOP` runs, so the count cannot reach zero and
`notifyReady( )` is not reached. Latent by an accounting property, not by luck.

This is written down because the first three greps suggested *"#12 is broader than the record says"*
and that conclusion is **wrong**. The record is right, and the difference between the two call sites
is the accounting, which no grep shows.

*Precision 2026-09-23.* The rule at `:62-67` names **two** calls, and the accounting property
covers only the second. At every in-handler catch — `:724`, `:871` reached from `:1205`, `:1455` —
`onOperationCompleted( )` finds `m_closing` newly true and `m_closeInitiated` false, so
`applyDecision( )` runs `initiateClose( )` **inline, under the task lock**, which `:279-283` says
never happens. Benign today only because neither driver's `initiateClose( )` takes the task lock —
h1 `:1515-1577` is `isChannelOpen( )`, `shutdownSocket( )`, `cancel( ec )`, `cancelIdleTimer( )`;
h2 `:2573` onward is `cancelTimers( )` (`:2115-2145`, cancels only) and the same pair. A property
either override could lose with no test noticing, and one the catch A3 adds inherits.

---

## 5. #13 — h2's `scheduleRead( )` has no accounting guard

### 5.1 The defect

h2's `scheduleRead( )` calls `base_type::beginOperation( )` and then `async_read_some( )` with **no
`catch` completing the operation**, where h1's read, write and timer each carry one. h1's comment
states the cost exactly: *"An operation which was begun and then never started has to be completed
here or the pending count never reaches zero again — and a task whose count cannot reach zero can
never take its terminal path, which is a hang rather than a failure."*

A **hang rather than a failure** is what makes this worth fixing despite being narrow: it needs
`async_read_some( )` to throw, which is rare, and its consequence is the worst kind of symptom.

*Corrected 2026-09-23 — the record undercounts.* h2 has **eight** `beginOperation( )` sites and
none is guarded: the read (`:1456`), the write pump (`:1624`), five timers (`:1789`, `:1867`,
`:1917`, `:2047`, `:2188`) and the command drain's post (`:492`, `:499`). The two `catch` clauses in
the file (`:623`, `:1236`) wrap the session, not an initiator. h1 guards all three of its own. What
can throw is the same at every site — allocation, since asio reports I/O failure through the
handler — so either the guard is owed at all eight or the record says why the read alone. **A3 is
eight sites, or it is a decision written down.**

### 5.2 The shape, and the one question the lane must settle first

The obvious fix is h1's `catch` verbatim. **It cannot be taken verbatim without answering §4's
question at h2's own call sites**, because that `catch` calls `onOperationCompleted( )` inline, which
is exactly what §4 establishes is unsafe when the count can reach zero.

h2 has two call sites, and they differ. *The second row was answered by reading on 2026-09-23.*

| site | context | can the count reach zero? |
|---|---|---|
| `onRead( )`'s re-arm (`:1540`) | inside a handler body, under the task lock | **No** — the completing operation is still outstanding, as §4.1 |
| the post-negotiation arm (`:2555`) | inside `continueAfterConnected( )` (`ClientConnectionTaskBase.h:598-626`), i.e. in the body of `onHandshakeCompleted( )` (`TcpSslBaseTasks.h:570-591`) or, cleartext, `onConnectionEstablished( )` (`TcpBaseTasks.h:1395-1435`) — under the task lock, with the establisher's plain `BL_TASKS_HANDLER_END( )` as epilog | **No** — `pumpWrites( )` at `:2553` precedes it in the same synchronous chain and begins the opening write (`:1624`) unconditionally: `Session`'s constructor calls `queueOpeningFrames( )` (`Session.h:608`, `:1406-1416`), so `wantsWrite( )` (`:899-907`) is true and the buffer is not empty. The count at `:2555` is at least one. If a command drained by `applyCommands( )` had begun a close, `scheduleRead( )` returns at `:1451` before `beginOperation( )` |

**So A3 does not wait on #12**, and the shape can be h1's catch. What the reading also found is
what today's code does with a throw at each site, which the record did not distinguish:

- At **`:1540`** the throw reaches `END_MULTIOP( )`, the phantom operation keeps the count above
  zero for good, and the task **hangs** — the record's consequence, at this site only.
- At **`:2555`** there is no `END_MULTIOP( )` between the throw and the establisher's plain epilog,
  so `notifyReady( eptr )` completes the task at once — the route h2's own comment names, *"the
  route that arrives with operations still pending, after a throw out of onProtocolNegotiated( )"*
  (`:2685-2687`). The opening write is still in flight; when the stream policy's
  `onTaskStoppedNothrow( )` shuts the socket it fails, `END_MULTIOP( )` records it as the first
  error, and the accounting takes its terminal a **second** time — `notifyReadyImpl( )` runs
  `scheduleTaskFinishContinuation( )` and `onTaskStoppedNothrow( )` again before `m_notifyCalled`
  stops it, the re-entry `MultiOperationTask.h:37-42` exists to prevent. Not a hang: a **double
  completion**.

h1's catch fixes both. At `:2555` it runs `initiateClose( )` inline under the task lock — §4.1's
benign breach — with the settings timer possibly armed by `pumpWrites( )` and the keep-alive and
idle timers not yet (`:2557-2558`).

h2 has no `scheduleTask( )` override; the establisher's is what runs, and it begins nothing the
accounting sees (`ClientConnectionTaskBase.h:309-319`). Settled by reading, not by absence.

*Implementation review 2026-09-23 (§11.1).* The table above answers the count for `scheduleRead( )`,
and this section then took h1's catch as the shape for all eight sites without asking the question at
each. It has a different answer inside `pumpWrites( )`: `chkArmSettingsTimer( )` runs **before** the
write's `beginOperation( )`, and the opening SETTINGS is unacknowledged from the session's
constructor, so at negotiation that arm is begun with the count at **zero** — and an inline
`onOperationCompleted( )` there takes the terminal under the establisher's lock, which is §4's
deadlock. **h1's catch is therefore not the shape.** The lane's `abandonOperation( )` is, and §11
reviews it. The count question is per site and has to be asked at every one; §5.2's "the shape can be
h1's catch" is withdrawn.

### 5.3 The red

A read initiator made to throw. If the harness refuses to make `async_read_some( )` throw, the
discrimination goes into the assertion instead — `src/utests/AGENTS.md` is explicit that a run count
is not evidence. **If no red can be built, this does not land**, and that outcome is recorded rather
than worked around.

*Note 2026-09-23.* `utf_baselib_tasks2/TestMultiOperationTask.h` already has probe tasks whose
`scheduleTask( )` begins operations (`:731-759`); the accounting consequence of an initiator that
throws — the phantom operation, the count that never returns to zero — can be shown red there
without a throwing socket. That is where the **mechanism's** red belongs if the driver harness
refuses one; the driver's own change is then reviewed against the pinned rule. The `:2555` double
completion, if it is to be pinned at all, needs the driver.

---

## 6. These are three change-sets, not one — four, if A4 is taken

*Table amended 2026-09-23.*

| | Item | Depends on | Gate |
|---|---|---|---|
| A1 | #8, the TLS read cancel | the `tls-h1-control` lane's fixture | its own red + the affected modules |
| A2 | #7, h1's write peer-close arm | nothing; adds one predicate to `NetUtils.h` (additive) | its own red + `httpclient7` unchanged |
| A3 | #13, h2's accounting guard — **eight sites** (§5.1) | nothing — §5.2 is answered | its own red, if one can be built; the mechanism's in `tasks2` |
| A4 *(proposed)* | #12, driver-local (§4) | nothing | its own red — in `httpclient7`, since it needs h1's own override (§14.5); the mechanism's in `tasks2` |

Ranked by what each defect costs, not by what is easiest: A1 can report a truncated body as a
success, A2 misclassifies an ending — and under today's code drops an early response's head whenever
the write notices first — A3 can hang a task on a rare throw, A4 can deadlock the scheduling thread
on one.

They do not share a file between them except that A1, A2 and A4 all touch h1's driver, so the
order is **A2, then A4, then A1** if all are taken — A2 first because it is the only one whose red
the existing harness can already produce, A1 last because its fixture has not landed. A2 also opens
a one-line follow-up in h2's `isPeerClosed( )` (§2.2), which is its own item and not A2's.

## 7. Acceptance

Per the standing rule: **agreed only when the maintainer and fable fully agree on the design, and
accepted only when both review and agree on the implementation.** The design half is now met —
§9 reviewed it and recommended (ii) and A4, the author concurred, and §10 records the maintainer
choosing both. **The implementation half is not**: each of A1–A4 is accepted only on its own
second gate.

## 8. What this design does not establish

*List amended 2026-09-23.*

- Whether #8's window can be made deterministic (§3.4). If it cannot, A1 does not land in this form.
- ~~Whether h2's post-negotiation `scheduleRead( )` can drive the count to zero (§5.2).~~
  **Answered 2026-09-23: it cannot.**
- ~~Whether #7's arm leaves `httpclient7`'s barrier cases untouched (§2.4). Argued from the arm
  order, **not verified by a run** — and H01's recorded fix died on exactly this kind of assumption.~~
  **Answered 2026-09-23: verified by a run, §12.2 — both barrier cases and all three peer-close cases
  10 of 10 after the arm and 5 of 5 before, on the lane's record.**
- The Windows spelling of a peer close on a write (§2.2). Owed to the matrix; the predicate admits
  what is measured, and this design does not guess it.
- Whether #8's gate must also cover the external cancel (§3.2, point 3).
- ~~Which shape #7 takes (§2.2) and which fix #12 takes (§4).~~ **Chosen 2026-09-23, §10: (ii) and
  A4.**
- **(ii)'s own premise**, promoted here from §2.2 because it is now the design's largest unmeasured
  claim: *a peer close that failed the write is always reported to the pending read as well, as one
  of the codes the read-side predicate admits.* Derived from the code, **measured nowhere**. A2's
  red must establish it on both platforms; if it does not hold, (ii) is wrong and §2.2's (i) is the
  fallback. **Measured on Linux 2026-09-23 and held in the letter, §12.3: the read is always told —
  as `connection_reset` when it reached the reset first, as `eof` when the write did — and what that
  `eof` means is §12.5's finding. (ii) stands; Windows still owed.**
- **A3's residual at `postCommand( )`** (§11.3): giving back the last operation of a closing task
  leaves the terminal untaken. Derived from the source, not observed; a control needs a
  fault-injecting allocator and a racing completion. A decision is owed at the merge.
- The double completion's entrances (§11.7): every throw in `onProtocolNegotiated( )` after the first
  operation, not the post-negotiation `scheduleRead( )` alone. Derived, not observed.

**For the owed list — found here, not fixed here:** h2's write-side peer-close arm does not admit
`broken_pipe` (§2.2); the TLS composed read's hang face, in both drivers (§3.2); the command a
throwing mailbox post leaves queued, and the TLS half of the establishment route (§11.8).

## 9. Design review, 2026-09-23

**Reviewer: Claude Fable 5.1, on `lazari2` @ `a723cee`, read against the source at that tip.**
Opened at the signature and read to the end: `MultiOperationTask.h` whole; `TaskBase.h`'s
invariants comment, the handler macros, `notifyReadyImpl( )`, `notifyReady( )`,
`requestCancelInternal( )` and `scheduleNothrow( )` with its catch; h1's class comment,
`onStartRequest( )`, `onWriteCompleted( )`, `scheduleRead( )`, `onBytesRead( )`'s prolog,
`isCleanEndOfStream( )`, `onPeerClosed( )`, `onReadCompleted( )`, `finishStream( )`,
`closeConnection( )`, `chkArmIdleTimer( )`, `cancelIdleTimer( )`, `onIdleDeadline( )`,
`initiateClose( )`, `scheduleTask( )` and `onTaskStoppedNothrow( )`; h2's rules L1-L4,
`postCommand( )`, `scheduleRead( )`, `isPeerClosed( )`, `onRead( )`, `onPeerClosed( )`,
`pumpWrites( )`, `onWrite( )`, every timer's arming site, `cancelTimers( )`,
`onProtocolNegotiated( )`, `initiateClose( )` and `onTaskStoppedNothrow( )`;
`ClientConnectionTaskBase.h`'s accounting note, `onProtocolNegotiated( )` and
`continueAfterConnected( )`; `TcpBaseTasks.h`'s `onConnectionEstablished( )` and
`continueAfterPreHandshakeStage( )`; `TcpSslBaseTasks.h`'s `onHandshakeCompleted( )` and
`isStreamTruncationError( )`; `NetUtils.h`'s three predicates and the rule above them;
`Session.h`'s constructor, `wantsWrite( )` and `queueOpeningFrames( )`; `OSBoostImports.h`'s
mutex typedefs; `io_op` in **Boost 1.90.0**'s `ssl/detail/io.hpp`, the dist that ships; the two
`httpclient7` test files' runners, comments and assertions; the h2 half-close case's assertions;
`TestClientSessionTls.h`, `TestHttp2ConnectionTaskTls.h` and `TestClientContracts.h` where they
touch ALPN; the tasks2 probe's `scheduleTask( )`; the owed-work record with its uncommitted diff;
teardown §1, §2.1, §4-§4.4, §8.1, §13, §14 and §16.9; and s6r3 §11. **Nothing was built or run.**
Every line number in the corrections was re-derived at `a723cee`.

**Verdict: not agreed as first written; corrected in place above, each correction dated, with two
choices left to the maintainer.** Every *finding* of the draft stands: #7, #8 and #13 are real,
pre-existing, and each is worth its own change-set; #12's four-link chain holds. Three
*specifications* did not survive re-derivation, and one *staging decision* is reopened:

1. **#7's predicate would not fire on the one code this suite has measured** (§2.2). The draft
   chose `net::isPeerClosedErrorCode( )` by the right question — *is the conversation over* — but
   both `NetUtils.h` predicates are read-side, and a write failed by the peer's RST completes
   `broken_pipe` on POSIX (`TestHttp1DriverWriteBarrier.h:167-175`, measured), which neither admits.
   Right conclusion on the variant, wrong premise on the coverage. The correction is the rule's
   own: a third predicate in `NetUtils.h`, additive, riding with A2. The same reading found h2's
   write arm has the same hole — recorded for the owed list.
2. **#7's recorded shape pre-empts the one classifier that can judge a close-delimited ending**
   (§2.2). `onPeerClosed( ec )` from the write handler always takes the unclean branch and resets
   the parser, so an early response whose head is still in the reactor is treated as unsolicited
   when the write's failure handler runs first. Shape (ii) — excuse the write, let the read report
   the ending — removes the order dependence entirely and is the smaller diff. Recommended, and
   left to the maintainer because it departs from what teardown §4.4 and §13 recorded.
3. **#8's gate, as recorded, does not cover an external cancel** (§3.2). `isClosing( )` is the
   state of *our* teardown; an external cancel tears down without setting it, and the read
   handler's cancel check sits after the arm. The draft's §3.2 argued from `isClosing( )`'s meaning
   without checking who writes it — the same premise teardown §14 verified for the write arm, where
   it is what one wants, and it cuts the other way here.
4. **#12's "clean fix" has an unexamined cost, and the core already supplies the route** (§4).
   Moving `scheduleTask( )` out of the guard opens a lost-cancel window in every task, because the
   guard is what serializes `Running` against `requestCancel( )`; and `scheduleNothrow( )`'s own
   catch completes a throwing `scheduleTask( )` off every lock. The chain is right; the framing
   "lock-scope defect in the task core" is not, and a flag-free driver-local fix exists. Reopened
   as a proposed A4; the maintainer chooses.

**§5.2's blocking question is answered: the post-negotiation site cannot reach zero.** The opening
write is begun two lines earlier in the same synchronous chain and is unconditional. What the
reading added is that the two sites fail *differently* today — a hang at `:1540`, a double
completion at `:2555` — which the record ran together, and that #13 is eight unguarded sites in h2,
not one.

**The four premises the brief asked to be attacked separately from their conclusions:**

- *§4's chain.* All four links hold at the source (anchors in §4). The lock is not recursive, and
  the deadlock is taken under the execution queue's lock as well.
- *§4.1's counter-claim.* Holds for `notifyReady( )`: the completing operation keeps the count
  above zero until `END_MULTIOP( )`. It is incomplete for the rule's first half — `initiateClose( )`
  *is* reached under the task lock at every in-handler catch, benign only because neither override
  takes it (§4.1). The draft's staging therefore stands, with that property stated rather than
  assumed.
- *§3.4's fixture claim.* Holds, once qualified to "through the h1 driver" (§3.4); the lane it
  names exists as a worktree with nothing landed.
- *§2.2's ordering.* Holds, and for a concrete reason the draft did not have: our own teardown's
  code is in the write-side predicate, so the two arms answer yes together on the barrier case
  every run (§2.2, §2.4). The draft's "whenever the codes happen to coincide" understated it.

**Comments checked against the code they describe, because the brief asked:** h2's `onWrite( )`
comment (`:1646` onward) — true of the code; h1's `scheduleRead( )` comment (`:840-847`) — true;
h1's `scheduleTask( )` *armed FIRST* comment (`:1598-1602`) — true; h2's `onTaskStoppedNothrow( )`
comment on the establishment route (`:2677-2687`) — true, and it is what settles §5.2's second
site; `MultiOperationTask.h:62-67` — true of the accounting and untrue of the three in-handler
catches, as §4.1 now records; `MultiOperationTask.h:401-404`'s reason for releasing the leaf lock
before the base call — true, and it names the very lock scope §4 is about. None outlived its fix.

**Corrections applied in place, each dated:** the status line; §1's framing note; §2.1's
"hand-rolled"; §2.2 whole; §2.3's red; §2.4's premise; §3.1's precision; §3.2's three precisions
and the hang face; §3.4's qualification; §4's verification and reopening; §4.1's first-half
precision; §5.1's site count; §5.2's answer; §5.3's note; §6's table and order; §8's list.

**Proposals, marked where they sit:** the write-side predicate's name and contents (§2.2); shape
(ii) (§2.2); the discriminating assertion in #7's red (§2.3); the gate's exact shape and its cancel
question (§3.2); A4 (§4, §6); the tasks2 home for the mechanism reds (§5.3).

**Findings for the maintainer that are not this design's to fix:** h2's write-side hole; the TLS
composed read's hang face in both drivers, closed only by the per-operation cancellation teardown
§2.4.3 defers; and the owed-work record's item 13, which should say "eight sites" when it is next
touched.

**What this review does not claim.** Nothing was compiled. That a peer close which failed a write
always reaches the pending read as an admitted code — what shape (ii) rests on — is derived from
the transport's behaviour on POSIX and from the Windows measurements the `PeerCloseErrorCodes_*`
controls recorded for the read side; the write side's Windows spelling has not been measured. The
double completion at `:2555` is derived from `notifyReadyImpl( )`'s order of operations, not
observed. Whether a cancelled request's sink is harmed by a late success (§3.2, point 3) needs the
request task, which was not read.

**Agreement.** Withheld until the maintainer chooses #7's shape and #12's fix. With those chosen and
the corrections above taken as part of the design, the reviewer would agree that A2, A3 and — after
its fixture — A1 may be implemented against it, in §6's order and with its gates.

---

## 10. The two choices, made 2026-09-23 — and one objection withdrawn

Both taken in the direction §9 recommended, so §9's condition is met and the design is **agreed**.

### 10.1 #7 takes shape (ii)

**The write handler declines to fail the task and does nothing else.** No `onPeerClosed( )`, no
`closeConnection( )` from the write side; the read — armed for the life of the connection — observes
the same ending with a read-side code and classifies it once, from the only side that can.

**The objection the author raised against (ii) is withdrawn, and it was wrong.** It was that (ii)
diverges from h2, where the write side does pre-empt the read, and that divergence between the two
drivers is a maintenance cost. Reading both refuted it: **the divergence tracks a real difference
between the two protocols**, and is therefore correct rather than merely tolerable.

| | HTTP/1.1 | HTTP/2 |
|---|---|---|
| Is connection close part of the message format? | **Yes.** `parseEof( )` is *"the one function which can declare a close-delimited message COMPLETE"* (`Http1ConnectionTask.h:1096`) | **No.** Every message is framed; a response finishes on a flag, never because the connection ended |
| Does the ending's error code carry meaning? | **Yes** — `onPeerClosed( )` takes an `error_code` and branches on `isCleanEndOfStream( )` (`:1106`, `:1113`) | **No** — `onPeerClosed( )` takes **no argument at all** (`Http2ConnectionTask.h:1557`) |
| So can the write side pre-empt the read? | **No.** The read side holds the parser and the bytes, so a write handler that ended the stream would reset the parser under a response still arriving. *(Corrected 2026-09-23, §12.4: the draft's "holds the only machinery that can tell a complete close-delimited response from a truncated one" is true of the machinery and false of the outcome — on the write-first ordering that machinery is handed `eof` and the write held the only evidence. (ii) still stands; the evidence question is A1's, §12.5.)* | **Yes**, and it costs nothing |

The two signatures are the protocol difference made concrete in this codebase, and they were read
rather than inferred. h2's shape is right for h2; (ii) is right for h1.

**What stands against (ii), and it is the only thing:** its premise, now promoted to §8 — that a peer
close failing the write always also reaches the pending read as an admitted code. Derived, never
measured. **A2's red must establish it on both platforms**, and if it does not, (i) is the fallback
and this decision is reopened rather than patched.

### 10.2 #12 takes the driver-local A4, and the core question gets a deferral

**A4 as §4 proposes it:** h1 propagates the throw on the schedule path — where
`scheduleNothrow( )`'s own catch already completes it off every lock — and keeps its catch for the
in-handler re-arm, where the count cannot reach zero. Flag-free, local to one driver, and it rides
in §6's order between A2 and A1.

**The core question is explicitly not settled by taking A4**, and the maintainer asked for that
recorded rather than closed: whether `TaskBase::scheduleNothrow( )` holding `m_lock` across
`scheduleTask( )` is a real design problem in the library. A4 removes this driver's exposure to it
and removes nothing else — **every other task in baselib that arms an operation synchronously from
`scheduleTask( )` keeps whatever exposure it has.** The full chain, §9's cancel-loss objection to
the core fix, and what would have to be established before taking it, are in
[`taskbase-schedule-lock-scope-deferral.md`](taskbase-schedule-lock-scope-deferral.md).

---

## 11. Implementation review, 2026-09-23 — A3's second gate

**Reviewer: Claude Fable 5.1, reading `a3-h2-accounting` @ `372a397` in the lane worktree, off
`lazari2` @ `a723cee`.** Opened at the signature and read to the end, at the commit: the diff whole;
`MultiOperationTask.h` whole; `TaskBase.h`'s handler macros, `notifyReadyImpl( )`, `notifyReady( )`,
`requestCancelInternal( )` and `scheduleNothrow( )` with its catch; h2's class comment and members,
`postCommand( )` with its pre-image at `a723cee`, `takeCommands( )`, `onCommandsPosted( )`,
`applyCommands( )`, `applySubmit( )`, `applyCancel( )`, `onHeaderBlocksProduced( )`,
`applyConsumed( )`, `applyProvideBody( )`, `pumpBody( )`, `raiseBodyWanted( )`, `pumpAllBodies( )`,
`closeStream( )` and both `closeAllStreams*( )`, `drainSessionEvents( )`, `dispatchEvent( )`,
`onStreamClosedEvent( )`, `onGoAwayReceived( )`, `onConnectionErrorEvent( )`, `scheduleRead( )`,
`onRead( )`, `onPeerClosed( )`, `pumpWrites( )`, `onWrite( )`, the five arms with their handlers and
cancels, `cancelTimers( )`, `closeGracefully( )`, `chkFinishClose( )`, `closeSubmissions( )`,
`onProtocolNegotiated( )`, `initiateClose( )`, `cancelTask( )`, `onTaskStoppedNothrow( )` and the four
`ClientConnection` entry points; h1's three guarded sites; `ClientConnectionTaskBase.h`'s accounting
note, `onProtocolNegotiated( )` and `continueAfterConnected( )`; `TcpBaseTasks.h`'s
`beginPreHandshakeStage( )`, `continueAfterPreHandshakeStage( )`, `onConnectionEstablished( )` and
`scheduleTaskFinishContinuation( )`; `TcpSslBaseTasks.h`'s `onHandshakeCompleted( )` and
`scheduleTaskFinishContinuation( )`; `TcpStrandedStreams.h`'s `postToStrand( )`; `Session.h`'s
`unacknowledgedSettingsCount( )`, `applyLocalSettings( )`, `submitRequest( )`'s checks,
`resetStream( )`, `consumed( )`, `ping( )`, `reapClosedStreams( )` and every caller of
`raiseConnectionError( )`; the mutex typedefs, `BL_MUTEX_GUARD( )` and `BL_NOEXCEPT_END( )`;
`HttpClientRequestTask.h`'s `submit( )` catch, `post( )`, `onDrain( )`, `applyEvent( )`,
`applyClosed( )`, `releaseConnectionSlot( )` and `releaseConnection( )`; the new module's three files
and marker against the six siblings'; `Http2DriverTestUtils.h` where the case touches it;
`src/utests/AGENTS.md`; §1-§10 above; teardown §16; and, outside the repo, the A3 section of the
lane's journal. **Nothing was built or run**, with two measurements made here independently: the
object sizes read off the lane's build tree, and tier 1 of `check_split.sh` run in the lane worktree.
**Line numbers below are at `372a397`**; the design's are pre-image, and the driver grew by 215 lines.

**Verdict: the departure from §5.2's shape is correct and was necessary; the implementation is right
at seven of the eight sites, and at the eighth it is right but not sufficient. Agreed, with two
things carried into the merge (the closing paragraph).** The load-bearing claim — an inline-completing
catch self-deadlocks on the establishment chain — holds, re-derived link by link (§11.1), at **one** of
the three sites the lane names. The other two are not reachable with the count at zero from anywhere,
and "four" names nothing. One deadlocking site is enough, so the conclusion stands — on a premise
two-thirds false, which is the failure this project keeps meeting, and this time it is written into
three code comments (§11.5). `abandonOperation( )` is additive and does decide nothing (§11.2).
Deciding nothing is sufficient wherever a decider follows the rethrow, which is every strand site and
not `postCommand( )`: there, giving back the last operation of a closing task leaves the terminal
untaken — a stall until the next command posts, permanent on a connection nobody posts to again;
reachable only as a race on top of an allocation failure, derived and not observed (§11.3). What each
site gives back is right, with one pointer-as-flag the stated principle would also return (§11.4); the
drain-flag bug is real and belongs here. The red is a red and the module is by the rules (§11.6). On
the open question: leave it, and for a stronger reason than the lane's — the double completion has
several entrances and the one catch would close one (§11.7).

### 11.1 The shape departure — right conclusion, one true premise out of three

**Eight sites, verified:** `postCommand( ):492`, `scheduleRead( ):1510`, `pumpWrites( ):1687`,
`chkArmSettingsTimer( ):1870`, `chkArmKeepAlive( ):1968`, `armPingDeadline( ):2031`,
`chkArmIdleTimer( ):2174`, `armDrainDeadline( ):2329`; each now has a `try` around its initiator and
a `catch( std::exception& )` that gives back and rethrows. No ninth `beginOperation( )` in the file,
and none in `src/include` outside h1 — §5.1's count stands.

**The chain, and the count at its head.** Cleartext: `onConnectionEstablished( )`
(`TcpBaseTasks.h:1395-1435`) opens `BL_TASKS_HANDLER_BEGIN_CHK_EC( )` — `BL_MUTEX_GUARD( m_lock )`
(`TaskBase.h:119-120`) — and calls `beginPreHandshakeStage( )` (`:1385`, whose default invokes the
continuation inline) → `continueAfterPreHandshakeStage( )` (`:1352`) → `beginProtocolHandshake( )` →
`continueAfterConnected( )` (`ClientConnectionTaskBase.h:598`) → `onProtocolNegotiated( )` (`:625`,
h2's at `:2634`). TLS: the same from `onHandshakeCompleted( )` (`TcpSslBaseTasks.h:570-591`), same
macro. The count is zero on entry: nothing before the handshake enters the accounting
(`ClientConnectionTaskBase.h:309-319`), `scheduleNothrow( )` resets it, and `postCommand( )` cannot
begin before `m_isStrandReady` (`:487`), which `onProtocolNegotiated( )` sets at `:2692-2695`.

**The site that holds.** `pumpWrites( )` runs `produce( )` (`:1670`), `onHeaderBlocksProduced( )`
(`:1672`) and `chkArmSettingsTimer( )` (`:1681`) **before** the write's `beginOperation( )` at
`:1687`. `unacknowledgedSettingsCount( )` is `m_unackedSettings.size( )` (`Session.h:737-740`), and
the push is in `applyLocalSettings( )` (`:1275`), which `queueOpeningFrames( )` runs from the
constructor — so it is already 1 at negotiation, and the settings timer is begun at `:1870` with the
count at zero. h1's catch there: `onOperationCompleted( eptr )` → count 1→0, `m_firstError`,
`m_closing = true` (`MultiOperationTask.h:398`), `m_closeInitiated` (`:404`), `takeTerminalNoLock( )`
true (`:147-157`: closing, count zero, not taken), `applyDecision( )` → `initiateClose( )` inline
under the lock (§4.1's breach) and then `notifyReady( )` (`:194`) → `notifyReadyImpl( )` →
`BL_MUTEX_GUARD( m_lock )` (`TaskBase.h:564`) on the `os::mutex` at `:436`, which is `boost::mutex`
(`OSBoostImports.h:120`) *(corrected 2026-09-23, §14.3: `std::mutex`, `:95`; same conclusion)*,
through `lock_guard` (`BaseDefs.h:52`). **Self-deadlock, exactly as the
lane says.**

**The two sites that do not.** `chkArmIdleTimer( )` has two callers, `onStreamClosedEvent( )`
(`:1414`) and `onProtocolNegotiated( )` (`:2713`); `armDrainDeadline( )` has two,
`onConnectionErrorEvent( )` (`:1470`) and `closeGracefully( )` (`:2417`). The lane's route for both
is `applyCommands( )` (`:2699`) draining events. At negotiation it cannot: the only command that
drains is `applyCancel( )` (`:750`), behind `isHeadersProduced` (`:726-731`), which no stream has
before the first `produce( )`; `applyConsumed( )`, `applyProvideBody( )` and `pumpBody( )` queue
frames and drain nothing; `onHeaderBlocksProduced( )`'s resets do emit a `StreamClosed`
(`resetStream( )` → `reapClosedStreams( )` → `emitStreamClosed( )`, `Session.h:1186-1194`,
`:3530-3546`) but by its own note (`:785-789`) they wait for the next handler's drain; and a
`ConnectionError` is raised only from `feed( )` (`Session.h:812`) and `onTimer( )` (`:888`), neither
of which runs at negotiation. The direct calls at `:2712-2713` come after the write (`:1687`) and the
read (`:1510`), so the count there is at least two. **Neither site is reachable with the count at
zero, from `applyCommands( )` or from anywhere.** The commit message, the journal and the
`scheduleRead( )` header say "four sites"; three are named, one holds, and the fourth is never named.

**Why the conclusion survives.** One site where h1's shape deadlocks is enough to rule it out as the
shape, and once two shapes would be needed, one that is safe everywhere is the better design. The
lane's argument for uniformity — robustness to a reorder of `onProtocolNegotiated( )` — is also
right on its own: the settings timer's safety rests on nothing but the order inside `pumpWrites( )`.

**What §5.2 got wrong, corrected there in place.** It answered the count question for
`scheduleRead( )` and generalised the shape to eight sites without asking it at each; the arm inside
`pumpWrites( )` that precedes the write is where the generalisation broke. The design's numbers were
wrong once already; this is the second time, and the lesson is §4.1's: the accounting property has to
be read per site, and no grep shows it.

### 11.2 `abandonOperation( )` is additive, and it decides nothing

`MultiOperationTask.h:225-254`: one new `protected` member, no data member, no existing function
touched, inserted between `beginOperation( )` and `beginClose( )`; the class is a template, so there
is no ABI. It takes the leaf lock, asserts (`:248`), decrements and returns. It never reads
`m_closing`, never calls `takeTerminalNoLock( )` or `applyDecision( )`. **Additive by the rule, and
"decides nothing" is literally true.** The class note at `:62-67` — `initiateClose( )` and the
terminal are invoked only from `onOperationCompleted( )` — stays true. `tasks2` compiled and ran green
against it (13 = 13 in the lane's table), and its probes do not call it.

### 11.3 The eighth site — where nothing decides after the rethrow

**Why deciding nothing is sufficient at seven sites.** Each rethrow has a decider downstream. In a
handler body the handler's own operation is outstanding until its epilog, so the count is at least
one through the body and `BL_TASKS_HANDLER_END_MULTIOP( )` (`TaskBase.h:279`) completes it, records
the first error, initiates the close and takes the terminal if it is due. On the establishment chain
the establisher's `BL_TASKS_HANDLER_END( )` completes the task with the exception — outside the
lock, because the macro closes the guard's scope (`:254`) before `exprOnFailure` (`:255-258`). The
count may then be zero and not closing, which is "simply stopped doing anything" — and harmless,
because the epilog completed the task anyway.

**`postCommand( )` has no downstream decider.** It runs on the caller's thread (`submit( )`,
`cancel( )`, `consumed( )`, `provideBody( )`, `:2876-2969`), under no task lock and in no handler;
the throw goes to the caller, and the operation it begins is completed by `onCommandsPosted( )`'s
epilog and by nothing else. The comment at `:463-470` says why the operation is begun under the
mailbox lock: so that a command accepted by a closing connection holds the terminal open until the
drain answers it. On the **error door** and the **cancel door** that is exactly the window: a first
error sets `m_closing` and runs `initiateClose( )` (`:2728-2770`), which cancels and shuts down but
does **not** close submissions — `closeSubmissions( )` runs from `onTaskStoppedNothrow( )` (`:2857`),
the terminal — so `postCommand( )` accepts and begins in between. Sequence: first error; the other
operations are cancelled; a caller begins the drain operation; the last real handler completes and
finds the count at one (ours), terminal not due; the post throws; `abandonOperation( )` → zero,
`m_closing` true, `m_terminalTaken` false, **and nothing is left to call `onOperationCompleted( )`.**
The terminal is taken the next time any `postCommand( )` succeeds — its drain's epilog finds it due,
and a request deadline's `cancel( )` will do — and never on a connection nobody posts to again:
`requestCancel( )` does not rescue it, since `cancelTask( )` (`:2791-2803`) begins no operation and
with the count at zero no handler is coming. Severity: a race between a completing handler and a post
that throws, on top of an allocation failure; self-healing on any later command; derived, not
observed. It is narrower than what the change-set closes at this site — before, every throw here
wedged both the mailbox and the count — so the change-set is a net gain as it stands.

**The obvious alternatives are not clean either.** h1's `onOperationCompleted( eptr )` here would
record a *new* first error on a task that has none and initiate the close on the caller's thread —
`initiateClose( )` → `cancelTimers( )` off the strand, the race of the timer record — and
`onOperationCompleted( nullptr )` has the same close hazard in the window between `beginClose( )`
(`chkFinishClose( ):2445`, reached through `onConnectionErrorEvent( )`, which does not close
submissions) and the epilog that sets `m_closeInitiated`.

**The fix that keeps one shape** is in the primitive: `abandonOperation( )` decrements and takes a
terminal that is **already due** — closing, count zero, not taken — and nothing else: no error, no
close. Its safety is structural rather than order-dependent. A due terminal means `initiateClose( )`
already ran *(precision 2026-09-23, §11.9.2: means `m_closeInitiated` is already set — the handler
that set it may still be inside `initiateClose( )` on the strand, since the flag is set under the
accounting lock and the call is made after it is released)* (an error sets `m_closeInitiated` in the
same call, `:398-404`; `beginClose( )` is called
from a handler whose own operation is outstanding until its epilog sets it), every handler has passed
its epilog and no timer is armed; and it cannot arise under the task lock at all — a handler's own
operation is outstanding, and the establishment chain runs before the first operation, when the task
is not closing. The one caveat to record with it: the terminal then runs `onTaskStoppedNothrow( )` on
the caller's thread, a route the comment at `:2820-2843` does not list; with the count at zero nothing
is pending, but a `cancelTimers( )` posted by `cancelTask( )` may be in flight on the strand, which is
two cancels of idle timers from two threads — the letter of the record's race, with nothing left to
lose. **The maintainer chooses**: take the clause now — recommended, because the primitive has one
caller set and its contract is cheapest to change before a second driver uses it, and its red belongs
in `tasks2` beside the probes §5.3 names — or record the residual in the primitive's doc and the owed
list. Either way the doc's "which the catch ... always is" (`:233-238`, of the task lock) is not true
of this site and should say so.

### 11.4 What goes back with the operation, at each of the eight

The principle the lane states — what the initiator claimed goes back, nothing else — checked against
what each site claims before `beginOperation( )`:

| site | claimed before the begin | given back | verdict |
|---|---|---|---|
| `postCommand( ):473` | `m_isDrainScheduled` (`:489`); the command, already in the mailbox | the flag, under the mailbox lock, not nested (`:522-526`) | right; the pre-existing bug is below, the command is §11.8 |
| `scheduleRead( ):1503` | nothing | nothing | right |
| `pumpWrites( ):1654` | `m_isWriteInFlight` (`:1685`); `isHeadersProduced` on every stream (`:761-767`); the frames now in `m_writeBuffer` | the flag (`:1711`) | right — the flag gates `initiateClose( )`'s forceful shutdown (`:2756-2761`), so a phantom write would cost a TLS close its `close_notify`; `isHeadersProduced` correctly stays, for h1's reason at `:688-693` (an initiating call can send speculatively, so a throw is not provably unwritten); the frames are lost with the task |
| `chkArmSettingsTimer( ):1844` | `m_isSettingsTimerArmed` (`:1868`) | the flag (`:1893`) | right |
| `chkArmKeepAlive( ):1953` | nothing | nothing | right |
| `armPingDeadline( ):2020` | nothing — the PING was queued by `onKeepAlive( )` before the call, and the task fails | nothing | right |
| `chkArmIdleTimer( ):2158` | nothing | nothing | right |
| `armDrainDeadline( ):2309` | **`m_drainTimer`'s presence**, which the function's own comment makes the gate: "a connection drains once" (`:2311-2323`) | nothing | the gate stays closed on a drain that was never bounded. Unreachable after the rethrow — every re-arm is behind `isClosing( )` — so no consequence today; but it is the same kind of claim as `m_isSettingsTimerArmed`, and the principle would return it: `m_drainTimer.reset( )` before the abandon, or one sentence saying why not |

**The pre-existing bug is real, and fixing it here is the principle and not scope creep.** Pre-image
(`a723cee`, `:487-499`): the flag is set and the operation begun under the mailbox lock, the post is
made outside it with no catch, and `takeCommands( )` is the only clear. A post that throws leaves the
flag true for good: every later `postCommand( )` sees a drain scheduled, posts nothing and returns
true — commands accepted and never applied, callers waiting for handlers that cannot come. The catch
that balances the count at this site has to give this flag back too, or it balances the count and
leaves the mailbox dead. Same catch, same site, same rule. The lock-order comment at `:463-470` stays
true: the catch takes the two locks one after the other.

### 11.5 Comments that state a premise the code refutes, and the neighbours that still hold

The brief asked for this hunt, and the change adds a lot of prose.

1. **`scheduleRead( )`'s header (`:1477-1500`)** — "reaches four of these sites before the first
   operation is begun: `chkArmSettingsTimer( )` through `pumpWrites( )`, and `chkArmIdleTimer( )` and
   `armDrainDeadline( )` through the events `applyCommands( )` can drain". One site, by §11.1. And
   "h1's driver completes its own inline ... because at all three of its sites another operation is
   outstanding" — **§4 of this design says the opposite**: h1's `scheduleRead( )` from
   `scheduleTask( )` runs with the count at zero, and A4 exists because of it. The comment restates
   the premise §9 corrected.
2. **`chkArmIdleTimer( )`'s catch (`:2188-2191`)** — "reachable with the count at zero through the
   stream closures `applyCommands( )` can drain". It is not.
3. **`armDrainDeadline( )`'s catch (`:2343-2346`)** — "reachable with the count at zero through a
   connection error `applyCommands( )` can drain". It is not.
4. **`pumpWrites( )`'s catch (`:1704-1709`)** — the reason it gives for clearing the flag, "so that a
   pump which runs again is not refused", cannot arise: after the rethrow the handler's epilog sets
   `m_closing` and every pump returns at `:1656`. The reason that does apply is `initiateClose( )`'s
   gate (§11.4). One line.
5. **`MultiOperationTask.h:233-238`** — the task lock "which the catch of an initiator called from a
   handler body, or from a task's establishment chain, always is": true of those two contexts and not
   of the mailbox post; tighten with §11.3's decision.

Corrections to 1-3 are owed before the merge — they would mislead the next lane exactly as §5.1's
"one site" and §5.2's shape misled this one. **Comment edits move `__LINE__`** into every macro below
them, so either the line counts are preserved or the module's green is run again; the lane has the
slot.

**Neighbours checked and still true:** `postCommand( )`'s "the one place" (`:463-470`);
`onHeaderBlocksProduced( )`'s drain note (`:785-789`); `chkArmSettingsTimer( )`'s "THE SITE THAT
SETTLES ITS SHAPE" (`:1884-1887`) — exactly right; `onTaskStoppedNothrow( )`'s route list
(`:2820-2843`) — still true, because the establishment route was left as it was (§11.7);
`initiateClose( )`'s comment (`:2733-2752`); the class comment's L4 (`:186-190`);
`MultiOperationTask.h:62-67`; h1's `scheduleRead( )` comment (`Http1ConnectionTask.h:840-847`); and
the test header's claim that h1 guards all three of its sites — true.

### 11.6 The evidence, and the module

**The red is the lane's report.** No log of the red or the green run exists in the state directory
(searched for the module and the case name; the journal is the only mention), so the 15.0 s red with
`critical check ( stopped ) has failed` and the 3.5 ms green are taken on the lane's word. Two things
were measured here: the object, 37,722,520 bytes = 35.97 MiB clang a64 debug, as reported; and tier
1, which reports exactly one violation, `C1 case ADDED` for the new case, and `eol PASS` — as
reported. Tier 3's "six parse clean" was not re-run.

**The case is worth its green, read line by line.** `postToStrand( )` is `asio::post( )` on the strand
(`TcpStrandedStreams.h:184-189`), so an allocation is the only thing it can throw, and the hiding
policy is the only seam this driver offers a test — §5.3 anticipated exactly this refusal. The arming
is deterministic: the sink holds no connection, so nothing calls `consumed( )`
(`Http2DriverTestUtils.h:296-313`) and no `Consumed` command can leave a drain scheduled ahead of the
second `submit( )`; the seam is one-shot and disarmed again regardless, because `cancelTask( )` posts
too (`:2795`). The wait is on `onTaskStoppedNothrow( )`, the terminal path itself, under the task lock
and notify-only — the right rendezvous, with nothing polled. `requestCancel( )` and not the queue's
`cancel( )`, for the reason the journal records. The teardown release runs before the assertion, so
the red is a failure and not a hung module, and it is never reached on the green path. The
discrimination is the terminal (`stopped`) plus the throw's route (`submitThrew`), the first
exchange, the `Closed` state and a clean peer. Against the unfixed driver the phantom holds the count
at one after the cancelled read completes and `stopped` stays false — §11.3's mechanism with the
count at one instead of zero.

**One red for eight guards is acceptable, with its cost named.** The seven unexercised guards are the
same three lines around initiators no harness can make throw without a fault-injecting allocator; the
design foresaw it and this review checked each by reading (§11.4). But unexercised is where the two
false-premise comments sit: nothing executed those sites, so nothing contradicted what was written
above them. That is the honest limit of a red at one site.

**The module is by the rules.** `utf_baselib_h2client7` follows the numbering; the `Main.cpp` mirrors
`h2client6`'s to the line apart from its own comment and include; the `devenv7_only` marker is the
siblings'; `notes.txt` names a case in its own module; no `data/`; no cross-module include; the
header is in a named namespace with the module's own helpers. The split is justified by the numbers:
`h2client6` is 34,683,904 bytes (33.08 MiB), and the lane's measured cost of a second stream policy
(+9.8 MB) would put it past the 40 MB target; `h2client2` and `h2client3` are at 38.09 and 39.75.
**Tier 2's SKIP is honest and not a pass**: the enforcing platform is `win-x86-*-debug`, unmeasured
here, and a one-case module at 35.97 MiB on a64 will not be smaller there. Two small notes:
`postFailureArmed( )` is process-global state a second case in this module would share, which its
comment does not say; and a `tasks2` case pinning the primitive's own contract is cheap, and is where
§11.3's clause, if taken, gets its red.

### 11.7 The open question — the double completion belongs to the establishment route, and one catch would not close it

**Re-derived, and it is real.** A throw out of `onProtocolNegotiated( )` after the first
`beginOperation( )` reaches the establisher's catch with operations pending and calls
`notifyReady( eptr )` outside the lock (`TaskBase.h:254-258`): `notifyReadyImpl( )` runs
`scheduleTaskFinishContinuation( )` and `onTaskStoppedNothrow( )` (`:2806`), which cancels the timers
and, through the policy, shuts the socket. The pending write or read then fails; `onWrite( )` and
`onRead( )` find `isClosing( )` false — the accounting never saw an error — so a non-peer-close code
goes through `BL_TASKS_HANDLER_CHK_EC( )` into `END_MULTIOP( )`, becomes the first error, initiates
the close and, at count zero, takes the terminal: a second `notifyReadyImpl( )`, a second
`scheduleTaskFinishContinuation( )` and `onTaskStoppedNothrow( )`, stopped at `m_notifyCalled`. (A
pending timer alone does not do this: its handler passes no error, so the count reaches zero while
not closing and nothing follows.) Bounded: the establisher's retry is gated on
`! hasHandshakeCompletedSuccessfully( )` (`TcpBaseTasks.h:1469-1481`) and does not fire; the TLS
continuation's second call sees `m_scheduledForShutdown` and rethrows the original
(`TcpSslBaseTasks.h:471-500`); h2's `onTaskStoppedNothrow( )` is idempotent by construction —
monotone publish, `closeSubmissions( )` on a closed mailbox, an empty stream table. Redundant work
and possibly a second log line; the pool sees one completion. Not observed, as §9 said.

**Its entrances at the commit** are every throw in `onProtocolNegotiated( )` after `:1687`: the
write's own `async_write( )` (`:1691`, with the settings timer pending), `scheduleRead( )` (`:1514`),
`chkArmKeepAlive( )` (`:1972`) and `chkArmIdleTimer( )` (`:2178`) — and, guard or no guard,
`createTimer( )`, `expires_from_now( )` or anything else in that stretch that allocates. **So the
inline-completing catch at `scheduleRead( )` would close one entrance of several**, and "two shapes
in one driver" would buy less than the framing says. The lane's judgement to leave it is right, and
for this reason before the reorder one.

**If it is ever closed**, the place is the boundary of `onProtocolNegotiated( )`, not an initiator:
catch there, and when operations are pending record the error and initiate the close *without
completing anything*, returning true so the establisher's epilog is not reached and the pending
handlers take the one terminal; when nothing is pending, rethrow. That needs one more additive
primitive in the accounting — "fail while operations are pending" — and is an owed-list item, not this
change-set's.

### 11.8 Found here, not fixed here

- **The command a throwing post leaves in the mailbox.** Before this change it was never applied,
  because the mailbox was dead; now the next drain applies it. The request task treats a throw from
  `submit( )` as `Failed` and not retryable, and releases the slot and the connection at once
  (`HttpClientRequestTask.h:731-760`), so there is no retry to duplicate; the late closure then
  reaches `applyClosed( )` with `m_connection` already reset, so the slot is not released twice
  (`:1403-1410`, `:1727`), and the completion flags keep the answer from being given again — though
  `m_outcome` is overwritten by the late closure's outcome after the task was answered
  (`:1213-1216`), which nothing then reads. What remains is the request reaching the origin once
  after its caller was told it failed: the "not provably unprocessed" ambiguity h1 writes down for a
  write that threw (`:688-693`), not written down for the mailbox. Owed list.
- **The establishment route's TLS half.** On a TLS connection the first `notifyReady( eptr )` of
  §11.7 starts the protocol shutdown with the read and write still pending — the very thing the
  accounting's class comment says it exists to prevent, and this route bypasses it. Pre-existing,
  named by h2's own `onTaskStoppedNothrow( )` comment, not read to the end here.
- **The design's own method.** §5.2 asked the count question once and generalised; the correction is
  in place there.

**What this review does not claim.** Nothing was compiled or executed except tier 1. The red and the
green are the lane's. The x86 debug object is unmeasured. §11.3's race and §11.7's double completion
are derived from the source; neither has been observed, and a control for §11.3 would need a
fault-injecting allocator and a racing completion.

**Agreement.** Agreed, with two things carried into the merge, both in the lane's power: the three
comment corrections of §11.5 (1-3), with the module's green run again or the line counts preserved;
and a decision on §11.3 recorded here — the due-terminal clause with its `tasks2` red, which this
review recommends, or the residual written into `abandonOperation( )`'s doc and the owed list. With
those, A3 is accepted; A1, A2 and A4 remain on their own gates.

### 11.9 Follow-up review, 2026-09-23 — `13aac19`, the two conditions of the second gate

**Reviewer: Claude Fable 5.1, reading `a3-h2-accounting` @ `13aac19` in the lane worktree, on
`372a397`, both off `lazari2` @ `a723cee`; the worktree is clean and nothing is stashed.** Read at the
commit: the diff whole; `MultiOperationTask.h` whole; the driver's `postCommand( )`, `takeCommands( )`,
`onCommandsPosted( )`, `applyCommands( )`, `applySubmit( )`, `applyCancel( )`,
`onHeaderBlocksProduced( )`, `drainSessionEvents( )` with its five callers, `dispatchEvent( )`,
`onStreamClosedEvent( )`, `onGoAwayReceived( )`, `onConnectionErrorEvent( )`, `scheduleRead( )`,
`onRead( )`, `onPeerClosed( )`, `pumpWrites( )`, `onWrite( )`, the five arms and their handlers,
`cancelTimers( )`, `onDrainDeadline( )`, `closeGracefully( )`, `chkFinishClose( )`,
`closeSubmissions( )` with its five callers, `onProtocolNegotiated( )`, `initiateClose( )`,
`cancelTask( )`, `onTaskStoppedNothrow( )` and the four entry points; `Session.h`'s `feed( )`,
`onTimer( )` and every `raiseConnectionError*( )` caller; `TaskBase.h`'s handler macros,
`notifyReadyImpl( )`, `requestCancelInternal( )`, `scheduleNothrow( )` and the `onTaskStoppedNothrow( )`
contract; both `onTaskStoppedNothrow( )`s in `TcpBaseTasks.h`; `TcpSslBaseTasks.h`'s
`scheduleTaskFinishContinuation( )`; `TcpStrandedStreams.h`'s `postToStrand( )`, `cancelTask( )` and
`shutdownSocketOnStrand( )`; h1's `scheduleTask( )` and `scheduleRead( )`; `ExecutionQueue.h`'s flush
family; the new `tasks2` probe and case beside the existing probe; the `h2client7` seam and case; the
timer race record's §1-§2; and, outside the repo, the A3 sections of the lane's journal and every file
in `logs/a3/`. **Nothing was built.** Measured here: the `tasks2` object on disk, its sections and the
probe's symbols, and the size gate's read-only summary over the lane's tree. **Line numbers are at
`13aac19`.**

**Verdict: both conditions are met, and A3 — both commits — should be accepted and merged.** The
due-terminal clause is genuinely narrow, cannot fire under the task lock, and is additive in effect for
every existing caller (§11.9.2); its red is a red, its derivation is on the record, and the case's own
bug was a real one (§11.9.4). The three corrected comments now say what the code does (§11.9.1).
Carried into the merge, in the lane's power and touching no code: **two comments that are false by the
letter** — one this follow-up introduced in the core primitive's class note, one it left in the header it
rewrote — and four precisions, every one within its existing line count so nothing need be rebuilt
(§11.9.5). The lane's two qualifications are accepted, the first with a precision (§11.9.3). The
`tasks2` size move belongs to the earlier figure, not to this commit, and does not bear on the merge
(§11.9.6).

#### 11.9.1 Condition 1 — each corrected comment re-verified at the source

1. **`chkArmIdleTimer( )`'s catch (`:2220-2224`)** — "both callers hold at least one operation". The
   callers are `:1432` and `:2755`, with no third. `onStreamClosedEvent( )` is reached only through
   `dispatchEvent( )` from `drainSessionEvents( )`, whose five callers (`:768`, `:1622`, `:1847`, `:1971`,
   `:2465`) are in handler bodies except `:768`, in `applyCancel( )` — which at negotiation returns at
   `:744-748`, because no stream has `isHeadersProduced` before the first `produce( )` (`:1701`), and
   `onHeaderBlocksProduced( )` resets without draining (`:803-807`). The body pump drains nothing and
   begins nothing (`:896-1043`). The negotiation-chain call at `:2755` follows the write (`:1716`,
   unconditional: the session's constructor queues the opening frames) and the read (`:1539`). **True.**
2. **`armDrainDeadline( )`'s catch (`:2376-2385`)** — callers `:1488` and `:2459`, both in handler bodies
   by the same routes, `closeGracefully( )`'s own callers being `:1441`, `:1453` and `:2260`. A
   `ConnectionError` is raised only from `Session::feed( ):812` and `Session::onTimer( ):888`
   (`raiseConnectionErrorFromException( ):1550` has the one caller at `:812`), which the driver calls
   only from `onRead( ):1616` and `onSettingsDeadline( ):1969`. **True.** The `m_drainTimer.reset( )` at
   `:2388` is safe — an `async_wait( )` that threw registered nothing — and `cancelTimers( )` tests the
   pointer (`:2315`); every re-arm is behind `isClosing( ):2352`, which the epilog sets on the rethrow,
   so "the principle and not a defect" is right.
3. **`pumpWrites( )`'s catch (`:1733-1741`)** — the gate is `initiateClose( ):2798`, verified; after the
   rethrow the epilog records the first error and every pump returns at `:1685`. **True, with one word
   wrong.** The `close_notify` at stake is not "a deliberate TLS close's" (`:1738`): the close that
   follows a pump throw is the first-error close, and no deliberate close can follow a recorded error.
   The cost is real all the same — `TcpSslBaseTasks.h:471-518` runs the protocol shutdown on a failed
   task too, unless `m_wasSocketShutdownForcefully` says the send side is already gone — so what the
   flag protects is the **failure** close's `close_notify`. Precision, §11.9.5.
4. **`scheduleRead( )`'s header (`:1506-1529`)** — the one deadlocking site is stated exactly (`:1710`
   arms before `:1716` begins), and the h1 paragraph now says what §4 says: h1's `scheduleTask( ):1605`
   reaches `scheduleRead( ):852` with nothing begun, and its catch at `:871` completes inline. **True,
   except for one universal.** "No OTHER site of the eight is reachable with the count at zero"
   (`:1518`) is false at `postCommand( )`, the eighth: `m_isStrandReady` is published at `:2736`,
   fourteen lines before the first strand-side begin at `:2750`, so a caller can begin the mailbox's
   operation at count zero during negotiation — and the follow-up's own comment at `:528-529` says the
   count reaches zero there after the last handler. The deadlock argument never applied to that site,
   which holds no task lock; the sentence means "no other site *on the strand*", and should say so.
   Required, §11.9.5.
5. **`MultiOperationTask.h:233-239`** — "NOT every caller holds it" is true of the eighth site.

Neighbours re-read and still true: `postCommand( )`'s lock-order note (`:463-470`);
`chkArmSettingsTimer( )`'s "THE SITE THAT SETTLES ITS SHAPE" (`:1917-1919`); `initiateClose( )`'s
comment (`:2760-2794`); `cancelTimers( )`'s invariant paragraph (`:2279-2283`), which names
`onTaskStoppedNothrow( )` as on the strand "by argument", and it is that argument §11.9.2 qualifies;
`MultiOperationTask.h:352-354` on `initiateClose( )`, untouched by a clause that passes `close` false.

**One neighbour is no longer true, and this follow-up made it so.** `MultiOperationTask.h:62-63`:
*"initiateClose() and the terminal notifyReady() are invoked ONLY from onOperationCompleted()"*. Since
`13aac19` the terminal is also taken from `abandonOperation( )` — `takeTerminalNoLock( )` at `:280`,
`applyDecision( )` at `:291`. §11.2 checked this note and found it true at `372a397`. It is the class's
stated invariant, in core, and the first thing the next reader of the accounting will rely on.
Required, §11.9.5.

#### 11.9.2 Condition 2 — the due-terminal clause, judged

**Narrow.** The clause fires only when `takeTerminalNoLock( )` (`:147-157`) is true after the decrement:
closing, count zero, not taken. Under the task lock that cannot be, in every context the lock is held:
a handler body's own operation is outstanding until its epilog (`TaskBase.h:279-283`), so the count is
at least one; the establishment chain runs before the first operation, when the task is not closing —
nothing sets `m_closing` before it: `beginClose( )`'s two callers, `chkFinishClose( ):2487` and
`onPeerClosed( ):1663`, are reached from handler bodies, and the `chkFinishClose( )` that `pumpWrites( )`
can call at negotiation returns at `:2475` with `m_isCloseWhenDrained` false; `cancelTask( ):2833` and
the establisher's `scheduleTask( )` begin and abandon nothing. Off the lock it fires exactly where
§11.3 found the stall — the eighth site, closing, after the last real handler — and nowhere a decider
follows, because the seven strand sites are under the lock. The "close is false on purpose" note
(`:286-289`) holds: `m_closing` is set either with `m_closeInitiated` in the same call (`:438-447`) or
by `beginClose( )` from a handler whose epilog initiates before its own operation is given back, so a
count of zero while closing implies the flag.

**Additive in effect.** No existing member of `MultiOperationTaskT` changed; `takeTerminalNoLock( )`
and `applyDecision( )` are private and untouched. `abandonOperation( )` has nine callers in the tree,
the driver's eight and the probe's `:1609`, and none of the mix-in's other users — h1,
`ClientConnectionTaskBase.h`, `TcpTunnelStage.h`, `Http2TestServer.h`, the `tasks2` and `h2client`
probes — calls it. Every existing caller's behaviour is bit-identical. The `BL_NOEXCEPT_*` wrap and the
release-build fall-through on a zero count are `onOperationCompleted( )`'s own shape.

**The caveat, and the lane's refinement.** The refinement is right in what it says. The task lock is
held on this route too — `notifyReadyImpl( )` takes it at `TaskBase.h:564` before
`onTaskStoppedNothrow( )` at `:604` — and `cancelTask( )` is always reached under it
(`requestCancel( ):1241`), so those two still exclude each other; what the lock never excluded is the
handler `cancelTask( )` posts (`:2837-2842`), which the *strand* used to order after every terminal and
on this route does not. **It is incomplete in scope, twice.**

- *`initiateClose( )` is not excluded either.* `m_closeInitiated` is set under the accounting lock
  (`:444`) and `initiateClose( )` runs afterwards, from `applyDecision( ):185`, with that lock released.
  The window: handler H completes with the count at two — its own and the mailbox's — its epilog
  decrements to one, sets the flag, and calls `initiateClose( )` on the strand; the caller's
  `abandonOperation( )` decrements to zero, finds the terminal due, and runs `notifyReady( )` on its own
  thread while H is still inside `initiateClose( )`. Reachable in exactly §11.3's race, narrowed to
  the duration of that call. §11.3's "a due terminal means `initiateClose( )` already ran" was this
  review's imprecision; it means the flag is set, and is corrected there.
- *The socket is shared as well as the timers.* On cleartext the socket base's `onTaskStoppedNothrow( )`
  shuts the socket down synchronously on the calling thread (`TcpBaseTasks.h:636-638`), and on TLS the
  finish continuation initiates `async_shutdown( )` from it (`TcpSslBaseTasks.h:513-515`) — which is the
  "socket call racing Asio's own handlers" the stranded policy posts its own shutdown to avoid
  (`TcpStrandedStreams.h:194-197`). What can overlap: `initiateClose( )`'s `cancel( ec ):2807` in the
  window above, and `shutdownSocketOnStrand( )` if a cancel was requested. On TLS the driver's
  `onTaskStoppedNothrow( )` is in fact reached on the strand afterwards, from `onShutdownCompleted( )`,
  because `notifyReadyImpl( )` returns at `TaskBase.h:570-572` once the shutdown is scheduled; only the
  initiation is off-strand.

"With the count at zero no operation is left for a lost cancel to strand" is true and stronger than
stated: no wait is pending on any timer or on the socket, so every cancel involved is a no-op, and what
remains is the letter of asio's shared-object rule — on the timers and the socket — in a window that is
an allocation failure, a race, and a few microseconds. Nothing to lose is the right verdict; the
comment should name both things it does not exclude and both objects, because the next reader will be
holding a ThreadSanitizer report. Precision, §11.9.5.

#### 11.9.3 The two qualifications, judged

1. **Accepted, with a precision.** `closeSubmissions( )` has five callers — `:1657`, `:2140`, `:2421`,
   `:2457`, `:2899` — and §11.3 named one; the wording read as exhaustive and was mine. The
   conclusion is unaffected, and the reason is sharper than "none is on the first-error path": four of
   the five close submissions **before** the task is closing (`onPeerClosed( ):1657` before
   `beginClose( ):1663`; `onPingDeadline( ):2140` and `onDrainDeadline( ):2421` before
   `requestCancelInternal( )`; `closeGracefully( ):2457` before `chkFinishClose( )`'s
   `beginClose( ):2487`), so on those routes the window §11.3 describes does not exist. It exists only
   where a first error is raised from a handler or by `chkFinishClose( ):2505`'s throw — and there
   submissions close at the terminal. The lane then wrote the same exhaustive-sounding shape into
   `:525-526`; a nit in §11.9.5.
2. **Accepted as stated.** The `h2client7` red restores the driver from `a723cee`, so it is the pair's
   red and not the follow-up's; the follow-up's driver-side effect — a due terminal taken from
   `postCommand( )`'s catch — is exercised at driver level by nothing, and at the primitive by the
   `tasks2` case alone. The seven strand guards remain unexecuted, as §11.6 said.

#### 11.9.4 The evidence, read rather than taken

The logs exist now and were read. `tasks2-abandon-RED.log`: `TestMultiOperationTask.h(1691): fatal
error ... critical check ( stopped ) has failed`, 15,014,736 us. `tasks2-abandon-GREEN.log`: the case
alone, 24,764 us, `No errors detected`. `utf_baselib_tasks2-utf.log`: 14 cases, 3.63 s, no errors; the
build log of that run has no compile line, so the object was already current from the final source.
`h2client7-RED.log`: `TestHttp2DriverAccounting.h(418) ... ( stopped ) has failed`, 15,002,529 us;
`h2client7-GREEN.log`: 2,188 us, no errors. `h2client2` 14, `h2client3` 3, `h2client6` 1 — no errors.
`tier1.log`: two violations, both `C1 case ADDED`, EOL PASS — as reported.

**The derivation is sound.** The red and green `tasks2` logs carry the same test line numbers
(`:1691`, `:1723`), so the red ran the final case source with only the primitive reverted, and both
build logs show a compile. Against a plain decrement the probe's `abandonFromCallerThread( )` leaves
the count at zero, closing, terminal untaken — the stall of §11.3 with nothing else in it — and the
cleanup route (`:1625-1632`) is what turns that into a failure at 15 s rather than a hung module. On
the green path `onTaskStoppedNothrow( )` runs inside the abandon call itself, so `waitForStopped( )`
returns at once and the case cannot flake on its own bound. The rendezvous on `initiateClose( )`
(`:1512-1526`) is the right one: it is the first moment the count is one and that one is the phantom,
and it hands the abandon to the test thread while H may still be inside `applyDecision( )` — the
window of §11.9.2, exercised.

**The case's own bug was real.** `flushAndDiscardReady( )` passes `nothrowIfFailed` false
(`ExecutionQueue.h:303-311`) and its doc says it throws when a ready task failed; this task fails by
design. `forceFlushNoThrow( true )` (`:1698`) is right, after the assertions have read the state.

#### 11.9.5 Carried into the merge — comment corrections, every one within its line count

Required, because each is false by the letter:

- **`MultiOperationTask.h:62-63`** — "invoked ONLY from onOperationCompleted()": name
  `abandonOperation( )` as the second source of the terminal, and only of the terminal. The paragraph
  is six lines; it can say so in six.
- **`Http2ConnectionTask.h:1518`** — "No OTHER site of the eight" → "no other site on the strand"; the
  eighth is reachable with the count at zero and the comment below it says so.

Precisions, in the same pass:

- **`:531-537` and `MultiOperationTask.h:250-252`** — the caveat's scope: it does not exclude
  `initiateClose( )` in the flag-to-call window either, and the socket is shared as well as the
  timers; "the close has already been initiated" means the flag is set.
- **`:1738`** — "a deliberate TLS close's" → the failure close's.
- **`:2857-2859`** — "always a strand handler" is no longer true; the route is named at
  `postCommand( )` and should be named here, where a reader will look for it.
- **`:525-526`** — "closeSubmissions( ) runs from the terminal" → "on this path".

Not owed: `TestHttp2DriverAccounting.h:71-72` says the case disarms the seam "on every path out"; a
throw from `:365` or `:370`, before the `submit( )`, would leave it armed. Allocation-class only, in a
test; an RAII disarm would make the sentence literal.

**`__LINE__`.** Every item above fits inside the lines it replaces. If the lane keeps the counts, the
greens of `13aac19` stand; if it does not, the modules are rebuilt and rerun as they were for this
commit.

#### 11.9.6 The `tasks2` size move — the earlier figure is the outlier

What the follow-up adds to the `tasks2` translation unit is strictly additive: 40 lines in
`MultiOperationTask.h` and 259 in the test header, zero deletions, and the driver is not among its
dependencies (`UtfBaselibTasks2Main.d` names `MultiOperationTask.h`, `TaskBase.h`, `TcpBaseTasks.h`
and the two test headers). An addition cannot shrink an object under fixed flags, so the two figures
were not produced the same way. The surviving evidence supports only today's:

- `UtfBaselibTasks2Main.o` is **27,467,864 bytes = 26.20 MiB**, read three ways — `ls`, the size
  gate's read-only summary (`utf_size_gate.py:52` defines "MB" as 1024²; `:133` measures the `.o`),
  and the journal's row.
- The lane's own history for this module on this platform, before A3, is **27,151,448 →
  27,165,352 bytes = 25.9 MiB** (`lane1.md:280`, `:336`). Today's object is +302 KB over that — the
  probe's cost: its own symbols are 19,303 bytes of text and data across 173 symbols, and 473
  `.debug_str` entries name it, with `.debug_str` at 6.76 MB the object's largest section.
- The `372a397` figure, **27.21 MiB = 28.5 MB**, would be +1.37 MB over that baseline for a 30-line
  `abandonOperation( )`. No artifact of it survives: the object was overwritten at 20:17, no build log
  of that round is in the state directory, and §11.6 measured `h2client7` independently but not
  `tasks2`.

The reading that fits every number is a unit slip in that one row: **27.21 read as decimal megabytes
is 27,210,000 bytes = 25.95 MiB** — the baseline plus the base commit's few kilobytes — and today's
object is 27.47 MB decimal, +258 KB, which is the probe. The `h2client7` row of the same table was in
MiB (§11.6's 37,722,520 bytes = 35.97), so the table mixed units in one row. This is inference, not
measurement; a rebuild of `tasks2` at `372a397` in a scratch checkout would settle it in minutes, and
this review does not build. **Nothing about the merge depends on it**: the module is measured at 26.2
MiB against a 40 MB target, the x86 ceiling is 75 MB, and the orchestrator's release builds produce
their own figures. The lesson is the journal's method — record bytes beside the rounded figure, as its
earlier rows did.

**What this review does not claim.** Nothing was compiled or run; the reds and greens are the lane's
logs, read here. §11.3's race is still unobserved — the `tasks2` case constructs its consequence at the
primitive, not the race in the driver. The x86 debug objects are unmeasured. The origin of the 27.21
figure is inferred, not shown. §11.7 and §11.8 stand as written.

**Agreement.** A3 is accepted — `372a397` and `13aac19` together — with §11.9.5 carried into the
merge in the lane's power: two required comment corrections and four precisions, none touching code,
all within their line counts. The orchestrator's clang release and gcc release runs are where this
change-set's variant and toolchain coverage is earned, and they should record `tasks2`'s size in bytes.
A1, A2 and A4 remain on their own gates.

---

## 12. Implementation review, 2026-09-23 — A2's second gate

**Reviewer: Claude Fable 5.1, reading `a2-write-peer-close` @ `04bcb6f` in the lane worktree, off
`lazari2` @ `a723cee`. Line numbers in this section are at `04bcb6f`.** Read whole, at the commit: the
diff; `NetUtils.h`'s rule and its four predicates; h1's class comment, `onStartRequest( )`,
`onWriteCompleted( )`, `scheduleRead( )`, `deliverBodyChunk( )`, `onBytesRead( )`,
`isCleanEndOfStream( )`, `onPeerClosed( )`, `onReadCompleted( )`, `finishStream( )`,
`closeConnection( )`, `initiateClose( )`, `scheduleTask( )`, `cancelTask( )` and
`onTaskStoppedNothrow( )`; `Http1Codec.h`'s `parseEof( )`; `HttpClientRequestTask.h`'s
`connectionFailureCause( )` and `answerOnClosed( )`; `MultiOperationTask.h`'s `beginClose( )`,
`isClosing( )` and `onOperationCompleted( )`; `TaskBase.h`'s cancel macro; the three `httpclient7`
test files and the rendezvous primitives of `Http1DriverTestUtils.h`; `httpclient4`'s
`ClientSession_AgainstTheLibraryHttpServerTests`; Boost **1.90.0**'s `epoll_reactor.ipp`
`perform_io( )` and `strand_executor_service.ipp`, the dist that ships; the Linux **6.8** kernel's
`sock_error( )` from this host's headers and, from the v6.8 source, `sk_stream_error( )`,
`sk_stream_wait_connect( )`, `tcp_sendmsg_locked( )`, `tcp_recvmsg_locked( )`, `tcp_done( )`,
`tcp_reset( )` and `tcp_fin( )`; the lane's journal, its raw-socket probe and the two run logs it
left; lane1's record of the `httpclient4` failure. **Nothing in the repo was built or run.** The lane's
standalone probe binary was re-run here, three iterations of each order (§12.3).

**Verdict: agree that the implementation should be accepted.** The arm is §10.1's shape (ii) exactly,
the predicate is additive and correctly reasoned, the red is certain for a reason that holds at the
source in Boost and in the kernel, the five cases that must not move were run and not argued, and the
one behavioural consequence the lane did not name is a correction (§12.1). The lane's three findings
against the design are right, and the largest of them re-specifies A1 (§12.5). What is owed is
recorded in §12.8, and none of it blocks this change.

### 12.1 The diff, verified

- **`NetUtils.h` is additive.** +48 lines, one predicate and its comment after
  `isCleanEndOfStreamErrorCode( )`, no existing line changed. *"THE FOURTH PREDICATE ... the three
  above are all READ side"* (`:426`) is true, and the third's own *"THE THIRD PREDICATE ... the two
  predicates above"* stays true. **One neighbouring comment was already stale and is now two behind:**
  the rule at `:334` still says *"Ask one of the two predicates below, and if neither fits, add a
  third HERE"* — false since the third landed, untouched by this change. Owed a one-line correction;
  not this change's defect.
- **The driver is shape (ii) exactly.** `isPeerClosedOnWrite` (`:808-809`) is computed before the
  prolog beside `isOurOwnTeardown` (`:779`); the guard is `if( ! isOurOwnTeardown &&
  ! isPeerClosedOnWrite )` (`:850`); `CHK_CANCEL_IMPL( )` (`:855`) is untouched; nothing else moves.
  The arm order is §2.2's and its reason holds. The −7: *"THE PREDICATE IS THIS TASK'S STATE AND NOT
  THE ERROR'S CODE"* would have been false of a function whose second arm is a code predicate, and
  *"only the error of OUR OWN teardown is swallowed here"* false once two endings are. Both
  corrections were necessary and are correct.
- **One consequence the lane did not name — benign, and unexercised on either side.** An external
  `cancelTask( )` (`:1676`) posts a forced `shutdownSocket( )` (`:1693`); a composed write between two
  steps then completes `broken_pipe`, which before this change went to `CHK_EC( )` and failed the task
  with the transport's code, and now passes the guard and reaches `CHK_CANCEL_IMPL( )`, which fails it
  with `operation_aborted` (`TaskBase.h:159-163`) — the code the comment above the arm says the
  accounting requires. A correction rather than a regression; a write the cancel reaps while it is
  registered was `operation_aborted` before and still is. No case cancels an h1 driver, in
  `httpclient3`, `httpclient7` or the test utils, so nothing measured it.

### 12.2 The evidence, judged

**The certainty argument is sound, and each premise it rests on was found at the source.**

1. *The reset finds a write op and no read op.* `scheduleRead( )` is called from `onReadCompleted( )`'s
   `else` branch **after** `onBytesRead( )` returns (`:1233`), and `onBytesRead( )` calls the sink
   synchronously through `deliverBodyChunk( )` (`:975`). A sink blocked inside `onData` is a read
   handler that has not returned, on a connection with nothing registered for reading. The case's
   `ResetWhileStrandIsHeldSink` (`:309-325`) takes its gate *before* the inner delivery, so the case's
   own body rendezvous cannot return while the strand is held.
2. *The write handler is enqueued on a locked strand and the read can only queue behind it.* The
   socket's executor is `asio::make_strand( )` (`TcpBaseTasks.h:2460`), so every completion here goes
   through `strand_executor_service`: `enqueue( )` (`strand_executor_service.ipp:97-124`) puts an op
   on `waiting_queue_` while `locked_`; `push_waiting_to_ready( )` (`:131-137`) promotes the whole
   queue in order and keeps the lock while anything is ready; `run_ready_handlers( )` (`:140-152`)
   drains in order. The read's completion is posted from `scheduleRead( )`'s speculative `recv( )` and
   reaches `enqueue( )` after the write's did, in every interleaving with the epilog. FIFO holds.
3. *The write must already be parked, and a bound too short can only lose the discrimination.* An
   unparked `async_write( )` has no op registered and its next step is a handler on the held strand.
   After the sink returns, `scheduleRead( )`'s speculative `recv( )` runs **before** the epilog
   releases the strand and consumes the reset itself, so the read classifies first,
   `closeConnection( )` sets `m_closing`, and the write's `EPIPE` is excused by the teardown arm on a
   tree **without** the new one — green, no discrimination. With the arm every assertion still holds
   on that path: the body was delivered before the gate fired, `errorCode` is the read's
   `connection_reset`, the state is not Ready. **`WRITE_PARKS_IN_MILLISECONDS` (`:154`) is safe in
   exactly the direction the lane says**, and the same holds of `STRAND_HELD_AFTER_RESET_IN_MILLISECONDS`
   (`:170`). The only cost of a slower machine is a negative-control run that proves nothing, which
   `notes.txt` says to look at first.
4. *The two losers are explained correctly.* v1's 1 in 20: `perform_io( )` (`epoll_reactor.ipp:783-820`)
   performs the write op before the read op — `j` descends except, write, read — returns the first
   completed op for inline completion and posts the rest, **and the posting is done by
   `perform_io_cleanup_on_block_exit`'s destructor before `do_complete( )` calls `op->complete( )` on
   the first** (`:830-832`), so a pool thread can reach the strand with the read before the reactor
   thread reaches it with the write. v2's 3 in 20: the composed write not yet parked when the head
   arrived — premise 3.

**What must not move.** The journal records the two barrier cases and the three peer-close cases at
10 of 10 each after the arm and 5 of 5 before, the whole module green 3 of 3, `httpclient3` green;
this reviewer read the lane's `httpclient3` run log (7 cases, no errors) and its `httpclient4` run log
(the one `connectionsCreated` failure, §12.6) in the shared scratchpad, and the built module
(`utf-baselib-httpclient7`, 19:41, after the last source edit) carries the case. **The loop runs
themselves — the 35 of 35, the 30 of 30 and the 50 of 50 — left no log that could be read**; they
are the journal's numbers. §8's *"argued from the arm order, not verified by a run"* is answered on
the lane's record and struck below; a reviewer who wants a file rather than a journal entry re-runs
`notes.txt`'s five recipes once.

### 12.3 Finding 1, verified: the code is decided by an exchange, not by timing

Re-ran the lane's probe (`rstprobe.c` — raw sockets and epoll, the case's wire shape): three
iterations of each order, identical every time. `epoll` reports `IN OUT ERR HUP` (0x1d); `send( )`
first → `ECONNRESET`, the `recv( )` after it → 0; `recv( )` first → `ECONNRESET`, the `send( )` after
it → `EPIPE`. At the source, kernel 6.8: `tcp_reset( )` writes `sk_err` once (`tcp_input.c:4437-4468`)
and `tcp_done( )` sets `sk_shutdown = SHUTDOWN_MASK` **without** `SOCK_DONE` (`tcp.c:4488-4512`);
`sock_error( )` is `xchg( &sk->sk_err, 0 )` (`include/net/sock.h:2531-2543`, this host's headers);
`tcp_sendmsg_locked( )` reaches it through `sk_stream_wait_connect( )` or `sk_stream_error( )`
(`net/core/stream.c`), both of which fall back to `-EPIPE` once the error is taken;
`tcp_recvmsg_locked( )`'s no-data path calls `sock_error( )` directly and, finding nothing, returns
0 on `RCV_SHUTDOWN`. **Deterministic given the syscall order. The lane's conclusion stands, and §2.3 is
corrected in place.** One precision, for the record and for the two comments that carry it
(`NetUtils.h:440-441` and the case's header): the exchange is `sock_error( )`'s; `sk_stream_error( )`
is only the send path's caller of it, and the receive path never goes through `sk_stream_error( )`.

**And one fact the probe did not print, which §12.5 rests on.** `tcp_reset( )` writes `ECONNRESET`
only from a state that has received no FIN — the `default` arm, ESTABLISHED — and `EPIPE` from
`CLOSE_WAIT` (`:4452-4458`); `tcp_fin( )` (`tcp_input.c:4484-4491`) sets `SOCK_DONE`, after which every
empty `recv( )` returns 0 before it looks at `sk_err`. So on POSIX **a write that completed
`connection_reset` proves the peer's ending was a reset with no FIN before it**, and a write that
completed `broken_pipe` proves nothing against the read's own code: the read took the reset itself,
or a FIN preceded it, or the pipe is ours.

**(ii)'s premise, §8's largest unmeasured claim, held on Linux in every run — in the letter.** The
pending read is always told; the code is `connection_reset` when the read reached the reset first and
`eof` when the write did. Windows is still owed to the matrix, where the question is the opposite one —
whether the read there ever sees `eof` for a reset at all; the peer-close record says it sees the
reset spellings.

### 12.4 Finding 2: §10.1 overstated the outcome; (ii) still stands

The row said the read side *holds the only machinery that can tell a complete close-delimited
response from a truncated one*. True of the machinery, false of the outcome on the write-first
ordering, where that machinery is handed `eof` and reaches the wrong answer while the write held the
only evidence. **(ii) stands, for the reason that was always the real one:** the read side holds the
parser and the bytes, so a write handler that ended the stream would reset the parser under a
response still arriving and drop what the reactor already holds — §2.2's early response — while a
write handler that says nothing loses nothing. The evidence question is separate, and it is A1's
(§12.5). The row is corrected in place.

**The same overstatement is in the landed code**, in the arm's third paragraph
(`Http1ConnectionTask.h:797-798`, *"the only side holding a parser that can tell a complete
close-delimited response from a truncated one"*) and in the commit message. The message is history;
the comment is owed the correction, in the same line count — the macros bake `__LINE__` in and the
module's green is against this object — either at the merge or with the change-set that alters what
the sentence describes (§12.5).

### 12.5 Finding 3: the misreport is real, pre-existing, not A2's to fix — and it invalidates A1 as recorded

**The caller's answer is the stream's verdict, verified.** `answerOnClosed( )`
(`HttpClientRequestTask.h:1325`) fails the request only on `event.errorCode` (`:1338`);
`connectionFailureCause( )` (`:1300`) is consulted only then and is *"CHAINED AND NOT SUBSTITUTED"*
(`:1354`). A stream that ended `closed:ok` is a completed response whatever the connection task did
beside it. So before A2, on the write-first ordering, the caller already received the 16-octet body
as a complete 200 while the connection task failed unread; A2 removes the failure and changes nothing
the caller sees. **Not a regression in the answer**, and the premise the lane says so on holds.

**The "by accident" claim, verified.** Without A2, under the held strand, the read handler re-arms at
`:1233` with `isClosing( )` still false — the write handler has not run — and only then does the
write's `CHK_EC( )` reach `onOperationCompleted( )`, which sets `m_closing`
(`MultiOperationTask.h:367`) and runs `initiateClose( )`; the read's `eof` handler then finds
`isClosing( )` **true**, §3.2's gate would have skipped `onPeerClosed( )`, and the task's failure would
have reached the sink through `onTaskStoppedNothrow( )` as `connection_reset` — the right verdict
for a reason nobody chose. With A2, `isClosing( )` is false there and the gate is blind. **A1 as
recorded does not close this face, and after A2 it never did by accident either.**

**Declaring `Content-Length` was the right call.** A case over the close-delimited framing would
have pinned a wrong answer as green, and the write arm — this change-set's subject — is exercised
either way; the reason is in the case's comment, with the measurement beside it.

**The candidate shape — the write records, the read consults — is necessary and not sufficient.**
Necessary because the write's code is the only evidence there is (§12.3). Not sufficient because the
read's `eof` handler can run **before** the write's handler: `perform_io( )` posts the read op before
it completes the write op inline (§12.2, premise 4), which is v1's 1 in 20, and a record not yet
written is a record the read cannot consult. The re-specification below supplies the second half.

#### A1, re-specified 2026-09-23 — three faces of one symptom

*The symptom:* a close-delimited message completed on an ending that was not the peer's orderly
close. *The three endings that produce it:*

- **Face 1 — our own teardown** (§3.1-3.2 as recorded): the composed TLS read re-arms past
  `initiateClose( )`'s cancel and observes an ending we caused. Gate `onPeerClosed( )` on
  `! isClosing( )`. Unchanged. *Corrected 2026-09-24 by the implementation review of A1-tls — §15.2,
  §15.5: unchanged in shape and in placement — asked at the observation, never at the delivery — and
  not unchanged in what it covers. After A2 nothing reaches this gate with a live parser unless
  `isCanceled( )` is true as well, so it has no red the cancel check of face 2 does not also fix; it
  lands as defence-in-depth by the maintainer's decision, with the matrix's row 2 as its control and
  its trigger recorded in §15.5.*
- **Face 2 — the external cancel** (§3.2, point 3): unchanged; the lane decides with the case in hand.
  *Decided 2026-09-24: `CHK_CANCEL_IMPL( )` narrowed to a live parser, ahead of the arm — the two
  candidates it ruled out are judged in §15.3. In this tree it is the gate that closes the symptom.*
- **Face 3 — the reset the write consumed** (new, this review). One RST sets `sk_err` once and the
  first syscall to reach it takes it. The write's `send( )` is first whenever both ops are registered
  — `perform_io( )`'s order — and whenever the composed write's next step runs before the read's
  re-arm; the read is then handed plain `eof`, `isCleanEndOfStream( )` (`:1115`) admits it, and
  `parseEof( )` (`:1165`) completes whatever arrived. Cleartext and TLS alike: the TLS engine reports
  the transport's `eof` as `stream_truncated`, which `isCleanEndOfStream( )` admits on purpose (RFC
  2818 §2.2.2). Measured on the lane's probe, 7 of 8 before A2 and 8 of 8 after.

*The rule, from §12.3:* **a write that completed `connection_reset` is proof the ending was a reset;
a write that completed `broken_pipe` is not evidence against the read's own code.**
`net::isPeerClosedErrorCode( writeEc )` asks this of a write's code on POSIX — it admits
`connection_reset` and refuses `broken_pipe`; *it also admits `eof`, which no write completes on
cleartext — §13.4* — and on Windows it admits the reset spellings a write may have got AFTER a FIN,
so the record is redundant there only if the pending read carries the reset for itself, and it is
**not harmless in one ordering**: a peer that half-closes and only then aborts would have its
FIN-framed message reported as a reset. *(Corrected 2026-09-23 by the implementation review, §13.3, on
the lane's finding; the sentence read "admits the collapsed spellings that the read there sees for
itself, so the record is redundant and harmless".)* If the lane wants it named —
`isPeerResetOnWriteErrorCode( )` — it is that expression with this reasoning beside it in
`NetUtils.h`, not a new comparison.

*The shape:*

1. **The write records and does not act.** `onWriteCompleted( )` keeps the ending's code in one
   member, written before the prolog beside `isPeerClosedOnWrite`, cleared where the write's storage
   is released. Shape (ii) is untouched.
2. **The read consults the record.** `onPeerClosed( )` with a live parser: if the recorded write code
   says reset, the ending is unclean whatever the read's code says — the unclean branch, **with the
   write's code**, so the caller sees *"connection reset by peer"* exactly as
   `Http1Driver_PeerResetsMidCloseDelimitedBodyTests` requires when the read sees it first.
3. **The read defers while the write is in flight.** When the read observes an end of stream with a
   live parser **and `m_isWriteInFlight`**, the write handler has not run and will: `initiateClose( )`
   already shuts the send side for a write in flight, so a parked write wakes with `broken_pipe`, one
   already failed at the syscall carries `connection_reset`, and a cancelled one `operation_aborted`.
   The read therefore records the ending, delivers nothing, calls `closeConnection( )` and completes
   its operation; **the write handler delivers `onPeerClosed( )`**, with the write's code if it is a
   reset and the read's otherwise. Both handlers are on the strand, so the flag is read consistently
   and the deferral is deterministic — it no longer matters which handler the dispatcher ran first.
   Without a write in flight nothing but the read could have consumed the reset, and nothing changes.
4. **Face 1's gate is asked of the read's observation, when it is observed.** The deferred delivery of
   step 3 runs with `isClosing( )` true by construction and must not be refused by it.

*Why the deferral gets the FIN cases right:* a peer that half-closes and keeps reading completes our
write normally, and the verdict is the read's; one that half-closes and never reads leaves the write
parked until our shutdown wakes it with `broken_pipe` (`SEND_SHUTDOWN`); a FIN followed by a RST is
`EPIPE` from `CLOSE_WAIT`. In every one the write's code is not a reset and the message the FIN
delimited completes, as it should. No hang: the deferral waits on a handler the epilog's own
`initiateClose( )` guarantees.

*The reds.* **R1**, the record path: the lane's close-delimited probe under the landed case's
held-strand arrangement, which makes the write handler run first for certain — body
`part-onepart-two`, a non-empty code, `! taskFailed`, not reusable. It is red today (8 of 8 with A2)
and is face 3's red. **R2**, the deferral path: a close-delimited answer, a partial body,
`shutdown( SHUT_WR )` and a peer that never reads — the read sees `eof` with the write in flight,
certainly, because the write cannot complete without the peer reading or our shutdown; the message
must complete `closed:ok`, the body delivered, the task clean. R2 is a control rather than a red —
today's tree completes that message too — but it pins the deferral's hang-freedom and the FIN
verdict. The interleaving that motivates the deferral (the read's `eof` handler first, the write's
`connection_reset` still in flight) cannot be arranged from a test; it differs from R2 only in the
code the write handler carries, and the selection of that code is what R1 pins. Whether every branch
having a certain test is enough where the race itself has none is the lane's call under
`src/utests/AGENTS.md`; this review says it is.

*Sequencing — proposed, not decided.* Face 3 needs no TLS and has its red in hand; faces 1 and 2 wait
on the `tls-h1-control` fixture (§3.4). **Split A1 into A1-cleartext (face 3) and A1-tls (faces 1-2)**,
A1-cleartext runnable now — before A4 if the maintainer prefers the larger defect first, which is
§6's own ranking. The arm comment of §12.4 and the two `isCleanEndOfStream( )` / `onPeerClosed( )`
comments (§12.7) ride with A1-cleartext, which is what changes what they describe.

*What stays open:* the Windows spelling of a write into a reset connection (§8, unchanged) and
whether the read there ever sees `eof` for a reset; whether `ssl::stream` hands a write the
transport's `connection_reset` unchanged (asio passes transport errors through the engine; not
measured); h2 has no face 3, because its messages are framed (§10.1).

### 12.6 The `httpclient4` failure is not this change's

`ClientSession_AgainstTheLibraryHttpServerTests` (`TestClientSession.h:2046`) runs a GET and a POST
over cleartext HTTP/1.1 through the ALPN fallback against the library's `HttpServer`, which answers
every request with `Connection: close`; both requests are small and each write completes long before
the server's close, so the write arm has nothing to classify. Measured 3 of 30 red on both sides by
this lane; 2 of 30 on both sides by lane1 on its own slice, and 6 of 90 against 7 of 90 across H04a's
gate; the mechanism lane1 records — an extra connection out of the pool's adoption window — is the
pool's. The lane's `run-hc4.log` is that failure on the fixed tree, at the same assertion. Not A2's.

### 12.7 Comments checked against the code they describe

Unchanged by this diff and still true: `onWriteCompleted( )`'s *"the read loop has been armed since
the task was scheduled"* (`:858-859`); `finishStream( )`'s barrier and shutdown paragraphs;
`initiateClose( )`'s composed-write paragraph and its *"parseEof( ) would complete a half-received
close-delimited body"*; `scheduleTask( )`'s *armed FIRST*; `onTaskStoppedNothrow( )`'s *"a write
which failed, or a cancel, completes the task through the handler macros"* (`:1712`). **Now known to
claim more than they can, and owed to A1-cleartext, not to this change:** `isCleanEndOfStream( )`'s
*"ended in a way a message framed BY that ending may be declared complete on"* (`:1103`) and
`onPeerClosed( )`'s *"a classification that admitted the Windows reset spellings would turn an
aborted transfer into a short response reported as a success"* (`:1121` onward) — on POSIX the
admitted `eof` does the same when the write consumed the reset. Stale before this change and untouched
by it: `NetUtils.h:334` (§12.1). Born overstated in this change: the arm's third paragraph (§12.4).
Substance right, function misnamed: *"sk_stream_error( ) takes it with an exchange"* in the
predicate's comment, the case's header and the commit message (§12.3).

### 12.8 Agreement, and what is owed

**Agreed; A2 is accepted on this review**, with nothing carried into the merge as a condition. Owed,
and recorded here so it is not rediscovered: the arm comment's overstatement (§12.4, same line count,
at the merge or with A1-cleartext); the four-predicate rule's stale sentence (§12.1); the two
read-side comments (§12.7); the Windows spelling (§8); and this design's status line, which still says
*not implemented* — A2 is implemented and accepted, A3 is accepted on §11, A1 is re-specified above
and A4 is unstarted.

**What this review could not settle by reading:** the loop counts (§12.2 — a journal, not a log);
Windows (§12.3, §12.5); the TLS spelling of a write's reset (§12.5); and whether the sink of a request
cancelled mid-write is harmed by its code now being `operation_aborted` rather than `broken_pipe`
(§12.1 — no case exists, and the request task was read only at `answerOnClosed( )`).

---

## 13. Implementation review, 2026-09-23 — A1-cleartext's second gate

**Reviewer: Claude Fable 5.1, reading `a1-cleartext` @ `6632469` in the lane worktree, off
`a2-write-peer-close` @ `04bcb6f`. Line numbers in this section are at `6632469`.** The specification
under review is §12.5, which this reviewer wrote; it is reviewed here as if by someone else, and two
of its own sentences are found wanting (§13.3, §13.4). Read whole, at the commit: the diff;
`NetUtils.h`'s rule and its five predicates; h1's class comment, the member block, `onStartRequest( )`,
`onWriteCompleted( )`, `scheduleRead( )`, `deliverBodyChunk( )`, `onBytesRead( )`,
`isCleanEndOfStream( )`, `onPeerClosed( )`, `onReadCompleted( )`, `finishStream( )`,
`closeConnection( )`, `initiateClose( )`, `scheduleTask( )`, `cancelTask( )`, `onTaskStoppedNothrow( )`,
`submit( )`, `cancel( )`, `onCancelStream( )` and `deriveIsReusable( )`; `TaskBase.h`'s handler macros;
`MultiOperationTask.h`'s `beginClose( )`, `isClosing( )` and `onOperationCompleted( )`;
`HttpClientRequestTask.h`'s `outcomeOnClosed( )`, `applyClosed( )` and `answerOnClosed( )`;
`TcpBaseTasks.h`'s `shutdownSocket( )` and both `isStreamTruncationError( )`s; the whole of
`TestHttp1DriverWritePeerClose.h`, the reset case of `TestHttp1DriverPeerClose.h`, and the rendezvous
primitives of `Http1DriverTestUtils.h`; `windows-peer-close-error-codes-record.md`; the lane's journal;
the lane's two probe sources and, in the scratchpad copy of the v6.8 source, `tcp_reset( )`
(`tcp_input.c:4437`) and `tcp_fin( )` (`:4484`). **Every log in `logs/lane3-a1/` was read, not
summarised from the journal.** Nothing in the repo was built or run; the lane's two kept
`httpclient4` binaries were checksummed against the tree's.

**Verdict: agree that the implementation should be accepted**, with **one condition carried into the
merge** — a comment in the new predicate states a premise this project's own record withdrew, and the
replacement text is supplied in §13.3, same line count — and three non-blocking recommendations
(§13.4, §13.5). **The Windows narrowing the lane named is deferred to the matrix, not taken and not
rejected** (§13.3). The lane's finding against §12.5 is right in its conclusion and rests on the wrong
premise, which is the recurring failure this feature has, and it is corrected in place above.

**A note on the worktree, for the orchestrator.** Between this reviewer's first status check and its
last, the lane worktree's branch became `h01-fix` (HEAD still `6632469`), `Http1ConnectionTask.h` was
touched and `TestHttp1DriverStrandSeam.h` appeared untracked: the H01 lane has branched off this
unreviewed tip and is working there now. Every line quoted below was re-anchored against
`git show 6632469:`, so this review is of the commit and not of a moving tree; but H01 now stacks on
A1-cleartext, so the merge order is A2, A1-cleartext, H01, and the one condition below lands as a
comment edit beneath H01's branch.

### 13.1 The diff, verified — and the two decisions the lane flagged

**The code-only delta is twenty lines** (the rest is comment, and every comment is checked in §13.7):
two members (`:240`, `:250`); `m_writeEndingCode = ec;` before the prolog (`:851`); the deferred
delivery in the write handler (`:913-920`); the consult (`:1251-1253`) substituted into the existing
test (`:1255`) and the unclean branch's `finishStream( )` (`:1266-1270`); the deferral in the read
handler (`:1361-1368`); the release (`:1521`); and the fifth predicate (`:519-522`).

**Decision 1 — the record is written raw, unfiltered by either arm: verified, and necessary.** The
interleaving the deferral exists for is: peer RST with both ops registered; `perform_io( )` performs
the write's `send( )` first (`connection_reset`) and posts the read op (`eof`) before completing the
write op inline (§12.2, premise 4); a pool thread reaches the strand with the read first. The read
handler then finds `m_parser && m_isWriteInFlight` (`:1361`), records `eof`, calls `closeConnection( )`
(`:1370`) → `beginClose( )` (`MultiOperationTask.h:239`) → `m_closing` set. The write handler runs
next, on the same strand, with `ec == connection_reset` and `isOurOwnTeardown == true` (`:801` —
`ec && isClosing( )`). A record filtered by that arm would be empty exactly here, the consult would
find nothing and `eof` would complete the truncated body — the defect, at v1's 1 in 20. Recorded raw,
the consult finds `connection_reset` and the caller sees the reset. **The lane's reasoning holds at
the source.**

**Nothing else can write a misleading code there.** Two writers only (`:851`, `:1521`); one reader
(`:1251`). A misleading code is one the fifth predicate admits while the ending it is consulted
against was orderly, and every route to one is closed: (i) *a previous message's record* cannot reach
the next message — `finishStream( )` clears it under `! m_isWriteInFlight` (`:1516-1521`), and a
stream that ended with the write still in flight is published Draining, never Ready (`:1474-1475`),
so no next message exists to consult it; (ii) *our own teardown* never spells `connection_reset` —
`shutdownSocket( )` is `shutdown_send` + cancel, which a parked write meets as `broken_pipe` or
`operation_aborted`, both refused by the predicate; (iii) *a write that ended cleanly* records the
empty code, which the predicate refuses; (iv) *a write reset while the parser is live* is consulted
against the `eof` that reset left — after `tcp_reset( )` → `tcp_done( )` the connection is gone, so no
later orderly ending can follow on that socket; (v) `operation_aborted` from an external cancel is
refused. The one code the predicate admits that a write should never carry is `eof` — §13.4.

**Decision 2 — the consult adds no branch: verified everywhere the test's result is used.** The old
`closeCode` was used twice in `onPeerClosed( )`: the `isCleanEndOfStream( )` test and the unclean
branch's `finishStream( )`. Both now take `endCode`. The clean branch never reads `closeCode` — it
runs `parseEof( )` and finishes on the parser's own verdict (`:1275-1300`) — so no use was missed.
`isRetryable` is `! m_requestMayHaveBeenSent` in every branch, and it is right on the deferred path
because the write handler's zero-octet correction (`:865-880`) runs before the deferred delivery
(`:913`), as the lane says it does.

**The deferral, read for what could go wrong with it.** (a) *No hang*: `closeConnection( )` →
`beginClose( )`; the read handler's own epilog reaches `onOperationCompleted( )`, which runs
`initiateClose( )` once when `m_closing && ! m_closeInitiated` (`MultiOperationTask.h:371`);
`initiateClose( )` shuts the send side down under `if( m_isWriteInFlight )` (`:1740`). Could
`initiateClose( )` already have run? Only after `m_closing` was set, and `onStartRequest( )` returns
on `isClosing( )` (`:614`) above the point that sets the flag (`:729`), so no write starts after it —
and one already in flight at that earlier run was woken by it. A write that already failed at the
syscall has its completion queued; one the cancel reaps carries `operation_aborted`; one that
completes *successfully* after the read's `eof` records the empty code and the read's own code decides
— all three end at the write handler, which delivers. (b) *No re-arm*: the end-of-stream branch
never calls `scheduleRead( )` (only the `else` at `:1379` does), so a second ending cannot arrive
behind a deferred one. (c) *Nothing lost by delivering nothing*: an admitted ending is a read that
transferred zero octets — asio reports data and the error on separate completions. (d) *A stream
ended in between*: a stream cancel posted between the two handlers runs `finishStream( )` and resets
the parser; the deferred delivery then meets `! m_parser` (`:1231`) and returns, the code having been
cleared first (`:917`). (e) *Once the read has deferred, the write's own error is always excused*
(`isOurOwnTeardown` is true by construction), so a genuine write error in that window can no longer
fail the task — acceptable, since the conversation is over and the stream's verdict is delivered, but
it is a property worth knowing. (f) *Under TLS* the composed write's next step fails at the transport
and the `io_op` hands the code to the handler, so the write handler runs whatever the code; the
deferral's safety does not depend on the TLS spelling (§13.9 for what does).

**The release: correct in both condition and order.** The consult at `:1251` precedes the clear at
`:1521` inside the same delivery, and the clear is skipped while a write is in flight — the case a
deferred delivery may yet need it.

### 13.2 The evidence, read

- **R1** (`TestHttp1DriverWritePeerClose.h:844`): `r1-red.log`, five runs, each failing at the
  same fatal error — the code assertion (`Http1DriverTestUtils.h(87)`) with
  `events: headers:200:final|data:8|data:8|closed:ok` — and UTF stops a case at its first fatal
  error, so the five assertions before it passed on every run. **"One assertion of six" is confirmed
  from the log, not the journal.** `r1-green.log`: 5 of 5, `No errors detected`. The discriminator is
  the right one — the defect *is* `closed:ok` — and the other five each refuse a different regression:
  `closed` a deferral that hangs (with the bound's diagnosis), status and body a fix that took the
  delivered octets away, the state a connection published reusable, `! taskFailed` A2's arm on this
  framing. They earn their place. What R1 does not pin is the code's *identity* — §13.4.
- **R2** (`:922`): `r2-before.log` and `r2-after.log`, 5 of 5 each, green. **Deterministic in the
  direction that matters, verified:** the peer's `readRequestHead( )` (`:194`) stops at the blank line
  and never reads again; `BLOCKED_BODY_SIZE` is 8 MB (`:102`) against this host's `tcp_wmem` maximum
  of 4 MB and the peer's 2 KB `SO_RCVBUF` (`:108`), so the write cannot complete before the FIN. A
  host tuned above 8 MB would let it complete, and R2 would then take the *direct* path and still
  pass — a silent loss of the deferral's only coverage, never a false red. `waitForClosed( )`'s bound
  is the hang diagnosis, which is a bound in the harness and not in the arrangement.
- **The branch probe** (`probe-branches.log`, temporary, removed, module rebuilt at 20:38 — the
  `final-hc7-build.log`): R1 delivers directly with `writeInFlight=0` and `resetTakenByWrite=1`;
  R2 **defers**, the write ends `Broken pipe`, `eof` decides; `Http1Driver_PeerResetsMidCloseDelimitedBodyTests`
  consults an empty record; and **A2's own case takes the record path too** (`resetTakenByWrite=1`).
  So part 2 of the shape has two cases and part 3 has one. **Judged sufficient**, on §12.5's own
  argument — the interleaving that motivates the deferral differs from R2 only in the code the write
  handler carries, and the selection of that code is what R1 and A2's case pin — with the weakness
  named in §13.8: nothing committed can tell that the deferral was taken.
- **The kernel rule, measured** (`finrst-probe.log`, 3 of 3): after a FIN then a RST, `send( )` is
  `EPIPE (32)` and `recv( )` is 0; and read at the source in the scratchpad copy — `tcp_reset( )`
  writes `EPIPE` from `TCP_CLOSE_WAIT` and `ECONNRESET` from the `default` arm, `tcp_fin( )` sets
  `SOCK_DONE`. **This supports exactly what §12.5 claims of it**: on Linux a write that completed
  `connection_reset` proves no FIN preceded the reset, so a FIN-framed message can never be overridden
  by the write's code there. It says nothing about Windows, which is §13.3.
- **What must not move**: `mnm-after-*.log`, six cases × 10 runs, all green; the module 3 of 3 at the
  commit (`commit-hc7-run.log`, built after the commit) and 3 of 3 twice before it. `httpclient3`:
  1 of 8 module runs red (`hc3-run.log` run 3, `Http1Driver_InterimResponsesPrecedeTheFinalBlockTests`,
  `stateAfterResponse == Ready`), 0 of 20 in isolation (`hc3-interim-20.log`) — the recorded H01
  reuse refusal, whose mechanism this change does not touch (the deferral fires only on an end of
  stream; that case completes by parsing). `httpclient4`: `hc4-run.log` 2 of 3 red on the fixed tree
  (`connectionsCreated == 2U`, §12.6's mechanism) against 0 of 3 on `hc4-base-run.log`; then
  `hc4-ab.log`, alternating, **base 1 of 10** (run 1, `connectionsCreated`) and **fixed 1 of 10** (run 5,
  `dispatched == 2U`); then `hc4-case-ab-*.log` on the `dispatched` case alone, **base 1 of 30** (run 3)
  and fixed 0 of 30. The alarming first result was load, as the lane says, and the A/B design is the
  right instrument for saying so. `hc4.fixed` is byte-identical to the tree's binary (built 20:46,
  seven minutes before the commit, with no source edit journaled between); `hc4.base` differs.
- **Tier 3 not run**: accepted. Its baseline predates the three cases that matter, and of the cases
  it could have judged only the two barrier cases are old enough — both unchanged in source and
  10 of 10 here. The orchestrator's release runs are where the new baseline comes from.

### 13.3 The lane's finding against §12.5, judged — and the narrowing decision

**Right conclusion.** §12.5 said the record is "redundant and harmless" on Windows. The lane is right
that *harmless* was overstated: the fifth predicate admits the reset spellings a write may have got
after a FIN there, so a peer that half-closes and only then aborts could have a FIN-framed, complete
message reported as a reset — conservative, and not a truncation reported as a success. §12.5 is
corrected in place above.

**Wrong premise — the recurring failure, and this time inherited from a neighbouring comment.** The
lane states the premise as *"there the stack collapses an orderly close into the reset spellings"*,
in the journal, the commit message and the predicate's comment (`NetUtils.h:501-503`). That is the
claim `windows-peer-close-error-codes-record.md` **withdrew on 2026-09-23** (its "Superseded in part"
section, `:12`): the collapse it measured was self-inflicted by `shutdown_both`, and a peer's orderly
close is `eof` on Windows now, as on POSIX. The record lists the same sentence in
`isOrderlyPeerCloseErrorCode( )`'s comment (`:342-353`) as owed and unedited — and the lane took its
premise from that neighbour, which is exactly the trap the brief named. The conclusion survives on a
narrower premise: **Winsock has no state-dependent spelling for a send into a reset connection — nothing
like `EPIPE` from `CLOSE_WAIT` — so a write's `WSAECONNRESET` or `WSAECONNABORTED` there cannot say
whether a FIN preceded it.** Asserted from the API, unmeasured. And the misreport is a race within a
rare shape even there: the read defers, `initiateClose( )` shuts our send side down, and whether the
parked write wakes with `WSAESHUTDOWN` (refused) or with the peer's later RST (admitted) is a race
between our shutdown and the peer's abort.

**Condition at the merge** (a comment, in the same line count — the macros bake `__LINE__` in and
the module's green is against this object): replace `NetUtils.h:501-509` with

    * WHAT IS OWED TO THE MATRIX, AND IT IS A BEHAVIOUR AND NOT ONLY A SPELLING. Winsock has
    * no state-dependent spelling for a send into a reset connection - nothing like EPIPE
    * from CLOSE_WAIT - so a write's WSAECONNRESET or WSAECONNABORTED there cannot say
    * whether a FIN preceded it, and this predicate admits a code a write may have got AFTER
    * one. A peer which half closes and only then aborts would, on that platform alone, have
    * its FIN-framed message reported as a reset rather than completed. Nothing is lost by it
    * on the face this exists for IF a reset there reaches the pending READ as a reset
    * spelling, which isCleanEndOfStreamErrorCode() refuses for itself - unmeasured for a
    * read and a write pending together, and both answers are the matrix's to give.

**Decision on the narrowing: deferred to the Windows matrix.** Not taken now, not rejected. The
lane's one-liner — refuse the record where `os::peerCloseWithUnreadDataIsReportedAsReset( )` — rests
on *"on that platform the READ sees the reset for itself"*, and that is the half of §12.5's sentence
the lane did **not** question and which is equally unmeasured for this shape: a RST landing with a
read and a write both pending. The two candidates fail in opposite directions on unmeasured premises.
The code as landed fails **conservative** — a complete message reported as a reset, in a rare shape,
behind a race. The narrowed code fails as **truncation reported as success** if a pending Windows
read is ever handed an orderly end for a reset — the worst class on this project's list, and the very
symptom this change-set exists to remove. That asymmetry decides it, and the record's own lesson
(`:87`, *"narrowing on a guess is how this record was opened"*) and AGENTS.md's rule for transport
error handling say the same. **What the matrix measures, once:** the lane's three-line probe
(`probe-branches.log` shows its shape) re-applied on the Windows tree for R1, A2's case and R2, and
one more run of R2's peer that closes with `SO_LINGER( on, 0 )` immediately after its `shutdown_send`
— reporting the read's branch, the read's code and the write's code. If the pending read carries a
reset spelling for itself in every reset run, take the narrowing then, as an `os::`-gated arm inside
`NetUtils.h` under the rule; if the read is ever handed `eof` for a reset there, the landed code is
the right one and the FIN-then-RST misreport is its recorded price.

### 13.4 Two findings of this review, both against §12.5's own wording

1. **The fifth predicate admits `eof`, and §12.5 said it did not.** `isPeerClosedErrorCode( )` is
   `isOrderlyPeerCloseErrorCode( ) || connection_reset` (`:384-387`) and the orderly predicate admits
   `eof` first (`:357`). §12.5's *"it admits `connection_reset` and refuses `broken_pipe`"* and the
   predicate's comment (`:496-499`) both omit it, and `onPeerClosed( )`'s *"IT CAN ONLY MAKE AN ENDING
   UNCLEAN"* (`:1247-1248`) is therefore guaranteed by an unstated premise — that no write completes
   `eof` — rather than by the predicate. The premise holds on cleartext: asio maps no send to `eof`.
   Under TLS a write can complete `eof` only through `SSL_ERROR_ZERO_RETURN` after a received
   `close_notify`, an *orderly* end on which substituting `eof` changes no verdict. So: unreachable on
   the face this fixes, benign where it is reachable at all, and a contract stated wider than the
   code enforces. **Recommended at the merge, non-blocking, same line count:** `return
   isPeerClosedErrorCode( ec ) && ! isCleanEndOfStreamErrorCode( ec );` at `:521`, with the four-line
   paragraph at `:496-499` saying that `eof` is the one admitted code that is never proof and is
   refused here. It is the definition of a predicate this change introduces and consumes at one site,
   so it changes no measured path.
2. **§12.5's *"exactly as `Http1Driver_PeerResetsMidCloseDelimitedBodyTests` requires"* overstated
   what that case requires.** It asserts only a non-empty code (`TestHttp1DriverPeerClose.h:321`), and
   R1 matches it (`:914`). The contract `onPeerClosed( )` states — the caller sees the *transport's*
   own code — is pinned by neither, so a regression to shape (i) (`broken_pipe` from the write handler)
   or to a parser code would pass both. **Recommended, non-blocking:** `net::isPeerClosedErrorCode(
   result.errorCode )` in R1, which refuses both regressions and holds on Windows under either
   hypothesis of §13.3. The older case has the same gap and is not this change's.

### 13.5 The behaviour change on A2's case, judged — a correction

`Http1Driver_PeerResetsWhileRequestWriteIsBlockedTests` now ends its stream `connection_reset` where
it ended with the parser's "partial message". The mechanism is verified: the write handler ran first
and recorded the reset; the read's `eof` reaches `onPeerClosed( )` and the consult now takes the
unclean branch before `parseEof( )` could refuse a short Content-Length body. **It is a correction.**
The transport's ending is the cause and the parser's code was its symptom; nothing upstream branches
on the category — `outcomeOnClosed( )` reads only whether the code is set and the connection's state
(`HttpClientRequestTask.h:1185-1196`), `answerOnClosed( )` carries it and chains the connection's
failure (`:1338-1354`) — and retryability is the same expression in both branches. The case's own
comment, *"the assertion on the error code says the same thing on every platform"*, still holds.
Recorded here so the next reader of that case's `partial message` in an old log knows why it changed.

### 13.6 The pre-existing flake — confirmed pre-existing

`ClientSession_SinkIsToldCompleteOnceAcrossTheFallbackRetryTests`, `stats.dispatched == 2U`: red once
on the *base* binary in `hc4-case-ab-*.log` (base run 3 of 30) and never on the fixed. One
observation at roughly 1 in 30 says nothing about the fixed side's 0 of 30, and does not need to: the
base observation is what establishes that the flake exists without this change. Unrecorded until
the lane met it; owed a record of its own, not here.

### 13.7 Comments checked against the code they describe

**Touched by this change, verified:** the three §12 assigned (`isCleanEndOfStream( )`'s,
`onPeerClosed( )`'s, A2's arm paragraph — each now says what the code does); the rule sentence
(`NetUtils.h:334-337`, five predicates); `sock_error( )` for `sk_stream_error( )` in the fourth
predicate's comment and the case header (§12.3); A2's `DECLARED_BODY_LENGTH` comment, which this
change made false and corrected in the same edit. **Born in this change:** the two member comments —
the claim *"an ending the read admits is never the empty code"* holds, since neither
`isPeerClosedErrorCode( )` nor `isExpectedSslErrorCode( )` admits the empty code; the deferral and
delivery comments — every claim in them is the one §13.1 verified; the fifth predicate's comment —
one withdrawn premise (§13.3, condition) and one incomplete clause (§13.4, recommendation).

**Untouched and still true**, each re-read at the commit: `onWriteCompleted( )`'s *"the read loop has
been armed since the task was scheduled"*; `finishStream( )`'s barrier paragraph — *"AND IT DOES NOT
HANG - BUT ONLY BECAUSE initiateClose( ) SHUTS THE SEND SIDE DOWN"* — which now has a **second
client**, the deferral, and says nothing of it; `initiateClose( )`'s composed-write, teardown and
gate paragraphs; `scheduleTask( )`'s *armed FIRST*; `onReadCompleted( )`'s classification comment;
the class comment's *"THE OPERATIONS IN FLIGHT"*; `onTaskStoppedNothrow( )`'s *"A write which failed,
or a cancel, completes the task through the handler macros and never through finishStream( )"* — still
true of what completes the *task*, though the write handler can now reach `finishStream( )` for the
read's deferred ending, so the sentence is narrower than it reads; `m_isWriteInFlight`'s *"which the
storage of a write and the reuse of this connection both depend on"* — now three things depend on it.
None is false; the last three are owed a clause when something else edits those lines. **One
pre-existing overstatement now repeated in the commit message:** A2's case comment *"8 runs each ...
with OR WITHOUT this driver's write arm"* (`TestHttp1DriverWritePeerClose.h:121-124`) — the lane's own
A2 journal records **7 of 8** without the arm (the eighth was the read-first ordering, correct by
accident) and 8 of 8 with it, which is also what §12.5 says. The message is history; the comment is
owed two words.

### 13.8 Found here, not fixed here

- **Ready can be published on a connection whose write the peer reset — A2's shape (ii), not this
  change's, and this change discards the evidence.** A peer that sends a complete keep-alive
  Content-Length response and then closes with our upload unread puts data and a RST on the wire.
  On Linux the queued response is handed over after the reset — R1's `data:8|data:8` behind the
  peer's `reset` record measures exactly that. When the write handler runs first it records
  `connection_reset` and, under (ii), closes nothing; the read then completes the message by parsing,
  `finishStream( )` asks `deriveIsReusable( )` (`:451`), which knows nothing of the write, `isReusable`
  is true (`:1474-1475` — the write is no longer in flight and nothing is closing) and the connection
  is published **Ready** — with the record cleared at `:1521` in the same call. Before A2 the write's
  failure closed the task and the same path published Draining. Self-healing: the next request's
  write meets `EPIPE` with zero octets and its read `eof`, so it fails *retryable* and the pool retries
  — a wasted attempt and a retire, not a wrong answer. No case exists; argued from the kernel and
  R1's measurement. Owed a red and a look, and the record that this change introduces is the obvious
  evidence for the reuse verdict to consult — which is H01's territory, where the lane already reads
  it.
  > **CLOSED 2026-09-24 by `edb6d96`** (owed item 6a). The reuse verdict now asks
  > `! m_writeEndingCode` on the **synchronous** path as well as the deferred one — the record this
  > paragraph predicted would be the evidence is exactly what it consults. *"No case exists"* and
  > *"owed a red"* are both answered: `Http1Driver_PeerResetsAfterACompleteKeepAliveResponseTests`
  > is **15/15 red without the term and 0/15 with it**, against a real peer with a real RST and no
  > seam, with `deferrals=0` in all thirty runs proving the runs exercise the synchronous verdict.
- **The deferral's coverage is silent.** Nothing committed can tell that R2 took the deferral rather
  than the direct path; the measurement lives in a removed probe and its log. A future "simplification"
  that dropped the deferral would keep R1, R2 and the module green and reintroduce v1's 1 in 20 as a
  truncation reported as success. The probe's recipe is in the lane's journal; if the orchestrator
  wants it durable, `notes.txt` is the place for a three-line "how to see which branch a case takes".
- **Two `readRequestHead( )` helpers**, `http1barrier` (`TestHttp1DriverWriteBarrier.h:100`) and
  `http1writeclose` (`:194`), in different namespaces — no ODR question, pre-existing from A2.

### 13.9 Agreement, and what is owed

**Agreed; A1-cleartext is accepted on this review, with one condition at the merge:** the fifth
predicate's Windows paragraph replaced with §13.3's text, same line count. **Recommended and not
required**, each cheap and none changing a measured path: the `eof` refusal in the predicate (§13.4.1);
R1's identity assertion (§13.4.2); the two-word precision in A2's case comment (§13.7). **The Windows
narrowing is deferred to the matrix** with its measurement named (§13.3). Owed and recorded so it is
not rediscovered: the Ready-on-reset shape (§13.8); the silent deferral coverage (§13.8); a record for
the `dispatched` flake (§13.6); the three untouched comments that are now narrower than they read
(§13.7); A1-tls, faces 1 and 2, whose gate goes where `onPeerClosed( )`'s comment now says
(`:1222-1226`) — at the observation and not at the delivery; and this design's status row, corrected
above.

**What this review could not settle by reading:** Windows, in both directions (§13.3 — the landed
code's misreport and the narrowing's premise are each unmeasured there); the TLS spelling of a
write's reset — asio's `engine::map_error_code( )` touches only `eof`, so a transport
`connection_reset` should reach the TLS write handler unchanged and the record should hold under the
TLS policy too, but that is read in asio and not measured, and it is A1-tls's to measure; ~~whether the
Ready-on-reset shape of §13.8 occurs against a real origin, which no case arranges~~ — **settled
2026-09-24: it occurs 15 times out of 15, deterministically, against a real peer** (`edb6d96`); and the loop
counts of the must-not-move runs beyond what the logs hold — here every count was a log, and the
journal was needed for nothing.

---

## 14. Implementation review, 2026-09-23 — A4's second gate

**Reviewer: Claude Fable 5.1, reading `a4-schedule-throw` @ `971342f` in the lane worktree, off
`h01-fix` @ `749943d`. Line numbers are at `971342f` unless marked *red*, which is the unfixed
driver at `749943d`.** Read whole, at the commit: the diff (`git diff 749943d..971342f`, six files);
h1's `armRead( )`, `scheduleRead( )`, `onReadCompleted( )`'s re-arm, `scheduleTask( )`,
`postToStreamExecutor( )`, `initiateClose( )`, `cancelTask( )`, `onTaskStoppedNothrow( )` and the
class comment on the read buffer; `TaskBase.h`'s invariants header, the handler macros,
`scheduleTask( )`'s contract at `:880-890`, `notifyReadyImpl( )` and `scheduleNothrow( )` whole;
the whole of `MultiOperationTask.h`'s accounting; `ExecutionQueueImpl.h`'s
`padExecutingQueueNothrow( )`, `pushInternal( )`, `push_back( )` and `wait( )`; `Algorithms.h`'s
`scheduleAndExecuteInParallelInternal( )`; `OSBoostImports.h`; `BaseDefs.h`'s `BL_ASSERT`;
`Http2ConnectionTask.h`'s base list and the comment at `:2670-2687`; `ClientConnectionTaskBase.h`'s
chain; **all 16 `scheduleTask( )` overrides and the arm functions behind them, with every `catch` in
those files mapped to its enclosing function**; the whole of the new case and of the seam's diff;
`Http1DriverTestUtils.h`'s `ScriptedPeer`, `waitForTaskEnd( )` and `taskFailureText( )`;
`TestMultiOperationTask.h:700-800`; the deferral in full, **as amended on disk during this review**
(its §5.1 and §7 already carry the lane's finding); the lane's journal section; every log in
`logs/lane1-a4/`; and `red-run2.bt` whole — all 39 threads, not the seven frames quoted. Nothing was
built or run; the on-disk object was checksummed against the journal's figure.

**Verdict: agree that the implementation should be accepted, with one condition carried into the
merge** — two comments in the diff state a false premise copied from this design and the deferral
(§14.3; replacement text supplied, one line for one line) — and three non-blocking recommendations
(§14.5, §14.7). **On §5's two premises: both are false as the deferral stated them; the lane's first
is right in its conclusion and needs one precision (§14.4).** The change does not quietly answer the
core question: `TaskBase` is touched nowhere, and what this review's reading of the 16 overrides
says about the deferral's step 1 is recorded for the deferral (§14.6), not decided here.

### 14.1 The diff, verified

**Two call sites, and only two.** `scheduleRead( )` is called from `onReadCompleted( )`'s body at
`:1411` and nowhere else; `armRead( )` from `scheduleRead( )`'s try at `:1001` and from
`scheduleTask( )` at `:2075` — every other mention of either name in the file is a comment. The
in-handler route is the old try/catch around the old body, byte for byte in behaviour: the completing
read is still outstanding until `END_MULTIOP`, the count cannot reach zero, and the phantom is given
back. The schedule route is the old body with the catch removed.

**The widened `try` changes nothing.** `BL_ASSERT` and `beginOperation( )` moved into
`scheduleRead( )`'s try with the body. `BL_ASSERT` is `BOOST_ASSERT` (`BaseDefs.h:63`) and nothing
under the tree — `bld/` excluded — defines `BOOST_ENABLE_ASSERT_HANDLER` or `BOOST_DISABLE_ASSERTS`,
so it aborts and cannot be caught, as the commit message says; `beginOperation( )` is `NOEXCEPT` and
takes a leaf lock.

**The propagating route, followed to the end.** `beginOperation( )` → the initiator throws → the
count is one → `scheduleTask( )` → `TaskBase::scheduleNothrow( )`'s catch (`:1210-1232`) posts
`notifyReadyImpl( false, eptr, false )` to the pool, and the guard at `:1167` is released on the way
out. asio invokes no handler for an initiating function that threw, so nothing is in flight. From the
pool: no finish continuation (`allowFinishContinuations=false` is the base's choice on that route,
unchanged, and nothing had been sent), then h1's `onTaskStoppedNothrow( )` (`:2160`) clears the
state and delivers `onClosed( )` to a sink if a request was pending, then the policy's shuts the
socket. The count stays at one; the three readers of `pendingOperations( )` are in `tasks2`
(`TestMultiOperationTask.h:788`, `:801`, `…Composition.h:253`) and
`MultiOperationTaskT::scheduleNothrow( )` zeroes it at `:408-414` — both as the new comment says.

**The seam arm.** Checked at the top of `HeldWriteStream::async_read_some( )`, before the socket is
touched and before the handler is copied; one shot through `exchange( false )`; initialised false in
the control block's constructor. The backtrace's template argument
(`Http1ConnectionTaskT< utest::http1seam::HeldWritePolicyT< void > >`) and frame 10 are the proof
the driver's `getStream( )` is the wrapper. Ten whole-module runs of twelve cases show no leak of the
flag between cases.

**The patch is the commit.** `logs/lane1-a4/a4-driver-fix.patch` and the committed h1 diff are
identical (a diff of the two diffs, `index` lines excluded, is empty). The h1 header grows by 37 lines
(2391 → 2428). The object on disk is 33,991,792 octets with md5 `bf4b473bc19d4dd5e6bbc6cf1a42b8e8` —
both the journal's "after" figures. **The "before" figure and the byte-identity across the
post-validation comment fix rest on the journal**: nothing was rebuilt here.

**The manifest.** The three seam cases move by exactly the seam's additions (14 + 1 + 16 = 31), the
helper block's sha changes because the block was edited, the new case is at `:220`; tier 1 reported
exactly C1 and C6 before the refresh and PASS after, eol PASS on all three runs — read from the logs.

### 14.2 The backtrace, read whole — what it establishes, and what it does not

**The file.** 39 threads. Thread 1 is the test thread — frames 23–40 are Boost.Test down to
`main( )`. Frames 0–5 are `futex_wait` ← `__lll_lock_wait` ← `lll_mutex_lock_optimized` ←
`___pthread_mutex_lock` ← `std::__1::mutex::lock( )` ← `std::__1::lock_guard`. Frames 6–13 all carry
`this=0xffffd0000dd0`, one task; frames 14–17 all carry `this=0xaaaaab6bed70`, one queue. Every
source line in the chain was re-anchored: `TaskBase.h:564` is `BL_MUTEX_GUARD( m_lock )` and `:1208`
is `scheduleTask( eq )` inside the guard opened at `:1167` (`TaskBase.h` is untouched by A4, so red
and green share it); `MultiOperationTask.h:194` is `notifyReady( )` in `applyDecision( )` and `:384`
is the `applyDecision( )` call; `ExecutionQueueImpl.h:694` is the `scheduleNothrow( )` call, `:778`
the `padExecutingQueueNothrow( )` call, `:800` the `pushInternalNoLock< >( )` call under
`BL_MUTEX_GUARD( m_lock )` at `:798`; and at *red* `Http1ConnectionTask.h:973` is the catch's
`base_type::onOperationCompleted( std::current_exception(), false )` and *red* `:2038` is the
`scheduleRead( );` line of `scheduleTask( )` (both verified with `git show 749943d:`). The test-file
lines (`:153`, `:172`, `:220`, `:225`) match the committed file.

**Measured, and the lane's four claims hold.** (1) One thread, four links: frames 6–12, one `this`.
(2) `close=true, terminal=true` in frame 8: `takeTerminalNoLock( )` (`:147-157`) returns true only
with `m_closing` set and `m_pendingOperations` at zero, so the count reached zero — the one link the
design derived is now observed. (3) Frame 11 is `scheduleTask( )` and there is no `onReadCompleted( )`
frame anywhere in the stack: the throw was on the schedule path, not a re-arm. (4) Frame 16 is inside
`pushInternal( )`'s guard: the queue's lock is held. **Two more the lane did not claim.** Frame 6's
`allowFinishContinuations=true` says the terminal came through `notifyReady( )` (frame 7, `:1018`)
and not through the base's catch, which passes `false` — the route is pinned by an argument value, not
only by the frames. And **the other 38 threads are 37 in `pthread_cond_wait` and one in `epoll_pwait`;
none is anywhere in `TaskBase`, `ExecutionQueueImpl` or the driver** — so the mutex Thread 1 waits on
(`futex_word=0xffffd0000e38`, `expected=2`, locked) is held by no other thread. The self-deadlock is
established by elimination across the whole process, which is the strongest thing in the file.

**Not established by the backtrace.** That the futex at `0xffffd0000e38` *is* `m_lock` comes from
the source line `:564`, not from a symbol; that the queue lock is *held* at frame 16 comes from `:798`
in the source, not from anything gdb printed. Both are readings of one line each and both are solid;
they are still readings. The throw shown is the seam's; a real initiator failure — the allocation —
has still never been observed, as the lane says. And frames 4–5 name the mutex type, which is the
next section.

### 14.3 Right conclusion, false premise — in the record, and copied into the diff

`OSBoostImports.h:95` reads `using std::mutex;`, unconditionally; `:120-121` are the guard typedefs
over it. **`os::mutex` is `std::mutex`, not `boost::mutex`**, and the backtrace agrees (frame 4). Both
are non-recursive, so every conclusion built on the premise stands — this is the failure mode named
in the brief, and it is in five places: the deferral §1.1 (`boost::mutex`), this design's §4 and
§11.1 (corrected inline above, dated), and **in the diff**: `armRead( )`'s doc comment,
`Http1ConnectionTask.h:953` — *"os::mutex is Boost's plain mutex and"* — and the case header,
`TestHttp1DriverScheduleThrow.h:45` — *"os::mutex is Boost's plain mutex and is not recursive"*. The
lane copied the design's premise into the source, where it would outlive everything.

**Condition, at the merge — one line for one line, so nothing else moves:**

- `Http1ConnectionTask.h:953`: `lock that notifyReadyImpl( ) re-acquires. os::mutex is Boost's plain mutex and`
  → `lock that notifyReadyImpl( ) re-acquires. os::mutex is std::mutex, and it` — `:954`'s
  *"is not recursive, so that is a self-deadlock…"* then reads on unchanged.
- `TestHttp1DriverScheduleThrow.h:45`: `re-acquires. os::mutex is Boost's plain mutex and is not recursive, so that is a SELF-DEADLOCK`
  → `re-acquires. os::mutex is std::mutex and is not recursive, so that is a SELF-DEADLOCK`.

Same line count on both: h1's `BL_THROW` / `BL_LOG` sites below keep their `__LINE__`, so the object
stays byte-identical; the case keeps `:220` and the file header is outside `namespace
http1schedulethrow`, so the manifest needs no second refresh. The deferral's `:37` is the
orchestrator's to correct — that file was being edited while this review ran, and it is not touched
here.

### 14.4 §5's two premises, verified at the source — and one precision

**Premise 1 — "the HTTP/2 driver documents that sibling route", read as h2 using it from a
`scheduleTask( )` of its own: false as implied.** `Http2ConnectionTask.h` has no `scheduleTask( )`
override — the whole file mentions `scheduleNothrow` twice, in comments (`:2475`, `:2686`), and
`scheduleTask` never. **The precision the lane's wording needs:** h2's chain is
`ClientConnectionTaskBaseT` = `MultiOperationTaskT< TcpTunnelStageT< TcpConnectionEstablisherConnector > >`
(`ClientConnectionTaskBase.h:332-333`), and neither the tunnel stage nor the connector overrides
`scheduleTask( )`, so h2 **inherits** `TcpConnectionEstablisherBase::scheduleTask( )`
(`TcpBaseTasks.h:874`), which arms `async_resolve( )` with no catch and with no `beginOperation( )`
(`:843-871`). So "h2 never arms anything from `scheduleTask( )`" is not exact: something is armed, by
the establisher, outside the accounting, with no catch — the propagating shape **by construction, not
by contract**. That sharpens the lane's point rather than weakening it: nothing in h2 chose the route.

**Premise 2 — the comment at `:2670-2687` is an observation, not a contract: holds as stated.** Read
whole, the comment argues why the un-posted `cancelTimers( )` at that site is safe, by enumerating who
can reach it and under which lock; its last sentence names the off-strand route only to say it fires
*before the handshake, where all five timers are null*. Guidance for the timer argument, not for an
override.

**So §5's own test resolves to "only implied" — agree, with one distinction kept apart.** The throw-out
route is unstated: `scheduleNothrow( )`'s catch comment (`:1212-1222`) is descriptive, and
`MultiOperationTask.h:62-67` names `BL_TASKS_HANDLER_END_MULTIOP( )` as the *only* caller of
`onOperationCompleted( )` — a rule which does not contemplate the initiator catches (`scheduleRead( )`,
`deferStreamEnd( )`) that call it directly. `padExecutingQueueNothrow( )`'s capitalised warning
(`:649-668`) is real; the codebase does spell out what it means. **But the negative rule is stated,
at the base, for every override:** `scheduleTask( )`'s contract at `TaskBase.h:880-883` says it
*"should never attempt to execute synchronously and call notifyReady( )"* — which is exactly what
h1's catch did, in the accounting's terms. So the documentation defect is precisely the positive half:
a sentence at `:890` (*an initiator that throws should be let out; the catch below completes the task
off every lock*) and one at `MultiOperationTask.h:62-67` naming the direct callers. What this changes
about the deferred question: it is no longer only "is the lock scope wrong" — it is also "which
contract does the base offer a throwing override, and where is it written", and §14.6 bears on the
first.

### 14.5 The three things found and not fixed, judged

**1. `postToStreamExecutor( )` throwing under the lock, after `armRead( )` — real, pre-existing,
and not h1's shape only.** `scheduleTask( )` `:2075-2105`: the read is accounted and pending; the two
posts are not accounted. A throw from `asio::post( )` propagates to the base's catch, which completes
the task with the read in flight. The policy's `onTaskStoppedNothrow( )` shuts the socket, so the read
will complete; `onOperationCompleted( )` then finds the count at zero and `m_closing` freshly true —
`initiateClose( )` runs on a finished task and a second `notifyReady( )` follows. **"Benign, guarded
by `m_notifyCalled`" understates it**: `notifyReadyImpl( )` runs `scheduleTaskFinishContinuation( )`
and `onTaskStoppedNothrow( )` *before* it consults `m_notifyCalled` (`TaskBase.h:566-615`), which is
the failure mode `MultiOperationTask.h:37-42` names as the reason the mix-in exists. For cleartext that
is a second `onTaskStoppedNothrow( )` — idempotent by its own design, the sink already moved out — and
a second socket shutdown; for the TLS instantiation `TcpSslSocketAsyncBase::scheduleTaskFinishContinuation( )`
(`TcpSslBaseTasks.h:471`) would begin a protocol shutdown on a socket already shut unless
`m_wasSocketShutdownForcefully` was set by the first teardown, **which was not traced here**. Untouched
by A4, absent from §4. And the shape is general: `HttpServerReceiveRequestTask::scheduleRead( )` arms
a timer and then the read (`HttpServer.h:158-170`), the `tasks2` probe arms N timers
(`TestMultiOperationTask.h:756-770`). The statement worth carrying: **the base's off-lock route
completes a task without waiting for operations the override armed before the throw.** That is a
property of the route, not of a driver, and it belongs in the deferral's §6 as a third adjacent fact,
because it bounds what *"throw out and the base will complete you correctly"* can ever promise.

**2. A regression hangs the module rather than failing it — adequate, with the facts named, and
one correction owed to the case's own bound.** The repository's own tier-3 runner bounds every module:
`scripts/utests/utf_runlog.py --timeout` (default 1800 s, result `timeout`), and `check_split.sh`
runs through it; the lane's module script bounds at 900 s. The case header and `notes.txt` say the
right thing about the deadlock, and there is no fail-fast for it — the reporting thread is the
deadlocked one, under the queue's lock. **What is not right as written is the case's own bound.**
`TASK_END_TIMEOUT_IN_MILLISECONDS`'s comment (`:88-93`) says it protects against a regression that
*loses* the completion, *"which would otherwise wait for ever below"* — but when `waitForTaskEnd( )`
times out, the lambda goes on to `eq -> wait( driverTask )` at `:187`, which is `waitInternal( )` and
unbounded (`ExecutionQueueImpl.h:1360-1366`), on a task with no pending operation for a cancel to
wake, so it cannot end. The bound buys thirty seconds and then the same hang, before any assertion
runs. **Recommended, not required:** `UTF_REQUIRE( result.ended )` — or the seam file's
`chkOrFail( )` — before the `wait( )`: a printed verdict, then the teardown hang, which is the trade
the header already accepts for the deadlock case; and the comment corrected either way, same line
count.

**3. §6's "the mechanism's in `tasks2`" — the departure is right, the stated reason slightly too
strong.** A regression of *this* fix needs h1's own override, and h1 is instantiated in `httpclient7`,
not `tasks2` — so A4's red lives where the lane put it, and the §6 row is corrected above. But §5.3's
note (`:414-420`) meant the *mechanism's* red, and that mechanism — an inline catch under the task
lock reaching `notifyReady( )` at count zero — is trivially constructible in `tasks2` with a probe
whose `scheduleTask( )` does `beginOperation( ); throw` inside a catch. It would hang `tasks2` the
same way, and it is the natural probe for the deferral's steps 2 and 3, not for A4. Departure
accepted; the `tasks2` mechanism probe stays available to the deferral.

### 14.6 The 16 overrides, classified by reading — for the deferral's step 1, not decided here

The question §4.1 of the deferral asks of each: can it complete the task synchronously, under the
lock `scheduleNothrow( )` holds? For a `MultiOperationTaskT` task that is "drive the count to zero
inline"; for a handler-macro task it is "call `notifyReady( )` inline". Every `catch` in the files
below was mapped to its enclosing function.

| Override | What it does synchronously | Catch on the schedule path | Shape |
|---|---|---|---|
| `Http1ConnectionTask.h:2049` | `armRead( )` accounted; two unaccounted posts | none, now | **was the shape; fixed by A4** |
| `async/AsyncExecutorImpl.h:560` → `scheduleCall( )` `:481` | creates the operation-state task | **yes** (`:518`) — but completes by `scheduleAsyncCall( )`'s *post* of `onExecute( )` (`:372-400`), whose `notifyReady( )` (`:293`) runs on the pool | catches, completes deferred — not the shape; the only other override that catches at all |
| `httpclient/HttpClientRequestTask.h:1826` → `postKind( )` → `post( )` `:374` | one `aioService( ).post( )` inside `BL_NOEXCEPT_BEGIN/END` | none — a throw there **terminates the process** | not the shape; a harsher policy, noted |
| `http/SimpleHttpTask.h:273` | `initRequest( )`, then the establisher's resolve | none | propagates |
| `httpserver/HttpServer.h:237`, `:412` | timer, then read / write | none (`:516`, `:526` are in `getProcessingErrorStatusCode( )`) | propagates |
| `messaging/TcpBlockTransferClient.h:799` | `startCommand( )` → `sendCommandPacket( )` or a `schedule*Data( )` | none (`:2112` is in `notifyCallback( )`) | propagates |
| `messaging/TcpBlockTransferServer.h:1483` | `async_read` | none (`:275-331` are in `checkToPrintExceptionInfo( )`) | propagates |
| `tasks/TaskBase.h:1292` | `BL_RIP_MSG` | — | never scheduled |
| `tasks/TaskBase.h:1348` | one post | none | propagates |
| `tasks/TaskBase.h:1824` | timer, then `cancelTask( )` under the lock by design | none | propagates |
| `tasks/TcpBaseTasks.h:874`, `:1193` | resolve / accept | none | propagates — and `:874` is what h2 inherits |
| `tasks/TcpBaseTasks.h:2213` | two queues of its own, `base_type::scheduleTask( )` | none; calls a *different* queue's `setNotifyCallback( )`, which the invariants header permits | propagates |
| `tasks/TcpSslBaseTasks.h:466` | handshake or shutdown, or `BL_THROW` | none (`:293`, `:332` are in `isProtocolHandshakeRetryableError( )`) | propagates |
| `tasks/utils/ShutdownTask.h:102` | signal wait | none | propagates |

The three overrides under `src/utests` (`TestAsyncCB.h:576`, `TestAsyncV2.h:652`,
`TestMultiOperationTask.h:731` — N accounted timers, no catch) are not the library's.

**Conclusion, by reading:** none shares h1's shape; every other override that arms anything lets a
throw out. That is the deferral's step 1 resolving to *"h1 alone"* — which §4.1 of the deferral says
closes the lock-scope question as *not a library problem* — with the two qualifications this review
adds: the contract is stated only in its negative half (§14.4), and the route has the residual of
§14.5(1). **Recorded for the deferral; not decided here, and not decided by the diff.** A reading is
not a test, and the deferral says so of its own steps.

### 14.7 Comments checked against the code they describe

- **A4's own, verified:** `armRead( )`'s two bullets against `scheduleTask( )` and
  `onReadCompleted( )`; *"no one reads it once the task has completed"* against the three `tasks2`
  readers; *"zeroes the whole accounting"* against `:408-414`; `scheduleTask( )`'s *"ARMED AND NOT
  SCHEDULED"*; the case header's *"inside eq -> push_back( ), on the test thread, holding the execution
  queue's lock"* against frames 15–17 and `:798`; *"six frames below"* — 6 and 12; `notes.txt`'s
  23 ms against `green-run1.log`'s 22,640 µs.
- **The false premise:** §14.3, the condition.
- **Two neighbours now narrower than they read — pre-existing, not A4's, but reached by A4's route.**
  h1 `:187-189`: *"onTaskStoppedNothrow, which runs when the multi-operation accounting has already
  established that nothing is in flight, so the strand is quiescent by then"*, and `:2154-2156`:
  *"It runs when the multi-operation accounting has established that no operation is in flight"*. On
  the propagating route the accounting has established nothing — the count is one — and the strand is
  quiescent because asio runs no handler for a throwing initiator: true conclusion, different reason.
  On §14.5(1)'s route the read *is* in flight and the conclusion is false. Owed corrections, same line
  count; not conditions, because the behaviour A4 adds is correct under both.
- **The case's bound comment:** §14.5(2), owed.

### 14.8 The evidence, read rather than taken

Per log, counted from the `rc=` trailer lines: `green-case-loaded` 15 of 15; `green-module` 3,
`green-module-loaded` 5, `final-module` 2 — ten whole-module runs of twelve cases, all `rc=0`;
`hc3-run` 1 and `hc3-load` 3 — four runs of seven cases, all `rc=0`; `red-run1` `rc=124` with the
attach refused (`ptrace_scope`), `red-run2` the stacks, taken with gdb as the parent. Every figure the
brief quotes is a log. "Under load" is the journal's description — a concurrent module run — and the
loaded logs overlap it in time (22:57); the load itself is not in any log. `tier1-before-refresh`
FAIL with exactly C1 and C6, `tier1-after-refresh` and `tier1-final` PASS; eol PASS on all three.
The object: §14.1.

### 14.9 Agreement, and what is owed

**Agreed; A4 is accepted on this review, with one condition at the merge:** the two one-line comment
replacements of §14.3. **Recommended and not required:** the `UTF_REQUIRE` before the wait and the
bound's comment (§14.5.2); the two h1 neighbour comments (§14.7). **Owed and recorded so it is not
rediscovered:** the residual of the off-lock route (§14.5.1) to the deferral's §6; the classification
of §14.6 to the deferral's §4.1, marked *by reading*; the two sentences that would close the
documentation defect (§14.4) — `TaskBase.h:890` and `MultiOperationTask.h:62-67`; the deferral's own
`:37` and the precision of its §5.1 (h2 inherits the establisher's arm); the `tasks2` mechanism probe
as the deferral's instrument, not A4's.

**What this review could not settle by reading:** the "before" object size and the byte-identity
across the comment fix — the journal's, not rebuilt; whether the concurrent load was the `5c` pattern
or a plain module loop — the journal's, not logged; the TLS second-terminal trace of §14.5(1) —
whether the first teardown sets `m_wasSocketShutdownForcefully` was not followed; and the real-world
reachability of an initiator throw, which is still only the seam's — as the lane says, the allocation
inside a real initiating call is not something a test can arrange from outside.

---

## 15. Implementation review, 2026-09-24 — A1-tls's second gate

**Reviewer: Claude Fable 5.1, reading `a1-tls` @ `db30f53` (and `8086829`, the manifest) in the lane
worktree, off `lazari2` @ `58aa106`. Line numbers in this section are at `db30f53`.** The
specification under review is §12.5, which this reviewer wrote; it is reviewed here as if by someone
else, and its face 1 sentence is corrected in place above. Read whole, at the commit: the diff; h1's
`onStartRequest( )`, `onWriteCompleted( )`, `armRead( )`, `onPeerClosed( )`, `onReadCompleted( )`,
`finishStream( )`, `publishStreamEnd( )`, the H01 continuation, `closeConnection( )`,
`chkArmIdleTimer( )`, `onIdleDeadline( )`, `initiateClose( )`, `scheduleTask( )`, `cancelTask( )`,
`onTaskStoppedNothrow( )`, `submit( )`, `onCancelStream( )` and `postToStreamExecutor( )`;
`TaskBase.h`'s handler macros, `requestCancel( )` and `notifyReadyImpl( )`; the whole of
`MultiOperationTask.h`'s accounting; `NetUtils.h`'s fourth and fifth predicates; the whole of the new
case file, `chkOrFail( )` and the `Http1TlsPeer` helpers it uses, `RecordingSink::waitForClosed( )`
and `scheduleAndExecuteInParallelInternal( )`; the two strand-probe precedents (`Http1DriverProbe`,
the H01 seam); teardown §2.1 and §17; the lane's journal and its four scripts. **Every log in
`logs/lane3-a1tls/` was read**, and the matrix was counted from the fatal-error lines rather than from
the summary. Nothing was built or run. The re-indented block was checked mechanically — the parent's
block indented four columns, `diff`ed against the commit's: identical, the three differing lines being
blank lines the indent had padded.

**Verdict: agree that the implementation should be accepted, with no condition at the merge.** Three
comment precisions are owed, none blocking (§15.7), and one harness hygiene defect on the failure path
only (§15.6). The §12.5 correction the lane left is made in place above and derived here (§15.2). Face
1's gate lands **by the maintainer's decision of 2026-09-24**, recorded in §15.5 with what its control
proves, what it does not, and its trigger — on a premise this review corrects. The strand-holding
instrument is judged acceptable (§15.6).

### 15.1 The diff, verified

**Eight code lines**, as the brief counts them: `if( m_parser ) { BL_TASKS_HANDLER_CHK_CANCEL_IMPL() }`
(`:1389-1392`) and `if( ! base_type::isClosing() ) { … }` (`:1415-1467`) in `onReadCompleted( )`'s
end-of-stream arm, with `closeConnection( )` (`:1476`) outside both; A1-cleartext's deferral block
nested inside the second, re-indented and otherwise untouched (`:1417-1466`). The rest is comment,
checked in §15.7. A `break` from `CHK_EC( )` inside the `if( m_parser )` braces leaves the prolog's
`do { } while( false )` exactly as it does from the `else` branch — braces are not a loop. `notes.txt`
gains two recipes in the file's own form; `Main.cpp` one include; the manifest's `854` and `881`
match the committed file and the final logs.

**Both questions at the observation, verified against the one path that must not be refused.** The
deferred delivery is `onWriteCompleted( ):918-925`, which neither gate touches, and the hand-over is
taken only inside `! isClosing( )` — a read that defers has `isClosing( )` false at its own observation
and sets it itself through `closeConnection( )`. §12.5 step 4 is satisfied structurally, and
`hc7-face3-probe.log` says the same in fact (§15.4).

**What `closeConnection( )` outside both gates does on each path.** On the `CHK_CANCEL_IMPL( )` path it
is not reached — the `break` skips it — and the read's own failure sets `m_closing` through
`onOperationCompleted( ):438`; the state reaches `Closed` at the terminal, the handle stays allocated
so `submit( )` refuses meanwhile, and the sink is moved out once — as on every other failing-macro
path in this driver. On the `isClosing( )`-true path `beginClose( )` sets `m_closingDeliberate` on a
task whose first error is already recorded — verified harmless: `m_firstError` is written once
(`MultiOperationTask.h:425-439`) and the terminal reports it, and the throwaway run measured
`operation_aborted` reaching the sink on this very path. On the deferral and delivery paths it is
A1-cleartext's, unchanged.

### 15.2 The lane's finding against §12.5, judged — right conclusion, one false premise, and the theorem restated

**The conclusion holds:** in this tree `isClosing( )` is never true at the end-of-stream observation
with a live parser unless `isCanceled( )` is true as well, so face 1's gate closes nothing face 2's
check does not. **The premise the case's header states for it is wrong in one of its three
eliminations** — the recurring failure this feature has — and it is recorded here so it is not copied
forward.

The producers of `m_closing` (three writers — `beginClose( ):310`, `onOperationCompleted( ):438`,
`scheduleNothrow( ):480` clears — teardown §14 premise 2, re-verified) reached with a **live parser
and a read still armed**, each at the source:

1. `beginClose( )` has one caller, `closeConnection( )`, which has three: the end-of-stream arm itself
   (`:1476`) — the read is completing and this branch never re-arms (only the `else` does,
   `:1483-1486`), so no later observation exists; `publishStreamEnd( ):1735` — `finishStream( )`
   reset the parser first (`:1642`, unconditional); `onIdleDeadline( ):2054` — guarded on no active
   handle (`:2043`), and the parser's lifetime lies inside the handle's (created at `:641` after
   `submit( )` allocated the handle, reset at `:1642` before `publishStreamEnd( )` releases it). **All
   three eliminated, on the premises the lane gives.**
2. A first error from an accounted handler while a response is in flight and the read armed: the
   read's own handler completes the read on either failing branch; the H01 continuation carries no
   failing macro and runs after the parser is reset; `chkArmIdleTimer( )`'s catch (`:2007`) runs
   under `activeHandle( ) == INVALID` (`:1970`) — **no parser, as the lane says**;
   **`onStartRequest( )`'s initiating catch (`:748-766`) runs with a LIVE parser** — it is created at
   `:641`, in the first `try`, above the write — so the header's *"The last two run with no parser"*
   (`TestHttp1DriverTlsCancelClose.h:56-58`) is false of this one. The elimination survives on a
   different premise: that catch clears `m_isWriteInFlight` (`:764`) before completing the operation,
   so `initiateClose( )` shuts nothing down there (`:2103`) and can provoke no ending, and not one
   request octet has left, so no response can be in the parser. It cannot produce the symptom — but
   not because there is no parser.
3. `onWriteCompleted( )` — and it has **two** failing macros, not one: `CHK_EC( ec )` behind the two
   arms (`:927-930`) and the unconditional `CHK_CANCEL_IMPL( )` (`:932`). The commit message and the
   header name only the first; run 3 of 5 in `red-probe-both.log` took the second — the write woke
   `broken_pipe` from `cancelTask( )`'s `shutdown_send`, `isPeerClosedOnWrite` excused it, and the
   cancel check failed the task. Both are behind `isCanceled( )` on POSIX: the arms excuse every
   ending code (`isPeerClosedOnWriteErrorCode( )` admits `broken_pipe`, `isStreamTruncationError( )`
   the TLS spelling, `isOurOwnTeardown` anything while closing), which leaves `operation_aborted`
   from a cancel's reaping. On Windows `WSAESHUTDOWN` reaches `CHK_EC( )` directly — the fourth
   predicate refuses it by design (`NetUtils.h:452`) — but only after `cancelTask( )`'s shutdown, so
   still behind the cancel; unmeasured.

**The theorem, on the right premises:** *an ending we provoked, observed with a live parser and
`isClosing( )` true, implies `isCanceled( )`* — because our send side is shut only by `cancelTask( )`
or by `initiateClose( )` with a write in flight, and the latter runs with a write in flight only from
a handler that is not the write's, which by 1 and 2 is never one that leaves the read armed with a
response in flight. **Its scope is endings we caused.** A write that fails with a code the arms do not
excuse and no cancel produced — `timed_out` after the retransmit budget, a TLS engine error — sets
`m_closing` with the parser live; `initiateClose( )` then issues only a `cancel( )`
(`m_isWriteInFlight` is already false, `:868`), and a read inside §3.1's window at that instant
re-arms past it and later observes whatever the **peer** does. There face 1's gate acts and the cancel
check does not: it refuses a message the peer's own close framed, on a task that has already failed,
and the caller sees the write's failure instead of a completed 200 to a request the origin never
fully received. Defensible, not the worst class, and not arrangeable from a test — it needs a
partition and a partial record in the same window. Recorded so that "no red of its own" is read as
*in what a fixture can arrange*, which is true, and not as *unreachable*, which is not quite.

**§3.2 and §12.5 are corrected in place above.** §3.2 point 2's list was right as a list; its
conclusion — that the gate changes behaviour at those three sites — is stale for the reasons in 2 and
3. §12.5's *"Unchanged"* was right of the gate's shape and placement and wrong of its coverage.

### 15.3 Face 2's shape — the two rejections verified, and what the narrowing preserves

**Rejection 1 — `! isClosing( ) && ! isCanceled( )` with a swallow — holds at the source.** With no
failing macro the read completes clean, `closeConnection( )` closes deliberately, nothing else is
pending on the GET, `onOperationCompleted( )` records no first error and the terminal is clean;
`onTaskStoppedNothrow( ):2240` then hands the sink `connection_aborted` — its default for a task with
no exception — for a request the caller cancelled, and `isFailed( )` is false. A misleading code in
place of a truncation, exactly as the lane says. On the POST the write's own `operation_aborted` is
already the first error, so the shape is wrong only on face 2 — the face it was proposed for.

**Rejection 2 — `CHK_CANCEL_IMPL( )` moved wholesale above the arm — holds, with one precision on what
"as it always has" means.** The wholesale move differs from the narrowed check on exactly one path: an
ending observed with **no parser** and `isCanceled( )` true — a cancelled idle connection whose
composed read slipped the cancel and then saw the peer answer our FIN. Today that path swallows,
closes deliberately and completes the task **clean**; the same connection whose read the cancel
*reaped* — the ordinary case, and the only case on cleartext — completes `operation_aborted` through
the `else` branch's `CHK_EC( )`. So the narrowing preserves a small inconsistency rather than a
uniform rule. It is harmless — no sink exists to be told either way, and nothing in the pool reads a
cancelled idle task's verdict — and the narrowing is the smaller change, which is the right default.
But the comment at `:1384-1386`, *"a cancelled idle connection whose read is reaped goes on ending as
it always has"*, describes the wrong path: a reaped read completes `operation_aborted`, is not an end
of stream, and never enters this branch at all. Owed a precision, same line count (§15.7).

**The narrowing itself is right for the symptom:** a live parser is a message that has not completed
(`finishStream( )` resets it on every completion), so *"with no message in flight there is nothing to
frame"* is exactly true, and the check sits ahead of both the deferral and the delivery, so a
cancelled read can neither hand an ending to the write handler nor frame one.

### 15.4 The evidence, read rather than taken

- **The matrix**, counted from the fatal-error lines of `matrix-{pristine,face1only,face2only,both}.log`:
  pristine 5/5 red on both cases; face1only 5/5 red on face 2 and 0/5 on face 1; face2only and both
  0/5 on both. Every red is the symptom and not a run count: *status 200, body 'part-one', sink
  headers 200|data 8|closed cleanly, peer …|partial:sent|peer-end:asio.ssl.stream:1 (stream
  truncated)|closed*. The summary's table is what the logs say.
- **The mechanism, in the trace** (`red-probe-both.log`, temporary `fprintf`, 5 runs): face 2 observes
  `eos=1 closing=0 canceled=1 parser=1 wif=0` every run — `m_closing` false throughout, as the case's
  comment claims. Face 1: `write ec=system:125 teardown=0 … closing=0 canceled=1 parser=1`, then
  `initiateClose wif=0 parser=1`, then `read … eos=1 closing=1 canceled=1 parser=1 wif=0` — the write
  reaped by the cancel, its `operation_aborted` the first error, `initiateClose( )` issuing only a
  cancel that finds the completed-but-unqueued read, and the read observing with `isClosing( )` true:
  §3.1's window exactly. Run 3 is the `broken_pipe` route of §15.2 point 3. **`red-face1-01.log` is
  not this**: it shows the read observing first with `closing=0 canceled=1 wif=1` and deferring — the
  journal's recorded trap, the peer draining early so the write woke excused; the case was then gated
  on the cancel having run (`:576`, with the measurement in its comment), and `red-face1-02.log`
  shows the committed mechanism. The case text grew 61 lines after the probe runs (the reds there
  report `:793` and `:820`; the committed cases are `:854` and `:881`), and `final-pristine.log`
  re-established both reds 5/5 on the committed text, between two 0/5 runs of the committed driver
  whose objects are byte-identical (`final-summary.log`).
- **Face 3's deferral** (`hc7-face3-probe.log`, 3 runs of R1, R2, the older reset case and A2's case,
  0 red): `read DEFERS` ×3 and `write DELIVERS deferred` ×3 — R2 takes the hand-over every run and
  the write handler delivers it — and `read DIRECT` ×9 for the other three. The delivering write
  carried `operation_aborted` in two runs and `broken_pipe` in one: §12.5 step 3 predicts
  `broken_pipe` for a parked write, and the `cancel( )` that follows the shutdown inside
  `shutdownSocket( )` evidently reaps it first about two times in three. Both are excused, the verdict
  was the read's `eof` each time, and the precision is worth the sentence §12.5 does not have.
- **What the caller sees** (`throwaway.log`, a temporary assertion, reverted; `restore-verify.log`
  2/2 green on the restored object, 49,713,864 octets, the validated size): `system:125` —
  `operation_aborted` — on both faces, *closed with an error*.
- **Modules:** `green-hc5-module.log` and `final-hc5-module.log` 3/3, `green-hc7-module.log` 3/3,
  `green-hc3-run{1,2,3}.log` 3/3, all `No errors detected`. §13.2's `httpclient3` reuse-refusal flake
  did not appear in three runs, which says nothing about it either way.
- **The object:** 49,713,864 is in the logs (`stat`, three times) and on disk; the before-figure
  49,308,288 is the journal's and was not rebuilt here. Against it the cases cost ~400 KB (the
  pristine driver with the cases: 49,708,896) and the eight code lines ~2.6 KB. **+0.8% on a module
  already over target with a recorded reason is accepted:** a sibling would pay the TU floor and a
  second TLS peer for +0.4 MB of content, and the cases name only types the module already
  instantiates.

### 15.5 Face 1's gate — decided, and the record made honest

**The decision (maintainer, 2026-09-24): the gate stays, as defence-in-depth, with the matrix's row 2
as its control.** Recorded here so the next reader does not reopen it, with exactly what was measured.

**What the gate has, stated precisely.** It is not a gate without a red. Against the tree with
**neither** gate the face-1 case is red 5/5, and with the `! isClosing( )` gate **alone** it is green
5/5 while the face-2 case stays red 5/5 (rows 1 → 2). That is a red-before, green-after for this gate
on the path it names — `isClosing( )` true at the observation, the trace of §15.4 — and it is also
its **specificity** control: it acts on that path and does not act on face 2's. **What it lacks is
independence**: with the cancel check in place the gate has no case that changes colour (rows 3 = 4),
because §15.2's theorem says today's only producer of that path is behind `isCanceled( )`. Row 2
therefore proves what the lane says it proves — that the gate acts on exactly the path it names —
and **does not prove necessity**, which in this tree nothing can. The record must say *redundant and
measured*, not *unmeasured*; the lane's "no red of its own" means the first.

> **SUPERSEDED 2026-09-24 by `5941a50`, and the "nothing can" above was wrong.** The claim held of
> what an **external cancel** can arrange; it did not hold of what a **fixture** can arrange, because
> the landed probe can call the epilog's own `closeConnection( )` + `initiateClose( )` pair directly
> instead of `requestCancel( )`. `Http1DriverTls_ClosingWithoutACancelCompletesACutShortBodyTests`
> does exactly that, and the matrix now has a fourth column:
>
> | variant | face 2 case | face 1 case | **face 1 alone** |
> |---|---|---|---|
> | neither gate | RED 5/5 | RED 5/5 | RED 5/5 |
> | face 1's gate only | RED 5/5 | GREEN 5/5 | GREEN 5/5 |
> | **face 2's gate only** | GREEN 5/5 | GREEN 5/5 | **RED 5/5** |
> | both | GREEN 5/5 | GREEN 5/5 | GREEN 5/5 |
>
> **Row 3 is the independence this section says nothing can provide.** Its failure text carries the
> same symptom the siblings pin — *status 200, body 'part-one', closed cleanly* — but with **`task
> clean`** rather than `Operation canceled [system:125]`, which is what shows no cancel was anywhere
> near it. So `! isClosing( )` is no longer redundancy anyone must remember to re-measure: **remove
> it and a case fails.** The maintainer's decision to keep the gate stands, and the note that went
> with it — *defence-in-depth, row 2 as its control, re-measure when the triggers fire* — is retired
> rather than carried.
>
> §15.9's owed item and `db30f53`'s commit message both predate this and are left as written.

**The accepted reasoning rests on one premise this review corrects.** *"§13.3 already defers narrowing
that arm to the Windows matrix"* — it does not. §13.3's deferred narrowing is of the **fifth**
predicate, `isPeerResetOnWriteErrorCode( )` (*"refuse the record where
`os::peerCloseWithUnreadDataIsReportedAsReset( )`"*), which is the consult inside `onPeerClosed( )`;
taking it changes what a recorded write code proves and changes **nothing** about which codes
`onWriteCompleted( )`'s arms excuse, so it cannot make `m_closing` reachable with a live parser. The
conclusion — keep the gate against a future producer — stands on its own, and the producers that
would matter are these. **The trigger, recorded:** the gate is to be **re-measured for a red of its
own** — the face-1 arrangement with the cancel replaced by the new producer — whenever any of the
following lands: (i) a change to `onWriteCompleted( )`'s two excusing arms (`isOurOwnTeardown`,
`isPeerClosedOnWriteErrorCode( ) || isStreamTruncationError( )`) that lets a code no cancel produced
reach `CHK_EC( )`; (ii) a new caller of `closeConnection( )` or `beginClose( )` while a response is in
flight — a drain, a per-request deadline, a `Connection: close` acted on early; (iii) a new accounted
operation whose failure can be the task's first error while a response is in flight. At (ii) with a
write in flight the gate would acquire its red with certainty — `initiateClose( )` shuts the send side
and the peer's answer is observed closing and not cancelled; at (i) and (iii), and at (ii) without a
write, whenever the producer shuts our send side or the peer closes while we are closing.

**The red can be given now, and this review recommends it (not a condition).** The probe class as
landed can call the driver's protected `closeConnection( )` and `initiateClose( )` from the first hold
in place of the test thread's `requestCancel( )` — the same two actions a first-error epilog performs,
on the strand, with no task lock — and the peer's script needs no change: our FIN goes out, the peer
reads to the end and closes, and the read observes with `isClosing( )` true and `isCanceled( )` false.
By the traces of §15.4 that case is red under `face2only` and green under `both`: a third case that
fails if the gate is ever dropped, which converts "remember to re-measure at the trigger" into
something the suite remembers. `initiateClose( )` would then run twice in that run — the real epilog's
call finds `m_isWriteInFlight` false and cancels an empty table — which is harmless and is the one
contract of the base it bends, in a test.

**On `src/utests/AGENTS.md`'s rule.** The rule as written — *show the case failing against the unfixed
code* — is met by rows 1 → 2. What it does not address is a guard that is **redundant with another**
in the tree it lands in, and the precedent is worth naming rather than leaving implicit. Proposed
wording, for the maintainer to apply (this review touches nothing under `src/`), as a sentence after
*"shown red before and green after"*:

> A guard that another guard already covers may land only if it has its own red against the tree
> with **neither** (so its effect is measured), the redundancy is itself measured (the other guard
> alone turns its case green), and the record names what would make it independent — with the
> re-measurement owed at that point, or a case that manufactures the producer now.

### 15.6 The instrument, judged — acceptable, with precedent, and one hygiene defect

**A test may reach onto a driver's strand this way.** The question the case asks — what the read
handler does when an ending is observed *after* a cancel that found nothing registered — lives in a
window between a reactor completion and a strand handler, and the only lever that opens it is
holding the strand. Two precedents already do so in this tree: `Http1DriverProbe`
(`Http1DriverTestUtils.h:820`), *"the driver under test with a door onto its OWN strand"*, posts from
a subclass exactly as `holdStrand( ):320` does; and H01's seam (`TestHttp1DriverStrandSeam.h`) holds a
write's completion through a test-only stream policy. The probe here is narrower than either: one
public method, no state, no override, no behaviour — the driver under test is the driver.

**Why a sink cannot do it, verified.** `BL_TASKS_HANDLER_BEGIN( )` is `BL_MUTEX_GUARD(
TaskBase::m_lock )` (`TaskBase.h:119-120`); every sink call is inside a handler body —
`deliverBodyChunk( )` from `onBytesRead( )` inside `onReadCompleted( )`, `onClosed( )` from
`finishStream( )` inside whichever handler ended the stream; `requestCancel( )` takes `m_lock`
(`:1237-1243`); and `scheduleRead( )` runs in the same handler (`:1485`) before the lock is released.
A cancel issued from a sink blocks until the read has re-armed, which is the other side of the window.

**The five octets are the right key.** A bare record header leaves the engine wanting input
(`io.hpp:156`), which is the one outcome that re-arms without completing; a full record completes the
read with data, which the `else` branch's `CHK_CANCEL_IMPL( )` (`:1481`) kills correctly — the tree
behaving. The cases assert the peer's own ending was **truncated** (`Http1TlsPeer::isTruncated( )`,
`TestClientSessionTlsHttp1.h:473`), which pins that the ending was our forceful shutdown and never a
close the client chose.

**It fails safe.** The two settles (`:715`, `:740`) bound one reactor hop each; too short, and the read
is still registered when the cancel runs, or the ending has not been observed yet — green against the
unfixed tree in both, never a red. The instrument, the number and the reason are A2's
`WRITE_PARKS_IN_MILLISECONDS`. The weakness is the one §13.2 recorded for R2: a slow host weakens the
*control* silently and never the case.

**One hygiene defect, on the harness-failure path only.** The four hold latches are declared inside
the `scheduleAndExecuteInParallel( )` lambda (`:666-669`) and captured by reference by handlers that
block on them. If a `chkOrFail( )` inside that lambda fires while a hold is blocked — the peer never
closing, say — `UTF_FAIL` throws, the lambda unwinds and destroys the latches, and
`scheduleAndExecuteInParallelInternal( )`'s catch then `forceFlushNoThrow( )`s a queue whose driver
task cannot end until the hold's 30 s wait times out and returns through a destroyed mutex. Undefined
behaviour in place of a diagnosis: a red run the case was built to diagnose could become a crash. None
of the recorded runs took that path — every red fired in `chkNotReportedComplete( )`, after the
lambda had returned. **Recommended, not a condition:** hoist the four latches to function scope beside
the peer's three (`:509-511`), which live through the catch. On the green path the lifetimes are
sound: every latch's last use is inside a hold that returns before the strand can run the handlers
the task's terminal needs, and `eq -> wait( driverTask )` (`:747`) returns only after that terminal.

### 15.7 Comments checked against the code they describe

**Born in this change, in the driver.** Face 2's paragraph (`:1374-1386`): *"m_closing has three
writers and cancelTask( ) is none of them"* — verified; *"The else branch asks the same question with
the same macro, and an ending never reaches it"* — verified; *"a cancelled idle connection whose read
is reaped goes on ending as it always has"* — the wrong path (§15.3), owed a precision in the same
line count: it is the connection whose read *slipped* the cancel that goes on ending as before, and
the reaped one never enters this branch. Face 1's paragraph (`:1395-1413`): *"the read then re-arms
and observes the ending our own shutdown_send provoked"* — true, with the precision that in the
measured red the shutdown is `cancelTask( )`'s and `initiateClose( )` issued none (`wif=0` in the
trace); *"ASKED WHERE THE ENDING IS OBSERVED AND NEVER AT THE DELIVERY … this read set it"* —
verified; *"Routing the ending to CHK_EC( ) instead would make eof the first error of a task closing
DELIBERATELY"* — verified against `onOperationCompleted( )`. *"OUTSIDE BOTH GATES"* (`:1470-1475`) —
verified in §15.1, including *"all but a no-op"* on a task already closing.

**Born in this change, in the case.** The header's *"The last two run with no parser"* (`:56-58`) —
**false of `onStartRequest( )`'s initiating catch** (§15.2); the conclusion it serves survives on the
premise given there, and the sentence is owed the correction, same line count. *"the write handler's
CHK_EC( )"* (`:58`, and the face-1 case comment) — its `CHK_CANCEL_IMPL( )` too, which run 3 took; a
precision. The peer's *"AND IT DOES NOT READ ONE OCTET UNTIL THE CANCEL HAS RUN"* paragraph
(`:565-576`) — verified against `red-face1-01.log`, which is the measurement it cites. The header's
*"a sink cannot be used for this"* — verified (§15.6). The face-2 case's *"m_closing IS FALSE
THROUGHOUT"* — verified in the trace. The bounds paragraph — verified in direction (§15.6).

**Neighbours, re-read at the commit.** `onPeerClosed( )`'s *"A gate on this driver's own teardown
belongs where the ending is OBSERVED and not here"* (`:1261-1264`) — now true of the code, not only
of the intent. `onWriteCompleted( )`'s *"AND CHK_CANCEL_IMPL( ) STAYS OUTSIDE THE GUARD"* — unchanged
and true. `onTaskStoppedNothrow( )`'s *"A write which failed, or a cancel, completes the task through
the handler macros and never through finishStream( )"* — still true, and the read's own cancel check
is now a third route to it; narrower than it reads, not false. `initiateClose( )`'s *"shutting the
RECEIVE side down makes OUR OWN socket report eof … parseEof( ) would complete a half-received
close-delimited body on"* — untouched and still the reason `shutdownSocket( )` is `shutdown_send`;
the lane's journal records having first read `force` as `shutdown_both` and correcting itself at the
source, which is the right order.

### 15.8 Found here, not fixed here

- **Windows, two things unmeasured, both behind a cancel:** `WSAESHUTDOWN` reaching `CHK_EC( )` from
  `cancelTask( )`'s shutdown (§15.2 point 3), and whether the composed TLS read's window has the same
  shape under IOCP, where a completed-but-unqueued read is the ordinary state rather than a race. The
  cases are POSIX-measured only; the matrix should run them.
- **The TLS spelling of a write's reset (§12.5, §13.9) is still owed.** This change measured the TLS
  spelling of a write's *cancel* — `operation_aborted` passed through unchanged, and `broken_pipe`
  likewise — not of a reset.
- **The idle-connection inconsistency** of §15.3: a cancelled idle TLS connection whose read slipped
  the cancel completes clean; one whose read was reaped completes `operation_aborted`. Harmless today;
  whoever next touches the arm should decide it deliberately rather than inherit it.
- **The hold-latch lifetime** (§15.6) and **the three comment precisions** (§15.7) — owed, none
  blocking.
- **A face-1 case without a cancel** (§15.5) — recommended, with its shape given.
- **The AGENTS.md sentence** (§15.5) — proposed, not applied.

### 15.9 Agreement, and what is owed

**Agreed; A1-tls is accepted on this review, with no condition at the merge.** Face 1's gate lands by
the maintainer's decision, and the record of it is §15.5: measured and redundant, not unmeasured; its
control proves specificity and not necessity; its trigger is any new producer of `isClosing( )` with a
response in flight — and not §13.3's narrowing, which cannot produce one. **Recommended and not
required:** the face-1 case without a cancel; hoisting the hold latches; the three comment precisions,
each in its line count. **Owed and recorded so it is not rediscovered:** the Windows measurements of
§15.8; the write-reset TLS spelling; the idle inconsistency; the AGENTS.md sentence; this design's
status row, corrected above.

**What this review could not settle by reading:** the before-object size (the journal's, not
rebuilt); Windows in every direction named; whether the `timed_out` route of §15.2 has ever occurred
against a real origin — argued from the kernel's retransmit behaviour and asio's window, arranged by
nothing; and the loop counts of the module runs beyond what the logs hold — here every count was a
log.

---

## 16. Implementation review, 2026-09-24 — 13a and 13e's second gate

**Reviewer: Claude Fable 5.1, reading `h2-write-peer-close` @ `bc1f13a` in the lane worktree, off
`lazari2` @ `865f923`. Line numbers in this section are at `bc1f13a`; Boost's are in the 1.90.0
source under the devenv dist.** The specification under review is the lane's reading pass
(`logs/lane1-h2shape/findings.md`, outside the repo), the owed-work record's 13a and 13e rows, and
§2.2 above as corrected on 2026-09-24. Read whole, at the commit: the diff (four files); h2's
`isPeerClosed( )`, `isPeerClosedOnWrite( )`, `onRead( )`, `onPeerClosed( )`, `onWriteScheduled( )`,
`pumpWrites( )`, `onWrite( )`, `scheduleRead( )`, `closeStream( )`,
`closeAllStreamsUnwrittenRetryable( )`, `sinkOf( )`, `onHeadersEvent( )`, `publishState( )` and the
tail of `onProtocolNegotiated( )`; `MultiOperationTask::beginClose( )`; `NetUtils.h`'s five
predicates and their comments; the whole of the new case file; `TcpStrandedStreams.h`'s policy
comment and `OSBoostImports.h`'s `strand_t`; `ThreadPoolImpl.h`'s `io_service` construction;
`utf_baselib_h2client4`'s `H2Pool_ADriverIsAdoptedOnlyOnceTheTaskPublishesClosedTests` and the stubs
it is built from; and in Boost's own source the whole of `epoll_reactor.ipp`,
`strand_executor_service.ipp` with its `.hpp` implementation, `handler_work.hpp`, `strand.hpp`'s
`execute( )`, `reactive_socket_send_op::do_complete( )`, `scheduler::do_run_one( )`,
`post_deferred_completions( )`, `post_immediate_completion( )`, `can_dispatch( )`,
`wake_one_thread_and_unlock( )`, the scheduler's constructor and
`io_context::basic_executor_type::execute( )`. Of `logs/lane1-h2write/`, the red, the control and the
final green were read whole; the build and must-not-move logs were read for their verdict lines and
case counts. **Nothing was built or run, and no probe was compiled:** the reactor ordering below is
settled by reading and is marked so wherever it is used.

**Verdict: agree that the implementation should be accepted, with one comment condition at the
merge, in its line count (§16.6).** The change is the right shape for the right reasons, both
defects are measured rather than argued — the (A) control row *is* 13e — and the reactor finding the
lane reports **holds**: it makes both defects the ordinary case rather than two windows' worth,
which strengthens the change and weakens two records. §2.2's point 2 is corrected in place above; the
owed-work record's 13e row is owed the same correction when the item is closed (§16.7). **13a and
13e can be marked closed.**

### 16.1 The diff, verified

**Six code lines change behaviour**, all in `onWrite( )`: the middle arm asks
`isPeerClosedOnWrite( ec )` (`:1870`) and its body is a comment. The helper (`:1618-1622`) is
`net::isPeerClosedOnWriteErrorCode( ec ) || base_type::isStreamTruncationError( ec )` — the fourth
predicate beside the truncation question exactly as `onRead( )`'s pair asks it — and the fifth is
refused for the reason NetUtils gives: it is `isPeerClosedErrorCode( ) && !
isCleanEndOfStreamErrorCode( )`, so it refuses `broken_pipe`, the code this defect is about.
`isPeerClosed( )` (`:1571`) is untouched and has exactly one caller left, `onRead( )` at `:1633`.
**The arm order is the one §2.2 requires and §2.4 warns about:** `isClosing( )` first, the peer-close
arm second, `CHK_EC( )` third — the barrier case's write ends `broken_pipe` from our own
`shutdown_send`, both questions answer yes there, and only the order keeps that case on the first
arm. Green in every log. `m_isWriteInFlight = false` still precedes the arms; the success branch, its
`CHK_CANCEL_IMPL( )` and the epilog are untouched.

**The case file, read whole.** The peer is four hand-built frames over a raw socket and never
`Http2TestServerT`, for the reason the barrier case's own peer gives. Its ending is
`shutdown( shutdown_send )` — the FIN that puts the driver's socket into CLOSE_WAIT — then
`SO_LINGER( true, 0 )` and `close( )`, which resets from FIN_WAIT unconditionally. The rendezvous is a
non-consuming `poll( )` on the driver's own descriptor from inside `onWriteScheduled( )`:
`available( )` (FIONREAD) with `poll( POLLIN )` between checks until the answer is queued, then
`poll( )` with an **empty** events mask for `POLLERR` against a **`steady_clock` deadline**. Both
halves are right: `POLLERR` is reported whether or not it was asked for, a FIN alone makes the socket
readable so a `POLLIN` mask returns at once on every call — the 4 ms the lane measured a
slice-counting helper burning — and the two reds' `system:32` *after* `sawPollError( )` is the proof
the `poll( )` took neither the error nor the bytes. The seam records and never asserts, and each case
asserts its own preconditions — `peer.failure( )`, `wasArmed( )`, `seamFailure( )`,
`sawPollError( )`, and for R2 `hasSpoken( )` — before the assertion it exists for. R1 is the preface
at `onProtocolNegotiated( )`'s window; R2 is the SETTINGS acknowledgement at `onRead( )`'s, with the
sink deliberately not given the connection so `consumed( )` adds no second pump. `notes.txt` gains
the two recipes; `Main.cpp` one include; the whole file body sits inside `#if ! defined( _WIN32 )`
(§16.5).

### 16.2 The evidence, read rather than taken

| log | what it shows |
|---|---|
| `red-unfixed.log` | at `865f923`, one run: R1 and R2 each `fatal error ... Broken pipe [system:32 at ... reactive_socket_send_op.hpp:136 ... do_complete]`; the barrier case green; `2 failures`, rc 201 |
| `control-A-predicate-only.log` | the predicate swapped, the write still calling `onPeerClosed( )`, one run: R1 **green**; R2 `the answered response did not reach the sink - status 0, recorded: closed with an error`; `1 failure` |
| `green-ii-final2.log` | the committed shape, three runs: all three cases green each time, `No errors detected` |
| `mustnotmove-h2client{2,3,4,7}.log` | 14, 3, 20 and 1 cases, each module `No errors detected`; h2client2's `DEBUG ... did not acknowledge our SETTINGS frame in time` is a line logged inside a green case, not a failure |

**The control row is 13e measured.** A server answered 200 with a body, a driver differing from the
committed one by six lines told the caller *closed with an error* and no status, and the committed
driver hands the answer over. That is the discriminator §2.3 asked for, shown against the shape
rather than argued. `shape-ii.patch` in the log directory is identical to the committed diff in its
code lines and differs in one comment paragraph: the patch still carried the record's ordering claim
(*"posts the READ's handler first ... the read has already closed the task"*), and the commit replaced
it with *"WHICHEVER of the two handlers runs first"* after the lane checked the reactor — the check
happened before the commit, not after.

**One precision on what the reds prove.** The rendezvous makes the *kernel's* state certain — RST
applied, answer queued, no read armed — and that removes the scheduler-versus-kernel race the
findings warned of. It does not fix the order in which the two *posted* completions reach the
strand: both are pushed to the scheduler queue by `post_immediate_completion( )` while the issuing
turn still holds the strand, and a woken pool thread taking the read op can reach the strand's mutex
before the thread taking the write op does. In that order the unfixed tree's R1 is **green** — the
read's `eof` closes the task first and the write finds `isClosing( )` — and the (A) control's R2 is
green, the answer being fed before `onPeerClosed( )` empties the table. So the two reds and the
control are overwhelmingly likely and not deterministic, and each was shown once. **The committed
cases are not so exposed:** under the write declining, both orders end green by construction, which
is what a case pinned to a shape needs. The lane's header sentence *"this makes the failure certain"*
overstates by exactly that much (§16.6).

Not in the logs, taken from the lane: the module size (33.1 → 33.4 MB; no size line in any log
here), the two `C1 ADDED` and the eol pass.

### 16.3 The reactor ordering — the lane's finding holds, by reading; the measurement is owed

The findings' §0, and §2.2's point 2 as first corrected, said: `perform_io( )` performs the write's
syscall first but posts the read's handler first, so with both ops registered the read handler runs
first, closes, and the write arm cannot fire; the arm fires only in the two no-read-armed windows.
**Traced at the source, that is not this reactor, and the lane is right.**

1. `epoll_reactor::run( )` performs no socket I/O. For each ready descriptor it does
   `descriptor_data->set_ready_events( )` and `ops.push( descriptor_data )` — the **`descriptor_state`
   itself** is the queued operation — and `task_cleanup` moves it onto the scheduler queue.
2. `perform_io( )` is reached only from `descriptor_state::do_complete( )`, when the scheduler pops
   that state. It loops `j = max_ops - 1 .. 0`, so the **write's `send( )` runs before the read's
   `recv( )`** — §12.3's syscall order, confirmed — and collects the completed ops as
   `[ write, read ]`. It returns the **front** (the write) and its `perform_io_cleanup_on_block_exit`
   destructor **posts the rest** (the read) with `post_deferred_completions( )`: to the scheduler
   queue under lock with a `wake_one_thread_and_unlock( )`, because `one_thread_` is
   `concurrency_hint == 1` (`scheduler.ipp:115`) and `ThreadPoolImpl` builds its `io_service` with a
   hint of 0 or the default.
3. `do_complete( )` then calls `op->complete( )` on the write op **in place**:
   `reactive_socket_send_op::do_complete( )` → `handler_work::complete( )`. The socket is built on
   `make_strand( )` (`TcpStrandedStreams.h`; `OSBoostImports.h:78` makes `strand_t` a
   `strand< io_context::executor_type >`), and the handler is a plain `cpp::bind( )` with no
   associated executor, so the `IoExecutor` is the strand, `handler_work_base< strand >` is the
   primary template whose `owns_work( )` is `true`, and `complete( )` goes to `dispatch( )` →
   `strand::execute( )` (`strand.hpp:267-271`).
4. `strand_executor_service::do_execute( )`: the reactor thread is not `running_in_this_thread( )`
   the strand, so it **enqueues** the write handler — `locked_ = true`, `ready_queue_ = [ write ]` —
   and, being first, calls `ex.execute( invoker )` on the inner `io_context` executor. That
   executor's `execute( )` (`boost/asio/impl/io_context.hpp:207`) runs the function **inline** when
   `blocking.never` is not set and `can_dispatch( )` — the thread is inside `scheduler::run( )` — is
   true. Both hold. The invoker runs `run_ready_handlers( )`, and **the write handler executes right
   there, inside `do_complete( )`**, before `do_run_one( )` has returned, while the read op sits in
   the scheduler queue behind the reactor task.
5. Whoever pops the read op next reaches the strand while it is locked, or after the write handler
   has finished, and runs the read handler after it.

**So the ordinary order is write handler first, read handler second — the opposite of the record.**
The lane's conclusion is right; its stated mechanism is off in one detail: `strand::execute( )` does
not *post* the write handler's invoker, it runs it inline through `can_dispatch( )`. The detail
changes nothing — even a posted invoker would find the write handler already enqueued on the strand,
and the read's later dispatch lands behind it. The read-first order is a race and, in an idle pool,
a narrow one: the thread woken in step 2 must pop the read op, run `recv_op::do_complete( )` and
reach the strand's mutex before the reactor thread gets from the end of `perform_io( )` to the same
mutex in step 4 — a condvar wake against a few hundred instructions. And it is not confined to the
`perform_io( )` batch: where the write's `send( )` fails **speculatively** inside the issuing turn —
the two windows the cases arrange, and every `pumpWrites( )` site with the read armed — the write
handler is posted during that turn, and the read's completion, whether posted by `scheduleRead( )`'s
speculative `recv( )` or queued as a `descriptor_state` when the reactor task next runs, is behind
it, except where the reactor had queued the read's completion before the issuing turn began.

**What follows, and it is the reason this section exists.** With the write handler ordinarily first
and `isClosing( )` false, the arm being removed fired **on every peer ending that reached a pending
write**: as 13a — `CHK_EC( )` and a failed task — whenever a FIN preceded the reset, and as 13e — the
answer dropped, the caller told *do not retry* — whenever the answer was queued and the code admitted.
The findings' §1 row *"FIN then RST, read armed ... its handler runs first and closes"* has the wrong
handler first; its §5 *"what cannot be arranged"* — a read carrying data queued behind the write
through the reactor — is the ordinary both-registered case and **can** be arranged, by the barrier
pattern: a full send buffer, the read armed, the peer's answer, FIN, linger-zero reset. **The change
that landed is strengthened** — (ii) is green in both orders, which is why no case for the
both-registered shape is needed to accept it — and §2.2's point 2 and the 13e row were wrong in the
direction that makes the defect larger, which is the direction this feature's records have been
wrong in before.

**The lane's decision to assert neither ordering in the code was the right call.** The committed
comment says the read decides *whichever* handler runs first, and that is the only sentence the
code's correctness rests on. A comment asserting the order measured by nobody would have been the
exact failure this feature keeps meeting — a true conclusion on a premise no one checked, outliving
whatever checks it later.

**What would settle it by measurement, none of it this slice's:**

- **No code:** build `utf_baselib_h2client6` with `ASIO_ENABLE_HANDLER_TRACKING=1`
  (`projects/make/common.mk`, which defines `BOOST_ASIO_ENABLE_HANDLER_TRACKING`) and run R2. Asio
  then prints every operation's creation and completion with sequence numbers on stderr, and the
  order of the `async_send` and `async_receive` completions is read off the log. The same build
  against a both-registered case is the measurement of the ordinary case.
- **A both-registered case:** the barrier case's parked peer with an answer, a FIN and a linger-zero
  reset in place of the park, asserting nothing about order — both orders are green under (ii) — and
  printing it. A probe for the record, not a case for the module.
- **A standalone probe** outside the repo, in the shape of §12.3's `rstprobe.c`: one strand, both ops
  registered, the peer resets, the two handlers log their order over a few hundred iterations.
  Cheapest, and it measures the race's width as well as its direction.

### 16.4 The three corrections to the findings, checked

1. **R1's recipe as written would not have produced a RST — correct.** At the R1 rendezvous the write
   being armed *is* the preface, so nothing of ours has reached the peer and its receive queue is
   empty; Linux sends a RST from `close( )` only when data was unread, so `shutdown( SHUT_WR )` then
   `close( )` puts out one FIN and nothing more. The driver's preface `send( )` then **succeeds** into
   its own buffer; the peer's orphaned socket resets on receipt, after the fact; the read armed next
   sees the FIN as `eof`, and the case is green on every tree. `SO_LINGER( true, 0 )` resets from
   FIN_WAIT unconditionally, and the two reds' `system:32` are the proof the pair FIN-then-RST landed.
2. **The `poll( )` rendezvous needs an empty events mask and a wall-clock deadline — correct**,
   §16.1: `POLLERR` is reported regardless of the mask, `POLLIN` after a FIN returns at once forever,
   and a slice count is time only while `poll( )` blocks.
3. **The pool reversal condition is vacuous — correct, and verified in the module.**
   `H2Pool_ADriverIsAdoptedOnlyOnceTheTaskPublishesClosedTests` (`TestConnectionPool.h:1658`) is
   built on `StubFactory` with `isFallback = true`, and the publication it waits for is
   `factory -> taskAt( 0U ) -> setState( httpclient::ConnectionState::Closed )` — **the case's own
   call on a `StubConnectionTask`**. `utf_baselib_h2client4` is one header and its main; the real
   `Http2ConnectionTaskT` appears in it once outside comments, in the `static_assert` at `:1910`
   pinning `UNCONFIRMED_MAX_CONCURRENT_STREAMS`. No publication of the real driver's is observed
   there, so the condition attached to the (ii) decision — that this case be unable to accommodate a
   publication one strand turn later — could never have fired. The module was run anyway (20 cases,
   green), which is the right thing to have done with a condition one believes vacuous.

### 16.5 Windows — the exclusion is honest, and it leaves one thing unmeasured that should be named

The two cases are `#if ! defined( _WIN32 )`, on two grounds: the FIN-then-`EPIPE` ending is a POSIX
kernel behaviour, and a Windows send into a reset connection takes `WSAECONNRESET` or
`WSAECONNABORTED`, which map to codes `isPeerClosedErrorCode( )` already admits there — so R1 would be
green before and after the predicate change and discriminate nothing. **Both grounds are true**, and
the predicate half of this change is a no-op on Windows by construction: the fourth and the read-side
predicates differ only by `broken_pipe`, which Winsock never spells for a send. R2 cannot run there
for a reason the lane did not state and NetUtils did: a Windows reset **discards** what is still
unread (measured, 0 of 16384), so no shape can hand the answer over and the status assertion is
unachievable.

**What the exclusion does not cover is the shape half.** (ii)'s premise — the write declines, so the
read must be told, or the ending waits on a timer — is *"unmeasured for a read and a write pending
together"* on Windows in NetUtils' own words, and the findings' §3 said taking (ii) here doubles that
exposure. Nothing compiled on Windows measures it for h2. AGENTS.md's rule is about the change, not
the new cases: **the matrix run of the existing modules is owed** — `h2client6`'s barrier case and
`h2client2`'s read-side closes are where a Windows regression would show. **Recommended and not
required:** a Windows-shaped R1 — the same peer ending with a Windows rendezvous in place of
`<poll.h>` — which is green under either predicate and is exactly the measurement of *the read is
told*; if it hangs to the connect deadline, (ii)'s premise has failed for h2 and §10.1's reopen
applies. Not evasive: the grounds are stated in the file header and the commit message, and NetUtils
says the Windows spelling is the matrix's. Incomplete, in that the consequence was recorded nowhere
until here.

### 16.6 Comments checked against the code they describe

**The two corrections the lane made, verified.** *"Nothing is lost by ending gracefully here"* was
false wherever the write handler ran first with an answer queued — §16.3 makes that the ordinary
case. *"Reaching it a second time would republish state and re-close streams"* was false in both
halves: `publishState( )` returns for any state at or below the current one (`<=` on the enum; the
store never runs), and `closeAllStreamsUnwrittenRetryable( )` loops until `m_streams` is empty, so a
second pass finds nothing; `beginClose( )` (`MultiOperationTask.h:310`) sets two flags under the lock
and is idempotent as well. The replacement gives the true reason for the order — `broken_pipe` is our
own `shutdown_send`'s code and both arms answer yes on our own teardown — and says nothing turns on
it while both arms do nothing. Right, and it matches §2.2's order paragraph.

**Condition at the merge, in its line count.** The second bullet of the *"AND THE READ IS ALWAYS
THERE TO DECIDE"* paragraph reads *"Or none was, and then THIS handler's own strand turn arms one
before it ends."* `onWrite( )`'s own strand turn arms nothing on this arm; the read is armed by the
**turn that issued the write** — `onRead( )`'s or `onProtocolNegotiated( )`'s `scheduleRead( )`, in
the same turn as the `pumpWrites( )` whose speculative `send( )` failed. The next sentence names those
two places correctly, so a reader is misled for one line; but a comment saying a handler arms a read
it does not arm is the class of thing this feature has paid for. One line, same count: *"Or none was,
and then the strand turn which issued this write arms one before it ends."*

**Precisions, recommended and not required.**

- The same paragraph's *"eof both times, measured on this platform in both orders"*: the two orders
  measured are §12.3's, on a bare reset. The FIN case's `eof` is by kernel reading (`tcp_fin( )`'s
  `SOCK_DONE`) plus these two cases' green, which shows the read was handed *an admitted code*, not
  which one. *"Measured for the reset, derived for the FIN"* is what holds.
- The case file's header, *"this makes the failure certain"*: the kernel state is certain; the order
  of the two posted completions is the narrow race of §16.2. *"This makes the window certain"* is
  what it proves.
- The case file's header and R2's comment say the read handler is *"posted second"* — true of the
  window they arrange and consistent with §16.3; nothing to change.
- `isPeerClosedOnWrite( )`'s comment says the fifth predicate *"refuses broken_pipe by
  construction"* — verified against `NetUtils.h`: `isPeerClosedErrorCode( ) && !
  isCleanEndOfStreamErrorCode( )` cannot admit a code the first conjunct refuses.

### 16.7 Found here, not fixed here

- **The 13e row in the owed-work record** says *"In the one window where h2's write arm can fire — no
  read armed"*. §16.3: it fired in the ordinary case. Correct the row's mechanism when the item is
  marked closed — the closure stands either way, and the correction makes the closed defect larger,
  not smaller.
- **The findings document** (`logs/lane1-h2shape/findings.md`, outside the repo) carries §0, the §1
  table's fourth row and §5's *"what cannot be arranged"* with the ordering backwards. It is a session
  log and not a record; nothing to fix, but it must not be read as the mechanism.
- **The double `onPeerClosed( )`** the findings' §6.4 said disappears under (ii): it does, and it was
  harmless before (§16.6). `onRead( )` still has no `isClosing( )` gate, which is right — the read is
  now the only caller.
- **`BL_TASKS_HANDLER_CHK_CANCEL_IMPL( )` sits in `onWrite( )`'s success branch only** (findings
  §6.6), where h1 keeps it outside the guard. Pre-existing; a write failing with an admitted code
  while an external cancel is pending is not checked here, and the accounting fails the task on the
  cancel regardless.
- **This design's status table** at the head has no row for 13a and 13e; they are owed-list items
  rather than change-set A's, and the owed-work record is where their closure belongs.

### 16.8 Agreement, and what is owed

**Agreed; the implementation is accepted on this review, with one condition at the merge:** the
one-line comment correction of §16.6, within its line count. **Recommended and not required:** the
three precisions of §16.6; the Windows-shaped R1 of §16.5. **Owed and recorded so it is not
rediscovered:** the Windows matrix run of the existing modules (§16.5); the (ii) premise on Windows
for h2, measured by nothing (§16.5); the measurement of the handler order (§16.3), which changes no
code and settles a record; the 13e row's mechanism (§16.7).

**What this review could not settle by reading:** the handler order itself — traced through seven
Boost files to one conclusion, run by nothing; the width of the race, which only a loop measures; the
module size and the tier-1 results, which are the lane's and not in the logs here; Windows in every
direction named; and whether the ordinary both-registered shape — a large upload answered early by a
peer that then closes — occurs against a real origin, which no case arranges and which the h1 record
already assumes it does.
