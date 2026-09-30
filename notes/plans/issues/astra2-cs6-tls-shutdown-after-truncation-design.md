# CS-6 / I2 — a TLS task's shutdown after the peer truncated: design note

**Date:** 2026-09-27. **Status:** revision 3, written by lane 3 — **agreed 2026-09-28** (§10). I2 is
implemented to this note.

**Revisions.** r1 `a8542d6` (with `700da22` and `bb36363`). **r2** `f913dbc` carries review round 1
(`CS6-I2-design-r1.md`, agree with changes) and the orchestrator's answers (`CS6-I2-orchestrator-r2.md`),
which accept everything: the null-stream guard (F1, §4.2 and §9), §7's ordering argument replaced (F2),
the clean path's assignment kept literally (§9), four text corrections (F3-F6), the sharper red (P8,
§8), ThreadSanitizer over `utf_baselib_httpclient8` too (P9), the module's `devenv7_only` marker (P10),
the readers of `hasShutdownCompletedSuccessfully( )` (P11, §5), and the stale CS-1 comment folded in
(F7, §9). Two of the review's own premises are corrected where they are used: the Boost guards around
asio are cited as they exist (a 1.81 guard does exist, in `UuidBoostImports.h:54`, but it is Boost.Uuid's,
and the 1.74 guard both rounds cited, `OSImplPlatformCommon.h:1175`, is Boost.Filesystem's
`copy_directory`; the asio guards are at 1.66, 1.72 and 1.89), and 22 other modules carry the marker,
not 23. **r3 (this)** carries review round 2 (`CS6-I2-design-r2.md`, agree with changes): the red asserts
`isCanceled( )`, which is what separates the probe that keeps its truncation (C1); a second ending the fix
changes, a peer which closed its socket entirely, recorded in §5 and given a case in §8 (C2); §1.3's
route for "as a cancel" corrected (C3); `onShutdownInternal( )` named as a second reader (C4); the asio
guards (C5); the F7 comment's last clause (C6); the drivers' cases committed with the red, the HTTP/2
one against a peer selecting `h2` (C7); the one recording source that is not the task's own stream (C8);
the object size's evidence (C9); and proposals P-b to P-e.

**What it implements.** Owed-list row I2 of
[`astra-remediation-owed-work.md`](astra-remediation-owed-work.md), decided by the maintainer on
2026-09-27 as shape (A), in core: *once a read has seen a truncation, the TLS task skips waiting for the
peer's close_notify. A clean close_notify ending is already prompt and must stay unchanged.* The shape is
not reopened here. This note settles the mechanism the decision leaves open: where the truncation is
recorded, whether our close_notify is still sent, how both HTTP drivers and every other TLS task are
affected, and why a clean ending is untouched.

**Provenance.** Read on `astra2-cs6` @ `ea7e414`; every line number is that tree's. Boost is the devenv7
dist's 1.90.0 and OpenSSL its 3.5.4. Nothing was built or run for this note beyond the lane's
characterization build, whose object size §8 cites from `logs/astra2/cs6/sizes.txt` (in the run's state
directory, `http2-l0-state/`, a sibling of the checkout, kept outside the repository on purpose);
the one behavioural measurement it relies on is CS-1's, recorded in
`TestHttp1DriverTlsTruncation.h:66-81`. Each claim is labelled
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
   is — no edge ever comes. Only the 60 s protocol timer ends the task, and it ends it **as a cancel**.
   `onProtocolTimer( )` → `requestCancelInternal( )` (`TcpSslBaseTasks.h:136-156`) shuts the socket down
   forcefully and sets `m_wasSocketShutdownForcefully`, and the pending read completes
   `operation_aborted`. `TcpSocketCommonBase::onTaskStoppedNothrow( )` then gives a cancelled task whose
   socket was shut down forcefully `operation_aborted` as its ending (`TcpBaseTasks.h:123-143`), which
   `notifyReadyImpl( )` stores when there is no original exception (`TaskBase.h:719-722`). That ending
   does not come from `onShutdownCompleted( )`'s own check (`TcpSslBaseTasks.h:613-641`): every consumer here counts the
   cancel as expected. The HTTP/1.1 driver does so because it never ran a handshake
   (`! m_isHandshakeCompleted`, `TcpSslBaseTasks.h:449-460`), and the connector's tasks through `TcpBaseTasks.h:1510-1520`.
   **VERIFIED** (mechanism); **measured by CS-1** on the HTTP/1.1 driver. A peer which has *fully*
   closed answers our close_notify with a reset, which does wake the read, so the hang needs a peer whose
   socket stays open. **INFERRED.**
