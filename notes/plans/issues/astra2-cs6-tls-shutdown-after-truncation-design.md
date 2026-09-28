# CS-6 / I2 — a TLS task's shutdown after the peer truncated: design note

**Date:** 2026-09-27. **Status:** revision 1, written by lane 3, **for review** — not coded until this
note carries a dated agreement line (§10).

**What it implements.** Owed-list row I2 of
[`astra-remediation-owed-work.md`](astra-remediation-owed-work.md), decided by the maintainer on
2026-09-27 as shape (A), in core: *once a read has seen a truncation, the TLS task skips waiting for the
peer's close_notify. A clean close_notify ending is already prompt and must stay unchanged.* The shape is
not reopened here. This note settles the mechanism the decision leaves open: where the truncation is
recorded, whether our close_notify is still sent, how both HTTP drivers and every other TLS task are
affected, and why a clean ending is untouched.

**Provenance.** Read on `astra2-cs6` @ `ea7e414`; every line number is that tree's. Boost is the devenv7
dist's 1.90.0 and OpenSSL its 3.5.4. Nothing was built or run for this note; the one measurement it
relies on is CS-1's, recorded in `TestHttp1DriverTlsTruncation.h:66-81`. Each claim is labelled
**VERIFIED** (read at the source cited), **INFERRED** (follows from verified facts, not checked) or
**NOT VERIFIED**.

---

## 1. The defect, at the source

1. **A truncation is asio's verdict, and OpenSSL never hears of it.** The engine runs over a BIO pair.
   When the socket read under an `ssl::stream` read returns end of stream, the composed operation leaves
   its loop with `eof` (`ssl/detail/io.hpp:316`), and `engine::map_error_code( )` turns that `eof` into
   `stream_truncated` — `asio.ssl.stream:1` — because `SSL_RECEIVED_SHUTDOWN` is clear
   (`ssl/detail/impl/engine.ipp:244-271`). Nothing is written to the BIO, so OpenSSL still holds the
   session open and believes no alert has arrived. **VERIFIED.**
2. **That read consumed the socket's only end-of-stream event.** Under epoll a stream read which returns
   zero octets is `done_and_exhausted` (`detail/reactive_socket_recv_op.hpp:70-75`), which clears the
   descriptor's `try_speculative_[ read_op ]` (`detail/impl/epoll_reactor.ipp:289-291`, `:803-805`). The
   registration is edge triggered (`:186`), so the next read on that descriptor is queued and waits for
   an edge. **VERIFIED.**
3. **The teardown's shutdown then reads.** `async_shutdown( )` is `SSL_shutdown( )` twice
   (`engine.ipp:364-370`). With `SSL_RECEIVED_SHUTDOWN` clear, `ssl3_shutdown( )` sends our close_notify
   and then wants the peer's (`ssl/s3_lib.c:4599-4627`), so asio queues a read on the same socket. If the
   peer keeps its socket open and sends nothing — a half-close, which is what a server's lingering close
   is — no edge ever comes. Only the 60 s protocol timer ends the task, and it ends it **as a cancel**:
   `onProtocolTimer( )` → `requestCancelInternal( )` (`TcpSslBaseTasks.h:136-156`), the pending read
   completes `operation_aborted`, and `onShutdownCompleted( )` fails the task with it when there is no
   original exception (`:613-641`). **VERIFIED** (mechanism); **measured by CS-1** on the HTTP/1.1
   driver. A peer which has *fully* closed answers our close_notify with a reset, which does wake the
   read, so the hang needs a peer whose socket stays open. **INFERRED.**
4. **It is Linux's.** Only the epoll reactor clears `try_speculative_` on an exhausted read; the kqueue
   reactor has no such rule (`detail/impl/kqueue_reactor.ipp`, `EV_CLEAR` only), and IOCP has no reactor
   for socket I/O — a second receive after a graceful close completes at once with zero octets.
   **VERIFIED** for the reactors; **INFERRED** prompt on Windows, not measured. The mechanism below is
   platform-independent and does nothing where there is no hang.

