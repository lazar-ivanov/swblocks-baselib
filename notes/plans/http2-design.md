# HTTP/2 Client Library: Design

**Status:** design, not implemented. Written 2026-09-17; its open items were settled with the author
the same day (section 0.2).

**Goal.** Add a comprehensive HTTP/2 client to the library, built on the existing task, stream-policy
and object-model idioms and on Boost.Asio, in C++11. Protocol capability is to match what curl gets
from nghttp2. Browser impersonation is an option, covering the TLS, HTTP/2 and header layers, with the
achievable fidelity of each layer stated rather than implied.

**Non-goal.** Changing the behavior of the existing HTTP/1.0 client in `src/include/baselib/http/`.
Nothing here alters `SimpleHttpTask`, the process-global TLS client context, or the TLS floor recorded
in `notes/plans/issues/tls-legacy-protocol-opt-in-removal-decision.md`.

**Companion records.**

- `notes/plans/issues/http2-server-side-deferral.md` - production HTTP/2 in `HttpServer`
- `notes/plans/issues/http-content-decoders-deferral.md` - how response decompressors are supplied

---

## 0. Decision ledger

### 0.1 Decided with the author, 2026-09-17

| # | Decision |
|---|---|
| D1 | **devenv7+ only.** The library is compiled out on devenv2-6 using the negative-filter pattern of `scripts/devenv7/AGENTS.md`. |
| D2 | **OpenSSL flavors.** Everything except impersonation works on both devenv7 flavors (3.5.4, and 1.1.1w under `BL_USE_OPENSSL_1X`). Requesting an impersonation profile on anything below OpenSSL 3.5 throws `NotSupportedException`. |
| D3 | **TLS backend: a seam, OpenSSL behind it.** All TLS-library-specific code is confined to the stream policy class. No new TLS dependency. A measured fidelity spike is part of the work (section 6.3). BoringSSL is never required; the seam only keeps it possible. |
| D4 | **Below-floor suites: advertise, verify, refuse.** Impersonation uses a per-profile `SSL_CTX`, never the global one. It advertises the browser's list, keeps security level 2, the TLS 1.2 floor and chain verification, and aborts after the handshake - before any HTTP byte - if the negotiated parameters are below the library floor. |
| D5 | **Client layers in scope:** HTTP/1.1 fallback; proxies via HTTP `CONNECT` and SOCKS5 (HTTPS proxies excluded); redirects and a cookie jar; content decoding as a **seam only** (D9). |
| D6 | **Transparent retry of provably unprocessed streams** (GOAWAY last-stream-id, `REFUSED_STREAM`) is part of the core, not an option. |
| D7 | **Generic capabilities live outside `http2/`.** The URL parser is a generic library capability with no dependency on HTTP/2 code. The same rule is applied to every other piece that is not HTTP/2-specific (section 3). |
| D8 | **Server side:** the protocol core is role-neutral and a test-only peer is built on it under `src/utests/`. Production HTTP/2 in `HttpServer`, with its DoS hardening, is deferred - see the companion record. |
| D9 | **Decoders:** the seam ships; no decompressor ships. How to supply them is deferred - see the companion record. |
| D10 | **Profiles:** Chrome, Edge, Firefox and Safari ship built-in and tested, each carrying an explicit fidelity grade and deviation list. |
| D11 | **Legacy HTTP/2 features: neither.** No server push (always `SETTINGS_ENABLE_PUSH = 0`; a `PUSH_PROMISE` is a connection error, per RFC 9113 section 8.4). No `h2c` via `Upgrade`. Cleartext HTTP/2 by prior knowledge is supported. |
| D12 | **Documents** live under `notes/plans/`, deferrals under `notes/plans/issues/`. |

**D2 is not currently satisfiable, and this is not specific to this design.** Executing 3.8 commit 4
established that `BL_USE_OPENSSL_1X=1` does not build today, for reasons that predate this work: the
3.x configuration is compiled with `-DOPENSSL_API_COMPAT=0x10100000L` and the 1.1.1w configuration
gets no equivalent, so `-Werror,-Wdeprecated-declarations` fails on `RSA_free`, the `SHA512_*` and
`SHA384_*` family and the `RSA`/`EVP_PKEY` conversions - headers reached by anything that includes
`crypto/CryptoBase.h`. Separately, no devenv7 dist on the development machine carries 1.1.1w at all.
So D2's promise cannot be demonstrated for **any** part of this work until that is resolved.

**Decided 2026-09-18: deferred, not abandoned** - `notes/plans/issues/openssl-1x-flavor-deferral.md`.
D2 stands as design intent; it is simply unverified on one of its two flavors, for the whole library
rather than for this feature. Nothing here waits on it. A slice that touches OpenSSL still guards by
version where this design already says to, and states in its acceptance that the second flavor is
owed rather than claiming both. The evidence of what could not be produced, and why, stays at
`notes/plans/issues/openssl-1x-evidence-not-producible-record.md`.

### 0.2 Decided on review, 2026-09-17

The first draft of this document raised twelve open items, O1-O12. The author settled all of them.
The old numbers are kept in the second column because earlier notes refer to them.

| # | Was | Decision | Section |
|---|---|---|---|
| D13 | O1 | **Executor-bound sockets.** Stream objects are constructed on a strand; handlers are not wrapped one by one. | 3.1 |
| D14 | O2 | **Layout:** `http2/`, `httpclient/`, and generic pieces where they belong. | 2.2 |
| D15 | O3 | **The HTTP/1.1 codec starts on Boost.Beast**, to see where it goes, with stated criteria deciding whether it stays. The Beast interfaces used are **abstracted and isolated in the `bl` namespace the same way the library's other Boost interfaces are.** **Settled 2026-09-19 by S2.5: Beast stays**, all four criteria measured - `notes/plans/issues/d15-http1-codec-backend-verdict.md`. | 5.5 |
| D16 | O4 | **In-house URI parser.** Boost.URL is rejected because it is not header-only. | 3.4 |
| D17 | O5 | **`BL_TASKS_HANDLER_END_IMPL` is generalized** for multi-operation tasks - subject to D19. | 3.2 |
| D18 | O6 | **`TcpConnectionEstablisherConnector` gains the pre-handshake hook** - subject to D19. | 3.6 |
| D19 | - | **The core base class changes of D17 and D18 are tested comprehensively, and land with their tests as a separate change, gated on the entire test suite passing.** No feature code rides with them. Extended by D26. | 3.8 |
| D20 | O7 | Area-based test module names. | 8.1 |
| D21 | O8 | Connection coalescing is designed, and default off in the first version. | 5.4 |
| D22 | O9 | TLS session resumption: the extension is advertised and never resumed, in the first version. | 6.3 |
| D23 | O10 | Custom-extension emulation of GREASE, ECH-GREASE and ALPS is decided by the spike and default off; `delegated_credentials` is never emulated. | 6.3 |
| D24 | O11 | URI parsing is RFC 3986 strict; no IDNA. | 3.4 |
| D25 | O12 | The non-goals list is confirmed. | 12 |
| D26 | - | **The two pure refactors of existing code paths join the gated change-set of D19** - the `createSocket` extraction in `TcpSslBaseTasks.h`, and the three-step split of `initNativeSslContext` in `CryptoBase.h`, refactor only - as separate commits, each with its own tests; one gate covers all four. **Purely additive API lands with the phase which first uses it**, validated by the focused modules. | 3.8, 10 |

Nothing is open.

---

## 1. What exists, and what it dictates

**1.1 The HTTP client is one-shot.** `SimpleHttpTaskT< STREAM >`
(`src/include/baselib/http/SimpleHttpTask.h:49`) is one task, one connection, one request: HTTP/1.0,
`Connection: close`, read until EOF. It derives from `TcpConnectionEstablisherConnector< STREAM >`
(`src/include/baselib/tasks/TcpBaseTasks.h:1267`), which supplies resolve, connect, handshake and the
handshake retry. `STREAM` is a policy class with a documented static interface
(`TcpBaseTasks.h:447-463`), implemented by `TcpSocketAsyncBase` and `TcpSslSocketAsyncBase`.

HTTP/2 keeps the `STREAM` seam and the establisher, and inverts the lifecycle: a long-lived connection
with many concurrent requests over it.

**1.2 Full-duplex I/O on a TLS stream has never been done here.** Messaging avoids it by design:
`MessagingClientFactory.h` opens separate inbound and outbound connections. HTTP/2 needs a pending
read and a pending write on one `asio::ssl::stream`. Asio documents shared `ssl::stream` objects as
unsafe unless every asynchronous operation runs in one strand, and the task lock does not provide
that: Asio's own intermediate SSL handlers run outside it, on the threads of the
`ThreadPoolId::NonBlocking` pool - up to four (`src/include/baselib/core/ThreadPool.h:111`,
`core/AppInitDone.h:186-190`). `asio::strand_t` is
typedef'd (`core/detail/OSBoostImports.h:78`) but used in one place only, with the version gate
written inline (`TcpBaseTasks.h:2243`).

**1.3 The handler macros assume one operation in flight.** `BL_TASKS_HANDLER_END_IMPL`
(`src/include/baselib/tasks/TaskBase.h:195`) completes the task from whichever handler fails first.
`notifyReadyImpl` runs `scheduleTaskFinishContinuation` and `onTaskStoppedNothrow` *before* its
`m_notifyCalled` guard (`TaskBase.h:547-596`), so a second failing handler re-enters the finish path
of a task that is already finishing. With a read, a write and timers outstanding together, that is the
normal failure mode, not an edge case.

**1.4 TLS is one global hardened client context.** `CryptoInitT::initNativeSslContext`
(`src/include/baselib/crypto/CryptoBase.h:257`): TLS 1.2 floor, security level 2, forward-secret AEAD
suites only, `SSL_OP_NO_TICKET`, session cache off. There is no ALPN anywhere. A client cannot supply
its own context: `AsioSslStreamWrapperT` takes a context pointer only for the server role
(`tasks/AsioSslStreamWrapper.h:332-350`). Per-connection `SSL*` access does exist - SNI is set that way
(`tasks/TcpSslBaseTasks.h:247`).

**1.5 OpenSSL is an optional dependency of the library.** `src/include/baselib/http/PreCompiled.h`
says so explicitly, and headers needing OpenSSL are kept separate. The new code follows that.

**1.6 Dependencies are minimal and deliberate.** Boost is built with `date_time system thread
filesystem program_options regex random test locale json`
(`scripts/devenv7/linux/build-boost-linux.sh:367`). OpenSSL is configured with `no-shared` only
(`scripts/devenv7/linux/build-openssl-linux.sh:539`). There is no zlib, brotli or zstd, and no
`boost_iostreams`. So there is no decompressor, and OpenSSL has no certificate compression.

**1.7 `http::HeadersMap` cannot carry what is needed.** It is an
`unordered_map< string, string >` (`http/Globals.h:29`): no order, no repeated names. Header order is
a fingerprint input and `set-cookie` repeats.

**1.8 `utf_baselib_http2` is taken.** It is the numbered sibling of `utf_baselib_http`, holding
HTTP/1.x TLS tests.

---

## 2. Architecture

### 2.1 Layers

```
  application
      |
  httpclient::ClientSession ------ profiles, cookie jar, redirect policy,        version-neutral
      |                            decoder registry, proxy configuration
  tasks::HttpClientRequestTask --- one per request; protocol-agnostic
      |
  httpclient::ConnectionPool ----- keys, stream slots, queueing, retry, coalescing
      |                       \
  tasks::Http2ConnectionTask   tasks::Http1ConnectionTask                       I/O shell,
      |                              |                                          templated on STREAM
  http2::Session (sans-I/O)     httpclient::Http1Codec (sans-I/O)               no Asio, no OpenSSL
      |
  STREAM policy: TcpSocketAsyncStrandedBase | TcpSslSocketAsyncStrandedBase      the TLS seam (D3)
      |
  TcpConnectionEstablisherConnector: resolve, connect, tunnel, handshake         existing, one hook added
```

**The protocol cores are sans-I/O.** `http2::Session` and `Http1Codec` take bytes in and give events
and bytes out. They know nothing of Asio, OpenSSL, tasks, threads or locks, and they are **not
templated on `STREAM`**. Three consequences, all wanted:

- They are instantiated once, which matters under the test-module size policy of
  `src/utests/AGENTS.md`. Only the thin I/O shell is a template.
- They are testable byte-for-byte without sockets, deterministically. "The HTTP/2 layer of a profile
  is exact" becomes an offline golden-bytes test.
- `http2::Session` is role-neutral, which is what D8 requires.

### 2.2 Layout (D14)

The author suggested `src/include/baselib/http2/`. That is where everything HTTP/2-specific goes. D7
pushes the rest outward, and the author accepted the result:

| Location | Namespace | Contents |
|---|---|---|
| `core/Uri.h` | `bl::net` | URI parsing, resolution, normalization |
| `core/detail/BeastBoostImports.h` | `bl::beast` | the only file which includes Boost.Beast (5.5) |
| `core/ErrorHandling.h` | `bl::eh` | new `errinfo_*` typedefs and exception declarations (additive) |
| `tasks/TaskBase.h` | `bl::tasks` | the multi-operation handler macro (3.2) |
| `tasks/MultiOperationTask.h` | `bl::tasks` | pending-operation accounting mix-in (3.2) |
| `tasks/TcpStrandedStreams.h` | `bl::tasks` | stranded plain stream policy (3.1) |
| `tasks/TcpSslStrandedStreams.h` | `bl::tasks` | stranded TLS stream policy (3.1) - needs OpenSSL |
| `tasks/TcpTunnelStage.h` | `bl::tasks` | `CONNECT` and SOCKS5 tunnel stage (3.6) |
| `crypto/TlsClientProfile.h` | `bl::crypto` | TLS client shaping, per-profile contexts, floor check (3.3) |
| `crypto/TlsClientHello.h` | `bl::crypto` | ClientHello capture; JA3 and JA4 (3.3) |
| `http/HeaderList.h` | `bl::http` | ordered multi-value header list (3.5) |
| `http2/*.h` | `bl::http2` | frames, HPACK, flow control, stream states, `Session`, fingerprint |
| `http2/Http2ConnectionTask.h` | `bl::tasks` | the HTTP/2 I/O shell |
| `httpclient/*.h` | `bl::httpclient` | session, pool, HTTP/1.1 codec, cookies, redirects, decoder seam, profiles |
| `httpclient/Http1ConnectionTask.h`, `HttpClientRequestTask.h` | `bl::tasks` | tasks, following where `SimpleHttpTask` lives |
| `data/models/HttpClientProfiles.h` | `bl::dm::httpclient` | JSON data models for profiles, following `data/models/Http.h` |

