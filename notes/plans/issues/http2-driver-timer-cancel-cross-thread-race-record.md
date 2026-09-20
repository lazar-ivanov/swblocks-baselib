# The HTTP/2 driver cancels its asio timers from two threads which do not exclude each other

**Origin:** item 3 of the L6 review's second-pass "what I would not ship"
(`notes/plans/issues/http2-l6-review-record.md`): *"A ThreadSanitizer run of `httpclient4` or
`httpclient5` before any a64 deployment"*. No TSan run had ever covered the session modules - both
lanes which built them reported "no ThreadSanitizer line" from **ordinary** builds, which is the
absence of a report and not evidence. **Date:** 2026-09-20. **Status:** found by the validation
round of §1-§5 (which could not fix it, see §6) and **FIXED** by the change-set of §7, on
`http2-lane-1`.

Host: `ub24-a64-dev-d2-rosetta`, Linux 6.8.0-139, aarch64, 2 cores. Toolchain devenv7, clang2010,
`VARIANT=debug`, `BL_CLANG_ENABLE_RA_TSAN=1`, revision `285085f`.

---

## 1. What was run

An instrumented tree of its own - the ordinary `bld/` was moved aside first, because `PLAT` carries
no sanitizer and the two would otherwise share `bld/ub24-a64-clang2010-debug` and silently mix
(`notes/plans/issues/tsan-baseline-and-threadpool-resize-race-record.md` §7). `TSAN_OPTIONS` was set
by hand on every run - make's `export` reaches only a binary make launches, and these are launched
by hand - to the same value `clang-analysis.mk:249` exports.

| module | runs | cases | result |
|---|---:|---:|---|
| `utf_baselib_httpclient4`, whole module | 11 | 17 | Boost.Test green every run; **4 of the 11 reported the race of §2** |
| `utf_baselib_httpclient4`, the two draining cases | 20 | 2 | green; **12 runs of 20 reported it**, 15 reports in all |
| `utf_baselib_httpclient4`, the three fallback cases | 20 | 3 | green; **no report** |
| `utf_baselib_httpclient5` (TLS) | 20 | 2 | green; **no report** |
| `utf_baselib_basictask`, the positive control | 3 | 7 | the known report, exit 66, every run |

**The control is what makes the empty cells mean something.** `utf_baselib_basictask` built in the
same instrumented tree produces the documented §4 baseline report at
`src/utests/utf_baselib_basictask/TestBaselibBasicTask.h:127` and exit 66 on every run, and no
`ThreadPoolImpl` access - exactly the post-`31f9463` delta the baseline record's §8 table predicts.
Each binary was also confirmed instrumented in its own right: run under `verbosity=1` it prints
`***** Running under ThreadSanitizer v3 *****` and reads the suppressions file.

## 2. The finding: `cancelTimers()` has two callers in two thread contexts

Every one of the 15 reports is the same defect, on the same pair of call sites, and Boost.Test
passed on every run which produced one.

    Http2ConnectionTaskT::cancelTimers( )            Http2ConnectionTask.h:1952, the cancel at :1980
      <- initiateClose( )                            :2392-2394     ON THE STRAND, NO TASK LOCK
      <- cancelTask( )                               :2404-2406     UNDER THE TASK LOCK, ANY THREAD

The two stacks TSan pairs, in full:

- **Under the task lock, on the request task's drain thread** - `HttpClientRequestTask.h:1593`
  `releaseStream( )` -> `ConnectionPool.h:2274` `runActions( )` -> `:1787` `task -> requestCancel( )`
  -> `TaskBase.h:1241` `BL_MUTEX_GUARD( m_lock )` -> `requestCancelInternal( )` (`:1050`) ->
  `cancelTask( )` -> `cancelTimers( )`. TSan names the task's own lock as `M0`.
- **On the strand, with no lock at all** - `Http2ConnectionTask.h:1608` `onWrite( )` -> the handler
  epilog `BL_TASKS_HANDLER_END_MULTIOP` -> `MultiOperationTask.h:384` `onOperationCompleted( )` ->
  `applyDecision( )` (`:181`) -> `initiateClose( )` -> `cancelTimers( )`. TSan names **no** mutex
  on this side.

