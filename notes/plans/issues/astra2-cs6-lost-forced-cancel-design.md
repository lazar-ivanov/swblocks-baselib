# CS-6 / D-L3-1 — a forced cancel lost between two steps of a TLS operation: design note

**Date:** 2026-09-28. **Status:** revision 2, written by lane 3, **for review round 2**. Nothing is coded
before its agreement line.

**Revisions.** r1 `8cdb529`. **r2 (this)** carries:
- the maintainer's shape (decision record §11, `1b0aac3`): (c), plus (a2) on the four deadlines which
  survive a cancel, plus the retry guard;
- review round 1 (`CS6-lostcancel-design-r1.md`, agree with changes): F1 and F3 to F14, each checked at
  the source before it was taken, with P1 and P3 to P10;
- the maintainer's answers to its three decisions, all as recommended:
  - D1, the complete known limit, in P2's words (§5);
  - D2, a deadline for SimpleHttpTask's handshake, folded into CS-6 (§8, new);
  - D3, a ThreadSanitizer characterization of the ranged connect against a plain policy's cancel (§9, new),
    and then its fix, our own per-endpoint connect loop, which the maintainer chose from its report
    (§9);
- from CS-4's review round 2 (R2-F4), relayed by the orchestrator: where the connect deadline's re-issue
  goes in CS-4's final `onConnectDeadline( )`, and what its red asserts (§4(a), §7).

F4's two corrections to project records are the orchestrator's, and are not in this note. The design of
§8 and of §9's fix is new in this revision, and is for this review round.

**What this is.** Decision D-L3-1, found by lane 3 while characterizing I13 on 2026-09-27, and folded into
CS-6 by the maintainer on 2026-09-28 ("fold now, design note first"). A forced cancel can reap nothing,
and no later cancel, deadline or timer can then end the task: it ends only when the peer acts. This note
gives the mechanism at the source, the deterministic reproductions, the candidate shapes with what was
measured of each, and the shape the maintainer decided.

**Provenance.** Source line numbers are `ea7e414`'s; at the branch tip `TcpBaseTasks.h` is one line longer
from `:305` on (I13's fix), and lazari2 — CS-4 and CS-5 — was merged in at `18b8fcd`, so CS-4's
`onConnectDeadline( )` is cited as merged. Boost is the devenv7 dist's 1.90.0 and OpenSSL its 3.5.4. The measurements
were made on a64 clang debug with a scratch test case and scratch prototypes, **none of them committed**.
Every run, the case and both prototype diffs are in the run's state directory under
`logs/astra2/cs6/lostcancel/`, indexed in `INDEX.txt`. Each claim is labelled **VERIFIED** (read at the
source cited, or measured where it says so), **INFERRED** (follows from verified facts, not checked) or
**NOT VERIFIED**.

---

## 1. The defect, at the source

1. **The forced cancel shuts the send side down and cancels.** `TcpSocketCommonBase::shutdownSocket( )`
   is, with or without force, `shutdown_send` then `cancel( )` (`TcpBaseTasks.h:241-368`); at the branch
   tip force changes nothing (I13, `1c828b2`). The plain TLS policy
   calls it from whichever thread cancels (`TcpSslBaseTasks.h:391-399`); the stranded one posts it to the
   stream's strand (`TcpSslStrandedStreams.h:116-126`, `:207-234`). **VERIFIED.**
2. **`cancel( )` reaps only what is registered at that instant.** On epoll it completes the operations
   queued on the descriptor (`epoll_reactor::cancel_ops( )`, `detail/impl/epoll_reactor.ipp:350-372`); on
   Windows it is `CancelIoEx( )` over the handle's outstanding I/O
   (`detail/impl/win_iocp_socket_service_base.ipp:236-262`). **VERIFIED.**
3. **A TLS operation is a chain of socket operations inside asio.** `ssl::stream`'s handshake, read, write
   and shutdown are each an `io_op` which runs the engine and then starts the next socket read or write
   from the previous one's completion. Between a step's completion and the next step's start, nothing of
   that operation is registered. A step which completes at once, as a small speculative write does, has
   its completion **posted to the scheduler**, not run inline
   (`detail/reactive_socket_service_base.hpp:613-631` → `epoll_reactor::call_post_immediate_completion( )`,
   `epoll_reactor.ipp:251-256`). A cancel which lands in that gap reaps nothing. **VERIFIED.**
4. **The step after the gap then decides the outcome.** A write step fails at once, because the send side
   is shut down: §3's control measures it. A read step registers on a socket whose **receive side is still
   open**, deliberately so — shutting it down would reset the connection on the peer's next arrival, on
   Windows at once and on Linux once our FIN is out (§4(c)), and throw away data we had delivered
   (`TcpBaseTasks.h:323-359`) — and it waits for the peer. **VERIFIED.**
5. **Nothing else can end that wait.** `requestCancelInternal( )` returns at once once a cancel has been
   requested (`TaskBase.h:1028-1031`). Every deadline in the library which exists to bound a silent peer
   cancels through it:
   - the TLS protocol timer, which bounds a handshake and a TLS shutdown (`TcpSslBaseTasks.h:136-156`; its
     purpose is stated at `:86-90`);
   - the HTTP establisher's connect deadline (`ClientConnectionTaskBase.h:478-500`);
   - the HTTP/2 driver's ping and drain deadlines (`Http2ConnectionTask.h:2181-2219`, `:2473-2502`);
   - the HTTP server tasks' timers (`HttpServer.h:119-134`, `:345-360`);
   - SimpleHttpTask's request timer (`SimpleHttpTask.h:325-358`).

   So after a lost cancel the task ends only when the peer sends, closes or resets. **VERIFIED** (the
   paths); **measured** for the protocol timer (§3).

   Three of these deadlines — the HTTP/2 driver's two and SimpleHttpTask's — are disarmed by the cancel
   which was lost, so after one they do not fire at all (§4(a)). Their tasks' own `cancelTask( )`
   disarms them: SimpleHttpTask's calls `cancelTimer( )` before the base's (`SimpleHttpTask.h:266-271`),
   and its `onTimer( )` returns on `operation_aborted` (`:327-330`); the HTTP/2 driver's posts
   `cancelTimers( )` (`Http2ConnectionTask.h:2910-2923`), which cancels both (`:2366-2398`), and both
   handlers act only on `! ec && ! isClosing( )` (`:2185`, `:2477`). Nothing re-arms them. The other four
   are disarmed only in their task's `onTaskStoppedNothrow( )` or on a re-arm (`TcpSslBaseTasks.h:535`;
   `ClientConnectionTaskBase.h:644`; `HttpServer.h:148-151`, `:374-377`). **VERIFIED.**