Dependency direction is one way: `httpclient/` depends on `http2/`; `http2/` depends on
`http/HeaderList.h`, `core/` and, for its task only, `tasks/`. Nothing generic depends on either.

**`http2/Http2ConnectionTask.h` is the one exception, and it was always going to be** (recorded in
S4.2, when it landed). It is the HTTP/2 driver, so it has to derive from the establishment base and
implement the connection contract - `httpclient/ClientConnectionTaskBase.h` and
`httpclient/ClientConnection.h`, both of which S2.6 and S4.1 put in `httpclient/`. The row above
already places this header in `bl::tasks` rather than in `bl::http2` for the same reason: it is the
I/O shell and not the protocol. Everything else under `http2/` - the codec, HPACK, the state
machine, the flow control and the session engine - depends on nothing in `httpclient/` and must not
start to. That is the direction which matters, because it is what keeps the engine sans-I/O.

`http/` is left as the legacy simple client plus shared low-level types. Putting the new
version-neutral client into a separate `httpclient/` keeps the legacy code visibly untouched.

Every new class uses the library's header-only idiom: `template< typename E = void > class FooT`,
`typedef FooT<> Foo`, `typedef om::ObjectImpl< Foo > FooImpl`, statics via `BL_DEFINE_STATIC_MEMBER`.

---

## 3. Generic capabilities added outside `http2/`

### 3.1 Stranded stream policies - the Boost.Asio capability adopted (D13)

**The author asked for a suggestion where a recent Asio capability is compelling. This is the one, and
it was accepted.**

Since Boost 1.70 an I/O object carries an executor. A socket constructed on a strand makes that strand
the default executor for *every* handler of *every* operation on it - including the intermediate
handlers of Asio's composed operations, which is precisely the part the task lock cannot reach
(1.2). Nothing has to be wrapped by hand, so nothing can be forgotten. With D1 this is available
unconditionally (Boost 1.90).

The alternative is `asio::bind_executor( strand, handler )` at every call site. It works, and it is
what `TcpBaseTasks.h:2243` does once. At the scale of a connection task - dozens of call sites across
read, write, timers, tunnel and handshake - one missed wrap is a data race that no test reliably
finds. **Decided (D13): executor-bound.**

How it is wrapped, so existing code is untouched:

- Two new policy classes, `TcpSocketAsyncStrandedBaseT` and `TcpSslSocketAsyncStrandedBaseT`, deriving
  from the existing policies and *hiding* `createSocket` (the static interface is resolved by template
  composition, not virtually, so hiding is the right mechanism). `createSocket` creates
  `m_strand = asio::make_strand( aioService )` and constructs the socket or SSL stream on it.
- They add to the static interface: `getStrand()`, `createTimer()` (a `deadline_timer` on the strand),
  and `postToStrand( handler )`.
- `AsioSslStreamWrapperT` gains a constructor taking the strand. `asio::ssl::stream`'s constructor
  already forwards its first argument to the next layer, so this is a forwarding overload.
- `TcpSslSocketAsyncBaseT::createSocket` has the SNI logic inline (`TcpSslBaseTasks.h:227-251`). It is
  extracted into `configureClientStream()` so the stranded policy does not duplicate it. Pure
  refactor, and it lands in the gated change-set (3.8, commit 3).
- The resolver stays on the plain `io_service`; its handler touches no stream state.

Everything composing on `STREAM` - the establishers, the server base - works unchanged with either
policy family.

`cancelTask()` in the existing policies calls `shutdownSocket` from whichever thread requested the
cancel (`TcpSslBaseTasks.h:352`). The stranded policies **post** that shutdown to the strand instead,
so it is serialized with Asio's internal SSL handlers rather than racing them.

**Considered and not proposed:** `async_compose` (the task state machine already is one),
per-operation cancellation slots (cancelling an HTTP/2 stream is a `RST_STREAM` frame, not an Asio
cancel), `experimental::channel` (a write queue under a strand is a `deque`), `bind_allocator`
(a later optimization, invisible to the design), io_uring (a build option), awaitables (C++20).

Note that `BOOST_ASIO_DISABLE_STD_CHRONO` is defined (`OSBoostImports.h:29`), so timers are
`asio::deadline_timer` on the wall clock, as everywhere else in the library. The design keeps that; a
clock step can fire or delay a timeout, which is an existing property, not a new one.

### 3.2 Tasks with several operations in flight (D17, D19)

This is the generic fix for 1.3. It changes `TaskBase.h`, which every task in the library includes, so
it is accepted on a condition: it is tested comprehensively and lands as a separate, gated change. What
that means is set out in 3.8. Two parts.

**A macro generalization.** `BL_TASKS_HANDLER_END_IMPL( expr )` hard-codes `notifyReady` on the failure
path. It becomes a one-line forwarder to a new `BL_TASKS_HANDLER_END_IMPL_EX( exprOnSuccess,
exprOnFailure )`. The expansion for every existing user is identical. A new
`BL_TASKS_HANDLER_END_MULTIOP()` routes both paths to `onOperationCompleted( eptr, isExpected )`.

**A mix-in**, `tasks::MultiOperationTaskT`, holding the accounting:

- `beginOperation()` increments a counter before each `async_*` call.
- `onOperationCompleted( eptr, isExpected )` decrements it, records the *first* error, and on the first
  error calls the virtual `initiateClose()` - cancel the socket operations and the timers.
- `notifyReady( firstError )` is called exactly once, when the counter reaches zero while closing.
- `isClosing()` is public, because the read loop of 5.1 needs it to decide whether to re-arm.

**It is parameterized on its base, not on nothing.** The shape is
`template< typename BASE = TaskBase > class MultiOperationTaskT : public BASE`, with a forwarding
constructor - `BL_VARIADIC_CTOR`, which owns the constructor definition, so the accounting members
are initialized in class rather than in an init list there is no way to write - and
`typedef MultiOperationTaskT<> MultiOperationTask`. This is what the word "mixed in"
in 5.1 requires: `Http2ConnectionTaskT` derives from `TcpConnectionEstablisherConnector< STREAM >`,
which already has `TaskBase` in its chain, so a mix-in hard-wired to `TaskBase` would give that task
two `TaskBase` subobjects and could not be combined with the establisher at all. Stated here because
the first implementation of 3.8 commit 1 built it as `template< typename E = void > : public TaskBase`
and the gap was only found when S4.1 was read against it.

**A derived task has two paths out and they must stay consistent:** `cancelTask()` for an external
cancel, and `initiateClose()` for the error path. Both converge on the single terminal `notifyReady`.

The rule this enforces: **a connection task has one terminal path, and takes it only after every
outstanding operation has completed or been cancelled.** That also keeps the TLS shutdown
continuation of `TcpSslSocketAsyncBase` from starting while a read or write is still pending.

Any future full-duplex task in the library needs the same thing, which is why it is generic.

### 3.3 TLS client profiles, contexts and the floor (D3, D4)

**`crypto/TlsClientProfile.h`.** A TLS profile is data: ordered TLS 1.2 suite names, ordered TLS 1.3
suites, groups with key-share marks, signature algorithms, ALPN list, and switches for the session
ticket extension, OCSP status request, SCT request and padding. It is not HTTP-specific.

**Per-profile contexts.** `initNativeSslContext` is refactored into three composable steps, with the
global context and the server contexts calling all three exactly as today:

1. protocol floor, options, security level - **common, never parameterized**;
2. cipher policy - the hardened alias list by default, or the profile's explicit list;
3. trust anchors.

The split itself is a pure refactor of security-critical code and lands in the gated change-set
(3.8, commit 4). What is built on it, `createAsioSslClientContext( profile )`, is additive and lands
with its first consumer, in P6.

A profile context is step 1, then the profile's step 2, then trust. `SSL_OP_NO_TICKET` is cleared only
if the profile advertises tickets. The session cache stays off. Contexts are cached per profile id.

Trust is **shared, not copied**: `SSL_CTX_set1_cert_store( profileCtx, SSL_CTX_get_cert_store(
globalCtx ) )`. One store, so no divergence between the default path and profiles. The verify callback
is unchanged - it is set per stream by `AsioSslStreamWrapper`.

**Profiles are loadable from JSON (6.2), so cipher strings are an injection surface.** A cipher string
can carry `@SECLEVEL=0`, which overrides `SSL_CTX_set_security_level`
(`tls-legacy-protocol-opt-in-removal-decision.md`, "Cipher lists"). The loader therefore accepts only
explicit suite names matched against an allowlist of characters, no aliases - and the context builder
asserts the level is still 2 after applying the list.

**The allowlist is a positive rule, not a list of forbidden characters.** Amended in S3.4, which found
the original wording unusable: it said "no `@`, `!`, `+`, `-`, `:` inside a name", and taken literally
that rejects **every TLS 1.2 suite name OpenSSL knows** - they are all of the form
`ECDHE-RSA-AES128-GCM-SHA256` - which would leave every profile's TLS 1.2 list empty. The rule is
instead: **a name is non-empty, every character is `[A-Za-z0-9_-]`, and the first character is
alphanumeric.** That refuses everything the original meant to and more. `@`, `!`, `+` and `:` cannot
appear at all, so `@SECLEVEL=0` and the alias operators are unreachable. `-` is admitted inside a
name, where it is part of the spelling, but not in first position - which is the only place OpenSSL
reads it as the "remove these ciphers" operator. And `,` and space are refused as well, which the
original did not name although they separate tokens exactly as `:` does.

**The floor check.** After the handshake and before `continueAfterConnected()` - therefore before the
HTTP/2 preface or any HTTP byte - the negotiated parameters are checked: version at least TLS 1.2, and
an **authenticated ephemeral AEAD** suite, which is three axes and not two -
`SSL_CIPHER_get_kx_nid` in `{ NID_kx_ecdhe, NID_kx_dhe, NID_kx_any }`, `SSL_CIPHER_get_auth_nid` in
`{ NID_auth_rsa, NID_auth_ecdsa, NID_auth_dss, NID_auth_any }`, and `SSL_CIPHER_is_aead`. A TLS 1.3
suite satisfies all three without being special-cased, reporting `NID_kx_any` and `NID_auth_any`. A
failure throws `SecurityException` carrying the negotiated suite and version as error info. It is
enforced whenever a non-default client context is in use.

**The authentication axis was added in the L3 review round** and it is what makes the next sentence
true. Without it the predicate admitted `ADH-AES128-GCM-SHA256` and `ADH-AES256-GCM-SHA384` - ephemeral
and AEAD, authenticating nobody - which Appendix A names as
`TLS_DH_anon_WITH_AES_128_GCM_SHA256` and `TLS_DH_anon_WITH_AES_256_GCM_SHA384`. Nothing was ever
exposed by that, because OpenSSL's `ssl_security_default_callback` refuses an unauthenticated suite at
every security level above 0 whatever its strength (`ssl/ssl_cert.c`, the `SSL_aNULL` arm of
`SSL_SECOP_CIPHER_*`), and every context this library builds pins level 2 - but that is a behaviour of
OpenSSL which this document never named, while D4 names the floor check.
The axis is stated as the accepted authentications rather than as a refusal of `NID_auth_null`, so the
property does not depend on which NID a given OpenSSL maps an anonymous suite to and an unfamiliar
authentication method fails closed. The PSK and SRP families need no axis of their own: their key
exchanges are `NID_kx_psk`, `NID_kx_dhe_psk`, `NID_kx_ecdhe_psk`, `NID_kx_rsa_psk` and `NID_kx_srp`,
none of which the first axis accepts.

A second, independent layer sits in front of it: `createAsioSslClientContext` appends `!aNULL:!eNULL`
after the profile's names in the TLS 1.2 cipher list, so the suites are not offered in the first place.
The tokens are the builder's own text, not a profile's, so the name allowlist above is untouched; they
go on the TLS 1.2 list only, because `SSL_CTX_set_ciphersuites` reads its argument as suite names and
silently ignores anything else.

This is strictly stronger than the cipher blocklist of RFC 9113 Appendix A, so
`INADEQUATE_SECURITY` never needs to be raised by us. Read off the appendix text: everything it lists
that is not AEAD is refused by `SSL_CIPHER_is_aead`, and its 56 AEAD entries carry exactly eight name
prefixes - `RSA`, `DH_RSA`, `DH_DSS`, `ECDH_ECDSA`, `ECDH_RSA`, `PSK`, `RSA_PSK` and `DH_anon`. The
first seven are a key exchange outside `{ NID_kx_ecdhe, NID_kx_dhe, NID_kx_any }`; the eighth is what
the authentication axis exists for. Nothing ephemeral, authenticated and AEAD is in the appendix at
all, which is how it was generated.

**ALPN.** `SSL_set_alpn_protos` per connection; `SSL_get0_alpn_selected` after the handshake. Trap
worth recording: `SSL_set_alpn_protos` returns **0 on success**, the inverse of most of the API, so it
must not go through `BL_CHK_CRYPTO_API_NM`. No selection by the server means HTTP/1.1.

**Wrapper changes** (`AsioSslStreamWrapperT`, all additive): a client-context pointer distinct from the
server flag (today a non-null pointer *means* server); ALPN offer and result; negotiated version and
suite; a ClientHello capture hook.

**`crypto/TlsClientHello.h`.** `SSL_set_msg_callback` captures the exact ClientHello bytes we send. A
parser turns them into JA3 and JA4 strings. This is what makes the fidelity report (6.6) honest: it
states what was *sent*, not what was *requested*.

### 3.4 URI (D7, D16, D24)

