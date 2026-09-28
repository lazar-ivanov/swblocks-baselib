# CS-6 / D-L3-1 — a forced cancel lost between two steps of a TLS operation: design note

**Date:** 2026-09-28. **Status:** revision 1, written by lane 3, **for review**. The shape then goes to the
maintainer. Nothing is coded before the maintainer has chosen it.

**What this is.** Decision D-L3-1, found by lane 3 while characterizing I13 on 2026-09-27, and folded into
CS-6 by the maintainer on 2026-09-28 ("fold now, design note first"). A forced cancel can reap nothing,
and no later cancel, deadline or timer can then end the task: it ends only when the peer acts. This note
gives the mechanism at the source, the deterministic reproductions, the candidate shapes with what was
measured of each, and a recommendation.

**Provenance.** Source line numbers are `ea7e414`'s; at the branch tip `TcpBaseTasks.h` is one line longer
from `:305` on (I13's fix). Boost is the devenv7 dist's 1.90.0 and OpenSSL its 3.5.4. The measurements
were made on a64 clang debug with a scratch test case and scratch prototypes, **none of them committed**.
Every run, the case and both prototype diffs are in the run's state directory under
`logs/astra2/cs6/lostcancel/`, indexed in `INDEX.txt`. Each claim is labelled **VERIFIED** (read at the
source cited, or measured where it says so), **INFERRED** (follows from verified facts, not checked) or
**NOT VERIFIED**.

---

## 1. The defect, at the source

1. **The forced cancel shuts the send side down and cancels.** `TcpSocketCommonBase::shutdownSocket( )`
   with force set is `shutdown_send` then `cancel( )` (`TcpBaseTasks.h:241-330`). The plain TLS policy
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
   open**, deliberately so — shutting it down would reset the peer on Windows and throw away data we had
   delivered (`TcpBaseTasks.h:323-359`) — and it waits for the peer. **VERIFIED.**
5. **Nothing else can end that wait.** `requestCancelInternal( )` returns at once once a cancel has been
   requested (`TaskBase.h:1027-1030`). Every deadline in the library which exists to bound a silent peer
   cancels through it:
   - the TLS protocol timer, which bounds a handshake and a TLS shutdown (`TcpSslBaseTasks.h:136-156`; its
     purpose is stated at `:86-90`);
   - the HTTP establisher's connect deadline (`ClientConnectionTaskBase.h:478-500`);
   - the HTTP/2 driver's ping and drain deadlines (`Http2ConnectionTask.h:2181-2219`, `:2473-2502`);
   - the HTTP server tasks' timers (`HttpServer.h:119-134`, `:345-360`);
   - SimpleHttpTask's request timer (`SimpleHttpTask.h:325-358`).

   So after a lost cancel the task ends only when the peer sends, closes or resets. **VERIFIED** (the
   paths); **measured** for the protocol timer (§3).

## 2. Where a cancel can be lost

| | Window | Can it hang? |
|---|---|---|
| **W1** | a TLS handshake's steps — client and server roles, both TLS policies | yes: a peer which accepted TCP and then says nothing (a wedged server, a proxy which answers nothing) |
| **W2** | a TLS shutdown — our close_notify written, the peer's awaited | yes: the peer the protocol timer exists for, one which never answers |
| **W3** | a TLS read which spans several socket reads (a record arriving in pieces), or a TLS write which must read first (a renegotiation) | only if the peer stops mid-record and stays open |
| **W4** | `asio::async_read_until( )`'s steps (`SimpleHttpTask.h:663`, `:710`) | only if the peer stops mid-head and stays open |

**Not windows** (**VERIFIED**):
- a cleartext `async_read_some( )`, which is registered from its start to its completion;
- `asio::async_read( )` and `async_write( )` loops given `untilCanceled( )`, which stops the loop at the
  next step once the task is cancelled (`TcpBaseTasks.h:71-109`). The block transfer tasks and the
  tunnel stage use it, so a proxy tunnel is not a window;
- any write step started after the forced shutdown.

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
starts hung 2 runs of 3. Each window is microseconds wide. It matters because what follows a hit is a
wait no deadline can end.

## 4. The candidate shapes

**(a) Deadlines re-issue the socket cancel.** When a deadline fires on a task which was already cancelled
and is still running, it calls `cancelTask( )` again instead of `requestCancelInternal( )`. That reaps the
read which has registered since. Three scopes:
- **(a1)** the protocol timer alone, which closes W2;
- **(a2)** every deadline in §1.5 — seven handlers in five files — through one additive `TaskBase` helper,
  so that no deadline in the library can be defeated by an earlier lost cancel;
- **(a3)** make `requestCancelInternal( )` itself re-issue. Rejected: fourteen `cancelTask( )` overrides in
  twelve files, several of them not stream tasks, would all be called twice (`TaskBase.h:1069` declares
  it).

A stream policy's `cancelTask( )` is idempotent at the socket level, since it shuts down and cancels again.
A deadline fires on a cancelled task which is still running when the cancel was lost. It can also fire in
the moment after a cancel which worked, before that cancel's aborted completions have run; a second
shutdown and cancel are harmless there. So (a) changes nothing else. **INFERRED.** The hang is bounded by
the deadline, 60 s by default, rather than ended promptly. It needs no platform-specific behaviour.

**Measured**, as (a1) on top of (c): W2 ended at **2003 ms**, the timer's fire, in **3 of 3**. It was 0 of 3
before. (`prototype-c-plus-a.diff`, `protoCA-plain-shutdown-t1.summary`.)

**(b) A protocol timer for every handshake.** The connector's handshake arms no timer: it is armed only by
the finish continuation and by a directly scheduled handshake or shutdown (`TcpSslBaseTasks.h:359`, `:377`,
`:512`). With (a), (b) would bound W1 everywhere. It changes behaviour for any TLS client whose
handshake legitimately takes longer than the timeout. It is **not needed with (c)**.

**(c) Shut the receive side too while the task's own TLS handshake is incomplete.** Before any application
data has passed, a receive shutdown loses nothing: the Windows reset the send-only rule avoids
(`TcpBaseTasks.h:323-359`) would destroy only handshake records. Once the receive side is shut down, a
read registered at the cancel wakes, and any read started after it ends at once. The next handshake step
fails, and the task ends as a cancel.
- **Measured on Linux 6.8** with a raw socket: after `shutdown( SHUT_RD )` a registered edge-triggered
  read gets an event at once, and a later read returns end of stream at once. After `SHUT_WR` alone,
  neither happens (`logs/astra2/cs6/hang1/shut-rd-probe.txt`).
- **Measured in the library** (`prototype-c.diff`): W1 stranded **3 of 3** and W1 plain **3 of 3** ended within
  **1–2 ms**, where today it is 0 of 3 each. W2 still hung 3 of 3, as designed.
- **What it must not touch:** a stream attached after its handshake — every driver's. There, a receive
  shutdown makes our own read report end of stream, which the HTTP/1.1 driver would take as the peer's
  close (`Http1ConnectionTask.h:2134-2139`). So the condition is "a handshake this task is running itself",
  and it must be read without a race:
  - the plain policy's `cancelTask( )` runs under the task lock;
  - the stranded policy's shutdown runs on the strand.

  The flag it reads must therefore be written under that lock, or on that strand: set when the policy
  begins its own handshake, and cleared when that handshake completes. The prototype also read the
  wrapper's `hasHandshakeCompletedSuccessfully( )`. That flag is written outside the lock
  (`AsioSslStreamWrapper.h:312`), so the implementation must not.
- **An interaction the prototype found.** With (c), the cancelled handshake fails with a truncation
  (`asio.ssl.stream:1`) rather than `operation_aborted`, and the connector's retry path counts a truncation
  as retryable (`TcpBaseTasks.h:1469-1494`). So it restarted establishment once before the restart saw the
  cancel. The retry condition needs `! isCanceled( )`. **VERIFIED** by the prototype's trace.

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

## 5. Recommendation — (c), (a2) and the retry guard

- **(c)** closes W1 promptly and deterministically, measured on both TLS policies. W1 is where a silent
  peer is plausible and nothing is at stake.
- **(a2)** makes every deadline in the library hold again after a lost cancel. That closes W2, measured
  through the protocol timer, and bounds W3 and W4 wherever their consumer has a deadline:
  - SimpleHttpTask's request timer;
  - the HTTP server's two timers;
  - the HTTP/2 driver's ping deadline.

  The change is one additive `TaskBase` helper and seven handlers, each of which re-issues only on a task
  already cancelled and still running (§4(a)).
- **The retry guard** keeps (c) from restarting a cancelled establishment.

**Not recommended:**
- (a1) alone leaves every other deadline defeatable;
- (b) is not needed once W1 is closed by (c);
- (d) is unsafe on the plain policies;
- (e) adds a periodic timer to every forced cancel, only to close what (a2) leaves.

**What stays open after it:** a W3 or W4 read on a consumer with no deadline armed, against a peer which
stops mid-record or mid-head and ignores our FIN. The HTTP/1.1 driver documents that it relies on the peer
answering our FIN in exactly this gap (A1-tls face 1, `Http1ConnectionTask.h:1411-1431`).

**Reversing conditions.**
- If the maintainer wants a cancel to end every stream task with no reliance on the peer or on a deadline,
  (e) replaces (a2), and (c) stays for W1's promptness.
- If the Windows measurement below shows that a receive shutdown does not end a later receive at once, (c)
  becomes (b) there, and W1 is bounded by the timer instead.
- If the maintainer wants the blast radius kept to the TLS policy's own files, (a1) replaces (a2), and
  §2's W3 and W4 stay defeatable wherever a deadline exists.

## 6. What Windows must measure

This is the Windows matrix owed for CS-6.
1. **The defect.** The two deterministic reproductions — W1 stranded, and W1 plain with one I/O thread —
   and W2 with a 2 s protocol timer, today and after. The gap is **INFERRED** on IOCP (`CancelIoEx( )` reaps
   only outstanding I/O); it is not measured there.
2. **(c)'s premise.** After `shutdown( SD_RECEIVE )`, does a `WSARecv( )` which is started, or already
   outstanding, complete at once, and with which code? This is **NOT VERIFIED**; the Linux answer is §4(c)'s.
3. **(c)'s effect on the peer.** A peer which sends after our `SD_RECEIVE` draws a reset on Windows. During
   a handshake that only fails the handshake a second way. The truncation cases and the handshake-failure
   cases should show nothing else move.

## 7. Tests, and files

- **Characterization first, green today and after:**
  - a cancel which lands while the handshake's read is registered still ends the task promptly, as
    `operation_aborted`;
  - the peer sees an orderly end of stream after a forced cancel;
  - a cancelled handshake is not retried;
  - `utf_baselib_tasks3`'s committed I13 and I2 cases stay green.
- **Reds, committed before the fix, deterministic under the gate's default arguments:**
  - W1 stranded (the posting order);
  - W1 plain, made certain without `--threads-count` by holding the I/O pool's other threads (it has at
    most `IO_THREADS_COUNT` = 4, `ThreadPool.h:111`) while the connect handler cancels;
  - W2 with a shortened protocol timer.
- **Where the tests go:** in `utf_baselib_tasks3`. The scratch case's four cases added about 0.3 MB there,
  34.5 to 34.8 MB (`logs/astra2/cs6/sizes.txt`).
- **Files the fix touches:**
  - `TcpSslBaseTasks.h` and `TcpBaseTasks.h` (owned);
  - `TcpSslStrandedStreams.h`, `TaskBase.h`, `ClientConnectionTaskBase.h`, `Http2ConnectionTask.h`,
    `HttpServer.h` and `SimpleHttpTask.h`, whose ownership the orchestrator must widen.

**Agreement:** pending.
