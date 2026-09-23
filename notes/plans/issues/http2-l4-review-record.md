# HTTP/2 client, Layer L4: review record

**Reviewed:** 2026-09-19, read-only, at tip `97fd06c`. **Status:** RECORD. Nothing was built and
nothing was run: the three production headers, the `CryptoBase.h` and `TcpTunnelStage.h` hunks, the
engine and base-class code they lean on, the specifications and the test suites were read; one
external reference was fetched (RFC 7301, with `curl`, for its sections 3.1 and 3.2). Two defects
are worth fixing before the slices that consume the code they sit in (findings 1 and 2); one
engine-side sibling of the ordering hazard S4.2 found (finding 3) and one under-reported retryable
flag (finding 4) belong with S5.2, which is the first consumer of both; the ALPN server half is
additive as claimed and right for the fallback, with a wrong justification and one leak on misuse
(finding 5). Nothing found blocks L5 from starting once findings 1 and 2 are scheduled. The notes
for later work orders are in section 8.

**The range.** `3c7269a..97fd06c`, eighteen commits: the five merges the brief names
(`7ac9f16`, `4b3883a`, `63cb44e`, `a05d2cd`, `e26d494`) with their component commits, four record
commits which touch only notes (`15d7e15` also corrects a comment in `ClientConnection.h`, the 11/3
hunk, read and comment-only), and the manifest refresh `97fd06c`, which touches only
`inventory.json`. The deliberate-close fix `f4d51a2` and the `findSetting` fix `9e5dadc` lie
between the L3 second pass's tip `3e8f723` and the start of this range; S4.1-S4.3 depend on the
first, so its diff was read and its consequences are relied on below, but neither is reviewed here
as a slice.

**What was reviewed against.** Design 2.2, 3.1, 3.2, 3.6, 5.1, 5.2, 5.4, 5.5, 5.7, 8.1, 8.2 at tip -
that is, after S4.2's amendments to 2.2 and 5.7, whose diff over the range was read whole (33
insertions, one deletion, the two sections and nothing else); plan section 6, the four work orders
and their execution records, and the acceptance criterion S4.3 corrected; the L3 record's section 7
notes for S4.1 and S4.2; the three obligations of `beginPreHandshakeStage` as `TcpTunnelStage.h`
states them. RFC 7301 sections 3.1 and 3.2 from the fetched text. RFC 9113 sections 5.1, 5.4.1,
6.5.3, 6.8 and 8.1 from the text held in the L3 pass, not re-fetched; RFC 9112 sections 9.3 and 9.6
from reading.

## Verdict per slice

| Slice | Verdict | Basis |
|---|---|---|
| `ffcc805` - the S3.5 fix riding with S4.1 | **Conforms.** | `detail::TunnelCleartextLayer` (`TcpTunnelStage.h:1340-1395`) selects on `BASE::isProtocolHandshakeNeeded`, returns the socket itself for a cleartext policy and `getStream().next_layer()` for a TLS one; the three call sites (`:1684`, `:1704`, `:1730`) are the three deletions the numstat shows and now go through `tunnelStream()` (`:1653-1666`). For a plain policy `getSocket()` and `getStream()` are the same `tcp::socket`, so "unchanged object for object" holds. Proven where claimed: `H2Connect_SilentProxyHitsTheConnectDeadlineTests` runs the stage over the TLS stranded policy and asserts the proxy recorded `CONNECT localhost:443 HTTP/1.1` in cleartext (`TestClientConnectionTaskBase.h:1241`). |
| S4.1 establishment base + ALPN dispatch | **Conforms, with one Medium: the deadline bounds neither end of what 5.7's row says it bounds (finding 1).** | Chain `MultiOperationTaskT< TcpTunnelStageT< TcpConnectionEstablisherConnector< STREAM > > >` read against design 3.2 and the S4.1 work order's accounting warning: nothing before `continueAfterConnected` calls `beginOperation()` or ends with `END_MULTIOP` (the deadline handler is a plain `BL_NOEXCEPT` block, `:476-499`). Floor before protocol before driver is structural (`continueAfterConnected`, `:596-624`) and pinned by the event list of `H2Connect_FloorCheckPrecedesEverythingTests`. Every ALPN outcome the L2 second pass asked for is pinned: empty selection through `withoutAlpn` with an empty identifier, a non-empty one through `fromAlpn`, an unknown one refused, no driver refused, forced http/1.1 by not offering `h2`. The retry budget of 1 is recorded and not pinned (nit 7b). |
| S4.4 test peer | **Conforms; test-only, never reachable from `src/include/` (grepped).** | Ephemeral port read back from the protected acceptor in a `continueAfterResolved` override and signalled under a mutex with a bounded wait (`Http2TestServer.h:1705-1725`, `:1761-1782`); the window stall is an event (`AwaitWindowStall` returns until both receive windows are non-positive, `:1318-1333`, and every DATA frame re-enters the script); the opening delay gates the pump with `m_isWriteAllowed` (`:724-731`). The engine accessors it needs (`streamReceiveWindow`, `connectionReceiveWindow`, `unacknowledgedSettingsCount`) pre-exist at `3c7269a`, so "nothing in the engine needed changing" is verified. One record inaccuracy (nit 7d): S4.1's suite already had a TLS peer choosing ALPN through a raw `::SSL_CTX_set_alpn_select_cb`. |
| S4.3 HTTP/1.1 driver | **Conforms, with one Medium: the request's own `Connection: close` is claimed in three places and checked in none (finding 2).** | Two operations at most, the idle read always armed (which is what keeps the pending count above zero so `beginClose()` can complete the task - verified against `takeTerminalNoLock`, `MultiOperationTask.h:147-157`); both `async_*` calls balance the accounting in a catch (`:590-613`, `:658-678`); state settled before `onClosed` (`finishStream`, `:1004-1055`); trailers from the two message-completion sites and never inside the NOEXCEPT `finishStream`; interims in order before the final block. The corrected acceptance criterion is genuinely better evidence: `HttpServer` proves the negative half against a real parser and the scripted peer proves the positive half. The TLS stranded policy is compiled by the explicit instantiation (`TestHttp1ConnectionTask.h:1022-1025`). |
| `600ce4a` - the ALPN server half | **Additive holds; the selection order and the NOACK choice are right for what they serve; the RFC justification is wrong, a second call leaks, the no-overlap path is untested (finding 5).** | Numstat 280/0 and every hunk read: two statics (`CryptoBase.h:73-74`), the detail block (`:1035-1271`), two `BL_DEFINE_STATIC_MEMBER` lines (`:1377-1378`), the public wrapper (`:1491-1523`); no existing line changed, `initSsl()` untouched, the index allocated lazily under its own lock. |
| S4.2 HTTP/2 driver | **Conforms; the RST_STREAM hold-back is correct and complete for the frame it holds back; one engine-side sibling (finding 3); one under-reported flag (finding 4); the SETTINGS reconciliation at 10 s is sound; the deadline is cancelled before the preface (finding 1).** | Rules L1-L4 read: strand state is touched only from handlers holding the task lock; the command mailbox is a leaf lock with the one documented exception, whose lock order (mailbox, then accounting) has no reverse path (`applyDecision` releases the accounting lock before calling anything, `MultiOperationTask.h:163-196`); events reach a request only by calling its sink from the strand. The opening write is one `produce()` after `applyCommands()` (`:2043-2052`), pinned by the probe's `onWriteScheduled`. Graceful close is the deliberate door and completes successfully; the PING deadline and an external cancel are the other door and fail; a connection error is thrown from `chkFinishClose` after its GOAWAY has gone out (`:1861-1884`), pinned. The re-entrancy guard on the drain (`:1015-1041`) is right and the reason given is right. |
| `e26d494` - the split | **Conforms in shape; the mechanical claims are as reported.** | Step A (`1dafcd9`) cuts the TLS block into a sibling header included from the same main; step B (`4cb795c`) is a rename detected at zero changes with the two recipes moved; both new mains carry the closed-module note and the `before - (a + b)` correction. "Preprocessed TU unchanged at +0.0046 percent" and the 70 + 15 assertion counts are the lane's measurements. |

