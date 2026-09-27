# CS-1 / D2 — the HTTP/1.1 driver starts in one accounted strand handler: design note

**Date:** 2026-09-27. **Status:** revision 2, written by lane 1 after review round 1, **not agreed,
not coded.** D2 is coded only once this note carries a dated agreement line (§12).

**Revisions.** r1 `e7c74a5`. **r2 (this)** carries fable's round-1 review (`D2-design-r1.md`, in the
run's state directory) and every change its §F requires; the orchestrator's answers to it
(`D2-orchestrator-r2.md`), which accept every finding and proposal; and the orchestrator's decisions on
the lane's first report (`CS-1-lane-report-1-decisions.md`), which move D2's cases to new modules and
lift the TLS peer. Where r2 changes a claim of r1, the section says so.

**What it implements.** Decision D2 of
[`astra-second-review-decisions.md`](astra-second-review-decisions.md) §3, as taken: *"Astra's startup
handler, closing both defects"* — the race between the first TLS read's start and a request's first
write, and the cancel that lands before the driver starts and is erased by the run's reset. This note
does not reopen the shape; it settles, at the source, the mechanism the decision leaves to the
implementation, and the tests.

**Provenance.** Read on `astra2-cs1` @ `7d21df3`, whose `src/` is `lazari2`'s. `Http1ConnectionTask.h`'s
line numbers are that tree's; D1's changes at `130e021` and `8a0cdd4` replace lines in place, so every
number cited here still holds. Boost is the devenv7 dist's 1.90.0 and OpenSSL its 3.5.4. **Nothing
was built or run for this note except the one measurement §9.1 names**; every mechanism below is a
static derivation. Each claim is labelled **VERIFIED** (read at the source cited), **INFERRED**
(follows from verified facts, not checked) or **NOT VERIFIED**.

---

## 1. The change, in one screen

`scheduleTask( )` (`Http1ConnectionTask.h:2172-2227`) today publishes `m_started` under `m_stateLock`,
arms the first read inline on the scheduling thread, then posts `onStartRequest( )` or
`chkArmIdleTimer( )`. After D2:

```
scheduleTask( eq )                                   // task lock held (TaskBase.h:1167)
    ensureChannelIsOpen();                           // unchanged, first
    beginOperation();                                // the startup handler's operation
    postToStreamExecutor( bind( &onStartConnection, selfRef() ) );

onStartConnection()                                  // on the strand
    BL_TASKS_HANDLER_BEGIN()                         // takes the task lock
    BL_TASKS_HANDLER_CHK_CANCEL_IMPL()               // §2.2 - settled in review (P1)
    if( Ready != m_state [under m_stateLock] )       // §3
        closeConnection(); break;                    // re-asserted close, no read
    scheduleRead();                                  // the first read, with the accounting's guard
    { m_stateLock: m_started = true; hasPending = m_startPending; }
    hasPending ? onStartRequest() : chkArmIdleTimer();   // §5
    BL_TASKS_HANDLER_END_MULTIOP()                   // completes the startup operation
```

`armRead( )` stays, as `scheduleRead( )`'s body; nothing calls it bare any more. `onStartRequest( )`,
`chkArmIdleTimer( )`, `submit( )`, `cancel( )`, `onCancelStream( )` and `initiateClose( )` are not
changed in logic. The name `onStartConnection( )` mirrors `onStartRequest( )` - settled in review (P2).
`scheduleTask( )`'s `beginOperation( )` is the fifth place this driver begins an operation, beside the
read (`:978`), the write (`:737`), the idle timer (`:2031`) and the deferred verdict (`:1821`).

From the first operation on, the only code that touches the stream runs on the strand: the first
read's start moves into a strand handler, and `submit( )` can post `onStartRequest( )` only once
`m_started` is set, which now happens on the strand after that start has returned. **VERIFIED** that
`m_started` has exactly one writer (`:2183`) and one reader (`submit( )`, `:2380`). The one socket call
left off the strand is `onTaskStoppedNothrow( )`'s `shutdownSocket( )` on §4's route, where a post threw
and no handler of ours was ever enqueued.

---

## 2. The handler macros, and why the epilog is the terminal path after a re-asserted close

### 2.1 `BL_TASKS_HANDLER_BEGIN( )` … `BL_TASKS_HANDLER_END_MULTIOP( )`, like every accounted handler here

- The startup handler completes an operation that `scheduleTask( )` began, so its epilog must be
  `END_MULTIOP( )`: that is what feeds `onOperationCompleted( )` (`TaskBase.h:279-283`). **VERIFIED.**
- `BEGIN( )` takes the task lock (`TaskBase.h:119-120`), as `onReadCompleted( )`, `onWriteCompleted( )`,
  `onIdleDeadline( )` and `onStreamEndDeferred( )` all do. The epilog's `onOperationCompleted( )` runs
  **outside** that lock: the guard lives in the block `BEGIN_IMPL( )` opens (`:111-112`) and
  `END_IMPL_EX( )` closes at `:255`, before `if( __eptr42 )` at `:256`. So a terminal reached from the
  epilog calls `notifyReady( )` with no task lock held, as `TaskBase.h:42-45` requires. **VERIFIED.**
