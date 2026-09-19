# HTTP/2 client, Layer L2: review record

**Reviewed:** 2026-09-19, read-only, at tip `7a4d974` (the range `5bf3648..7a4d974`). **Status:**
RECORD. Three defects were found and are fixed in the review's follow-up rather than carried - the
S2.6 contract could not convey the response status, S2.5 folded through `std::locale()` on a
smuggling gate, and S2.7 rejected `Domain=localhost` on `localhost`; nothing found blocks L3. The
findings which affect a later slice are written into that slice's work order in
`notes/plans/http2-implementation-plan.md`; the two places the design was silent or said something
the code does not do are corrected in `notes/plans/http2-design.md` §5.3, §5.5 and §5.6. This record
is the standalone account: the verdict per slice, the findings ranked, what was verified against what,
and what was not checked.

**What was reviewed against.** Design §4.1-§4.5, §5.2-§5.6; plan §4 work orders S2.1-S2.9;
`d15-http1-codec-backend-verdict.md` and `beast-availability-probe-record.md`. RFC 9113, RFC 7541,
RFC 9110/9112, RFC 6265 and 6265bis, WHATWG Fetch, from the texts (RFC 7541 downloaded and compared
mechanically; the rest from reading).

## Verdict per slice

| Slice | Verdict | Basis |
|---|---|---|
| S2.1 FrameCodec | **Conforms.** | Every section 6 length, stream-id and padding rule read against RFC 9113 and against the test vectors; the incremental reader's block-continuity rule and oversize-before-buffer property checked in code and pinned in tests. One nit (below). |
| S2.2 HPACK | **Conforms.** | All 257 rows of the Huffman table compared mechanically to Appendix B of the RFC text: 0 mismatches, Kraft sum 1. Every Appendix C hex literal in `TestHpack.h` found byte-identical in the RFC's dumps (the 14 not found are the hand-built error cases, each read). Static table verified row by row. Integer, string, padding, size-update and decoded-size-limit rules read against RFC 7541 sections 4-6; the transactional dynamic table reasoned through, including logical eviction under a block. |
| S2.3 FlowControlWindow | **Conforms.** | Zero-increment, overflow, negative-window, SETTINGS re-adjustment and the "connection error although the window is a stream's" exception all read against RFC 9113 6.9, and pinned with the RFC's own worked example. |
| S2.4 StreamStateMachine | **Conforms.** | Both transition tables in `TestStreamStates.h` re-derived from RFC 9113 5.1 independently of the test and found identical; the three closed-state answers, the closed-stream DATA accounting, identifier arithmetic (`2^30` and `2^30 - 1`), and the registry's unknown-stream rules read. One cross-slice hazard for S3.1 (below), one server-role nit. |
| S2.5 HTTP/1.1 codec, D15 | **Conforms; D15 verdict supported.** | The facade's five checks read; the isolation greps of §5.5 run at tip and clean (no `boost::beast` outside the import header, no `bl::beast` outside the backend, the facade absent from every `PreCompiled.h`). The smuggling cases are asserted from both sides in the tests. One defect, fixed (finding 3), and two low findings (below). The object-size and toolchain numbers in the verdict record were **not** re-measured. |
| S2.6 contracts | **Did not compose - a defect, fixed.** | See finding 1. Everything else in the contract matches the work order and §5.2-§5.4. |
| S2.7 cookie jar | **Conforms to RFC 6265, with the stated 6265bis exception, which is the right call.** | Domain-match, path-match, default-path, cookie-date, Max-Age precedence, ordering, caps and eviction read against RFC 6265 5.1-5.4 and 6.1. One defect, fixed (finding 3), and two low findings (below). |
| S2.8 redirects | **Conforms; the three additions and the HEAD exemption are sound.** | The full 5x6 rewrite matrix agrees with RFC 9110 15.4 and WHATWG Fetch; cross-origin is origin (scheme, host, effective port) which is stricter than curl's host-only and safer. One unflagged addition (below). |
| S2.9 decoder seam | **Conforms.** | Both caps enforced on the seam, before delivery; the ratio has a grace window; a default registry is empty and a case pins D9. |

The placement and idiom checks of the plan's verification protocol pass on every header: no
`BL_DEVENV_VERSION`, no OpenSSL in either umbrella, the `T< E = void >` idiom throughout, statics via
`BL_DEFINE_STATIC_*`. `net::Uri` lowercases scheme and host on parse (`Uri.h:735`, `:850`), so the
origin comparisons S2.7 and S2.8 make and the `ConnectionKey` of S2.6 are case-safe.

## Findings, ranked

### 1. The S2.6 contract had no path for the response status - a defect, fixed

