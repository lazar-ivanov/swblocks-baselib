# HTTP/2 through L6 and embedded decompression: architecture and security review

Completed: 2026-09-21. Reviewed production snapshot: `c8e9be88ff3d1a8c56008f4fa50f90c7001bfbd0`.

## Assessment and scope

The separation of protocol engine, connection drivers, request tasks, and session policy is appropriate for baselib. The implementation nevertheless has unresolved memory-lifetime, concurrency, replay-safety, streaming-completion, and protocol-state defects. I would address the P1 findings before treating L0–L6 as ready for production use. The embedded decoder proposal is a plausible direction, but its current safety and acceptance criteria are insufficient to close the content-decoding deferral.

This was a static review: source and test inspection, repository-wide reference searches, comparison with the design and previous review records, and consultation of primary protocol/library documentation. **No builds, tests, fuzzers, benchmarks, or executable probes were run. No implementation files were changed and nothing was committed. This report is the only file created by this review.** Suggested checks below are future validation, not results obtained here.

The interrupted review originally examined `f7bb591ca6abf92c9f3e6e5608fb2cf2d816a9e4`. The intervening commits `ca5bf08` and `c8e9be8` changed test compatibility only; the production findings remained applicable. HEAD and the tracked working tree were unchanged at the final resumption. The three compression documents were already untracked and were preserved as supplied. Earlier attempts to write this report were rejected by automatic approval review because of usage limits; those attempts created no file.

Reviewed plans:

- [HTTP/2 design](http2-design.md), including decisions, concurrency rules, security limits, and non-goals.
- [HTTP/2 implementation plan](http2-implementation-plan.md), through L6; L7/L8 obligations were considered where they depend on an existing contract.
- [Embedded compression research](embedded-compression-design.md), [narrowed decompression design](embedded-decompression-design.md), and [decompression implementation plan](embedded-decompression-implementation-plan.md).
- Existing [L2](issues/http2-l2-review-record.md), [L3](issues/http2-l3-review-record.md), [L4](issues/http2-l4-review-record.md), [L5](issues/http2-l5-review-record.md), and [L6](issues/http2-l6-review-record.md) review records, relevant follow-up records, and the [decoder deferral](issues/http-content-decoders-deferral.md). Historical test results in those records were not independently reproduced here.

Severity: **P1** means a release-blocking safety, availability, or data-integrity problem, or a blocking flaw in the proposed implementation approach. **P2** means a material correctness, interoperability, integration, or verification problem. **P3** means a narrower API or policy defect. Proposal findings describe hazards in specified future work, not vulnerabilities in a decoder that already ships. Conditions and static inferences are identified explicitly; no exploitability or race reproduction is claimed.

## Architecture and dependency coverage

The main execution and data paths traced were:

```text
ClientSession / SessionRequestTask
  -> HttpClientRequestTask -> ConnectionPool -> ConnectionAttempt
  -> Http2ConnectionTask -> ClientConnectionTaskBase
     -> TcpTunnelStage -> TCP establishment -> optional TLS handshake/floor check
     -> HTTP/2 Session -> FrameCodec, HPACK, StreamStateMachine, FlowControlWindow
     -> or factory-created Http1ConnectionTask -> Http1Codec -> isolated Beast backend
  <- ClientStreamEventSink -> request mailbox -> BodySink or buffered response
  -> wrapper continuation -> cookies / decoder / redirect / retry
  -> TaskBase / MultiOperationTask / ExecutionQueue completion and disposal
```

| Area | Files and dependencies inspected | Main obligations traced |
|---|---|---|
| L0, existing task infrastructure | `tasks/TaskBase.h`, `Task.h`, `MultiOperationTask.h`, `ExecutionQueueImpl.h`, `TcpBaseTasks.h`, `TcpSslBaseTasks.h` | Handler completion, operation accounting, pre-handshake hook, cancellation, wrapper forwarding, lock order |
| L1, generic capabilities | `core/Uri.h`, `http/HeaderList.h`, `tasks/TcpStrandedStreams.h`, `TcpSslStrandedStreams.h`, `TcpTunnelStage.h`, `crypto/TlsClientProfile.h`, `TlsPeerVerification.h`, `TlsClientHello.h`, `CryptoBase.h`, `tasks/AsioSslStreamWrapper.h` | Input validation, authority/hostname handling, proxy negotiation, strand ownership, profile context isolation, verification and negotiated floor |
| L1, profile types | `http2/Http2Profile.h`, `httpclient/HeaderProfile.h`, `data/models/HttpClientProfiles.h`, precompiled-header boundaries | Configuration ownership, serialization shapes, later profile dependencies, optional dependency boundaries |
| L2–L3, protocol engine | `http2/Globals.h`, `FrameCodec.h`, `HpackEncoder.h`, `HpackDecoder.h`, `HpackDynamicTable.h`, `HpackHuffman.h`, `FlowControlWindow.h`, `StreamStateMachine.h`, `Session.h` | Parsing, state transitions, SETTINGS/HPACK ordering, flow control, malformed messages, bounded peer-controlled state |
| L2, common HTTP | `httpclient/ClientTypes.h`, `ClientConnection.h`, `Http1Codec.h`, `detail/Http1CodecBeastImpl.h`, `CookieJar.h`, `RedirectPolicy.h`, `ContentDecoder.h` | Contracts, framing, repeated fields, streaming ownership, cookie scope, redirects, decoding limits |
| L3–L5, I/O and coordination | `ClientConnectionTaskBase.h`, `Http1ConnectionTask.h`, `http2/Http2ConnectionTask.h`, `HttpClientRequestTask.h`, `ConnectionPool.h` | Buffer lifetime, one active write, driver publication, pool reservations, retry classification, deadlines, disposal and callback ordering |
| L6, composition | `ClientSession.h`, legacy `http/SimpleHttpTask.h`, task/object-model utilities | Per-session snapshots, hop transitions, credential stripping, body replay, response decoding, secure diagnostics |
| Tests and build boundaries | HTTP/2 core/client/profile and HTTP-client test modules, shared test peer, `src/utests/AGENTS.md`, devenv/toolchain makefiles | What assertions establish, unsupported combinations, C++11/header-only constraints, test-module size requirements |

Repository-wide reverse-reference searches found the new production HTTP client concentrated in these headers; existing applications have not yet been migrated to the L8 facade. The generic task/TLS changes have a broader impact because existing networking code already uses those bases. This is a dependency-oriented review, not a claim to have re-audited every unrelated application, Boost/OpenSSL implementation, or numeric table entry in the repository. File/line references below refer to the reviewed snapshot.

## Current implementation findings

### H01 — P1: HTTP/1 completion invalidates buffers still owned by an asynchronous write

