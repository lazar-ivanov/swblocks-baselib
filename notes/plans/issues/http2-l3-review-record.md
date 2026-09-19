# HTTP/2 client, Layer L3: review record

**Reviewed:** 2026-09-19, read-only, at tip `174e78b`. **Status:** RECORD. Nothing was built and
nothing was run: the code, the tests and the specifications were read; a handful of external
references were fetched (the FoxIO JA4 definition; the OpenSSL documentation of
`SSL_CTX_set_verify`, `SSL_CTX_set_security_level`, `SSL_CIPHER_get_*` and `openssl ciphers`; the
OpenSSL cipher table in `ssl/s3_lib.c` and the security callback in `ssl/ssl_cert.c`; the text
of RFC 9113 for Appendix A, with two independent transcriptions beside it); one read-only
`openssl ciphers` query was run against the dist's own OpenSSL 3.5.4 and the host's 3.0.13, and one
`curl` fetched the RFC text. Two defects are worth fixing before
the slices that consume the code they sit in (findings 2 and 3); one hardening item on the floor
check (finding 1) is not exploitable while security level 2 is pinned; nothing found blocks L4 from
starting once those are scheduled. The notes for later work orders are in section 7.

**All of findings 1 through 6 were carried out the same day**, in a three-lane fix round merged at
`d7e1046`, `13f51fd` and `a102b23`, each validated under clang and gcc at release. The bodies below
are left as written, so the reasoning that led to each fix survives; what changed is recorded here.

- **Finding 1** — the authentication axis is in the floor, `:!aNULL:!eNULL` is appended to the TLS
  1.2 list, `ADH-AES128-GCM-SHA256` is pinned as refused, and design §3.3 names all three axes with
  its Appendix A arithmetic shown. **The implementing lane corrected this finding's own
  recommendation:** the proposed set of `rsa`/`ecdsa`/`any` would have had the library refuse,
  post-handshake, the `DHE-DSS-AES128-GCM-SHA256` its own hardened list offers at level 2, so
  `NID_auth_dss` is in the set. It is written as a positive set rather than a refusal of
  `NID_auth_null`, so it depends on no mask-to-NID mapping and fails closed on an unfamiliar method.
  No negative control was possible - the harness refused a temporary removal of the axis as a
  security weakening - so the proof is in the case, which asserts the two conjuncts the old
  predicate consisted of.
- **Finding 2** — `SessionLimits::maxDecodedHeaderListSize` with a 64 KB default, applied from
  construction as `max( row, advertised )`; the ACK still applies an advertised value. Contract 3's
  text now names this setting as the one it does not govern, since §6.5.2 makes it advisory.
- **Finding 3** — the status is judged before the trailing-bytes check, so a refusal carrying a body
  keeps its status. **The lane then found a pre-existing 1-in-16 flake in the same suite** and fixed
  it separately: `FakeProxy` had no happens-before between the proxy worker's last record and the
  assertion, and one site had been papered over with a 300-iteration poll. It would have reddened
  the layer gate.
- **Finding 4** — `judgeDataFrame()` applies the decode/judge/transition ordering on the DATA path.
  **The lane verified nghttp2 rather than trusting the recall here, and found the recall
  understated it**: nghttp2 answers DATA-before-HEADERS with a *connection* error. RFC 9113 §8.1.1
  asks only for a stream error, which is what was implemented, with nghttp2's stricter choice
  recorded in the comment.
- **Finding 5** — both justifications corrected in the header and the plan; neither decision changed.
- **Finding 6** — all six S3.1 nits fixed. `settingsTimeoutInSeconds` was deliberately left at 10
  and written into S4.2's work order, since that value is S4.2's to set. On §6.8 the lane read the
  RFC rather than adding an error: "MUST NOT increase" is a *sender* rule with no receiver error
  prescribed, so a raised second GOAWAY is clamped to the first value.

**Finding 1 was filed as High and withdrawn the same day**; the correction note above the findings
has the measurement and the mechanism.

**Corrected the same day.** The first draft rated finding 1 High on the premise that OpenSSL's
security level 2 admits anonymous suites. The coordinator measured the opposite on the dist's
3.5.4 with `openssl ciphers -s`; I reproduced it on both binaries and found the rule in OpenSSL's
source. The premise was mine to get wrong: my corroborating query had omitted `-s`, which lists the
configured rather than the supported ciphers. Finding 1 is rewritten below and kept in its place so
the cross-references hold; by severity the order is now 2, 3, 1, 4.

