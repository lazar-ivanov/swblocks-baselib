# HTTP/2 client, layer L7 (browser impersonation) — execution plan

**Date:** 2026-09-29. **Status: approved by the maintainer on 2026-09-29, with the start held.** Phase 1
begins when the maintainer says so.
- D-L7-1 to D-L7-5 in §3 were all taken on 2026-09-29.
- **Reviewed 2026-09-29** by a one-off Fable review, `http2-l0-state/l7/reviews/plan-fable-r1.md`: agree
  with changes, with no P1. All seventeen findings were checked at the source and taken
  (`plan-fable-orchestrator-r1.md`). Each is marked where it landed by its F-number, except F13's three
  text corrections, which were made in place.
- **The Windows round `f481a2d`, analysed 2026-09-29** by the orchestrator and by Fable, independently
  (`l7/reviews/windows-round-impact-*.md`). Neither found anything requiring an amendment before
  Phase 1. Each item folded is marked by its G-number.

**What this is.** The implementation plan's §9 (S7.1 to S7.5) was written on 2026-09-17, before L0 to
L6 were built and reviewed. This document refreshes it against the code as it stands on `edd921b` — and
the Windows round `f481a2d` which followed changed no file §1 cites; its one library edit is a comment in
`tasks/TcpBaseTasks.h`, and its harness changes are the ones §5 names. It folds
in the S7.3 work which the layer reviews left, answers how L7 sequences against the decompression
work, and turns the slices into change-sets for the parallel workflow
(`parallel-implementation-workflow.md`). Where it and §9 differ, it says why (§4.1). The design is
unchanged: `http2-design.md` §6 and decisions D2, D3, D9, D10, D22 and D23.

---

## 1. Where L7 starts from

Every item below was read at the source on 2026-09-29.

### 1.1 What the lower layers built for it

- **The profile types (S1.4).**
  - `crypto/TlsClientProfile.h`:
    - the TLS 1.2 and 1.3 suites;
    - groups, each with its key-share flag;
    - signature algorithms and ALPN;
    - `session_ticket`, `status_request`, SCT and padding.
  - `http2/Http2Profile.h`:
    - the ordered SETTINGS;
    - the connection `WINDOW_UPDATE` increment and threshold;
    - idle-stream PRIORITY frames and the `HEADERS` priority;
    - the pseudo-header order;
    - the HPACK table size, the indexing policy, and cookie crumbling.
  - `httpclient/HeaderProfile.h`:
    - one entry per request kind — `Navigation`, `Fetch` and `Subresource` — each with its default
      headers, some marked `isComputed`, which nothing reads yet;
    - the placement of the caller's headers, and the HTTP/1.1 case map;
    - the RFC 9218 `priority` value, `acceptEncoding`, and the `accept-language` q-values.
  - `data/models/HttpClientProfiles.h`: the JSON data model, `BrowserProfile`, with the identity (id,
    family, grade, deviations), the three shapes, and the version strings (user agent, `sec-ch-ua`
    brands, platform).
- **The TLS context (S3.4).** `crypto::CryptoBase::createAsioSslClientContext( TlsClientProfile )`
  (`CryptoBase.h:1591`) has:
  - the cipher allowlist, security level 2, and the negotiated-parameter floor;
  - `NotSupportedException` below OpenSSL 3.5 (D2);
  - **and it applies the two cipher lists only.** The groups and key-share marks, the signature
    algorithms, and the `status_request`, SCT and padding switches are carried by `TlsClientProfile`
    and applied by nothing (`CryptoBase.h`, the comment above `:762`; the implementation plan's S3.4 as
    landed).

  devenv7 ships **OpenSSL 3.5.4** (`projects/make/devenv-detect.mk`).
- **The ClientHello (S3.6).**
  - `AsioSslStreamWrapper::enableClientHelloCapture( )` (`AsioSslStreamWrapper.h:983`) records the
    hello a connection sends.
  - `crypto/TlsClientHello.h` parses it and computes JA3 and JA4.
  - Two rows of design §6.3's table are confirmed against 3.5.4. The rest of that column is inferred.
- **The HTTP/2 layer (S3.1).** `http2::Session` honours an `Http2Profile`. The HTTP/2 driver's config
  carries one into it (`http2/Http2ConnectionTask.h:108`, `:2801`).
- **The session (S6.1).**
  - `ClientSession` applies a `HeaderProfile` by request kind.
  - It sends `accept-encoding` as the profile's list ∩ the registered decoders:
    `acceptEncodingValue( )`, `ClientSession.h:408`.
  - It has a strict mode.
  - It puts `tlsProfileId` and `http2ProfileId` into every `ConnectionKey` (`ClientConnection.h:403-404`).
- **The response** reserves `impersonationReport( )` (`ClientTypes.h:576`, `:665`), an empty
  `om::Object` slot. So the response's report fills a slot. The connection has no such slot (§1.3 item
  9).

### 1.2 What L7 still has to build

