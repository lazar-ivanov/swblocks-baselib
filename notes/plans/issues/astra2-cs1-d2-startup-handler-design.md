# CS-1 / D2 — the HTTP/1.1 driver starts in one accounted strand handler: design note

**Date:** 2026-09-27. **Status:** written by lane 1 for review, **not agreed, not coded.** D2 is
coded only once this note carries a dated agreement line (§12).

**What it implements.** Decision D2 of
[`astra-second-review-decisions.md`](astra-second-review-decisions.md) §3, as taken: *"Astra's startup
handler, closing both defects"* — the race between the first TLS read's start and a request's first
write, and the cancel that lands before the driver starts and is erased by the run's reset. This note
does not reopen the shape; it settles, at the source, the mechanism the decision leaves to the
implementation, and the tests.

**Provenance.** Read on `astra2-cs1` @ `7d21df3`, whose `src/` is `lazari2`'s. Line numbers are that
tree's. Boost is the devenv7 dist's 1.90.0. **Nothing was built or run for this note**; every
mechanism below is a static derivation. Each claim is labelled **VERIFIED** (read at the source cited),
**INFERRED** (follows from verified facts, not checked) or **NOT VERIFIED**.

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
    BL_TASKS_HANDLER_CHK_CANCEL_IMPL()               // §2.2 - a detail the decision leaves open
    if( Ready != m_state [under m_stateLock] )       // §3
        closeConnection(); break;                    // re-asserted close, no read
    scheduleRead();                                  // the first read, with the accounting's guard
    { m_stateLock: m_started = true; hasPending = m_startPending; }
    hasPending ? onStartRequest() : chkArmIdleTimer();   // §5
    BL_TASKS_HANDLER_END_MULTIOP()                   // completes the startup operation