The object is the drain deadline: a 120-byte `deadline_timer` heap block created by
`TcpStrandedStreams.h:169` `createTimer( )` from `armDrainDeadline( )`
(`Http2ConnectionTask.h:2021`), itself reached from `closeGracefully( )` (`:2099`). The racing byte
is asio's own `impl.might_have_pending_waits`, read at `deadline_timer_service.hpp:162` and written
at `:172` inside `deadline_timer_service::cancel`.

**Why the epilog holds nothing.** `BL_TASKS_HANDLER_BEGIN_CHK_EC( )` opens a block whose guard is
`BL_MUTEX_GUARD( bl::tasks::TaskBase::m_lock )` (`TaskBase.h:119-120`), and
`BL_TASKS_HANDLER_END_IMPL_EX` closes that block (`TaskBase.h:255`) **before** its
`if( __eptr42 ) ... else ...` (`:256`) calls `onOperationCompleted( )` (`:279-283`).
`applyDecision( )`'s own comment says the same thing from the other end: "Performs what was
decided under the accounting lock, **with the lock released**" (`MultiOperationTask.h:160`). So
the driver's close path takes no task lock, by design, and the task lock `cancelTask( )` holds
excludes nothing on the strand.

### Why it is real, not formal

`boost::asio::basic_deadline_timer` is documented *Distinct objects: Safe. **Shared objects:
Unsafe***. Two threads inside one timer's `cancel( )` is a use of a shared object, and the library's
own idiom everywhere else is to keep every touch of a timer on one executor.

The byte TSan caught bounds one concrete consequence, and it is not nil: `cancel( )` returns early
when `might_have_pending_waits` is false, so an update lost between the strand's arm and another
thread's cancel leaves a later `cancel( )` returning 0 **without cancelling**, and a timer which
should have been disarmed fires. In this driver the drain deadline's handler cancels the task and
the PING deadline's handler closes every stream, so the cost of a missed cancel is an abrupt end to
a connection which had already closed cleanly. The same `cancelTimers( )` touches five timers
(settings, keep-alive, PING deadline, idle, drain) and the arms - `expires_from_now( )` plus
`async_wait( )` - run under the task lock in handler bodies, so the arms are exposed to the
unlocked `initiateClose( )` in exactly the same way (**corrected in §7**: they are not - the arms
and `initiateClose( )` are both on the strand, so they are serialised by it; what the arms were
exposed to is the off-strand `cancelTask( )`); the reports all name the drain timer only
because that is the timer alive at the moment the pool cancels a draining connection.

### Which component owns it

The **HTTP/2 driver**, `src/include/baselib/http2/Http2ConnectionTask.h`. The pool and the
`MultiOperationTask` mix-in are both doing what they document: the pool cancels connections
(`forgetConnection( )` says cancelling is its only lever), and the mix-in calls `initiateClose( )`
with the accounting lock released, which is what a close path must do.

**The HTTP/1.1 driver is the counter-example, and it is correct.** Its `initiateClose( )` cancels
the idle timer with no task lock, on the strand (`Http1ConnectionTask.h:1285-1307`, and its comment
says "called once, with no task lock, for the sockets AND the timers"); its `cancelTask( )` touches
no timer at all - it posts to the stream executor (`:1375-1390`). One context, therefore safe. The
h2 driver's `cancelTask( )` adds a second context.

**This is what the L6 review's finding-3 verification passed over.** It recorded that the h2 driver
"relies on a different serialisation ... its `cancelTask( )` calls `cancelTimers( )` on the
cancelling thread, under the task lock `requestCancel` holds ... and its arm runs in handler bodies
which hold the same lock - so the two drivers are each correct for a reason the other does not
share". The first half is accurate; what it does not account for is the third caller,
`initiateClose( )`, which by the mix-in's design holds nothing. The L4 record reasoned about the
same drain-timer path as entirely on the strand (`http2-l4-review-record.md:468-484`) and had no
reason to look outside it.