- A capture endpoint for real browsers.
- The four profiles' content, derived from captures, with grades and deviations.
- A JSON loader that validates untrusted input, and the `BrowserProfiles` registry.
- The browser-profile API on the session, `session -> profile( BrowserProfiles::get( "chrome" ) )`
  (design §5.8), gated to OpenSSL 3.5.
- The wiring across the layers.
- `http2/Fingerprint.h`.
- The fidelity report.
- The pinned test vectors.

### 1.3 Open gaps L7 must close

Items 1 to 5 were left for S7.3 by the layer reviews. Items 6 to 9 were found by the plan's Fable review
(`l7/reviews/plan-fable-r1.md`, F1 to F4), each checked at the source.

1. **Every client TLS connection shares one context**, the global `CryptoBase::getAsioSslContext( )`.
   - Both TLS policies' `createSocket( )` build the stream with no client context:
     `TcpSslBaseTasks.h:233`, and `TcpSslStrandedStreams.h:146`, which the HTTP client uses.
   - The wrapper already has a constructor taking a client context, but only on the `io_service` path
     (`AsioSslStreamWrapper.h:506`). There is none for a strand (`:473` takes a server context pointer).
   - A per-profile context therefore needs a change in `tasks/`, which is core code.
2. **`key.http2ProfileId` carries the session's `#h2-only` marker.** The factory must strip it before
   resolving a profile. `http2ProfileIdOf( key )` was recommended by the L6 review (§14) and never
   added.
3. **The HTTP/1.1 case map reaches no wire.**
   - `applyHttp1Casing( )` was deleted, by the second review's C1, "S7.3 can route through
     `Http1Codec`".
   - `Http1Codec::renderName( )` already takes a profile (`Http1Codec.h:1297`). The HTTP/1.1 driver
     passes none.
4. **The ALPN offer is per connection** (`ClientConnectionConfig::alpnOffer`, `h2, http/1.1` by default
   at `ClientConnectionTaskBase.h:132`, applied per attempt at `:605`; the session narrows it for the
   h2-only key at `ClientSession.h:1968`), and the preference is per context (the L4 review). A profile
   shapes the offer through the first.
5. **The session's configuration contract** (second review, C2): configured before requests are made or
   between them, never concurrently with `createRequestTask( )`. The browser-profile setter follows it.
6. **The context builder shapes a context by its cipher lists alone** (§1.1). Applying the rest of the
   TLS profile is L7's. It is a change to `crypto/CryptoBase.h`, which every OpenSSL user compiles.
7. **Four header-layer pieces exist as fields only.** `isComputed`, `priorityHeaderValue`,
   `acceptLanguageQValues`, and the version strings (`userAgent`, `secChUaBrands`, `platform`) have no
   reader. `buildRequestHeaders( )` (`ClientSession.h:551-620`) copies each value literally. Nothing
   computes `sec-fetch-*`, renders `accept-language` or the `priority` header, or composes the version
   strings into the header lists.
8. **A profile set after construction cannot reach the connection factory.** The session builds the
   pool's factory in its constructor, from values it captures by copy (`ClientSession.h:1864-1873`,
   `:1976-1992`). The pool holds it `const` (`ConnectionPool.h:887`). The L6 review said the factory
   would have to capture a profile registry (`issues/http2-l6-review-record.md:460-462`).
9. **The connection has no report.** `impersonationReport( )` is on the response only. Design §6.6 puts
   the report on the connection too, and logs it once per profile and backend. So L7 changes the
   `ClientConnection` contract (S2.6), or adds a carrier to the connection task.

---

## 2. L7 and the decompression work