**Evidence:** [Http1ConnectionTask.h](../../src/include/baselib/httpclient/Http1ConnectionTask.h#L623), lines 623–675, 690–710, 902–924, 1076–1125, and 1650–1671.

`onStartRequest()` stores the serialized head and body in members and passes views into them to `asio::async_write()`. The completion handler retains the task, but does not separately retain immutable write storage. The read loop is already active. A complete response calls `finishStream()` immediately: it clears `m_requestHead`, resets `m_requestBody`, discards the request, and can publish `Ready`. Neither response completion nor `deriveIsReusable()` waits for the write to complete. Stream cancellation takes the same buffer-clearing route.

**Trigger:** Send a large POST. The peer reads its headers, returns a complete response such as 413, and stops consuming the upload while the composed write is still pending. The response path mutates the write's head storage and releases body ownership. A subsequent request can overwrite those members and initiate another write. Caller release after request completion can also release the remaining body owner.

**Impact:** Invalid buffer lifetime, changed bytes during an outstanding operation, and potentially overlapping composed writes; memory safety and wire corruption are possible. Retaining the connection task and balancing `MultiOperationTask` operations do not retain the contents of members that `finishStream()` clears. Asio requires both buffer lifetime through completion and exclusion of overlapping writes. [Asio async_write contract](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference/async_write/overload1.html).

**Intent and correction:** The design's full-duplex connection and reusable h1 driver require a write-completion barrier. Own each write's immutable storage until its handler finishes. Stop or drain an early-aborted upload and keep that connection unavailable for reuse until all relevant handlers settle. Validate early final responses, cancellation during a blocked upload, and immediate next-request submission, with lifetime instrumentation.

### H02 — P1: HTTP/1 treats an unfinished write as proof that the request was not sent

**Evidence:** [Http1ConnectionTask.h](../../src/include/baselib/httpclient/Http1ConnectionTask.h#L690), lines 697–699 and 955–987; [ConnectionPool.h](../../src/include/baselib/httpclient/ConnectionPool.h#L430), replay predicate; [ClientSession.h](../../src/include/baselib/httpclient/ClientSession.h#L1151), `chkPrepareRetry()`.

`m_requestBytesWritten` becomes true only when the composed write invokes its completion handler. `onPeerClosed()` uses its negation as `isRetryable`. Bytes can have reached the server long before that callback. A read-side EOF can therefore classify a partially transmitted request as provably unprocessed.

**Trigger and impact:** A server acts on an upload prefix or request headers, then closes while a large write remains pending. The session's default retry rule accepts `isRetryable` for a replayable body even for POST. It can repeat a side effect without the caller enabling idempotent connection-loss retries. This is independent of H01's storage problem.

**Intent and correction:** D6 distinguishes proof of non-processing from uncertainty; write completion is not that proof. Mark a request as potentially sent once writing starts, unless a lower-level result establishes that zero bytes could have escaped. Keep method-based optional retries separate. Validate a read-close/write-completion interleaving with a side-effect-counting peer and an otherwise replayable POST.

### H03 — P1: pool disposal races with actions already collected outside its lock

**Evidence:** [ConnectionPool.h](../../src/include/baselib/httpclient/ConnectionPool.h#L1747), `runActions()` at 1747–1781, `startConnection()` at 1835–1860, `armMaintenance()` at 1898–1914, `disposeInternal()` at 2019–2123.

Actions are collected under `m_lock` and executed after releasing it. `runActions()` dereferences the member `m_eqConnections` when scheduling. Concurrent disposal sets the disposed flag and clears state under the lock, then flushes and resets that same non-atomic smart pointer outside the lock. An action batch can be suspended between collection and execution, let disposal finish, and then dereference a reset queue or race its ownership operation.

The maintenance timer has a related unsynchronized path: deferred `armMaintenance()` calls `expires_from_now()`/`async_wait()`, while disposal calls `cancel()` on the same timer. The timer is not protected by a common executor or lock across those operations. Shared timer objects are not generally thread safe. [Asio deadline_timer contract](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference/basic_deadline_timer.html).

**Impact:** Undefined behavior or crash during shutdown; work can also be scheduled after the disposal sweep. Internal maintenance can provide the competing action batch even if an application stops submitting new requests before disposal.

**Intent and correction:** Keeping the pool lock a leaf is correct, but requires a lifetime/admission protocol for deferred work. Retain a queue reference in an admitted batch and coordinate batch completion with disposal; serialize timer mutation. Do not fix this by acquiring an execution-queue lock while holding the pool lock. Validate paused action batches and maintenance rearming across disposal, including one-thread execution.

### H04 — P1: the pool reads fallback-driver and negotiated state before synchronized publication

**Evidence:** [ClientConnectionTaskBase.h](../../src/include/baselib/httpclient/ClientConnectionTaskBase.h#L577), assignments at 581 and 608, getter at 669; [ClientSession.h](../../src/include/baselib/httpclient/ClientSession.h#L1730), driver resolver; [ConnectionPool.h](../../src/include/baselib/httpclient/ConnectionPool.h#L1206), `refreshEntry()` at 1215–1234 and `effectiveMaxConnectionsPerKey()` at 953–963; [Http2ConnectionTask.h](../../src/include/baselib/http2/Http2ConnectionTask.h#L2711), negotiated-state contract.

The connection strand assigns the plain `m_connection` smart pointer after creating an h1 fallback driver. The pool's resolver copies that pointer without synchronization and is called before waiting for establishment-task completion. The pool mutex does not protect a strand-side write that does not take it.

Separately, `effectiveMaxConnectionsPerKey()` evaluates `connection->negotiated().protocol()` before `entry->isReady`. It can read the plain negotiated value while a Connecting driver writes it. The driver's own getter says it is valid only after leaving Connecting.

**Impact:** C++ data races, including unsafe smart-pointer publication, regardless of whether a particular platform usually observes the expected pointer/value. The atomic `TaskBase` state and atomic connection state do not establish publication when the reader has not acquired the relevant completed/ready state first.

**Correction:** Publish an immutable driver snapshot through a lock, posted event, or appropriate atomic ownership mechanism. Require the synchronized ready state before reading negotiated data. Preserve the pool's leaf-lock contract. Validate fallback creation/maintenance overlap and the Connecting-to-Ready transition under a race detector. Earlier L5/L6 records identify the unresolved publication concern; its current composition remains unsafe by inspection.

### H05 — P1: informational responses bypass aggregate memory limits

**Evidence:** [Session.h](../../src/include/baselib/http2/Session.h#L3155), `deliverHeaderBlock()`; [HttpClientRequestTask.h](../../src/include/baselib/httpclient/HttpClientRequestTask.h#L838), lines 845–854 and mailbox storage; [Http1Codec.h](../../src/include/baselib/httpclient/Http1Codec.h#L694), `fileInterimAndRestart()`; [Http1ConnectionTask.h](../../src/include/baselib/httpclient/Http1ConnectionTask.h#L768), interim delivery.

Every h2 100/103 block can be individually within the header cap, leave final headers pending, and append another retained `InterimResponse`. The request mailbox is another accumulation point. In h1, the codec appends each informational response and resets per-message parsing limits before the next one. Neither path imposes an aggregate interim count or byte bound.

**Trigger and impact:** A peer sends many valid informational responses, optionally using compact HPACK representations of repeated fields. This consumes retained memory without DATA flow control or the PING/SETTINGS rate limiter. Timeouts bound elapsed time, not the amount of memory a fast peer can allocate before expiry.

**Intent and correction:** Design §4.6's bounded peer-controlled state must include aggregate informational headers, not just each block. Bound count and cumulative bytes at the parser/session boundary and at event admission; avoid moving the unbounded queue to another layer. On excess, reset the h2 stream or close h1 as appropriate. Validate repeated 103 blocks under the per-block cap and a slower mailbox consumer.

### H06 — P1: partial BodySink consumption can lose response bytes and has no resume path

**Evidence:** [ClientTypes.h](../../src/include/baselib/httpclient/ClientTypes.h#L305), BodySink contract; [HttpClientRequestTask.h](../../src/include/baselib/httpclient/HttpClientRequestTask.h#L952), `offerToSink()`, `applyData()`, `applyClosed()` at 1114–1149, and success at 1268–1272; [existing streaming test](../../src/utests/utf_baselib_httpclient/TestHttpClientRequestTask.h#L1396).

The contract permits a sink to consume zero or part of a block and promises to offer the remainder again. `offerToSink()` retains the remainder and stops after a partial result, but another DATA event is the normal trigger to offer it again. There is no sink-readiness notification. Once receive credit is exhausted, the peer cannot send the event needed to restart delivery.

If END_STREAM arrives while pending download bytes remain, `applyClosed()` cancels timers, queues `onComplete()`, releases the connection, and marks success without draining that queue. The caller can receive a truncated body accompanied by successful completion.

**Existing evidence is unusually direct:** the test sends `abcdefgh` then `ij`, accepts three bytes per offer, closes, and asserts success with only `abcdef` received (lines 1432–1458). It verifies ordering of the accepted prefix while explicitly accepting loss of `ghij`.

**Correction:** Provide a posted readiness/resume mechanism and separate transport end-of-stream from completion of application delivery. Complete only after all pending bytes are accepted; preserve deadline/cancel behavior while waiting. Fix the test to require all ten bytes, plus a zero-consumption/window-exhaustion/resume case. This is a current contract defect, separate from the documented non-ready upload-source deferral.

### H07 — P2: exceptions from final sink callbacks can be converted into successful completion

**Evidence:** [HttpClientRequestTask.h](../../src/include/baselib/httpclient/HttpClientRequestTask.h#L450), `applyEvents()` at 450–525, `applyClosed()` at 1133–1142, `failWith()` at 1390–1397.

The drain first applies a whole event batch under the task lock, then runs deferred callbacks. A Closed event sets `m_isCompletionPending` before `onData()`/`onComplete()` execute. If one of those callbacks throws, `runDeferred()` captures the exception and calls `failWith()`, but `failWith()` returns immediately when completion is already pending. The earlier success survives.

**Trigger:** Final DATA and Closed in the same batch with a throwing sink, or a sink whose `onComplete()` throws. The latter does not depend on a cross-thread race.

**Intent and correction:** The comments promise that the first deferred callback failure fails the request while cleanup still runs. Allow a pending success to become failure until publication; preserve an earlier real failure. Validate throwing final-data and terminal callbacks, and verify both the exception and exactly-once resource release.

### H08 — P2: retries reuse a BodySink after delivering its terminal callback

**Evidence:** [ClientSession.h](../../src/include/baselib/httpclient/ClientSession.h#L1151), retry predicate and reuse of `m_bodySink` in `startHop()`; [HttpClientRequestTask.h](../../src/include/baselib/httpclient/HttpClientRequestTask.h#L1114), unconditional sink completion on stream closure; [ClientTypes.h](../../src/include/baselib/httpclient/ClientTypes.h#L329), terminal contract.

The driver's stream-closure path calls the same sink's `onComplete()` even when that hop fails. A retry then sends new data to that already-completed sink. The default h1 fallback bounce can trigger this even before any body was delivered. With optional idempotent-on-connection-loss retry enabled, a GET whose body prefix was already delivered can append the retried body to that prefix.

**Impact:** Data after a promised terminal callback, duplicate terminal calls, and potentially duplicated response bytes. Request-body replayability does not establish response-sink replayability.

**Correction:** Own sink completion at the logical request level. Suppress completion for an invisible internal attempt and forbid transparent retries after response bytes escape unless the sink explicitly supports rollback/reset. The existing refusal to follow redirects with a sink recognizes the same problem and should inform retry semantics. Validate fallback with a terminal-state-checking sink and connection loss after a visible response prefix.

### H09 — P1: session continuations execute caller code and decoding under execution-queue locks

**Evidence:** [ExecutionQueueImpl.h](../../src/include/baselib/tasks/ExecutionQueueImpl.h#L470), lock at 480 and continuation call at 499; [TaskBase.h](../../src/include/baselib/tasks/TaskBase.h#L458), wrapper forwarding; [ClientSession.h](../../src/include/baselib/httpclient/ClientSession.h#L1376), lock at 1390, `absorbResponse()` at 1181, decoder factory/write/finish at 1247–1263, and body-source `rewind()` at 1171/1359.

The queue invokes `continuationTask()` while holding its mutex. The session then takes the wrapper mutex and calls registered decoder code and caller-provided `BodySource::rewind()`. Reentering wrapper operations from those callbacks can deadlock on the wrapper mutex; submitting work to the same execution queue can deadlock on the queue mutex. CPU-heavy decoding also stalls unrelated completions on that queue.

The completed hop has already canceled its timers. Consequently, this decoding work is not interrupted by the request's active deadline, despite the chain-level network deadline fix. Merely running on GeneralPurpose rather than the I/O pool does not make work under these locks safe.

**Intent and correction:** This violates the nonblocking composition intended by design §5.2. Move response transformation and replay preparation into scheduled task work that runs without queue/wrapper locks; snapshot state and apply the resulting transition under brief synchronization. Keep cancellation and the logical deadline active through transformation. The earlier L6 review records the lock problem as owed work; shipping real peer-driven codecs increases its importance. Validate reentrant callbacks and a deliberately slow decoder with other queued completions.

### H10 — P2: queued header blocks can follow a SETTINGS ACK with obsolete HPACK or frame limits

**Evidence:** [Session.h](../../src/include/baselib/http2/Session.h#L3606), encode/fragment at 3615–3660, `produce()` at 857–877, and peer settings at 2287–2293/2471–2529; [Http2ConnectionTask.h](../../src/include/baselib/http2/Http2ConnectionTask.h#L1572), write-in-flight guard.

Headers are HPACK-encoded and framed when queued, not when committed to the next write. While an existing write is blocked, another request can queue a block using the old table/frame limits. A peer SETTINGS reduction updates the encoder's pending capacity and queues its ACK. `produce()` emits control frames before the already-encoded block.

**Trigger:** Queue headers, receive HEADER_TABLE_SIZE=0, then produce. The ACK precedes a block encoded without the required reduction update and possibly using old dynamic indices. A MAX_FRAME_SIZE reduction likewise leaves already-framed blocks too large after its ACK. A later `encode()` cannot repair bytes already queued.

**Correction:** Coordinate SETTINGS acknowledgements with the actual emission boundary and HPACK history. Either safely finish prior blocks before acknowledgement or regenerate the affected queued sequence against the new state. Independently re-encoding one block can also desynchronize later blocks. Validate blocked writes plus SETTINGS reductions and several queued requests using an independent peer. [RFC 9113 §4.3.1](https://www.rfc-editor.org/rfc/rfc9113.html#section-4.3.1) requires the reduced-table update in the next block after acknowledgement.

### H11 — P2: the decoder deliberately accepts a missing mandatory HPACK size update

**Evidence:** [HpackDecoder.h](../../src/include/baselib/http2/HpackDecoder.h#L139), `setMaxDynamicTableSize()` at 161–164, decode dispatch at 178–245, and size-update validation at 538–571; [Session.h](../../src/include/baselib/http2/Session.h#L2571), acknowledged settings.

On acknowledgement of a smaller advertised table, the decoder changes only the maximum permitted future update. It neither requires an update at the next block nor rejects fields using the old table first. The comment at lines 149–158 explicitly chooses tolerance of a peer that never reduces its table.

**Trigger:** Advertise table size zero, receive the ACK, then receive a header block without the reduction instruction. Existing indices and further insertions can still use the original capacity.

**Intent and correction:** Pre-ACK tolerance is correct and already implemented; post-ACK tolerance conflicts with the required connection-level COMPRESSION_ERROR. [RFC 9113 §4.3.1](https://www.rfc-editor.org/rfc/rfc9113.html#section-4.3.1). Track a required reduction, including the smallest pending bound, and require a conformant first instruction before fields. This is not unbounded memory growth: the retained table remains bounded by its old capacity. Validate legal pre-ACK blocks and rejected post-ACK missing updates.

### H12 — P2: a nondefault initial encoder capacity is neither clamped nor announced

**Evidence:** [Session.h](../../src/include/baselib/http2/Session.h#L501), initializer at 517 and `encoderTableSize()` at 1267–1276; [HpackEncoder.h](../../src/include/baselib/http2/HpackEncoder.h#L108), constructor and deferred size-update logic.

`hpackEncoderTableSize` directly initializes the table, while the constructor initializes `m_sizeUpdatePending=false`. A profile requesting more than 4096 bytes can retain and reference entries a default peer has evicted, before a peer setting constrains it or if that setting is omitted. Smaller custom capacities also begin without an announcement of the chosen size.

**Intent and correction:** Profile control must remain within the connection's negotiated compression state. Initialize from the protocol's 4096-byte default, clamp to the peer's currently known bound, and signal changes before using them. [RFC 9113 §4.3.1](https://www.rfc-editor.org/rfc/rfc9113.html#section-4.3.1). Validate custom capacities above and below 4096, including a peer that sends an empty SETTINGS frame and never advertises HEADER_TABLE_SIZE. This is an existing profile-knob defect even though complete browser profiles belong to L7.

### H13 — P2: prioritized HEADERS can exceed the peer's maximum frame size by five bytes

**Evidence:** [Session.h](../../src/include/baselib/http2/Session.h#L3617), first-fragment calculation and `serializeHeaders()` call; [FrameCodec.h](../../src/include/baselib/http2/FrameCodec.h#L1134), HEADERS payload construction.

The first fragment may use all of `m_peerMaxFrameSize`. `serializeHeaders()` then adds the five-byte priority fields when the profile requests them. With a 16384-byte limit, an otherwise valid large block produces a 16389-byte HEADERS payload. CONTINUATION fragmentation does not repair the first frame.

**Correction:** Subtract priority and any padding overhead from the first fragment's payload budget; continuation fragments use their own budget. This preserves the intended priority fingerprint while remaining within framing limits. Validate encoded block lengths around the first-fragment boundary with priority both enabled and disabled. [RFC 9113 §4.2](https://www.rfc-editor.org/rfc/rfc9113.html#section-4.2).

### H14 — P2: padding-only DATA can exhaust a stream window permanently

**Evidence:** [Session.h](../../src/include/baselib/http2/Session.h#L1798), padding accounting at 1817 and 1875–1879, and `consumed()` at 1142–1160; [Http2ConnectionTask.h](../../src/include/baselib/http2/Http2ConnectionTask.h#L1353), empty-data return at 1362–1369.

Padding is charged to flow control and immediately marked consumed internally, but that path does not flush the stream's pending WINDOW_UPDATE. The driver suppresses an empty application-data event, so no consumer callback calls `Session::consumed()` to flush it either.

**Trigger:** A valid sequence of DATA frames containing only padding, without END_STREAM, consumes the stream's advertised credit. The peer then waits for WINDOW_UPDATE while the client waits for application data that cannot be sent.

**Correction:** Evaluate stream credit after internally consuming padding, without depending on an application callback. Connection-level credit is separately flushed by the engine's reap path; this finding is specifically about the stream window. Validate a full window of padded frames followed by real data, with another stream making progress concurrently.

### H15 — P2: reducing the initial receive window leaves its automatic update threshold too large

**Evidence:** [FlowControlWindow.h](../../src/include/baselib/http2/FlowControlWindow.h#L470), constructor threshold, `applyInitialWindowSizeChange()` at 534–540, and `shouldSendWindowUpdate()` at 598–600; [Session.h](../../src/include/baselib/http2/Session.h#L2664), application to existing streams.

The receive-credit threshold is derived from the initial window once. Applying an acknowledged smaller initial window changes the window but leaves that threshold unchanged. A rider opened before acknowledgement starts at 65535, with a threshold of 32767. If the profile advertises a 1024-byte window, acknowledgement reduces the window to 1024 while retaining the old threshold. Receiving and consuming all 1024 bytes cannot trigger an update.

**Correction:** Distinguish an explicit profile threshold from the automatic half-window policy and recompute the latter when the window changes. Ensure available pending credit can unblock an exhausted window. Validate a stream opened before ACK, a smaller acknowledged window, full consumption, and continued peer progress; checking only the numeric window adjustment misses this liveness defect.

### H16 — P2: protocol-neutral outgoing headers are not validated for HTTP/2, including after redirects

**Evidence:** [Http2ConnectionTask.h](../../src/include/baselib/http2/Http2ConnectionTask.h#L2731), `toSessionRequest()`; [Session.h](../../src/include/baselib/http2/Session.h#L929), request validation and header append; [ClientSession.h](../../src/include/baselib/httpclient/ClientSession.h#L1337), redirect rewrite; compare [Http1ConnectionTask.h](../../src/include/baselib/httpclient/Http1ConnectionTask.h#L475), request rendering.

The h2 conversion copies ordinary headers unchanged. `Session::submitRequest()` checks only basic pseudo-header presence before appending them. Generic HeaderList validation and lowercasing do not reject HTTP/2-forbidden connection fields, disallowed TE values, inconsistent Host/authority, or a Content-Length inconsistent with the body. The h1 renderer, by comparison, rejects request Transfer-Encoding and recomputes/removes Content-Length.

A concrete internal trigger does not require an invalid original request: a POST with an explicit Content-Length follows a 303. The session changes it to GET and drops its body, but retains body-related headers. The h2 driver can send END_STREAM with the old nonzero Content-Length.

**Impact and correction:** Valid protocol-neutral use or a legal redirect can produce a malformed h2 request. Normalize or reject protocol-specific fields at the driver boundary and update representation headers whenever redirect policy drops the body. Preserve ordered-header semantics while doing so. Validate ordinary h1-style caller headers, conflicting authority, and POST-to-GET redirects on both protocols. Connection-specific fields are forbidden by [RFC 9113 §8.2.2](https://www.rfc-editor.org/rfc/rfc9113.html#section-8.2.2). No request-smuggling exploit is established by this review.

### H17 — P2: bodyless responses accept nonempty DATA as successful content

**Evidence:** [Session.h](../../src/include/baselib/http2/Session.h#L2140), `judgeDataFrame()` at 2155–2188, `expectsNoContent` at 949/3172–3174, and `onPeerEndStream()` at 3213–3232.

HEAD requests and 204/304 responses set `expectsNoContent`. The DATA validator uses that flag only to disable Content-Length checking; it does not reject nonempty DATA. Such bytes are delivered and the stream can complete successfully.

**Intent and correction:** Exempting representation metadata from a body-length equality check is necessary for HEAD/304, but must not grant permission to carry a body. Reject nonempty content before delivery, while allowing legitimate representation-length metadata where appropriate. Validate each bodyless case with forbidden content and with legal empty completion. [RFC 9110 §6.4.1](https://www.rfc-editor.org/rfc/rfc9110.html#section-6.4.1).

### H18 — P2: the engine does not enforce an initial non-ACK SETTINGS frame

**Evidence:** [Session.h](../../src/include/baselib/http2/Session.h#L693), `feed()`, frame dispatch around 1710, and `handleSettings()` at 2257; the driver's `peerSettingsSeen` bookkeeping controls capacity, not first-frame validation.

The server role checks the client connection-preface string, but frame dispatch does not require the first following frame to be non-ACK SETTINGS. In client role there is no initial SETTINGS phase either. A peer can ACK the client's opening settings and send response HEADERS without ever sending its own settings; these paths are accepted.

**Impact and correction:** The engine accepts a malformed connection preface, weakening both protocol validation and the pool's assumptions about when peer limits are known. Require an initial non-ACK SETTINGS frame before normal dispatch. This does not require delaying the client's allowed opening request. Validate ACK, DATA, PING, and HEADERS as illegal first frames, plus a legal SETTINGS coalesced with following traffic. [RFC 9113 §3.4](https://www.rfc-editor.org/rfc/rfc9113.html#section-3.4).

### H19 — P2: the plain client still requires OpenSSL headers and version macros

**Evidence:** [ClientConnectionTaskBase.h](../../src/include/baselib/httpclient/ClientConnectionTaskBase.h#L20), unconditional `CryptoBase.h` include at 29 and OpenSSL version error at 48–49; [ClientSession.h](../../src/include/baselib/httpclient/ClientSession.h#L20), include graph; [design §1.5](http2-design.md#L118).

Instantiating the cleartext stream policy cannot remove unconditional includes or preprocessor errors. The shared connection base imports CryptoBase and enforces OpenSSL 1.1+ even for plain TCP. Keeping OpenSSL out of a PreCompiled header does not make the actual client include path OpenSSL-optional.

**Intent and correction:** D3 and the library's optional-dependency practice require TLS-specific checks behind the stream-policy/header boundary. Move the OpenSSL specialization and its capability guard to a TLS-specific header, leaving the plain policy independent. This is a static include-graph conclusion, not a claimed failed build. A future standalone plain-client include/instantiation check without OpenSSL include paths should cover it.

### H20 — P2: request diagnostics have no secure-redaction contract and include complete URLs

**Evidence:** [HttpClientRequestTask.h](../../src/include/baselib/httpclient/HttpClientRequestTask.h#L1444), timeout text at 1458; [ClientSession.h](../../src/include/baselib/httpclient/ClientSession.h#L1024), exhausted-budget text; [Uri.h](../../src/include/baselib/core/Uri.h#L1327), recomposition including userinfo/query/fragment; legacy [SimpleHttpTask.h](../../src/include/baselib/http/SimpleHttpTask.h#L118), secure mode and redaction at 187/220/552–559.

The new request and session types format `url().toString()` into exceptions without an equivalent of the existing secure-mode redaction policy. A URL can contain credentials, query tokens, sensitive paths, and even a fragment that is not sent over HTTP. These values can escape through ordinary timeout logging and exception diagnostics.

**Intent and correction:** L8's compatibility facade is intentionally future work, but the lower layers that construct exceptions need a redaction contract before that facade can reliably honor `isSecureMode`. Always redact userinfo and propagate a request/session diagnostic policy for other sensitive components. Validate both task-level and logical-request timeout diagnostics using distinctive secret markers. This concerns secret-bearing URLs, not a claim that every logged URL is sensitive.

### H21 — P2: successful HTTP/1 selection consumes a retry; disabling retries breaks first requests

**Evidence:** [ClientSession.h](../../src/include/baselib/httpclient/ClientSession.h#L1142), fallback explanation and retry; [existing fallback test](../../src/utests/utf_baselib_httpclient4/TestClientSession.h#L2041), `maxRetriesPerRequest=0` and asserted failure; [L6 review follow-up](issues/http2-l6-review-record.md#L711).

The first request rides the h2 Connecting placeholder even when the configured cleartext protocol is already h1. After selection/handoff creates a usable h1 driver, the placeholder bounces the request as retryable and the session must spend a retry to use it. With zero retries the otherwise valid request fails. On close-after-each-response h1 peers, this happens repeatedly.

**Intent and correction:** The rider optimization avoids a round trip for h2, but protocol selection is not a failed application attempt. Transfer the untransmitted request to the selected driver, or avoid dispatching a rider when the selected protocol is already known to be h1. Validate first-use cleartext h1 and TLS ALPN fallback with retries disabled. This is an acknowledged unresolved design compromise; the current test documents the defect rather than preventing it.

### H22 — P3: cancellation between redirect hops can report the intermediate response as success

**Evidence:** [ClientSession.h](../../src/include/baselib/httpclient/ClientSession.h#L1369), cancellation flag and continuation at 1392–1396; [previous L6 finding 9](issues/http2-l6-review-record.md#L332).

After a successful redirect hop has finished, cancellation can set `m_cancelRequested` before the wrapper decides the next hop. The continuation returns null and leaves the completed hop's success as the logical request result. The caller sees a successful intermediate 3xx rather than the expected cancellation of a request that would otherwise continue.

**Correction:** Specify a logical completion/cancellation boundary and make cancellation before that boundary an expected aborted result. Validate a controlled pause between finalizing the 3xx hop and creating the next one. Cancellation after final logical completion may remain a no-op.

### H23 — P3: one retry knob controls two independently renewed budgets

**Evidence:** [ConnectionPool.h](../../src/include/baselib/httpclient/ConnectionPool.h#L430), request predicate and waiter budget at 1587; [ClientSession.h](../../src/include/baselib/httpclient/ClientSession.h#L1130), dispatched-attempt count; [previous L6 finding 10](issues/http2-l6-review-record.md#L341).

Queued establishment retries and dispatched request retries each use `maxRetriesPerRequest`. Reacquiring for another dispatched attempt creates a new waiter budget. Across the appropriate sequence of queued establishment failures and dispatched retryable failures, the resulting upper bound is multiplicative, potentially `(N+1)^2` establishments per hop rather than `N+1`. This is not a claim that every request, or every first rider, takes that many attempts.

**Correction:** Share a logical attempt budget or expose separately named establishment and replay limits. The repaired chain deadline bounds elapsed network time but does not make the request-count promise accurate. Validate mixed queued/dispatched failures, not only one retry mechanism at a time.

### H24 — P2: Content-Encoding is treated as one field value instead of an ordered coding list

**Evidence:** [ClientSession.h](../../src/include/baselib/httpclient/ClientSession.h#L1218), `tryGet()`/exact registry lookup at 1225–1232 and removal of all encoding fields at 1282–1283; generic HeaderList permits repeated fields.

A legal `Content-Encoding: gzip, br` is looked up as one registry key, so the session silently leaves it encoded even when both codecs are registered. Repeated Content-Encoding fields are worse: `tryGet()` selects one value, decodes only that layer, and then removes all Content-Encoding fields. Depending on order, that either fails decoding the wrong outer layer or returns still-encoded bytes without the metadata needed to interpret them.

**Intent and correction:** Parse all field lines as one ordered list and decode in reverse application order, with a bounded coding depth and appropriate intermediate/output limits. If a chain is unsupported, preserve all metadata and expose that outcome consistently. Validate combined and repeated field forms, unknown layers, and repeated codings. Content-Encoding records coding application order. [RFC 9110 §8.4](https://www.rfc-editor.org/rfc/rfc9110.html#section-8.4).

### H25 — P2: decoding runs on bodyless responses and on failed partial responses

**Evidence:** [ClientSession.h](../../src/include/baselib/httpclient/ClientSession.h#L1181), `absorbResponse()` checks only status, decoding at 1218–1263, and the later exception/retry decision at 1399–1408.

Two missing preconditions matter as soon as real codecs are registered. First, a legal HEAD or 304 can carry Content-Encoding as representation metadata but no message body. The code still creates a decoder and calls `finish()` with no compressed stream; a validating gzip/Brotli decoder will report truncation. Second, a failed hop that received final headers is decoded before the original exception is considered. A truncated compressed body can replace the transport exception with a decoding error and prevent the intended retry decision.

**Correction:** Decide message-content eligibility and transport completeness before decoding/finalizing. Preserve the original failed-hop cause and apply the logical retry policy before transforming a failed partial body. Distinguish absent message content from a valid compressed representation of an empty body. Validate compressed HEAD/304 metadata and a lost connection after a compressed prefix. The failed-hop half was already recorded as owed work in L6; the empty-body half becomes a codec-adoption blocker.

### H26 — P2: HTTP CONNECT renders IPv6 origins without authority brackets

**Evidence:** [Uri.h](../../src/include/baselib/core/Uri.h#L754), host extraction at 763–768 and authority helper at 1045; [TcpTunnelStage.h](../../src/include/baselib/tasks/TcpTunnelStage.h#L621), CONNECT/Host construction at 631–640 and origin storage at 1528.

The URI parser correctly stores an IPv6 host without brackets. Tunnel negotiation concatenates that host, a colon, and the port directly, yielding e.g. `CONNECT ::1:443 HTTP/1.1` and the same invalid Host authority. Direct URI authority rendering already handles the brackets correctly.

**Correction:** Use a shared authority renderer for CONNECT and Host, while preserving the unbracketed address for DNS/IP verification. Validate IPv6 literals through CONNECT, including nondefault ports. This concerns the HTTP authority form, not SOCKS5 behavior. [RFC 9110 §9.3.6](https://www.rfc-editor.org/rfc/rfc9110.html#section-9.3.6).

### H27 — P3: merging caller and jar cookies treats cookie names as case-insensitive

**Evidence:** [ClientSession.h](../../src/include/baselib/httpclient/ClientSession.h#L479), merge at 501–508 and shared `contains()` helper at 710–718.

The helper used to detect caller overrides compares names with `equalsIgnoreCase()`, which is appropriate for header names and coding tokens but not cookie names. A caller's `sid=...` can suppress the jar's distinct `SID=...`, silently changing the request's authentication/session inputs.

**Intent and correction:** Preserve the deliberate caller-wins rule for exactly matching cookie names; use byte-sensitive comparison for cookie identity. Validate both differently cased names and an actual same-name override. [RFC 6265 §5.3](https://www.rfc-editor.org/rfc/rfc6265.html#section-5.3) defines replacement in terms of matching cookie names.

### H28 — P3: the public cookie jar's non-HTTP mode can overwrite an existing HttpOnly cookie

**Evidence:** [CookieJar.h](../../src/include/baselib/httpclient/CookieJar.h#L807), incoming-cookie check and replacement/deletion at 875–905; [L2 review](issues/http2-l2-review-record.md#L113).

`isHttpApi=false` rejects a new cookie carrying HttpOnly, but does not inspect the HttpOnly flag of an existing cookie with the same identity. An unflagged replacement or expiry deletion therefore succeeds. The protection must also cover the stored cookie. [RFC 6265 §5.3](https://www.rfc-editor.org/rfc/rfc6265.html#section-5.3).

**Scope and correction:** L6's session always uses the HTTP API, so this is not a newly reachable remote bypass through the session. It is an existing public-API contract gap, already acknowledged in L2. Either implement the stored-cookie check or explicitly remove/limit the unsupported non-HTTP mode. Validate overwrite and deletion through that mode, while retaining normal HTTP replacement behavior.

### H29 — P2, design assumption: omitting Accept-Encoding does not request identity-only content

**Evidence:** [ContentDecoder.h](../../src/include/baselib/httpclient/ContentDecoder.h#L395), default-registry commentary; [ClientSession.h](../../src/include/baselib/httpclient/ClientSession.h#L389), capability intersection; [decoder deferral](issues/http-content-decoders-deferral.md#L47), claims at 51–52 and 80–84.

The documents infer that omitting Accept-Encoding means servers send identity and that an unregistered coding was never advertised. Protocol negotiation does not establish that inference: an absent field permits any coding; an empty field expresses no desired coding. [RFC 9110 §12.5.3](https://www.rfc-editor.org/rfc/rfc9110.html#section-12.5.3). A conforming peer can therefore send an encoded representation to the no-decoder client, which returns those raw bytes successfully.

**Intent and correction:** Matching the legacy client's omission is a deliberate compatibility choice, but the guarantee of usable identity content is overstated. Define normal-mode behavior explicitly: request identity/no coding when that is required, or document and expose raw/unsupported content as a possible result. Keep strict fingerprint mode distinct. Validate a peer that selects an otherwise supported HTTP coding when the field is absent, plus empty-field and explicit-capability cases.

## Findings on the three compression/decompression proposals

The broad research note and the narrowed design have different scopes. The research explores compression and decompression; the narrowed design deliberately selects decoder-only, explicit opt-in, private vendored implementations. That reduction is sensible. The findings below apply to the narrowed design and plan where a research assumption was retained or an essential integration obligation was dropped. No embedded codec implementation is claimed to exist at the reviewed snapshot.

### C01 — P1: the prescribed headerization strategy requires a newer C++ standard than baselib supports

**Evidence:** [Research note](embedded-compression-design.md#L249), examples at 249/276–287 and `std::span` at 344; [narrowed design §5.2](embedded-decompression-design.md#L220), `inline constexpr` at 227; [toolchain definition](../../projects/make/toolchain/gcc-default.mk#L289), `-std=c++11`; [HTTP/2 design](http2-design.md#L7), explicit C++11 requirement.

The research bases ODR safety on C++17 inline variables, and the narrowed normative transform still prescribes `inline constexpr` file-scope tables. Those variables are not C++11. The research's `std::span`/`std::byte` sketches require newer library facilities as well. A fake template around the public facade does not change the language requirements of generated private free functions and tables.

**Impact and correction:** The approach as specified cannot meet the project's standard-mode contract; a successful probe in a compiler's newer default mode would be misleading. Select C++11-compatible storage, such as template static data or inline accessor functions containing constant local storage, using the library's existing idioms. Make the actual standard mode mandatory in every probe. A repository-wide standard upgrade is a separate decision, not an implicit codec prerequisite. Inline variables are a C++17 feature. [GCC language support](https://gcc.gnu.org/projects/cxx-status.html#cxx17).

### C02 — P1: output caps do not bound decoder working memory or the cost before output

**Evidence:** [Narrowed design §2.1](embedded-decompression-design.md#L50), especially 60–63; [plan S0.2](embedded-decompression-implementation-plan.md#L71), cap-ignorant decoder acceptance; [ContentDecoder.h](../../src/include/baselib/httpclient/ContentDecoder.h#L274), input call and output checks at 340–389.

The proposal concludes that an outer output/ratio limiter removes the need for an allocator or other resource policy inside the codec. The checks occur only when an already-created DataBlock reaches the output callback. They cannot constrain a frame's requested history window, internal tables, an oversized output allocation before callback invocation, or CPU consumed without producing output. The generic compression API would also be usable without the HTTP wrapper's caps.

Zstd is a concrete example: its streaming API separately exposes `ZSTD_d_windowLogMax`, and the inspected upstream version defaults that limit to a 128 MiB window. That is distinct from total emitted output. [Pinned upstream Zstd API](https://raw.githubusercontent.com/facebook/zstd/v1.5.7/lib/zstd.h). HTTP zstd must support windows through 8 MiB, while HTTP encoders must not require more; oversized inputs still need rejection. [RFC 9659 §3–4](https://www.rfc-editor.org/rfc/rfc9659.html#section-3).

**Correction:** Keep cumulative HTTP output/ratio limits at the wrapper boundary, but also define codec working-memory limits, bounded output chunk allocation, and cancellation/work-budget behavior. For HTTP zstd, explicitly configure the 8 MiB window boundary; account for additional workspace and concurrent decoders. Do not enable Brotli's extended large-window mode implicitly. Validate large-window/small-output streams, many concurrent instances, long low-output inputs, and output-block allocation before callback. This is a missing safety design, not a claim of a tested attack against shipped embedded code.

### C03 — P2: registering codecs does not make the current client an incremental decoding pipeline

**Evidence:** [Narrowed design §2.1](embedded-decompression-design.md#L50), streaming-seam assessment; [plan L0 gate](embedded-decompression-implementation-plan.md#L104) and [L4 adoption](embedded-decompression-implementation-plan.md#L288); [ClientSession.h](../../src/include/baselib/httpclient/ClientSession.h#L1210), entire `decodeBody()`.

The proposal treats the seam as nearly ready for codec substitution. The actual session decodes only after the encoded response has been fully accumulated and the hop has completed. It skips decoding entirely when `m_bodySink` is set. Registering a coding can still add it to a profile's normal-mode Accept-Encoding, so a streaming sink can receive encoded bytes even though the session has that decoder.

Buffered decoding also retains the encoded response, a growing decoded string, transient decoder output, and a final copied DataBlock during conversion. The original hop can retain the encoded body too. This contradicts the deferral record's claim that the body is never held twice. H09 explains the lock/deadline consequences; H06–H08 explain why attaching a codec to the present sink path is not sufficient either.

**Correction:** Add an explicit HTTP integration layer before adoption: encoded DATA to a bounded decoder pipeline, then either the accumulator or BodySink; finish the codec before logical success, outside queue locks, under the remaining deadline. Define ownership and backpressure in terms of compressed input and retained decoded output, rather than crediting HTTP/2 flow control using decoded byte counts. Validate both protocols, buffered and streamed responses, slow sinks, cancellation, and decoder failures. One successful compressed fetch does not establish this contract.

### C04 — P2: the deflate plan specifies raw framing where HTTP requires zlib framing

**Evidence:** [Plan S3.2](embedded-decompression-implementation-plan.md#L269), raw `InflateDecompressorT` at 277 and the gzip/raw compatibility question at 281–284.

HTTP `deflate` denotes zlib-wrapped DEFLATE. Raw DEFLATE is a compatibility exception for mislabeled responses, not the standard wire format. The plan supplies raw inflate and gzip framing, but does not specify the zlib-framed HTTP adapter; its note assigns the raw-deflate tolerance question to `GzipDecompressorT`. [RFC 9110 §8.4.1.2](https://www.rfc-editor.org/rfc/rfc9110.html#section-8.4.1.2).

**Correction:** Define separate raw-DEFLATE, zlib/HTTP-deflate, and gzip framing contracts, even if they share the same implementation. zlib already supports these modes through initialization parameters. [zlib manual](https://zlib.net/manual.html). Require gzip header/trailer and checksum validation; specify raw fallback only for the intended mislabeled coding, with a policy for deciding before unrepeatable output escapes. Validate externally generated wrapped deflate and gzip, optional raw compatibility, wrong wrappers, and checksum/trailer failures.

### C05 — P2: the generic streaming concept omits input-consumption and end-of-stream obligations

**Evidence:** [Narrowed facade design](embedded-decompression-design.md#L114), three-method concept; [plan S0.1/S0.2](embedded-decompression-implementation-plan.md#L57), interface and exception acceptance; codec validation slices in L1–L3.

`write()`/`finish()` can be an adequate public API, but only with a precise internal contract. Upstream decoders expose consumed input, need-more-input/output, and format end separately. The plan does not settle what happens to unconsumed suffixes, how a member/frame end differs from the HTTP body's end, or which trailing bytes are accepted. zlib's inflate API, for example, does not automatically decode concatenated gzip members in one uninterrupted invocation. [zlib manual](https://zlib.net/manual.html). Brotli also reports input remaining at format completion. [Pinned Brotli decoder API](https://raw.githubusercontent.com/google/brotli/v1.1.0/c/include/brotli/decode.h).

**Correction:** Specify that each `write()` consumes or safely retains all supplied input, with bounded retention. Define concatenated gzip members, zstd frames/skippable frames, forbidden trailing data, required dictionaries, checksum verification when present, truncated finish, valid compressed-empty streams, and terminal behavior after failure/finish. Preserve cumulative caps across members and coding chains. Translate codec-format errors without disguising a SecurityException, allocation failure, cancellation, or consumer exception as malformed input. Validate these cases across every input split boundary with independent fixtures; one corrupt and one truncated fixture is insufficient.

### C06 — P2: throwing from C allocation callbacks is not established to be exception safe

**Evidence:** [Narrowed design §5.5](embedded-decompression-design.md#L257), binding allocation to throwing library behavior; [plan S0.2](embedded-decompression-implementation-plan.md#L81), exception translation.

Compiling C source as C++ does not add RAII to its allocation and cleanup paths. Upstream code generally expects allocation callbacks to return failure and then performs explicit cleanup. For example, Brotli's creation path allocates state, calls initialization, and frees state on a false return; it does not contain an exception guard around that sequence. [Pinned Brotli creation implementation](https://raw.githubusercontent.com/google/brotli/v1.1.0/c/dec/decode.c).

**Risk:** If an allocator throws from a nested initialization path, ordinary C failure-return cleanup can be skipped before the facade owns the context. Similar questions apply to any exception allowed through upstream processing callbacks. This is an exception-safety obligation missing from the proposal, not a demonstrated leak in a chosen generated version; the exact selected upstream allocation paths must be audited.

**Correction:** Prefer nonthrowing C callbacks that return null and record the allocation failure, then translate the result after control returns to the C++ wrapper. Establish ownership immediately and prohibit unintended exception escape through upstream callbacks. Fault-inject each allocation position and consumer callback; verify cleanup and the original error category. Keep these bindings small and idiomatic instead of rewriting upstream internals broadly.

### C07 — P2: namespace-plus-inline is not a complete linkage, amalgamation, or mixed-TU ODR strategy

**Evidence:** [Research note](embedded-compression-design.md#L218), transformation strategy; [narrowed design §5.2/§5.6](embedded-decompression-design.md#L220), namespace/inline conversion and preserved architecture conditionals; [plan multi-TU proof](embedded-decompression-implementation-plan.md#L178).

Upstream Brotli and Zstd contain `extern "C"` declarations/definitions. Wrapping those in a private C++ namespace does not provide the intended external C-symbol isolation. Concatenating original translation units also merges formerly separate file-local helper names. Moving system includes inside that namespace creates a separate class of type/declaration problems. [Pinned Brotli implementation](https://raw.githubusercontent.com/google/brotli/v1.1.0/c/dec/decode.c), [pinned Zstd header](https://raw.githubusercontent.com/facebook/zstd/v1.5.7/lib/zstd.h).

The preserved `#ifdef` paths introduce another obligation: two translation units can include the same versioned inline implementation under different target features, debug/configuration macros, or upstream feature switches. Successful linking and shared symbol addresses do not prove that their definitions or internal layouts satisfy the ODR. A version namespace alone does not distinguish configurations. Compile-time selection can also allow instructions inappropriate for the runtime CPU if feature selection is not controlled.

**Correction:** Specify C-linkage removal/renaming, matching declaration transformations, per-original-TU helper disambiguation, external include placement, canonical private configuration, and CPU-dispatch rules. Either ensure identical cross-TU definitions or deliberately distinguish supported configurations. Validate mixed target flags/include orders, real external codec headers and libraries in the same program, debug/release macro differences, and multiple vendored versions. Check LTO diagnostics and runtime behavior as well as symbols. Final-link deduplication is not evidence that intermediate object-size limits are satisfied.

### C08 — P2: the proposed macro-isolation probe cannot detect clobbered consumer macros

**Evidence:** [Narrowed design §5.4](embedded-decompression-design.md#L250), probe at 255; [plan S1.2](embedded-decompression-implementation-plan.md#L154), equivalent acceptance.

The probe defines `MIN`, `MAX`, and `ERROR` after including the codec and checks that they survive. That can pass even if the codec has already undefined or overwritten the consumer's earlier definitions. It also does not establish isolation of upstream include guards/configuration macros, which can silently suppress an external header in the same translation unit.

**Correction:** Define representative consumer macros before inclusion, include the embedded codec, and verify unchanged expansion afterward; check object-like and function-like forms. Inspect the macro set before/after and test upstream headers both before and after the embedded header. Prefix private macros and guards consistently, and balance warning pragmas. Do not treat trailing `#undef` of unprefixed upstream names as sufficient isolation. This strengthens the proposed probe without changing the intended public API.

### C09 — P2: the go/no-go gate does not establish the safety and portability claims attached to it

**Evidence:** [Narrowed verification design](embedded-decompression-design.md#L273), proof claims; [plan S1.5/S1.6/G-E1](embedded-decompression-implementation-plan.md#L192), dependencies and criteria at 227–237; [test-module rules](../../src/utests/AGENTS.md).

G-E1 can follow S1.6, whose dependency is S1.3, without S1.5's fuzzing acceptance being an explicit prerequisite or gate criterion. Its compiler criterion lists gcc and clang but omits the design's MSVC obligation. Object measurements focus on x86 debug, which is necessary for the enforced size ceiling, but do not establish x86 release compiler-memory feasibility. Golden vectors and a linking two-TU probe cannot prove transformed behavior or ODR correctness generally.

The proposed fuzzer's positive control is useful, but demonstrates that the harness can find that deliberate bug, not that the transformed decoder preserves upstream behavior. A generic corruption test must also avoid assuming every changed compressed byte is invalid: checksum-free formats can legitimately decode altered input to different output.

**Correction:** Make actual instrumented fuzzing, upstream differential comparison, allocation/error-path checks, and the strengthened multi-TU/configuration probes prerequisites of the decision gate. Record compiler/standard/architecture/variant coverage, including MSVC/clang-cl where supported and the x86 release compilation constraint. Check decompressed output, completion/error classification, and input consumption against the pinned reference. Keep tests in appropriately sized modules or a deliberate helper/probe target; avoid expanding already-large single-TU modules. No such validation was run during this review.

### C10 — P2: adoption criteria overstate what registration and a single fetch close

**Evidence:** [Plan S0.3](embedded-decompression-implementation-plan.md#L89), assertion at 100; [plan S4.1/S4.2](embedded-decompression-implementation-plan.md#L292), adoption/deferral closure; [ClientSession.h](../../src/include/baselib/httpclient/ClientSession.h#L389), profile-coding intersection; [narrowed design open questions](embedded-decompression-design.md#L372).

Registration does not by itself add a coding to an arbitrary session's Accept-Encoding. The actual value is the profile's requested list intersected with the registered list; an empty profile list still produces no coding advertisement. That is consistent with explicit opt-in, but the acceptance statement omits the precondition. Closing decoder availability also does not close streaming decoding, coding-chain semantics, resource safety, or every browser fingerprint deviation.

**Correction:** Provide an explicit opt-in registration/configuration example that establishes both available decoders and the intended profile coding list. Gate closure of each coding on C02–C05 and H24–H25, across h1/h2 and buffered/streamed modes, with exact body and metadata assertions. Keep certificate compression and L7's measured TLS/browser fidelity separate. Define whether the deferral is closed for opted-in users only; do not silently register heavy codecs through the normal session include path. E5's repository-versus-dist decision must be settled before vendoring, as the proposal already states.

### C11 — P3: the generated timestamp conflicts with byte-for-byte reproducibility unless defined

**Evidence:** [Plan S1.1](embedded-decompression-implementation-plan.md#L123), generated banner includes a date; [G-E1](embedded-decompression-implementation-plan.md#L237), deterministic generation; research reproducibility and notice requirements around lines 1344–1401.

A banner containing the wall-clock generation date makes identical source, manifest, and generator inputs produce different bytes on different days. The plan does not specify the timestamp source. The broad research's provenance and notice-preservation checks also need to survive the transition into the concrete generator acceptance criteria.

**Correction:** Use a fixed source/manifest timestamp or a reproducible epoch, and include it among the declared inputs. Pin generator/transformation versions as well as upstream tag/hash. Require reproducibility across separate runs and environments, preserved upstream notices, and reviewable transformation/patch deltas. Generated output can be excluded from ordinary hand editing without excluding its security-relevant transformation from review.

## Design decisions, prior fixes, and remaining boundaries

The following points affect interpretation of the findings. They should not be counted again as newly discovered implementation defects.

| Decision or prior issue | Current assessment |
|---|---|
| Sans-I/O engine and separate drivers | Sound architectural separation. The important failures above occur where state, lifetimes, and completion cross the boundaries; replacing the protocol engine wholesale is not the indicated remedy. |
| Strand-bound stream policies and MultiOperationTask | Appropriate for simultaneous read/write/timer operations and consistent with baselib's task model. Whole-task operation accounting does not by itself make an individual HTTP/1 request's buffers safe to release (H01). |
| Atomic TaskBase state | The current task state is atomic. An allegation that `getState()` itself is an ordinary unsynchronized field read would be incorrect. H04 concerns different fields and use before the publication barrier. |
| External cancellation of connection timers | The strand-posted cancellation fixes were present in the reviewed drivers. H03 concerns the separate pool maintenance timer and deferred-action lifecycle. |
| Closed state and capacity publication | The reviewed h2 closing paths contain the state/slot publication fixes from the follow-up work. The earlier stale-Ready outcome concern should not be re-reported as if that reorder were absent. |
| One scheme per session | The session rejects incompatible initial URLs and refuses cross-scheme redirects. The previously identified cleartext request to a TLS port, including Secure cookies, is fixed. This deliberately limits automatic redirects across http/https. |
| Logical network deadline | The session carries a remaining chain budget across attempts/hops. The old full-timeout-per-hop finding is fixed for network work. H09/C03 concern transformation after hop timers have stopped. |
| h1 idle lifetime and establishment causes | The session propagates h1 idle configuration and preserves establishment exceptions through the request failure path. These earlier omissions were addressed; H21's use of a retry for fallback remains. |
| Default idempotent-on-connection-loss retry | Still opt-in. H02 does not require this option: it incorrectly asserts the stronger safe-to-retry flag. H08's visible-prefix replay requires the option or another retry after visible output. |
| Pool's initial-capacity sentinel | Conservatively reserving one slot until peer capacity is known is intentional. The special small-limit underfill and bounded maintenance polling are documented tradeoffs. H18 is about accepting an invalid preface, not a request to wait an extra round trip before opening a legal rider. |
| BodySource that returns empty and nonfinal | The missing upload readiness path is an acknowledged deferral. H06 is the distinct response-sink readiness/completion problem, for which the public contract already promises partial consumption. |
| Streamed redirects and streamed h1 uploads | The session refuses redirects with a BodySink; streaming uploads are restricted to h2. Those explicit supported-surface limits are not accidentally missing branches. Retry/sink semantics still need H08. |
| Coalescing, push, resumption, h2c Upgrade, production h2 server | Coalescing is disabled, push is rejected, resumption is deferred, h2c Upgrade is absent, and the server role is test support. These are documented choices. They limit the broad phrase “full HTTP/2 support” but are not missing L6 work items. |
| TLS verification and negotiated floor | The inspected path applies hostname/IP verification and the negotiated TLS floor before HTTP traffic; per-profile contexts avoid mutating the shared default context. No default certificate-verification bypass was established. The pre-existing explicit untrusted-certificate opt-in remains a separate caller policy. |
| OpenSSL 1.1.1w | The design already records that the required dist flavor is unavailable. Compatibility is an outstanding evidence obligation, not a result established by this review. |
| Public suffix and cookie behavior | Lack of a public suffix list is an accepted security residual: unrelated sites under a suffix such as `co.uk` can share an incorrectly accepted domain-cookie scope. One-scheme sessions do not eliminate that cross-site risk. The jar should not be represented as providing complete browser cookie isolation. Host-only identity including its flag is deliberate; Secure-cookie acceptance follows the recorded policy. H27/H28 are separate issues. |
| Header-only and dependency minimalism | Generic URI/header types, isolated Beast use, opt-in codecs, and the fake-template idiom fit baselib. H19 and C01 identify actual violations of the promised boundaries; neither requires abandoning those principles. |
| L7/L8 features | Built-in browser profiles, final fidelity grading/capture gates, and the compatibility facade are later-layer work. Their absence is not evidence that completed L6 slices failed their assigned scope. Existing contracts needed by them are reviewed above. |

### Browser impersonation and the decompression decision

The original design usefully separates TLS, HTTP/2 settings/priority/HPACK, and HTTP header fingerprints, and requires explicit fidelity grades and deviations. Keep that separation in the acceptance criteria: decoder availability fixes a content-coding capability gap; it does not prove a browser-equivalent ClientHello or server-observed behavior.

For the selected OpenSSL integration, the narrowed design is right to keep HTTP content decompression separate from TLS certificate compression. Adding header-only HTTP decoders does not wire them into OpenSSL's handshake machinery. Advertising an active extension that the handshake cannot process is unsafe: a peer can legitimately use the advertised feature. The prohibition on faking certificate compression should remain, and any ALPS/custom-extension work still needs the original spike's semantic validation, not just a desired extension-type hash. See [narrowed design §9](embedded-decompression-design.md#L326) and [HTTP/2 design §6.3](http2-design.md#L1460).

Avoid closing the overall impersonation gap based on one decoder, one JA3/JA4 value, or one successful site request. The pending L7 capture/interop work must measure the actual supported profile and record remaining deviations. The research note's broad source-vendoring approach and the narrowed Brotli-first feasibility gate are reasonable experiments once C01/C02 and the probe limitations are corrected. “Feasible” is currently a design hypothesis with unresolved compiler, ODR, resource, and adoption obligations.

## Recommended sequence to close the decompression gap

These are recommendations for subsequent authorized implementation, not changes made during this review.

| Order | Concrete work | Completion evidence |
|---|---|---|
| 1 | Resolve H01–H06 and H09: write ownership, replay proof, pool lifecycle/publication, interim bounds, sink progress, and callback execution context | Controlled interleavings and observable body/side-effect assertions; memory/race instrumentation appropriate to each change |
| 2 | Repair protocol boundaries H10–H18 and the request/header transformations used by decoding and redirects | An independent peer validates SETTINGS ordering, HPACK state, frame boundaries, bodyless messages, and receive-credit progress |
| 3 | Rewrite the decoder contract in C01/C02/C04/C05 terms and resolve E5 | C++11 storage/linkage design, HTTP framing table, input/finish/error contract, per-codec workspace/chunk limits, update/packaging ownership |
| 4 | Introduce the real incremental HTTP transform path from C03; settle H07/H08/H24/H25 | Both h1/h2 and buffered/streamed paths preserve complete bytes, metadata, errors, deadlines, and bounded backpressure |
| 5 | Perform the proposed single-codec feasibility spike under strengthened C06–C09 gates | Reproducible source transformation, upstream differential fixtures/fuzzing, allocation-failure cleanup, mixed-TU isolation, supported compiler/architecture/variant evidence, object/compile-memory measurements |
| 6 | Add further codecs and explicit adoption from C10 | Correct HTTP `gzip`, zlib-wrapped `deflate`, `br`, and `zstd` behavior for opted-in configurations; truthful Accept-Encoding and strict/raw-mode outcomes |
| 7 | Close only the verified portions of the deferral and continue L7/L8 | Decoder availability and integration recorded separately from certificate compression, full browser fidelity, and compatibility-facade obligations |

## Validation gaps that matter to the review conclusion

Static tracing is enough to identify the contradictory predicates, lifetime releases, and absent synchronization above. It does not establish their frequency or all platform manifestations. The future checks should target contracts and interleavings rather than repeat assertions that mirror implementation choices.

- The streaming test cited in H06 must check every delivered byte and terminal ordering. Its current successful-prefix assertion is evidence of a missing requirement, not protection against truncation.
- The fallback test cited in H21 currently asserts failure with retries disabled. Retain it as a historical explanation if useful, but invert its expected behavior when protocol handoff is corrected.
- Drive real concurrent requests through session, pool, and driver together, including a withheld first response, peer SETTINGS changes, cancellation, GOAWAY, and h1 fallback. Stub-only slot arithmetic does not establish end-to-end multiplexing or safe publication.
- Add deterministic pauses at the exact write/dispose/publication/continuation boundaries described above. Race-detector silence without exercising those boundaries is weaker evidence than a controlled schedule followed by instrumentation.
- Compare malformed-wire handling and HPACK evolution with an independent implementation. Client and test peer sharing the same engine can agree on the same mistake.
- Before shipping embedded decoders, cover working memory, compressed/decoded buffering, finish/checksum/trailing-input behavior, coding chains, and slow-sink progress. Output-size and expansion-ratio tests alone leave the resource model unverified.
- Preserve the project's focused-module and build-serialization rules when future validation is authorized. Nothing in this report authorizes a full build, test run, source repair, or commit in the present task.

All forty numbered entries above are review findings or explicitly labeled design/proposal concerns: twenty-nine concern the current HTTP client/design and eleven concern the compression proposals. Seven current implementation findings are P1 (H01–H06 and H09); two proposal findings are blocking design prerequisites (C01/C02). The documented deferrals and already-fixed issues in the reconciliation table are not included in that count.