## 2. Where a cancel can be lost

| | Window | Can it hang? |
|---|---|---|
| **W1** | a TLS handshake's steps — the client role (reproduced) and the server role (**INFERRED**), both TLS policies | yes: a peer which accepted TCP and then says nothing (a wedged server, a proxy which answers nothing) |
| **W2** | a TLS shutdown — our close_notify written, the peer's awaited | yes: the peer the protocol timer exists for, one which never answers |
| **W3** | a TLS read which needs another socket read — a record arriving in pieces, or a non-application record (a TLS 1.3 NewSessionTicket or KeyUpdate) consumed with no application data behind it — or a TLS write which must read first (a renegotiation) | only if the peer then sends nothing and does not close in answer to our FIN |
| **W4** | `asio::async_read_until( )`'s steps (`SimpleHttpTask.h:663`, `:710`), over cleartext as well as TLS | only if the peer stops mid-head and does not close in answer to our FIN |

W3's next socket read waits for the peer's next bytes, whatever they are: `io_op` feeds the engine what
arrived and retries (`ssl/detail/io.hpp:264-282`), and an engine which consumed a non-application record
and has no application data asks for input again. The lane's own TLS peer turns session tickets off for
exactly that reason (`utests/baselib/TlsEndingPeer.h`). Every window also needs the peer to leave our FIN
unanswered, since the FIN goes out even when the cancel reaps nothing. **VERIFIED.**

**Not windows** (**VERIFIED**):
- a cleartext `async_read_some( )`, which is registered from its start to its completion;
- `asio::async_read( )` and `async_write( )` loops given `untilCanceled( )`, **over a cleartext stream**.
  The condition stops the loop at the next step once the task is cancelled (`TcpBaseTasks.h:71-109`), and
  asio consults it only between its own steps (`impl/read.hpp:393`, `:405`).
  - The tunnel stage reads and writes the cleartext lowest layer (`TcpTunnelStage.h:1695-1698`), so a
    proxy tunnel is not a window.
  - **Over TLS, each step of such a loop is itself an `ssl::stream` read, whose own socket reads are
    W3** (`impl/read.hpp:398`, `ssl/detail/io.hpp:179-181`). So the block transfer client and server over
    `TcpSslSocketAsyncBase` (`MessagingClientFactory.h:366`, `TcpBlockServerDataChunkStorage.h:36`,
    `TcpBlockServerMessageDispatcher.h:41`, `:55`) are W3 consumers (`TcpBlockTransferServer.h:468`,
    `:1311`; `TcpBlockTransferClient.h:501`, `:703`). Their writes are safe after the forced shutdown, as
    the next bullet says.
- any write step started after the forced shutdown.
- **Not a window but a delay:** `asio::async_connect( )` over the resolved endpoints
  (`TcpBaseTasks.h:1455`).
  - A connect step which the cancel reaps completes `operation_aborted` on a socket still open, so the
    loop moves to the next endpoint (`impl/connect.hpp:490-510`), closing and re-opening the socket for
    it (`:465-476`), which also drops the shutdown.
  - The next attempt runs to completion or to the operating system's timeout, and
    `onConnectionEstablished( )` then ends the task (`TcpBaseTasks.h:1404`).
  - On the plain policies the loop's close and re-open run on an I/O thread with no lock of ours while a
    cancel reads the same socket object: a race of I13's class, characterized in §9 (D3).

## 3. Deterministic reproductions — measured, today's code

A scratch case in `utf_baselib_tasks3` (`lostcancel/ScratchLostCancel2.h.evidence`). A connector probe
connects either to a listener which never accepts (W1), or to a TLS peer which completes the handshake,
reads our close_notify, and then holds its own answer with its socket open (W2). The probe records its
stop. The case waits 5 s, then makes the peer act (the listener closes, or the peer answers), so a lost
cancel fails the bound instead of hanging the run.