**L7 starts now and does not wait for the decoders** (the maintainer's question, 2026-09-29).
- The design separated the two on purpose (D9): the seam and the intersection rule are built (§1.1),
  and "each decoder registered later raises fidelity with no other change" (design §6.5).
- The prototype in `~/dev/bl-compression`, not yet finished, targets that same seam through an adapter.
  Its own plan (§5.3) expects L7 to proceed first, with the deviation reported. Its decision D-5 makes an
  impersonating session opt in to registering decoders.

**Per slice:**
- S7.1, S7.2, S7.3 and S7.4 are unaffected. S7.3 feeds each profile's list into the existing
  intersection, and S7.4 reports what was sent.
- S7.5 pins two decoder states, so that the real decoders do not re-pin every vector (D-L7-3).

**What stays outside L7, and why none of it blocks it:**
- **Registering the decoders.** That happens when the prototype is adopted into baselib. It deletes
  each profile's `accept-encoding` deviation.
- **P1 of `issues/http-content-decoders-deferral.md`.** Decoding runs under the execution queue's
  scheduling lock, uncancellable and undeadlined. Fixing it is a core change gated on the whole suite,
  and it must land before any real decoder ships. It is the prototype's gate, not L7's.
- **TLS certificate compression** (the deferral record's item 6). The design names it one of three gaps
  between OpenSSL's ClientHello and Chrome's JA4; the other two are ALPS and ECH-GREASE. Three is the
  design's premise, not a measurement. The stock 3.5.4 hello also carries `encrypt_then_mac`
  (extension 22), which BoringSSL clients may not send, and JA4 counts a surplus extension exactly as it
  counts a missing one. L7-B diffs the sets both ways.
  - The prototype's embedded libraries never reach OpenSSL's build, so it needs OpenSSL rebuilt with
    real libraries: a devenv decision of its own.
  - The spike records the gap in the meantime.
- **The only coupling is shared files.** L7-F edits `ClientSession.h`, and so will the prototype's
  adoption and P1. Whichever reaches the file second is sequenced after the first.

---

## 3. The decisions to take before the lanes start

In the order they are needed. Decisions the design already took are not reopened: D2, D3, D9, D10,
D22, D23, and the standing rule that a change to an existing core code path lands as its own change-set,
gated on the whole suite.

### D-L7-1 — who captures the real browsers, on which hosts, and in what order

- **What it is.**
  - Every value in a profile comes from a real browser's bytes (design §6.7). Illustrative values in
    the design must not be used.
  - A capture means pointing the browser at the capture endpoint (L7-A) for a navigation, a `fetch`
    and a subresource. It records the raw ClientHello and the opening HTTP/2 frames, plus an HTTP/1.1
    visit for the case map.
  - This Linux a64 host can run Chromium and Firefox at most. Safari needs macOS, and iOS for its
    second profile (design §6.7). Edge needs Windows or macOS.
- **What happens if it is not done.** The work that needs the browsers' own values stalls:
  - S7.2 has no content;
  - the spike cannot compare our JA4 with the browser's;
  - S7.5 has no browser values to record beside ours.

  Everything else in Phase 1 can proceed.
- **Risk and reach.** None to the code. It is logistics, and it is on the critical path.
- **The undecided part.**
  - Which hosts run which browsers, and who drives them: the maintainer, or the Windows agent, which
    already runs the Windows matrix.
  - Which platform each profile's version strings name.
- **Recommendation.**
  - The Windows agent captures Chrome, Edge and Firefox on Windows. Windows is the commonest platform,
    and the agent already has a pinned-tip procedure.
  - The maintainer captures Safari on a Mac, and on iOS if an iOS profile is wanted in L7.
  - The pipeline is built end to end on **Chrome first**, the one `Ja4Candidate` family. Edge follows,
    since it is identical below the header layer, then Firefox, then Safari.
  - **Reverses** if a Mac is readily available with all four browsers: one capture session there
    covers every family. The platform strings then name macOS.
  - If Safari's captures cannot be had within L7, shipping three families first would change D10, and
    that would be put again.
- **Taken 2026-09-29.** The maintainer captures, by hand, with no automation:
  - **On Windows: Chrome, Edge and Firefox.** Those profiles' version strings name Windows.
  - **On a Mac: Safari and Edge.** Safari's profile names macOS. The Mac Edge capture is evidence, not
    a profile: diffed against the Windows Edge capture, it shows whether the Chromium shape is the same
    across platforms.
  - **Safari ships for macOS in L7.** iOS is a separate profile (design §6.7), and comes later.
- **How a capture works**, as explained to the maintainer on 2026-09-29:
  - The tool is a local HTTPS server which a real browser visits in a fresh profile, so that no
    extension or cookie adds headers. The tool never launches or drives a browser: automation can change
    the behaviour it would record.
  - It keeps the raw ClientHello, which is sent before any certificate is seen. After the handshake —
    the operator trusts a temporary local CA, or clicks through the warning — it keeps the decrypted
    opening of the HTTP/2 connection.
  - The page it serves loads a subresource and issues a `fetch`, so one visit yields all three request
    kinds. A second pass, offering only `http/1.1`, yields the HTTP/1.1 casing and order.
  - It writes the raw bytes and a summary derived from them. It runs on the machine that runs the
    browsers.
  - It takes about fifteen minutes of setup per machine, then about five minutes per browser.
  - The shape — TLS, HTTP/2 and header order — belongs to the browser's own stack: BoringSSL for Chrome
    and Edge, NSS for Firefox. The version strings belong to the operating system. That is why
    the design keeps them apart.
- **What a capture asks of the operator**, from the review's F5:
  - **A hosts-file name**, `capture.test` → `127.0.0.1`, on each capture machine. Not an IP address,
    which sends no SNI, and not `localhost`, which browsers special-case.
  - **A temporary local CA**, trusted for the session and removed after. Clicking through a
    certificate warning works, but moves the navigation onto a new connection.
  - **On the Mac, a python.org or Homebrew Python 3.** Apple's bundled one may not link OpenSSL. The
    tool prints its TLS library and refuses to run without TLS 1.3 and ALPN.
  - **The URL typed into the address bar.** Following a link changes `sec-fetch-site` and
    `sec-fetch-user`.
  - **A fresh browser profile, visited twice a few minutes apart.** Chrome fetches its field-trial
    settings soon after first start, and the two captures must agree.
  - **The browser's exact version recorded.**

### D-L7-2 — where the capture endpoint lives

- **What it is.** The endpoint does four things:
  - it terminates TLS with a local certificate, and keeps the ClientHello exactly as received;
  - it negotiates `h2`, and keeps the preface and frames up to each stream's first `HEADERS`;
  - it decodes HPACK so the header order is readable;
  - it serves a page that issues the `fetch` and loads the subresource.

  It writes raw bytes, and a summary derived from them.
- **What happens if it is not done.** There are no captures (D-L7-1).
- **Risk and reach.** A test tool; it ships nothing. The one real risk is that its parsers are wrong, and
  wrong ground truth is worse than none.
- **The undecided part — which implementation:**
  - **(a) A Python tool under `scripts/impersonation/`.**
    - The standard library's `ssl` with a `MemoryBIO` keeps the ClientHello bytes.
    - HPACK comes from the pure-Python `hpack` package, added to the dev venv's requirements, or from
      about 200 lines written in the script, Huffman table included.
    - It runs wherever Python 3 does: this host, the Mac, and the Windows venv.
  - **(b) A small C++ program built on baselib's own HPACK decoder, frame codec and ClientHello
    parser.** It adds no dependency. But the ground truth would come from the implementation it is
    meant to check.
- **Recommendation: (a), with the `hpack` package.**
  - The tool is independent of the code under test, so it cross-checks our parsers rather than
    repeating their mistakes.
  - The raw bytes are stored either way, so any derivation can be re-run.
  - **Reverses to (a) without the package** if you would rather not add a dev-only Python dependency.
    **To (b)** if a Python runtime is unavailable on a capture host.
- **Taken 2026-09-29: (a) with no new package.** The HPACK decoding, Huffman table included, is written
  in the script. It is tested against RFC 7541's Appendix C examples, and cross-checked against
  baselib's own decoder on the same bytes.
  - The whole tool is nearer 800 to 1,200 lines than the HPACK part's 200 (the review's F16). It has to
    serve the page, so it includes a minimal HTTP/2 server.
  - `ssl` never exposes the ClientHello. The tool keeps every byte it reads from the socket before
    feeding it to the `MemoryBIO`, and reassembles the hello across reads and records.

### D-L7-3 — S7.5's vectors, pinned in two decoder states

- **What it is.** The header vectors pin `accept-encoding` as sent. That value depends on the
  registered decoders, which today are none (§2).
- **What happens if it is not done.** Every header vector pins "header omitted". The day the real
  decoders are registered, every one of them has to be re-pinned, in the prototype's change-set rather
  than in L7's.
- **Risk and reach.** Test-only. A test decoder registered through the public registry is all it needs.
- **The undecided part.** Two states, or only today's.
- **Recommendation: two states per profile.**
  - With no decoders: the header is omitted and the deviation is listed.
  - With test decoders registered for `gzip`, `deflate`, `br` and `zstd`: the header is exact.

  The prototype's adoption then only confirms the second state with the real decoders.
  **Reverses** if you would rather keep L7's vectors to today's behaviour alone.
- **Taken 2026-09-29: two states**, as recommended.

### D-L7-4 — how far the spike tests the custom-extension workarounds

- **What it is.** D23 already decided the policy: GREASE, ECH-GREASE and an inert ALPS decoy through
  `SSL_CTX_add_custom_ext` are evaluated, default off, and none is adopted without evidence. What is
  open is where that evidence comes from.
- **What happens if it is not done.** The workarounds stay off, and Chrome stays `Ja4Candidate`
  without a measured match. That is safe, but it is the one lever for Chrome's JA4.
- **Risk and reach.**
  - Testing against **local server stacks** reaches nothing outside this machine: OpenSSL's
    `s_server`, nginx, Go's `crypto/tls`, and a BoringSSL-based server if one can be built here.
  - Testing against **public endpoints** sends crafted handshakes to third parties, which is an
    outward action.
- **The undecided part.** Local stacks only, or public endpoints too.
- **Recommendation: local stacks only in L7.** Record the evidence, and leave every workaround off.
  Interop against named public endpoints would be a later step, approved separately, with the list of
  endpoints. **Reverses** if you want a workaround adopted within L7. That needs public interop
  evidence, and so a list of endpoints you approve.
- **Taken 2026-09-29: off, with a light local check.**
  - The spike checks the decoy ECH and ALPS extensions against the TLS servers already on this host,
    records the result, and leaves every workaround off in L7. Nothing leaves the host.
  - Those servers are all OpenSSL-based: the system's 3.0.13 `s_server`, Python's `ssl`, and baselib's
    own test servers on 3.5.4. nginx, Go and BoringSSL are not installed. Such a check cannot justify
    turning a workaround on, which is why none is.
  - Chrome's report lists the missing extensions.
  - Testing against public endpoints is not part of L7. It would be a separate step, approved with its
    list of endpoints.

### D-L7-5 — what L7 computes for `sec-fetch-site`

Put to the maintainer on 2026-09-29, from the Fable review's F2.

- **What it is.**
  - Browsers send `sec-fetch-site` on every request. It is `none` for a navigation the user typed;
    `same-origin` when the page and the request share scheme, host and port; `same-site` when they
    share a registrable domain, such as `www.example.com` and `api.example.com`; and `cross-site`
    otherwise.
  - Telling `same-site` from `cross-site` needs the Public Suffix List, since `a.github.io` and
    `b.github.io` are different sites. The design has none (§5.6, for the cookie jar).
  - The library also does not know which page a request "comes from", unless the caller says.
- **What happens if it is not done.** Every request carries the captured literal. A same-origin `fetch`
  is then right, but a request to another origin carries a claim a WAF can check against the `origin`
  and `referer` headers it also sees.
- **Risk and reach.** `ClientSession.h` only: header computation. No transport change.
- **The undecided part.** How much the library computes, and who supplies what it cannot know.
- **The options:**
  - **(a)** Send the captured literal by default. With a caller-supplied initiator origin, compute
    `none`, `same-origin` or `cross-site`, **never** `same-site`, recompute on each redirect hop, and
    list the rule in the report as a deviation. This is the review's recommendation.
  - **(b)** As (a), and the caller may also declare a request `same-site`. Only the caller can know
    that without a suffix list. The library never guesses.
  - **(c)** Adopt the Public Suffix List as data. `same-site` is then computed exactly, and the cookie
    jar could use it too. It adds a data file to refresh, a treadmill of its own.
- **Recommendation: (b).** It is exact wherever the library can be exact. It is truthful where only the
  caller knows. It adds no data dependency. And it leaves (c) open later without an API change,
  because the initiator parameter stays.
- **Reverses** to (c) if impersonated traffic regularly spans subdomains of one site and callers
  cannot be relied on to declare it.
- **Taken 2026-09-29: (b)**, as recommended.

---

## 4. The change-sets

### 4.1 How this differs from §9

- **The capture endpoint moves first.** §9 had the capture procedure in S7.5, yet S7.1 and S7.2 need its
  output.
- **S7.2 splits in two:** the loader, validation and registry (L7-C), which needs no content, and the
  content per family (L7-E), which needs captures and grades.
- **S7.4 splits in two:** `Fingerprint.h` (L7-D), which is pure and starts at once; and the report (L7-G),
  which needs the wiring.
- **S7.1 gains the builder change it pins** (§1.3 item 6): applying the rest of the TLS profile.
- **S7.3 gains the open items of §1.3**: 1 to 5 from the layer reviews, 7 and 8 from the Fable review,
  and the header-layer computations of §4.3 items 7 to 10.
- **S7.4 gains the connection's report and its log** (§1.3 item 9).
- **S7.5 pins two decoder states** (D-L7-3), **and delivers the refresh procedure.**

### 4.2 The table

| Change-set | Slice | Delivers | Depends on | Design note | Tests |
|---|---|---|---|---|---|
| **L7-A** | §6.7 | The capture endpoint (D-L7-2), its README and self-test; the capture procedure. **Acceptance includes the review's controls (F5):** tickets off, and no hello with `pre_shared_key` (41) taken as a source; every connection recorded, with the navigation's marked; GREASE stripped; the extension *set* derived, with JA4 identical across hellos; SNI by hosts-file name; the build and field-trial-visible extensions recorded, twice; ALPN shown as `h2`; each HPACK field's representation recorded, and a cookie set so crumbling shows; two subresource destinations; a typed navigation; an interpreter check; **and the tool never closes a connection on its own initiative while a browser may still be sending** (the Windows round's G4). One connection carries the navigation, the subresource and the `fetch`, and every close it makes is a lingering one: GOAWAY (on the HTTP/1.1 pass, `Connection: close`), `shutdown( SHUT_WR )`, reads discarded until the browser's end of stream or a short bound, then `close`. The capture browsers run on Windows (D-L7-1), where a segment meeting a closed socket draws a reset that discards what the browser has not yet read (owed list W11) | D-L7-2 | no | the tool's own, with RFC 7541 Appendix C; a self-check against our own client's hello, parsed by `TlsClientHello.h` too |
| **L7-B** | S7.1 | The TLS spike, **and the builder change it pins** (F1): `createAsioSslClientContext( )` applies the groups and key shares, signature algorithms, `status_request`, SCT and padding, with the name allowlist extended to groups and signature algorithms through the shared rule header L7-C introduces. Each 3.5.4 knob is verified from our own captured hello. Each family's extension set is diffed against ours **both ways**, and every surplus or missing extension is named with its switch; a switch found, such as `encrypt_then_mac`, is added to `TlsClientProfile` and the data model (F7). JA3/JA4 per family against the browser's. The decoy ECH and ALPS are checked against this host's OpenSSL servers only, and all left off (D-L7-4). `delegated_credentials` is never emulated. A spike record goes under `issues/`. **Every addition stays inside the fake templates** (`CryptoInitT`, `AsioSslStreamWrapperT`), with no table or function at namespace scope. `utf_baselib_io` compiles these headers, and sits at 74.34 of 75 MB on `win-x86-ccl16-debug` (W16). The lane reports `io`'s a64 object delta, which should be about zero, and the Windows section builds `win-x86-ccl16-debug` whole (G8) | captures for the comparison (D-L7-1); L7-C's rule header for the builder change; none for the knob checks | its evaluation method, reviewed first | `utf_baselib_h2profiles`, pinning each verified knob. **Gate: every OpenSSL-linked module**; lands before L7-F |
| **L7-C** | S7.2a | `httpclient/BrowserProfiles.h`: JSON to `BrowserProfile` to the three typed profiles, validating untrusted input (design §6.2); `get( id )` and `load( json )`; the version strings composed into the per-kind header lists (§4.3 item 7). **The name rule** moves to an OpenSSL-free header, shared by the loader and the builder: extended to groups and signature algorithms, anonymous and NULL suites refused, every list bounded (F8). The header is chosen to keep this change-set's gate narrow (§5). Once `CryptoBase.h` includes it, it reaches `utf_baselib_io`, so its rule lives inside a fake template, not at namespace scope (G8). **Decides** how HTTP/1.1-only headers and the `Host` position are represented (F6). Compiles without OpenSSL (F14). *(Corrected 2026-09-30 by lane 1's Q1. The data model itself reaches OpenSSL, through `DataModelObject.h`'s `crypto/HashCalculator.h`, so a loader built on it cannot be OpenSSL-free. The rule header is OpenSSL-free. `BrowserProfiles.h` adds no OpenSSL edge beyond the data model's, names no OpenSSL identifier, and enters no `PreCompiled.h`. That is shown by include graph.)* | — | no | `utf_baselib_h2profiles`, fixtures only |
| **L7-D** | S7.4a | `http2/Fingerprint.h`, rendering the frames a `Session` produced. OpenSSL-free | — | no | `utf_baselib_h2profiles` or an h2core sibling, by headroom |
| **L7-E** | S7.2b | The four built-in profiles as JSON literals, derived from the captures, each with its grade and deviations. The raw captures are stored beside them as test data, read before they are committed. Chrome first. **Acceptance (F5):** every connection recorded; the profile derived from the navigation's connection; JA4 identical across all hellos; no extension 41; no GREASE value in the profile | L7-A, L7-B (grades), L7-C, captures | no | `utf_baselib_h2profiles`: each loads and validates, and each matches its capture |
| **L7-F** | S7.3 | The wiring (§4.3) and `session -> profile( BrowserProfile )`, gated to 3.5 | L7-B (builder), L7-C | **yes** | a new module, `utf_baselib_h2profiles2`, reserved |
| **L7-G** | S7.4b | The report on the response **and on the connection** (design §6.6), with the once-per-profile-and-backend debug log. Its note chooses between two shapes (F4). **(a)** An accessor on `ClientConnection`, which changes both drivers and the seven test stubs implementing it: `Http2DriverTestUtils.h:514` (twelve modules), `utf_baselib_httpclient/TestClientContracts.h:387`, `…/TestHttpClientRequestTask.h:102` and `:528`, `utf_baselib_h2client/TestClientConnectionTaskBase.h:405`, `utf_baselib_h2client4/TestConnectionPool.h:365` — W18's file, whose fix at `:82-270` must survive — and `utf_baselib_h2client11/TestNegotiatedPublication.h:715`. Its Windows section then runs `h2client4` through tier 3's runner, where W18's harness race alone reproduced (G5). **(b)** A carrier in the connection task, published the way CS-9 publishes the negotiated protocol: written once on the strand, behind an atomic flag, readable from any thread. That shape was settled on Windows (handoff D1), and V01's lesson applies: a test waits for the publication, then reads (G6). The `accept-encoding` deviation is computed per request, from what was sent (F17) | L7-D, L7-F | yes, for the report's shape | `utf_baselib_h2profiles2` |
| **L7-H** | S7.5 | The vectors per profile, in two decoder states, pinning the profile's order whatever the registration order (F17). The `NotSupportedException` rule asserted at version numbers either side of 3.5, as S3.4's test does, and the 1.1.1w run itself owed (F14). **The version-string refresh procedure**: what is re-captured, which JSON fields change, which vectors re-pin; a refresh must not move the TLS or HTTP/2 vectors (F15) | L7-E, L7-G | no | `utf_baselib_h2profiles` and `…2`; captures and vectors in `…3` if headroom requires |