`core/Uri.h`, `bl::net::UriT`. No dependency on any HTTP code.

- Parse into scheme, userinfo, host (IPv6 literals included), port, path, query, fragment.
- Reference resolution per RFC 3986 section 5, for redirects.
- Normalization: scheme and host case, dot-segment removal, percent-encoding of unreserved characters.
- `origin()` (scheme, host, effective port) for pool keys, cookie scoping and cross-origin checks;
  `authority()` and `pathAndQuery()` for `:authority` and `:path`.
- Complements, does not replace, `str::uriEncode` / `uriDecode` (`core/StringUtils.h:183`).

**Strictness (D24).** RFC 3986, strict: control characters, whitespace and backslashes are errors. The
WHATWG leniency browsers apply is a known source of parser differentials between a validator and a
fetcher. IDNA is not supported: a non-ASCII host is an error and callers pass A-labels.

**Boost.URL is not used (D16).** It exists, it is C++11, and wrapping Boost is the library's habit. But
it is **not header-only**: it is a compiled library, so it would need `--with-url` added to every Boost
build script and the dists rebuilt on every platform, for one parser of about 400 lines. The parser is
written in-house. The public API above is deliberately backend-neutral, so the implementation can be
switched later without touching a caller - which is the README's stated reason for encapsulating
dependencies.

### 3.5 Ordered header list

`http/HeaderList.h`, `bl::http::HeaderListT`: a vector of name/value pairs preserving insertion order
and original case, with case-insensitive lookup, multi-value access, and conversion to and from
`http::HeadersMap` for the compatibility facade. Validation helpers reject CR, LF and NUL in values
and non-token characters in names; the HTTP/2 encoder additionally lowercases and the HTTP/1.1
serializer keeps the case given.

### 3.6 Tunnel stage (D5, D18, D19)

`tasks/TcpTunnelStage.h`. A tunnel runs after TCP connect and before the TLS handshake, in cleartext
on the lowest layer of the stream. Two implementations:

- **HTTP `CONNECT`**: `CONNECT host:port HTTP/1.1`, optional `Proxy-Authorization: Basic`, a minimal
  status-line-and-headers reader bounded to 64 KB, success on 2xx.
- **SOCKS5** (RFC 1928, RFC 1929): no-auth and username/password, `DOMAINNAME` addressing so the proxy
  resolves, bounded replies.

With a proxy, the resolver targets the proxy while SNI and peer-name verification stay with the
origin. `continueAfterResolved` is virtual, so a derived establisher passes the origin name to
`createSocket`.

Inserting the stage needs one thing. `TcpConnectionEstablisherConnector::onConnectionEstablished`
(`TcpBaseTasks.h:1315`) goes straight from connect to `beginProtocolHandshake` and is not virtual.
**Decided (D18):** one protected virtual, `beginPreHandshakeStage( continueCallback )`, whose default
invokes the callback immediately - behavior-preserving for every existing user. The alternative was a
derived class that duplicates the roughly thirty lines of that handler; duplicating connection
establishment logic is how the two copies drift. Like 3.2 this touches a base class every TCP client
task derives from, so it lands under the conditions of 3.8.

The contract, which the tests of 3.8 pin down:

- `onConnectionEstablished` passes the hook a continuation which performs exactly what is inline
  today: `beginProtocolHandshake( continueAfterConnected )`. With the default hook the same call is
  made synchronously, in the same handler, under the same lock, with the same result.
- An override returns `true` if it started asynchronous work, and later invokes the continuation from
  its own handler - inside the task handler macros, the way `onHandshakeCompleted` does
  (`TcpSslBaseTasks.h:530-551`). If it completes synchronously it returns the continuation's result.
- A failure of the stage fails the task **before** any handshake is attempted.
- The handshake retry of `scheduleTaskFinishContinuation` (`TcpBaseTasks.h:1389`) restarts the whole
  resolve and connect transaction, so the stage runs **once per attempt** and must carry no state from
  one attempt to the next.

HTTPS proxies need TLS inside TLS, a stream layered on a stream; `AsioSslStreamWrapper` is hard-wired
to a TCP socket. Excluded (D5).

### 3.7 Error information

Additive, in `core/ErrorHandling.h` next to `errinfo_http_*` (`:473`):

- `errinfo_http2_error_code`, `errinfo_http2_stream_id`, `errinfo_http2_goaway_last_stream_id`,
  `errinfo_http2_debug_data`, `errinfo_http2_is_retryable`
- `errinfo_http_alpn_selected`, `errinfo_tls_negotiated_cipher`, `errinfo_tls_negotiated_version`
- `BL_DECLARE_EXCEPTION( Http2ProtocolException )` for connection errors and
  `BL_DECLARE_EXCEPTION( Http2StreamException )` for stream errors

HTTP status failures keep using `HttpException` with `errinfo_http_status_code`, exactly as today, so
`addExpectedHttpStatuses` semantics and existing catch sites carry over. Secure-mode redaction
(`SimpleHttpTask.h:187`, `:368`) is preserved: URL path, headers and bodies are `[REDACTED]` in
exceptions and logs.

### 3.8 The gated core change-set (D19, D26)

The author accepted the two changes to core base classes - 3.2 and 3.6 - on a condition: **they are
tested comprehensively, and they land with their tests as a separate change, gated on the entire test
suite passing.** The two pure refactors of existing code paths were then placed under the same rule
(D26). This section says what the change-set contains, what "comprehensively" means for each part, and
what the gate is.

**Scope.** Four commits, each independently reviewable, and nothing else:

| Commit | File | Change |
|---|---|---|
| 1 | `tasks/TaskBase.h`; new `tasks/MultiOperationTask.h` | `BL_TASKS_HANDLER_END_IMPL_EX`, with `..._END_IMPL` as a forwarder to it; `BL_TASKS_HANDLER_END_MULTIOP`; the mix-in of 3.2 |
| 2 | `tasks/TcpBaseTasks.h` | `beginPreHandshakeStage` (3.6) |
| 3 | `tasks/TcpSslBaseTasks.h` | `configureClientStream()` extracted from `createSocket` (3.1) |
| 4 | `crypto/CryptoBase.h` | `initNativeSslContext` split into its three steps (3.3) - **the refactor only** |

The mix-in is in commit 1 rather than in a feature phase because it is the only consumer of the new
macro, and so it is what makes the macro testable. Commit 4 stops at the split: the new
`createAsioSslClientContext( profile )` factory is additive, has no consumer until P6, and lands
there.

No stranded policy, no HTTP/2 code, and no additive row of section 10 rides with it.

**It goes first.** Everything in the I/O shell depends on it; it depends on nothing else in this
design; and it is the only part of the work which can break code that already exists. Doing it first
retires that risk before anything is built on top.

**Characterize first, then change.** Wherever the existing behavior can be observed without the
change, the test which pins it is committed **before** the commit it guards, and passes on both sides
of it. A test written after a refactor can only ever agree with the refactor.

#### Commit 1 - the macro (D17)

`BL_TASKS_HANDLER_END*` is expanded at 58 sites in 20 files under `src/`. Three layers of evidence,
strongest first.

**1. The expansion is unchanged, shown mechanically.** The body of `..._END_IMPL_EX` is the old body
verbatim, with the success expression and the failure expression as its two parameters, and
`..._END_IMPL( expr )` forwards to it with today's `notifyReady` call as the failure expression. So
the preprocessed token stream of every translation unit the makefiles build - test modules, apps,
plugins - must be the same before and after.

One difference is expected and it is constant. The edit moves the lines of `TaskBase.h` beneath it,
and `BL_EXCEPTION` records `__LINE__` (`core/ErrorHandling.h:50`), so integer literals which come from
expansion sites *inside* `TaskBase.h` shift by one fixed amount. The check is therefore a small
stdlib-only script beside `utf_inventory.py` - `scripts/utests/utf_ppstream.py`, as built: preprocess
each translation unit on the parent commit and on the change, strip the line markers, tokenize, and
require the two streams to be equal except for integer literals which differ by exactly that amount.

This is worth the trouble because it proves what no test enumerates: that the order of the catch
clauses, the expected-exception classification and the scope of the task lock all survived. At least
one existing test already depends on *which* catch clause of the macro handles an exception
(`src/utests/utf_baselib_http/TestClientHttpTasks.h:1189`). The technique has precedent here:
`src/utests/AGENTS.md` uses "the preprocessed translation unit is unchanged" as the gate for splitting
a test header.

**2. New tests for the new path**, beside the existing task tests in `utf_baselib_tasks`, headroom
permitting, otherwise a numbered sibling. Each uses a test-only task with several timers or loopback
operations in flight:

| Case | Asserts |
|---|---|
| all operations succeed | one completion, no error |
| one fails while others are pending | `initiateClose()` runs once; the task completes only **after** the others have completed; the error reported is the first |
| several fail | the first error wins; exactly one completion |
| cancel with N operations pending | `operation_aborted`, classified as expected; exactly one completion |
| `initiateClose()` itself throws | the task still completes exactly once, with the original error |
| the finish continuation | `scheduleTaskFinishContinuation` is entered **once**, and only after the pending count is zero - the direct regression test for the defect of 1.3 |
| restart of a completed task | the accounting is reset by `scheduleNothrow` |
| stress, multi-threaded pool | many operations completing concurrently; run under ThreadSanitizer |
| the same suite, single-threaded pool | no case depends on a second thread |

**3. The entire suite** - the gate below.

#### Commit 2 - the hook (D18)

The contract is in 3.6. Plain-stream cases go beside the existing TCP task tests in
`utf_baselib_tasks`, TLS cases beside `TestAsioSslStreamWrapper.h` in `utf_baselib_http2`, both
headroom permitting. Each uses a test-only connector which overrides the hook and records what it
saw. What can be characterized first is today's order - connect, handshake, `continueAfterConnected` -
through the existing `continueAfterConnected` virtual; the cases which need the hook to exist follow
it.

| Case | Asserts |
|---|---|
| default hook, plain and TLS | the call order is connect, hook, handshake, `continueAfterConnected`; the handshake continuation runs synchronously inside the hook, in the connect handler |
| asynchronous stage | the hook returns `true`; the handshake starts only when the stage invokes the continuation |
| the stage fails | the task fails with the stage's exception; **no handshake is attempted**; the socket is closed |
| cancel during the stage | `operation_aborted`, classified as expected; no handshake |
| cancel before connect completes | the hook is never invoked |
| retryable handshake error | the transaction restarts and the hook runs **once per attempt** (`TcpBaseTasks.h:1389`) |
| the continuation returns `false` | the task completes, as it does today when `continueAfterConnected` returns `false` |

#### Commit 3 - the `createSocket` extraction (D26)

`TcpSslSocketAsyncBaseT::createSocket` (`TcpSslBaseTasks.h:212`) is on the path of every TLS client
and every TLS server task in the library. The commit moves the SNI decision (`:227-251`) into
`configureClientStream()` and changes nothing else.

- **Already characterized.** `TlsHandshake_SniOmittedForAddressLiterals`
  (`src/utests/utf_baselib_http/TestTlsHandshakeVerification.h:558`) captures the raw ClientHello and
  asserts both halves of the decision: the extension is present for a name and absent for an IPv4
  literal. The server role - a server context, so no SNI call at all - is exercised by every TLS
  server test in the suite.
- **To add first.** The same assertion for an IPv6 literal, which takes the same branch today and is
  not pinned.

The stranded policy of 3.1 calls `configureClientStream()` as well, and repeats the capture for
itself; that is P0, not this commit.

#### Commit 4 - the `initNativeSslContext` split (D26)

This is the security-critical one. Every TLS context in the library is built by this function: the
process-global client context (`CryptoBase.h:465`) and every server context (`:493`). Three layers
again.

**1. The observable configuration is unchanged, shown mechanically.** A small probe dumps, for the
global client context and for a server context: the option bits; the minimum and maximum protocol
version; the security level; the **ordered** list of cipher suites, and the TLS 1.3 suites; the
session cache mode; the verify mode and depth; and the number of trust anchors in the store. It is
run on the parent commit and on the change, and the two dumps must be identical. This is the
counterpart of commit 1's token-stream check, and it covers what the permanent tests deliberately do
not: `TlsProtocolPolicy_CipherSuitesAreAeadOnly` asserts a property of the list, not the list, because
a literal list would break on every OpenSSL upgrade. For proving a refactor, the literal list is
exactly what is wanted - once.

**2. Already characterized, and must pass unchanged:** `TestTlsProtocolPolicy.h` in
`utf_baselib_http2` - the floor and the security level are pinned; the hardening options are pinned;
the suites are AEAD only; TLS 1.0 and 1.1 are refused; bad server key material is rejected.

**3. To add first:** that the trust anchors loaded into a context are the registered ones, by count;
and the session cache mode of each role - off for the client, `SSL_SESS_CACHE_SERVER` for a server
(`CryptoBase.h:471`, `:500`). The split does not touch the cache mode, but profile contexts will rely
on the client value (D22), so it should be pinned before anything builds on it.

**Both OpenSSL flavors.** The function is full of `#if OPENSSL_VERSION_NUMBER` branches, so the
evidence above, and the TLS modules in the gate, are produced on 3.5.4 **and** on 1.1.1w under
`BL_USE_OPENSSL_1X` - as they were for the TLS floor change (see "Status" in
`notes/plans/issues/tls-legacy-protocol-opt-in-removal-decision.md`).

#### The gate

- **"The entire test suite" is every `utf_*` module, on the toolchains and variants `AGENTS.md` names.**
  On Linux that is what `scripts/devenv7/linux/run-matrix.sh` runs - `gcc1520` and `clang2010`, debug
  and release, builds at `-j1`, tests at `-j5` - and on Windows `msvc` and `clang`. Narrowing that
  matrix is the author's call when the change is executed. It is not assumed here.
- **Baseline-relative, not merely green.** `scripts/utests/utf_runlog.py` already compares what the
  test binaries did between two runs. Capture the suite on the parent commit - twice, which is how the
  tool derives its list of nondeterministic cases - capture it on the change, and `--compare`. The gate
  passes when, for every pre-existing case, the registered set, the executed set, pass or fail, and
  the assertion counts of the deterministic cases are the same, and the new cases pass.
  `run-matrix.sh` already keeps the per-module logs the tool parses, under
  `utflogs-<toolchain>-<variant>/`.