| Window, policy | How the order is made certain | Today (3 runs each) |
|---|---|---|
| W1, stranded TLS | the cancel is requested in the connect handler, on the strand, before the handshake starts: the forced shutdown is posted to the strand first, and the ClientHello's completion reaches the strand only through the scheduler, behind it | **0 of 3** ended within 5 s; each ended at 5 s, on the listener's reset |
| W1, plain TLS, one I/O thread | the cancel is requested inline right after the handshake starts: with `-- --threads-count 1` the I/O pool has one thread (`AppInitDone.h:186-189`), so the ClientHello's completion cannot run before the handler returns | **0 of 3** |
| W2, plain TLS, one I/O thread, protocol timer 2 s | the cancel is requested inline right after the TLS shutdown starts | **0 of 3**; the timer fired at 2 s and nothing followed it: no second `cancelTask( )`. Each ended at 5 s, when the peer answered |
| control: plain TLS, one I/O thread, cancel before the handshake starts | the forced shutdown precedes the ClientHello's write | **3 of 3** ended within milliseconds: the write failed at once |

The windows are certain rather than probable because the order is fixed by the posting order, or by a
single thread. In production the order is a race: the plain policy's cancelling thread against the pool
thread running a step's completion, and on the stranded policy any cancel which reaches the strand while
a step's completion is still in the scheduler. The first sighting was one hang in 14 runs of an early
characterization (`logs/astra2/cs6/hang1/`). A stranded cancel requested just after the handshake
starts hung 2 runs of 3 (`logs/astra2/cs6/scratch-lostcancel-run2.log` hung, `-run3.log` ended,
`scratch4-lostcancel-run.log` hung). Each window is microseconds wide. It matters because what follows a hit is a
wait no deadline can end.

## 4. The candidate shapes

**(a) Deadlines re-issue the socket cancel.** When a deadline which its own cancel leaves armed fires on a
task which was already cancelled and is still running, it calls `cancelTask( )` again instead of
`requestCancelInternal( )`. That reaps the read which has registered since. Three scopes:
- **(a1)** the protocol timer alone, which closes W2;
- **(a2)** the four deadlines which can fire after a cancel — the TLS protocol timer, the HTTP connect
  deadline, and the HTTP server's idle and response timers — through one additive `TaskBase` helper. The
  HTTP/2 PING and drain deadlines and SimpleHttpTask's request timer are excluded: their own
  `cancelTask( )` disarms them (§1.5), so after a lost cancel they fire only if their expiry was already
  queued when it landed — `deadline_timer::cancel( )` cannot recall a queued expiry. **Decided by the
  maintainer on 2026-09-28** (decision record §11). Each of the four is enabled by default, at 60 s,
  wherever it is armed (`TcpSslBaseTasks.h:73`, `ClientConnectionTaskBase.h:77`, `HttpServer.h:711`);
- **(a3)** make `requestCancelInternal( )` itself re-issue. Rejected: fourteen `cancelTask( )` overrides in
  twelve files, several of them not stream tasks, would all be called twice (`TaskBase.h:1069` declares
  it). More to the point, a repeated `requestCancel( )` is no evidence of a lost cancel, and code in the
  tree relies on `cancelTask( )` being called at most once per run, and says so (`TaskBase.h:1841-1851`,
  `:1878-1880`, `TcpBaseTasks.h:806-810`). (a2) re-issues only when a deadline fires on a task which is
  running and already cancelled — a state which does nothing today.

The helper applies `requestCancelInternal( )`'s own rule and calls `cancelTask( )` only for a running task
(`TaskBase.h:1035-1042`). A stream policy's `cancelTask( )` is idempotent at the socket level, since it shuts
down and cancels again: a second `shutdown_send` is a no-op or fails `not_connected`, which
`isExpectedSocketException( )` admits without logging (`TcpBaseTasks.h:188-200`); a second `cancel( )` finds
nothing registered; the stranded policy posts one more `shutdownSocketOnStrand( )`. A deadline which its own
cancel leaves armed fires on a cancelled task which is still running when the cancel was lost. It can also
fire in the moment after a cancel which worked, before that cancel's aborted completions have run; a second
shutdown and cancel are harmless there. So (a) changes nothing else. **INFERRED.** The hang is bounded by
the deadline, 60 s by default, rather than ended promptly. It needs no platform-specific behaviour.

**The connect deadline, in CS-4's final `onConnectDeadline( )`** (`4ef25a7`, with `ead7600`'s comments,
merged into this branch at `18b8fcd`). That function reads `isDeadlineCancel = ! isCanceled( )` under the
task lock before it requests the cancel, and gives its TimeoutException reason only when it is the one
cancelling. (a2)'s re-issue goes in the `! isDeadlineCancel` branch and **gives no reason there**: the cancel
it re-issues is someone else's. CS-4's review round 2 (R2-F4) found a second route into that branch: a
deadline already due while another cancel is still on its way to the strand, which
`cancelConnectDeadline( )` cannot recall. There the re-issue is a second `cancelTask( )` on a cancel which is
about to work, and harmless for the reasons above.

**Measured**, as (a1) on top of (c): W2 ended at **2003 ms**, the timer's fire, in **3 of 3**. It was 0 of 3
before. (`prototype-c-plus-a.diff`, `protoCA-plain-shutdown-t1.summary`.)