**The range.** The brief named `b188b73..lazari2` and, within it, `69a8f6b`. `69a8f6b` (S3.4 and
S3.6) is an ancestor of `b188b73` - it was merged before the design amendment `352d4c3` and the
size note `b188b73` - so it lies outside that range; it was reviewed by its own diff
(`69a8f6b^1..69a8f6b`, i.e. `f570d5e` and `f0bcbeb`) and at tip. The other three merges (`b66cc72`,
`6b776c7`, `5aa5004`) are inside the range. The one commit above the session's starting tip
`03fe0bc` is `174e78b`, a record; it changes no source.

**What was reviewed against.** Design §3.1, §3.3, §3.6, §4.5, §4.6, §5.7, §7, §10; plan §5, the
work orders S3.1-S3.6 and their execution records, in particular the four S3.1 contracts and the
three S3.5 obligations; the L2 review record, whose two S3.1 hazards became contracts 3 and 4;
`multioperation-deliberate-close-fails-task-record.md`, `tls-handshake-retry-unreachable-record.md`,
`openssl-1x-flavor-deferral.md`, `http2-error-raiser-consolidation-record.md`. RFC 9113, RFC 7541,
RFC 1928 and 1929, RFC 9110 §9.3.6 and RFC 7617 from reading. RFC 9113 Appendix A could not be
reached through the page tool - every fetch truncated before it - and was read from `rfc9113.txt`
fetched with `curl`, cross-checked against Go's `x/net/http2/ciphers.go` and Jetty's
`HTTP2Cipher`, which transcribe it.

## Verdict per slice