**A direction, not a prescription** (unverified, and the owner's call): the h2 `cancelTask( )`
override may not need `cancelTimers( )` at all. `base_type::cancelTask( )` cancels the socket,
whose woken handlers reach `onOperationCompleted( )` -> `applyDecision( close )` ->
`initiateClose( )` -> `cancelTimers( )` on the strand, which is precisely the h1 arrangement. What
would have to be checked before removing it is whether a connection can be cancelled with no socket
operation in flight, so that nothing would wake to run the epilog.

### Where it shows, and where it cannot

The pair needs a **draining connection** and a **pool cancel of it**, so it needs the composition:
the two cases which reported are `ClientSession_GoAwayRetiresTheConnectionTests` and
`ClientSession_DrainingReserveReachesTheDriverTests`. The driver-level TSan runs lane 1 and lane 2
recorded clean (`utf_baselib_h2client2`, 4 runs; `utf_baselib_h2client4`, 3 runs) were not wrong -
nothing in a driver-level module calls `requestCancel( )` from another thread while the strand is
closing. `utf_baselib_httpclient5` cannot reach it either: its two cases neither drain nor cancel.

## 3. What was NOT found: the read the review predicted

The review expected `resolveDriver( )` reading `attempt.driver( )` under the pool lock to surface
first - L5 finding 7's unsynchronised reads, "on every ALPN fallback". **It did not, in the 32
runs which took it.**

The negative is not vacuous, because the path demonstrably ran. A cleartext connection takes
`NegotiatedProtocol::withoutAlpn( cleartextProtocol )` (`ClientConnectionTaskBase.h:275-281`), which
for the default `Http11` (`:134`) means the h2 task's `onProtocolNegotiated( )` falls through to the
base and **writes** `m_connection = m_driverFactory -> createDriver( ... )` on the strand (`:581`);
the pool then adopts that driver only by **reading** it through `attempt.driver( )` ->
`connection( )` (`:671`) in `resolveDriver( )` (`ConnectionPool.h:932`, called at `:1236`). A run of
`ClientSession_AgainstTheLibraryHttpServerTests` with `--bl-logging-level=6` logs `ALPN selected ''`
twice and passes, so the fallback was taken twice in that case alone - and the request could not
have completed had the read not returned the driver.

So the reads execute and are not reported. The likely reason - **inferred, not measured** - is that
the pool's read and the strand's write are in practice ordered through the execution queue's and
the io_context scheduler's own mutexes, which is real synchronisation that TSan honours even though
it is not the synchronisation the code documents. L5 finding 7's prescription (test `isReady`
first; read the driver only after the task reads `Closed`) is not refuted by this; it is unproven,
and it remains the cheaper way to be right than to depend on an incidental edge.

The other read of finding 7 - `effectiveMaxConnectionsPerKey( )` reading
`connection -> negotiated( ).protocol( )` before `entry -> isReady` (`ConnectionPool.h:962-978`) -
needs a second connection to be considered for a key while a first is still `Connecting`, which
`canStartAnother( )` (`:1461`) only asks under concurrency. **These runs never exercised it**: every
case in both modules is sequential. It is the concurrent-request case, item 2 of the same
not-shippable list, which would put that read under the tool.

## 4. What this round therefore establishes

- The session modules have now been under ThreadSanitizer, with a control which fired: item 3 of
  the second pass is discharged **as a run**, and it found something.
- One real defect, in the h2 driver, on the ordinary "peer went away while we were writing" route -
  12 runs in 20 of two cases, so it is cheap to reproduce and will be cheap to confirm fixed.
- The `resolveDriver( )` read is not observable here, and the reason to fix it is unchanged and
  is now argued rather than assumed.
- Nothing in `utf_baselib_httpclient5` (TLS, real ALPN) reported anything in 20 runs.

## 5. Reproducing it

    mv bld <somewhere outside the repo>          # instrumented objects must not mix
    make -k -j1 utf_baselib_httpclient4 TOOLCHAIN=clang2010 VARIANT=debug BL_CLANG_ENABLE_RA_TSAN=1
    export TSAN_OPTIONS=second_deadlock_stack=1:suppressions=$( pwd )/projects/make/toolchain/tsan-suppressions.txt
    ./bld/ub24-a64-clang2010-debug/utests/utf_baselib_httpclient4/utf-baselib-httpclient4 \
        --log_level=test_suite \
        --run_test=ClientSession_GoAwayRetiresTheConnectionTests,ClientSession_DrainingReserveReachesTheDriverTests