- **One gate, on the tip of the change-set, covers all four commits.** A gate costs roughly three
  full-suite runs per toolchain and variant - two for the baseline, one for the change - which is why
  the commits share one rather than having one each. Each commit must also build and pass its own
  focused modules by itself, so that a regression the gate finds can be bisected across four commits.
- **An intermittent failure is not re-run until it goes green.** It is reproduced, or explained
  against the records under `notes/plans/issues/`, and the gate result says which.
- **Nothing which depends on these commits is merged before the gate passes.** Afterwards the feature
  phases do not modify the code these commits touched: they add new names to those files, or new
  files, only. If a later phase finds it must change that code again, that is a new gated change, not
  an amendment.

---

## 4. The HTTP/2 protocol core

All of this is `bl::http2`, sans-I/O, role-neutral. RFC 9113 throughout, RFC 7541 for HPACK.

### 4.1 Frame codec - `http2/FrameCodec.h`

- The 9-byte header; incremental parsing across arbitrary read boundaries.
- All ten frame types parsed and validated: `DATA`, `HEADERS`, `PRIORITY`, `RST_STREAM`, `SETTINGS`,
  `PUSH_PROMISE`, `PING`, `GOAWAY`, `WINDOW_UPDATE`, `CONTINUATION`. Length checks per type
  (`FRAME_SIZE_ERROR`), stream-id rules, padding validation (pad length not less than the payload is
  a `PROTOCOL_ERROR`), the reserved bit ignored.
- Unknown frame types are ignored, except inside a header block, where they are a connection error.
- Inbound frames larger than our advertised `SETTINGS_MAX_FRAME_SIZE` are a connection error before
  the payload is buffered.
- Serialization of every type we send, including optional `PRIORITY` fields on `HEADERS` and optional
  padding.

### 4.2 HPACK - `http2/Hpack*.h`

- Static table; dynamic table as a ring with the RFC's size accounting (name + value + 32).
- Integer codec with overflow protection; string literals with length limits.
- Huffman: the code table is a constant; the decode state machine is **generated from it at first
  use** under C++11 static-local initialization, rather than embedding a second hand-copied table that
  can disagree with the first. Padding is validated: at most seven bits, all ones; an encoded EOS is an
  error.
- Decoder: dynamic table size updates only at the start of a block and never above what we advertised;
  anything else is `COMPRESSION_ERROR`.
- **Bounded work.** Cumulative decoded size is checked against the section 4.6 row - our
  `SETTINGS_MAX_HEADER_LIST_SIZE` when the profile advertises more than the row, the row otherwise,
  and in force from construction because that setting is advisory (RFC 9113 section 6.5.2) -
  *during* decoding. On overflow the decoder keeps consuming the block - the dynamic table must stay
  in sync - but discards the fields, and the stream is reset. Separately, compressed header-block
  bytes and `CONTINUATION` frame count per block are capped; exceeding either is a connection error
  with `ENHANCE_YOUR_CALM`.
- Encoder: a policy object decides per field between indexed, incremental-indexed, literal and
  never-indexed, and whether Huffman is shorter. Cookie crumbling (RFC 9113 section 8.2.3) is a
  profile switch, since browsers do it.

### 4.3 Stream state machine - `http2/StreamStateMachine.h`

The RFC 9113 section 5.1 machine, parameterized by role. The reserved states exist in the type for
completeness and are unreachable (D11). Stream ids are odd and monotonic for the client; approaching
`2^31 - 1` marks the connection as draining so the pool opens another.

Recently closed streams are remembered for a bounded time. Frames arriving on a stream we have reset
are tolerated, and - the detail that is easy to miss - **`DATA` on a closed stream still consumes the
connection window and must still be credited back**, or the connection window leaks until the
connection stalls.

### 4.4 Flow control - `http2/FlowControlWindow.h`

Signed 32-bit windows at connection and stream level, in both directions. A `SETTINGS_INITIAL_WINDOW_SIZE`
change adjusts every open stream and may drive a window negative, which is legal. A zero increment is
a stream `PROTOCOL_ERROR`; overflow past `2^31 - 1` is `FLOW_CONTROL_ERROR`.

Receive side: `WINDOW_UPDATE` is sent only as the **consumer** acknowledges data, at a threshold of
half the window. This is the backpressure mechanism of the whole client (5.3).

### 4.5 The session engine - `http2/Session.h`

`SessionT` is the counterpart of nghttp2's session. Single-threaded by contract; the strand provides
that.

**Pull-style, both directions.** `feed( bytes )` parses and appends to an internal event queue; the
caller drains it afterwards. There are no callbacks out of `feed`, so nothing can re-enter the session
mid-parse - the usual source of bugs in callback-driven protocol engines. Outbound, the caller asks
`wantsWrite()` and `produce( buffer )`.

Events: headers (interim 1xx, final, trailers), data, stream closed (with error code and a
**retryable** flag), settings received, settings acknowledged, ping acknowledged, GOAWAY (with last
stream id), connection error.

Commands: submit request (header list, priority, whether a body follows), provide body bytes, reset
stream, consumed (drives `WINDOW_UPDATE`), ping, GOAWAY, apply local settings.

**Message validation** (RFC 9113 sections 8.1-8.3): pseudo-headers first and only the defined ones;
exactly one three-digit `:status`; no uppercase names; no connection-specific fields (`connection`,
`keep-alive`, `proxy-connection`, `transfer-encoding`, `upgrade`; `te` only as `trailers`); no CR, LF
or NUL in values; `content-length` consistent with the `DATA` total; `101` invalid; trailers carry no
pseudo-headers. A malformed message is a stream `PROTOCOL_ERROR`.

**Write scheduling.** Control frames (`SETTINGS` ack, `PING` ack, `WINDOW_UPDATE`, `RST_STREAM`,
`GOAWAY`) precede everything. A header block with its `CONTINUATION` frames is atomic. `DATA` is
scheduled by RFC 9218 urgency, round-robin within an urgency, each frame bounded by the peer's
`SETTINGS_MAX_FRAME_SIZE` and both windows. Request bodies are **pulled** only when window and queue
space allow, so nothing buffers ahead of the peer *inside the engine*. The qualification is not
pedantry: this sentence was read as a statement about the client as a whole, and until S5.1 the shell
above the engine had no pull at all — see §5.3.

**Settings.** Ours are sent from the profile's ordered list (6.4). The peer's are applied and
acknowledged; unknown ids are ignored; out-of-range values are connection errors. An unacknowledged
`SETTINGS` of ours times out as `SETTINGS_TIMEOUT`.

**Early responses.** A complete response followed by `RST_STREAM( NO_ERROR )` while we are still
uploading is a success; we stop sending (RFC 9113 section 8.1).

### 4.6 Limits - client side

A client needs fewer defenses than a server, not none. All are configurable, with these defaults.

| Limit | Default | On breach |
|---|---|---|
| Decoded header list size | 64 KB, or our `SETTINGS_MAX_HEADER_LIST_SIZE` when the profile advertises more | stream reset; block still consumed |
| Compressed header block bytes | 256 KB | connection error, `ENHANCE_YOUR_CALM` |
| `CONTINUATION` frames per block | 64 | connection error |
| Queued control-frame bytes (acks owed to a peer that will not read) | 64 KB | connection error |
| Inbound `PING` and `SETTINGS` per second | 100 | connection error |
| Remembered closed streams | 1000 or 30 s | oldest forgotten |
| Response body, buffered mode | 64 MB, matching `SimpleHttpTask.h:83` | stream `CANCEL`, exception |
| Retries of one request (D6) | 3 | fail with the last error |

### 4.7 Deliberately absent

Server push and `h2c` via `Upgrade` (D11). Extended `CONNECT` and WebSockets (RFC 8441). The
`PRIORITY` dependency tree as a *scheduler* - RFC 9113 deprecates it; we send whatever priority signals
a profile dictates and ignore the peer's. `ORIGIN` frames are parsed and recorded, and only consulted
if coalescing is enabled (D21).

---

## 5. The I/O shell

### 5.1 `Http2ConnectionTaskT< STREAM >`

A long-running task deriving from `TcpConnectionEstablisherConnector< STREAM >`, like `SimpleHttpTaskT`
but long-lived, with `STREAM` one of the stranded policies and `MultiOperationTaskT` mixed in.

It is a **task**, not a bare object, for concrete reasons: connection establishment with its handshake
retry is already task logic; the handler macros give exception enhancement, expected-exception
classification and logging; and an execution queue of connection tasks gives the pool the shutdown
semantics `TcpServerBase` already relies on for `m_eqConnections` (`TcpBaseTasks.h:1733`).

**Retry budget.** That handshake retry is reachable with a real peer since the classifier was
widened after L0 (see the plan, §2 decision 2): it restarts the whole resolve/connect/handshake
transaction on a truncated handshake as well as on `eof`, with no delay, up to `MAX_RETRY_COUNT`
(5), so a peer which consistently truncates costs six attempts. `m_maxRetryCount` is the derived
task's to set; S4.1 chooses the h2 task's budget and records it in its acceptance.

**Lifecycle.** Resolve, connect, optional tunnel, TLS handshake, floor check, ALPN result.
`continueAfterConnected()` returns `true` - the task keeps running - and starts:

1. **The opening write**, assembled whole and flushed in one `async_write`: client preface,
   `SETTINGS`, connection `WINDOW_UPDATE`, any profile `PRIORITY` frames, and the first request's
   `HEADERS` if one is waiting. Browsers coalesce these, so we do.
2. **The read loop**: `async_read_some` into a `data::DataBlock`, `Session::feed`, drain events,
   re-arm.
3. **The write pump**: exactly one `async_write` in flight, gathering what `Session::produce` offers;
   re-armed on completion while `wantsWrite()`.
4. **Timers**, on the strand: connect deadline, `SETTINGS` ack, optional keepalive `PING` and its
   deadline, idle close.

It ends on GOAWAY drained, idle timeout, fatal error, or cancel. On a graceful end it sends
`GOAWAY( NO_ERROR )` best-effort; the inherited `isCloseStreamOnTaskFinish( true )` then performs the
TLS shutdown once no operation is pending (3.2).

`DATA` payloads are copied once, from the read buffer into pooled `data::DataBlock`s
(`data::datablocks_pool_type`), and handed on by reference.

### 5.2 The concurrency model

Three kinds of actor: connection tasks, request tasks, the pool. **They communicate by posting, never
by calling into each other under a lock.**

| # | Rule |
|---|---|
| L1 | A connection's protocol state (`Session`, write queue, stream table) is owned by its strand. Only strand handlers touch it. |
| L2 | Code on a connection strand never acquires a request task's lock, the pool lock, or an execution queue lock - other than through its own terminal `notifyReady`, which `TaskBase` already calls without the task lock. |
| L3 | Request to connection: `postToStrand`. Connection to request: the request's **mailbox** plus a posted drain. Connection to pool, and pool to request: posted notifications. |
| L4 | The pool lock and mailbox locks are leaf locks. Nothing is called while holding one. |

**Why L2 is a rule and not a preference.** Suppose a connection dispatched "stream closed" into a
request task synchronously, while holding its own task lock `C`. The request completes and calls
`notifyReady`, which enters the user's execution queue; that queue schedules the next pending request,
whose start asks the pool for a connection; the pool creates one and pushes it onto the pool's queue,
taking that queue's lock `Ep`. Meanwhile another thread is shutting the pool down with `cancelAll`,
holding `Ep` and waiting for `C`. That is a deadlock built entirely from individually reasonable
calls. It is the same hazard the header of `TaskBase.h` warns about, with more hops. Posting removes
the edge.

**Mailboxes preserve order.** Posting two events to a multi-threaded `io_service` does not guarantee
they run in order. So each request task has a mailbox: a small mutex-protected deque plus a "drain
scheduled" flag. The connection appends and, if no drain is scheduled, posts one. The drain runs under
the request's own lock and consumes everything in order.

**Nothing blocks.** No actor ever waits on another; every interaction is a post and a return. So the
model is correct with a single thread in each pool, which the header of `TaskBase.h` (`:74-100`) asks
of task designs, and which `AppInitDone.h:186-190` makes possible for the I/O pool.

Request-task handlers run on `ThreadPoolId::GeneralPurpose`, leaving the I/O threads to I/O; cookie
handling, redirects and eventually decompression are not I/O.

### 5.3 `HttpClientRequestTaskT` and backpressure

One per request, protocol-agnostic: it talks to a `ClientConnection` interface that both connection
tasks implement (submit, cancel, consumed). The header event the driver delivers carries the response
status **beside** the header list, because `HeaderList` cannot hold `:status` (3.5) and the session
engine strips the pseudo-headers when it validates - S2.6 as first published omitted the status; the
L2 review found it and the plan records the fix. **Each header block carries its own status**, so an
interim block brings the 1xx it is (103 Early Hints being the one that matters) and the final block
brings the response's; the request task fills `ClientResponse::status()` from the final one only.

`ClientResponse::negotiatedAlpn()` was unfillable for the same reason and is fixed the same way: the
connection reports a **`NegotiatedProtocol`** - the protocol together with the identifier the peer
selected verbatim - rather than the protocol alone, and that one value fills both response fields.
The identifier is **empty whenever ALPN did not decide the connection**, which a cleartext connection
and a TLS connection whose peer selected nothing both are; a connection which fell back must not
report `"http/1.1"` as though the peer had chosen it, and that is the one case a value derived from
the protocol gets wrong. Because the only constructor which sets a non-empty identifier derives the
protocol from it, the two halves cannot disagree and neither has to be named authoritative.

`scheduleTask` only posts a start handler and returns, so nothing pool-related runs under the user's
queue lock, honoring the contract at `TaskBase.h:857-869`. The start handler asks the pool for a
connection; the pool answers by posting back. Completion is `notifyReady` from the request's own drain
handler, the ordinary way.

