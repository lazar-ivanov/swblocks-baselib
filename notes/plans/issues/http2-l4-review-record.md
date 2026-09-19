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