### 4.3 L7-F, the wiring: what its design note must settle

0. **How a profile set after construction reaches the factory** (F3). A registry object — id → TLS
   context, `Http2Profile`, ALPN offer — captured by shared pointer, and read under C2's contract. It is
   not the session, so there is no cycle through the pool. The alternative is rebuilding the pool on
   `profile( )`, which drops live connections. Recommended: the registry.
1. **A per-profile TLS context on each connection.**
   - The context is built once per `tlsProfileId`, and cached in the registry.
   - The stranded policy needs a client-context path, and the wrapper a strand constructor taking a
     client context.
   - **Two shapes, and the note measures the choice** (F12):
     - a policy variant which hides `createSocket( )`, as the stranded policy already does. It is
       additive, but it makes the impersonating session a separate instantiation of every
       policy-parameterized layer: a second policy measured 9.8 MB for the driver alone (design §5.6);
     - a client-context member on the existing stranded policy, read by its `createSocket( )`. It
       modifies a landed `tasks/` header, so by the standing rule it is a change-set of its own, gated on
       the whole suite, landing before L7-F.
   - Either way the client-role test (`! m_serverContext`, `TcpSslBaseTasks.h:251`) stays true, so SNI
     and peer verification are unchanged. The note says so.
2. **The HTTP/2 profile per connection.** The connection factory resolves `key.http2ProfileId`, through
   `http2ProfileIdOf( key )`, into the driver config's `profile`.
