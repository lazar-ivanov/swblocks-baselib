# HTTP/2 in `HttpServer`: Deferral Record

This document records the decision **not** to add HTTP/2 to `bl::httpserver::HttpServer` as part of
the HTTP/2 client work, what is done instead so that the option stays cheap, and what has to be true
before the server side is taken on. It is a scoping decision, not an assessment that a server is
unwanted.

**Design:** `notes/plans/http2-design.md`, decision D8.

---

## Decision

**Date:** 2026-09-17
**Status:** Phase 0 is part of the client design; production server support is deferred behind a
design of its own.

| # | Item | Disposition |
|---|---|---|
| 1 | The protocol core - frames, HPACK, flow control, stream states, `http2::Session` - is role-neutral | **In scope now** |
| 2 | A test-only HTTP/2 peer built on that core, under `src/utests/` | **In scope now** |
| 3 | `HttpServer` negotiates `h2` by ALPN and serves it | **Deferred** |
| 4 | The denial-of-service hardening a listening HTTP/2 endpoint requires | **Deferred** with item 3 - it is what sets the price |
| 5 | Mapping HTTP/2 streams onto `Request`, `Response` and `ServerBackendProcessing` | **Deferred** with item 3 |
| 6 | Server push | **Not planned.** The client never accepts it (D11) and browsers have removed it |

---

## What is deferred, and how large it is

Most of the *protocol* is not deferred at all. HTTP/2 is symmetric, and items 1 and 2 put the frame
codec, HPACK, both flow-control levels, the stream state machine and the session engine in place for
both roles. What remains for a server is three things.

**The I/O shell.** A server connection task, the ALPN selection callback on the server `SSL_CTX`, and
the accept path. Small, and structurally the mirror of the client's connection task.

**The mapping onto the existing server model.** `HttpServerConnection`
(`src/include/baselib/httpserver/HttpServer.h:446`) is a `WrapperTaskBase` stepping one request through
`RECEIVE`, `PROCESS`, `RESPOND` and then closing - it is HTTP/1.0 with `Connection: close`
(`httpserver/Response.h:77`). An HTTP/2 connection carries many requests at once, so that state
machine becomes per stream, and the connection becomes a multiplexer which hands each completed
request to `ServerBackendProcessing::getProcessingTask`
(`httpserver/ServerBackendProcessing.h:58`) and writes each `Response` back as it completes. The
backend interface itself need not change, which is the point of doing it this way.

**The hardening.** This is the bulk of the work and the reason for the deferral.

---

## Why this is deferred: a listening HTTP/2 endpoint is an attack surface of its own

An HTTP/2 client talks to servers it chose. An HTTP/2 server talks to anyone. The protocol gives a
peer many ways to make the other side do work or hold memory cheaply, and the history is that each one
was found in the field, in mature implementations, not in review.

| Attack | Reference | What the peer does | What a server must do |
|---|---|---|---|
| Rapid Reset | CVE-2023-44487 | opens a stream and resets it at once, repeatedly; each costs backend work while never counting against the concurrency limit | count peer resets per window; keep a stream counted until its *backend work* ends, not until the stream closes; close on abuse |
| MadeYouReset | CVE-2025-8671 | provokes the *server* into resetting streams, by protocol errors sent after dispatch, to the same effect | count server resets caused by peer errors the same way |
| `CONTINUATION` flood | CVE-2024-27316 and siblings | an endless header block that never ends | cap block bytes and frame count |
| HPACK bomb | - | small encoded references that expand to a huge header list | enforce the list size limit *during* decoding |
| `PING` flood, `SETTINGS` flood | CVE-2019-9512, -9515 | frames that each demand an acknowledgement, from a peer that never reads | cap the queue of owed acknowledgements; rate-limit |
| Reset flood | CVE-2019-9514 | malformed requests, each eliciting a `RST_STREAM` to queue | same cap |
| Empty frames flood | CVE-2019-9518 | zero-length frames without end flags | rate-limit |
| Data dribble, internal buffering | CVE-2019-9511, -9517 | tiny windows, or never reading, so responses buffer without bound | cap buffered output per connection; write deadlines; a minimum throughput |
| Resource loop | CVE-2019-9513 | churns the priority tree | do not build one - RFC 9113 deprecates it |
| Zero-length headers leak | CVE-2019-9516 | header names and values of length zero held in memory | reject |