| Slice | Verdict | Basis |
|---|---|---|
| S3.1 Session | **Conforms; the four contracts are decided correctly and implemented; two gaps, one of them a bounded-work defect (findings 2, 4).** | Each contract read against RFC 9113 and RFC 7541 and against the code path that implements it, and its case read for what it asserts. Both "wrong, not merely awkward" claims tested and found to stand (finding 5). The ordering rule holds at the single place a header block completes; it is not applied on the DATA path (finding 4). |
| S3.2 stranded plain policy | **Conforms.** | `createSocket` builds on `asio::make_strand`; `cancelTask` posts the shutdown with the flag set synchronously, and the stated reason is right (`TcpSocketCommonBase::onTaskStoppedNothrow`, `TcpBaseTasks.h:123`, reads it). The probe's unguarded counters plus the on-strand assertion are the right pair of observables; the cancel case pins "posted" with a strand blocker and a peer which must not see EOF within 250 ms. |
| S3.3 stranded TLS policy | **Conforms.** | Same policy over the wrapper's strand constructor (guarded on the same `BOOST_VERSION`), then `configureClientStream()` for SNI. The two TLS-specific notes checked: the flag is read by `scheduleTaskFinishContinuation` (`TcpSslBaseTasks.h:450-458`) and `isShutdownNeeded()` (`:646-654`), so it must be synchronous; the protocol timer's handler touches no stream state. "S3.3 is S3.2 but over TLS" is structural - the probe and both case bodies are templates and the TLS header adds a peer only. |
| S3.4 profiles, contexts, floor | **Conforms. The allowlist is stronger than the original wording; the floor implements the design's own definition, which omits authentication - a hardening item, not a live defect (finding 1).** | Allowlist rule read and every refusal in the test reasoned; the `@SECLEVEL` premise is pinned on a raw context first, which is the right way round. The floor predicate admits an anonymous ephemeral AEAD suite on paper; what keeps one off the wire is security level 2, which OpenSSL's security callback applies to every unauthenticated suite (measured on the dist's 3.5.4) and which step 1 pins and the builder re-asserts after the profile's list. The design's sentence "strictly stronger than RFC 9113 Appendix A" is still false. |
| S3.5 tunnel stage | **Conforms; the three obligations are genuinely discharged; one defect in the CONNECT reader's error precedence (finding 3).** | Obligation 1 read (`onTaskStoppedNothrow` releases the continuation and the negotiation, `TcpTunnelStage.h:1558-1560`); 2 read (every handler starts the next socket operation inside itself under the task lock, and the stage owns no other asynchronous object); 3 read and grepped (no `beginOperation`, no `_MULTIOP`). Once-per-attempt is structural (`:1525`) and driven through a reproduced restart made of the production branch's three calls. The origin/proxy split is the one argument at `:1491`, and the case pins which name arrives there. RFC 1928/1929 and the CONNECT bytes checked against the vectors; the 255-byte bounds are enforced at configuration. |
| S3.6 ClientHello, JA3, JA4 | **Conforms.** | Every JA4 rule the plan lists checked against the FoxIO definition text; JA3 against its definition; the cursor's bounds and the one-byte `supported_versions` length read. The two hand-worked vectors re-derived by hand except for the digests. |

The placement and idiom checks of the plan's verification protocol pass: no `BL_DEVENV_VERSION` in
any new header; `TlsClientHello.h` includes OpenSSL and lives in `crypto/`, outside both umbrellas;
`Session.h` carries no Asio, OpenSSL, task or lock; the `T< E = void >` idiom and
`BL_DEFINE_STATIC_*` throughout. **Additive-only holds.** `git diff --numstat b188b73 lazari2`
shows every pre-existing file under `src/` with zero deletions (`FlowControlWindow.h` +23,
`HpackDecoder.h` +22, `http2/PreCompiled.h` +1, two `Main.cpp`, two `notes.txt`), and `69a8f6b`
adds 407 lines to `CryptoBase.h` and deletes none - one `#include` at `:22` and one `#if` block.
The D26 rule is met by every slice.

## Findings

Numbered as first drafted, so the references above and in section 7 hold; by severity after the
correction the order is 2 (Medium), 3 (Medium), 1 (Low/Medium hardening), 4 (Low), then 5 and 6.

### 1. Low/Medium hardening - S3.4: the floor never asks who was authenticated; the security level does, and that is not where D4 says the check lives

*Reclassified from High after measurement - see the correction note at the top.*

`doNegotiatedParametersMeetFloor` (`CryptoBase.h:612-637`) refuses a version below TLS 1.2, a key
exchange outside `{ NID_kx_ecdhe, NID_kx_dhe, NID_kx_any }` and a non-AEAD cipher. It never asks
who was authenticated. OpenSSL's cipher table (`ssl/s3_lib.c`, fetched) defines
`ADH-AES128-GCM-SHA256` and `ADH-AES256-GCM-SHA384` as `SSL_kDHE, SSL_aNULL, ..., SSL_AEAD,
TLS1_2_VERSION, SSL_NOT_DEFAULT | SSL_HIGH`: ephemeral DH, no authentication, AEAD, TLS 1.2. On
paper both pass the predicate, and the positive-rule allowlist (`:526`) admits their names, which
is what the rule is meant to do. And for such a suite OpenSSL verifies nothing - the
`SSL_CTX_set_verify` documentation (fetched): *"If no server certificate is sent, because an
anonymous cipher is used, SSL_VERIFY_PEER is ignored."* - so the wrapper's verify callback
(`AsioSslStreamWrapper.h:643`) would never run.

**What stops it is the security level, and it does stop it.** OpenSSL's
`ssl_security_default_callback` (`ssl/ssl_cert.c`, fetched) returns 1 for everything at level 0
and at every other level refuses, before any bit count, `/* No unauthenticated ciphersuites */
if (c->algorithm_auth & SSL_aNULL) return 0;`. The documentation of the levels does not mention
it, which is where the first draft went wrong. Measured on the dist's own OpenSSL 3.5.4
(`/home/lazar/swblocks/dist-devenv7-ub24-gcc1520-clang2010-a64/openssl/3.5.4/ub24-a64-clang2010-release/bin/openssl`)
and on the host's 3.0.13, with
`openssl ciphers -s -ciphersuites '' '@SECLEVEL=<n>:ADH-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256'`:
`ADH-AES128-GCM-SHA256` is listed at level 0 and at no other level, while
`ECDHE-RSA-AES128-GCM-SHA256` is listed at every level, so the filter is selecting rather than
emptying. Without `-s` the ADH row appears at every level on both binaries - which is the query
the first draft ran, and it lists "all ciphers that match the cipherlist" (`openssl-ciphers(1)`),
not the supported ones; `-s` is what applies the level, and the handshake applies it too. Step 1
pins level 2 (`CryptoBase.h:365-370`), the builder asserts it is still 2 after the profile's list
(`:757-760`), the allowlist refuses the `@` a `@SECLEVEL` token needs, nothing under `src/include`
lowers the level or installs another security callback (grepped), and OpenSSL consults the
callback both when it writes the ClientHello and when it checks the ServerHello. There is no path
by which an anonymous suite is offered or accepted while that holds.

**Why it is still worth fixing.** D4 and design §3.3 say the *floor check* is what makes
"advertise, verify, refuse" hold, and §7 says chain verification applies to every profile
context. Today that property rests on the security level - an OpenSSL behaviour the design never
names and the documentation does not state - while the check the design does name would pass an
unauthenticated suite. A future decision to run a context at level 0, a different TLS backend
behind the seam (D3), or an OpenSSL which changed that rule would each remove the only guard.
**Fix, two layers, both cheap:** in the predicate, refuse a cipher unless
`SSL_CIPHER_get_auth_nid` reports certificate authentication - `NID_auth_rsa`, `NID_auth_ecdsa`,
or `NID_auth_any` for TLS 1.3 - an API from OpenSSL 1.1.0, the gate the block already has, with
a peer-certificate presence check (`SSL_get0_peer_certificate` on 3.x) beside it if the
implementer prefers a positive test; in the builder, append the library's own exclusion tokens
(`:!aNULL:!eNULL`) after the profile's names at `:737` - they are the builder's text, so the
allowlist is untouched. Then one more suite in `TlsClientContext_NegotiatedParametersFloorTests`
(`TestTlsClientContext.h:594`): `ADH-AES128-GCM-SHA256` through `createBelowFloorContext`, refused
by the predicate itself; the three suites there today (`:605-607`) cover the key exchange and the
cipher and not the authentication. And amend §3.3 to say "authenticated ephemeral AEAD".

**Separately, and independent of the security level:** the sentence in §3.3 that the floor is
"strictly stronger than the cipher blocklist of RFC 9113 Appendix A, so INADEQUATE_SECURITY never
needs to be raised by us" is false as written: Appendix A lists
`TLS_DH_anon_WITH_AES_128_GCM_SHA256` and `TLS_DH_anon_WITH_AES_256_GCM_SHA384`, and the predicate
admits both. How that was established: the appendix was read in the RFC text itself -
`rfc9113.txt`, fetched whole with `curl` after every fetch through the page tool had truncated
before it - where the two suites are entries in the list under "An HTTP/2 implementation MAY treat
the negotiation of any of the following cipher suites with TLS 1.2 as a connection error
(Section 5.4.1) of type INADEQUATE_SECURITY" (lines 3943-3944 of the text; the appendix begins at
3831), and neither `TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256` - named only in §9.2.2 as the suite a
TLS 1.2 deployment MUST support - nor `TLS_DHE_RSA_WITH_AES_128_GCM_SHA256` is. Two independent
transcriptions, Go's `x/net/http2/ciphers.go` (`isBadCipher`) and Jetty's
`org.eclipse.jetty.http2.HTTP2Cipher` (`__blackCiphers`), agree on all four. With the
authentication axis added to the predicate the sentence becomes true; without it, drop it.