The placement and idiom checks of the plan's verification protocol pass: no `BL_DEVENV_VERSION`
test in any new header (`ClientConnectionTaskBase.h:44-45` mentions it in a comment and guards on
`OPENSSL_VERSION_NUMBER` at `:48`); `ClientConnectionTaskBase.h` reaches OpenSSL and is kept out of
`httpclient/PreCompiled.h` with the reason written in; every case in the four new suites has a
`--run_test=` recipe in its module's `notes.txt` (checked name by name); `BL_DEFINE_STATIC_MEMBER`
for the new statics. **Additive-only holds** for every pre-existing production file in the range:
`CryptoBase.h` 280/0, `TcpTunnelStage.h` 89/3 with the three deletions being the three call sites
the fix had to replace, `ClientConnection.h` 11/3 in a comment, `httpclient/PreCompiled.h` 10/0 in a
comment; `Session.h`, `MultiOperationTask.h` and `TcpBaseTasks.h` are untouched inside the range.
Design 2.2's one permitted exception - `http2/Http2ConnectionTask.h` depending on `httpclient/` -
is the only `http2/` header which does (the includes at `Http2ConnectionTask.h:24-26`; nothing else
under `http2/` names `httpclient/`).

## Findings

By severity: 1 (Medium), 2 (Medium), 3 (Low/Medium, engine side), 4 (Low), 5 (Low), 6 (Low), then
the nits in 7 and the notes in 8.

### 1. Medium - S4.1 and S4.2: the connect deadline bounds neither end of what design 5.7 says it bounds

Design 5.7's row reads *Connect: resolve through preface - 60 s - connection task*. What is
implemented is a deadline from **TCP connected** to **ALPN read**, and in one common configuration
it covers nothing at all.

**The front end.** The deadline is armed in the `beginPreHandshakeStage` override
(`ClientConnectionTaskBase.h:530-537`), which the establisher calls from inside the connect
completion handler (`TcpBaseTasks.h:1363-1404`, the call at `:1381`). The resolve (`:831`) and the
`asio::async_connect` (`:1423`) run before it. The header says so (`:521-523`) and calls both
"bounded by the operating system". That bound is not 60 s and is not one number: the query is
`all_matching` (`:723`), so v4 and v6 addresses are both returned, and `async_connect` over a
resolver range tries each endpoint in turn, each black-holed one costing the OS SYN timeout (Linux,
`tcp_syn_retries` 6, about two minutes; from memory, not measured). A host whose addresses drop
SYNs therefore holds the task for minutes per address before the deadline is ever armed - and with
a proxy configured the same applies to the proxy's address. With `DEFAULT_HANDSHAKE_RETRY_COUNT`
of 1 (`:98`) a retry then re-arms a fresh 60 s on the new socket (`:525-527`), so the establishment
bound the row promises as 60 s is `resolve + connect + 60 s`, twice, in the worst case.

**The back end, which is the one that matters for the driver.** The base's comment on
`cancelConnectDeadline()` says "a derived driver calls it once the preface is away" (`:447`). The
h2 driver calls it as the **first statement** of `onProtocolNegotiated`
(`Http2ConnectionTask.h:2005`), before the session exists (`:2017`) and before the opening write
is issued (`:2052`). So "through preface" is not implemented, and:

- **Cleartext, no proxy - the deadline covers nothing.** It is armed and cancelled inside one
  synchronous chain: `onConnectionEstablished` -> arm -> default hook -> `beginProtocolHandshake` ->
  the plain policy's helper calls `continueAfterConnected` synchronously -> `onProtocolNegotiated`
  -> cancel. Nothing asynchronous happens between the two.
- **Cleartext with a proxy** - it covers the tunnel, which is the case
  `H2Connect_SilentProxyHitsTheConnectDeadlineTests` pins.
- **TLS** - it covers tunnel and handshake, the second of which the inherited 60 s protocol timer
  already bounds.
- **The fallback path** never cancels it in `onProtocolNegotiated` (`:575-594`); `onTaskStoppedNothrow`
  does (`:642`), which is fine since that task is completing.

**What actually bounds a silent peer, and where that stops working.** After the opening write it is
the SETTINGS timer, at 10 s: `chkArmSettingsTimer` (`:1452-1487`) is called in `pumpWrites` after
`produce()` and before the `async_write` (`:1387`), so it is armed whether or not the write
completes, and `H2Driver_SettingsAcknowledgementTimeoutTests` pins that a peer which reads the
preface and never acknowledges is dropped with the GOAWAY out first. But that path ends the task
**only through the write pump**: `onConnectionErrorEvent` (`:1202-1232`) sets `m_isCloseWhenDrained`
and the close is taken by `chkFinishClose` from inside `pumpWrites` (`:1369`, `:1382`), and
`pumpWrites` returns immediately while `m_isWriteInFlight` (`:1362`). That works whenever the write
completes - which it will for the preface and for the GOAWAY, since the kernel's send buffer
absorbs a few hundred bytes even from a peer which never reads (a claim about socket buffers, not
something observed). It does **not** work for a peer which stops reading once the send buffer is
full - an upload into an advertised window the peer never drains: the stalled `async_write` is
never cancelled, the idle timer's `closeGracefully` and a request's `cancel` both end in
`pumpWrites` returning, and the task lives until an external `cancelTask()` or TCP's own
retransmission timeout, and indefinitely against a zero-window peer. The PING deadline path is the
one that works: `onPingDeadline` (`:1634-1669`) cancels through `requestCancelInternal`, and it is
armed whether or not the PING could be written (`:1587`), so keepalive on gives a bound. The
default is off (`:111`).