`ClientStreamEventSink::onHeaders( handle, http::HeaderList&&, isInterim )` (`ClientConnection.h:159`)
was the only header event, `http::HeaderList` rejects `:status` by design (a colon is not a token
character), and `ClientResponse::status()` (`ClientTypes.h:425`) was therefore unfillable by a
request task from anything a driver could hand it. The stub sink in `TestClientContracts.h` shows
the gap - it records a header count. The work order line "`onHeaders( HeaderList, isInterim )`" is
where it was lost; the design said how the Session reports status (`:status` inside the decoded
field list) but not how it crosses the driver-to-request-task seam. **Verified**, not inferred: the
sink interface, `HeaderList::isTokenChar` and the stub were all read, and the coordinator confirmed
it independently.

This was first filed as needing the maintainer's word, because the contract is declared frozen and
negotiated. On reflection that framing was wrong, and the coordinator's is right: a contract that
cannot work as written is a defect, and fixing it is not a choice between options because the
option space was already closed - a `HeaderList` mode admitting pseudo-headers was rejected in S2.2
for reasons that still hold (a decode failure is a connection error, a malformed message a stream
error), and a separate `onStatus` event would add a fourth ordering guarantee to a sink whose whole
comment exists to make the order a guarantee rather than a hope. **The fix is a `status` parameter
on `onHeaders`** - an `unsigned`, the three-digit `:status` for h2 and the status-line code for h1 -
with the stub and its case updated in the same change; S4.2 and S4.3 deliver it and S5.1 consumes
it. Recorded in the plan under S2.6 and in design §5.3. **Nothing in L3 touches it.**

### 2. Two composition hazards for S3.1, both silent failures - recorded in its work order

- **Local settings take effect on the peer's ACK** (RFC 9113 6.5.3). The plan already records this
  for the HPACK decoder's capacity; it is the same rule for `SETTINGS_INITIAL_WINDOW_SIZE` on the
  receive side and for `SETTINGS_MAX_FRAME_SIZE`. Applied at send time, S2.3's receive-side
  `consume` raises FLOW_CONTROL_ERROR on data the peer sent legally under the old window.
  **Inferred** from the RFC and the window arithmetic; not a defect in S2.3, which is correct
  arithmetic either way.
- **A `StreamError` on a closed stream cannot be answered through the registry.** S2.4 returns
  `StreamError( STREAM_CLOSED )` for a frame on a stream the peer reset, and `canSend( RST_STREAM )`
  is false in the closed state, so routing the answer through `StreamRegistry::onFrameSent` throws
  `UnexpectedException`. **Verified** by reading both branches; the RFC contradicts itself here
  (5.1 versus 5.4.2) and nghttp2 ignores the frame. S3.1 must decide, not discover it.

### 3. Two more defects, fixed after the review rather than carried

Both were first filed lower than they belong - one as a nit, one as a documentation item - and the
coordinator's upgrade of each is right.

- **S2.5 folds through `std::locale()` on a smuggling gate.** `Http1Codec.h:885` trims the
  `Transfer-Encoding` value with `str::trim_copy` before the "exactly `chunked`" comparison, and
  `:1149` lowercases the profile case-map key with `str::to_lower_copy`; both take the global
  locale. `HeaderList.h` documents its ASCII-only fold precisely because those functions "can be
  changed out from under the lookup by a caller which installs a different global locale", so
  S2.5 reintroduced the hazard S1.2 went out of its way to avoid - and did so on one of the
  facade's own defences. The perturbation runs toward leniency: a locale in which some obs-text
  octet counts as whitespace makes `chunked` followed by that octet pass the gate that the C locale
  refuses, and the backend then frames the message its own way - exactly the two-readings shape
  the facade exists to close. Beast's independent parse limits what is reachable, but a security
  check must not depend on an embedder's `std::locale::global`. **Fix:** an ASCII-only OWS trim and
  `HeaderList::equalsIgnoreCase` on the gate, an ASCII-only lower on the case-map key. Design §5.5
  now states the rule for the whole codec.
- **S2.7 rejects `Domain=localhost` on `localhost`.** The no-dot rule exists to stop `Domain=com`,
  but `localhost` is a legitimate host that happens to have no dot, and RFC 6265 5.3 step 5 says a
  domain attribute identical to the request host becomes a host-only cookie rather than a
  rejection. As landed, the single most common local-development case silently loses its cookies.
  **Fix:** when the canonicalized attribute equals the request host and fails the dot test, store
  host-only; everything else in `isAcceptableDomainAttribute` stands. Design §5.6 now states the
  rule.

### 4. Low - deviations not flagged by the lanes

- **S2.8** drops `Proxy-Authorization` cross-origin in addition to `Authorization` and `Cookie`.
  Harmless while proxy credentials are session configuration applied by the tunnel stage; wrong if a
  caller-supplied header ever carries them. Noted for S6.1 and in design §5.6.