### 2. Medium - S3.1: the decoded header list is unbounded for the default profile, and unbounded before the ACK for every profile

`m_localMaxHeaderListSize` starts at `SIZE_MAX` (`Session.h:452`) and changes in exactly one place,
`applyAcknowledgedSettings` (`:2379-2381`), when the peer acknowledges a `SETTINGS` of ours which
carried `SETTINGS_MAX_HEADER_LIST_SIZE`. Two consequences:

- **A profile which does not advertise the setting - `Http2Profile()`, the default - never gets a
  bound.** Design §4.6 row 1 says the limit is "our SETTINGS_MAX_HEADER_LIST_SIZE" and assumes we
  advertise one; L1 decided, rightly per RFC 9113 §6.5.2, that `Globals` defines no numeric value
  and the decoder takes it as a parameter. Nobody decided what happens when no profile supplies it,
  and the answer today is: nothing bounds the decoded list.
- **For a profile which does advertise it, contract 3 applies it on the ACK**, which a hostile
  peer withholds for up to `settingsTimeoutInSeconds` (10 s by default). The test pins this window
  as intended (`TestSession.h:1177-1180`: `SIZE_MAX` before `settle()`).

Why this is a bounded-work defect and not a nit: HPACK expands. The compressed block is capped at
256 KB (§4.6 row 2), but an indexed representation is one octet for the 65 most recent dynamic
entries (indices 62-126 fit the 7-bit prefix), and a dynamic entry can hold a value of nearly 4 KB
in the default 4096-octet table. A peer inserts one such entry - about 4 KB of block - and then
references it some 258,000 times in the rest of the block; `HpackDecoderT::emit`
(`HpackDecoder.h:415-438`) checks each field against `maxDecodedSize`, which is `SIZE_MAX`, and
pushes every one. That is on the order of a gigabyte of `HpackField`s for one block, swapped into
a `SessionEvent` and held until drained: the HPACK bomb the §4.6 row exists to close, and neither
the block cap nor the CONTINUATION cap touches it. (Arithmetic from the representation sizes, not a
measured allocation.)