```

`armRead( )` stays, as `scheduleRead( )`'s body; nothing calls it bare any more. `onStartRequest( )`,
`chkArmIdleTimer( )`, `submit( )`, `cancel( )`, `onCancelStream( )` and `initiateClose( )` are not
changed in logic. The name `onStartConnection( )` mirrors `onStartRequest( )`; it is a proposal.

From the first operation on, the only code that touches the stream runs on the strand: the first
read's start moves into a strand handler, and `submit( )` can post `onStartRequest( )` only once
`m_started` is set, which now happens on the strand after that start has returned. **VERIFIED** that
`m_started` has exactly one writer (`:2183`) and one reader (`submit( )`, `:2380`).

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

### 2.2 `CHK_CANCEL_IMPL( )` at the top — proposed; the decision says "under the handler macros" and no more

A **task** cancel (`requestCancel( )`) can land between `scheduleTask( )`'s post and the handler:
`requestCancelInternal( )` sets `m_cancelRequested` and, the task being `Running`, calls the driver's
`cancelTask( )` (`TaskBase.h:1026-1059`), which posts `shutdownOnStreamExecutor( )` **behind** the
startup handler (`:2239-2254`). Without the check the handler arms the read and **starts a pending
request's write** before that shutdown runs, so the request is then "may have been sent" and its
`onClosed( )` from `onTaskStoppedNothrow( )` carries `isRetryable = false` (`:2321`). With it, the
handler ends the run with `operation_aborted` (expected, `TaskBase.h:149-155`) before anything is
armed, and a pending request is answered retryable. The task outcome is the same either way - a
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
     `isShutdownNeeded( )` true
     for a stream the establisher handshook (`:666-674`) - so the close_notify exchange runs as the
     finish continuation, bounded by the 60 s protocol timer (`:109-134`), and the task completes
     from `onShutdownCompleted( )` (`:592-647`), where a peer's `eof` or truncation is not an error.
     **VERIFIED** for the code path; the peer's answer is the test's to arrange (§9.2).
   Then `onTaskStoppedNothrow( )` publishes `Closed` and finds no sink - the cancel's own
   `publishStreamEnd( )` took it (`:1753-1761`) - so the sink is told exactly once. **VERIFIED.**

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

`asio::post( )` can throw only on allocating its operation, and then nothing is enqueued. **INFERRED**
from asio's contract (the tree does not document it). The throw leaves `postToStreamExecutor( )` and
`scheduleTask( )` unchanged and reaches `TaskBase::scheduleNothrow( )`'s catch (`TaskBase.h:1210-1232`),
which posts `notifyReadyImpl( false /* allowFinishContinuations */, eptr, false )` to the thread pool.
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
  `finishStream( )` delivers to the sink, and the request task's sink methods only append to its
  mailbox (`HttpClientRequestTask.h:1928-1943`). **VERIFIED.**
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
**That discrepancy is pre-existing, not introduced here**: every re-arm from `onReadCompleted( )`
(`:1491-1494`) and the write initiator's catch in `onStartRequest( )` (`:752-768`), when reached from
the handler, already do the same. Recorded, not changed. **VERIFIED.**

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
  than `scheduleNothrow( )`'s catch, exactly as the decision record says.
- **What its red becomes.** Against a handler which called `armRead( )` bare instead of
  `scheduleRead( )`, the throw would reach the handler's own catch, the epilog would complete only the
  startup operation, and the count would stay at one with `m_closing` set - the task would never end.
  So the case now pins "the handler arms through the guard", and its red is still a hang, now inside
  the bounded `waitForTaskEnd( )` rather than a deadlock inside `push_back( )`. **INFERRED.**
- **What only the post can still throw** - the scheduleTask route of §4 - has no case, as A4's own
  scheduleTask route had no other; an allocation cannot be arranged from outside. Recorded.
- **Text that goes stale, in files this change-set does not own** (orchestrator's decision, §11): the
  file header and the case's comment in `TestHttp1DriverScheduleThrow.h`, and the A4 recipe in
  `utf_baselib_httpclient7/notes.txt`, which describe the self-deadlock in `scheduleTask( )` as this
  case's red.

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
  **INFERRED**; its comment goes stale (not owned - §11).
- `TestClientSessionTlsHttp1.h:113` and `:861` say `chkArmIdleTimer( )` "is posted from scheduleTask( )";
  after D2 it is called from the handler scheduleTask( ) posts - the same one strand hop. The close_notify
  control's timing does not move. **INFERRED.** Comment only, not owned.
- No invariant in `MultiOperationTask.h` or `TaskBase.h` asks for an operation to be begun **and
  started** inside `scheduleTask( )`: the class note asks only that the count not fall to zero while
  the task is running and not closing (`MultiOperationTask.h:58-60`), and the startup operation keeps
  it at one until the read is begun. **VERIFIED.**

---

## 9. Tests

Every red and green below is deterministic, and each is shown red once and green once
(decision record §2).

### 9.1 Where they live — `utf_baselib_httpclient8`, new, and why

- The barrier case needs a **read-start hook**, which only a test stream policy can give (the driver's
  handlers are non-virtual and bound by `cpp::bind`, which is why the H01 and A4 seams are stream
  policies too). A new policy is a new driver instantiation. `utf_baselib_httpclient7` is the module
  with the strand seams, measured by the orchestrator at 44.6 MB a64 gcc debug, about 38 on x86 - near
  the target - and its seam header is not this change-set's to extend.
- The TLS cases need a TLS HTTP/1.1 peer, which exists only in `utf_baselib_httpclient5` (over
  target, and not to be added to). A test header is never included across modules, and lifting that
  peer into `src/utests/include/` would edit a file this change-set does not own. So `httpclient8`
  carries its own, self-contained - the precedent `TestClientSessionTlsHttp1.h:86-90` states.
- `httpclient8` is created per `src/utests/AGENTS.md`'s checklist and also holds D1's cases, which need
  the same TLS peer. Its object size is measured when it is built and recorded in the lane journal
  and the report; if it lands over the target, that is put to the orchestrator rather than split
  unasked.

**The seams.** One hook policy per transport, each hiding `getStream( )` exactly as
`HeldWritePolicyT` does (`TestHttp1DriverStrandSeam.h:465-492`), over `TcpSocketAsyncStrandedBase`
and `TcpSslSocketAsyncStrandedBase`. The hook stream forwards `async_read_some( )` and
`async_write_some( )` to the real stream - taking the handler by forwarding reference, the defect
`AsioSslStreamWrapper.h:526-547` records - and adds, armed one-shot:

- on the **first** `async_read_some( )`: record `read-start:begin`, call the real initiator, then
  record whether the current thread is running the socket's strand
  (`get_executor( ).target< asio::strand_t >( ) -> running_in_this_thread( )` - Boost 1.90
  `execution/any_executor.hpp:693-706`, `strand.hpp:343-346`; **VERIFIED** in the headers, not yet
  compiled), signal "entered", **hold** until released, record `read-start:end`, return;
- on every `async_write_some( )`: record `write-start` before calling the real one.

Holding **after** the real initiator is deliberate: on the unfixed tree the read's own engine step has
finished before the hold, so the red run observes the ordering violation without itself driving two
TLS engine steps at once. The TLS shutdown of the finish continuation calls the base policy's own
`getStream( )` (`TcpSslBaseTasks.h:548-567`), so it never goes through the hook. **VERIFIED.**

### 9.2 The cases

**D2-a, the barrier — `Http1Driver_…` and `Http1DriverTls_…`.** A helper thread pushes the driver (on
the unfixed tree that call itself enters the hook, under the queue's lock and the task lock). The case
thread waits for "entered", calls `submit( )`, then decides when the hold may end:

- the hook reported **on the strand** (the fixed shape): nothing can run on the strand while it is
  held, and `submit( )` only posts, so no write can start during the hold - release at once;
- **off the strand** (the unfixed shape, or any fix that keeps the start off-strand): post a probe to
  the strand and wait for it. Posts from one thread run in order, so when the probe has run, anything
  `submit( )` posted has run, and a write it started has recorded `write-start` - then release.

Then the exchange completes (the peer answers 200 with a length) and the case asserts **no
`write-start` between `read-start:begin` and `read-start:end`**, plus the response. Red today: the
submit sees `m_started`, `onStartRequest( )` runs on a free strand, the probe runs after it, and the
record is `begin, write-start, end`. Green after: `begin, end, write-start`. Neither branch waits on a
timer, and a reorder fix would also pass, which is right - the case pins the property, not the shape.

**D2-b, cancel before start — `Http1Driver_…` and `Http1DriverTls_…`.** The peer accepts (and
handshakes), then reads until its stream ends, records how it ended, answers a close_notify with its
own, and closes; it never ends the connection on its own. The case: `submit( )`, `cancel( )`, **wait for
the sink's `onClosed( )`** (so the cancel has run before the schedule - the erased-by-reset ordering the
decision describes), then push the driver, with a finite idle lifetime longer than the bound. It
asserts that the task **ended within a bound, successfully**; that the sink was told `operation_aborted`
exactly once; and that the peer saw **our** orderly close (cleartext `eof`, TLS `eof` = close_notify).

Red today: the reset erases `m_closing`, the read is armed and nothing will ever end the task - the
peer does not close and the idle lifetime is past the bound - so the bounded wait expires, and the case
then cancels the task to tear down. Green after: the handler takes the terminal on its first strand
turn. **The bound decides nothing about ordering**: on the unfixed tree the event it waits for cannot
happen at all, and on the fixed one it is due at once, so the only way to misjudge is a stall longer
than the bound - which can only turn a red green, never a green red. That is the same instrument A4's
case uses (`TASK_END_TIMEOUT_IN_MILLISECONDS`), stated here so that it is argued and not assumed.

**A4** - run unchanged, green (§7). The whole of `httpclient3`, `httpclient5` and `httpclient7` is run
as well, for the probe of §8 and every case that starts a driver.

---

## 10. Comments that change with the code — in a comment-only commit after the logic commit

The logic commit carries the new handler with its own documentation and `scheduleTask( )`'s. The
statements elsewhere in `Http1ConnectionTask.h` which D2 makes false are corrected in a separate,
comment-only commit, per AGENTS.md:

- the class note, "THE OPERATIONS IN FLIGHT. At most four" (`:88-98`) - the startup operation, for one
  strand hop;
- `m_readBuffer`'s note, "or armRead( ) threw and nothing ever was" (`:188-191`);
- `armRead( )`'s note, whose first bullet is the `scheduleTask( )` call site (`:947-972`);
- `chkArmIdleTimer( )`'s "It is also why scheduleTask( ) POSTS this" (`:1998-2003`);
- `onTaskStoppedNothrow( )`'s "or the schedule-path arm threw and none ever was" (`:2278-2280`);
- `onStartRequest( )`'s "NOT one of the task handler macros … This runs from a plain post"
  (`:565-573`) - still true of the post from `submit( )`, and it is now also called from inside an
  accounted handler;
- `onWriteCompleted( )`'s "armed since the task was scheduled" (`:824-825`, `:935-937`).

---

## 11. For the orchestrator — outside this change-set's files

- **Stale text D2 leaves in files CS-1 does not own**: `TestHttp1DriverScheduleThrow.h` (header and
  case comment), `utf_baselib_httpclient7/notes.txt` (A4 recipe), `Http1DriverTestUtils.h:887-890`,
  `TestClientSessionTlsHttp1.h:113`, `:861`. Either CS-1's ownership is widened to their comment lines,
  or they are recorded as owed.
- **Not D2, recorded so it is not attributed to it**: a `cancel( )` whose `onCancelStream( )` runs
  after the start still wakes nothing by itself (`s6r2-design.md`, "onCancelStream( ) does not wake
  anything by itself"); it is closed by the next completion or by the pool's `requestCancel( )`. D2
  closes the startup window only, which is what was decided.

---

## 12. Open for review, and agreement

1. `CHK_CANCEL_IMPL( )` at the top of the handler (§2.2) - an addition inside "under the handler
   macros"; the reviewer may prefer the decision's list read literally.
2. The handler's name.
3. Whether §9.2's bounded wait is acceptable as the red of D2-b, as argued there.

**Agreement:** *(pending - to be dated by the orchestrator when the review agrees)*