## 2. What skips the wait — `SSL_RECEIVED_SHUTDOWN`, set as the shutdown begins

**The mechanism.** When the shutdown begins on a stream a read has seen truncated, set
`SSL_RECEIVED_SHUTDOWN` on its `SSL` object, then begin `async_shutdown( )` exactly as today:

```
::SSL_set_shutdown( ssl, ::SSL_get_shutdown( ssl ) | SSL_RECEIVED_SHUTDOWN );
```

- `ssl3_shutdown( )` then sends our close_notify and, once the alert is dispatched, returns 1 — both
  flags set, nothing pending (`s3_lib.c:4599-4607`, `:4630-4632`). `engine::perform( )` sees output
  pending and a positive result and answers `want_output` (`engine.ipp:324-328`); asio writes the alert
  and completes the handler with no error. **No read is started.** **VERIFIED.**
- **OpenSSL's own precedent.** With `SSL_OP_IGNORE_UNEXPECTED_EOF`, OpenSSL answers an unexpected EOF
  with exactly `SSL_set_shutdown( ssl, SSL_RECEIVED_SHUTDOWN )` (`ssl/record/rec_layer_s3.c:512`). That
  path never runs for us, because the BIO pair never reports an EOF (§1.1). **VERIFIED.**

**Why it is set when the shutdown begins, and not when the truncation is seen.** Once the flag is set,
`SSL_read( )` returns 0 at once (`ssl/ssl_lib.c:2348-2351`), and `SSL_get_error( )` calls that
`SSL_ERROR_ZERO_RETURN` only if the alert was a close_notify (`:4956-4958`); otherwise it is
`SSL_ERROR_SYSCALL`, which asio reports as an unspecified system error. A read issued after an early
flag would therefore fail with a code no predicate recognizes, or — had we also faked the alert — look
like a clean close_notify ending, which is precisely what D1 must be able to tell apart. So the record
is ours (§4), and the OpenSSL flag is written only at the one moment after which the stream is never
read again. **VERIFIED** (OpenSSL); **INFERRED** (the consequence for a later read).

**It adds no new concurrency to the `SSL` object.** It is written on the same thread, under the same
task lock, immediately before `async_shutdown( )` initiates — and that initiation already runs its
first engine step inline on the `SSL` object (`ssl/detail/io.hpp:343-349`, `io_op( ... )( ec, 0, 1 )`).
**VERIFIED.**

## 3. Our close_notify is still sent

**Recommended: sent, and not waited for** (§2). The alternative is to skip the TLS shutdown altogether
— `isShutdownNeeded( )` answering false after a truncation, so the socket gets a FIN and nothing else.

| | Send ours, do not wait (§2) | Skip the TLS shutdown |
|---|---|---|
| RFC 8446 §6.1, RFC 5246 §7.2.1: a party MUST send close_notify before closing its write side | kept | broken |
| CS-1's truncation cases (`utf_baselib_httpclient8`) assert the peer read our close_notify — `client-ended:asio.misc:2` (`TestHttp1DriverTlsTruncation.h:566-577`) | stay green | **turn red** |
| A server's session stays in its cache (`ssl_clear_bad_session( )` removes a session whose `SSL_SENT_SHUTDOWN` is clear, `ssl/ssl_sess.c:1264-1274`) | as today | evicted |
| Complexity | one OpenSSL call and one flag | one flag |

The table's second row is decisive on its own: those cases are green today and are about D1, not
about this. The third is minor — servers keep a session cache (`CryptoBase.h:960`), clients turn theirs
off (`:847`, `:931`) — but it is one more thing the skip would change. **VERIFIED.**

## 4. Where the truncation is recorded

### 4.1 The record lives in the stream wrapper