- `break` leaves the `do { } while( false )` of `BEGIN_IMPL( )` (`:116-117`, `:196-197`) and lands in
  the epilog with no error - the idiom `onStreamEndDeferred( )` already uses (`:1888-1891`). The
  not-Ready branch uses it. **VERIFIED.**

### 2.2 `CHK_CANCEL_IMPL( )` at the top — settled in review (P1), inside "under the handler macros"

A **task** cancel (`requestCancel( )`) can land between `scheduleTask( )`'s post and the handler:
`requestCancelInternal( )` sets `m_cancelRequested` and, the task being `Running`, calls the driver's
`cancelTask( )` (`TaskBase.h:1026-1059`), which posts `shutdownOnStreamExecutor( )` **behind** the
startup handler (`:2239-2254`). That order is not a race: `requestCancel( )` takes the task lock
(`TaskBase.h:1237-1246`), which `scheduleNothrow( )` holds from before `scheduleTask( )` until after its
post (`:1167`, `:1208`), so the cancel's post cannot precede the handler's. **VERIFIED.**

Without the check the handler arms the read and **starts a pending request's write** before that
shutdown runs, so the request is then "may have been sent" and its `onClosed( )` from
`onTaskStoppedNothrow( )` carries `isRetryable = false` (`:2321`). With it, the handler ends the run
with `operation_aborted` (expected, `TaskBase.h:149-155`) before anything is armed, and a pending
request is answered retryable. The task outcome is the same either way - a
cancelled task fails with `operation_aborted`, and `TcpSocketCommonBase::onTaskStoppedNothrow( )`
(`TcpBaseTasks.h:111-146`) re-states that for a forcefully shut socket. **VERIFIED** for the paths;
**INFERRED** that no existing case cancels a driver task between its push and its first strand turn.
A task cancel issued **before** the push never reaches `scheduleTask( )` at all - `scheduleNothrow( )`
throws `operation_aborted` first (`TaskBase.h:1196-1206`). **VERIFIED.**

### 2.3 The terminal after a re-asserted close

The not-Ready branch leaves the startup operation as the only one of the run. Traced through
`MultiOperationTask.h`, all **VERIFIED**:

1. `MultiOperationTaskT::scheduleNothrow( )` zeroed the run: count 0, `m_closing`,
   `m_closingDeliberate`, `m_closeInitiated`, `m_terminalTaken` false, no first error (`:477-485`).
2. `scheduleTask( )`'s `beginOperation( )` makes the count 1 (`:218-223`).
3. The handler's `closeConnection( )` sets `m_state` to `Draining` unless `Closed` (`:1966-1982`) and
   calls `beginClose( )`: `m_closing` and `m_closingDeliberate` true (`:310-316`). No operation is
   started.
4. The epilog: `onOperationCompleted( nullptr, false )` takes the count to 0; no error to record;
   `m_closing && ! m_closeInitiated` sets `close` (`:442-447`); `takeTerminalNoLock( )` finds closing,
   count 0, not taken, and claims it (`:147-157`, `:449`).
5. `applyDecision( )` runs `initiateClose( )` - channel open, no write in flight, so only
   `getSocket().cancel( )` on a socket with nothing registered and `cancelIdleTimer( )` with no timer
   (`:2144-2165`) - and then `notifyReady( nullptr, false )` (`MultiOperationTask.h:163-196`).
6. `notifyReadyImpl( true, … )` (`TaskBase.h:551-733`) asks `scheduleTaskFinishContinuation( )`:
   - cleartext: `TaskBase`'s default, `false` (`TaskBase.h:934-939`); nothing between the plain
     stranded policy and `TaskBase` overrides it (grep of `tasks/` and `httpclient/`); **VERIFIED**;
   - TLS: `TcpSslBaseTasks.h:470-517`. No forced shutdown, not yet scheduled for shutdown,
     `m_isCloseStreamOnTaskFinish` set by the driver's constructor (`Http1ConnectionTask.h:310`), and
     `isShutdownNeeded( )` true for a stream the establisher handshook (`:666-674`) - so the
     close_notify exchange runs as the
     finish continuation, bounded by the 60 s protocol timer (`:109-134`), and the task completes
     from `onShutdownCompleted( )` (`:592-647`), where a peer's `eof` or truncation is not an error.
     **VERIFIED** for the code path; the peer's answer is the test's to arrange (§9.2).
   Then `onTaskStoppedNothrow( )` publishes `Closed` and finds no sink - the cancel's own
   `publishStreamEnd( )` took it (`:1753-1761`) - so the sink is told exactly once. **VERIFIED.**

   Two details the implementation review will ask about, both **VERIFIED**: `notifyReadyImpl( )`
   re-takes the task lock itself (`TaskBase.h:564`) and runs the finish continuation and
   `onTaskStoppedNothrow( )` under it, so on the TLS route `beginProtocolShutdown( )` is initiated on
   the strand under the task lock - which is what today's idle close already does; and the first
   `notifyReadyImpl( )` returns at `:572` once the continuation is scheduled, so `onTaskStoppedNothrow( )`
   runs exactly once, from `onShutdownCompleted( )`'s `BL_TASKS_HANDLER_END( )` (`TcpSslBaseTasks.h:646`).