**(b) A protocol timer for every handshake.** The connector's handshake arms no timer: it is armed only by
the finish continuation and by a directly scheduled handshake or shutdown (`TcpSslBaseTasks.h:359`, `:377`,
`:512`). With (a), (b) would bound W1 everywhere. It changes behaviour for any TLS client whose
handshake legitimately takes longer than the timeout. It is **not needed with (c)**.

**(c) Shut the receive side too while the task's own TLS handshake is incomplete.** Before any application
data has passed, a receive shutdown loses nothing of value. A peer which sends after it is reset:
- on Linux, once our FIN is out — and the forced path sends it at once. **Measured**, Linux 6.8: after
  `SHUT_RD` then `SHUT_WR` the peer's next send fails `EPIPE`, our `SO_ERROR` becomes `ECONNRESET`, and
  `TCPAbortOnData` counts one; after `SHUT_WR` alone, or `SHUT_RD` alone, there is no reset
  (`logs/astra2/cs6/lostcancel/appendixA-reset-probe.txt`, the review's Appendix A re-run by the lane);
- on Windows, in every case, including data already queued at `SD_RECEIVE` (`TcpBaseTasks.h:329-331`).

What either side can lose to that reset is the handshake records of a handshake we are abandoning. Once
the receive side is shut down, a read registered at the cancel wakes, and any read started after it ends at
once. The next handshake step fails — with a truncation, or with `connection_reset` if the peer's answer
arrived first, which is not retryable (`NetUtils.h:358-361`) — and the task ends as a cancel. It escapes the
trap I2 fell into: the receive shutdown raises a fresh `EPOLLIN` edge at the cancel
(`hang1/shut-rd-probe.txt`), which either performs the read already queued or re-enables speculation for
the next one (`epoll_reactor.ipp:279-303`, `:783-813`) — so the read ends either way. **INFERRED**; the
prototype's 1–2 ms endings are consistent with it. It adds no write of I13's class: `socket_ops::shutdown( )`
only reads the descriptor (`detail/impl/socket_ops.ipp:519-530`).
- **Measured on Linux 6.8** with a raw socket: after `shutdown( SHUT_RD )` a registered edge-triggered
  read gets an event at once, and a later read returns end of stream at once. After `SHUT_WR` alone,
  neither happens (`logs/astra2/cs6/hang1/shut-rd-probe.txt`).
- **Measured in the library** (`prototype-c.diff`): W1 stranded **3 of 3** and W1 plain **3 of 3** ended within
  **1–2 ms**, where today it is 0 of 3 each. W2 still hung 3 of 3, as designed.
- **What it must not touch: a connection's application phase.**
  - For a stream attached after its handshake the flag below is never set. That covers the HTTP/1.1
    driver, the HTTP server's tasks, and the block transfer server's connections. The HTTP/1.1 driver's
    own cancel path does not reach the policy at all (`Http1ConnectionTask.h:2305-2335`).
  - **The HTTP/2 driver is not attached.** It runs its own resolve, connect and handshake
    (`Http2ConnectionTask.h:215-216`, `ClientConnectionTaskBase.h:570-575`), and its cancel reaches the
    policy (`Http2ConnectionTask.h:2922` → `TcpBaseTasks.h:770-778` → `TcpSslStrandedStreams.h:207-234`).
    What protects its application phase is the flag's clear, and nothing else. A receive shutdown there
    would make its own read report end of stream, which the driver takes as the peer's close.

  So the condition is "a handshake this task is running itself, on the stream it now holds". It must be
  read without a race:
  - the plain policy's `cancelTask( )` runs under the task lock — its only caller is
    `requestCancelInternal( )` (`TaskBase.h:1054`), and every caller of that holds the lock — and so does
    the stranded policy's when it has no strand of its own (`TcpSslStrandedStreams.h:209-219`);
  - the stranded policy's `shutdownSocketOnStrand( )` runs on the strand, without the lock.

  The flag is therefore written only under the task lock and, for a stranded policy which built its own
  stream, on its strand as well — "under that lock, or on that strand" would admit a write on the strand
  alone, which the no-strand fallback's reader would race. Both hold at the sites:
  - it is set in `beginProtocolHandshake( )`, which is reached only from a handler body — the connector's
    connect handler (`TcpBaseTasks.h:1400-1418`) or a pre-handshake stage's own handler, which the contract
    at `:1372-1376` puts inside the handler macros — or from `scheduleTask( )` under `scheduleNothrow( )`'s
    lock (`TcpSslBaseTasks.h:357-361`, `TaskBase.h:1167`, `:1208`); for a stranded policy those handlers are
    dispatched through the socket's executor, the strand (`TcpSslStrandedStreams.h:138-156`), and `io_op`
    calls the final handler directly (`ssl/detail/io.hpp:309-311`);
  - it is cleared in `onHandshakeCompleted( )`'s body **before `continueCallback( )`**
    (`TcpSslBaseTasks.h:576-580`), and in `resetStreamState( )`, `attachStream( )` and `detachStream( )`, so
    that "this task's own handshake, on the stream it now holds" stays literal. A clear placed later — in
    `onTaskStoppedNothrow( )`, say — would shut an HTTP/2 connection's receive side on every external cancel.

  A handshake which fails, or is cancelled, leaves the flag set — harmlessly, because the handler macros
  break out before the body (`TaskBase.h:165-168`) and that stream never carries application data.

  (c) goes into `TcpSslSocketAsyncBase::cancelTask( )` and `shutdownSocketOnStrand( )`. It never goes into
  the static `shutdownSocket( )`, which both drivers' own teardown calls (`Http1ConnectionTask.h:2161`,
  `:2328`; `Http2ConnectionTask.h:2879`). The prototype also read the wrapper's
  `hasHandshakeCompletedSuccessfully( )`. That flag is written outside the lock
  (`AsioSslStreamWrapper.h:312`), so the implementation must not read it.