A flag on `AsioSslStreamWrapperT`, beside the handshake and shutdown state it already keeps
(`AsioSslStreamWrapper.h:81-83`):

- **It travels with the stream.** Streams move between tasks — the establishers build and handshake
  one and hand it to a driver, and the HTTP server moves its stream from the receive task to the send
  task (`HttpServer.h:566-567`, `:649-650`). A flag on the task instance could be separated from its
  stream; one on the stream cannot. **VERIFIED.**
- **It dies with the stream.** A retried handshake builds a new stream (`resetStreamState( )`,
  `TcpSslBaseTasks.h:176-181`), and `beginProtocolHandshake( )` already resets the per-handshake state
  (`AsioSslStreamWrapper.h:673-675`); the flag is reset there too. **VERIFIED.**
- **It survives a task's re-run.** `scheduleNothrow( )` clears the task's exception when a task is
  restarted (`TaskBase.h:1175-1186`), which is exactly what the block transfer client does to run its
  connection task a second time for the shutdown alone (`TcpBlockTransferClient.h:1664-1670`). The
  stream, and the flag, are untouched by that. **VERIFIED.**

The wrapper also owns the use of it: its `beginProtocolShutdown( )` (`:735-764`) — the only caller of
`async_shutdown( )` in the library, reached only from the policy's `beginProtocolShutdown( )`
(`TcpSslBaseTasks.h:548-567`) — sets `SSL_RECEIVED_SHUTDOWN` when the flag is set, and nowhere else is
the `SSL` object's shutdown state touched.

### 4.2 Who sets it — three options

**(a) The wrapper's read path.** `async_read_some( )` (`AsioSslStreamWrapper.h:549-560`) wraps each
handler in one which notes a truncation before forwarding the result.
- *For:* complete coverage — every read through the wrapper, whatever the consumer does with the code.
- *Against:* a new handler type on **every TLS read in the library and in every application**, so every
  TLS module's object changes. The wrapper must forward the handler's associated executor, allocator and
  cancellation slot, and its continuation hint, across the Boost versions the library still compiles
  against — guards for versions below 1.74 and 1.81 remain in `src/include` — which means the generic
  associator where the Boost in use has one, the individual traits where it does not, and the legacy
  `asio_handler_*` hooks on the oldest. A composed `asio::async_read( )` hands this function its own
  `read_op` (the file's comment at `:526-547` records the data-loss defect that forwarding this very
  handler once caused). And a handler which can outlive the wrapper must not write into it. This is a
  universal path; its blast radius is the whole TLS stack.

**(b) The policy, where consumers already classify the ending — recommended.** The TLS policy is the one
component consumers ask what a TLS ending means, and they ask it about their own stream's operations:
- `isStreamTruncationError( ec )` (`TcpSslBaseTasks.h:341-344`) — non-static, non-const — is what both
  HTTP drivers ask of every read and write that ends (`Http1ConnectionTask.h:1384-1385`, `:852-853`;
  `Http2ConnectionTask.h:1604`, `:1629-1630`), and what the HTTP server's receive task asks
  (`HttpServer.h:178`). When the answer is yes, it records it on the stream.
- `isExpectedException( eptr, exception, ec )` (`TcpSslBaseTasks.h:438-463`) — virtual, non-static — is
  what every handler failure reaches: `BL_TASKS_HANDLER_CHK_EC( ec )` asks it (`TaskBase.h:139-157`), and
  so does every catch of the handler epilogs that carries a code (`:195-257`). When the code is a
  truncation, it records it on the stream too. Every override above the policy reaches it for a
  truncation code: the connector's (`TcpBaseTasks.h:1504`) answers early only for a cancel, the block
  transfer connection's (`TcpBlockTransferCommon.h:407`) only for a cancel, an eof or a transport code
  (`isExpectedSocketException( )`, `TcpBaseTasks.h:151-230`), SimpleHttpTask's
  (`SimpleHttpTask.h:439`) only for an expected HTTP status, and the block transfer client's
  (`TcpBlockTransferClient.h:753`) asks its base first; `Pinger.h:74`'s is not a stream task.
  **VERIFIED**, by grep and reading each. *(Corrected 2026-09-27, before review: the first commit said no
  other stream task overrides it — the grep that said so had been cut short.)*