**The task completes successfully**: `beginClose( )` records no error, and nothing else ran. The
decision record's "the task takes its terminal path at once" holds, with the TLS close_notify
exchange as the one hop it cannot skip. That exchange is not a wait on the idle timeout or on the
peer choosing to close; it is the peer answering our close (§9.2 makes the test depend on exactly
that). **INFERRED** from the above.

Why this is the first completion after `beginClose( )`: nothing else is in flight - no read was armed,
and a request cancelled before the start never wrote. **VERIFIED**: the only operations this driver
begins are the read (`:978`), the write (`:737`), the idle timer (`:2031`) and the deferred verdict
(`:1821`), and none is reached on this branch.

---

## 3. What "no longer Ready" reads, and under which lock

**`httpclient::ConnectionState::Ready != m_state`, read under `m_stateLock`** - the lock that guards
`m_state`, `m_handle` and the handed-over request (`:258-276`).

- **It is the one signal the run's reset cannot erase.** `m_closing` is zeroed by `scheduleNothrow( )`
  (`MultiOperationTask.h:480`), so `isClosing( )` cannot tell the handler that a cancel ran first;
  `m_state` belongs to the driver and nothing resets it per run. **VERIFIED.**
- **Before the handler, only a stream cancel moves it off `Ready`.** Its writers are three:
  `publishStreamEnd( )` (`:1762-1768`), `closeConnection( )` (`:1973-1976`) and `onTaskStoppedNothrow( )`
  (`:2305`). The first two are reached only from strand handlers (their callers: `finishStream( )` from
  `onStartRequest( )`, `onBytesRead( )`, `onPeerClosed( )`, `onCancelStream( )`; `closeConnection( )`
  from `onReadCompleted( )`, `onIdleDeadline( )`, `publishStreamEnd( )`), and before the start the only
  one of those that can run is `onCancelStream( )` - `onStartRequest( )` needs `m_started`, and every
  other handler follows an operation this run has not begun. The third runs only at a terminal,
  which cannot be due while the startup operation is pending. **VERIFIED** by enumerating the writers
  (`grep -n "m_state"`) and their callers.
- **One read is exact.** Between the check and the handler's `m_started` critical section, the only
  concurrent writer is `submit( )`, which moves `m_handle` and `m_startPending` and leaves `m_state`
  `Ready`; a `cancel( )` only posts, so its `onCancelStream( )` runs after the handler. **VERIFIED.**
- **`Closed` is covered too.** A driver re-pushed after a completed run would reach the handler with
  `m_state == Closed` (`shutdownSocket( )` does not close the socket, `TcpBaseTasks.h:241-368`, so
  `ensureChannelIsOpen( )` passes), and `closeConnection( )` leaves `Closed` alone. Nothing re-pushes a
  driver - the pool schedules one only while it is `Created` (`ConnectionPool.h:1400-1409`) - so this
  is **INFERRED** and not a case.

**The start is exactly-once against `submit( )`**, and the handler does not clear `m_startPending`
itself: it sets `m_started` and reads `m_startPending` in one critical section, and `submit( )` sets
`m_startPending` and reads `m_started` in one (`:2362-2381`). Whichever runs second starts the request -
the handler by calling `onStartRequest( )`, which takes the flag under the same lock (`:582-606`), or
`submit( )` by posting it. After the handler's section a second `submit( )` is refused by the
allocated handle (`:2365-2371`), and nothing else sets `m_startPending`. **VERIFIED.**

---

## 4. A post that throws in `scheduleTask( )` — A4's route, count left at one

A post onto the strand never runs inline - `asio::post( )` requires `blocking.never`
(`detail/initiate_post.hpp:52`, `:114`) and `do_execute( )` then skips its inline branch
(`detail/impl/strand_executor_service.hpp:226-235`) - and it allocates and constructs its operation
before it enqueues it (`:238-246`), so a throw there enqueues nothing. The one call after the enqueue,
scheduling the strand's invoker on the io_context (`:250`), can fail on the same allocation, and then
leaves the handler enqueued on a strand nothing will ever run. Either way no handler of ours runs, and
the route below is the same. **VERIFIED** in the dist's Boost 1.90 headers. *(r1 said "can throw only
on allocating its operation, and then nothing is enqueued", which is narrower than the source - F4.)*

The throw leaves `postToStreamExecutor( )` and `scheduleTask( )` unchanged and reaches
`TaskBase::scheduleNothrow( )`'s catch (`TaskBase.h:1210-1232`), which posts
`notifyReadyImpl( false /* allowFinishContinuations */, eptr, false )` to the thread pool.
**VERIFIED.** From there:

- the task completes with that exception, from a pool thread with no lock held - which is what that
  catch exists for, and why `armRead( )`'s comment sends its throw there (`:969-972`);