Expect exit **66** with one or two reports in roughly half the runs, and `*** No errors detected`
in all of them - the sanitizer's exit code arrives after Boost.Test has already passed, so a TSan
result must be read by grepping for `ThreadSanitizer`, never by the exit code alone.

## 6. Why nothing was fixed

The round was a validation lane, whose brief forbids changing source, and AGENTS.md gates a
modification of an existing core code path as its own tested change-set. `initiateClose( )` and
`cancelTask( )` are on the terminal path of every HTTP/2 connection, and the candidate fix in §2
removes a cancel rather than adding one, which is precisely the kind of change that needs the whole
suite behind it rather than a lane's focused module.

## 7. The fix - one context, by posting the one caller which had another

**Date:** 2026-09-20, `http2-lane-1`, one commit over
`src/include/baselib/http2/Http2ConnectionTask.h`: **nine lines added and one removed**; the rest
of the diff is the invariant, written down where the next reader will look for it. Every function
named below is named, not numbered - §2's line numbers are revision `285085f`'s and no longer
resolve in the working tree.

    virtual void cancelTask() OVERRIDE
    {
        if( base_type::isSocketCreated() )
        {
            base_type::postToStrand(
                cpp::bind( &this_type::cancelTimers, self_ref_t::acquireRef( this ) )
                );
        }

        base_type::cancelTask();
    }

The guard is `isSocketCreated( )` rather than h1's `! isChannelOpen( )`: both stranded policies
set `m_strand` and then create the socket in one call, so a created socket means `postToStrand( )`
has a strand to post to - and with no socket no timer was ever created either, since
`createTimer( )` is built on that same strand. It is also the predicate `initiateClose( )` right
above already uses. `cancelTimers( )` is bound directly, needing no `…OnStrand( )` wrapper: it is
already `NOEXCEPT` and already tests each pointer.

The direction §2 floated - dropping `cancelTimers( )` from `cancelTask( )` altogether, on the
argument that the socket cancel wakes handlers which reach `initiateClose( )` - was **not** taken.
Posting keeps the disarm and still gives it one context, and it does not depend on the question
§2 left open (whether a connection can be cancelled with no socket operation in flight, so that
nothing would wake to run the epilog).

**The price of posting, stated plainly.** The disarm now happens one strand turn later, and in that
window a deadline which has already expired is honoured IN FULL: `onDrainDeadline( )` and
`onPingDeadline( )` gate only on `! ec && ! isClosing( )`, and `isClosing( )` is still false then,
so a sink can be answered `timed_out` where it would otherwise have got the `operation_aborted` of
the cancel, and `onIdleDeadline( )` can still take `closeGracefully( )` and issue a GOAWAY write
which the shutdown then aborts. None of those outcomes is new: pre-fix the same ones were reachable
whenever an expiry was already queued ahead of the inline cancel, and whenever the racing cancel
was the one which got lost. What changes is their probability, not the set of them.

**The class comment's rule L1 needed correcting with this change**, and was: it stated that every
strand handler here also holds the task lock, which the posted handler does not. It is the one
exception, it touches only the five timer pointers, and every other reader and writer of those is
on the strand.

### `onTaskStoppedNothrow( )`'s `cancelTimers( )` stays, and stays a DIRECT call

**It is not redundant in the sense the first reading means.** "The task has stopped, so every
operation has completed" holds only for the mix-in's terminal `notifyReady( )`, which
`takeTerminalNoLock( )` gates on `0U == m_pendingOperations` - and every `async_wait( )` in this
driver is preceded by `beginOperation( )`, so there a pending count of zero really does mean no
timer is armed. That is not the only route in. `TcpBaseTasks.h onConnectionEstablished( )`,
`TcpSslBaseTasks.h onHandshakeCompleted( )` and - on a proxied cleartext connection -
`TcpTunnelStage.h onTunnelDataWritten( )` / `onTunnelDataRead( )` all end with plain
`BL_TASKS_HANDLER_END( )`, and `continueAfterConnected( )` -> `onProtocolNegotiated( )` runs
*inside* them; that function ends with `pumpWrites( )`, `scheduleRead( )`, `chkArmKeepAlive( )`,
`chkArmIdleTimer( )`. A throw from any of them completes the task with operations pending and a
timer armed.