- **The retry guard, needed with or without (c).** A cancelled handshake can fail with a truncation —
  through (c), or today through a lost cancel followed by the peer's orderly close, which the engine maps to
  `stream_truncated` (`ssl/detail/impl/engine.ipp:266-269`). That failure meets the connector's retry path,
  which counts a truncation as retryable and finds the channel open (`TcpBaseTasks.h:1469-1494`,
  `TcpSslBaseTasks.h:329`). The path restarts establishment, and the restart ends only at `onResolved( )`'s
  cancel check (`TcpBaseTasks.h:813-816`), after a new resolve. The retry condition needs
  `! base_type::isCanceled( )`, read under the task lock (`TaskBase.h:564-570`); a cancel which arrives
  after the retry decision is caught by the restart's own checks (the resolver cancel, `TcpBaseTasks.h:772-775`;
  `onResolved( )`, `:813-816`; `onConnectionEstablished( )`, `:1404`). **VERIFIED** by the prototype's trace
  with (c); **INFERRED** for today's code.

**(d) Close the socket on the strand after the forced shutdown.** This would end W1–W4 on the stranded
policies: every later step would fail on a closed descriptor. It is **unsafe on the plain policies**,
because closing races a step starting on another pool thread and frees the reactor's per-descriptor data
under it. It also breaks two things that hold today:
- the contract that the forced path never closes (`TcpBaseTasks.h:241-330`, and
  `TestTcpPreHandshakeStage.h:99-101` in `utf_baselib_tasks2`);
- every `isChannelOpen( )` decision taken after a cancel.

Not measured, because the plain-policy objection is decisive on its own.

**(e) A re-cancel watchdog.** After a forced cancel, re-issue `cancel( )` on a short period until the task
stops, on the strand for a stranded policy. It covers W1–W4 on every policy, bounded by the period. It adds
a timer to every forced cancel of every stream task, and the HTTP/1.1 driver's own cancel path
(`Http1ConnectionTask.h:2305-2335`) would need it too. Not measured.

**(f) Per-operation cancellation.** asio 1.90's `ssl::stream` operation records a cancellation requested
through a bound cancellation slot: after each socket step it ends with `operation_aborted` instead of
starting the next (`ssl/detail/io.hpp:102`, `:274-279`, `:289-294`). The ranged connect does the same
(`impl/connect.hpp:504-508`). A signal emitted by the forced cancel would therefore be seen even between two
steps. Its limits:
- it is sound only where the emit cannot run concurrently with the operation — the stranded paths — so it
  cannot serve the plain policies;
- it needs one signal per outstanding operation;
- it needs a slot bound at every asynchronous call site on those streams.

Not chosen. It is the alternative to (e) for the stranded consumers' part of the known limit (§5). Not
measured.

## 5. The decided shape — (c), (a2) on four deadlines, and the retry guard

**Decided by the maintainer on 2026-09-28** (decision record §11, `1b0aac3`): revision 1's recommendation,
with (a2) limited to the deadlines which survive a cancel.
- **(c)** closes W1 promptly and deterministically, measured on both TLS policies. W1 is where a silent
  peer is plausible and nothing is at stake.
- **(a2)**, on four deadlines, makes each deadline which survives a cancel hold again after a lost one:
  - it closes W2, measured through the protocol timer — every TLS shutdown runs under that timer;
  - it backs (c) up on W1 wherever the connect deadline or the protocol timer is armed;
  - it bounds W3 in the HTTP server's two tasks.

  The change is one additive `TaskBase` helper and four handlers. Each re-issues only on a task already
  cancelled and still running, and it applies `requestCancelInternal( )`'s rule of calling `cancelTask( )`
  only for a running task (§4(a)).
- **The retry guard** keeps a cancelled establishment from being restarted, with (c) or without it
  (§4(c)).

**Not chosen:**
- (a1) alone leaves the other surviving deadlines defeatable;
- (b) is not needed once W1 is closed by (c);
- (d) is unsafe on the plain policies;
- (e) and (f) are what would close the known limit below; neither was chosen.

**What stays open after it — the known limit of the shape, accepted by the maintainer on 2026-09-28 (D1).**
After a cancel lost in the gap of a TLS read, or of a cleartext `async_read_until( )`, the task ends only
when the peer sends or closes. A peer which then sends nothing, and does not close in answer to our FIN,
holds it. This applies on every consumer whose own cancel disarms its deadline, or which has none:
- the HTTP/2 driver, whose connect deadline is disarmed once the preface is away
  (`Http2ConnectionTask.h:1915`);
- the HTTP/1.1 driver, while it reads a response, since its idle timer is disarmed while a request is in
  flight (`Http1ConnectionTask.h:623-627`) and armed only when no stream is active (`:2020-2028`);
- SimpleHttpTask, over TLS (`SimpleHttpTask.h:801`, `:883`) — and, for the status line and the headers,
  over cleartext too (`:663`, `:710`);