- no finish continuation runs, so no TLS shutdown is attempted; the policy's `onTaskStoppedNothrow( )`
  shuts the socket (`TcpSslBaseTasks.h:538-541`, `TcpBaseTasks.h:636-639`); **VERIFIED**;
- the driver's `onTaskStoppedNothrow( )` publishes `Closed` and answers a request submitted before
  the schedule with `onClosed( error, isRetryable = true )` - nothing was written (`:2283-2328`);
  **VERIFIED**;
- **the count is left at one** - `beginOperation( )` ran and nothing completes it. Nothing reads it:
  no handler was enqueued, the terminal is taken from the catch rather than from the accounting, and
  `onCancelStream( )` → `closeConnection( )` → `beginClose( )` touches only the flags. The next run's
  `scheduleNothrow( )` zeroes it (`MultiOperationTask.h:479`). **VERIFIED** for the writers; the
  decision chose this route over `abandonOperation( )` + rethrow, which would leave zero and buys
  nothing observable.

---

## 5. The idle timer and a pending start, inside the handler

Both are called **directly**, on the strand, inside the accounted handler - "both of which already
expect to run on the strand" (decision record). Neither needs a post of its own any more.

- **A pending start** (a request submitted before the handler ran, which is the pool's first request
  on a fresh fallback driver): `onStartRequest( )` takes the flag, records `m_requestSaidClose`,
  cancels the idle timer (none is armed yet in this run), builds the parser, renders, and begins the
  write operation (`:575-771`). Its three exits behave as they do from a post:
  - `isClosing( )` true - possible only if `scheduleRead( )` just failed (§6) - finishes the stream
    `operation_aborted`, retryable, not usable (`:620-636`);
  - a render failure finishes it `invalid_argument`, retryable, connection usable (`:685-706`);
  - an initiator throw completes the write's operation with the error (`:752-768`). The count cannot
    reach zero there: the startup operation is still pending.
  Calling it under the task lock is what `onReadCompleted( )` already does when it finishes a stream:
  `finishStream( )` delivers to the sink, and the request task's sink methods append to its mailbox
  under the mailbox's own lock and at most post a drain to the thread pool's io_service
  (`HttpClientRequestTask.h:374-411`, `:1928-1943`) - no task lock and no call into an execution queue.
  **VERIFIED.** *(r1 said the sink methods "only append", which left out the drain post - F5.)*
- **No pending start**: `chkArmIdleTimer( )` (`:2005-2054`) arms the timer only when enabled, not
  closing, the channel open and **no handle allocated**. A `submit( )` which lands after the handler's
  critical section allocates the handle first, so either the timer is not armed or `onStartRequest( )`
  - posted by that `submit( )` - cancels it (`:618`), and `onIdleDeadline( )` then finds `ec` set and
  closes nothing (`:2083-2098`). **VERIFIED.** This is today's rule, moved one post earlier.
- **After a re-asserted close** neither is reached: the branch breaks first (§2.3).

---

## 6. The first read inside the handler: `scheduleRead( )`, and what its catch does there

`scheduleRead( )`'s catch completes the phantom operation with the error (`:1003-1013`). Inside the
handler that call runs **under the task lock**, with the startup operation still pending, so the count
cannot reach zero and no terminal is due - `notifyReady( )` cannot be reached under the lock.
**VERIFIED.** What it does reach, as the first error, is `initiateClose( )` (`MultiOperationTask.h:442-447`),
under the task lock. `initiateClose( )` takes no task lock (`:2103-2166`), so this is safe; but
`MultiOperationTask.h:349-354` says initiateClose( ) is called "never while the task lock is held".

**That discrepancy is pre-existing at three sites and extended by one here.** Each initiator catch in
this file completes its operation with `onOperationCompleted( eptr )`, and a first error there runs
`initiateClose( )` inline. Three of them are already reached inside a handler body, under the task lock:

- the re-arm's (`:1009-1012`), from `scheduleRead( )` in `onReadCompleted( )` (`:1493`);
- `chkArmIdleTimer( )`'s (`:2043-2051`), from `publishStreamEnd( )` (`:1787`), which a keep-alive response
  reaches from `onReadCompleted( )` through `onBytesRead( )` and `finishStream( )` (`:1182`, `:1716`), and
  the deferred verdict from `onStreamEndDeferred( )` (`:1952`);
- `deferStreamEnd( )`'s (`:1835-1840`), from `finishStream( )` (`:1712`) inside `onReadCompleted( )`.