4. **It is Linux's.** Only the epoll reactor clears `try_speculative_` on an exhausted read; the kqueue
   reactor has no such rule (`detail/impl/kqueue_reactor.ipp`, `EV_CLEAR` only), and IOCP has no reactor
   for socket I/O — a second receive after a graceful close completes at once with zero octets.
   **VERIFIED** for the reactors; **INFERRED** prompt on Windows, not measured. The mechanism below is
   platform-independent and changes no outcome where there is no hang: where the shutdown's read would
   end at once, the fix changes its code from the truncation to none, and the task's ending is the same
   either way.
   *(MEASURED ON WINDOWS, 2026-09-29, `win-x64-vc143-debug` at `edd921b` with `a5d9d9a` reverted in a
   throwaway tree: `TlsShutdown_ATruncationDoesNotWaitForTheCloseNotifyTests` and the three driver cases,
   `Http1DriverTls_ATruncatedIdleConnectionEndsWithoutWaitingTests`,
   `SimpleHttpTls_ACompleteResponseEndedByATruncationSucceedsTests` and
   `Http2DriverTls_ATruncatedIdleConnectionEndsWithoutWaitingTests`, were green 5 of 5 each BEFORE the
   fix, and are after it: on IOCP the wait does not hang, as inferred. `http2-l0-state/logs/win-astra/`.)*

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
  by setting `SSL_RECEIVED_SHUTDOWN` — `SSL_set_shutdown( ssl, SSL_RECEIVED_SHUTDOWN )` — and, because it
  is making the ending look clean, by faking `warn_alert = SSL_AD_CLOSE_NOTIFY` beside it
  (`ssl/record/rec_layer_s3.c:511-513`). The flag is the precedent; the faked alert is what the next
  paragraph rejects. That path never runs for us, because the BIO pair never reports an EOF (§1.1).
  **VERIFIED.**

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
  against — the guards around asio in `src/include` are at 1.66 (`NetUtils.h:249`,
  `TcpSslBaseTasks.h:244`), 1.72 (`OSBoostImports.h:77`, `AsioSslStreamWrapper.h:68`) and 1.89
  (`BoostAsioCompat.h:41`, `AsioSslCompat.h:29`) — which means the
  generic associator where the Boost in use has one, the individual traits where it does not, and the
  legacy `asio_handler_*` hooks on the oldest. A composed `asio::async_read( )` hands this function its own
  `read_op` (the file's comment at `:526-547` records the data-loss defect that forwarding this very
  handler once caused). And a handler which can outlive the wrapper must not write into it. This is a
  universal path; its blast radius is the whole TLS stack.