3. **The header profile and the `accept-encoding` list** come from the browser profile, and feed the
   existing intersection.
4. **The ALPN offer** comes from the TLS profile's `alpnProtocols`. The h2-only key keeps its h2-only
   offer, and the report on such a connection lists the narrowed offer as a deviation (F11).
5. **The HTTP/1.1 case map.** The HTTP/1.1 driver passes the profile to `Http1Codec::renderName( )`,
   **for the casing only**; the order is the session's. `orderHeaders( )` has no caller and stays
   unused (F6).
6. **The API.**
   - `profile( BrowserProfile )` stays beside `profile( HeaderProfile )`.
   - It throws `NotSupportedException` below 3.5, through `isTlsClientProfileSupportedOnOpenSslVersion( )`.
     Only the 3.5-specific calls sit under `OPENSSL_VERSION_NUMBER >= 0x30500000L` (F14).
   - It is configured before use (C2).
   - It registers no decoder, per the prototype's D-5.
7. **The version strings in the headers** (F2). The loader composes them into the per-kind lists, so
   that a refresh is a JSON edit. The rendering is byte-exact: `sec-ch-ua` as a structured-field list,
   in its captured brand order and with its greased brand's exact spelling; `sec-ch-ua-platform`; and
   `user-agent`.
8. **`accept-language`** (F2). A session setter for the caller's languages, rendered with the profile's
   q-value ladder. The captured value is the default.