**Backpressure is HTTP/2 flow control, end to end.** The connection credits a stream's window only
when the request task reports bytes consumed. So a request's mailbox can never hold more than the
stream window we advertised, and a connection never more than the connection window. A slow consumer
slows the server, which is what flow control is for.

Two body modes. **Buffered**, the default and what `SimpleHttpTask` does: the body accumulates up to
the size limit. **Streaming**: a `BodySink` receives `DataBlock`s and acknowledges consumption; a
`BodySource` is pulled for uploads and declares whether it can rewind, which decides replayability
(5.4).

**The upload pull reaches the source, and did not until S5.1.** §4.5's "an upload never buffers ahead
of the peer" was true of the session engine and false of the shell: `Session::bodyBytesWanted()` is a
hard contract the driver obeys, but the `ClientConnection` contract as S2.6 published it had no event
running the other way, so the driver could not ask the layer above for more and simply held whatever
`provideBody()` gave it. A request task therefore had two possible behaviours and only one of them was
an implementation — hand the whole source over, buffering the entire upload inside the *driver*, or
hand over a bounded amount and stall for want of any progress signal. S4.2 found this and left it;
S5.1 closed it by adding `ClientStreamEventSink::onBodyWanted( handle, bytes )`, which carries the
pull one hop further so the bytes stay with the source until the windows can take them. One pull is
answered by exactly one `provideBody()`, which bounds the driver to a single un-placed chunk per
stream; the pull is outside the response ordering guarantee, since it concerns the request body and
interleaves freely with the response; and the HTTP/1.1 driver never raises it, because it refuses a
`BodySource` at `submit()` and so has nothing to ask for.

**Where that pull stops.** `BodySource::read()` is synchronous and `BodyReadResult` admits a source
which yields nothing without being finished. Such a source answers the pull with an empty non-final
chunk and the driver re-raises when its room next grows — a read or a write completion — so on a
connection carrying no other traffic it can still stall. Closing that needs a readiness signal on
`BodySource` itself, a change to the frozen `ClientTypes.h`; it is recorded in
`issues/body-source-readiness-deferral.md` rather than left implicit.

### 5.4 `ConnectionPoolT`

**Key:** scheme, lowercased host, port, proxy identity, TLS profile id, HTTP/2 profile id,
verification flags. The HTTP/2 profile belongs in the key because the connection-level fingerprint is
per connection.

**Connection states:** `Connecting`, `Ready`, `Draining`, `Closed`. A `Connecting` placeholder is
inserted under the pool lock *before* the lock is released and the task is created, so concurrent
requests for the same key queue behind it instead of each opening a connection. The connection task is
pushed onto the pool's private queue (`OptionKeepNone`) outside the pool lock (L4).

**Dispatch.** Prefer a `Ready` connection with a free stream slot. Slots are bounded by the peer's
`SETTINGS_MAX_CONCURRENT_STREAMS`; until the peer's `SETTINGS` arrives, 100 is assumed - the first
request goes out with the preface, as browsers do it. Otherwise wait, FIFO, up to the request's own
deadline. Policy knobs: connections per key (1 for HTTP/2 by default, 6 for HTTP/1.1), total
connections, idle lifetime, keepalive `PING` interval.

Three things S5.2 settled about that paragraph. The count which limits dispatch is the **pool's
own**, not `freeStreamSlots()`: a request the pool has answered has not reached the driver's strand
yet, so that value is stale *high* for as long as that takes; it is read only for its zero and to
latch the peer's limit at a moment when the pool holds no slot. The request which rides the preface
must be **replayable**, because a connection which is still establishing may yet turn out to speak
`http/1.1` and bounce it - a request which cannot be replayed waits for `Ready` instead, which
costs it milliseconds. And the pool caps what it will put on one connection whatever the peer
advertises (`maxStreamsPerConnection`, 256), since RFC 9113 section 6.5.2 gives that setting no
upper bound and one connection absorbing every request also means every request failing together.

**What the L5 review found in that latch, and what the fix round did.** "At a moment when the pool
holds no slot" is right about the arithmetic and wrong about the clock: the driver publishes `Ready`
*before* it writes the preface - which is design 5.1's rule and the establishment contract S4.1 and
S5.2 both read - so the first such moment precedes the peer's `SETTINGS` by a round trip, and what
was latched was the driver's assumed 100. A peer which allows 16 was then sent bursts sized to 100
for as long as the connection stayed busy, and each burst above 16 lost requests rather than
retrying them, because the dispatched half of the retry below belongs to nobody yet. `Ready` was not
moved; the pool changed. It now dispatches **one** stream to a connection whose limit it has not
been told, which is design 5.1's own preface rule applied by the dispatcher, and it learns the limit
in one of three ways: a reading outside `[ assumed - dispatched, assumed ]`, which the driver's
assumption could not have produced; a response which came back in full, since a peer's `SETTINGS` is
the first frame it sends (RFC 9113 section 3.4) and frames are applied in order; or, for the peer
whose limit *is* the assumed number and which therefore never distinguishes itself, a one second
settle window, after which the reading is taken as the peer's - which is what the code did
unconditionally before, so the window costs only the round trip it buys. Between the exact moments
`freeStreamSlots() + <dispatched>` is an upper bound on the limit which the pool only ever takes
downward, and that is also what bounds one examine's burst to what the driver last reported. The
driver reporting one free slot until the peer's `SETTINGS` (L5 finding 5(c), the driver's own
change-set) would remove the settle window's cost, since a reading of one is one the assumption
cannot produce.

**The draining reserve, chosen in S5.2: 1024 identifiers.** Section 4.3's "approaching `2^31 - 1`"
is a margin `StreamRegistry` leaves to the pool, and it has to cover what the pool has committed to
a connection but not yet opened as a stream - bounded by the dispatch ceiling above. 1024 is four
times that ceiling and costs under one millionth of a connection's 1.07 billion identifiers, while
a margin too small leaves nothing in hand for what the pool has already committed to that
connection - a dispatch ceiling's worth of requests, past the pool's own check and not yet opened,
each of which then bounces back retryable at the one moment a connection can least afford it. It reaches the registry through `SessionLimits::drainingReserve`, which S5.2
added for it - the pool configures the driver, the driver configures the session, and the session
configures the registry.

**What makes the margin visible is the driver, and the L5 review found it missing.** The registry
begins draining silently, so `Http2ConnectionTaskT::applySubmit()` asks `isDraining()` at both of
its answers and publishes `Draining` for it, and `publishFreeStreamSlots()` stores zero whenever
`canOpenStream()` is false. Without those the connection went on reading `Ready` with slots free
past the margin and the pool went on dispatching to it, which is the stream of bounces the reserve
is chosen to prevent, arriving one reserve later rather than never; the retirement this section
describes then follows from the existing "last stream closed while `Draining`" path.

**There is no connection-to-pool notification, and rule L2 of 5.2 anticipates one.** A driver
publishes its state into an atomic and offers nothing to subscribe to, so the pool cannot be told
that a connection became `Ready`, that an establishment is overdue or that a queued request's
deadline has passed. Until a driver exposes that posted notification, the pool **looks**, on a
maintenance tick which runs only while it has a waiter or a connection which is not usable yet, and
whose interval backs off from 10 ms to 250 ms for every tick that changes nothing. That is the seam
the notification plugs into; nothing above the pool would change.

**Retry (D6).** A request is replayed on another connection when both hold:

- *provably unprocessed*: its stream id is above a GOAWAY's last-stream-id; or it was reset with
  `REFUSED_STREAM`; or the connection failed before any byte of the request was written;
- *replayable*: its body is buffered or its source can rewind.

Bounded by the retry limit (4.6). A request the server may have processed is **not** retried
automatically; retrying idempotent methods after connection loss is a separate knob, default off.

**Which half of the retry the pool counts, settled in S5.2.** The *rule* is one predicate,
`chkRequestMayBeReplayed` in `httpclient/ConnectionPool.h`, and both halves use it. The *counter*
cannot be in one place, because the S2.6 contract gives the pool no request identity: `acquire`
takes a `ClientRequest` by reference and `releaseStream` names a handle the pool never issued. So a
request still queued in the pool when the connection it was queued behind failed is replayed and
counted **by the pool**, which never let go of it. The pool's part of the guarantee is that such a
request cannot land back on the connection which failed it: a connection reported
`ConnectionUnusable`, or observed `Draining` or `Closed`, is retired before the next `acquire` is
answered.

**The other half has no owner yet, and this sentence used to say it did** (L5 finding 5(a)). S5.2
wrote that a request which had already been dispatched "comes back through `releaseStream` and a
fresh `acquire`, and its attempts are counted by the request task, which has per-request state by
construction" - the state exists, the counter does not. `HttpClientRequestTaskT` calls `acquire`
exactly once and holds no attempt count; it reports `isRetryable()` and `outcome()` and leaves the
decision to its caller, which today is nobody. So a `REFUSED_STREAM`, a GOAWAY above the stream's
id or an ALPN bounce of the preface rider **fails** its request while the requests queued behind it
are retried, and that asymmetry is a property of the tree rather than of the design. **S6.1 owns
it**: the session is the first thing above the request task which sees a request end and can start
another, and it is where the two halves of the counter meet. Until it does, the pool must not make
a bet it cannot pay for - which is why the dispatch section above dispatches one stream to a
connection whose limit is unknown instead of assuming.

**Landed in S6.1, and the ALPN bounce named above is not hypothetical - it is what found it.**
`SessionRequestTaskT::chkPrepareRetry()` holds the per-hop attempt count and applies the same
`chkRequestMayBeReplayed` predicate, so both halves now count and neither invents a rule. What
proved it is the session running against the library's own `HttpServer`: the pool dispatches the
first request of a key onto the `Connecting` placeholder so that its `HEADERS` ride the preface,
and over a **fallback** connection that placeholder is the HTTP/2 task, which hands the connected
stream to the HTTP/1.1 driver and completes - answering the rider it still holds with
`connection_aborted`, correctly flagged retryable because not a byte of it was written. So without
this half **every** first request over a fallback connection fails, which is what that case did on
its first run. It is pinned with the control which makes the failure certain: the same request with
`maxRetriesPerRequest` of zero, asserted red.

**GOAWAY.** Mark `Draining`, stop dispatching to it, replay what qualifies, let in-flight streams at or
below the last id finish. Servers commonly send two - first with `2^31 - 1`, then the real id - and
both are handled.

**What the pool's forget-cancel costs on this route.** When the last of those streams comes back the
entry is retired with no slots out, so the pool forgets it and cancels its task - the invariant that
a connection the pool forgets is one nothing else will stop. On *this* route the driver is already
closing: the same strand handler which posted that last stream's `onClosed` called
`closeGracefully()` on an empty table, so by the time the release reaches the pool the GOAWAY is
queued and the drain deadline armed, and the cancel races that write. The task then ends as a cancel
marked expected rather than as a success, and the peer sees the GOAWAY only if it had already left
the socket buffer - which for a nine-byte frame it ordinarily has. That is classification and a
SHOULD, not correctness, and it is the shape the drain deadline has for the same reason: both go
through `requestCancelInternal()`. The connection the cancel is really for is the one which is
`Draining` with nothing to drain - a session refused at birth - or a healthy HTTP/1.1 connection a
request task reported `ConnectionUnusable`.

**Coalescing (D21).** RFC 9113 section 9.1.1 allows reusing a connection for another host if the
certificate covers it and the host resolves to the connection's address. The check would reuse
`TlsPeerVerification::certificateMatchesPeerName` (`crypto/TlsPeerVerification.h:100`) against the
saved peer certificate; a `421` forces a dedicated connection and is remembered. It is an optimization
with security subtlety, so: designed, **default off** in the first version.

S5.2 ships the knob and **refuses** it rather than ignoring it: a caller who sets
`enableCoalescing` is told it is not implemented, because the failure mode of believing otherwise
is a request for one host being sent on another host's connection. The design above is repeated at
the knob, where whoever implements it will be standing.

**Disposal.** The session is `om::Disposable`, and so is the pool - disposing it fails queued
requests with `operation_aborted`, cancels the connection tasks, and flushes the connection queue,
the `TcpServerBase` pattern. What it does **not** do is send a `GOAWAY` first, and that is a
limitation rather than a choice: a driver has no public "say `GOAWAY` and close" entry point -
`closeGracefully()` is its own, taken from its idle timer, its drained `GOAWAY` and its last stream
closing - so the graceful path an ordinary connection takes is the **idle lifetime the pool
configures**, and disposal is the abrupt one. From the *destructor* the pool answers nobody, on
purpose: a request which is waiting holds the pool it is waiting on, so a pool being destroyed has
no listener left, and that destructor can run during process teardown, when posting to the thread
pool is fatal rather than merely late.

### 5.5 HTTP/1.1 and ALPN fallback (D5, D15)

A browser profile offers `h2, http/1.1`, so a server may choose `http/1.1` - the library's own
`HttpServer` among them. The protocol is decided per connection, after the handshake; the request task
does not care.

`httpclient::Http1Codec`, sans-I/O: a request serializer honoring profile header order and case, and a
response parser handling the status line, headers, `Content-Length`, chunked transfer coding with
trailers, read-until-close, and bodiless responses (1xx, 204, 304, `HEAD`). Response-side smuggling
defenses: conflicting `Content-Length` values are an error; `Transfer-Encoding` together with
`Content-Length` is refused outright - RFC 9112 section 6.3 says such a message "ought to be handled
as an error", and S2.5 took that reading rather than letting the coding win and closing afterwards;
obsolete line folding is rejected in the header section (a fold in a chunked trailer section is
unfolded by the backend and not detected - the trailer fields are restricted to a safe subset, so
this is recorded rather than closed); headers are capped at 64 KB, matching `SimpleHttpTask.h:77`.
Every check in the codec folds case and trims whitespace ASCII-only, never through `std::locale()` -
the rule `http/HeaderList.h` states, for the reason it gives: a global locale an embedder installs
must not be able to change what a security check compares. The L2 review found the
`Transfer-Encoding` gate (`str::trim_copy`) and the profile case-map lookup (`str::to_lower_copy`)
had broken it; both were fixed.