**(b) The policy, where consumers already classify the ending — recommended.** The TLS policy is the one
component consumers ask what a TLS ending means, and they ask it about their own stream's operations:
- `isStreamTruncationError( ec )` (`TcpSslBaseTasks.h:341-344`) — non-static, non-const — is what both
  HTTP drivers ask of every read and write that ends (`Http1ConnectionTask.h:1384-1385`, `:852-853`;
  `Http2ConnectionTask.h:1604`, `:1629-1630`), and what the HTTP server's receive task asks
  (`HttpServer.h:178`). When the answer is yes, it records it on the stream.

  Both predicates record **only when `m_sslStream` is non-null**. They are `NOEXCEPT` members of the
  policy, and a task holds no stream before `createSocket( )` (a resolve failure reaches CHK_EC with
  none, `TcpBaseTasks.h:1449`), after `resetStreamState( )` (`TcpSslBaseTasks.h:178`) and after
  `detachStream( )` (`:718-725`); `getStream( )` asserts on null (`:266-271`). No path pairs a
  truncation code with a null stream today — truncations come only from operations on a stream, and
  `isProtocolHandshakeRetryableError( )` is behind `isChannelOpen( )` (`TcpBaseTasks.h:1474`) — but a
  universal-path predicate must not depend on that. **VERIFIED.**
- `isExpectedException( eptr, exception, ec )` (`TcpSslBaseTasks.h:438-463`) — virtual, non-static — is
  what every handler failure reaches: `BL_TASKS_HANDLER_CHK_EC( ec )` asks it (`TaskBase.h:139-157`), and
  so does every catch of the handler epilogs that carries a code (`:199-236`, `:247-254`). When the code is a
  truncation, it records it on the stream too. Every override above the policy reaches it for a
  truncation code: the connector's (`TcpBaseTasks.h:1504`) answers early only for a cancel, the block
  transfer connection's (`TcpBlockTransferCommon.h:407`) only for a cancel, an eof or a transport code
  (`isExpectedSocketException( )`, `TcpBaseTasks.h:152-235`), SimpleHttpTask's
  (`SimpleHttpTask.h:439`) only for an expected HTTP status, and the block transfer client's
  (`TcpBlockTransferClient.h:753`) asks its base first; `Pinger.h:74`'s is not a stream task.
  **VERIFIED**, by grep and reading each. *(Corrected 2026-09-27, before review: the first commit said no
  other stream task overrides it — the grep that said so had been cut short.)*

*For:* a small radius — it runs only when a code already is a truncation, and every clean path is
untouched by construction. *Against:* a question now leaves a record behind. That is deliberate and
confined: it is the policy remembering what it was told, which is its own stream's ending on every path
but one (below), and the record only ever shortens that stream's own teardown. And it covers only
consumers which ask or fail with the code — §5 names the one in the library which does neither.

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