*For:* a small radius — it runs only when a code already is a truncation, and every clean path is
untouched by construction. *Against:* a question now leaves a record behind. That is deliberate and
confined: it is the policy remembering what it was told about its own stream, and the record only ever
shortens that stream's own teardown. And it covers only consumers which ask or fail with the code —
§5 names the one in the library which does neither.

**(c) `isShutdownNeeded( )` probing the transport.** Decide at teardown, with no record, by peeking the
socket for end of stream (`recv( MSG_PEEK | MSG_DONTWAIT )` returning 0). *Rejected:* it is platform code
in core — a peek on a Windows socket asio has not made non-blocking would block — it answers "the
socket is at end of stream", not "a read has seen a truncation", and it adds a system call to every TLS
teardown.

**An explicit notification each consumer calls** — `notifyStreamTruncated( )` at every classifying site —
is (b) without the side effect, and is *rejected*: it needs both drivers edited and leaves every consumer
which forgets it on the 60 s path.

**A spurious record is harmless.** Its only effect is a unidirectional close — ours sent, the peer's not
awaited — which RFC 5246 §7.2.1 permits outright: *"It is not required for the initiator of the close to
wait for the responding close_notify alert before closing the read side of the connection."* The paths
which ask about a stream whose handshake has not completed — `isProtocolHandshakeRetryableError( )`
(`TcpSslBaseTasks.h:329`) and the handshake handler's CHK_EC (`:574`) — record on a stream whose
handshake failed, and `isShutdownNeeded( )` is false for such a stream (`:666-674`, with
`AsioSslStreamWrapper.h:312`). **VERIFIED.**

## 5. How each TLS task is affected

A TLS task runs the shutdown by one of two routes, and both end in the policy's
`beginProtocolShutdown( )` and so in the wrapper's: as its finish continuation, which needs
`isCloseStreamOnTaskFinish( true )` — false by default (`TcpBaseTasks.h:68`) and set only by the tasks in
the table — or through `scheduleProtocolOperations( )` (`TcpSslBaseTasks.h:346-389`), which the policy's
own `scheduleTask( )` (`:465-468`) and the block transfer client's shutdown-only re-run
(`TcpBlockTransferClient.h:809`) reach. **VERIFIED**, by grep of `src/include`.