The rule of contract 3 was applied uniformly where it belongs only to the settings which change
what the peer may *legally* send (`MAX_FRAME_SIZE`, `INITIAL_WINDOW_SIZE`, `HEADER_TABLE_SIZE`).
`SETTINGS_MAX_HEADER_LIST_SIZE` is advisory to the peer - §6.5.2, exceeding it is not a protocol
violation - so there is no frame the peer "sent legally under the old value" to protect, and
enforcing a bound from construction, whether or not the profile advertises one, violates nothing.
**Fix:** a `SessionLimits` row for the decoded list, applied from the constructor, with a default
in the tens of kilobytes or the advertised value when larger; `applyAcknowledgedSettings` then only
*lowers* it to what was advertised. And a case with `Http2Profile()` and a block which decodes to
more than the row, closed with `ENHANCE_YOUR_CALM` and the connection alive, beside the
bounded-profile case at `TestSession.h:2541`. Design §4.6 row 1 should say what bounds the list
when nothing is advertised.

### 3. Medium - S3.5: a refused CONNECT whose body arrives with its headers is reported as a framing error and loses its status

`HttpConnectNegotiationT::onDataRead` (`TcpTunnelStage.h:716-724`) checks that nothing follows
`CRLF CRLF` *before* it judges the status line. The trailing-bytes refusal is right for a 2xx - the
origin cannot have spoken - but a proxy answering 407 or 403 sends an HTML body with it, and
because the reader asks for at most 257 octets per read (`:602`, `:521`) the read which completes
the header section will usually carry the first bytes of that body as well. The result is
`InvalidDataFormatException( "The proxy sent data past the end of the CONNECT response headers" )`
rather than `HttpException` with `errinfo_http_status_code( 407 )` - which the test's own comment
(`TestTcpTunnelStage.h:848`) names as the point: telling a 407 asking for credentials from a 403
refusing them, and what design §3.7 says an HTTP status failure carries. Both refused-status cases
in the tree send no body (`:862`; `:1515` with `Content-Length: 0`), so the case which would fail is
absent. **Fix:** parse and judge the status line first, and apply the trailing-bytes check only to a
2xx - a non-2xx fails the task either way and whatever follows it is irrelevant. One case: a 407
with a body in the same segment, `HttpException` carrying 407.

### 4. Low - S3.1: two gaps in message validation on the DATA path

- **DATA before the first non-informational HEADERS is delivered rather than refused.**
  `handleData` (`Session.h:1657-1780`) never consults `context.headersReceived`, so a server which
  sends DATA on a stream before its response HEADERS produces a `Data` event, then a `Headers`
  event when the block arrives. RFC 9113 §8.1 defines a message as optional informational HEADERS,
  one HEADERS, zero or more DATA, optional trailers, and §8.1.1 makes "an invalid sequence of HTTP
  messages" malformed - a stream error. nghttp2 refuses it with PROTOCOL_ERROR ("DATA: stream not
  opened"); that is recalled, not verified, the fetch of `nghttp2_session.c` having truncated
  before the function. No case covers it. The consumer (S5.1) would see body bytes with no status.
  It should be a `rejectMalformedMessage` in `handleData` when `! context.headersReceived`.