`Http1ConnectionTaskT< STREAM >` serves one request at a time and returns to the pool if the response
was fully consumed and neither side said `Connection: close`.

**Boost.Beast (D15).** The codec starts on Beast, to see where it goes - and, as of S2.5, stays there. Beast is header-only, so there
is no dist rebuild; it is C++11, usable sans-I/O, and heavily fuzzed; and HTTP/1.1 response parsing is
exactly where hand-written code goes subtly wrong. Against it: Beast is template-heavy, and test module
object size is a real constraint here. The library also already has a hand-written request parser
(`httpserver/Parser.h`), so there is precedent both ways - hence "see where it goes", against the
criteria below.

**What is used, and what is not.** Only Beast's sans-I/O parser core, `http::basic_parser`: the codec
derives from it and receives the status line, each field, body bytes, chunk headers and trailers as
callbacks, which it turns directly into the library's own `http::HeaderList` and events. Beast's
`fields`, `message`, body types, serializer and stream algorithms are **not** used. The request
serializer is in-house, about a hundred lines: exact header order and case are the whole point of it,
they must be ours byte for byte, and there is nothing subtle in it for Beast to get right on our
behalf. Using the least of Beast that does the job is also what keeps it easy to isolate and cheap to
compile.

**Isolation, the way the library does it for its other Boost interfaces.** Three layers:

1. **One import header**, `core/detail/BeastBoostImports.h`, in the form of `TimeBoostImports.h`. It
   is the only file which includes `<boost/beast/...>` - the narrow headers, never the `beast.hpp`
   umbrella - between `BoostIncludeGuardPush.h` and `BoostIncludeGuardPop.h`. It brings **exactly the
   names the codec uses** into `bl::beast` and `bl::beast::http` with individual `using` declarations,
   so the surface we depend on is explicit and can be read off one file. Like `core/AsioSSL.h` it is
   not reachable from `BaseIncludes.h` or any `PreCompiled.h`; only the codec backend includes it.
   **S2.5 found this and the plan's umbrella convention point opposite ways**, since the facade
   includes the backend, so listing the facade in `httpclient/PreCompiled.h` would make Beast
   reachable from an umbrella. The property wins: it is a stated isolation guarantee with a
   compile-cost reason, while the convention is merge hygiene. The consequence is written into that
   umbrella rather than left implicit - **`httpclient/PreCompiled.h` is not a complete index of
   `httpclient/`**, and a reader looking for the codec will not find it listed there.
2. **A compatibility shim**, `core/detail/BoostBeastCompat.h`, in the form of `BoostAsioCompat.h`, if
   and only if a Beast API difference between Boost versions ever needs absorbing.
3. **A facade in the library's own terms**, `httpclient/Http1Codec.h`. No Beast type appears in any of
   its signatures - only `http::HeaderList`, `data::DataBlock`, `eh::error_code` and the library's
   exceptions. The backend is `httpclient/detail/Http1CodecBeastImpl.h`. An in-house backend, if it is
   ever written, is a sibling `detail/` header selected by a macro: the arrangement of
   `core/JsonUtils.h` over `core/detail/BoostJsonImpl.h` and `JsonSpiritImpl.h`.

The rule which follows, and which a `grep` can check: **no `boost::beast` name appears outside layers
1 and 2, and no `bl::beast` name appears outside the backend header.**

**Settled 2026-09-19: Beast stays.** S2.5 measured all four criteria rather than arguing them - the
chunked and trailers question probed with a TU deriving from `basic_parser` and 33 responses, the
object-size delta measured on *both* toolchains because L1 found the clang debug figure is the small
one (+0.27 MB there, +0.68 MB on gcc release, both noise against a 40 MB target). The verdict and its
evidence are in `notes/plans/issues/d15-http1-codec-backend-verdict.md`. What the facade turned out
to be for is not portability but **safety**: Beast accepts three things a client must not, and the
facade closes them - obsolete line folding, which Beast silently unfolds so it cannot be seen from
the callbacks at all; a `Transfer-Encoding` which is not exactly `chunked`, including `chunked, gzip`
which it converts to read-until-close over the raw chunk framing; and a `Content-Length` of `5, 5`,
which is what a proxy produces when it joins two fields.

**The criteria for "see where it goes".** Beast stays if all four hold; otherwise the in-house backend
is written behind the same facade, and nothing above the facade changes.

- It builds as C++11 on all four devenv7 toolchains against Boost 1.90.
- The object-size delta of the consuming test module is acceptable against the 40 MB target of
  `src/utests/AGENTS.md`. This is measured, not predicted - that file records size predictions which
  were once off by a factor of forty.
- `basic_parser`, used sans-I/O, covers the codec's cases: chunked bodies with trailers;
  read-until-close; bodiless responses; interim 1xx responses; header and body limits.
- Its strictness is at least that of the smuggling defenses listed above. Where Beast is more lenient
  than this design requires, the facade adds the check itself - it sees every field as it arrives -
  and that counts as holding. Where it cannot, it does not.

### 5.6 The session: redirects, cookies, decoder seam (D5, D9)

`httpclient::ClientSessionT` holds the pool, the active profile, proxy configuration, a cookie jar, the
redirect policy and the decoder registry, and creates request tasks.

**Redirects.** Off by default, as today - the existing client reports the target in
`errinfo_http_redirect_url` and leaves it to the caller. When enabled: a hop limit; `303` and, for
`POST`, `301`/`302` rewritten to `GET` without a body; `307`/`308` preserve method and body, so they
require a replayable body; `Authorization` and cookies for the old origin are dropped on a cross-origin
hop; an `https` to `http` downgrade is refused unless explicitly allowed. Targets resolve through
`net::Uri`. S2.8 added, and the L2 review judged sound: `HEAD` is exempt from the `303` rewrite (WHATWG
Fetch, and what browsers and curl do); a target whose scheme is not `http` or `https` is refused; a
response carrying two `Location` fields is refused rather than resolved; and `Proxy-Authorization`
is dropped with the other credentials, which is more than this section asks and is noted for S6.1.

**Cookie jar.** RFC 6265: domain and path matching, `Secure`, `HttpOnly`, expiry and `Max-Age`,
host-only cookies, per-domain and total caps. There is no public suffix list, so a cookie whose
`Domain` has no dot, or equals a bare TLD, is rejected - unless it is identical to the request host,
which RFC 6265 section 5.3 step 5 makes a host-only cookie - and the residual supercookie risk for
multi-label public suffixes such as `co.uk` is documented rather than hidden. The jar is per session
and thread safe.

Three points on which RFC 6265 is not the last word, and what S2.7 settled (L2 review): replacement
identity includes the host-only flag, per RFC 6265bis section 5.6 rather than 6265's name, domain and
path - 6265's rule lets a page widen its own host-only cookie into a domain cookie silently; a
`Domain` attribute identical to a single-label request host (`Domain=localhost` on `localhost`) was
rejected by S2.7 as first landed rather than stored host-only, which is what 6265's step 5 does for
a public suffix, so a test server on `localhost` lost the cookie - a defect, fixed after the L2
review, and the sentence above now states the rule; and Secure cookies follow 6265 alone, so
RFC 6265bis section 5.5's "leave secure cookies alone" - an `http` response can neither set nor
overwrite a `Secure` cookie - is **not** implemented. The last is a session-fixation surface for a
session which speaks both schemes to one host, and it is left as a decision for a later slice rather
than added silently.

**Decoder seam.** `httpclient::ContentDecoder` is a streaming transform interface keyed by
content-coding token, with an output-size cap and an expansion-ratio cap as decompression-bomb
defenses. A registry per session. No decoder ships (D9). The consequences, and the interaction with
`accept-encoding` under impersonation, are in 6.5 and in the companion deferral record.

**What S6.1 settled, and one of them is a property rather than a choice.**

`ClientSessionT` is parameterized on the **stream policy**, as every layer beneath it already is -
`Http2ConnectionTaskT`, `Http1ConnectionTaskT`, `ClientConnectionTaskBaseT` and
`ClientDriverFactoryT` all are, and a session which chose the policy at run time would type-erase
all four here and make every translation unit that named it instantiate both the cleartext and the
TLS half of each (measured at 9.8 MB for the driver alone, which is why `utf_baselib_h2client3`
exists). The consequence is that **one session speaks one scheme**: `createRequestTask` refuses a
URL whose scheme is not the transport's rather than connecting cleartext to a TLS port. That
settles the last paragraph above - the Secure-cookie session-fixation surface needs one session
speaking both schemes to one host, which this type cannot do - and it settles `isHttpApi`, which is
always `true` because there is no non-HTTP API here for RFC 6265 section 5.3 step 11 to be about.

**One `Cookie` field, merged.** RFC 6265 section 5.4 gives a request exactly one, and the jar and a
caller-supplied header both produce one; they are merged rather than both appended. The caller's
pair wins for a name they share. The place the two meet is specific and is where the case for it
lives: on a **same-origin** redirect the caller's header survives `dropCredentialHeaders()` while
the jar recomputes for the new target, so both are in scope at once.

**`Proxy-Authorization` is never a request header from this client.** The session drops a
caller-supplied one, which makes `RedirectPolicy::dropCredentialHeaders()`'s removal of the same
field a no-op by construction rather than a coincidence. Proxy credentials are session
configuration applied by the tunnel stage (3.6), and RFC 9110 section 11.7.1 makes the field
hop-by-hop - so a caller-supplied one on a request through a `CONNECT` tunnel would be a credential
sent to the **origin**.

**A streaming upload is never dispatched to HTTP/1.1.** The HTTP/1.1 driver refuses every request
carrying a `BodySource`, and the session is the only layer which knows both the request and what a
connection for a key will speak. A session whose transport can never produce HTTP/2 refuses such a
request in `createRequestTask`, before an attempt is spent; a session whose transport negotiates
routes it to a key of its own whose connections offer `h2` alone, since a peer may select only from
what it was offered (RFC 7301 section 3.1).

**A caller's `BodySink` and a followed redirect are mutually exclusive.** `BodySink` carries no
status, so a hop's body cannot be told from the final one on the way through, and a streamed 302
body would reach the caller's sink as if it were the answer. A request with a sink installed
therefore reports its 3xx rather than following it, which is what this client does with redirects
off in any case.

### 5.7 Timeouts and cancellation

| Timeout | Default | Owner |
|---|---|---|
| Connect: **TCP connected** through preface | 60 s, per attempt | connection task |
| **Establishment: `acquire` through a connection which can carry a request** | **120 s** | **pool** |
| TLS handshake and shutdown | 60 s, inherited (`TcpSslBaseTasks.h:73`) | stream policy |
| Request total, including pool wait | 30 min, matching `http/Globals.h:186` | request task |
| Response headers | off | request task |
| Stream idle | off | request task |
| `SETTINGS` acknowledgement | **10 s** | connection task |
| Keepalive `PING` reply | 15 s, when keepalive is on | connection task |
| Drain: a close waiting on its `GOAWAY` | 10 s | connection task |
| Connection idle | 5 min | **pool sets it, each driver enforces it** |

**The connect row said "resolve through preface" until the L4 review and bounded neither end of
it.** Both ends were corrected there, one in the code and one here.

The back end was the code's: the h2 driver disarmed the deadline as the first statement of
`onProtocolNegotiated()`, so in cleartext with no proxy it was armed and cancelled inside one
synchronous chain and covered nothing at all. It is now disarmed when the opening write completes,
which is the first moment the preface really is away, so "through preface" is true as written.

The front end is this row's. The deadline is armed inside the TCP connect completion handler, so
the resolve and the `async_connect` are outside it and the row may not claim them. What bounds them
is the operating system, which is neither 60 s nor one number: the resolver query is `all_matching`,
so `async_connect` walks every address returned and each black-holed one costs a full SYN timeout -
**measured at 134 s** on a Linux host with the default `tcp_syn_retries` of 6. A host whose
addresses drop SYNs therefore holds the task for minutes per address, before the deadline is armed
at all, and `DEFAULT_HANDSHAKE_RETRY_COUNT` of 1 then buys a second attempt which re-arms a fresh
60 s on a new socket. So the establishment bound is `resolve + connect + 60 s`, twice, and not 60 s.

Arming at schedule time instead - one deadline across the retry, on `aioService()`, the way the TLS
protocol timer of `TcpSslBaseTasks.h:120` already is - would let the row keep its original wording.
It is not forbidden by the strand rule, since `onConnectDeadline` touches no stream state. It is
deferred rather than rejected: an overall establishment bound belongs with the retry policy, which
design 5.4 gives to the pool, and the pool is where a caller's deadline for "get me a connection"
is actually known.

**The hole that left is closed by S5.2, and the establishment row above is where it went.** The
deferral was of a mechanism, not of the bound: what the connect deadline does not cover is now
covered by the pool, which arms its own deadline when it inserts the `Connecting` placeholder and
satisfies it the first time the connection reads `Ready`. So the bound spans resolve, connect,
tunnel, handshake, floor, ALPN and the preface, **across the establisher's handshake retry** -
exactly the set nothing else bounds - and on expiry the pool cancels the connection task, retires
the placeholder and retries or fails every request queued behind it. `ClientConnectionTaskBase` is
unchanged; the connect deadline still means what its row says.

**120 s, and the number it has to be argued against is the 134 above.** S5.2 argued it from two
attempts of the 60 s per-attempt deadline plus two resolve-and-connect legs, and claimed it
"truncates nothing which would have succeeded". The L5 review was right that both halves are wrong:
that sum exceeds 120 by the two legs, and the per-attempt deadline does not cover
resolve-and-connect at all - which is the whole reason this bound exists. What it truncates is
measured in SYN timeouts. An origin whose **first** address is black-holed and whose second answers
- a dual-stack host with a stale `AAAA`, which `getaddrinfo` returns first - would have connected at
about 135 s and is failed at 120 instead, on each of the three attempts section 4.6 allows.