| Task | How a truncation reaches it | Recorded by (b) | Effect of the fix |
|---|---|---|---|
| `Http1ConnectionTaskT` (`:316`) | read and write ask the predicate and swallow the ending | yes | teardown prompt; the task ends clean instead of failing as a cancel 60 s later |
| `Http2ConnectionTaskT` (`:440`) | `isPeerClosed( )` / `isPeerClosedOnWrite( )` ask it and swallow | yes | the same — the hang is **INFERRED** for this driver and will be measured by §8's red |
| `HttpServerReceiveRequestTask` | asks it (`HttpServer.h:178`), then fails the task through CHK_EC; the connection then ends with no send task (`:598`) | yes | none: that path runs no TLS shutdown, and the record dies with the stream |
| `HttpServerSendResponseTask` (`:311`) | writes only | — | none |
| `TcpBlockTransferServer` (`:184`) | reads fail through `BL_TASKS_HANDLER_BEGIN_CHK_EC( )` (`:552`, `:1182`, …) | yes | the teardown no longer holds the connection for 60 s; the task still fails with the original truncation, as today |
| `TcpBlockTransferClientConnectionT` | reads fail through `BEGIN_CHK_EC( )` (`:482`, `:522`, `:744`); the owner re-runs it for the shutdown alone (`:1664-1670`) | yes, and the record survives the re-run (§4.1) | the same |
| `SimpleHttpTaskT` (`:155`) | failures go through CHK_EC (`:661`, `:679`, `:726`, `:872`); **a complete Content-Length body ended by a truncation is a success**, classified by the *static* `isExpectedProtocolException( )` (`:832-834`) | failures yes; **that success, no** — until `SimpleHttpTask.h:833` asks the member predicate, which is part of this change (§9) | failures prompt; that success too, with the one line. Without it the success waits 60 s and then fails as a cancel (**INFERRED**, by the same path as the drivers', not measured) |
| The establishers | `isProtocolHandshakeRetryableError( )` asks it (`TcpSslBaseTasks.h:329`) | yes, inertly (§4.2) | none |
| Any task over the cleartext policy | its `isStreamTruncationError( )` answers false (`TcpBaseTasks.h:577-582`) | — | none |

**The final exception is unchanged where the task already failed.** A task which failed with the
truncation keeps it as `m_originalException` (`TcpSslBaseTasks.h:564`), and `scheduleTaskFinishContinuation( )`
re-throws it after the shutdown (`:494-497`) — today 60 s later, after the fix at once. Where the ending
was swallowed, there is no original exception, and the task now ends clean where today it ends as a
cancel. **VERIFIED.**

**`hasShutdownCompletedSuccessfully( )` stays false after a skipped wait.** Today it means the
bidirectional closure completed: `onShutdownInternal( )` sets it from the shutdown's own code
(`AsioSslStreamWrapper.h:331`), which is clean only once the peer's close_notify has been read or was
already in. After a truncation the peer's never came, so the fix keeps it false. This matters:
`utf_baselib_io`'s `TestIO.h:3194-3205` asserts it is false on a server whose peer was cancelled with a
FIN and no close_notify — a truncation on the server's side, recorded through CHK_EC (INFERRED; that
case is in the gate either way). `wasShutdownInvoked( )` and `isShutdownNeeded( )` are unchanged.

## 6. Why a clean ending is untouched

- **A clean close_notify ending.** The peer's alert is processed by the read that meets it, which sets
  `SSL_RECEIVED_SHUTDOWN` itself (`rec_layer_s3.c:911`); asio then leaves the `eof` alone
  (`engine.ipp:266-269`), so no predicate answers yes, nothing is recorded, and the shutdown runs exactly
  as today — already prompt, since the flag it needs is already set. **VERIFIED.**
- **We close first, the peer answers.** No read has seen a truncation, nothing is recorded, and the
  shutdown waits for the peer's close_notify as today. **VERIFIED.**
- **We close first, the peer never answers.** Unchanged: the wait, bounded by the protocol timer, is
  kept whenever no truncation was seen. §8 pins this with a peer that withholds its close_notify.
- **We close first, the peer answers with a bare FIN.** The shutdown's own read meets it (nothing
  exhausted the descriptor before), completes `stream_truncated`, and `isExpectedException( )` calls it
  expected (`TcpSslBaseTasks.h:444-447`) — prompt today and after. The record this leaves comes after
  the only shutdown. **INFERRED.**

## 7. Thread safety of the record

A plain flag, as the wrapper's other state is:
- **Every write** happens inside a handler of the task which owns the stream, before that handler
  releases the task lock: CHK_EC and the epilog catches run under it (`TaskBase.h:107-117`); the
  HTTP/2 driver asks inside its prolog (`Http2ConnectionTask.h:1795`, `:1879`); the HTTP/1.1 driver
  asks just before its prolog in the same strand handler (`Http1ConnectionTask.h:1384-1387`,
  `:852-868`).
- **Every read** happens under the task lock — `scheduleTaskFinishContinuation( )` from
  `notifyReadyImpl( )` (`TaskBase.h:564-570`), `scheduleProtocolOperations( )` from `scheduleTask( )`.