One path asks about a code which is not this stream's at all. The block transfer server's
`checkToPrintExceptionInfo( )` (`TcpBlockTransferServer.h:216`) asks `isExpectedException( )` about a
backend operation's exception (`chk4ServerErrors( )` `:376`, from `onAsyncCommandCompleted( )` `:914`
and `onChunkDataProcessed( )` `:1364`), so a backend which itself speaks TLS can leave another stream's
truncation recorded here. That record is spurious in the same harmless way: the connection's close
becomes unidirectional. **VERIFIED** (the path); **INFERRED** (that a backend produces such a code).

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
| `HttpServerSendResponseTask` (`:311`) | writes only; a write that ends with a truncation fails through CHK_EC | yes, through `isExpectedException( )` | teardown prompt on that failure; nothing else changes |
| `TcpBlockTransferServer` (`:184`) | reads fail through `BL_TASKS_HANDLER_BEGIN_CHK_EC( )` (`:552`, `:1182`, …) | yes | the teardown no longer holds the connection for 60 s; the task still fails with the original truncation, as today |
| `TcpBlockTransferClientConnectionT` | reads fail through `BEGIN_CHK_EC( )` (`:482`, `:522`, `:744`); the owner re-runs it for the shutdown alone (`:1664-1670`) | yes, and the record survives the re-run (§4.1) | the same |
| `SimpleHttpTaskT` (`:155`) | failures go through CHK_EC (`:661`, `:679`, `:726`, `:872`); **a complete Content-Length body ended by a truncation is a success**, classified by the *static* `isExpectedProtocolException( )` (`:832-834`) | failures yes; **that success, no** — until `SimpleHttpTask.h:833` asks the member predicate, which is part of this change (§9) | failures prompt; that success too, with the one line. Without it the success waits 60 s and then fails as a cancel (**INFERRED**, by the same path as the drivers', not measured) |
| The establishers | `isProtocolHandshakeRetryableError( )` asks it (`TcpSslBaseTasks.h:329`) | yes, inertly (§4.2) | none |
| Any task over the cleartext policy | its `isStreamTruncationError( )` answers false (`TcpBaseTasks.h:577-582`) | — | none |
| The test peers over the TLS policy, e.g. `Http2TestConnectionT` (`src/utests/include/utests/baselib/Http2TestServer.h:842`) | ask the predicate (`:1008`) and close their stream on finish | yes | their teardown after a client's truncation is prompt too; the whole-suite gate is their check |

**The final exception is unchanged where the task already failed.** A task which failed with the
truncation keeps it as `m_originalException` (`TcpSslBaseTasks.h:564`), and `scheduleTaskFinishContinuation( )`
re-throws it after the shutdown (`:494-497`) — today 60 s later, after the fix at once. Where the ending
was swallowed, there is no original exception, and the task now ends clean where today it ends as a
cancel. **VERIFIED.**

**A peer which closed its socket entirely changes too, though it never hung.** Our close_notify draws a
reset, which wakes the shutdown's read (§1.3), and today the shutdown completes with that reset.
`onShutdownCompleted( )` fails a task with it (`TcpSslBaseTasks.h:639`) when the task has no original
exception and its `isExpectedException( )` does not admit a reset. That is a task which ran its own
handshake (`m_isHandshakeCompleted`, written only at `:576`) and has no override admitting one: the
HTTP/2 driver after `onPeerClosed( )` (`Http2ConnectionTask.h:1686-1701`, whose chain admits only a
cancel, `TcpBaseTasks.h:1510-1520`), and `SimpleHttpTaskT`'s truncated but complete success. Today
those fail at once with the reset, reported as unexpected. After the fix the shutdown reads nothing,
completes with no error, and they end clean. Three kinds are unchanged:
- the HTTP/1.1 driver and the HTTP server's send task, which never handshake and so admit a reset
  through `! m_isHandshakeCompleted` (`TcpSslBaseTasks.h:449-460`);
- the block transfer tasks, which admit one through `isExpectedSocketException( )`
  (`TcpBlockTransferCommon.h:413-421`);
- every task which already failed, which re-throws its original.

**VERIFIED** (the paths); **INFERRED** (the outcome, until §8 measures it).

*(Corrected 2026-09-28, by measurement: **on Linux this ending does not change**. Once the peer's FIN
has been received, a read reports end of stream even after a reset has arrived; the error is only
left pending, as `SO_ERROR` EPIPE (`logs/astra2/cs6/c2-reset-after-fin-probe.txt`, kernel 6.8). So
today's shutdown read reports a truncation, which `isExpectedSslErrorCode( )` admits, and these tasks
end clean today as well. `TlsShutdown_ATruncationThenACloseEndsCleanTests` pins it (`323a538`),
green before the fix. The paragraph above holds only where a platform hands the read the reset
itself — INFERRED possible on Windows, not measured; the Windows matrix measures it (superseded below).)*

*(Measured on Windows, 2026-09-29: **Windows does hand the read the reset.** Before the fix the
shutdown's read after the truncation got `system:10053`, and
`TlsShutdown_ATruncationThenACloseEndsCleanTests` FAILED with it, 5 of 5; after the fix, which reads
nothing, it ends clean, 5 of 5. So on Windows the fix changes this ending too - from a failure to a
clean end - and the case is red before it and green after there; its header says so now.
`http2-l0-state/logs/win-astra/`, `win-x64-vc143-debug`.)*

**`hasShutdownCompletedSuccessfully( )` stays false after a skipped wait.** Today it means the
bidirectional closure completed: `onShutdownInternal( )` sets it from the shutdown's own code
(`AsioSslStreamWrapper.h:331`), which is clean only once the peer's close_notify has been read or was
already in. After a truncation the peer's never came, so the fix keeps it false. This matters:
`utf_baselib_io`'s `TestIO.h:3194-3205` asserts it is false on a server whose peer was cancelled with a
FIN and no close_notify — a truncation on the server's side, recorded through CHK_EC (INFERRED; that
case is in the gate either way). Its only readers before this change are test assertions: three in
`TestIO.h` (`:561`, `:2470`, `:3204`), and one on a freshly built wrapper in
`utf_baselib_http2/TestAsioSslStreamWrapper.h:209`, which no shutdown has touched; no library code
decides on it. **VERIFIED**, by grep.
`wasShutdownInvoked( )` and `isShutdownNeeded( )` are unchanged.

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