9. **The `priority` header** (F2). One source, not two: the per-kind literal in `defaultHeaders`, with
   `priorityHeaderValue` removed or made that literal's source.
10. **`sec-fetch-site`**, by D-L7-5.
11. **W17 is not this change-set's.** The library's server-side lingering close is deferred by the
    maintainer as a product question, and is not folded here, although this change-set may be open on
    neighbouring `tasks/` files (G11).

### 4.4 The phases

| Phase | Lanes | Runs |
|---|---|---|
| **1** | L7-A, L7-C and L7-D in parallel, on disjoint files. L7-B's knob checks alongside them, on `CryptoBase.h` and `TlsClientProfile.h`, which no other Phase 1 lane touches. L7-C owns the new name-rule header; L7-B's builder commit consumes it once L7-C has merged. Three lanes' worth on two build slots, as in L0 to L6 | once the plan is approved |
| **2** | L7-F, once L7-C and L7-B's builder change land. L7-E, family by family, as captures arrive, with L7-B's comparison beside it | captures gate L7-E and L7-B's second half |
| **3** | L7-G once L7-F lands. L7-H last | — |

**Two paths run side by side:**
- **The captures:** D-L7-1, then L7-A, then Chrome's capture, then L7-E, then L7-H.
- **The wiring:** L7-C and L7-B's builder, then L7-F, then L7-G.