The fourth is new with D2: the write initiator's catch in `onStartRequest( )` (`:752-768`) runs today
only from a plain post with no task lock (`:565-573`, posted from `:2202-2207` and `:2385-2390`), and
after D2 it also runs inside the accounted handler. `chkArmIdleTimer( )` gains the handler as a caller
too, but it is a site already. All are safe for the same reason - `initiateClose( )` takes no task lock
and begins nothing (`:2103-2166`). Recorded, not changed; the comments in this file that state the
invariant are corrected in §10. **VERIFIED.** *(r1 called the re-arm and the write's catch
pre-existing; the write's is new with D2 - F3. F3 counted one pre-existing site; the idle timer's and
the deferred verdict's catches make it three, which changes the count and not the conclusion.)*

**Written once, here, for the core comment no lane owns** (P6). After D2, `initiateClose( )` is reached
under the task lock from four initiator catches inside handler bodies. `MultiOperationTask.h:349-354`
should eventually say "never while the task lock is held, except from an initiator's catch inside a
handler body, where the terminal cannot be due". The orchestrator's owed list carries a one-line
pointer to this paragraph; the core comment is not changed by CS-1.

The handler then goes on to set `m_started` and start or idle as in §5 - with `isClosing( )` true
after a failed arm, so a pending request is answered retryable and the timer is not armed - and its
epilog takes the terminal with the read's exception as the first error (§7).

---

## 7. What A4's case exercises afterwards

`Http1Driver_ScheduleReadInitiatorThrowEndsTheTaskTests` (`utf_baselib_httpclient7/TestHttp1DriverScheduleThrow.h:230-243`)
arms the seam so the next read initiator throws, then pushes the driver. After D2, traced:
`push_back( )` → `scheduleTask( )` begins the startup operation and posts - nothing throws there -
and returns. The handler runs on the strand, `scheduleRead( )` → the seam throws → the catch completes
the read's phantom operation with it: first error, `m_closing`, `initiateClose( )`, count 1. No
request, idle timeout disabled, so nothing further is begun. The epilog takes the count to 0 and the
terminal, and the task completes **failed, with "The read initiator was made to fail"**. **INFERRED**
from the verified pieces above; to be shown by running it.

- Its three assertions - ended, failed, our message - **still hold**, by the handler's route rather
  than `scheduleNothrow( )`'s catch, exactly as the decision record says. The assertion set is
  unchanged: the case does not assert `pendingOperations( )` (`TestHttp1DriverScheduleThrow.h:225-228`),
  and the propagating route's "count left at one" (§4) is still the convention `armRead( )`'s comment
  states (`:969-972`).
- **What its red becomes.** Against a handler which called `armRead( )` bare instead of
  `scheduleRead( )`, the throw would reach the handler's own catch, the epilog would complete only the
  startup operation, and the count would stay at one with `m_closing` set - the task would never end.
  So the case now pins "the handler arms through the guard", and its red is still a hang, now inside
  the bounded `waitForTaskEnd( )` rather than a deadlock inside `push_back( )`. **INFERRED.**
- **What only the post can still throw** - the scheduleTask route of §4 - has no case, as A4's own
  scheduleTask route had no other; an allocation cannot be arranged from outside. Recorded.
- **Text that goes stale**: the file header and the case's comment in `TestHttp1DriverScheduleThrow.h`,
  and the A4 recipe in `utf_baselib_httpclient7/notes.txt`, which describe the self-deadlock in
  `scheduleTask( )` as this case's red. CS-1 owns their comment lines for this purpose (§11).

---

## 8. The reversal condition — does anything depend on the read starting inside `scheduleTask( )`?

The decision reverses to the reorder "if implementing it turns up a test or invariant that depends on
the read starting inside `scheduleTask( )` itself". Searched: every `scheduleTask`, `m_started`,
`armRead` and "synchronously" under `src/utests` and `src/include`. **Found one dependent, and it does
not reverse the decision:**

- `Http1DriverProbe::scheduleTask( )` (`src/utests/include/utests/baselib/Http1DriverTestUtils.h:883-902`,
  used by `utf_baselib_httpclient3`) calls the base and then posts `submitAndProbe( )`, commenting that
  the base "arms the read and publishes m_started - both synchronously". After D2 the base posts the
  startup handler **first**, from the same thread, to the same strand, so the handler runs before
  `submitAndProbe( )` and `m_started` is set when `submit( )` reads it - the probe's
  `[ onStartRequest, peerClosedProbe ]` ordering is unchanged. It depends on **strand FIFO** for two
  posts from one thread, which the H01 seam cases already rely on, not on the read starting inline.
  One detail: the startup handler, dequeued first, **blocks on the task lock** until
  `scheduleNothrow( )` returns, which is after the probe's own post - an ordinary wait, not a deadlock,
  because the scheduling thread waits on nothing. **INFERRED**; its comment goes stale (§11).
- `TestClientSessionTlsHttp1.h:113` and `:861` say `chkArmIdleTimer( )` "is posted from scheduleTask( )";
  after D2 it is called from the handler scheduleTask( ) posts - the same one strand hop. The close_notify
  control's timing does not move. **INFERRED.** Comment only (§11).
- No invariant in `MultiOperationTask.h` or `TaskBase.h` asks for an operation to be begun **and
  started** inside `scheduleTask( )`: the class note asks only that the count not fall to zero while
  the task is running and not closing (`MultiOperationTask.h:58-60`), and the startup operation keeps
  it at one until the read is begun. **VERIFIED.**

---

## 9. Tests

Every red and green below is deterministic, and each is shown red once and green once
(decision record §2).

### 9.1 Where they live, and what they share

**Modules - decided by the orchestrator on the lane's first report.** D2's four cases go to a new
module, **`utf_baselib_httpclient11`**, reserved for CS-1, and not to `httpclient8`: `httpclient8`
measures 36.8 MB a64 clang debug with D1's four cases alone, and `httpclient7` measures 34.1 MB there
and about 38 MB on x86, which puts `httpclient8` at about the 40 MB x86 target already. All four are
built in `httpclient11` and measured; **if it lands over 36 MB a64 clang debug, the TLS pair moves to
`utf_baselib_httpclient12`**, also reserved for CS-1. Each is created per `src/utests/AGENTS.md`'s
checklist, with a `notes.txt` recipe for every case. The hook of this section is written in its own
header in `httpclient11`, and is lifted into `src/utests/include/` only if the split happens - a
helper two modules need.

A new module at all, for r1's reason: the barrier case needs a read-start hook, which only a test
stream policy can give - the driver's handlers are non-virtual and bound by `cpp::bind`, which is why
the H01 and A4 seams are stream policies too - and a new policy is a new driver instantiation, while
`httpclient7`'s seam header is not CS-1's to extend.

**A measured correction to the review's size accounting.** Round 1 (§D) said that including
`Http1DriverTestUtils.h` instantiates `Http1DriverProbe`, a cleartext driver, in the including TU, and
asked for it to be counted. It is not in the object: `httpclient8` includes that header, and its a64
clang debug object at `3996848` carries **no** symbol naming `Http1ConnectionTaskT< TcpSocketAsyncStrandedBaseT< void > >`,
`Http1DriverProbe` or `TcpSocketAsyncStrandedBaseT< void >` - 0, 0 and 0 lines of llvm-nm, against 901
naming the TLS driver the module does use (`logs/astra2/cs1/hc8-object-driver-symbols-3996848.log`).
**MEASURED**, clang debug only: an inline function nothing calls emits nothing, whatever it names. So
`httpclient11`'s size is its own two hook drivers, two establishers and two peers.

**The TLS peer - lifted, by the orchestrator's decision.** Two CS-1 modules now need a TLS HTTP/1.1
peer, so the brief's rule applies: `httpclient8`'s peer moves into one new shared header,
**`src/utests/include/utests/baselib/Http1DriverTlsTestUtils.h`** - a file only CS-1 touches, distinct
from CS-2's `HttpClientSessionTestUtils.h` - in a commit of its own before D2's cases, with
`httpclient8` re-run green after it. The TLS establishment helpers (the key, the request, and the
establisher over a given driver type) follow it into that header when D2's TLS cases, their second
user, are written. `httpclient5`'s peer is not touched; one peer for the tree stays owed. *(r1 carried
a self-contained copy; the orchestrator first accepted that and then amended the answer,
`CS-1-lane-report-1-decisions.md` item 1a.)*