A plain flag, with the discipline the wrapper's other state already has:
`m_hasHandshakeCompletedSuccessfully` is written by `onHandshakeInternal( )`
(`AsioSslStreamWrapper.h:312`) before the transfer callback takes the task lock
(`TcpSslBaseTasks.h:574`) and read under it later.

- **Written under the task lock** by every CHK_EC and epilog catch (`TaskBase.h:107-120`, `:139-157`,
  `:195-254`), by the HTTP/2 driver inside its prolog (`Http2ConnectionTask.h:1642` in `:1638`,
  `:1879` in `:1795`), and by `onShutdownCompleted( )` (`TcpSslBaseTasks.h:622` in `:594`).
- **Written before the prolog, outside the lock,** by the HTTP/1.1 driver on the stream's strand
  (`Http1ConnectionTask.h:1384-1385` before `:1387`; `:852-853` before `:868`), by the HTTP server's
  receive task on a pool thread (`HttpServer.h:178-181` before `:183`), and by `SimpleHttpTask.h:832-835`
  after §9's switch (before `:837` or `:872`).
- **Read** by the wrapper's `beginProtocolShutdown( )`, reached from
  `scheduleTaskFinishContinuation( )` under the lock in `notifyReadyImpl( )` (`TaskBase.h:564-570`) or
  from `scheduleProtocolOperations( )` under the lock in `scheduleNothrow( )` (`:1167`).
- **Read also by `onShutdownInternal( )`** (§9), in the shutdown's completion handler and outside the
  task lock, because it runs before the transfer callback takes the lock (`AsioSslStreamWrapper.h:317-336`,
  `TcpSslBaseTasks.h:594`). asio orders a completion after its initiation, which read the same record
  under the lock. Nothing records in between: no other operation on the stream is outstanding while
  the shutdown runs (**INFERRED**), and the one recording site inside it, `onShutdownCompleted( )`'s
  `isExpectedException( )` (`TcpSslBaseTasks.h:622`), runs after `onShutdownInternal( )` in the same
  call chain.

What orders a pre-prolog write with the read: the read is reached from the writer's own epilog
(`TaskBase.h:267`, program order) or, on a driver, from a later strand handler. On the multi-operation
drivers the terminal is taken only when the pending count reaches zero (`MultiOperationTask.h:147-157`,
`:393-458`), so every handler's pre-prolog write happens before it, through the accounting lock - the
one terminal taken off the strand, from `abandonOperation( )`, included. The only other thread that can
reach `scheduleTaskFinishContinuation( )` while a handler is between its write and its prolog is a
cancel, and every cancel path sets `m_wasSocketShutdownForcefully` under the task lock while the channel
is open (`requestCancel( )`, `TaskBase.h:1237-1243`; `TcpSslStrandedStreams.h:226`;
`Http1ConnectionTask.h:2312`; `TcpSslBaseTasks.h:395-397`; the protocol timer, `:140`), so
`scheduleTaskFinishContinuation( )` returns at `:474-501` and the wrapper is never asked. The HTTP
server's receive task runs no TLS shutdown at all (§5). **VERIFIED** for these paths; **INFERRED** that
no other reacher exists.