L7-F carries a design note, its review, possibly a core change-set with a whole-suite gate, and its own
gate. So the wiring is the critical path, unless the captures take more than about three days (F16).

---

## 5. How each change-set is validated

The workflow applies unchanged:
- a brief per lane, with reserved module names;
- a design note where §4.2 says so, agreed before code;
- lane validation at clang debug, one module at a time;
- an Opus checkpoint review;
- the orchestrator's merge, tier-1 refresh, and a gate of clang release and gcc debug over the affected
  modules from the compiler's dependency files;
- the records, and v2.17's sweep.

Specific to L7:
- **Test modules** (F10).
  - `utf_baselib_h2profiles` holds the pure-data and TLS-context tests. It was 24.9 MB at clang debug
    and **49.9 MB at gcc release** at L3. The implementation plan's rule is that a slice deciding
    whether to split measures the gcc release object. Measure that before each addition, and the x86
    debug object through the Windows section.
  - The session-level cases instantiate the session and both drivers, so they go to a new module,
    **`utf_baselib_h2profiles2`, reserved**.
  - **It will be over the 40 MB target from its first case** (the Windows round's G7). The session-level
    modules the Windows matrix measured on 2026-09-29 sit at 44.4 to 56.3 MB on `win-x86-*-debug`:
    `httpclient9` 44.4/44.7, `…10` 54.6/55.3, `…5` 55.5/56.3. So its `Main.cpp` records the reason at
    creation, as theirs do.
  - **An a64-to-x86 estimate uses the measured 1.15 to 1.25** (2^20 units), not row 5c's 1.13 to 1.17.
  - **L7-F's note measures each shape of §4.3 item 1 on this module's x86 debug object**, as well as at gcc
    release. A second policy's instantiation of every layer, in a module already near 55 MB, is what
    could reach the 75 MB ceiling.
  - **`utf_baselib_h2profiles3` is reserved** for the captures and vectors, if headroom requires.
  - `data/` cannot be shared, so a module reading a capture holds its own copy.
  - **Captures are read before they are committed.** `accept-language` names the operator's locale,
    and the tool's cookie is the only one a fresh profile should carry.
- **The gate's reach** (F9).
  - A core change in `tasks/` gates on the whole suite.
  - A change to `crypto/CryptoBase.h` or `crypto/TlsClientProfile.h` reaches every OpenSSL-linked
    module, and gates on all of them. That is L7-B's builder change.
  - A change to `httpclient/ClientConnection.h` reaches both drivers, the pool and the session. That is
    L7-G's, if its note chooses the interface.
  - Everything else gates on its dependents.
  - **`utf_baselib_io` on Windows** (G8). Through `AsioSslStreamWrapper.h:20`, `CryptoBase.h` and
    `TlsClientProfile.h` are also compiled by `utf_baselib_io`, at 74.34 of 75 MB on
    `win-x86-ccl16-debug`, where the ceiling fails the build (W16; the sizes record).
    - Both headers are fake templates, so an addition inside `CryptoInitT` or another class template
      costs a module nothing it does not instantiate. L7-B and L7-C keep every new table and function
      inside one.
    - L7-B's Windows section builds `win-x86-ccl16-debug` whole, as the round did.
    - `TcpSslStrandedStreams.h` is included by no library header, so §4.3 item 1's shape (b) does not
      reach `io`.
- **The test peers, as the Windows round `f481a2d` left them** (G2, G3; the orchestrator's O4).
  - **HTTP/2.** The shared peers — `Http2TestServer.h` and `RawFrameScriptPeer.h`, through
    `HttpClientSessionTestUtils.h` — end a cleartext connection with a lingering close (W11). A scripted
    `closeConnection( )` ends the record log at `closed the connection`, the client's own close is not
    recorded, and the socket closes only once the client has closed or 5 s have passed. TLS closes as
    before. L7-F branches from a tip on which the Linux gate over those peers' twelve modules is green.
  - **HTTP/1.1.** A case against `ScriptedPeer` sends one request per script, and the script reads it
    whole before it answers. `closeSocket( )` is abortive by decision (W20, deferred as (b)). A
    multi-request keep-alive script, or one which answers on the head, is the reversing condition that
    record names: do not write one without taking (a) there first.
  - **Not the library's own `HttpServer`** as a peer in L7's new cases. Its teardown has no lingering
    close (W17, deferred).
- **What cannot run here, recorded as owed:**
  - the 1.1.1w run (S7.5). The 1.1.1w flavor does not build on this host
    (`issues/openssl-1x-flavor-deferral.md`). L7-C and L7-D add no OpenSSL dependency of their own, and
    only 3.5-specific calls are guarded, so that run is a build-and-run, not a port (F14; L7-C's wording
    corrected 2026-09-30, lane 1's Q1);
  - a Windows section in the handoff per change-set that adds tests, as for the earlier rounds.
- **The spike's measurements** go to the evidence directory before they are cited, and its record states
  what was measured and what was inferred.

---

## 6. After L7, and outside it

- **The version-string treadmill** (D10): Chrome, Edge and Firefox monthly, Safari yearly. It is an
  operating obligation once the profiles ship. L7-H delivers the procedure (F15), and its owner is a
  decision put when L7-H starts.
- **Session resumption** (D22) stays out of the first version.
- **L8** (the facade, tooling and final verification) follows L7.

## 7. Estimate

**About two weeks of run time.** *Corrected 2026-09-29 by the Fable review's F16: this said "about a
week".*
- **Phase 1 takes two to three days.** It is four change-sets, each with an Opus review loop. The capture
  tool is 800 to 1,200 lines. L7-B's builder change carries a gate over every OpenSSL-linked module.
- **Phase 2 takes four to five days.**
  - L7-F: a design note and its review, possibly a core change-set with a whole-suite gate, then its own
    gate.
  - L7-E: about half a day per family, once the captures are in.
- **Phase 3 takes two to three days.**

The captures are the one thing outside our control. The wiring path is the critical path unless they
take more than about three days.

**The Windows sections run as each change-set lands** (the Windows round's G12). A section the size of
`f481a2d`'s took the Windows agent about a day, and the agent is free. That is scheduling, and the
run-time figure above does not include it.