**The placement reasoning, corrected in one word.** The plan (section 6, S4.1, "Two decisions worth
carrying") and merge `7ac9f16` say the override arms before the base call because "after it there
is an `async_connect` in flight". After the base call there is no `async_connect` - the connect
completed before the hook was entered; what is in flight afterwards is the tunnel's first write, or
the TLS `async_handshake` started synchronously by the default hook. The header gets it right
(`:511-519`: the argument is about arming in `continueAfterResolved`, where an `async_connect`
really would be in flight). The conclusion - arm first, because a throw after the base call would
take the handler's failure path with an operation outstanding in a phase outside the mix-in's
accounting - holds for the tunnel write and the handshake exactly as it would for a connect.

**Fix, three parts, the first before S5.2.** (a) Cancel the deadline when the opening write
completes rather than before it is issued: a flag set in `onProtocolNegotiated`, and in `onWrite`
`if( flag ) { flag = false; cancelConnectDeadline(); }` - three lines, and the row's "through
preface" becomes true. (b) Either arm at schedule time - a `scheduleTask` override, the timer on
`ThreadPoolDefault::getDefault( getThreadPoolId() )->aioService()` - or amend 5.7. The strand rule
does not forbid the first: `onConnectDeadline` touches no stream state, takes the task lock and
requests a cancel (`:476-499`), which is precisely what the TLS protocol timer already does from a
timer built on `aioService()` (`TcpSslBaseTasks.h:120`, `:136-156`), and the cancel path already
handles a resolve in flight (`TcpBaseTasks.h:738-746`) and a socket not yet created. One deadline
across the retry is also what "resolve through preface, 60 s" reads as. If the row is amended
instead, it should say "TCP connected through preface" and record the per-endpoint multiplication.
(c) On `SETTINGS_TIMEOUT`, either take the PING path (cancel; RFC 9113 5.4.1's GOAWAY is a SHOULD,
and the PING path already forgoes it for the same reason) or bound the drain with a timer, so that
a connection error is never waiting on a write the peer controls.

### 2. Medium - S4.3: the request's own `Connection: close` is claimed in three places and checked in none

`deriveIsReusable` (`Http1ConnectionTask.h:365-405`) consults `m_parser->headers()` at `:384` and
`:391` and nothing else which carries a `Connection` field; `hasConnectionToken` has exactly those
two call sites (grepped). The comment above it (`:349-351`) says *"and equally any REQUEST which
carried it: design 5.5 says neither side may have said close, and the request's word binds this
client whatever the server answers"*; merge `63cb44e` says *"Connection: close from EITHER side"*;
the plan's S4.3 record repeats it. `serializeRequestHead` (`:424-463`) copies the caller's headers
through, so a caller's `Connection: close` goes on the wire and the verdict never sees it.
`Http1Driver_ReuseVerdictInputsTests` (`:1214-1313`) has five inputs and all five are
response-side; no case in the suite sets a request `Connection` header (the only request header a
case adds is the `Transfer-Encoding` at `:1565`, for the refusal).

**Consequence.** RFC 9112 9.6: a server which receives `close` *MUST* close after its final
response and *SHOULD* echo `close`. Against a server which closes without echoing, the driver
reports `Ready`, the pool may dispatch the next request onto it, its bytes reach the socket before
the FIN is noticed, and `onPeerClosed` (`:883-917`) answers with `protocol_error` and
`isRetryable = ! m_requestBytesWritten = false` - a spurious, non-retryable failure of a request
the server never saw. Narrow, because most servers echo; but it is the stated rule, the design's
rule, and the one input of the derivation with no case.

**Fix.** `m_request` is still populated when the verdict is taken (`:851`; it is reset in
`finishStream` at `:1025`), but it is guarded by `m_stateLock` (`:173-178`) and `deriveIsReusable`
takes no lock - so record the fact once, where `onStartRequest` already copies the request under
the lock (`:485-495`): a `bool m_requestSaidClose` beside `m_requestBytesWritten`, consulted at the
top of `deriveIsReusable`, reset with the rest. Plus one case in `Http1Driver_ReuseVerdictInputsTests`
whose request carries `Connection: close` and whose scripted response does not echo it.

### 3. Low/Medium - S3.1 through S4.2: a header block queued for a stream a peer GOAWAY dooms is still written, after the GOAWAY - the RST_STREAM hazard's sibling, on the engine's side

**First, the hold-back itself is correct and complete for RST_STREAM.** `isHeadersProduced` is
per stream, false on the fresh `StreamState` (`Http2ConnectionTask.h:302`, `:585`), and set for
every stream after every `produce()` (`:676-681`). Every stream in `m_streams` has its block either
in the header-block queue at that `produce()` or already written: the stream entry is created only
after `submitRequest` has queued the block (`Session.h:910-923`, `:585-607`), `produce()` takes the
whole queue (`:838-845`), and the one way a block leaves the queue unwritten is
`raiseConnectionError`'s clear (`:1497`), which also closes every stream so `applyCancel` finds
none (`:634-639`). A held cancel is dropped with its stream by `closeStream` (`:971-972`), which is
right. The reset is queued into the control queue after that `produce()` and goes out in the next
write, behind the HEADERS already in flight; one write at a time makes that an order on the wire.
`H2Driver_CancelResetsOneStreamOnlyTests` pins it on the schedules where the submit and the cancel
are drained together (nit 7c).

**No other frame the driver queues has the problem.** DATA cannot precede its HEADERS: `produce()`
writes header blocks before DATA (`:838-857`) and a stream's context exists only after its block
was queued. A stream-level WINDOW_UPDATE before HEADERS would need a `consumed()` for octets never
received, which `FlowControlWindow::onConsumed` refuses (`FlowControlWindow.h:582-587`) - as a
thrown `BL_CHK` inside `applyConsumed` (`:716`), so a request task which over-acknowledges fails
the *connection* task rather than its own stream; that is a note for S5.1, not a wire hazard. PRIORITY
on an idle stream is permitted. PING, SETTINGS and their acks are connection-level.

**The sibling.** `handleGoAway` (`Session.h:2296-2342`) calls `closeStreamsAbove` (`:3330-3356`),
which calls `forceCloseStream` (`:3389-3407`): the registry is transitioned as if a RST_STREAM had
been sent, and `m_headerBlockQueue` is not touched - only `raiseConnectionError` clears it. A block
is a bare `wire_buffer_t` with no stream identifier, and `produce()` writes every one
unconditionally. So: a request is submitted while a write is in flight (its block queued,
`isHeadersProduced` false), and a peer GOAWAY arrives with `last_stream_id` below its id. The engine
closes the stream retryable (`isRetryable`, `:3312-3328` - correct), the driver tells the sink
(`onStreamClosedEvent`, `:1164-1188`; a pool replays it on another connection), `closeGracefully`
queues our GOAWAY (`:1835`), and the next `produce()` writes **[our GOAWAY][the stale HEADERS]**.
RFC 9113 6.8: receivers of a GOAWAY *MUST NOT* open additional streams, and the peer "will ignore
frames sent on streams initiated by the receiver if the stream has an identifier higher than the
included last stream identifier" - so a conforming peer raises nothing. But the bytes of a request
the client has already declared unprocessed and retryable are sent to the origin after the origin
said stop; a non-conforming or merely buggy peer which acts on them turns the replay into a
duplicate. The window - a write in flight when the GOAWAY lands - is narrow and, under load,
ordinary.

Same shape as the hazard S4.2 found: the header-block queue lags the stream state. **The fix is the
engine's**: give the queue entries their stream id (`std::pair< std::uint32_t, wire_buffer_t >`)
and have `forceCloseStream`, or `closeStreamsAbove`, drop the doomed stream's block the way
`raiseConnectionError` drops all of them. Additive in shape, but `Session.h` is landed core and it
changes an existing path, so by the house rule it is its own small change-set, gated on the
`h2core` suite - with a case that queues a block, feeds a GOAWAY below it and asserts the next
`produce()` carries no HEADERS.

### 4. Low - S4.2: a stream whose HEADERS were never handed to a write is reported non-retryable on peer close and on task stop

`onPeerClosed` (`Http2ConnectionTask.h:1325-1341`) and `onTaskStoppedNothrow` (`:2091-2122`) call
`closeAllStreams( ..., false /* isRetryable */ )`. The comment (`:1319-1322`) - *"Every stream
still live was written and may have been processed"* - is false for a stream whose block is still
in the header-block queue because a write was in flight when it was submitted. Its
`isHeadersProduced` is false, which is exactly the information design 5.4's third limb needs: *"the
connection failed before any byte of the request was written"*. Reachable: submit while a write is
in flight, then EOF or reset. `closeSubmissions` gets the neighbouring case right (`:1916-1920`,
retryable, "never reached a stream"); this is the same fact one step later.

**Fix.** Derive the flag per stream: `closeAllStreams( errorCode )` with
`isRetryable = ! state.isHeadersProduced`. The flag is the conservative side of the line - a block
*handed* to a write which then failed part way is not provably unwritten, and that stream stays
non-retryable, which is right.

### 5. Low - the ALPN server half: additive holds and the semantics are right for what they serve; the justification mis-cites the RFC, a second call leaks, and the no-overlap path is untested

**What is right.** Strictly additive, as the verdict table records. The callback walks the server's
list in order and takes the first entry the client also offered, with the walk bounded by `inLen`
rather than by the length octets (`CryptoBase.h:1170-1210`) - RFC 7301 3.2's *"SHOULD select the
most highly preferred protocol that it supports and that is also advertised by the client"*, and
`H2Driver_TlsAlpnServerPreferenceWinsTests` pins that a server preferring `http/1.1, h2` answers a
client offering `h2, http/1.1` with `http/1.1`. The list lives in the context's ex_data so the
pointer handed back at `:1203` outlives the callback. The client side of NOACK is handled and
pinned: `getAlpnSelected()` returns empty for a zero-length selection
(`AsioSslStreamWrapper.h:833-846`), `negotiatedProtocol` maps empty to `withoutAlpn( Http11 )` and
never reaches `fromAlpn( "" )` (`ClientConnectionTaskBase.h:229-236`), and
`H2Connect_TlsWithNoAlpnSelectionFallsBackTests` drives exactly that with a peer whose raw callback
returns `SSL_TLSEXT_ERR_NOACK` (`TestClientConnectionTaskBase.h:292-295`), asserting the empty
identifier and one fallback creation. So "handshake completed, nothing selected" is representable
and is what the fallback receives.

**What is wrong.**

- **The citation.** The header (`:1053-1054`), the merge and the plan say RFC 7301 3.2 "also
  allows" the fatal alert. The fetched text: *"In the event that the server supports no protocols
  that the client advertises, then the server SHALL respond with a fatal 'no_application_protocol'
  alert."* The alert is not a permitted alternative; it is what 3.2 *requires* of a server which
  implements ALPN and finds no overlap. What NOACK models is 3.1's server, which *"MAY return a
  suitable protocol selection response"* and did not - a server that did not act on the extension,
  which is what the library's own `HttpServer` is and what design 5.5's fallback exists for. The
  choice is right for a test peer and right for the client; the sentence should say that, and say
  that a production server built on this entry point would be non-conforming on no-overlap. That
  belongs in `http2-server-side-deferral.md` as well, since it is the first server-side OpenSSL
  entry point in `src/include/`.
- **A second call leaks.** The comment at `:1254-1256` claims a list stored by a previous call "is
  freed by the ex_data free callback when the context goes away". It is not: `SSL_CTX_set_ex_data`
  overwrites the slot's pointer, and `freeAlpnPreference` runs once, at `SSL_CTX_free`, on the
  final pointer. The first list leaks. Freeing it here would be worse - a handshake on another
  thread may be reading it - which is the real reason to refuse a second call: `BL_CHK` that the
  slot is null. A leak on a misuse the comment says does not happen; Low.
- **The NOACK branch of this callback is unexercised.** Both h2client3 cases overlap. The S4.1
  case above pins the client against a *different* callback. One case with a client offering
  `http/1.1` alone against a peer preferring `h2` alone - the forced-http/1.1 configuration already
  exists - would pin this code's `:1211` and the fallback through `setAlpnServerPreference` in one
  go.
- Nit: `alpnPreferenceExIndex()` takes `g_alpnPreferenceLock` on every ClientHello (`:1160` via
  `:1096`). Fine for a test peer; a production server would read the index once.

The 1.1.1w half of the acceptance is owed, not claimed, and the deferral record's addition says so
accurately, including that the callback is passed with no cast exactly as `SSL_set_msg_callback` is.

### 6. Low - "correct over all four policies" is claimed twice and is a fact at most once

`Http1ConnectionTask.h:67-70` says every post goes through `getSocket().get_executor()`, "which is
the strand under a stranded policy and the I/O service under a plain one, and therefore correct over
all four policies". Posting to an `io_service` is not a serialization. `onStartRequest`
(`:478-616`) runs from a plain `asio::post` with no task lock and touches `m_parser`,
`m_requestHead`, `m_requestBody` and `m_bodyChunk`; under a plain policy `onReadCompleted`
(`:919-953`), which holds the task lock but runs on whichever I/O thread the read completes on, can
run concurrently with it. The class is sound on a strand - which is what design 5.1 prescribes - so
the fix is the sentence, not the code: "on a stranded policy". The plain policies are not
instantiated anywhere: the cases run the cleartext stranded policy and the explicit instantiation
names the TLS stranded one (`TestHttp1ConnectionTask.h:1022-1025`).

`ClientConnectionTaskBase.h:296-298` makes the same claim, and there the code really is
executor-agnostic - but it is instantiated over the two stranded policies only
(`TestClientConnectionTaskBase.h:772-776`), so by the rule S4.3 itself set ("a template nothing
instantiates is not compiled"), "correct over all four" is a claim and not a fact.

### 7. Nits - preferences and small inaccuracies, not defects

- **(a)** The `async_connect` sentence in the plan's S4.1 record and in merge `7ac9f16` - see
  finding 1; the header is right and the two records are not.
- **(b)** `DEFAULT_HANDSHAKE_RETRY_COUNT = 1` is recorded and not pinned: no case in the S4.1 suite
  names `maxHandshakeRetries` or counts handshakes against a peer which truncates.
- **(c)** `H2Driver_CancelResetsOneStreamOnlyTests` submits and cancels back to back (`:833-838`).
  When the strand drains both in one batch the hold-back runs; when it drains the submit first and
  `pumpWrites` produces the block before the cancel arrives, the direct path runs. Both pass, so the
  case does pin the property on the schedules that matter, but not deterministically. A submit made
  while a write is known to be in flight - behind an upload with `setWithholdWindowUpdates` - then
  a cancel, then an assertion that the peer recorded HEADERS before RST_STREAM, would pin it every
  run.
- **(d)** S4.4's record and the h2client3 header (`TestHttp2ConnectionTaskTls.h:61-63`) say no TLS
  test peer was possible before `setAlpnServerPreference`. S4.1's suite already had one selecting
  ALPN through a raw `::SSL_CTX_set_alpn_select_cb` (`TestClientConnectionTaskBase.h:244-248`); the
  statement about `src/include/` is exact, "no TLS peer is possible" is not. S4.4 ran in parallel
  with S4.1 and may not have seen it.
- **(e)** One contract, two answers to a null sink: `Http2ConnectionTaskT::submit` throws
  `ArgumentException` (`:2135-2141`), `Http1ConnectionTaskT::submit` returns
  `INVALID_STREAM_HANDLE` (`:1262-1265`). S5.1 will expect one of them.
- **(f)** `http2/PreCompiled.h` does not list `Http2ConnectionTask.h` and, unlike
  `httpclient/PreCompiled.h` (which S4.1 annotated for `ClientConnectionTaskBase.h`), does not say
  so. The same reason applies - it reaches OpenSSL through the base - and design 2.2's exception
  paragraph is where a reader would look next.
- **(g)** Design 5.7's third reason for 10 s - the acknowledgement "is emitted by the peer's
  protocol layer rather than by its application" - is equally true of a PING ack, so it argues for
  10 s on both rows rather than for 10 against 15. The first two reasons carry the decision, and
  the decision is right: one number, the engine's, with the driver holding no second copy (verified
  - `chkArmSettingsTimer` reads `limits().settingsTimeoutInSeconds`, `:1470-1474`, and the case sets
  it to 1 and dies on it).
- **(h)** With `keepAliveInterval` on and `keepAlivePingReplyTimeout` disabled, a peer which never
  acknowledges silently ends the keepalive after one PING: `chkArmKeepAlive` is re-entered only
  from `onPingAcknowledged` (`:1627-1632`). Harmless; worth one sentence in
  `Http2ConnectionConfig`.
- **(i)** `errorCodeOf` (`:908-928`) folds every HTTP/2 error code other than `NO_ERROR`, `CANCEL`
  and `REFUSED_STREAM` into `errc::protocol_error`, and the closure carries no `errinfo`. A request
  task cannot tell `ENHANCE_YOUR_CALM` or `HTTP_1_1_REQUIRED` from a malformed response. Fine for
  L4; S5.1 may want the raw code on the event.

### 8. Notes for later slices

- **S5.1:** a `consumed()` for more octets than arrived fails the *connection* task, not the stream
  (finding 3's aside); `cancel()` on HTTP/1.1 ends the connection, as S4.3 records; a held-back
  cancel is acknowledged to the sink only after the next write completes, so a request task must
  complete on its own deadline and not wait for `onClosed` - which is what 5.7 already says; the
  body-pull gap the plan assigns; nits 7(e) and 7(i).
- **S5.2:** finding 1's timing - a `Connecting` placeholder can stand for minutes on a black-holed
  host - and finding 1(a) before the pool starts relying on a connection task's failure time.
  Finding 4 before replay is implemented. Two things about the fallback path: a request submitted
  to an h2 task before ALPN resolves is failed with `connection_aborted`, retryable, when the peer
  selects `http/1.1` (`onTaskStoppedNothrow` -> `closeSubmissions`, `:2109`, `:1916-1920`), so the
  pool must re-dispatch it to the driver the task exposes through `connection()`; and that task's
  `state()` reads `Closed` afterwards (`:2117`) while the connection lives on in the fallback
  driver, so the pool must take the driver from `connection()` and never the task's own state as the
  connection's. Finding 3 lands in the engine before the pool replays on GOAWAY.
- **Server deferral (`http2-server-side-deferral.md`):** finding 5's first point - the entry point's
  no-overlap behaviour is the fallback's, not RFC 7301 3.2's.
- **S7.3:** the ALPN offer is per connection (`ClientConnectionConfig::alpnOffer`) and the
  preference is per context; a profile which shapes the offer goes through the first.

## What was verified versus inferred

Verified by reading the code against the design and the RFCs: every row of the verdict table's
basis column; the whole of `ClientConnectionTaskBase.h`, `Http2ConnectionTask.h` and
`Http1ConnectionTask.h`; every hunk of `CryptoBase.h` and `TcpTunnelStage.h` in the range; the
engine's `produce`, `wantsWrite`, `submitRequest`, `queueHeaderBlock`, `resetStream` and
`sendRstStream`, `queueGoAway`, `raiseConnectionError`, `handleGoAway`, `closeStreamsAbove`,
`forceCloseStream`, `isRetryable`, `onTimer`, `applyLocalSettings`, `consumed`; the mix-in in full,
including that `beginOperation()` after the terminal is benign (`takeTerminalNoLock` refuses a
second terminal and the posted handler answers the command through `canOpenStream()`); the
establisher's `onConnectionEstablished`, `continueAfterResolved`, `scheduleTaskFinishContinuation`,
`startConnectionEstablishingInternal`, `cancelTask` and `detachStream`; the TLS base's protocol
timer, `hasHandshakeCompletedSuccessfully`, `isShutdownNeeded` and `onTaskStoppedNothrow` - which is
what makes the h2 fallback over TLS safe with `isCloseStreamOnTaskFinish( true )` set: once the
stream is detached `m_sslStream` is null, so no shutdown is attempted on a stream the task no
longer owns (`TcpSslBaseTasks.h:631-634`, `:646-654`, `:518-521`); the handler macros;
`FlowControlWindow::onConsumed`; the stranded policies' `attachStream` consequence
(`TcpStrandedStreams.h:83-86`). Verified by fetching: RFC 7301 3.1 and 3.2. Verified by
`git diff --numstat 3c7269a 97fd06c -- src/` and by reading the diffs: the additive claims, and that
`Session.h`, `MultiOperationTask.h` and `TcpBaseTasks.h` are untouched inside the range. Verified by
grep: no test-peer header reachable from `src/include/`; no `BL_DEVENV_VERSION` test in any new
header; every new case has a recipe; `hasConnectionToken` has two call sites and both are on the
response; the three engine accessors the peer uses exist at `3c7269a`. Verified by reading the
tests: what every case named above asserts, the raw ALPN callback of S4.1's peer, the fallback
factory's record, the probe's write capture, and that the split's step B is a rename detected at
zero changes.

Inferred or recalled: the SYN-timeout figures in finding 1 (Linux `tcp_syn_retries` 6, about two
minutes) are from memory; that the kernel send buffer absorbs the preface and the GOAWAY is a
claim about socket buffers and not an observation; RFC 9113 sections are from the text held in the
L3 pass and were not re-fetched; that `TunnelCleartextLayer` and the explicit instantiation compile
over the policies named is the lane's, since nothing was built; that `asio::async_connect` over a
range tries every endpoint in turn is from the Asio documentation as remembered.

## What was not checked

- **No build and no test run.** Every "clean under clang and gcc at release", every TSan result,
  every object size, the 70 + 15 assertion counts and the "+0.0046 percent" of the split are as
  reported. The negative control S4.4 describes (the rendezvous degraded to an immediate read)
  exists in the lane's account, not in the tree.
- **`Http2TestServer.h`, `RawFrameScriptPeer.h` and `Http2DriverTestUtils.h`** were indexed and
  read at the points cited - the port readback, the opening delay gate, `AwaitWindowStall`, the
  fallback driver and factory, the probe - not in full. **`TestHttp2TestPeer.h`** was not read; its
  five cases are counted, not reviewed.
- **The h2 driver's upload path** beyond `pumpBody` and `H2Driver_FullDuplexUploadAndDownloadTests`;
  the interaction of `pendingUpload` with a `BodySource` is the gap the plan hands to S5.1 and was
  not traced further.
- **`ClientTypes.h`** and `NegotiatedProtocol::fromAlpn`'s mapping were not re-read; S2.6's, as the
  L2 review left them. **`Http1Codec`** and Beast behind `needsEof()`, `isComplete()` and
  `interimResponses()`, likewise.
- **The deliberate-close fix `f4d51a2`** was read as a diff and relied on; it was not reviewed as a
  change-set, and neither was `9e5dadc`.
- **Windows**, and the **1.1.1w flavor**, whose debt the deferral record states correctly for the
  one new OpenSSL entry point.
- **The object sizes** were not re-measured; the h2client3 "0.6 MB of headroom" is the lane's.

## Second pass: the fix round `97fd06c..013ed0b`, tip `013ed0b` (2026-09-19)

**Verdict: the six findings are carried out as described and every departure from what was
specified is the right call; nothing new in the production code; three things unresolved, all
about evidence rather than behaviour, and one premise in the tooling merge which the tree does not
support.** Read in full: the five merges' production diffs (`Http2ConnectionTask.h` 183/10,
`Session.h` 53/4, `CryptoBase.h` 50/16, `Http1ConnectionTask.h` 39/4, `ClientConnectionTaskBase.h`
5/3, comment-only), the six test diffs, the tooling diff and the whole of `utf_runlog.py` and
`check_split.sh` around it, the design, plan and deferral-record diffs, and every commit message in
the range. Two external sources were fetched and one local one read: OpenSSL's `openssl-3.5` branch
`ssl/statem/statem_srvr.c` (for departure 3), Boost 1.90's `plain_report_formatter.ipp` from the
devenv7 dist itself (for departure 5), and the two tracked runlog baselines, loaded read-only with
`python3` to list their modules. Nothing was built and nothing was run; the module results
(12/12, 9/9, 3/3, 67/67, 6/6 under both release toolchains) and the 973-case manifest are as
reported, and the manifest diff shows exactly the two new cases and nothing lost.

**Departure 1 - finding 1b: the doc amendment over arming at schedule time is the right call, 5.7
now describes the code, and the deferral has no owner.** The row reads *Connect: TCP connected
through preface - 60 s, per attempt*, and that is what the code does: armed in the connect
completion handler (unchanged), disarmed when the opening write completes -
`m_isPrefaceWritePending` is set in `onProtocolNegotiated` (`Http2ConnectionTask.h:2178`) and read
on `onWrite`'s success path (`:1473-1484`), so a write which never completes is ended by the
deadline and one which fails is ended by the handler with the base disarming from
`onTaskStoppedNothrow` (`:642` in the base, unchanged) - and re-armed per attempt by the override,
as before. The paragraph under the row states the front-end gap exactly, names the query as
`all_matching`, the per-address multiplication and the retry's second 60 s, and records the
schedule-time alternative as deferred with the reason, not rejected. On the merits the lane's
ground is better than the finding's: `onConnectDeadline` is a per-task backstop for a phase the
task owns, while a bound on "get me a connection" is a per-key policy which has to be weighed
against the retry limb of 5.4 and the request's own 30-minute total - and that is the pool's, which
does not exist yet. The process ground (the arming point lies in a module the lane's brief did not
assign, so the change could not have been validated where it matters) is sound too.

Two things follow from deferring and one of them is missing. Until S5.2 builds a pool-level bound,
a black-holed origin holds a `Connecting` placeholder - and, by 5.4, every request for that key
queued behind it - for `addresses x ~134 s`, twice on a TLS retry, with the request's 30-minute
total the only bound. That is the cost of the deferral and it is acceptable; but S5.2's work order
(plan section 7) carries nothing about it - grepped for "establish", "deadline", "schedule",
"Connecting": the only hits are the placeholder sentence and the retry bullet. Design 5.7 says the
bound "belongs with the retry policy, which design 5.4 gives to the pool"; neither 5.4's knob list
nor S5.2's deliver line has it. A deferral with a reason and no ledger entry is half of what the
house rule asks for. **Unresolved: one bullet in S5.2 - "an establishment bound across resolve,
connect and retry, on the pool's `aioService()`, the way `TcpSslBaseTasks.h:120` builds its
protocol timer; the connection task's own deadline starts at TCP connected (5.7)".** A nit beside
it: `ClientConnectionTaskBase.h:521-523` still reads "Both are bounded by the operating system"
with no multiplication and no pointer to 5.7, which now carries the number.

**Departure 2 - finding 1c: bounding the drain rather than taking the PING path is right, and the
flagged consequence is real, diagnostic only, and cheaply avoidable.** The reasoning checks.
`H2Driver_SettingsAcknowledgementTimeoutTests` asserts `sawGoAway` (`TestHttp2ConnectionTask.h:662-679`),
so a `SETTINGS_TIMEOUT` which cancelled the socket at once would have failed a pinned property -
the GOAWAY first, RFC 9113 5.4.1's SHOULD - and the finding's own text had conceded that GOAWAY. And
the two other setters of `m_isCloseWhenDrained` end in `pumpWrites` returning exactly as the error
path does, so a fix on `SETTINGS_TIMEOUT` alone would have left the idle close, the GOAWAY-received
close and a cancel's reset unbounded. There are exactly two setters and both arm the drain:
`closeGracefully` (`:1983`, armed at `:1990` after `cancelTimers()` at `:1987`, which is the right
order since `cancelTimers()` now cancels the drain timer too, `:1869-1872`) and
`onConnectionErrorEvent` (`:1278`, armed at `:1285`). `armDrainDeadline` creates the timer once
(`m_drainTimer ||` is the "a connection drains once" guard), on the strand, inside the accounting;
the disarm path is `chkFinishClose` -> `beginClose()` -> the handler's `END_MULTIOP` ->
`initiateClose()` -> `cancelTimers()` (`:2245-2255`), and the cancelled wait's own `END_MULTIOP`
balances the `beginOperation()`. On the strand the two orders of "GOAWAY write completes" and
"drain deadline fires" both resolve cleanly: `chkFinishClose` first makes `isClosing()` true and the
later `onDrainDeadline` skips; the deadline first cancels, and the already-successful `onWrite`
throws `operation_aborted` from `BL_TASKS_HANDLER_CHK_CANCEL_IMPL`. `DEFAULT_DRAIN_TIMEOUT_IN_SECONDS
= 10L` (`:93`), on by default (`:131`), with the reason written at `:64-68` - the one duration here
which is the task's own backstop rather than a policy from above, which is the right distinction.
The row in 5.7 says the same and says the GOAWAY is still sent first wherever it can be, which is
true on every path.

The consequence the lane flagged is exactly as stated, and slightly worse than "the reason is lost".
`onDrainDeadline` ends the task through `TaskBase::requestCancelInternal()` (the PING path's
ending), so the write's abort is recorded as the task's first error **with `isCanceled()` true**,
which `TcpConnectionEstablisherConnector::isExpectedException` (`TcpBaseTasks.h:1478-1488`)
classifies as an *expected* exception: a peer which stopped reading is reported to the queue and to
the log as a benign cancel, and `m_connectionErrorReason` is never thrown because `chkFinishClose`
(`:2004-2044`) is never reached. The pool is not misled - `isFailed()` is true, `state()` is
`Closed`, the streams carry `timed_out` - so it is Low, diagnostics and classification. The shape
`chkFinishClose` already uses avoids it at no cost: in `onDrainDeadline`, `base_type::beginClose()`
and then `BL_THROW` an `Http2ProtocolException` carrying `m_connectionErrorReason` when there is
one and "did not drain" when there is not. The exception is not an abort, so it becomes the first
error; `initiateClose()` then cancels the socket and the timers; the write's abort arrives after a
first error and is not reported; `isCanceled()` stays false. Two notes, neither a defect:
`onDrainDeadline`'s `closeAllStreamsUnwrittenRetryable( timed_out )` is a no-op in practice, since
every path to `m_isCloseWhenDrained` has an empty stream table by then (the engine's `StreamClosed`
events precede its `ConnectionError`, and the three callers of `closeGracefully` all test
`m_streams.empty()`); and the drain deadline is unpinned, for a reason given under "evidence".

**Departure 3 - finding 5.2: refusing a second call is the right shape, and the use-after-free
argument is verified in OpenSSL's source, not just plausible.** `tls_handle_alpn`
(`ssl/statem/statem_srvr.c:2243-2298` on the `openssl-3.5` branch, fetched) calls the callback at
`:2250-2254` and then, after it has returned, reads the pointer it was handed **three times**:
`OPENSSL_memdup( selected, selected_len )` at `:2258`, `memcmp( selected, ... )` at `:2273`, and a
second `OPENSSL_memdup` at `:2290`. Those reads are on the handshake's own thread, so a list freed by
`setAlpnServerPreference` on another thread between the callback's return and `:2290` is a
use-after-free, and so is a free during any concurrent handshake's walk of the list inside the
callback. Nothing in OpenSSL synchronises a context's ex_data against its handshakes, so there is no
safe moment to free and therefore no safe replacement; the `BL_CHK` on the slot being null
(`CryptoBase.h:1259-1276`) is the only correct answer short of a lock every ClientHello would take.
The comment's phrasing - "OpenSSL reads it after the callback has returned" - is literally true of
`:2258`. Two notes: the check reads the slot without the global lock, so two threads making the
*first* call on one fresh context concurrently could both pass it - a misuse of a misuse, noted and
not counted; and the refusal is unpinned (no case calls twice), also under "evidence". The citation
half is right in all three places (`:1052-1070`, `:1532-1545`, the deferral record's item 3): 3.1's
"MAY return" server is what NOACK models, 3.2's SHALL is what a conforming server owes, and the
sentence a production server would need a second entry point is where it belongs.

**Departure 4 - the 134 s figure is stated as this host's, not as a universal.** 5.7 says
*"measured at 134 s on a Linux host with the default `tcp_syn_retries` of 6"*, which qualifies it by
operating system and by the sysctl that produces it, and the sentence before it makes the general
claim ("neither 60 s nor one number") rather than the number. The arithmetic is consistent: six
retransmissions at 1, 2, 4, 8, 16 and 32 s and the final 64 s wait give 127 s nominal, and 134.4 s
is that plus timer granularity and the connect call's own accounting - from memory of the Linux
schedule, not measured here. One clause would close the last reading it admits: other operating
systems use shorter SYN schedules (Windows retransmits twice, macOS for about 75 s - recalled, not
verified), so the multiplication by address count is the point and the number is illustrative.
Nit.

**Departure 5 - the tooling change-set went past its brief in the right direction, and not far
enough in the one that matters; and one of its premises is not in the tree.**

*The wording coverage is complete.* Boost 1.90's `plain_report_formatter::test_unit_report_start`
(`boost/test/impl/plain_report_formatter.ipp:107-116`, read from the devenv7 dist) chooses between
exactly five strings - `has passed`, `was skipped`, `has timed out`, `was aborted`, `has failed` -
and `REPORT_VERDICT` (`utf_runlog.py:73`) names all five. Covering all five rather than the two the
brief named was right: `has timed out` would have fallen through the same way.

*The `reported` guard is sound for what it guards.* A case with an `Entering`/`Leaving` pair and no
recognised verdict is scored `unknown` when the run printed any verdict at all (`:198-201`), and
`unknown` differs from every baseline outcome, so a wording drift on a baselined case surfaces as
`OUTCOME CHANGED` instead of reading green. Keeping the `passed` default for a run which printed no
verdicts is also right: make's own `UTF_FLAGS` (`projects/make/common.mk:204-205`) carry
`--log_level=test_suite` and `--catch_system_errors=no` and **no `--report_level`**, so every log
`check_split.sh` parses in its `--parse-logs` branch (`:206-213`) is at Boost's default `confirm`
level, at which `test_unit_report_start` is never called for a case - the only lines are
`*** No errors detected` or `*** N failure(s) ... detected` (`plain_report_formatter.ipp:170-200`).
Flipping the default would indeed have reported 741 outcome changes on every such comparison.

*The differential choice is right for `exit` and wrong for `clean` and `failures`, and its stated
premise is not where it says it is.* `exit` must be differential: a `--parse-logs` snapshot has none,
and `utf_baselib_jni` does exit 200 on this host after every case passes - a known host property,
recorded in this project's memory. But "utf_baselib_jni exits 200 in both committed baselines" is
not true of the tree: the two tracked baselines, `runlog.json` and `runlog-pass2.json`, hold the
same 17 modules, all 741 cases `passed`, **every module at exit 0, and no `utf_baselib_jni` at all**
(loaded and listed; `grep -c jni` on both files is 0), and the plan already records that this
baseline "holds 17 modules and 741 cases against 30 and 795 today, and was built for the split
work's family-scoped comparisons rather than as a whole-tree gate" (`:383-385`). Whatever captures
the lane examined live outside the tree, and the merge message should say so. And the jni fact
justifies only the `exit` half: a run which exits 200 after every case passed prints
`*** No errors detected` (Boost's 200 is `exit_exception_failure`, an exception outside the test
cases - recalled, not verified against Boost's source), so its `clean` is true and its `failures`
is `None`, and an *absolute* check on those two would never have fired on it. Making all three
differential gave away the one verdict a confirm-level log carries.

*Can a run still be scored green that should not be? Yes, and by the same route the L4 gate took.*
`compare()` (`:406-499`) is differential in every branch that can see a failed case: outcomes are
compared over `set( old_cases ) & set( new_cases )` (`:443`), and the new exit, clean and failures
checks over `set( before ) & set( after )` (`:484`). Nothing absent from the baseline side can ever
fail it. A module added in a round - which is what every layer of this feature has done and what
the plan already tells S5.2 to do with `utf_baselib_h2client4` - has no case in `old_cases` and no
record in `before`; its `aborted`, `failed`, `timed out` or `unknown` outcomes are never looked at,
its `*** 1 failure is detected` is never looked at, and the one absolute check in the function,
`incomplete` (`:469-472`), fires only when a case entered and never left, which an aborted case
does not do. What such a module produces is a `NEWLY RUNS` line per case - a *failure* line
(`:433-434`), so the tool exits 1 - and a reader who knows the module is new reads past exactly
those lines, which is how a round that adds a module trains its reader to accept a red tool as
green. That is the shape of the L4 gate's miss, and the fix round has narrowed it (the aborted case
would now carry `aborted` instead of `passed`) without closing it (nothing compares that `aborted`
to anything). **Fix, two one-line absolute checks on the `after` side, both immune to the jni
case:** any case whose `outcome` is not `passed` or `skipped` is a failure (`OUTCOME: aborted`
reads differently in kind from `NEWLY RUNS`), and any module whose `failures` is a positive number
is a failure. Either alone would have turned the L4 gate red on the h2client2 abort: the first from
a `--run` log, the second from a make log. Keep the three differential checks beside them for the
change-detection they were added for.

*One more, structural.* Because the committed runlog baseline predates the whole feature, every
HTTP/2 module is compared per case only against whatever capture the previous layer's gate left
outside the tree, and never against anything committed; the inventory manifest is committed and
refreshed every round, the runtime baseline is not. Capturing `runlog.json` at each layer tip - or
at least committing the L4 capture beside `inventory.json` - would give the next gate a baseline
that contains the modules it is gating.

**The rest of the round, checked.** Finding 2: `m_requestSaidClose` (`Http1ConnectionTask.h:182`)
is set in `onStartRequest` from the local copy taken under `m_stateLock` (`:514-527`), consulted
first in `deriveIsReusable` (`:384-387`), cleared in `finishStream` with `m_requestBytesWritten`
(`:1077`); it is strand-owned like its neighbour, so the lock argument is the right one and
`serializeRequestHead` is rightly untouched. The sixth input (`TestHttp1ConnectionTask.h:1344-1389`)
is the case the finding asked for - a request saying close, a response saying nothing - and it
asserts the token reached the wire before asserting the verdict, which is what makes the negative
control the lane describes precise; `runExchange` grew a defaulted `requestHeaders` parameter and
records the whole request through the peer's own lock. Finding 3: `QueuedHeaderBlock` carries the
id (`Session.h:406-410`, `:430`, set at `:3640-3642`), `produce()` reads `.frames` (`:855`),
`dropQueuedHeaderBlocks` erases in place and moves nothing past anything (`:3404-3418`), and it is
called first in `forceCloseStream` (`:3436`) so `closeEveryStream`'s callers are covered by
construction, as the merge says; `Session_GoAwayDropsQueuedHeaderBlockTests`
(`TestSession.h:3550-3675`) pins both halves - the doomed block gone and `wantsWrite()` false, and
a surviving block for a stream at or below the last id still written, by stream id, with that
stream then completing normally. Finding 4: `closeAllStreamsUnwrittenRetryable`
(`Http2ConnectionTask.h:1030-1047`) derives per stream from `! isHeadersProduced`, is used from
`onPeerClosed` (`:1385`), `onTaskStoppedNothrow` (`:2288`) and the new `onDrainDeadline`, and the
two-argument form is kept for `chkFinishClose` and the PING deadline, where "handed to a write"
is the right answer; the corrected comment at `:1373-1379` is accurate. Finding 5.3: the no-overlap
case (`TestHttp2ConnectionTaskTls.h:304-354`) offers `http/1.1` alone against `h2` alone, so
`alpnSelectCallback`'s `NOACK` return is reached through `setAlpnServerPreference`, and it asserts
the empty identifier at both the task and the fallback record, with the overlapping sibling as the
counterfactual; the recipe is in `notes.txt`. Finding 6: comment-only in both headers, the h1
sentence now says why a plain policy would be a lock change and not a comment change
(`Http1ConnectionTask.h:68-81`), and the base's says what is compiled
(`ClientConnectionTaskBase.h:294-300`); the lane's refusal to instantiate over a plain policy is
better than the finding's hint, for the reason it gives. The plan's S4.3 enumeration is now the
code's, input for input (`e382777`); the S4.1 bullet no longer claims an `async_connect` in flight
and says the header always had it right; the deferral record's item 3 carries finding 5's first
point. The flake fix (`c5da33e`): `awaitStreamClosed()` is a script step
(`Http2TestServer.h:359-368`) re-evaluated on every read and write (`:1404-1420`), waiting on the
peer session's own `StreamClosed` for the stream (`:1061-1074`, any error code), placed between
`endStream()` and `closeConnection()` in the one case which uploads (`TestHttp2ConnectionTask.h:364`);
the diagnosis - `endStream()` closes the response half, the request half closes on the client's
schedule, `closeConnection()` tears down when the peer's queue is written - is right, the driver is
not at fault, and the lane's candour that the 160 green runs are a regression check and not a
reproduction is the standard this record asks for. The manifest: 973 cases, exactly the two new
names, nothing lost.

**New or unresolved.**

- *Unresolved, ledger:* the deferred establishment bound has no owner - one bullet in S5.2
  (departure 1).
- *Unresolved, evidence:* four behaviours the round added are pinned by no case, and the round was
  not run under TSan (below).
- *New, tooling:* `compare()` has no absolute after-side check, so a module absent from the
  baseline cannot fail it; the jni premise is mis-cited; `clean` and `failures` are differential
  without need (departure 5).
- *New, Low:* a drain-deadline expiry reports `operation_aborted`, classified expected, and drops
  the connection-error reason (departure 2).
- *Nits:* `ClientConnectionTaskBase.h:521-523` and the one clause on the 134 s; the unlocked
  first-call race on the ALPN slot.

**What else in this layer rests on evidence that thin.** Measured against the rule the layer set
for itself - S4.1's "a template nothing instantiates is not compiled" and S4.3's "a claim into a
fact" - the following are claims:

1. **Four of the round's own behaviours have no pin**, grepped: no case names `drainTimeout`,
   `isPrefaceWritePending`, the retryable flag on peer close, or a second `setAlpnServerPreference`.
   Two are hard to pin on loopback and the record should say why rather than leave it implicit:
   the preface-cancel (1a) and the drain deadline (1c) are observable only when a write stalls,
   which loopback does not produce without clamping the client socket's `SO_SNDBUF` below the
   opening write - possible through `tryConfigureConnectedStream`, and worth one case if a stalled
   peer is ever to be more than reasoning. Two are cheap and should be added: finding 4 (submit a
   second request behind an in-flight write, have the peer close, assert the second sink's
   `isRetryable`) and the refusal (call twice, expect the throw). Each was validated only by the
   existing suites not regressing, which pins nothing about it.
2. **No TSan run is recorded for the fix round.** S4.2's "clean over four runs" predates a new
   timer, a new flag on the strand and a new per-stream loop; the strand model makes a new race
   unlikely, and the release-only validation the round reports cannot see one.
3. **The runtime baseline** (departure 5): every HTTP/2 module's per-case outcomes have only ever
   been compared against an out-of-tree capture, and a module added in a round against nothing.
4. **The flake's trigger is the gate's normal condition.** The lane found that even loadavg 12 did
   not reproduce it while a concurrent compile did, and the orchestrator compiles while lanes test.
   Any peer script which still advances past something the other side has not finished is
   therefore exposed by the gate itself, not by a stress run. The lane checked the one shape it
   fixed (`closeConnection()` under an upload) across the tree; the sibling shapes - a script
   ending on a `Delay` a client may outlast, a `GoAway` step under a stream the client is still
   writing - were not checked here and are the next place a 1-in-40 will come from.
5. **`utf_baselib_h2client3` at 39.6 MB** holds three cases with 0.4 MB of headroom; the module is
   closed on arrival, as its main says, and the next TLS-side case is a new module.
6. **Unchanged from the first pass:** the 1.1.1w half of S4.2's acceptance is owed; the negative
   controls exist in lane accounts and not in the tree; Windows has not run any of it.

   **The Windows clause is stale and is corrected in place, 2026-09-22 (astra R4).** It was true
   when written. A full 12-combination Windows build-and-test matrix has since run over the whole
   tree at commit `ba71298` - 3 architectures x {vc143, ccl16} x {debug, release}, one combination
   at a time - and it built and ran the fourteen modules this feature added, which is how
   `utf_baselib_httpclient4` and `utf_baselib_httpclient5` come to have measured object sizes on
   `win-x86`. See `windows-matrix-2026-09-21-module-sizes-record.md`. Getting there took `ca5bf08`,
   three MSVC translation failures in the pulled tests that no Linux build can report.

   **What that does not license.** The record which survives that run is an object-size table, not
   a per-case pass ledger for this range, so "Windows has not run any of it" is false while "every
   case in the L4 range is pinned green on Windows" is not established by it either. The 1.1.1w
   half and the negative controls are untouched by this correction and remain owed.

**Verified versus inferred in this pass.** Verified by reading: every diff named above and the tip
code around each, including the exactly-two setters of `m_isCloseWhenDrained`, the disarm path
through `initiateClose`, the classification of a cancelled task's abort, and the four grep-negative
results for pins. Verified by fetching: `tls_handle_alpn` on OpenSSL's `openssl-3.5` branch, lines
2243-2298. Verified by reading the dist's own Boost 1.90: the five verdict strings and the
confirm-level output. Verified by loading the two tracked runlog baselines: 17 modules, all exit 0,
no jni, no HTTP/2 module; and `common.mk:204-205` for the flags make passes. Inferred or recalled:
the Linux SYN schedule arithmetic and the other operating systems' schedules; that Boost's exit 200
is `exit_exception_failure` and leaves the report clean; that the L4 gate compared against a
capture kept outside the tree; that loopback does not stall a small write; that TSan was not run,
from its absence in every message of the round.

**Not checked in this pass.** No build and no test run; the 297-log old-versus-new parse, the
byte-identical baseline comparisons, the 160 and 240 runs and the 500 ms control are the lane's.
`check_split.sh` tiers 1 and 2 were read, not run. The peer's re-evaluation of `AwaitStreamClosed`
on the write path was read at the cited lines only. The sibling script shapes in item 4 above were
not surveyed. Windows, and the 1.1.1w flavor, as before.