**But it is not the only thing which would disarm them either, and the first version of this
section wrongly said it was.** The arms come after `pumpWrites( )` and `scheduleRead( )`, so on
that route there is always a read in flight - and the same `onTaskStoppedNothrow( )` chain
continues into the stream policy's own `onTaskStoppedNothrow( )`, which (the task sets
`isCloseStreamOnTaskFinish( true )` in its constructor) calls `shutdownSocket( )`:
`shutdown_both` plus `cancel( )`. The read then wakes - `operation_aborted`, or `eof` if the peer
closed first - and either outcome reaches `cancelTimers( )` on the strand, the first through
`onOperationCompleted( )` -> `initiateClose( )` and the second through `onPeerClosed( )`. Without
the direct call the timers would still be disarmed, a wake later.

**So the honest reason to keep it is that it is a disarm which needs no wake, it is safe, and it
costs nothing** - not that it is the only one, and not that it is always the first. It is first on
the cleartext route only: under the TLS policy `notifyReadyImpl( )` returns at
`scheduleTaskFinishContinuation( )` **before** it reaches `onTaskStoppedNothrow( )` at all
(`TaskBase.h`, the continuation is consulted first), and
`TcpSslSocketAsyncBase::scheduleTaskFinishContinuation( )` starts the SSL shutdown - so this call
does not run until a later `notifyReady( )` gets past the continuation. **Which one is not
guaranteed**: `beginProtocolShutdown( )` sets `m_scheduledForShutdown`, from which point the
continuation returns false for every later call, and the SSL shutdown sits OUTSIDE the
multi-operation accounting - so it is `onShutdownCompleted( )`'s `notifyReady( )`, or the mix-in's
terminal one if the woken read, the write and the cancelled timers all drain the accounting first
and `onShutdownCompleted( )` then finds `m_notifyCalled`. Either way a `close_notify` which woke
the pending read may already have disarmed through `onPeerClosed( )` by then.
Posting it, on the other hand, really is wrong, for the reason a termination path always makes it
wrong: a handler posted there may never run, and then the disarm is lost rather than delayed.

Safe, because `onTaskStoppedNothrow( )` is invoked from `notifyReadyImpl( )` **under the task
lock** - the declaration of the virtual in `TaskBase.h` says so - so it excludes every handler
body, which is every arm and both of the other strand callers. It excludes `cancelTask( )` too:
that is only ever reached from `requestCancelInternal( )`, and all five of its call sites in this
chain (`TaskBase.h requestCancel( )`, the two `BL_TASKS_HANDLER_BEGIN` bodies here, the connect
deadline and the TLS protocol timer) hold that lock. What is left holds **no** lock at all, and it
is two things, not one: `initiateClose( )`, and **the handler `cancelTask( )` now posts** - which
touches all five timers holding nothing. Both run on the strand, and so does every `notifyReady( )`
which can reach `onTaskStoppedNothrow( )` with a timer armed, so the exclusion between them and
this direct call is the STRAND, not the task lock. The one off-strand route is
`scheduleNothrow( )`'s catch, which posts `notifyReadyImpl( )` to `getThreadPool( eq )` - the
execution queue's local pool if it has one, otherwise `ThreadPoolDefault::getDefault( )` for this
task's `getThreadPoolId( )`, which both stream policies answer `NonBlocking`, i.e. the I/O pool and
not the general purpose one. It is reachable only before the handshake, where all five timer
members are still null.

### The arm side is closed too, and §2 overstated the exposure

After the change every touch of every timer - arm and cancel - is on the strand. That was already
true of the arms against `initiateClose( )`, which is what §2 gets wrong: the arms run in strand
handler bodies and `initiateClose( )` runs in a strand handler's epilog, so the strand serialises
them and they could never have raced. The task lock the arms hold is not what protects them there.
What the arms *were* exposed to is the same off-strand `cancelTask( )` the reports named, and that
is what this change removes. The evidence agrees: in the fifteen reports of §2 and the nine of the
run below, `armDrainDeadline( )` appears only in the *Location is heap block … allocated by*
section - as the allocator of the racing timer, never as a racing access.

### The mechanism, confirmed - and a worse hazard the same fix closes