**I13's class is not reintroduced.** I13 is asio socket state written from a cancelling thread while
an operation starts on a pool thread (`TcpBaseTasks.h:319`). Nothing here touches socket state; the
one new touch of shared OpenSSL state, `SSL_set_shutdown( )`, is on the thread and at the point where
`async_shutdown( )` already runs `SSL_shutdown( )` inline (§2). **VERIFIED.** §8 runs the new module
under ThreadSanitizer with its positive control.

*(Revised 2026-09-27 after review round 1, F2: the first revision said every write happens before its
handler releases the task lock, and that a release and an acquire order the two. Three sites write
before their prolog, outside the lock, and the ordering is the one above.)*

## 8. Tests — `utf_baselib_tasks3`, a new numbered sibling

A new module, because `utf_baselib_tasks` is 67.7 MB at win-x86 debug (`UtfBaselibTasks2Main.cpp`
says so) and `…2` instantiates no TLS policy; no other lane has reserved the name. It carries the
`devenv7_only` marker, as 22 other modules do, because the stranded policies `#error` below Boost 1.72
(`TcpSslStrandedStreams.h:34-36`). I13's cases go there too. Its TLS peer is a raw `asio::ssl::stream`
server of its own, which ends the stream in a chosen way — it is not an HTTP peer, so it is not one of
the roles CS-5 is unifying. Every bound is well under the 60 s timer, every rendezvous is the peer's own
record, and no case sleeps. Probe tasks derive from the TLS policy, stranded and not, and end the way
the consumers of §5 do: one asks the predicate and ends clean (the drivers), one fails with the code
(the messaging tasks).

- **Characterization, committed before the fix and green on today's code:** a clean close_notify ending
  tears the task down within the bound, `hasShutdownCompletedSuccessfully( )` true; we close first and
  the peer answers — prompt; we close first and the peer **withholds** its close_notify — the task is
  still running when the peer is asked, and ends only once it answers.
- **The red, committed before the fix:** a peer which truncates — a half-close, its socket kept open
  and silent until the case ends. The probe's protocol timeout is shortened with `setProtocolTimeout( )`
  (`TcpSslBaseTasks.h:682-685`) to a few seconds, so that on today's code the case observes the symptom
  I2 names rather than a bound expiring: the protocol timer cancels the task (`onProtocolTimer( )`,
  `:136-156`), so `isCanceled( )` is true. The probe that swallowed the ending then ends **as a
  cancel**, `operation_aborted`, which `TcpSocketCommonBase::onTaskStoppedNothrow( )` gives a cancelled
  task whose socket was shut down forcefully (`TcpBaseTasks.h:123-143`, stored at `TaskBase.h:719-722`).
  The probe that failed with the code keeps its truncation, re-thrown and stored first
  (`TcpSslBaseTasks.h:494-497`, `TaskBase.h:599-600`; §5). `hasShutdownCompletedSuccessfully( )` is
  false for both. On the connector the timer bounds the shutdown alone: it is armed only by the finish
  continuation (`TcpSslBaseTasks.h:512`) and by a directly scheduled handshake or shutdown (`:359`, `:377`), and the
  connector's handshake is neither. After the fix the same case ends before the timer, with
  `isCanceled( )` false — clean for the probe that swallows the ending, and with the truncation itself
  for the one that fails with it. The peer has read our close_notify, and
  `hasShutdownCompletedSuccessfully( )` is still false (§5). **`isCanceled( )` is the assertion that
  makes both probes red**: the probe that fails with the code ends with the same exception before and
  after, and within the teardown bound both times. The red is certain, not probable: with the peer
  silent, no event can wake the shutdown's read (§1.2). The teardown bound stays as the safety net that
  fails a case instead of hanging it.
  *(Corrected 2026-09-28, by measurement: certain only once the client's read is registered before the
  peer's FIN arrives. A FIN whose epoll event is processed after the client's speculative read has
  already consumed the end of stream sets `try_speculative_` back to true (`epoll_reactor.ipp:796`), and
  the shutdown's read then completes at once — INFERRED as the mechanism of the one SimpleHttpTask run
  in the first twenty-one which ended at once on today's code (`logs/astra2/cs6/size-h1simple-httpclient13-run.log`),
  whose peer truncates straight after its response. So the peer now holds its ending until the client's
  read is registered: the tasks3 probes signal once their read is armed (`3bfdd21`), the HTTP/1.1
  driver's case waits for a marker posted behind its start handler, and the HTTP/2 driver's case for its
  strand to show no write in flight (`14cfb07`). SimpleHttpTask offers nothing to wait for; see §9's
  correction.)*