**The hook, specified to the point of being buildable (F7).**

- **The stream.** One template, `ReadStartHookStream< INNER >`, over the stream the policy hands out -
  `tcp::socket` for cleartext, `AsioSslStreamWrapper` for TLS. It holds a reference to that stream and
  forwards `async_read_some( )` and `async_write_some( )`, taking the handler by forwarding reference
  (the defect `AsioSslStreamWrapper.h:526-547` records). It declares `executor_type` and
  `get_executor( )`, which `asio::async_write( )` needs (`HeldWriteStream` has both,
  `TestHttp1DriverStrandSeam.h:251-262`): the socket's own executor - `inner.get_executor( )` for
  cleartext, `inner.getSocket( ).get_executor( )` for TLS - which is the strand, and which is also what
  the case posts its handler and its probe to.
- **The policies.** `ReadStartHookPolicyT< BASE >`, over `TcpSocketAsyncStrandedBase` and
  `TcpSslSocketAsyncStrandedBase`, each hiding `getStream( )` exactly as `HeldWritePolicyT` does
  (`TestHttp1DriverStrandSeam.h:465-492`) and building the hook stream lazily over
  `base_type::getStream( )`. `stream_t` and `stream_ref` are inherited, so the establisher hands over
  the real stream unchanged. The TLS finish continuation's shutdown calls the base policy's own
  `getStream( )` (`TcpSslBaseTasks.h:548-567`), which a hiding definition cannot reach, so it never
  goes through the hook. **VERIFIED.**
- **The control block and the record.** Process global, like `SeamControl`
  (`TestHttp1DriverStrandSeam.h:93-164`) and for its reason - the driver is built by a factory, so no
  per-connection control can reach it, and the module runs one connection at a time. One mutex and one
  condition variable guard `armed` (one shot: the next `async_read_some( )` holds, and only that one),
  `entered`, `onStrand`, `released`, and the record - an ordered list of `read-start:begin`,
  `read-start:end`, `write-start`, `hold-timeout` and `no-strand`. Each case re-arms it, which resets
  all of it.
