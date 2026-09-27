# HTTP/2 L0–L6 remediation: follow-up architecture and security review

**Date:** 2026-09-26

**Reviewed HEAD:** `db97372598afe595cf6c253920841774aad18e9f` (`lazari2`)

**Production comparison:** `c8e9be88ff3d1a8c56008f4fa50f90c7001bfbd0..db973725`

**Previous report:** [HTTP/2 through L6 and embedded decompression review](http2-l0-l6-architecture-security-review-2026-09-21.md)

## Assessment

The remediation makes substantial, useful corrections. The original write-buffer lifetime defect, unsynchronized pool-pointer reset, fallback publication race, informational-response accumulation, SETTINGS/header-block ordering, initial encoder limit, priority-frame sizing, padding credit, receive-window threshold, initial SETTINGS requirement, cookie defects, and redirect-cancellation result have concrete fixes in the reviewed source.

I would nevertheless **not give the current implementation a clean architectural or security sign-off**. This review identifies **seven code findings: three P1 and four P2**, followed by **two disposition/documentation findings**. The P1s are an unsafe TLS end-of-message decision, removal of the only upstream bound on HTTP/1 response accumulation, and an HTTP/1 startup path that can initiate TLS operations concurrently despite using a stranded socket. H08 and H16 are not completely closed; H17's intended fix is present but exposes a method-case regression.

The distinction between fixed, deliberately accepted, and deferred remains essential. I have not treated the maintainer's explicit decisions as forgotten work or demanded that every deferral be reversed. Two premises do need correction: changing an unbounded transfer into an unbounded queue is a memory-safety/resource decision, and an application can register a decoder today without waiting for the embedded-codec programme.

**Method and limits.** This was a static review of the production diff, its callers and dependencies, relevant test implementations, commit history, design records, and primary protocol/library documentation. No builds, tests, executable probes, fuzzers, or benchmarks were run. Historical validation records were read as reports, not reproduced or certified here. No implementation or existing documentation was edited. This new report is the only file created, and it is uncommitted.

The working tree was clean at the start. File and line references below refer to the reviewed HEAD. The repository contains 23 changed production headers in this range. The review follows their dependencies through the task framework, both drivers, the protocol engine, and the session; it is not a claim to have audited every unrelated application or every line of vendored Boost/OpenSSL.

**Severity.** P1 means a release-blocking safety, availability, or data-integrity concern. P2 means material correctness, interoperability, or integration risk. P3 means a narrower maintenance/status defect. All trigger sequences below are static derivations; none is presented as a reproduced race or exploit.

## Findings

### R01 — P1: a truncated TLS connection can complete a close-delimited HTTP/1 response successfully

**Location:** [Http1ConnectionTask.h](../../src/include/baselib/httpclient/Http1ConnectionTask.h), `isCleanEndOfStream():1232–1235`, `onPeerClosed():1267–1346`, and `onReadCompleted():1374–1484`; [TcpSslBaseTasks.h](../../src/include/baselib/tasks/TcpSslBaseTasks.h), `isStreamTruncationError():341–344` and `isExpectedSslErrorCode():735–758`; [NetUtils.h](../../src/include/baselib/core/NetUtils.h), commentary preceding `isCleanEndOfStreamErrorCode():421`.

**Status relative to the remediation:** pre-existing behavior retained and explicitly justified by the new transport-classification code. This is separate from the correctly fixed cleartext reset case and the correctly fixed locally cancelled-read cases.

The driver's clean-end predicate returns true for either transport EOF **or TLS truncation**. On a peer-initiated truncation, with no local cancel and no task close already in progress, `onReadCompleted()` enters `onPeerClosed()`. The clean-end branch calls `Http1ResponseParser::parseEof()`. A response whose body is framed only by connection closure then becomes complete, and the driver publishes a successful stream end.

A concrete exchange is:

1. A TLS server sends a valid `200` header section with neither Content-Length nor chunked framing.
2. Some body records arrive.
3. The transport ends without an authenticated TLS closure alert, at a TLS-record boundary.
4. The TLS policy identifies the ending as truncation; the HTTP/1 predicate nevertheless permits `parseEof()`.
5. The request succeeds with only the received prefix. A BodySink can receive `onComplete()`.

The security consequence is false completeness: an application may consume or persist a truncated document as the complete authenticated response. The peer can be faulty; an attacker able to terminate the transport can also exploit the distinction without forging TLS application records. No such attack was executed in this review.