- **The ordering rule is not applied on the DATA path.** Contract 1's "judge before you
  transition" holds at the one place a header block completes, and that is indeed the only place.
  But a content-length violation on the DATA frame which carries END_STREAM (`:1744-1756` for too
  much; `onPeerEndStream`, `:2947-2967`, for too little) is judged *after*
  `m_registry.onFrameReceived( DATA )` has run, so when the stream was `HalfClosedLocal` - the
  ordinary client case, request already ended - it is now `Closed` and the RST_STREAM §8.1.1
  demands cannot be sent. `rejectStream` (`:2985-3011`) records the reason on the closure, and its
  own comment (`:2996`) knows the reset cannot go out. The caller is told; the peer is not. Harmless
  in effect, since the peer had finished, but it is the same silent shape contract 1 exists to
  name, and the DATA path could judge the running total against `length` before the transition
  exactly as the HEADERS path judges the block.

### 5. The two "wrong, not merely awkward" claims, tested: both stand; one justification is imprecise

- **Contract 2 - "construct the decoder after the ACK is wrong".** True. At the ACK the peer's
  encoder has already reduced its table to the new maximum and evicted; what survives is still
  indexed by the peer. A decoder constructed fresh at that moment has an empty table, and the next
  block's indexed references fail - unless the new size is 0. The setter (`HpackDecoder.h:155-158`)
  moves the ceiling only, the ceiling is what `decodeSizeUpdate` (`:557-563`) enforces, and
  `advertisedHeaderTableSize` (`Session.h:1129-1150`) starts at `max( advertised, 4096 )`; so a size
  update to the old 4096 before the ACK is accepted and one above the new value after it is
  `COMPRESSION_ERROR`, and `Session_HpackDecoderCeilingTests` pins both. **One sentence of the
  reasoning is wrong in a way that changes nothing.** The header and the plan say that evicting on
  the ACK "would drop entries the peer's encoder still indexes". For a conforming peer it would
  not: RFC 7541 §4.3 has the encoder evict when its maximum is reduced, and §4.2 has it signal that
  at the start of the next block, so by the ACK the peer has already evicted exactly what an
  eviction here would drop. Not evicting is still the better decision - it tolerates a peer which
  reduced late or not at all, and the table stays bounded by what we once advertised - but the
  justification should read "tolerates a late peer", not "a conforming peer would break".
- **Contract 4 - "the naive path is an UnexpectedException".** True, read through: a stream the
  peer reset is retired to `m_closed`, and `StreamRegistryT::onFrameSent`
  (`StreamStateMachine.h:1192-1201`) reaches `StreamStateMachineT::onFrameSent`, whose
  `BL_CHK( canSend )` (`:534-542`) fails in `Closed` for anything but PRIORITY; a stream already
  forgotten fails the `BL_CHK` at `:1194`. Either is an `UnexpectedException` and neither is an
  `Http2*Exception`, so it would escape `feed()`, which promises that nothing a peer can cause
  escapes. `answerStreamError` (`Session.h:1430-1453`) drops the error when `canSend( RST_STREAM )`
  is false and `handleData` has credited the connection window first;
  `Session_StreamErrorOnClosedStreamTests` pins the drop, the credit, the `HalfClosedRemote` answer
  and the `EndStreamReceived` connection error. A nit on the prose: "answering a reset with a reset
  invites a loop" is not what RFC 9113 says - a peer which sent RST_STREAM must ignore ours (§5.1) -
  so the loop is not the reason; the §5.1 prohibition and the crash are.
- **Contract 1** stands as decided. Decode first is mandated by §4.3; judge before transition by the
  pair §8.1.1 / §5.1; and `completeHeaderBlock` (`Session.h:1861-1961`) is the single completion
  point for `beginHeaderBlock` and `continueHeaderBlock` alike. For a known stream the registry is
  told only after the judgement passes, and for a known stream `canSend( RST_STREAM )` is always
  true - client streams are never idle in the registry and closed ones are not known - so the reset
  always goes out, which is the property the ordering buys. One consequence worth knowing: a HEADERS
  on a `HalfClosedRemote` stream is judged first, so a bad trailer section is reset with
  `PROTOCOL_ERROR` where the state machine alone would have said `STREAM_CLOSED`; both are resets.