- **A peer which truncates and then closes its socket entirely**, committed with the red. The teardown
  is prompt before the fix and after. On today's code the probe that swallows the ending fails with the
  reset its close_notify drew — asked of `net::isPeerClosedErrorCode( )`, never compared by hand — and
  after the fix it ends clean. The probe that fails with the code keeps its truncation both times.
  *(Corrected 2026-09-28: measured green on today's code on Linux — see §5's correction. It is committed
  as a characterization there, and it is the red on a platform which hands the read the reset.)*
- **The drivers themselves**, committed with the red and red on today's tree. This is where the HTTP/2
  driver's hang, which §5 calls INFERRED, is first measured. An idle connection of each HTTP driver is
  truncated by the same peer. The HTTP/2 driver takes its own read loop only when HTTP/2 was negotiated
  (`Http2ConnectionTask.h:2755-2764`) — otherwise it hands the stream to the factory and completes — so
  for it the peer selects `h2` by ALPN. These cases go in a sibling module if this one's measured size
  does not allow them: it is 34.5 MB at a64 clang debug with the characterization alone
  (`logs/astra2/cs6/sizes.txt`). The existing D1 cases in `…httpclient8` run through the fixed path
  unchanged and stay in the focused set.
  *(Recorded 2026-09-28: they went to two siblings, because the three consumer cases in one module
  measured 45.0 MB — the HTTP/1.1 driver and SimpleHttpTask to `utf_baselib_httpclient13` (36.8 MB), the
  HTTP/2 driver to `utf_baselib_h2client9` (37.6 MB), reserved by the orchestrator. *(2026-09-29: MB here
  is 10^6 bytes. `src/utests/AGENTS.md` records sizes in 2^20 bytes, in which these read about 4.6% lower:
  the 45.0 MB is 45,012,488 bytes, 42.9 MB; `logs/astra4/cs9/sizes-units.txt`.)* The HTTP/2 driver
  writes its preface as soon as the handshake is done, and an ending which met that write in flight
  would take the driver's forced `initiateClose( )`, which runs no TLS shutdown at all
  (`Http2ConnectionTask.h:2873-2880`) — which is why its case waits for the write to be over.)*
- **ThreadSanitizer** over the new module at the tip, with `utf_baselib_basictask` as the positive
  control, and over `utf_baselib_httpclient8`, whose D1 truncation cases exercise the HTTP/1.1 driver's
  pre-prolog write (`Http1ConnectionTask.h:1384-1385`) and the fixed shutdown on the real driver, in the
  same instrumented tree as I13's evidence.

## 9. Files

- `src/include/baselib/tasks/AsioSslStreamWrapper.h` — the flag, its setter and getter, its reset in
  `beginProtocolHandshake( )`, `SSL_RECEIVED_SHUTDOWN` in `beginProtocolShutdown( )`, and
  `onShutdownInternal( )` keeping a skipped wait from reading as a completed closure: the assignment at
  `:331` stays literally as it is, and one statement after it sets `m_hasShutdownCompletedSuccessfully`
  to false when the record is set — the wait was skipped, so the peer's close_notify was never read. It
  can only turn `:331`'s true into false, never the reverse, and a stream whose record is clear runs
  today's code unchanged. **Owned by the brief "if the note puts the record there"**, which it does.