- the block transfer client and server over TLS, whose `untilCanceled( )` loops stop only between their
  own steps (§2).

The gap opens mid-record, and also at a record boundary after a non-application record such as a TLS 1.3
NewSessionTicket.

A deliberate close in either driver meets the same gap. There `initiateClose( )` cancels without shutting
the send side when no write is in flight (`Http1ConnectionTask.h:2155-2167`,
`Http2ConnectionTask.h:2873-2885`), so the peer is not even sent a FIN. The HTTP/1.1 driver's A1-tls face 1
(`Http1ConnectionTask.h:1411-1431`) assumes "the ending our own shutdown_send provoked", which is true only
on the write-in-flight arm.

A re-cancel watchdog, (e), closes all of it. Per-operation cancellation, (f), would close the stranded
consumers' part. Every item needs the same coincidence — a cancel landing in a gap microseconds wide — and
then a peer which sends nothing and ignores our FIN; the deliberate close needs only the silence.

**Reversing conditions.**
- If a stop path which must finish is shown to wait on one of the consumers above, bound that consumer:
  with (f), for the drivers; with a timer which survives its own cancel, for SimpleHttpTask; with (e), for
  the plain-policy TLS connections.
- If the Windows measurement below shows that a receive shutdown does not end a later receive at once, (c)
  becomes (b) there, and W1 is bounded by the timer instead.

## 6. What Windows must measure

This is the Windows matrix owed for CS-6.
1. **The gap on IOCP.** The reproductions — W1 stranded (the posting order), W1 plain (held threads, or one
   I/O thread) and W2 stranded (the posting order) — each today and after. The gap is **INFERRED** on IOCP
   (`CancelIoEx( )` reaps only outstanding I/O); it is not measured there.
2. **(c)'s premise.** After `shutdown( SD_RECEIVE )`, does a `WSARecv( )` which is started afterwards
   complete at once, and with which code (the documented behaviour suggests `WSAESHUTDOWN` — **NOT
   VERIFIED**)? Does one already outstanding at `SD_RECEIVE` complete? The Linux answer is §4(c)'s.
3. **(c)'s effect on the peer.** A peer which sends after our receive shutdown is reset. This is measured on
   Linux, and owed on Windows, where data already queued at `SD_RECEIVE` is reset too. During a handshake,
   that only fails the handshake a second way. Record which code the handshake fails with — end of stream (a
   truncation), `WSAESHUTDOWN` or `WSAECONNRESET` — because the retry guard's red depends on it: only end of
   stream reaches the retry path, so on Windows the guard's red must come from §7's orderly-close peer, not
   from (c).
4. **The four (a2) reds and the HTTP/2 application-phase characterization** (§7). Their logic is
   platform-independent; the transport codes they end on are not (`AGENTS.md`'s networking rule).
5. **D2's red and controls** (§8), which end on a TimeoutException and are expected to be
   platform-independent.
6. **D3's fix** (§9). ThreadSanitizer does not run on the Windows toolchains, so Windows runs the non-TSan
   red — a cancel during a multi-address connect ending promptly — and the connect loop's ordinary cases.

## 7. Tests, and files

- **Characterization first, green today and after:**
  - a cancel which lands while the handshake's read is registered still ends the task promptly, as
    `operation_aborted`;
  - the peer sees an orderly end of stream after a forced cancel, in two cases: of a stream past its
    handshake, and during our handshake when the peer sends nothing more. A peer which answers after our
    cancel is reset by (c) on both platforms (§4(c)), so that case is not a characterization;
  - an HTTP/2 connection past its handshake, cancelled externally with its read registered, still ends as
    `operation_aborted`, and its peer sees an orderly end of stream — the flag's clear is what keeps (c) off
    it (§4(c));
  - `utf_baselib_tasks3`'s committed I13 and I2 cases stay green.
- **Reds, committed before the fix, deterministic under the gate's default arguments:**
  - W1 stranded (the posting order);
  - W1 plain, made certain without `--threads-count` by holding the I/O pool's other threads (it has at
    most `IO_THREADS_COUNT` = 4, `ThreadPool.h:111`) while the connect handler cancels;
  - W2, made certain by the stranded posting order: the cancel is requested in the finish continuation, on
    the strand, after the TLS shutdown has started;
  - the connect deadline and each of the HTTP server's two timers. Each uses a probe whose first
    `cancelTask( )` is swallowed — a lost cancel by construction, without the real gap. Today the deadline
    does nothing and the task ends only when the test's peer acts; after, it ends at the deadline. The
    connect deadline's red also asserts, in CS-4's `onConnectDeadline( )` (R2-F4):
    - after an external cancel which carried no reason and was lost, the deadline adds none: the chain has
      no TimeoutException "did not establish within";
    - after the pool's bound gave its reason and its cancel was lost, the pool's reason is the one kept;
  - the retry guard, using the stranded W1 order against a peer which reads the ClientHello whole and then
    shuts its send side: today the connector restarts the cancelled establishment; after, it does not.
- **D2's red and controls** are §8's; **D3's reds** are §9's.
- **Where the tests go:** the policy-level cases in `utf_baselib_tasks3` (34.7 MB at a64 clang debug; the
  scratch reproductions added about 0.3 MB, `logs/astra2/cs6/sizes.txt`). The HTTP ones — the connect
  deadline, the HTTP server's timers, the HTTP/2 characterization, D2's — in a CS-6 module with room
  (`utf_baselib_httpclient13` 36.8 MB, `utf_baselib_h2client9` 37.7 MB) or a new numbered sibling, each
  measured before it is committed.