- **The hold.** On the armed `async_read_some( )`: record `read-start:begin`; call the real initiator;
  take the socket's executor and its `target< asio::strand_t >( )` - if that is null, record
  `no-strand`, signal `entered` and do not hold, so the case fails with a name instead of mis-branching;
  otherwise store `running_in_this_thread( )` as `onStrand` and signal `entered`; wait for `released`,
  **bounded at 30 s**, recording `hold-timeout` if the bound expires; record `read-start:end`; return.
  Every later `async_read_some( )` forwards and records nothing; every `async_write_some( )` records
  `write-start` and forwards. Holding **after** the real initiator is deliberate: on the unfixed tree
  the read's own engine step has finished before the hold (`ssl/detail/io.hpp:151-154`, `:343-349`), so
  the red run observes the ordering violation without itself driving two TLS engine steps at once.
- **Why the type test cannot miss** (F9). `make_strand( io_context& )` yields
  `strand< io_context::executor_type >` (`TcpStrandedStreams.h:143`, `TcpSslStrandedStreams.h:144`),
  which is exactly `strand_t` (`OSBoostImports.h:78`), and the socket built on it carries that type
  inside its `any_io_executor` (`execution/any_executor.hpp:692-706`; `running_in_this_thread( )`,
  `strand.hpp:343-346`). A `target<>( )` of the wrong type returns null **silently** and would send
  every run down the off-strand branch - which on the fixed tree waits for a probe the held strand
  cannot run, and which the hold's bound would turn into a false red. `no-strand` names that failure.
  **VERIFIED** in the headers; not yet compiled.

### 9.2 The cases

**D2-a, the barrier - `Http1Driver_NoWriteStartsBeforeTheFirstReadStartReturnsTests` and its
`Http1DriverTls_` twin.** The peer answers one request with a 200 and a length, and then waits to be
released. The case arms the hook and has a helper thread push the driver - on the unfixed tree that
call itself enters the hook, under the queue's lock and the task lock (`ExecutionQueueImpl.h:644-694`,
`TaskBase.h:1167`). It waits for `entered`, bounded, and **reads `onStrand` before it submits anything,
then decides how to submit and when the hold may end**:

- **on the strand** (the fixed shape): `submit( )` from the case thread, then release at once. Nothing
  can run on the strand while the hook holds it, and `submit( )` only posts, so no write can start
  during the hold;
- **off the strand** (the unfixed shape, or any fix that keeps the start off-strand): post **one**
  handler to the strand which calls `submit( )` and then posts the probe - the
  `Http1DriverProbe::submitAndProbe( )` shape (`Http1DriverTestUtils.h:823-835` says why nothing weaker
  is deterministic) - and wait for the probe, bounded; then release. While that handler holds the
  strand its two posts are queued in order behind it, `[ onStartRequest, probe ]`, and the write's
  completion cannot be enqueued before `onStartRequest( )` has run, so it lands behind the probe.
  `submit( )` from the case thread with a probe posted afterwards is NOT this: the write's completion
  can reach the strand between the two posts, its prolog takes the task lock the held `push_back( )`
  holds (`:859`), the strand blocks, and the case hangs inside `push_back( )` with the queue lock -
  A4's unreportable hang, as a race. *(r1 posted the probe from the case thread - F1.)*

Then the response completes, the case joins the helper, and it asserts: no `hold-timeout` and no
`no-strand`; **no `write-start` between `read-start:begin` and `read-start:end`**; and the response - one
final 200, closed successfully. Red today: `m_started` is set before the read's start, the handler's
`submit( )` posts `onStartRequest( )`, the probe runs after it, and the record is
`begin, write-start, end` on every run. Green after: `begin, end, write-start`, because the start
handler's own critical section starts the request after `scheduleRead( )` has returned. Neither branch
waits on a timer - the bounds only turn a broken arrangement into a reported failure - and a reorder
fix would also pass, which is right: the case pins the property, not the shape.

**How D2-a's driver ends** (F7.5). The response is keep-alive and the idle timer is disabled, so after
its assertions the case ends the connection **from the peer**, and waits for the task, bounded: the
cleartext peer closes; the TLS peer sends a close_notify and waits for ours (`stream.shutdown( ec )`),
so the driver's idle read ends `eof` and its own shutdown meets a peer which answers. The case asserts
the task ended clean. The TLS peer ends with a close_notify and not a truncation because after a
truncation the driver's TLS shutdown waits the full 60 s protocol timer - measured in D1's module
(the lane journal's finding F-f), pre-existing and not D2's.

**D2-b, cancel before start - `Http1Driver_CancelBeforeStartEndsTheConnectionTests` and its
`Http1DriverTls_` twin.** The peer accepts (and handshakes), reads until its stream ends and records
how it ended; the TLS peer then answers our close_notify with its own - `stream.shutdown( ec )` after
the read returned `eof`, which sends the alert at once because ours was already received - and the
script ends. It never ends the connection on its own (F7.6). The case: `submit( )`, `cancel( )`, **wait
for the sink's `onClosed( )`** (so the cancel has run before the schedule - the erased-by-reset
ordering the decision describes), then push the driver, built with an idle lifetime of 120 s, longer
than the bound. It asserts that the task **ended within the bound, successfully**; that the sink was
told `operation_aborted` exactly once; and that the peer saw **our** orderly close - cleartext `eof`,
TLS `eof`, which is our close_notify.