- `src/include/baselib/tasks/TcpSslBaseTasks.h` — the two predicates record, each guarded on
  `m_sslStream` (§4.2). Owned.
- `src/include/baselib/http/SimpleHttpTask.h:833` — **accepted by the orchestrator on 2026-09-27 as shape
  (A) applied to the legacy client's path, with the lane's ownership widened to that line.** It asks
  `base_type::isStreamTruncationError( ec )` where it asks the static `isExpectedProtocolException( nullptr,
  std::exception(), &ec )`. The two answer the same for both policies — the cleartext pair are both
  false, and the TLS pair are both `isExpectedSslErrorCode( ec )` (`TcpSslBaseTasks.h:341-344`,
  `:760-779`) — so the line changes what is recorded and nothing it decides. Without it the legacy client
  keeps I2 on its one path that ends in success — and against a server which closes its socket entirely,
  that complete response fails at once with the reset our close_notify draws (§5; *corrected
  2026-09-28: on Linux it ends clean today, see §5*). Its test is a
  `SimpleHttpSslTask` (`http/SimpleHttpSslTask.h:94`, a GET) against the same peer, answering with a
  complete Content-Length body and then truncating: red on today's tree, green after.
  *(Corrected 2026-09-28, agreed by the orchestrator: that red is probable, not certain. SimpleHttpTask
  offers nothing to wait for once it has armed its content read, so the peer cannot hold its ending as
  §8's correction describes, and the case ended at once in one run of the first twenty-one on today's
  code. Its green is certain: with the record set before the shutdown, the shutdown reads nothing. The
  certain red for this line is a second case, committed after the core fix and before the line: at the
  moment its shutdown begins, the stream has recorded the truncation — false with the core fix alone,
  since the static predicate records nothing, and true with the line.)*
- `src/utests/utf_baselib_httpclient8/TestHttp1DriverTlsTruncation.h:69-79` — **folded into CS-6 by the
  orchestrator on 2026-09-27, the lane's ownership widened to these comment lines only.** The paragraph
  records the 60 s ending as a measured property of the tree, and this change makes that false. It is
  rewritten to say the driver's teardown is now prompt after a truncation (CS-6, I2), that the cases
  still wait for the peer's script and cancel the driver, and that the cancel usually finds the task
  already ended but can still land between the driver's close_notify leaving and its shutdown handler
  running — the driver then ends as a cancel (`Http1ConnectionTask.h:2305-2320`, `TcpBaseTasks.h:123-143`)
  — which is why the cases still do not assert the driver's own ending. Comment-only, in its own commit
  after I2's logic. CS-5 edits other lines of the file
  (its peer), so the two merge as separate hunks.

## 10. Reversing conditions, and agreement

- If the maintainer wants the fix to reach **consumers outside the library** which neither ask the
  policy nor fail with the code, (b) is the wrong recording point and (a) is the answer, at the cost
  §4.2 names.
- If sending our close_notify after a truncation is found to draw a **reset** where today there is none
  — a peer which closes on it with our data unread — skipping the TLS shutdown becomes worth weighing,
  and the D1 cases would have to change with it. Nothing found so far says so: after our alert the
  policy's teardown sends a FIN, exactly as it does 60 s later today.

**Agreement: 2026-09-28.** Round 1 by fable (`CS6-I2-design-r1.md`), round 2 and its round-3 check by an
Opus reviewer (`CS6-I2-design-r2.md`); the reviewer agrees with revision 3 (`0ddfc51`) and the edits K-1
and K-2 which follow it; the orchestrator agrees. I2 is implemented to this note. *Recorded by the
orchestrator on the maintainer's explicit authorization: the lane's own commit of this line was refused
by the session's permission check.*