- A lock release after the write and the acquire before the read order the two. **INFERRED**; §8 runs
  the new module under ThreadSanitizer with its positive control.

## 8. Tests — `utf_baselib_tasks3`, a new numbered sibling

A new module, because `utf_baselib_tasks` is 67.7 MB at win-x86 debug (`UtfBaselibTasks2Main.cpp`
says so) and `…2` instantiates no TLS policy; no other lane has reserved the name. I13's cases go
there too. Its TLS peer is a raw `asio::ssl::stream` server of its own,
which ends the stream in a chosen way — it is not an HTTP peer, so it is not one of the roles CS-5 is
unifying. Every bound is well under the 60 s timer, every rendezvous is the peer's own record, and no
case sleeps. Probe tasks derive from the TLS policy, stranded and not, and end the way the consumers of §5
do: one asks the predicate and ends clean (the drivers), one fails with the code (the messaging tasks).

- **Characterization, committed before the fix and green on today's code:** a clean close_notify ending
  tears the task down within the bound, `hasShutdownCompletedSuccessfully( )` true; we close first and
  the peer answers — prompt; we close first and the peer **withholds** its close_notify — the task is
  still running when the peer is asked, and ends only once it answers.
- **The red, committed before the fix:** a peer which truncates (a half-close, its socket kept open and
  silent until the case ends) — the teardown does not end within the bound on today's code. It is
  certain, not probable: with the peer silent, no event can wake the shutdown's read (§1.2).
- **Green after the fix:** the same cases end within the bound; the peer read our close_notify;
  `hasShutdownCompletedSuccessfully( )` false; the task that swallowed the ending ends clean, the one
  that failed keeps its truncation.
- **The drivers themselves:** an idle connection of each HTTP driver truncated by the same peer, if the
  module's measured size allows, otherwise in a sibling; the existing D1 cases in `…httpclient8` run
  through the fixed path unchanged and stay in the focused set.
- **ThreadSanitizer** over the new module at the tip, with `utf_baselib_basictask` as the positive
  control.

## 9. Files

- `src/include/baselib/tasks/AsioSslStreamWrapper.h` — the flag, its setter and getter, its reset in
  `beginProtocolHandshake( )`, `SSL_RECEIVED_SHUTDOWN` in `beginProtocolShutdown( )`, and
  `onShutdownInternal( )` keeping a skipped wait from reading as a completed closure. **Owned by the
  brief "if the note puts the record there"**, which it does.
- `src/include/baselib/tasks/TcpSslBaseTasks.h` — the two predicates record. Owned.
- `src/include/baselib/http/SimpleHttpTask.h:833` — **accepted by the orchestrator on 2026-09-27 as shape
  (A) applied to the legacy client's path, with the lane's ownership widened to that line.** It asks
  `base_type::isStreamTruncationError( ec )` where it asks the static `isExpectedProtocolException( nullptr,
  std::exception(), &ec )`. The two answer the same for both policies — the cleartext pair are both
  false, and the TLS pair are both `isExpectedSslErrorCode( ec )` (`TcpSslBaseTasks.h:341-344`,
  `:760-779`) — so the line changes what is recorded and nothing it decides. Without it the legacy client
  keeps I2 on its one path that ends in success. Its test is a `SimpleHttpSslGetTask` against the same
  peer, answering with a complete Content-Length body and then truncating: red on today's tree, green
  after.

## 10. Reversing conditions, and agreement

- If the maintainer wants the fix to reach **consumers outside the library** which neither ask the
  policy nor fail with the code, (b) is the wrong recording point and (a) is the answer, at the cost
  §4.2 names.
- If sending our close_notify after a truncation is found to draw a **reset** where today there is none
  — a peer which closes on it with our data unread — skipping the TLS shutdown becomes worth weighing,
  and the D1 cases would have to change with it. Nothing found so far says so: after our alert the
  policy's teardown sends a FIN, exactly as it does 60 s later today.

**Agreement:** pending.