- **S2.5** refuses `Transfer-Encoding` with `Content-Length` outright, where the design's wording
  implied "the coding wins, then close". RFC 9112 6.3 permits refusal; the design now says what the
  code does.
- **S2.5**'s obsolete-fold refusal covers the header section only; a fold in a chunked trailer
  section is unfolded by Beast and not detected. Impact is low because trailer fields are restricted
  to a safe subset, and the design now records the boundary.
- **S2.7** does not implement RFC 6265 5.3 step 11's non-HTTP-API clause (a non-HTTP set must not
  replace an existing HttpOnly cookie). `isHttpApi` is always `true` from this client, so it is
  unreachable today; noted for S6.1.
- **S2.7** follows RFC 6265 alone on Secure cookies set over `http`; RFC 6265bis 5.5 ("leave secure
  cookies alone") is not implemented. A session-fixation surface only for a session that speaks
  both schemes to one host. The design said "RFC 6265", so this is silence rather than a deviation;
  it is now stated in §5.6 as a decision for a later slice.
- **S2.6** `ClientRequest` carries no stream-idle timeout, which design §5.7 lists as a request-task
  timer. Off by default; noted for S5.1.

### 5. Nits - preferences, not defects

- **S2.1**: a `DATA` frame with `PADDED` and length 0 is a *stream* FRAME_SIZE_ERROR here (RFC 9113
  4.2 allows it, since DATA carries no field block); nghttp2 makes it a connection PROTOCOL_ERROR.
  Both defensible.
- **S2.4**, server role only: a HEADERS on a never-opened identifier below the highest seen is
  ignored as implicitly closed; nghttp2 answers PROTOCOL_ERROR. The server is test-only (D8).
- **S2.5** `hostHeaderValue` renders `example.com:443` when the URL spells the default port;
  browsers omit it. A fidelity detail for S7.
- **S2.9** `ContentDecoderStreamT` copyability - asked by the coordinator whether a copy taken
  before a cap trips could keep decoding through the copy, which would make the caps advisory.
  **It cannot be constructed.** The stream's `m_decoder` is an `om::ObjPtr`, and `om::ObjPtr` is
  `BL_CTR_COPY_DELETE` over a `SafeUniquePtr` base (`ObjModel.h:110`), so the stream's implicit copy
  constructor and copy assignment are deleted with it: the class is move-only by construction,
  which `createStream`'s return by value needs and nothing else uses. A *move* carries the counters
  and the tripped flag to the destination together with the decoder, so the caps travel with the
  bytes; the moved-from object keeps its (copied) flags but a null decoder, so `write()` on it
  passes `chkUsable()` and dereferences null - a programming error in library code, unreachable
  from the network, and not a bypass since it can decode nothing. Not a security finding. The one
  improvement worth making, if any: `chkUsable()` could test `m_decoder` so that misuse of a
  moved-from stream is an exception rather than a crash. The earlier nit was mis-stated - it said
  "copyable", which was wrong.

## What was verified versus inferred

Verified by mechanical comparison: the Huffman table (257 rows, 0 mismatches) and the Appendix C
vectors (33 RFC-sourced literals, all found). Verified by running: the four grep invariants of §5.5
and the plan's verification protocol, at tip. Verified by reading against the RFC text: everything
in the verdict table's "basis" column. Verified by reading the library: `om::ObjPtr`'s deleted copy
(`ObjModel.h:110`), which settles the decoder-stream question, and the two locale-dependent calls at
`Http1Codec.h:885` and `:1149`. Inferred: the two S3.1 hazards above are conclusions about
composition drawn from the L2 code and the RFC, not observed failures - there is no Session yet to
observe them in.

## What was not checked

- **No build and no test run.** The lanes' claims that the cases pass were not re-executed; the
  tests were read for what they assert, which is what this review was for. In particular the
  D15 record's object-size figures (+0.27 MB clang debug, +0.68 MB gcc release, module 24.0 to
  25.0 MB) and its "2 of 4 toolchains demonstrated" are taken as recorded.
- **Windows.** Nothing here touches the msvc and clang-cl question, which the D15 record already
  states as argued, not measured.
- **Beast's own behavior** on the leniencies the facade closes was not re-probed; the facade's checks
  were read on the assumption that the S2.5 probe's description of Beast is accurate. If Beast's
  callback order (`do_field` before or after `on_field_impl`) differs from what the facade assumes,
  the pair `Content-Length` + `Transfer-Encoding: chunked` is still refused, by whichever of the two
  sees it first.
- **`net::Uri` itself** (S1.1) beyond the accessors S2.7 and S2.8 depend on.
- **Thread safety of `CookieJar`** beyond reading the lock discipline; the setters for the caps are
  unlocked, which is fine for the construct-then-use pattern the class describes.