Red today: the reset erases `m_closing`, the read is armed and nothing will end the task - the peer
does not close and the idle lifetime is past the bound - so the bounded wait expires, and the case
then cancels the task to tear down. Green after: the handler takes the terminal on its first strand
turn. **The bound decides nothing about ordering**: on the unfixed tree the event it waits for cannot
happen at all, and on the fixed one it is due at once, so the only way to misjudge is a stall longer
than the bound on the **fixed** tree - which can only make a passing case fail, never make the unfixed
tree pass. A false red is investigated; a false green is not, and this instrument cannot produce one.
*(r1 said the opposite - F2.)* The bound is A4's 30 s (`TASK_END_TIMEOUT_IN_MILLISECONDS`); a test header
is never included across modules, so the value is repeated, with a pointer to A4's.

**A4** - run unchanged, green (§7). The whole of `httpclient3`, `httpclient5`, `httpclient7` and
`httpclient8` is run as well, for the probe of §8 and every case that starts a driver.

### 9.3 The evidence the implementation owes

Each file in `logs/astra2/cs1/`, naming the commit it was taken at: D2-a and D2-b, cleartext and TLS,
**red on the tree before the logic commit and green after** - four reds, four greens; A4 green after;
`httpclient3`, `5`, `7`, `8` and the new module or modules whole-module green; the D2-b TLS run showing
the peer recorded `eof`; each new module's measured object size; and the tier 1 report, with every
line explained in the lane journal.

---

## 10. Comments that change with the code — in a comment-only commit after the logic commit

The logic commit carries the new handler with its own documentation and `scheduleTask( )`'s. The
statements elsewhere in `Http1ConnectionTask.h` which D2 makes false are corrected in a separate,
comment-only commit, per AGENTS.md:

- the class note, "THE OPERATIONS IN FLIGHT. At most four" (`:88-98`) - the startup operation, for one
  strand hop;
- the class note, "onStartRequest() runs from a plain post holding no task lock" (`:72-81`) - still true
  of the posted route, and it is now also called from inside the accounted handler; the paragraph's
  conclusion, that the class is correct over the stranded policies only, is unchanged (F8);
- `m_readBuffer`'s note, "or armRead( ) threw and nothing ever was" (`:188-191`);
- `m_idleTimer`'s note, "cancelled from initiateClose( ), which the accounting calls exactly once and
  never while the task lock is held" (`:198-201`) - already untrue of the three sites of §6, and after
  D2 of a fourth (F8);
- `armRead( )`'s note, whose first bullet is the `scheduleTask( )` call site (`:947-972`);
- `chkArmIdleTimer( )`'s "It is also why scheduleTask( ) POSTS this" (`:1998-2003`);
- `onTaskStoppedNothrow( )`'s "or the schedule-path arm threw and none ever was" (`:2278-2280`);
- `onStartRequest( )`'s "NOT one of the task handler macros … This runs from a plain post"
  (`:565-573`) - still true of the post from `submit( )`, and it is now also called from inside an
  accounted handler;
- `onWriteCompleted( )`'s "armed since the task was scheduled" (`:824-825`, `:935-937`).

---

## 11. Outside `Http1ConnectionTask.h`

- **Stale text D2 leaves in other files - decided** (`D2-orchestrator-r2.md`): CS-1's ownership is
  widened to the **comment lines only** of `TestHttp1DriverScheduleThrow.h` (the header and the A4
  case's comment), `utf_baselib_httpclient7/notes.txt` (the A4 recipe), `Http1DriverTestUtils.h:887-890`
  and `TestClientSessionTlsHttp1.h:113` and `:861`. They are corrected in a comment-only commit after
  D2's logic commit, together with or beside §10's.
- **Not D2, recorded so it is not attributed to it**: a `cancel( )` whose `onCancelStream( )` runs
  after the start still wakes nothing by itself (`s6r2-design.md`, "onCancelStream( ) does not wake
  anything by itself"); it is closed by the next completion or by the pool's `requestCancel( )`. D2
  closes the startup window only, which is what was decided.

---

## 12. Review, and agreement

**Round 1 (2026-09-27)**, fable, `D2-design-r1.md`: *agree with changes*. The three questions r1 left
open are settled - `CHK_CANCEL_IMPL( )` at the top of the handler (P1, §2.2), the name
`onStartConnection( )` (P2, §1), and the bounded wait as D2-b's red with its direction corrected (P3,
§9.2). The orchestrator accepted every finding and proposal (`D2-orchestrator-r2.md`). §F's changes are
in this revision: F1 and F7 in §9.1-9.2, F2 in §9.2, F3 and P6 in §6, F4 in §4, F5 in §5, F8 in §10, F9
in §9.1; F6/P4 is superseded by the orchestrator's decision to lift the TLS peer (§9.1).

**Two claims of round 1 revised in place, at the source**: §6 counts three pre-existing under-lock
initiator catches where F3 counted one - the conclusion is unchanged; and §9.1 measures that
`Http1DriverTestUtils.h` puts no cleartext driver in the object, where §D asked for one to be counted.

**Agreement:** *(pending - dated by the orchestrator once fable has re-checked this revision against §F)*