Both read in the Boost 1.90 the build uses, `boost/asio/detail/deadline_timer_service.hpp`:

- **The byte is what §2 says it is.** `cancel( )` returns 0 early when
  `! impl.might_have_pending_waits` (`:160-166`), and `async_wait( )` is what sets that flag
  (`:276`). An off-strand cancel which reads a stale `false` therefore cancels nothing and reports
  success, which is precisely "a timer which should have been disarmed fires".
- **And a re-arm DESTROYS the timer object.** `m_idleTimer = base_type::createTimer( )`, and the
  same assignment in the settings, keepalive and PING-deadline arms, move-assigns a
  `cpp::SafeUniquePtr`; its `operator =` calls `reset( )` (`core/CPP.h`), which destroys the
  `deadline_timer` it held, and *that object's* destructor is what runs
  `deadline_timer_service::destroy( )` -> `cancel( )` (`:102-106`). `armDrainDeadline( )` is the
  one arm which never reassigns, because it returns early when its timer already exists - which is
  not the same as being the only guarded arm: `chkArmSettingsTimer( )` has an armed guard of its
  own, the `m_isSettingsTimerArmed` flag. **The hazard needs no claim about a pending wait.**
  Pre-fix the strand could be destroying the object - having already overwritten the pointer the
  other thread had just dereferenced - while the pool's thread was inside `cancel( )` on it. That
  is a use-after-free, worse than the lost update the reports show, reached by the same two
  contexts and closed by the same fix, since only one context is left which touches them.

The clean post-fix runs are also not a suppression artifact:
`projects/make/toolchain/tsan-suppressions.txt` holds exactly one entry, the unrelated
`AsyncExecutor` lock-order inversion, and nothing which could mask a data race in this driver.

### Validation

Same recipe as §5, same host, instrumented tree kept outside `bld/` and swapped by hand.

| binary | runs | result |
|---|---:|---|
| `utf_baselib_httpclient4`, the two draining cases, **pre-fix** | 20 | 20 green; **8 runs reported, 9 reports** |
| `utf_baselib_httpclient4`, the two draining cases, **post-fix** | 20 | 20 green; **no report** |
| `utf_baselib_httpclient4`, whole module (17 cases), post-fix | 3 | green; **no report** |
| `utf_baselib_basictask`, the control, before and after the runs | 2 | the known `TestBaselibBasicTask.h:127` report, exit 66, both times |

All nine pre-fix reports are the same pair, checked by extracting the two ACCESS stacks of each
and tallying them: nine `cancelTask( )` against nine `initiateClose( )`. The control was run at
both ends of the session, so the clean cells are not an uninstrumented tree.

The seven modules which compile this header were built and run, clang2010 debug, one at a time:
`utf_baselib_h2client2`, `h2client3`, `h2client4`, `h2client5`, `utf_baselib_httpclient4`,
`httpclient5`, `httpclient6`. The release pass then closed **14 of 14** - those same seven under
`clang2010` **and** `gcc1520` release, `rc=0`, no leaks, 13 / 3 / 16 / 1 / 17 / 2 / 1 cases per
toolchain.

**The whole-suite gate AGENTS.md asks of a core code change is closed, and the strongest thing
about it is not that it passed.** G1, `gcc1520` debug, 42 modules, 1010 cases run of 1021
registered, against baseline `1bcde00` - and its report is **byte for byte the L6 gate's report**,
`md5 a05f1b3dfc57e17a58d75092c916bd3a`, zero diff lines. Both gates ran against the same baseline,
so differencing the two differentials isolates exactly this change-set, and that difference is
empty: **this change moved nothing in the suite.** That is a stronger statement than a pass, which
would only have said that nothing it noticed was broken.

The 519 differences the report does list against `1bcde00` are inherited from the L0-L6 feature
work and stand identically in both reports: 259 `REGISTRATION ADDED`, 259 `NEWLY RUNS`, and one
`ASSERTION COUNT CHANGED` - `TlsHandshake_SniOmittedForAddressLiterals`, 14 -> 21, which is S0.3's
deliberate IPv6-literal extension in `d7f5ef0` and is already recorded in the plan. No pre-existing
case changed outcome, disappeared, or moved its assertion count because of this change-set.