- **Files the fix touches:**
  - owned: `TcpSslBaseTasks.h` and `TcpBaseTasks.h`;
  - widened by the orchestrator on 2026-09-28: `TcpSslStrandedStreams.h`, for (c); `TaskBase.h`, the
    additive helper only, with `requestCancelInternal( )` unchanged; `ClientConnectionTaskBase.h`,
    `onConnectDeadline( )` only, on top of CS-4's version; `HttpServer.h`, the two `onTimer( )` handlers;
    `SimpleHttpTask.h`, for D2 (§8); `TcpTunnelStage.h`, for D3's fix (§9).
  - Not in scope: the HTTP/2 driver's timer handlers and SimpleHttpTask's timer handler for (a2).

## 8. D2 — SimpleHttpTask's handshake has no deadline at all (folded into CS-6)

**The defect, pre-existing and not a lost cancel.** SimpleHttpTask creates and arms its request timer in
`continueAfterConnected( )` (`SimpleHttpTask.h:293-323`), which the TLS connector calls from
`onHandshakeCompleted( )` — after the handshake (`TcpBaseTasks.h:1352-1360`, `TcpSslBaseTasks.h:569-590`).
`SimpleHttpSslTask` adds nothing (`SimpleHttpSslTask.h:46-85`), and the protocol timer is armed only for a
directly scheduled handshake or shutdown and for the finish continuation (`TcpSslBaseTasks.h:359`, `:377`,
`:512`). So a TLS server which accepts TCP and never answers the ClientHello holds a `SimpleHttpSslTask`
until the server acts. No cancel is involved, so (c) does not help. **VERIFIED** (the arming points);
**INFERRED** (the hang), and measured by the red below.

**Decided by the maintainer on 2026-09-28:** SimpleHttpTask arms its own request timer before the
handshake, in `beginPreHandshakeStage( )`, the way `ClientConnectionTaskBase` arms its connect deadline
(`ClientConnectionTaskBase.h:532-539`). The maintainer was told that the timeout then includes the
handshake.