**It stays at 120 s, as a policy choice and not as a free one.** What is being bounded is
addresses x SYN timeout x handshake attempts, so a bound which clears one dead address is about
200 s, two about 330 and three about 470: no single number rescues the ordinary case without giving
up on bounding anything. And the bound is spent once per retry, so what a caller waits is up to
three times it - 200 s would be ten minutes of silence for an origin the operating system is going
to refuse anyway. **So a black-holed first address is a hard failure through the pool**, written
here rather than left to be discovered. The real fix is not a number in the pool: it is the
**per-endpoint connect bound** in the establisher, the front end this section already defers (L4
finding 1, still open), which is what Happy Eyeballs does and what makes a dead address cost a
second instead of 134. With it every address of an ordinary origin is tried well inside 120 s and
this bound goes back to being a backstop on an establishment which is going nowhere.

**The drain row is a backstop and not a protocol deadline.** A close is taken only through the
write pump, which returns while a write is in flight, so a peer that stops reading with our send
buffer full would otherwise hold a closing connection open for as long as TCP keeps retransmitting.
`Http2ConnectionConfig::drainTimeout` bounds it, and on expiry the connection is cancelled rather
than closed - a peer which is not reading will not read a `GOAWAY` either, which is the same
reasoning the keepalive `PING` deadline already follows. RFC 9113 section 5.4.1 asks for the
`GOAWAY` with a SHOULD, and it is still sent first on every path where it can be.

**The idle row said "pool" and the pool has never had a reaper** (L6 finding 5). It said so because
the knob is pool policy, and that half is right: `ConnectionPoolPolicy::idleTimeout` is where the
number lives and the session writes it into every connection it builds. What was never true is the
enforcement. The pool cannot perform it: its only lever on a connection is `requestCancel()`, which
5.4 above names as the abrupt path and this row's own lifetime as the graceful one, and a reaper
would also have raced the h2 driver's timer on every h2 connection. So the row is split rather than
moved - the pool sets it, and **each driver** arms a timer while it holds no stream and closes
itself gracefully when it expires. The h2 driver did this from S4.1; the h1 driver got it in the L6
fix round, where until then an idle keep-alive HTTP/1.1 connection was closed by nothing of ours.

**The `SETTINGS` row said 30 s until S4.2 while the engine said 10; it now says 10, and there is one
number rather than two.** `SessionLimits::settingsTimeoutInSeconds` is that number and the connection task
holds no second one of its own - it arms its deadline from the limits it was given and lets
`Session::onTimer()` decide, which is what stops the two drifting apart again. Three reasons for
taking the engine's value rather than this table's. RFC 9113 gives `SETTINGS_TIMEOUT` no numeric
value at all, so neither number was ever a standard. The keepalive `PING` reply deadline in the row
below is **15 s**, so a 30 s `SETTINGS` deadline would make the *first* control frame a peer must
answer, on a connection which has only just been established, twice as lenient as the steady-state
liveness check on the same connection - which is backwards. And a `SETTINGS` acknowledgement is
emitted by the peer's protocol layer rather than by its application, so it is not subject to the
scheduling delays which justify a generous application-level timeout; 10 s is already two orders of
magnitude above a wide-area round trip.

A request timeout, or `requestCancel()` on a request task, posts a `RST_STREAM( CANCEL )` to the
connection and completes the request - with `TimeoutException` and the existing message shape in the
first case. **Cancelling a request never closes the connection.** Cancelling a connection task fails
every stream on it, each flagged retryable or not by the rule in 5.4.

**A `RST_STREAM( CANCEL )` must not overtake the request it cancels** (found in S4.2).
`Session::produce()` writes the control queue before the header block queue, so a reset queued while
the stream's own `HEADERS` are still waiting goes out in front of them, and the peer sees a
`RST_STREAM` on a stream it has never heard of - a connection error of type `PROTOCOL_ERROR` by
RFC 9113 section 5.1. "Submit, then cancel" is the ordinary shape of a request whose deadline expired
while it was queued, so this is reachable rather than theoretical. `Http2ConnectionTaskT` holds such
a cancel back until the block has actually been handed to a write. The ordering is the driver's and
not the engine's, deliberately: the engine's queue order is what serializes a header block with its
own `CONTINUATION` frames and is not the thing to change.

### 5.8 API sketch

```cpp
typedef httpclient::ClientSessionImplT< tasks::TcpSslSocketAsyncStrandedBase >  SessionImpl;

const auto session = SessionImpl::createInstance< httpclient::ClientSession >();

session -> profile( httpclient::BrowserProfiles::get( "chrome" ) );        /* optional */

httpclient::ClientRequest request;
request.url( net::Uri::parse( "https://example.com/api/items" ) );
request.method( "POST" );
request.headers().append( "content-type", "application/json" );
request.body( std::move( json ) );

const auto task = session -> createRequestTask( request );

eq -> push_back( om::qi< tasks::Task >( task ) );
eq -> flush();

const auto& response = task -> response();      /* status(), headers(), body(), trailers(),
                                                   negotiated(), impersonationReport() */
```

**Two corrections this sketch carried until S6.1, both of which the layers below had already
settled.** `ClientRequest` is a **value** type and not an `om` object - L2 froze it that way in
`httpclient/ClientTypes.h`, because a redirect derives a second request from the first rather than
mutating it - so it is constructed and passed by reference, and its header list is
`http::HeaderList`, whose method is `append`. And the session is parameterized on the stream policy,
so `ClientSessionImpl` is `ClientSessionImplT< STREAM >`: one session speaks one scheme, for the
reason 5.6 gives. `ClientRequestTask`, which `createRequestTask` returns, is the interface carrying
`request()`, `response()` and `redirectHops()`; it is scheduled as a `tasks::Task` and it is one
task however many hops and retries it runs.

**Compatibility facade.** `SimpleHttp2GetTaskImpl`, `...PutTaskImpl` and so on, with the constructor
shape of `BL_TASKS_DECLARE_HTTP_TASK_*` (`SimpleHttpTask.h:1051`) and the same getters -
`getResponse()`, `getHttpStatus()`, `getResponseHeaders()`, `isSecureMode`, `addExpectedHttpStatuses` -
over a process-default session. An existing caller opts in by changing a type name. Nothing migrates
automatically.

---

## 6. Browser impersonation

### 6.1 The model

Impersonation is judged across three layers **together**; a detector looks for any layer contradicting
the claimed browser.

| Layer | What is observed | Where it is produced | Fidelity here |
|---|---|---|---|
| TLS | ClientHello: suites, extensions, groups, signature algorithms, ALPN; hashed as JA3 and JA4 | OpenSSL, behind the seam | **graded** - see 6.3 |
| HTTP/2 | `SETTINGS` ids, values and order; connection `WINDOW_UPDATE`; `PRIORITY` frames; pseudo-header order; `HEADERS` priority | `http2::Session` | **exact**, provable offline |
| Headers | set, order, values, by request kind | the session | **exact**, except `accept-encoding` while no decoders exist |

TCP/IP-level fingerprinting (TTL, window, options) is the operating system's and out of scope.

**Every profile carries a grade** for its TLS layer, so a caller sees it before relying on it:

- `Ja4Candidate` - the ClientHello is expected to match the browser's JA4, subject to the spike.
  Chrome and Edge.
- `Approximate` - it cannot match, for stated reasons. Firefox and Safari.

An `Approximate` profile is useful against servers that inspect headers and HTTP/2 only. Against one
that cross-checks TLS with the claimed browser, it is not.

### 6.2 Profile data

`data/models/HttpClientProfiles.h`, with the `BL_DM_*` macros as in `data/models/Http.h`. A
`BrowserProfile` is:

- identity: id, family, grade, the list of known deviations;
- **shape** - changes a few times a year: a `TlsClientProfile`, an `Http2Profile`, a `HeaderProfile`;
- **version strings** - change every four weeks: user-agent, the `sec-ch-ua` brand list, platform.

Shape and version strings are separate on purpose. A Chrome 120 user-agent in late 2026 is itself a
bot signal, so the strings must be cheap to refresh, and **profiles load from JSON**, so a refresh
needs no library release. Built-in profiles are JSON literals parsed at first use, so built-in and
supplied profiles take one code path.

**Loaded profiles are untrusted input.** Validation on load: cipher names against the allowlist of
3.3; setting ids and values in range; header names as tokens and values free of CR, LF and NUL (header
injection); priority fields in range; every list bounded in length.

### 6.3 The TLS layer on OpenSSL

What each knob maps to. **The OpenSSL column is from the API and changelog and has not been probed**:
the devenv7 dist is not unpacked on the design machine. Verifying it is the first task of the spike.

**Two rows are now confirmed against a real 3.5.4, in S3.6** (2026-09-19). A genuine ClientHello was
captured through the shipped `enableClientHelloCapture()` with **no peer at all** - `SSL_do_handshake`
on a never-connected client writes its hello into the stream's own memory BIO and then asks for a
read it never gets, by which time the callback has already fired. In it: extension `65281` (0xFF01)
is `renegotiation_info`, so the `0x00ff` SCSV is indeed not sent; and the offered group list carries
`4588` (0x11EC), `X25519MLKEM768`, so the hybrid group really is offered. The rest of the 3.5.4
column is still inferred, and **the 1.1.1w column is entirely unprobed and cannot be probed here** -
see `notes/plans/issues/openssl-1x-flavor-deferral.md`.

| ClientHello element | Mechanism | 3.5.4 | 1.1.1w |
|---|---|---|---|
| TLS 1.2 suite order | `SSL_CTX_set_cipher_list`, explicit names | yes | yes |
| TLS 1.3 suite order | `SSL_CTX_set_ciphersuites` | yes | yes |
| Groups | `SSL_CTX_set1_groups_list` | yes, incl. `X25519MLKEM768` | no hybrid group |
| More than one key share | 3.5 tuple syntax of the same call | yes | no |
| Signature algorithms | `SSL_CTX_set1_sigalgs_list` | yes; SHA-1 refused at level 2 | yes |
| ALPN | `SSL_set_alpn_protos` | yes | yes |
| `session_ticket` | clear `SSL_OP_NO_TICKET` | yes | yes |
| `status_request` | `SSL_set_tlsext_status_type` | yes | yes |
| SCT request | `SSL_enable_ct`, permissive | yes | yes |
| Padding | `SSL_OP_TLSEXT_PADDING` | yes | yes |
| `renegotiation_info` instead of the `0x00ff` SCSV | automatic from 3.2 | yes | **no** - an OpenSSL tell |
| EC point formats list | no public setter known | **no** - affects JA3, not JA4 | no |
| Certificate compression | needs OpenSSL built with a compressor | not as built | no |
| GREASE in suites, groups, versions | none | no | no |
| Extension order and Chrome's shuffle | none; fixed internal order | no | no |
| ALPS | none | no | no |
| `delegated_credentials`, `record_size_limit` | none | no | no |

This is why D2 draws the line at 3.5: on 1.1.1w a current-browser profile is materially wrong, not
slightly off.

**Per family.**

- **Chrome, Edge - `Ja4Candidate`.** Chrome has shuffled its extension order per connection since
  v110, so OpenSSL's fixed order is a legitimate sample and detectors use the order-insensitive JA4 for
  it. JA3 cannot match (order, point formats). What stands between OpenSSL and Chrome's JA4 is the
  extension *set*: ALPS, certificate compression and ECH-GREASE are missing. Edge is identical below
  the header layer.
- **Firefox - `Approximate`.** Fixed extension order; `delegated_credentials` and
  `record_size_limit`; and its trailing SHA-1 signature algorithms cannot be advertised at security
  level 2, which D4 keeps.
- **Safari - `Approximate`.** Fixed order, and its suite list still ends in 3DES suites that stock
  OpenSSL 3.x compiles out. Rebuilding OpenSSL with `enable-weak-ssl-ciphers` to advertise them is
  **not** proposed.

**The spike - measured, and its results recorded per profile.**

1. Unpack devenv7; for each profile dump the ClientHello via 3.3's capture and compute JA3 and JA4.
2. Verify each "yes" in the table above against the real 3.5.4.
3. Evaluate, **default off**, whether `SSL_CTX_add_custom_ext` can close the JA4 gap for Chrome. JA4
   hashes extension *type codes* only, so candidates are: GREASE extensions with empty bodies; an
   ECH-GREASE placeholder, accepting and ignoring the server's retry configs; and an ALPS extension
   listing a protocol we never negotiate, so that no server ever selects it. A genuine ALPS
   advertisement is unsafe: a server that negotiates it expects a client `EncryptedExtensions` message
   OpenSSL will never send, and the handshake fails.
4. Record, for each workaround, the interop risk found. Adopt none without evidence (D23).

**`delegated_credentials` is never emulated.** A server that supports it may answer with a delegated
credential that OpenSSL cannot verify, turning a fidelity gap into a handshake failure.

**Session resumption (D22).** The profile context advertises `session_ticket`, as the browser's first
ClientHello does, and never resumes; the cache stays off. A real browser's *later* connections carry
`pre_shared_key`; ours will not. Real resumption is a separate feature with its own key-handling
questions and is left out of the first version.

### 6.4 The HTTP/2 layer

`Http2Profile`: an ordered list of `( id, value )` settings, allowing ids the library does not itself
interpret; the connection `WINDOW_UPDATE` increment; a list of `PRIORITY` frames to send on idle
streams after `SETTINGS`; optional priority fields on `HEADERS`; pseudo-header order; HPACK encoder
table size, indexing policy and cookie crumbling; the `WINDOW_UPDATE` threshold.

**The RFC 9218 `priority` header value by request kind lives in `httpclient/`, not here.** This
paragraph originally placed it under `Http2Profile`, which cannot hold: it is keyed by the request
kind, whose enum §6.2 puts in `httpclient/`, and §2.2 says that dependency runs one way,
`httpclient/` → `http2/`. Of the three, the placement is the one that gives. S1.4 built it as
`httpclient::HeaderProfileForKind` and recorded the reason in both headers, so neither reads as an
oversight.

`http2/Fingerprint.h` renders the frames a session *actually produced* in the conventional
`SETTINGS|WINDOW_UPDATE|PRIORITY|pseudo-order` form, for the report and the tests.

**Illustrative values only.** These are from public fingerprint corpora as recalled, not captured, and
are here to show the schema covers the known shapes. They **must** be re-captured (6.7).