- **Contract 3** stands, with finding 2 as its one mis-application. `MAX_FRAME_SIZE` to the reader,
  `INITIAL_WINDOW_SIZE` to the receive windows through the additive
  `ReceiveFlowControlWindowT::applyInitialWindowSizeChange`, `HEADER_TABLE_SIZE` to the ceiling, the
  timer stopped - all on the ACK, FIFO, and the receive-window case is pinned with the window driven
  negative (`TestSession.h:1157-1162`). The encoder mirror - the peer's `HEADER_TABLE_SIZE` applied on
  receipt with the size update emitted at the start of the next block (`HpackEncoder.h:147-165`,
  `:186-194`) - is right per RFC 7541 §4.2, including the smallest-then-final rule for two changes
  between blocks.

### 6. Nits - preferences and small inaccuracies, not defects

- **S3.1** `SessionLimits::settingsTimeoutInSeconds` is 10 (`Session.h:104`); design §5.7 lists the
  `SETTINGS` acknowledgement timeout as 30 s. The engine's comment says the number is its own; S4.2
  should set it from §5.7 or the design should record 10.
- **S3.1** `m_settingsSentAt` is overwritten by every `applyLocalSettings` (`:1120`), so an older
  unacknowledged frame's timeout is measured from the newest send.
- **S3.1** the comment on `isClosed()` (`:494-502`) says "or a GOAWAY of ours", but `goAway()`
  (`:1088-1097`) does not set `m_isClosed` - correctly, since in-flight streams finish; the comment
  is what is wrong.
- **S3.1** `submitHeaders` (`:888-913`, server role) does not reap after a HEADERS which closes a
  `HalfClosedRemote` stream, so that `StreamClosed` event waits for the next `produce()` or `feed()`.
- **S3.1** `te` is compared case-sensitively (`:2613`); the value is a case-insensitive token in
  RFC 9110, so `te: Trailers` is malformed to this engine. Request side only, so the test peer is
  the only thing which sees it.
- **S3.1** nothing checks that a second GOAWAY does not raise `lastStreamId` (§6.8 MUST NOT);
  `handleGoAway` (`:2121-2154`) takes the new value.
- **S3.1** after a connection error `produce()` (`:777-813`) still writes header blocks queued before
  it, after the GOAWAY.
- **S3.1** the plan says "one case per §4.6 row"; row 6, remembered closed streams, has its defaults
  asserted in `Session_LimitsTests` and its behaviour only in S2.4's registry cases.
- **S3.4** `CryptoBase.h:22` now includes `TlsClientProfile.h` in a header every OpenSSL user
  compiles; that header is `BaseIncludes.h` plus `<string>` and `<vector>`, so the cost is nil, but
  it is an include edge added to core rather than a new name.
- **S3.5** has no deadline of its own: a proxy which accepts the TCP connection and never answers
  holds the task until an external cancel. That matches the establisher, which has no connect
  deadline either, and the class comment already says a timer of its own would acquire obligation 2.
  It makes the design's 60 s "resolve through preface" timer (§5.7) S4.1's, and S4.1 must arm it
  before the stage rather than after it.
- **S3.6** nothing found. The JA4 details the plan lists as easy to get wrong were each checked
  against the definition: version from `supported_versions` ignoring GREASE; `d`/`i`; counts which
  exclude GREASE and include SNI and ALPN; a hash which excludes SNI and ALPN; signature algorithms
  unsorted after `_` and omitted with their underscore when absent; twelve zeros for an empty
  section. The definition also says to count non-cipher values such as the SCSV, which the code does
  since it drops GREASE only.

### 7. Notes for later slices

- **S4.1:** arm the connect deadline before the tunnel stage, not after it (nit above). S4.1 runs
  on the default hardened context, whose list carries `!aNULL`, so finding 1 does not reach it.
- **S4.2:** choose `settingsTimeoutInSeconds` against §5.7; the decoded-list bound of finding 2 is a
  `SessionLimits` row this slice will construct the session with.
- **S5.1:** until finding 4's first gap is closed, a `Data` event can precede the `Headers` event on
  a stream; the request task must not assume otherwise.
- **S7.2:** the loader's validation (design §6.2) should refuse anonymous and NULL suite names
  outright as well, because a loader can say why where a handshake failure cannot; and the
  hardening of finding 1 should land before **S7.3** hands a profile context to a connection - not
  because anything is exploitable while level 2 is pinned, but because that is when the floor
  starts being relied upon.