The comment's RFC 2818 §2.2.2 justification is incorrect. That section requires the server to send its closure alert; it permits closing without **waiting for the other side's alert**, not omission of its own. Current HTTP/1 requirements make a close-delimited TLS response complete only after a valid closure alert. Length- and chunk-framed responses can be recognized as complete independently. See [RFC 9112 §§8 and 9.8](https://www.rfc-editor.org/rfc/rfc9112.html#section-9.8) and [RFC 2818 §2.2](https://www.rfc-editor.org/rfc/rfc2818.html#section-2.2).

**Design intent and correction:** retaining interoperability with peers that omit TLS shutdown is reasonable for an already self-delimited message. It cannot authenticate the end of an otherwise unframed body. Keep the centralized transport predicates and stream-policy knowledge, but distinguish “the connection ended” from “this ending establishes message completeness.” Do not pass TLS truncation into the close-delimited success path. This needs no weakening of certificate verification and no change to the chosen local teardown mechanism.

**Coverage gap / future check:** the new TLS cases in `TestHttp1DriverTlsCancelClose.h` exercise endings caused by our cancellation or deliberate close; their comments repeat the same RFC misconception. Add a peer-initiated missing-alert case with no local cancellation, a clean close_notify control, and complete Content-Length/chunked controls. Assert the request result and sink completion, not just the driver task's terminal state.

### R02 — P1: N1 removes the upstream body bound without replacing it with a bound on queued HTTP/1 data

**Location:** [Http1Codec.h](../../src/include/baselib/httpclient/Http1Codec.h), `Http1ResponseLimits():300–307` and `onBodyData():1176–1192`; [Http1ConnectionTask.h](../../src/include/baselib/httpclient/Http1ConnectionTask.h), `deliverBodyChunk():1067–1088`, `onReadCompleted():1491–1493`, and `consumed():2451–2457`; [HttpClientRequestTask.h](../../src/include/baselib/httpclient/HttpClientRequestTask.h), `post():374–405`, `applyData():885–949`, and `offerToSink():969–1011`.

**Provenance:** `9895df2`, N1's change of the parser's default from 64 MiB to `NO_MAX_BODY_SIZE`. The absence of HTTP/1 read backpressure predates that change; removing the finite transfer ceiling materially enlarges its consequences.

The HTTP/1 driver reads continuously, copies body chunks into DataBlocks, posts them to the request task, and rearms its read. Its `consumed()` is a no-op. The request task's sink branch adds every block to `m_pendingDownload` and bypasses `maxResponseBodySize`. A sink returning zero is explicitly allowed by the BodySink contract. No byte bound limits that pending deque or the preceding event mailbox.

Consequently, a long or endless HTTP/1 response and a sink that temporarily stops accepting bytes accumulate data until memory is exhausted or some unrelated resource ends the exchange. The idle timeout does not help while data continues arriving. A total timeout is a time limit, not a memory limit; it also acts through the request's event processing. Even a normally consuming sink can leave an unbounded mailbox while its callback is blocked or its drain thread is delayed.

Before N1, Beast's aggregate body limit stopped this particular producer after approximately 64 MiB. The design's statement that streamed memory “was never bounded by body_limit anyway” overlooks the blocks retained **after** `m_bodyChunk` is cleared. The previous cap was a transfer limit, but it also imposed a finite upper bound on how much transfer data could be queued downstream.

This is not merely the accepted BodySink readiness deferral. The readiness gap concerns restarting a paused consumer. The current HTTP/1 driver never pauses its producer at all. HTTP/2's receive-window mechanism does not protect HTTP/1. Direct users of the default buffering `Http1ResponseParser` also lose their previous default bound, since that parser appends to its own `m_body` when no callback is installed.

**Design intent and correction:** unlimited *total* streamed downloads are appropriate; unlimited *outstanding* data is not required to support them. Preserve the distinction between total transfer length and resident queued bytes. Bound the mailbox/pending data at ingress and either fail the request on overflow or pause HTTP/1 reads until consumption resumes. The existing `consumed(handle, bytes)` seam is available for a driver-side outstanding-byte policy; a frozen BodySink IID does not require accepting unlimited buffering.

The tradeoff is explicitly discussed in [S6R.2 §9](issues/s6r2-design.md), but on the incorrect memory premise above. Reconsidering it is therefore a review correction to that decision, not an attempt to silently undo the streaming-size goal.

**Coverage gap / future check:** `Http1Codec_BodyIsUncappedByDefaultTests` uses a callback that immediately consumes every byte. It proves transfer-size behavior, not the end-to-end memory bound. Use a real HTTP/1 driver and a sink returning zero, with a small configured outstanding-byte budget. Also delay a request drain to cover the mailbox before `applyData()`. The test should prove a bounded pause or explicit overflow failure without allocating a large body.

### R03 — P1: HTTP/1 publishes “started” before its initial TLS read is serialized on the strand

**Location:** [Http1ConnectionTask.h](../../src/include/baselib/httpclient/Http1ConnectionTask.h), `scheduleTask():2172–2207`, `submit():2348–2393`, `onStartRequest():575–750`, and `armRead():974–991`; [TaskBase.h](../../src/include/baselib/tasks/TaskBase.h), `scheduleNothrow():1160–1208`; [ExecutionQueueImpl.h](../../src/include/baselib/tasks/ExecutionQueueImpl.h), `padExecutingQueueNothrow():644–703`; [AsioSslStreamWrapper.h](../../src/include/baselib/tasks/AsioSslStreamWrapper.h), `async_read_some()/async_write_some():551–573`.

**Status:** a pre-existing startup ordering defect retained by A4's initial-read exception fix. It is distinct from the accepted question of changing TaskBase's lock scope.

The following interleaving is permitted by the actual locks:

1. A queue thread enters `scheduleTask()` under the task mutex.
2. Under `m_stateLock`, it sets `m_started = true`, observes no pending request, and releases that lock.
3. Before or during its direct call to `armRead()`, another thread calls `submit()`. The driver is Ready, its slot is empty, and `m_started` is true, so `submit()` posts `onStartRequest()` to the socket's strand.
4. An I/O worker runs that posted callback. `onStartRequest()` does **not** take the task mutex; it is a `BL_NOEXCEPT` callback, not a `BL_TASKS_HANDLER_BEGIN` body.
5. The queue thread can be initiating `ssl::stream::async_read_some()` while the strand worker initiates `async_write()` on the same SSL stream.

The task mutex excludes ordinary read/write completion-handler bodies, but does not exclude this request-start callback. Stream accessors and the SSL wrapper add no hidden lock or strand dispatch. A strand associated with the socket orders handlers; it does not move this direct initiating call from the queue thread onto that strand.

This is reachable through the public driver's supported submit/schedule overlap, and through concurrent pool acquisition: the adopted driver is published under the pool lock before its deferred scheduling action executes. The pool's queue push itself is not required to run on the connection's strand.

The risk is concurrent access to Asio's SSL engine and its shared stream state, with undefined behavior, corrupt I/O, or a crash possible. Boost explicitly requires shared SSL-stream asynchronous operations to use the same strand. See [Boost.Asio ssl::stream thread safety](https://www.boost.org/latest/doc/html/boost_asio/reference/ssl__stream.html). The interleaving is source-derived; no TSan result or concrete memory-corruption reproduction is claimed.

**Design intent and correction:** keep the per-connection strand model. Make the initial read and the transition allowing posted request starts part of a single serialized bootstrap, with operation accounting and cancellation handled there. This is a driver-local problem; moving `TaskBase::scheduleTask()` outside the task mutex would not by itself fix it and would reopen the cancellation concern already deferred.

**Coverage gap / future check:** the new strand seam controls completion order, and the schedule-throw case controls an initiation exception. Neither controls concurrent *initial* initiation. Hold the first read initiator at a deterministic barrier while submitting a request from another thread. Assert that a write initiator cannot enter the stream concurrently; cover the TLS policy and cancellation during bootstrap.

### R04 — P2: the new sink replay guard misses bytes consumed before a later callback throws

**Location:** [HttpClientRequestTask.h](../../src/include/baselib/httpclient/HttpClientRequestTask.h), `offerToSink():973–1004`, `runDeferred():542–560`, `applyClosed():1199–1263`, and `failWith():1522–1554`; [ClientSession.h](../../src/include/baselib/httpclient/ClientSession.h), `chkPrepareRetry():1208–1234`.

**Provenance:** H08's new guard in `0f8d9bd`. The ordinary retry-after-delivery case is fixed; exception safety of its accounting is incomplete.

`offerToSink()` accumulates accepted bytes in a local variable and updates `m_sinkDelivered` only after its entire loop returns normally. Each successful full-block callback removes that block from the pending deque immediately. A later callback in the same loop may throw.

A concrete sequence is a pending queue `[A, B]` and a failed network close in the same event batch:

- The sink accepts all of A; A is popped.
- The callback for B throws, so the assignment to `m_sinkDelivered` is skipped.
- Any remaining deferred offers of B can also fail; none repairs the lost count.
- The close has classified the connection as unusable. The existing network failure remains the first completion exception.
- With `retryIdempotentOnConnectionLoss = true`, a replayable GET can pass `chkPrepareRetry()`, since `sinkDelivered()` still returns zero.
- A successful second attempt appends a complete response after A, silently duplicating the prefix in the caller's non-resettable sink.

This does not depend on a sink claiming to have consumed bytes before throwing. A's successful callback explicitly returned its byte count; only a **different**, later callback throws.

**Design intent and correction:** the chosen “never replay onto a sink that has received bytes” policy is sound. Record each successful callback's accepted count before invoking another callback that can throw. Separately, a sink exception should prevent further automatic delivery/replay for that logical request unless a reset contract explicitly permits it; a generic deferred-cleanup runner should not imply that all later sink actions remain valid.

**Coverage gap / future check:** `ClientSession_RetryIsRefusedOnceTheSinkHasSeenBytesTests` covers normal delivery. `BatchThrowingSinkT` accepts its first block in an earlier batch, so its count has already been committed before the tested throw. Add a same-offer multi-block case, followed by a retryable connection-loss verdict, and assert one network attempt and no duplicate prefix. Preserve H07's first-failure precedence and the obligation to release the pool slot.

### R05 — P2: H16 normalizes only the first TE field and drops Connection before reading its nominated fields

**Location:** [Http2ConnectionTask.h](../../src/include/baselib/http2/Http2ConnectionTask.h), `normalizeHeaders():3197–3246`; [HeaderList.h](../../src/include/baselib/http/HeaderList.h), `tryGet():532–537`; [Session.h](../../src/include/baselib/http2/Session.h), `submitRequest():1001–1015`.

**Provenance:** `38c6f3d`, H16's new protocol-neutral header normalization.

Two supported HeaderList shapes escape the intended normalization:

- `TE: trailers` followed by `TE: gzip`: `tryGet("te")` sees only the first value, passes it, and leaves **both** fields in the list. `HpackFields::appendAll()` carries the second value to the wire. A conforming HTTP/2 peer must reject the resulting message.
- `Connection: x-hop-token` with `X-Hop-Token: secret`: the Connection field is removed by the fixed-name loop before its token list is inspected. The nominated field is forwarded even though the caller marked it as connection-specific. Multiple Connection fields have the same problem.

The first is an interoperability failure. The second can disclose or misinterpret metadata intended for a single hop; it does not mean that an arbitrary unknown header is intrinsically forbidden in HTTP/2.

**Design intent and correction:** normalization is preferable to rejecting ordinary protocol-neutral inputs, as the implementation chose. Apply that decision to the complete ordered multimap: collect Connection tokens from all occurrences before removing anything, remove nominated fields, and validate/canonicalize all TE occurrences. Do not solve this by making HeaderList itself discard repeated fields; preserving repeated fields is an intentional library contract. See [RFC 9113 §8.2.2](https://www.rfc-editor.org/rfc/rfc9113.html#section-8.2.2) and [RFC 9110 §7.6.1](https://www.rfc-editor.org/rfc/rfc9110.html#section-7.6.1).

**Coverage gap / future check:** the current normalization tests cover a single valid TE, a single invalid TE, and the five fixed field names. Add repeated-TE and multiple-Connection/token-list cases, retaining end-to-end fields that were not nominated. Inspect the emitted HPACK fields, not only the original request object.

### R06 — P2: the new Host agreement check defaults to the URL's explicit port and treats malformed ports as absent

**Location:** [Http2ConnectionTask.h](../../src/include/baselib/http2/Http2ConnectionTask.h), `isSameAuthority():3119–3176` and the Host branch of `normalizeHeaders():3280–3299`; [Uri.h](../../src/include/baselib/core/Uri.h), `effectivePort():1211–1214`.

**Provenance:** `38c6f3d`, H16's authority check.

The code says it defaults an absent port from the scheme on both sides, but it actually defaults from `url.effectivePort()`. That accessor returns the URL's explicit port when present.

For `https://example.com:8443/p` and `Host: example.com`, the comparison therefore substitutes 8443 for the Host field's absent port and accepts it. The scheme default would be 443. The request is then sent to `:authority = example.com:8443` after silently deleting the disagreeing Host value.

The helper also returns zero for a missing port, a nondigit port, and the numeric value zero. Unsigned decimal accumulation is not range-checked. Thus values such as `example.com:garbage` and `example.com:0` can be treated as if their port were omitted, and large values can wrap. Only the first Host occurrence is checked before all occurrences are removed.

This is a failure of the explicitly chosen “reject conflicting authority rather than silently changing the request” behavior. It is not a claim that these inputs redirect the TCP connection to an attacker-selected host: the URL remains the connection target.

**Design intent and correction:** use the existing URI/authority validation idiom, distinguish “absent” from “invalid,” and use the scheme default independently for an omitted Host port. Validate every Host occurrence or reject duplicates at this boundary. Preserve the accepted equivalence between `https://example.com/` and `Host: EXAMPLE.com:443`.

**Coverage gap / future check:** current tests cover explicit matching nondefault ports and a nondefault Host port against a default URL, but miss the inverse case. Add the inverse, invalid and overflowing ports, and contradictory repeated Host fields. This is a pure boundary test; no network fixture is required.

### R07 — P2: H17 now rejects valid response bodies for case-distinct methods such as “head”

**Location:** [Session.h](../../src/include/baselib/http2/Session.h), `submitRequest():1013`, `judgeDataFrame():2328–2344`, and `isHeadMethod():2894–2900`; [ClientTypes.h](../../src/include/baselib/httpclient/ClientTypes.h), `method():404–407`; [Http2ConnectionTask.h](../../src/include/baselib/http2/Http2ConnectionTask.h), `toSessionRequest():3315`.

**Provenance:** the case-insensitive helper predates the remediation; `9e5cb11` makes its overly broad classification reject nonempty DATA.

ClientRequest preserves the method's case and the HTTP/2 driver transmits it unchanged. The engine nevertheless compares all four letters of HEAD case-insensitively when setting `expectsNoContent`. H17's new check rejects any subsequent nonempty DATA when that flag is set.

A caller can therefore send `:method = head`, receive a legitimate `501` explanation body from a server that does not implement that distinct method, and get a stream protocol error from baselib. A server implementing a case-distinct extension method has the same problem. The HTTP/1 driver correctly configures its parser with the exact comparison `"HEAD" == request.method()`.

Method names are case-sensitive; case folding of field names does not extend to methods. See [RFC 9110 §9.1](https://www.rfc-editor.org/rfc/rfc9110.html#section-9.1).

**Design intent and correction:** keep H17's DATA rejection for actual HEAD, 204 and 304 responses. Recognize only exact `HEAD` as the bodyless request method, consistently with the HTTP/1 path. Do not uppercase every outgoing method, since that would change the request's semantics.

**Coverage gap / future check:** add exact HEAD, `head`, and mixed-case method controls. A DATA body on a 501 response to `head` must be delivered; the same DATA on a response to HEAD must still be rejected.

### R08 — P2, disposition: “no codec ships” does not make H24/H25 unreachable through the public API

**Location:** [ClientSession.h](../../src/include/baselib/httpclient/ClientSession.h), public `decoders():188`, its implementation at `2014`, `absorbResponse():1241–1257`, and `decodeBody():1278–1343`; [ContentDecoder.h](../../src/include/baselib/httpclient/ContentDecoder.h), public `registerDecoder():521`; [decoder deferral](issues/http-content-decoders-deferral.md), “The decision on P2 and P3.”

This is a correction to the reachability premise of an accepted deferral, not a new copy of H24 and H25.

The default registry has no nonidentity codec, so the ordinary unconfigured path is safe from those two findings. An application can nevertheless call `session->decoders().registerDecoder(...)` today. `ClientSession_AcceptEncodingAndStrictDecodeTests` already exercises that API with a test transform. Registration is neither disabled nor dependent on vendoring an embedded codec, E5, C++ language changes, or L7.

After registration, the unchanged code can decode only the first Content-Encoding field and remove all its occurrences (H24), or decode a bodyless/failed partial response before deciding its actual outcome (H25). A custom transform is sufficient to expose the sequencing defect; no production gzip implementation needs to be shipped in this repository.

The current statement should be: **“deferred; reachable for applications registering a decoder; dormant only while the registry lacks a matching decoder.”** The H09 callback/lock/deadline concern is also relevant to such applications.

**Decision needed:** whether to retain a supported opt-in registration API with these known semantic defects or close the small HTTP-message integration defects before inviting use of that API. My recommendation is to fix the integration guards/list handling independently of the choice of codec supplier. The condition that would reverse that recommendation is an explicit restriction that custom registration is unsupported or disabled until those prerequisites land. Documentation must state the actual restriction; an empty default registry is not such a restriction.

**Future check:** use the existing test-only transform for repeated coding fields, a HEAD/304 response, and a failed coded partial response. These checks do not require un-parking the embedded decompression implementation.

### R09 — P3: the current design and owed list contain several stale statuses, not only the H21 row

The requested owed-list sweep found these directly checkable inconsistencies:

| Current record | What it says | Source/commit evidence and required current status |
|---|---|---|
| `issues/astra-remediation-owed-work.md:572`, row 16 | H21 is entirely deferred; quotes the old all-HTTP/1 zero-retry limitation | `f23b205`, `ClientSession.h:1752–1755`, and `ConnectionPool.h:1647`: fixed when the session cannot produce h2; the ALPN/ridePreface remainder remains |
| Same file, row 12 at `277` | A4 is “scheduled” | `69d6d6f`; `scheduleTask()` now calls `armRead()` and propagates initiation failure to TaskBase. That particular deadlock fix is landed |
| Same file, row 13e at `299` | The h2 write-first response-loss defect is “LIVE TODAY” | The same file's row 13a at `525` correctly records `bc1f13a`/`09e22d8` as fixing 13a and 13e together; the current write handler leaves classification to the read |
| Same file, row 8a at `519` | A1 “must be re-specified before it is implemented” | Current source and row 7a's `6632469` entry already include the separate close-delimited reset classification |
| `http2-design.md:1618–1627` | H05 is “not yet fixed” and interims are unbounded on both protocols | The new engine and codec aggregate count/byte limits are implemented |
| `http2-implementation-plan.md:1612–1617` | H22 has a design; H21 refers to the old handover; H24/H25 are latent until a codec ships | H22 is landed, H21 is narrowed, and R08 qualifies H24/H25 |

These are active status statements in the documents readers are directed to use, not merely old measurements preserved in a dated implementation journal. They can lead another reviewer to redo completed work or overlook a reachable residual.

**Correction:** update the current-status rows and canonical security summary together, retaining historical reasoning behind links or explicitly marked history. Include R01–R08 and the carried risks below in the same reconciliation. Existing files were deliberately left untouched during this review; this table is the requested sweep result, not a claim that the ledger has been repaired.

## Reconciliation of all original implementation findings

“Fixed” below means the specific original mechanism was corrected by the inspected code. It does not mean the entire containing subsystem is defect-free or that this review reran its tests.

| Original | Current assessment | Evidence / remaining qualification |
|---|---|---|
| H01 | Fixed as scoped | Active write storage survives stream completion; the deferred reuse verdict refuses reuse while a write remains outstanding or failed. R03 concerns startup, not that lifetime fix |
| H02 | Fixed as scoped | `m_requestMayHaveBeenSent` is set before issuing the write; the read's close waits for a pending write's result before using an unsent verdict |
| H03 | Original pointer/timer races fixed | The queue pointer is retained; maintenance arm/cancel uses the leaf timer lock and disposed latch. Admission/disposal quiescence is still a separate recorded residual |
| H04 | Fixed | One observed task-connection state gates fallback-pointer access and retirement; protocol inspection is gated on published readiness |
| H05 | Fixed | Both parsers count aggregate interim responses and bytes before retaining another interim. Canonical design status is stale, R09 |
| H06 | Silent successful truncation fixed; readiness remains deferred | Final drain requires all pending bytes to be taken before onComplete; zero progress with bytes left fails. R02 identifies the separate HTTP/1 unbounded-buffer consequence |
| H07 | Fixed as scoped | A deferred exception overrides a pending success; an earlier failure remains first |
| H08 | Incomplete | The fallback no longer spuriously completes the sink, and ordinary replay after delivery is refused. R04 bypasses the new delivery counter on an exception path |
| H09 | Accepted structural deferral, reachable | Session continuation still runs decode/factory/rewind work under wrapper/queue locking. A blocking or re-entrant application callback remains a live risk |
| H10 | SETTINGS-ordering defect fixed | ACK shares the ordered header-block queue and its bytes count toward the control budget. The separate dropped-HPACK-block hazard remains deferred under the current L6 driver's stated restrictions |
| H11 | Deliberate conformance leniency | No enforcement code was added. Accept the documented decision; a caller advertising a smaller table can still exercise it |
| H12 | Fixed | Initial encoder capacity is capped at the peer's default 4096; later peer settings control further capacity changes. The earlier review's concern about a smaller initial capacity was overbroad |
| H13 | Fixed | First HEADERS fragment subtracts priority-field bytes from the frame payload allowance |
| H14 | Fixed | Padding is credited and both window-update paths are flushed without waiting for an application data callback |
| H15 | Fixed | Default threshold follows initial-window changes; exhausted windows with pending credit can send an update even under an explicit large threshold |
| H16 | Incomplete | Body-derived Content-Length and the common forbidden fields are handled; R05/R06 identify remaining normalization and authority defects |
| H17 | Intended rejection fixed; regression found | Nonempty DATA on true bodyless responses is rejected while empty/padding-only completion remains possible. R07 narrows HEAD recognition |
| H18 | Fixed | The first received frame must be non-ACK SETTINGS before ordinary frame dispatch |
| H19 | Accepted dependency boundary | Plain session inclusion still reaches TLS/OpenSSL headers. The header split remains deferred; it was not implemented |
| H20 | Fixed as scoped | Both timeout messages use the redacted URL renderer, which omits userinfo via `Uri::authority()`, query and fragment. Paths remain visible by the chosen policy; this is not a general guarantee that every path is nonsecret |
| H21 | Narrowed, not eliminated | `ridePreface` is disabled when the session cannot produce h2. Negotiated TLS fallback can still consume a request attempt with the rider enabled; callers can disable the rider |
| H22 | Fixed | A redirect that would continue is decided before the cancellation latch determines the chain result; a final response remains a no-op cancellation. The exception uses the wrapper's existing forwarding model |
| H23 | Accepted two-budget policy | Pool establishment and session replay budgets remain separate and can multiply. Documentation is a disposition, not a runtime cap change |
| H24 | Deferred, reachable with registration | Raw unsupported-coding behavior remains correct by default. Custom registered codecs reach the existing field-list problem, R08 |
| H25 | Deferred, reachable with registration | Bodyless and failed-partial decode ordering is unchanged, R08 |
| H26 | Fixed | CONNECT renders unbracketed IPv6 host input as a bracketed authority in both request target and Host |
| H27 | Fixed | Cookie-name conflict resolution uses byte-exact comparison; the shared case-insensitive header/coding helper was retained |
| H28 | Fixed | Non-HTTP replacement/deletion checks the existing cookie's HttpOnly flag before altering it |
| H29 | Protocol assumption corrected in the deferral record | Absence of Accept-Encoding does not force identity. Unknown codings still reach callers with encoded bytes and metadata intact |

This supports the claimed substantial progress, but not the unqualified “21 fully fixed” reading. H08 and H16 require further corrections, and H17's fix has the additional case-sensitivity consequence. H06, H03 and H10 must be read with their explicitly narrower scope.

## Shared changes and architectural assessment

The separation between the sans-I/O protocol engine, strand-owned connections, request mailboxes, and session policy remains appropriate for baselib. The fixes mostly preserve its intended idioms: no network callbacks into the pool under connection locks, no decoder dependency forced into the default build, repeated fields retained by HeaderList, centralized transport-code interpretation, and private implementation changes without casually changing published IIDs.

The following wider changes were examined in addition to the H-numbered fixes:

- **TLS handler forwarding, `9ca4678`.** Taking the handler by forwarding reference avoids moving Asio's composed-operation state while another call argument still reads that state's buffer sequence. The implementation forwards only inside the wrapper, after argument evaluation. This is the right boundary fix and applies beyond HTTP/2; no alternate by-value adapter was introduced in the inspected wrapper. The recorded target-dependent evaluation-order failure explains why architecture/toolchain diversity matters. Those historical measurements were not rerun.
- **Socket shutdown, `bb53bdd`.** The shared helper now shuts down send, then cancels socket operations, while preserving the established force/linger behavior. Its direct and indirect consumers include legacy TCP/TLS connection and server tasks, not just the new clients. The change does not provide an authenticated TLS message boundary (R01), and it does not itself make outstanding receive operations impossible (the accepted composed-read residual below).
- **Transport predicates.** The new predicates distinguish conversation end, retryable orderly close, message-framing EOF, and write-side peer endings. The Windows reset narrowing is reflected in the current code. R01 is a misuse of the TLS extension to that classification, not a request to reintroduce open-coded transport comparisons.
- **Operation accounting, `372a397`/`13aac19`.** All eight HTTP/2 begin/async-initiation sites have an abandonment path. Giving back an operation without independently recording another error is appropriate inside accounted handlers. The already-recorded exceptional off-strand terminal path remains a qualification; an operation count of zero is not a general substitute for an executor-ownership invariant.
- **Pool retirement bound, `24db294`.** A never-usable retired connection now spends the queued establishment budget. The completed-response witness avoids charging an origin merely for closing after an answered request. Publication gating and retry accounting are separate responsibilities and are correctly treated separately.
- **Time conversion.** The shared seconds conversion uses a 64-bit count, and CookieJar no longer narrows Max-Age through Windows' 32-bit long before reaching it. The change has shared-core scope even though the motivating use is a cookie duration.

The principal architectural gap is still the mismatch between transport progress, application consumption, and task completion. R01, R02 and R04 are three different failures at that boundary. A long callback, a queued-but-unconsumed byte, an ended TCP connection, and a successfully completed HTTP message are different states; a single completion or timeout flag cannot stand in for all of them.

## Carried risks that are not new remediation requests

These remain real consequences of recorded choices. I am not relabeling them as fixed or scheduling them without a new decision:

| Existing decision/residual | What a caller can still encounter |
|---|---|
| H09 continuation lock scope | Deadlock/re-entrancy or long queue stalls from application callbacks; post-network decoding is not covered by an actively enforced request timer |
| BodySource / BodySink readiness deferral | A temporarily unavailable source or an h2 sink can stall until timeout because it has no readiness signal; a final drain does not supply an asynchronous resume facility |
| Sink failure notification/reset deferral | A failed request need not notify the sink through a terminal callback; its owner must inspect the task. A non-resettable sink cannot safely receive a transparent replay |
| Pool admission versus disposal | A scheduling action collected before disposal can arrive after its cancellation sweep; disposal can wait for an orphan driver's own lifetime. Retaining the queue removes the pointer race, not this protocol gap |
| TLS composed-read cancellation residual, owed row 13b | A partial TLS record and a silent peer can leave a composed read beyond the selected cancellation mechanism. “Closed as a consequence of a decision” does not mean the behavior disappeared |
| Exceptional completion/accounting residual, row 13d and the task-lock deferral | An initiation/post failure after another operation is armed can reach the plain task epilog before normal multi-operation retirement; the limited existing disposition remains |
| Exceptional `postCommand()` abandonment | The reviewed A3 record explicitly retains possible off-strand teardown versus strand timer/socket cancellation after a posting failure. “No pending operation” does not establish Asio shared-object thread safety; it remains an accepted exceptional-path qualification |
| H10 dropped encoded-block hazard | The current L6 client has no subsequent client header/trailer submission after the relevant drop paths. Recheck before exposing such a caller or reusing the general Session API in a broader role |
| H11/H19/H21/H23 | Deliberate HPACK leniency, OpenSSL header dependency, negotiated-fallback attempt cost, and multiplying retry budgets respectively |

The Linux x86-64 matrix remains explicitly owed as **X1** in the current ledger. The existing Windows and Linux validation summaries provide useful history, but neither the summaries nor this static review replace that missing target coverage. No new matrix was run here.

## Decompression proposals

The three documents named in the original request are unchanged between their commit with the previous review (`633ca6b`) and this HEAD:

- [Embedded compression research](embedded-compression-design.md)
- [Embedded decompression design](embedded-decompression-design.md)
- [Embedded decompression implementation plan](embedded-decompression-implementation-plan.md)

C01–C11 therefore remain the previous review's assessment of parked proposals, not vulnerabilities in a built-in decoder now shipping. There is no evidence in this change range that those implementation gates have been satisfied. In particular, the C++11 mismatch, decoder workspace and output bounds, finish/consumption contract, failure behavior, namespace/configuration isolation, fuzzing/adversarial validation, and registration/adoption semantics still need closure before that programme is presented as complete.

R08 is the relevant new disposition correction: the absence of built-in codecs does not protect users of the already-public custom registry. R02 also has to be considered before describing a future decoding sink as providing bounded streaming on both transports.

## Recommended decision batch

These are review recommendations, not authorization to modify the implementation. They are grouped by the code and contracts they affect rather than one lane per finding.

| Order / batch | What and consequence if omitted | Risk, complexity and blast radius | Undecided shape, recommendation and reversal condition |
|---|---|---|---|
| 1. Transport and response delivery: R01–R04 | Prevent false TLS completeness, unbounded HTTP/1 queued data, concurrent startup I/O and replay onto a partially written sink. Leaving these carries wrong-answer, availability and concurrency risks | Moderate design work across the HTTP/1 driver and common request task; common sink changes also reach h2. Preserve shared TaskBase and published IID behavior where possible | Choose bounded failure versus read pause for queue pressure, and a serialized startup/accounting shape. Recommend strict framing, an explicit outstanding-byte bound, strand bootstrap and exception-safe delivery accounting. Reverse only if the relevant public paths are explicitly disabled or a source-level serialization/bound not present here is established |
| 2. Protocol semantics: R05–R07 | Finish H16's multivalue/authority checks and keep H17 scoped to actual HEAD. Otherwise legitimate inputs still produce invalid requests, silent authority reinterpretation or rejected response bodies | Low-to-moderate complexity, contained to HTTP/2 normalization and method classification | Choose canonicalization versus rejection for ambiguous repeated fields. Recommend preserving valid protocol-neutral input, rejecting ambiguous authority, and exact method matching. Reverse only under a documented stricter request API enforced before these paths |
| 3. Decoder seam: R08 and existing H24/H25/H09 | Decide what custom registrants may safely rely on today. Otherwise the published seam exposes failures described as unreachable | HTTP-message fixes are independent of a codec vendor; H09's structural fix has wider task-framework scope and remains separately decided | Recommend fixing H24/H25 integration before advertising custom decoding as supported; keep codec supply parked. The alternative is an explicit temporary restriction on registration. An empty default registry alone does not reverse the recommendation |
| 4. Current-status records: R09 | Make the design, plan and owed list tell the same current story, including accepted reachable risks | Low implementation risk; documentation-only scope, but important for review and release decisions | Recommend one current row per item with links to dated history. Preserve historical measurements without presenting their old status as current. If a different canonical ledger is chosen, redirect readers to it and stop labeling this one authoritative |

For future verification, use deterministic cases for the branch/ordering defects above. Repetition is not a substitute for the missing scenario. Follow the project's supported toolchain/variant and Windows transport checks when fixes are authorized; the x86-64 GCC gap is particularly relevant to shared TLS changes.

## Review coverage and evidence boundaries

| Changed area | Cross-file paths followed | Review result |
|---|---|---|
| Core transport/time | NetUtils → OS predicates → HTTP/1 and h2 end handling / TLS establishment retry; TimeBoostImports → CookieJar | Predicate intent verified; R01; shared scope identified |
| TLS and task infrastructure | AsioSslStreamWrapper → stranded policies → TaskBase/ExecutionQueue scheduling → MultiOperationTask accounting → both drivers' teardown | Forwarding fix supported; R03; accepted exceptional-path qualifications preserved |
| HTTP/2 primitives/engine | Globals → FrameCodec → Session → HPACK encoder/decoder, stream registry and flow windows → driver event delivery | Original targeted protocol fixes traced; R05–R07 at message-boundary integration |
| HTTP/1 parser/driver | Limits → Beast facade → body callback → DataBlock event → request mailbox → stream completion / reuse / idle close | Lifetime/reuse changes supported; R01–R03 |
| Request task/session | Event batching → deferred sink calls → failure precedence → retry eligibility → redirect continuation / decoder / cookies | H07/H22 verified as scoped; R04 and R08 |
| Pool | Factory → fallback driver publication → state polling → deferred schedules → reservation release → maintenance timer / dispose | H03/H04 and retirement correction supported; admission residual retained |
| Proxy/cookie/header helpers | Uri authority → CONNECT renderer; CookieJar existing-cookie lookup → replacement; HeaderList multimap → normalization/HPACK | H26–H28 supported; R05/R06 |
| Tests and records | Relevant h2core, h2client, httpclient, shared driver probes, schedule/strand seams, cancellation and sink tests; remediation commits and active ledgers | Scenario coverage inspected; missing cases identified above; no execution claimed |
| Compression proposals | Three parked documents and public decoder registry → actual session decode call path | Proposal text unchanged; prior C01–C11 carried, reachability corrected |

The strongest positive evidence in this review is a corrected invariant that can be followed across its producer and consumer, such as retained write storage, a publication gate, or an ACK placed in the same ordered queue as the blocks it governs. Historical green runs add context; they do not refute a newly identified path that their fixtures never exercise.