| Family | Shape |
|---|---|
| Chrome, Edge | `1:65536;2:0;4:6291456;6:262144\|15663105\|0\|m,a,s,p`; `HEADERS` priority weight 256, exclusive |
| Firefox | `1:65536;2:0;4:131072;5:16384\|12517377\|0\|m,p,a,s`; older versions also sent a `PRIORITY` tree on idle streams 3-13 |
| Safari | `2:0;3:100;4:2097152;8:1;9:1\|10420225\|0\|m,s,a,p`; ids 8 and 9 are ones we only pass through |

### 6.5 The header layer

`HeaderProfile`, by **request kind** - navigation, fetch/XHR, subresource - because a browser sends
different headers for each. For each kind: an ordered list of default headers, some with computed
values (`sec-fetch-site` depends on the origin relationship); the placement rule for caller-supplied
headers; the HTTP/1.1 case map. `accept-language` is caller-configurable and rendered in the profile's
q-value style. Under HTTP/2, `:authority` replaces `host`.

**`accept-encoding` and the decoder seam.** A browser profile dictates
`gzip, deflate, br, zstd`. The value actually sent is that list **intersected with the registered
decoders**. With none registered (D9) the header is omitted and the deviation is reported. A
**strict** mode sends the exact header and returns the raw encoded body with its `content-encoding`,
for a caller who decodes it themselves.

So until decoders exist the header layer is exact *except* for this one header, which the report says
plainly.

### 6.6 The fidelity report

Available on the connection and on each response, and logged once per profile and backend at debug
level. It states: the profile and its grade; the TLS backend and version; every knob as `Honored`,
`Approximated` or `Unsupported`, with the reason; **the JA3 and JA4 computed from the ClientHello
actually sent**; the HTTP/2 fingerprint of the frames actually sent; the `accept-encoding` actually
sent; the deviation list.

This report is what D3's "honest reporting" means in practice.

### 6.7 Ground truth and maintenance

None of a profile's values can be captured from the design environment, and none in this document
should be trusted.

**Capture procedure, per family and version.** Point the real browser at a capture endpoint that
records the raw ClientHello and the opening HTTP/2 frames, for a navigation, a `fetch` and a
subresource. Store the raw bytes beside the profile JSON. Derive the profile from the bytes, never from
memory or from another tool's tables.

**Test vectors, per profile.** Exact opening bytes from `http2::Session`; the HTTP/2 fingerprint
string; header order for each request kind; and the *effective* JA3 and JA4 pinned against OpenSSL
3.5.x - a regression guard on what we send, distinct from the browser's own values, which are recorded
next to them so the gap is visible.

**Cadence.** Version strings monthly for Chrome, Edge and Firefox, roughly yearly for Safari, which
needs separate macOS and iOS profiles. Shape when a browser changes it. Four families is four
treadmills (D10).

### 6.8 Gating

Everything in this section requires OpenSSL 3.5 or later (D2). On 1.1.1w the profile API exists, so
that callers compile, and `profile( ... )` throws `NotSupportedException`. The impersonation test module
asserts exactly that on that flavor.

---

## 7. Security considerations

- **The TLS floor is not weakened.** Level 2, the TLS 1.2 minimum, chain verification, peer-name
  verification and the shared trust store apply to every profile context. Step 1 of 3.3 is never
  parameterized. The default path does not change at all.
- **Nothing below the floor carries data** (D4). The check precedes the first HTTP byte.
- **Profiles are untrusted input** (6.2), and a cipher string is a policy-injection vector (3.3).
- **All peer-controlled sizes are bounded** (4.6): header lists, header blocks, `CONTINUATION` runs,
  control-frame queues, bodies.
- **Decoders, when they arrive, are a decompression-bomb surface.** The caps are part of the seam now
  (5.6), so they cannot be forgotten later.
- **Redirects** drop credentials across origins and refuse downgrades by default (5.6).
- **Cookies** without a public suffix list carry a documented residual risk (5.6).
- **Proxy credentials** and `Authorization` values are redacted in secure mode like everything else.
- **Impersonation is a capability, not a policy.** It changes how the client presents itself and
  nothing about what it verifies. Whether presenting as a browser is appropriate for a given target is
  the application's decision.

---

## 8. Testing

### 8.1 Modules (D20)

`utf_baselib_http2` is taken, and appending a digit to anything ending in `2` reads badly under the
numbering scheme of `src/utests/AGENTS.md`. Area-based names avoid both problems and split by weight:

| Module | Contents | Sockets |
|---|---|---|
| `utf_baselib_h2core` | frame codec; HPACK against the RFC 7541 Appendix C vectors; Huffman; flow control; stream states; `Session` conformance as byte scripts, including every limit in 4.6 | none |
| `utf_baselib_h2client` | connection task against the test peer, cleartext and TLS; pool; GOAWAY and `REFUSED_STREAM` retry; timeouts; cancel; full-duplex stress on a multi-threaded I/O pool, and the same suite on a single-threaded one | loopback |
| `utf_baselib_httpclient` | HTTP/1.1 codec and fallback; redirects; cookies; decoder seam with a test-only transform; `CONNECT` and SOCKS5 against in-process fake proxies | loopback |
| `utf_baselib_h2profiles` | 6.7's vectors; the `NotSupportedException` contract on 1.1.1w | loopback |

`net::Uri` tests go next to `TestNetUtils.h` in `utf_baselib2`, where the other `bl::net` tests are,
headroom permitting.

Each new module pays the roughly 21 MB floor. The sans-I/O cores being non-templates is what keeps the
first module light. Check with `make utests-sizes` before and after.

### 8.2 The test peer (D8)

In `src/utests/include/utests/baselib/`, in the `T< E = void >` idiom:

- **`Http2TestServer`** - `TcpServerBase< STREAM >` plus a connection task over
  `http2::Session( Role::Server )`, with scriptable behavior: delays, GOAWAY after N streams,
  `REFUSED_STREAM`, window stalls, trailers, interim responses.
- **`RawFrameScriptPeer`** - the `RawHttpResponder` idea (`TestClientHttpTasks.h:31`) at frame level:
  byte-exact malformed input the real core would never produce.

The first exercises the client against an independent *use* of the core, not an independent
implementation. That gap is closed manually (8.4).

### 8.3 Concurrency

The full-duplex stress test is run under ThreadSanitizer at least once per phase. There is precedent
for TSan-found lock inversions in this codebase
(`notes/plans/issues/async-executor-tsan-inversion-record.md`), and 5.2's rules are exactly what it
checks.

### 8.4 Outside the unit tests

No unit test touches the network. Documented manual procedures cover: interop against nghttpd and
nginx; the effective fingerprint against a fingerprint-echo endpoint. The four pure byte consumers -
`Session::feed`, the HPACK decoder, the HTTP/1.1 parser, `net::Uri` - get libFuzzer harnesses as an
optional target, since clang is in the devenv.

### 8.5 Matrix

Per `AGENTS.md`: gcc and clang on Linux, msvc and clang on Windows, debug and release, focused
per-module builds, `-j1` when more than one module is built.

---

## 9. Build integration

- **devenv7+ (D1).** Test modules are discovered by wildcard (`projects/make/common.mk:268`), and the
  existing way to exclude some is a marker file: `jni_enabled` at `common.mk:290`. Proposal: a
  `devenv7_only` marker in each new module's directory and one `filter-out` block using the negative
  filter. This is the **only** makefile change. Headers guard themselves on the capability they need -
  `BOOST_VERSION` for executor-bound I/O objects, `OPENSSL_VERSION_NUMBER` for impersonation - with a
  clear `#error`, never on `BL_DEVENV_VERSION`, which only the project makefiles define and which no
  public header may require (`notes/plans/issues/devenv7-breaking-changes-release-notes.md:352`).
- **OpenSSL-optional (1.5).** Headers that need OpenSSL are separate and stay out of `PreCompiled.h`.
  The cores and the plain-TCP path compile without it.
- **OpenSSL flavor (D2).** Impersonation code is guarded by `OPENSSL_VERSION_NUMBER >= 0x30500000L`.

---

## 10. Changes to existing files

Everything else is new files. The changes fall into two classes, and D19 and D26 treat them
differently.

**Changes to code which existing users run through - the gated change-set, G1 (3.8).** One commit
each, comprehensive tests, one gate on the entire suite:

| File | Change | Kind |
|---|---|---|
| `tasks/TaskBase.h` | `BL_TASKS_HANDLER_END_IMPL_EX`; `..._END_IMPL` forwards to it; `..._END_MULTIOP` | refactor, identical expansion |
| `tasks/TcpBaseTasks.h` | `beginPreHandshakeStage` virtual with a pass-through default | behavior-preserving |
| `tasks/TcpSslBaseTasks.h` | extract `configureClientStream()` from `createSocket` | pure refactor |
| `crypto/CryptoBase.h` | split `initNativeSslContext` into three steps | pure refactor, security-critical |

**Purely additive - new names only; no existing code path changes.** Each lands with the phase which
first uses it, validated by the focused modules:

| File | Addition | Lands in |
|---|---|---|
| `core/ErrorHandling.h` | new `errinfo_*` typedefs, two exception declarations | P0 |
| `tasks/AsioSslStreamWrapper.h` | strand constructor; ALPN offer and result; negotiated version and suite | P0 |
| `tasks/AsioSslStreamWrapper.h` | client-context constructor; ClientHello capture hook | P6 |
| `crypto/CryptoBase.h` | `createAsioSslClientContext( profile )` | P6 |
| `projects/make/common.mk` | the `devenv7_only` marker filter | P1, with the first new module |

Additive means what it says. The wrapper's new constructors are **overloads**; the existing
constructor, and its rule that a non-null context pointer means the server role
(`AsioSslStreamWrapper.h:341`), are left exactly as they are. If an addition turns out to need a
change to existing code after all, it stops being additive and becomes a new gated change (3.8).

The P6 acceptance tests for `createAsioSslClientContext` are that a profile context reports level 2
and a TLS 1.2 minimum, that it shares the global trust store, and that a cipher string carrying
`@SECLEVEL` is refused.

---

## 11. Phasing

**Sequencing is governed by `notes/plans/http2-implementation-plan.md`**, which re-cuts this phase
table into layers and slices for parallel execution and records where it consolidates the "Lands in"
column of section 10. The table below remains the dependency summary at design altitude.

Each phase is independently buildable, testable and committable. **G1 comes first and is gated**
(D19, D26); P0 onwards add new files and new names only.

| Phase | Content | Exit |
|---|---|---|
| **G1** | the gated core change-set of 3.8: the handler macro with `MultiOperationTask`; the pre-handshake hook; the `createSocket` extraction; the `initNativeSslContext` split - four commits, tests first where behavior can be characterized first | **the entire suite, baseline-relative, on the full matrix and both OpenSSL flavors for the TLS modules** |
| P0 | `net::Uri`; `http::HeaderList`; error info; stranded policies; the additive wrapper changes for strands and ALPN | focused modules; new unit tests |
| P1 | `http2/` sans-I/O core; the `devenv7_only` marker | `utf_baselib_h2core` |
| P2 | connection task, test peer, request task, pool; cleartext and TLS on the **default hardened context** | `utf_baselib_h2client`, including a TSan run |
| P3 | HTTP/1.1 codec on Beast behind its facade, measured against D15's criteria; connection task; ALPN fallback | fallback against the library's own `HttpServer`; the D15 verdict recorded |
| P4 | session: redirects, cookies, decoder seam | `utf_baselib_httpclient` |
| P5 | tunnel stage: `CONNECT`, SOCKS5 | fake-proxy tests |
| P6 | TLS profiles, floor check, ClientHello capture; **the spike**; profiles in the order Chrome, Edge, Firefox, Safari; capture procedure | `utf_baselib_h2profiles`; spike results recorded |
| P7 | compatibility facade; optionally a `bl-tool` command for manual runs | - |

Plain HTTP/2 is usable after P2. Impersonation is last because it depends on everything else and
because its values need the capture work.

P1, and the parts of P0 which touch no TLS - `net::Uri`, `http::HeaderList`, the error info - depend
on nothing in G1 and can proceed while its gate runs. The rest cannot: the stranded TLS policy of P0
needs commit 3, the connection task of P2 needs commit 1, P5 needs commit 2 and P6 needs commit 4.

---

## 12. curl parity

| Capability | Here |
|---|---|
| Multiplexing; both flow-control levels; HPACK; `CONTINUATION`; padding; trailers; 1xx | yes |
| ALPN `h2`; cleartext by prior knowledge | yes |
| HTTP/1.1 fallback | yes |
| Connection reuse; wait-to-multiplex rather than open another | yes, pool policy |
| Retry on GOAWAY and `REFUSED_STREAM` | yes (D6) |
| Keepalive `PING` | yes |
| Per-request priority | yes; RFC 9218, and RFC 7540 fields where a profile dictates |
| Connection coalescing | designed, default off (D21) |
| Cookies; redirects | yes |
| Content decoding | seam only (D9) |
| Proxies: `CONNECT`, SOCKS5 | yes |
| Impersonation (the curl-impersonate fork, not upstream curl) | yes, graded (section 6) |
| Server push; `h2c` via `Upgrade` | no (D11) |
| HTTPS proxies; HTTP/2 *to* a proxy | no |

**Non-goals, confirmed by the author (D25):** authentication schemes beyond caller-supplied headers,
proxy Basic and SOCKS5 username/password; HSTS and Alt-Svc caches; a DNS cache or Happy Eyeballs
beyond the existing sequential `async_connect`; a multipart form builder; WebSockets and extended
`CONNECT`; HTTP/3.

---

## 13. References

RFC 9113 (HTTP/2), RFC 7541 (HPACK), RFC 9218 (priorities), RFC 9110 and RFC 9112 (HTTP semantics and
HTTP/1.1), RFC 6265 (cookies), RFC 3986 (URI), RFC 7301 (ALPN), RFC 8701 (GREASE), RFC 8879
(certificate compression), RFC 8336 (`ORIGIN`), RFC 1928 and RFC 1929 (SOCKS5).