- **S7.3:** the group list and signature algorithms go to OpenSSL as strings too
  (`SSL_CTX_set1_groups_list`, `SSL_CTX_set1_sigalgs_list`); the same positive-rule allowlist
  belongs on those names.

## What was verified versus inferred

Verified by reading the code against the RFC text: every row of the verdict table's basis column;
the four contracts; the three obligations; the SOCKS5 and CONNECT byte vectors in the tests against
RFC 1928 §3-§6, RFC 1929 §2, RFC 9110 §9.3.6 and RFC 7617 §2. Two negative results worth keeping:
`HeaderList::isFieldVChar` (`HeaderList.h:211-216`) admits obs-text, so the engine's value rule
matches RFC 9110's `field-vchar` and does not over-reject; and the tunnel stage's claim that a
short transfer cannot reach `chkFullTransfer` on a cancel holds, because
`BL_TASKS_HANDLER_BEGIN_CHK_EC` (`TaskBase.h:165-168`) runs `BL_TASKS_HANDLER_CHK_CANCEL_IMPL`
before the body and `untilCanceled()` only cuts a transfer short when `isCanceled()` is already
true. Verified by fetching: the FoxIO JA4 definition (every rule in section 6 above); the OpenSSL
cipher table entries for the two ADH-GCM suites (`SSL_kDHE`, `SSL_aNULL`, `SSL_AEAD`, TLS 1.2);
the OpenSSL documentation that `SSL_VERIFY_PEER` is ignored for an anonymous cipher, that
`SSL_CIPHER_get_kx_nid` returns `NID_kx_dhe` for DHE and `NID_kx_any` for TLS 1.3, and that
`openssl ciphers` applies the security level only under `-s`; the `SSL_aNULL` refusal in
`ssl_security_default_callback` (`ssl/ssl_cert.c`), which the level documentation omits; Appendix
A's two anonymous GCM entries in the RFC text itself (`rfc9113.txt`, fetched whole with `curl`)
and in two independent transcriptions (Go, Jetty). Verified by running one read-only
`openssl ciphers` query on the dist's OpenSSL 3.5.4 and the host's 3.0.13:
with `-s`, `ADH-AES128-GCM-SHA256` survives at security level 0 only; without `-s` it is listed at
every level, which is the query the first draft mistook for corroboration. Verified by
`git diff --numstat` and by reading the `69a8f6b` diff: the additive-only claims. Verified by grep:
no PSK callback, no security-level call other than the pin, under `src/include`.
Inferred: the size of the HPACK expansion in finding 2 is arithmetic from the representation sizes;
nghttp2's behaviour in finding 4 is recalled; the mapping of `SSL_aNULL` to `NID_auth_null` is
recalled from the OpenSSL source, the documentation listing examples only, which is why the fix
recommends a positive test for certificate authentication with the peer-certificate check beside it.

## What was not checked

- **No build and no test run.** Every "passes", "TSan clean over three runs", "exited 201 with the
  predicted failure" and every object size in the plan's L3 records is taken as reported. The
  negative controls of S3.2/S3.3 exist in the lane's account, not in the tree, and were not
  reproduced.
- **The digest literals** in `TestTlsClientHello.h` (two MD5, four SHA-256 prefixes) were not
  recomputed; the strings they hash were re-derived by hand and agree with the vectors.
- **OpenSSL 3.5.4** was probed with one read-only `openssl ciphers` query and nothing else; the
  cipher table and the security callback were read from OpenSSL master, the verify semantics from
  the current documentation.
- **The wrapper's message callback** which keeps only the ClientHello (`onSslMessageCallback`) was
  not read; the capture case asserts the first byte is `0x01`.
- **`FrameReaderT`'s block-continuity rule**, which `continueHeaderBlock`'s `BL_CHK` trusts, was
  verified by the L2 review and not re-read here.
- **`HpackEncoderT`** beyond the size-update path, and **`HpackDynamicTableT`** beyond
  `setCapacity` and the constructor.
- **Windows**, and the **1.1.1w flavor**, whose debt the deferral record states correctly for every
  L3 slice which touches OpenSSL.
- **The `httpclient2` split** and the gcc release sizes were not re-measured.