The role-neutral core already carries the limits a *client* needs - the `CONTINUATION` cap, the HPACK
bound and the owed-acknowledgement cap are in section 4.6 of the design - and those transfer as they
are. The rest are server concerns: they are about admission, accounting against backend work, and
fairness between connections, and they need limits chosen from measurement rather than guessed.

There is a second reason. `TcpServerBase` derives its connection cap from
`connectionMemoryFootprint()` (`src/include/baselib/tasks/TcpBaseTasks.h:2062`), which for `HttpServer`
is one request plus one response (`HttpServer.h:744`). Under HTTP/2 a connection's worst case is that
multiplied by the concurrent stream limit, plus HPACK tables and flow-control buffers. The cap, and
with it the server's behavior under load, has to be re-derived, and that touches a base class every
server in the library shares.

None of this is needed for the client, and folding it in would roughly double the design and much
more than double the test matrix.

---

## What is done now - Phase 0

**The core is role-neutral.** `http2::Session` takes a role. Stream-id parity, which side may send
which frames, and the direction of the state machine all key off it. Nothing in `http2/` assumes it is
a client.

**A test peer exists**, `Http2TestServer` in `src/utests/include/utests/baselib/`, built on
`TcpServerBase< STREAM >` and `http2::Session( Role::Server )`. It exists to test the client. It has
scriptable misbehavior and **none of the hardening above**.

That gives the client an in-process peer without an external tool, and it means the server role of the
core is exercised from the first day rather than discovered to be broken later.

**Standing rules for the interim.**

1. **`http2/` stays role-neutral.** A client-only assumption added to the frame codec, HPACK, flow
   control, the state machine or the session is a defect, even while no server exists.
2. **The test peer stays under `src/utests/`.** It must never be included from `src/include/`. It is
   not a server; it is a fixture which happens to accept connections.
3. **Limits in the core are configurable per role**, so that server defaults can differ from client
   defaults without a second copy of the code.

---

## What it buys, and why that is not urgent

- The library's REST and messaging-gateway servers could serve browsers and modern clients over one
  multiplexed connection instead of one connection per request.
- The client would be tested against the library's own production server, not only a fixture.
- Header compression and multiplexing reduce latency for chatty internal APIs.

None of these is a current requirement. The existing servers work, and the client's HTTP/1.1 fallback
(design section 5.5) means the new client talks to them as they are.

---

## Conditions to revisit

- A concrete need for `HttpServer` to serve HTTP/2 - a browser-facing deployment, or a peer which
  requires it.
- The client has shipped and the core has real mileage, so the server design starts from a proven
  engine.
- A decision to move `HttpServer` beyond HTTP/1.0 semantics at all. Keep-alive under HTTP/1.1 raises
  several of the same questions - connection lifetime, per-connection memory, idle deadlines - and
  answering them once for both would be better than twice.

---

## Sequencing when it does happen

1. A server design document of its own, covering admission control, backend-work accounting, fairness
   between connections, and the re-derivation of `connectionMemoryFootprint()` and the connection cap.
2. Limits chosen from measurement against the library's real backends, not copied from another
   server's defaults.
3. ALPN selection on the server context (`SSL_CTX_set_alpn_select_cb`) in
   `CryptoInitT::createAsioSslServerContext`, offering `http/1.1` only until the rest is ready.
   **It must answer no overlap with the fatal `no_application_protocol` alert, which
   `CryptoBase::setAlpnServerPreference` does not.** That entry point answers
   `SSL_TLSEXT_ERR_NOACK` - no ALPN extension in the ServerHello, handshake completes, nothing
   selected. RFC 7301 section 3.2 says a server which implements ALPN and finds no overlap *SHALL*
   respond with the alert, so NOACK is section 3.1's ALPN-*unaware* server and a production server
   built on that entry point would be non-conforming. It is right for what it serves - the TLS test
   peer, which exists so that design 5.5's client-side fallback can be driven at all, and a peer
   which sent the alert could not produce that outcome. A conforming server therefore needs a
   second entry point rather than this one. (L4 review, finding 5.)
4. The server connection task and the per-stream mapping onto `Request`, `Response` and
   `ServerBackendProcessing`.
5. The hardening table above, each row with a test that mounts the attack against the real server and
   asserts the bound holds.
6. Conformance with an external tool such as `h2spec`, which tests servers and so becomes usable at
   this point for the first time.
7. Only then, `h2` offered by default.

Step 5 is the one which must not be compressed. Every row of that table is a vulnerability some mature
server shipped.