**The shape** (the orchestrator's recommendation, checked at the source):
- **One deadline, from the first attempt's TCP connect through the response.** `SimpleHttpTaskT` overrides
  `beginPreHandshakeStage( )`: when no timer exists yet, it creates one on the resolver's executor, exactly
  as `continueAfterConnected( )` does today, and arms it (`scheduleTimer( )`); then it calls the base, whose
  continuation starts the handshake. The stage is entered from the connect handler, under the task lock
  (`TcpBaseTasks.h:1400-1418`).
- **`continueAfterConnected( )` no longer creates or arms the timer**; everything else it does stays.
- **A handshake retry does not restart it.** The connector's retry restarts resolve, connect and handshake
  in place (`TcpBaseTasks.h:1469-1494`) and enters `beginPreHandshakeStage( )` again on the new attempt;
  the timer exists by then, so it is left running. It survives the retry's `m_resolver.reset( )`: the timer
  holds the resolver's executor — the I/O pool's — not the resolver.
- **The source argues for one more line.** `onTimer( )` acts only when `base_type::isChannelOpen( )`
  (`SimpleHttpTask.h:350`). Between a retry's `resetStreamState( )` and its new `createSocket( )` — the
  retry's resolve — there is no channel, and an expiry there would be ignored: the one deadline would be
  lost for the rest of the task. Today the condition never matters, because the timer is armed after the
  handshake and the channel stays open until the task ends; it dates from the initial commit, with no
  stated reason. **Proposed:** drop the condition, so that such an expiry cancels the task too — the cancel
  reaches the resolver through `TcpConnectionEstablisherBase::cancelTask( )` (`TcpBaseTasks.h:770-778`),
  and the task ends with the same TimeoutException. **A caller sees this only as the deadline holding
  during a retry's resolve**, which is what "one deadline through the response" means; it is named here so
  the orchestrator can take it as a decision if it reads it otherwise.
- **The cleartext SimpleHttpTask is unchanged in effect.** Its policy has no handshake, and
  `beginProtocolHandshake( )` calls the continuation at once (`TcpBaseTasks.h:562-569`), so the timer is
  armed in the same connect handler, just before `continueAfterConnected( )`, where it was armed before.
- **What the task ends with at the deadline** is today's: `onTimer( )` sets `m_timedOut` and requests the
  cancel, and `onTaskStoppedNothrow( )` turns a cancelled, timed-out task into a TimeoutException
  (`SimpleHttpTask.h:240-259`). During the handshake that cancel is a forced cancel of the handshake —
  D-L3-1's (c) and retry guard make it prompt and final; without them a cancel landing in the handshake's
  gap would be lost (§1).

**Tests**, in `utf_baselib_httpclient13` if its measured size allows:
- **The red, certain:** a `SimpleHttpSslTask` with a short timeout against a listener which accepts TCP and
  never answers the ClientHello. Today it outlives its timeout and ends only when the case closes the
  listener; after, it ends at the timeout with the TimeoutException `m_timedOut` gives. Certain, because
  when the timer fires the ClientHello's write has long completed and the ServerHello's read is registered,
  with nothing arriving.
- **Controls:**
  - the cleartext SimpleHttpTask's timeout is unchanged: a GET with a short timeout against a listener which
    accepts and never answers ends at the timeout with the TimeoutException, before and after;
  - a normal HTTPS request under a generous timeout still succeeds, before and after.
- **Files:** `SimpleHttpTask.h` only; `SimpleHttpSslTask.h` needs nothing.

## 9. D3 — a plain policy's forced cancel races asio's ranged connect (folded into CS-6)

**Characterized under ThreadSanitizer on 2026-09-28, as the maintainer decided**
(`logs/astra2/cs6/d3-rangedconnect/REPORT.txt`). A scratch connector probe connected over two endpoints —
127.0.0.2:P, refused at once, then 127.0.0.1:P, a listener whose full accept queue drops the SYN — and was
cancelled 100 ms after its first attempt started, which put the cancel after the switch in both runs (the
task stopped at once, `operation_aborted`). TSan reported 4 data races, the same two on each plain policy
(`TcpSocketAsyncBase`, `TcpSslSocketAsyncBase`):
1. the cancelling thread, under the task lock, reads `impl.socket_` in `isChannelOpen( )` from
   `cancelTask( )`; the previous write is `reactive_socket_service_base::do_open( )` on an I/O thread, from
   `iterator_connect_op` re-opening the socket for the next endpoint (`impl/connect.hpp:465-476`);
2. the same thread, from `shutdownSocket( )` → `cancel( )`, reads `impl.reactor_data_` in
   `epoll_reactor::cancel_ops( )` (`:353`); the previous write is `epoll_reactor::register_descriptor( )`
   (`:169`) in that re-open.

The positive control (`utf_baselib_basictask`) reported its one known race in the same session. A harmful
outcome — a `shutdown( )` reaching a descriptor number another thread has reused — was not measured and is
**INFERRED** possible. **The stranded policies** have a smaller form of it, **INFERRED** and not measured:
their `cancelTask( )` reads `isChannelOpen( )` on the cancelling thread before it posts
(`TcpStrandedStreams.h:205-233`, `TcpSslStrandedStreams.h:207-234`), while the strand re-opens the socket;
a cancel which reads the socket closed between the two returns without setting the forced flag or posting,
so that cancel is lost as well.

**The fix, decided by the maintainer on 2026-09-28: our own per-endpoint loop**, replacing
`asio::async_connect( )` in both places it is used — the connector (`TcpBaseTasks.h:1456`) and the tunnel
stage's proxy connect (`TcpTunnelStage.h:1610`) — through one protected helper of
`TcpConnectionEstablisherConnector`, which the tunnel stage already derives from and whose
`onConnectionEstablished( )` it already names.
- **The semantics mirrored from `iterator_connect_op`** (`impl/connect.hpp:462-512`):
  - the endpoints are tried in the resolver's order;
  - before each attempt the socket is closed, and `async_connect( )` opens it for that endpoint's protocol,
    exactly as asio does (`:473-476`);
  - an attempt which succeeds ends the loop with no error and the iterator at that endpoint;
  - an attempt which fails moves to the next, and when none is left the loop ends with the **last** error
    and the end iterator — which is what `onConnectionEstablished( )` is given today, and all it reads is
    whether the iterator is the end (`TcpBaseTasks.h:1400-1404`);
  - an empty list ends at once with `not_found` and the end iterator, posted rather than inline, as asio
    does (`:480-487`).
- **The socket is re-opened through our own path:** each attempt's completion is a task handler — under the
  task lock, and for a stranded policy on the strand — and it does the close, the re-open and the next
  connect there. The switch is then ordered with every `cancelTask( )`, which also runs under the lock, and
  with the stranded policies' posted shutdown, which runs on the same strand.
- **The cancel is checked between endpoints, under the lock:** an attempt which failed on a cancelled task
  ends the loop with `operation_aborted` instead of starting the next attempt. Today the loop starts the
  next attempt and the cancel waits for it (§2, the ranged connect's delay).
- **What a caller sees differently:** a cancel during a multi-address connect ends the task promptly,
  where today it waits for the next attempt to finish or time out. Nothing else is meant to change — the
  order, the last error and the end iterator are asio's — and anything the implementation finds it cannot
  keep comes back to the orchestrator as a decision before code.
- **Not touched:** D-L3-1's (c), which lives in the policies' `cancelTask( )`, and `cancelTask( )` itself.

**Tests:**
- **The red and green pair is ThreadSanitizer's:** the scratch probe becomes a committed case, run
  instrumented, over the plain **and** the stranded policies — reports today, none after. Its cancel is
  landed after the switch by a 100 ms wait inside the second attempt's first SYN-retry interval (1 s),
  which is timing, not a rendezvous: no deterministic signal is available from outside the connect, since
  the switch runs inside asio. Its certainty is labelled as such — TSan's analysis needs no exact timing,
  and whether the cancel landed after the switch is read back from the task's ending — unless the
  implementation's own loop offers a hook that makes it a rendezvous, which the committed case would then
  use.
- **A second red, not a TSan one:** a cancel during a multi-address connect whose next attempt cannot
  complete ends promptly after the fix; today it waits for that attempt.
- **Files:** `TcpBaseTasks.h` (owned) and `TcpTunnelStage.h`, whose ownership the orchestrator widened on
  2026-09-28.

**Agreement:** pending.
