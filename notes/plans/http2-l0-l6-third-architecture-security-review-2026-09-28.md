# HTTP/2 L0–L6: third architecture, correctness, and security review

**Date:** 2026-09-28

**Reviewed production HEAD:** `93e2d90b12747e01ddc6c444aa6312eb4746468d` on `lazari2`.

**Final repository HEAD:** `b4d36dfa3c02649199b7616551d677344a8f055e`. The pull during this review changed only `scripts/devenv7/AGENTS.md`, concerning the Windows Python environment. It changed no reviewed production code. Its instructions were read.

**Comparison:** `db97372598afe595cf6c253920841774aad18e9f..93e2d90b12747e01ddc6c444aa6312eb4746468d`.

**Previous reports:** [September 26 remediation review](http2-l0-l6-remediation-review-2026-09-26.md) and [September 21 architecture/security and decompression review](http2-l0-l6-architecture-security-review-2026-09-21.md).

## Assessment and scope

The runtime corrections requested in R01–R07 are present and address their reported triggers. R08's mistaken deferral premise has also been addressed in code: an application-provided decoder now encounters the new eligibility checks, independently of whether baselib ships any codec. The additional pool, request-completion, allocation, and TCP/TLS changes fix substantive problems beyond those findings.

This review records **one P2 public-API concurrency finding and one P3 documentation finding**. T01 is newly identified but demonstrably pre-existing at the comparison base; it is not a regression introduced by CS-1–CS-6. T02 is recurring status drift in the category of R09. The specific older ledger entries were corrected, but the canonical security summary and decoder deferral still describe some landed changes as pending.

**No new P1 or confirmed newly introduced runtime regression was established in this review.** That is a scoped static-review conclusion, not an unconditional security or portability sign-off. The accepted cancellation and continuation-lock limits remain reachable, and the current Windows and older-Boost obligations remain outstanding in the committed records.

The production diff contains 18 headers. I traced their changed behavior through the request mailbox, pool, both connection drivers, HTTP/2 session, connection/tunnel establishment, task/queue completion, stream policies, and affected legacy HTTP/server/block-transfer consumers. I also examined relevant test sources, design notes, commit history, and the consolidated owed list. This is not a claim to have re-audited every unchanged codec, unrelated application, or line of Boost/OpenSSL.

**Execution and changes:** no builds, tests, executable probes, fuzzers, or benchmarks were run. No production code, tests, or existing documents were edited. This new, uncommitted report is the only file created by the review. Historical test results below are attributed to their records; they were not reproduced or independently certified here.

File and line references refer to the reviewed production snapshot; they are unchanged at the final repository HEAD. P2 denotes a material correctness/concurrency or API-integration problem; P3 denotes a narrower maintenance/status problem. The interleaving in T01 is source-derived, not a claimed TSan reproduction.

## Findings

### T01 — P2: status getters on a completed single-hop request can race a late Closed event

**Provenance:** newly identified, pre-existing. Both the late writes and the unlocked getters exist at `db973725`. CS-4's I8 correctly guards late response/header/trailer writes; this is an adjacent remaining publication issue, not a claim that its response guards failed.

**Location:** [HttpClientRequestTask.h](../../src/include/baselib/httpclient/HttpClientRequestTask.h), fields at 419–420; `applyEvents()` at 654–729; `applyClosed()` at 1553–1570; `answerOnClosed()` at 1679–1689; `isRetryable()/outcome()` at 2403–2410. Supporting dependencies: [CPP.h](../../src/include/baselib/core/CPP.h), `ScalarTypeIniter` at 512–550; [TaskBase.h](../../src/include/baselib/tasks/TaskBase.h), completion publication at 724–730; [ExecutionQueueImpl.h](../../src/include/baselib/tasks/ExecutionQueueImpl.h), `onReady()` at 470–544.

**Affected use:** a direct user of the public `HttpClientRequestTaskImpl` reads `isRetryable()` or `outcome()` after waiting for the task to complete, while the driver has not yet delivered its final close. This does **not** require reading an actively running task.

The task deliberately answers its caller before stream cleanup finishes when a timeout, cancellation, or another request-side failure wins. `applyStopped()` queues cancellation and calls `failWith()`; `applyEvents()` then sets `m_isCompleted` and invokes `notifyReady()`. The task remains reachable until the driver's `Closed` event returns its slot. `onDrain()` continues to process later mailbox batches after task completion.

The late close still executes:

- `m_isRetryable = event.isRetryable` at 1564;
- `m_outcome = outcomeOnClosed(event)` at 1570.

The early guard in `applyClosed()` checks only whether a previous close was applied. The completed/pending guard is in `answerOnClosed()`, after these writes and the slot-release decision. Both fields are ordinary `ScalarTypeIniter` storage, not atomics. Their public getters take no lock.

A permitted sequence is:

| Step | Request drain / driver | Application thread |
|---|---|---|
| 1 | Apply a timeout or cancel; request stream cancellation. | Wait for task completion through the execution queue. |
| 2 | Publish completion through `notifyReady()`; the stream's close is still pending. | Completion wait returns; retain the request implementation object. |
| 3 | Apply a later `Closed` batch and write both status fields under the task mutex. | Read either getter without that mutex. |

Completion orders writes made **before** publication. It does not order a later close-handler write against a caller's subsequent read. The task mutex on the writer alone does not establish that missing synchronization. This is a C++ data race even when a particular close writes the same value already stored.

**Important scope qualification:** the built-in `ClientSession` retry decision is safe from this particular interleaving. `notifyReady()` calls the queue synchronously; the queue calls `continuationTask()` before returning; and the request drain cannot apply its next batch until that call unwinds. The new `isOwnFailure()` refusal also correctly prevents a request-side failure being replayed merely because a close behind it reports a connection failure. No duplicate replay in that path is alleged.

The comment at 2424–2428 explicitly identifies reading from the completed hop's synchronous `continuationTask()` as safe. That narrow statement holds. It does not make a general read after queue completion safe, although these methods are public and grouped under “What the caller reads afterwards.” If continuation-only access is the intended exclusive API contract, this finding becomes a restriction that must be made explicit and enforced at that boundary; task completion by itself is insufficient.

Likewise, a late `outcome()` changing is not by itself proof that the timeout was replaced with a successful request result. The code intentionally distinguishes the request's winning exception from the connection's subsequent outcome. The defect here is unsynchronized publication, not that distinction.

**Existing coverage and why it misses this:** [TestHttpClientRequestTaskAfterTheFailure.h](../../src/utests/utf_baselib_httpclient/TestHttpClientRequestTaskAfterTheFailure.h), `requireLateBlocksChangeNothing()` at 92–191, explicitly completes a failed request and only afterwards delivers headers, trailers, and close. It proves the lifetime arrangement needed above and checks the corrected immutable response. It waits for pool release before inspecting the response and does not concurrently read the two status getters.

**Decision shape and recommendation:**

- **What needs deciding:** whether public status accessors expose synchronized live connection metadata or an immutable snapshot of request completion.
- **If unchanged:** a direct caller can obey ordinary “wait, then inspect” task usage and still race internal cleanup. Its wait has not established safety for these two fields.
- **Risk, complexity, and reach:** the change can stay in the request task and affects direct users over both HTTP/1 and HTTP/2. It need not modify the frozen stream interfaces or shared `TaskBase`. Freezing values requires care because pool reuse still needs the actual late connection outcome.
- **Recommendation:** preserve the currently documented late-connection meaning and synchronize these getter reads with their writes, documenting that the values may change during cleanup. Use a single protected snapshot if callers require the pair to be mutually consistent. Reverse that recommendation if the intended API promises all result metadata is immutable at task completion; in that case publish a completion snapshot and keep cleanup metadata separately.
- **Do not fix this by dropping late `Closed` events.** Their slot release and connection cleanup are necessary.

**Future verification, not run here:** withhold a stub connection's close, finish a request by timeout/cancel, and inspect the public metadata from a consumer while permitting the delayed close to drain. A focused TSan check should cover the accessor reads, while deterministic assertions verify the chosen live/snapshot semantics and exactly-once slot release. Merely checking that the final value is correct after pool release would miss the race.

### T02 — P3: current security/status records still say landed changes are unimplemented

**Provenance:** residual/recurring documentation drift in R09's category; no runtime regression. The original row-level corrections should retain credit.

**Locations and contradictions:**

| Current text | Source and lines | Actual source/commit state |
|---|---|---|
| HTTP/1 outstanding body accumulation is “decided, not yet fixed.” | [http2-design.md](http2-design.md), security considerations, 1625–1631 | `9028bff`, merged in CS-2 at `bc431f2`, implements the request ingress cap. |
| R01 and R03 are “not yet implemented” and “Both hold until CS-1 lands.” | Same document, 1643–1647 | CS-1 merged at `a04f29c`; `130e021` and `ea47826` implement those fixes. |
| All three decoder prerequisites are “live now,” followed by decision language about P2/P3. | Same document, 1634–1642 | P1/H09 remains; the minimal P2/P3 protections landed in `0f3b0ba`, with the 205 guard in `c02f7b4`. |
| The revisited P2/P3 decision is “not yet implemented.” | [http-content-decoders-deferral.md](issues/http-content-decoders-deferral.md), 242–247 | The implementation and custom-decoder tests have landed. |
| R09 is marked fixed, without these remaining stale current-status statements being reconciled. | [astra-remediation-owed-work.md](issues/astra-remediation-owed-work.md), row R09 at 63 | The R01–R08 rows themselves now record their landed commits accurately; these other authoritative summaries lag. |

This matters because a reader using the design's security summary is told the client still accepts truncated close-delimited TLS responses and retains an unbounded HTTP/1 request backlog. Neither describes the reviewed client path. Another reader using the ledger is told the status cleanup is complete. Repeatedly reconciling those sources costs review effort and can direct later work at already-fixed behavior.

**Intent and correction:** preserve dated decisions as history, but update the current security summary and the revisited decoder disposition to say what landed, at which commits, and what remains. P1/H09 and full multi-layer decoding remain separate matters. This is a text correction to decisions already made, not a reason to reopen those decisions or to change code.

**Verification:** reconcile the four statements above with the R01–R08 rows and CS-1/CS-2/CS-4 implementation. No build or test is relevant to this correction. No existing record was edited in this review.

## Verification of the previous review

“Fixed” below means the source addresses the reported trigger, including the chosen narrower scope where one was explicit. It does not mean every nearby architectural deferral disappeared.

| Previous finding | Current determination | Source-based verification |
|---|---|---|
| **R01 — TLS truncation falsely completes a close-delimited HTTP/1 body** | **Fixed** by `130e021`; CS-1 `a04f29c`. | `Http1ConnectionTask::isCleanEndOfStream()` at 1242 accepts the centralized clean-EOF predicate alone. The broader peer-ended/truncation classification still reaches cleanup, but `onPeerClosed()` refuses `parseEof()` for truncation. Independently complete Content-Length/chunked messages remain distinct. Local cancel/closing and pending-write verdict guards remain. |
| **R02 — unbounded HTTP/1 outstanding response data** | **Fixed at the request/session boundary** by `9028bff`; CS-2 `bc431f2`. | The new 64 MiB `maxOutstandingResponseBodySize` charges payload plus a per-block allowance before enqueue, under the mailbox mutex. The crossing block becomes one overflow marker; subsequent data is dropped by the latch. Append/consume/drop releases the charge. Overflow fails explicitly and cancels the stream. This is an outstanding-data limit, not a total streamed-transfer limit. |
| **R03 — first TLS read can race first request write** | **Fixed** by `ea47826`; CS-1. | `scheduleTask()` at 2183 accounts and posts one startup handler. `onStartConnection()` at 2251 runs on the stream executor, starts the read, and only then publishes `m_started`. It handles a pre-start cancellation that the per-run closing reset would otherwise erase. Read-initiation failure retains an outstanding startup operation, avoiding inline terminal notification under the task lock. |
| **R04 — delivered bytes lost when a later sink call throws** | **Fixed** by `b1adabf`; strengthened by CS-4's `535d830`. | Each successful sink consumption updates `m_sinkDelivered` before another callback. A throw latches `m_hasSinkThrown`; later delivery and replay refuse that sink. Session retry refusal occurs before body-source rewind. Own-failure attribution additionally refuses caps/timeouts/source failures when those supplied the winning exception. |
| **R05 — incomplete TE/Connection normalization** | **Fixed** by `e0cc4a4` and `fced53a`; CS-3 `f48acd2`. | The HTTP/2 boundary collects tokens from every Connection field before removing anything, removes nominated fields, and handles TE and Host through their own rules. Every TE field/list contributes to a single canonical `te: trailers`, or no TE. Keeping `Connection: TE` from suppressing the legal trailers declaration and retaining Host for validation are deliberate refinements. |
| **R06 — malformed/incorrect Host authority comparison** | **Fixed** by `e3e431d`; CS-3. | `tryParseHostField()` at 3130 reuses `net::Uri`, rejects userinfo/path/query/fragment, and checks all Host occurrences. Matching uses the parsed host, IP-literal distinction, and effective scheme port. A portless Host does not borrow the URL's explicit nondefault port; malformed and overflowing ports no longer silently normalize to it. |
| **R07 — case-insensitive HEAD classification** | **Fixed** by `164a195`; CS-3. | `Session::isHeadMethod()` matches the exact method token `HEAD`. The new decoder eligibility rule uses the same case-sensitive treatment. A distinct extension method `head` is not treated as HEAD. |
| **R08 — public registry makes decoder integration defects reachable** | **Premise corrected and minimal runtime fix landed** in `0f3b0ba`; 205 added by `c02f7b4`. | `tryGetTheOnlyCoding()` at 1307 counts nonempty list elements across all Content-Encoding fields. `decodeBody()` at 1380 requires a successful, content-bearing, buffered, non-strict response and exactly one registered coding. Layered/unknown encodings remain raw with their metadata intact. HEAD, 204, 205, and 304 do not invoke a decoder. |
| **R09 — stale status records** | **Original entries corrected; current summaries still need reconciliation.** | The ledger records the real H21 narrowing and R01–R08 commits. T02 identifies the remaining/recurrent contradictory statements rather than alleging that every previous record correction was omitted. |

The strict R01 result is consistent with [RFC 9112 §9.8](https://www.rfc-editor.org/rfc/rfc9112.html#section-9.8): an otherwise close-delimited TLS message needs the valid closure alert to establish completeness. This does not require rejecting a message already complete by its own framing.

Two limits to the R02 closure deserve explicit treatment:

1. The default standalone `Http1ResponseParser` remains uncapped by N1's deliberate choice. The new cap belongs to `HttpClientRequestTask`; a direct parser or driver consumer must supply its own resource policy.
2. Charged bytes plus a per-block allowance bound the request's outstanding backlog. They are not a promise that process RSS equals that number: parser/read buffers, temporary copies, and retained oversized blocks from a caller-provided pool are separate. I found no renewed unbounded request backlog in the normal driver path.

## Additional changes: architecture and cross-file reasoning

### Request mailbox, failure precedence, and pool lifetime

The ingress cap is placed before the mailbox rather than only in `applyData()`. That is the critical property for a blocked sink or delayed drain: memory admission does not depend on the consumer making progress. The mailbox mutex remains a leaf, and disposal of the overflow block happens after releasing it. Charging block overhead also prevents a stream of very small payloads from defeating a payload-only budget.

The drain continues to serialize its apply/deferred/completion phases. Caller BodySink and BodySource operations remain in the deferred phase, each action is guarded, and one throw does not prevent a later pool-slot release. This fits the library's existing task/queue lock discipline.

The own-failure flag in `failWith()` is committed with the exception that wins, not simply set whenever some later callback throws. This preserves the explicit first-failure policy. CS-4 correctly adds completion guards to headers, interim responses, trailers, data, and upload pulls. T01 is limited to the still-live connection-status fields.

The two pool changes complement one another:

- `refreshEntry()` reports a completed establishment failure only when that arm actually retires the entry. Re-examining a retired entry whose rider still holds a slot no longer spends queued requests' retry budgets repeatedly.
- `releaseStream(ConnectionUnusable)` marks an unreported failure only for a never-usable entry. The next refresh consumes that mark once. The already-usable witnesses prevent a connection that served traffic being reclassified as a failed establishment.

I traced the changes through `examineKey()`, driver adoption, retired-entry retention, replacement admission, the queued/dispatched retry split, and release after a failed rider. They preserve the chosen separation of pool and session budgets. They do not merge those budgets or resolve the separately accepted H23 behavior.

The cancel-with-reason callback is additive. The pool collects its reason under its lock and invokes it outside that lock, ahead of the corresponding cancel. The default factory binds it to the held connector. The connector's own deadline uses the same first-reason mechanism, and the original connection error remains in the exception chain.

The record's known edge remains: a connection can fail naturally and still be finishing TLS shutdown when an establishment bound later cancels it. In that case the timeout may describe subsequent abandonment rather than the original transport failure. The preserved original exception is why this is not treated as a new wrong-cause finding here. See [the CS-4 account](issues/astra-second-review-decisions.md), lines 900–911.

### HTTP/2 header boundary and allocation

The normalization stays at the protocol-neutral-to-HTTP/2 boundary, rather than weakening `HeaderList` or altering the h2 engine to accept invalid pseudo-header data. This preserves the design's ordered headers and profile behavior while correcting wire legality.

The Host fix uses the existing URI authority parser, which avoids another independent port parser with different overflow, IPv6, and default-port rules. Query and fragment checks use presence flags, so even an empty query/fragment delimiter is rejected. Duplicate Host fields remain an error instead of allowing the first to conceal a conflict.

The DATA allocation changes `411f8d0` and `6cbefba` address two distinct paths. `blockOf()` at 1086 requests payload-sized fresh storage; zero-length DATA does not request a zero-capacity block that would select the DataBlock default. If a recycled block is too small, replacement goes through `DataBlock::get()`, which supplies a writable block with size reset before append. It does not append to a constructor-created block whose initial size equals capacity.

This removes the previous default fresh-allocation amplification and preserves payload bytes and flow-control accounting. A supplied pool can still return larger existing blocks; the fix does not claim to resize caller-owned pooled capacity.

### HTTP/1 startup, cancellation, and message completeness

The startup handler reconciles three constraints that a mere reorder in `scheduleTask()` would not:

- the initial TLS read and request writes must be serialized on the stream executor;
- cancellation submitted before scheduling must survive the task's per-run state reset;
- a failed read initiation must not complete inline under the task/queue locks.

The new operation accounting and startup checks address all three. Public submit still owns the single-request slot under the driver's state mutex, and posted work rechecks closing/request state.

The source and the new startup barrier tests distinguish operation **initiation** from completion serialization. That distinction matters because an associated strand cannot serialize a direct SSL initiating call made concurrently elsewhere. It is consistent with [Boost.Asio's shared SSL-stream requirements](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference/ssl__stream.html).

The ending classifier continues to distinguish “peer ended,” “clean enough to retry an unused connection,” and “clean enough to complete a close-delimited response.” Those questions should remain separate. The new R01 rule does not erase the existing write/read barrier or make platform-specific reset/abort codes equivalent to authenticated TLS closure.

### Shared TCP connector and proxy path

`TcpConnectionEstablisherT` now owns its per-endpoint connect loop. The socket close/reopen transition occurs in its locked task handler, with the cancellation check before another attempt. For stranded policies the handler also uses the stream's strand. This fixes both the reported socket-state race and continuing into another address after cancellation.

The direct resolver path and `TcpTunnelStage::continueAfterResolved()` both call the same `beginConnect()`; the proxy path did not retain the old ranged `asio::async_connect()`. I checked endpoint order, empty-list asynchronous completion, successful-endpoint propagation, last-error behavior, task-reference capture, cancellation before another attempt, and the version-selected empty-list post.

One departure from Asio is intentional and documented: a socket-open failure is treated as that attempt's error, allowing the next endpoint or returning the real last error. It no longer becomes an unexplained `operation_aborted`. The forced-open-failure test is POSIX-only; Windows coverage is not implied by it.

This is core infrastructure, also used by legacy HTTP and messaging/transfer consumers. Its portability reach is substantially broader than the h2 client even though the helper is small. The uncompiled pre-1.72 branch and the outstanding Windows connect-loop matrix therefore remain material qualifications.

### TLS teardown, cancellation, and task deadlines

Removing the forced-cancel `set_option(linger)` eliminates the reported write to Asio's socket state during concurrent I/O initiation on a plain policy. The remaining send shutdown/cancel path preserves a linger setting deliberately supplied by a socket owner. I found no justification to reintroduce that write; the race fix should not be mistaken for a new policy of forcing abortive close.

The TLS own-handshake flag limits receive-side shutdown to a handshake initiated by that task. It is cleared on successful handshake and on stream reset/attach/detach. An attached, already-negotiated application stream is not classified as the task's own handshake. The stranded cancel path posts the shutdown to the strand; the plain path uses the task lock. The connector also refuses to retry a cancelled establishment.

`TaskBase::requestCancelOrReissueInternal()` is used only by the four deadlines whose timers survive the relevant cancellation: TLS protocol, HTTP connect, HTTP-server idle, and HTTP-server response. It reissues cancellation only while the task is still Running. Ordinary `requestCancel()` remains idempotent; this change does not globally convert repeated cancellation into repeated teardown.

`SimpleHttpTask` now arms its existing request deadline before the first TLS handshake, once for the logical task, and keeps it across a handshake retry and retry-time resolution. The timer can act while no channel is open. It still starts after the initial TCP connection; no newly added first-DNS/first-connect timeout is claimed.

The truncation observation belongs to the TLS stream wrapper and is reset at the next handshake. Both protocol drivers and affected legacy paths feed it through the stream policy. Shutdown sets `SSL_RECEIVED_SHUTDOWN` only after application reads have ended, preserving the local alert attempt while avoiding an impossible peer-alert wait. The separate `hasShutdownCompletedSuccessfully` verdict remains false after observed truncation, even if shutdown's completion code is clear. This preserves the distinction between completing teardown and receiving an authenticated peer closure. The flag operation is supported by [OpenSSL's shutdown-state API](https://docs.openssl.org/3.0/man3/SSL_set_shutdown/).

The implementation also documents that a posted stranded shutdown may outlive the task, so detaching or replacing its stream must obey the strand ownership rule as well as task-state locking. The pool handoff uses that ownership boundary.

These changes narrow particular cancellation holes; they do not provide a general bound on application-phase composed-read cancellation. The accepted remainder is described below.

### Shared consumers and test infrastructure

The dependency sweep included the new-client paths and the legacy users of the shared networking code: `SimpleHttpSslTask`, HTTP server receive/response tasks, `TcpBlockTransferCommon`, transfer `SendRecvContext`, and the connection/tunnel/task infrastructure they use. Call-site searches confirmed the four reissue-deadline users and both direct/tunnel users of the new connect helper.

CS-5 centralizes the TLS test peer by role, avoiding copies with subtly different shutdown behavior. CS-4's test-stub lock-inversion fix moves destruction of the held completion reference outside the stub mutex, removing the test-only reverse edge into the execution queue. The duplicate include and stale helper deletion introduce no replacement production behavior.

The meaningful test additions address the important discriminators: first-read/first-write barriers; overflow before a drain and with a blocked/throwing sink; custom-decoder eligibility; real establishment failures through pool/session; ALPN fallback with a counting sink; small and undersized pooled DATA blocks; connect iteration/cancel; and shared TLS teardown. These are observations about test implementation and intent, not new test-run results.

## Accepted limits that remain reachable

The following are not counted as new findings and are not silently converted into new implementation requests. Their consequences still limit what “all fixed” can mean.

| Limit | What a caller can still encounter | Disposition relevant to this review |
|---|---|---|
| **H09 / decoder P1: caller work under continuation locks** | Decoder creation/decode, body-source rewind, and related continuation work can block or re-enter while the queue/wrapper lock is held. A callback's own duration is not bounded by a timer that cannot run that continuation forward. | Structural work remains deferred. The new decoder guards reduce incorrect invocation, not lock scope. The public registry makes custom decoders reachable today. |
| **Lost cancellation between steps of a composed read** | A silent peer can retain a cancelled task until it sends or closes. The affected set includes both HTTP drivers, legacy TLS HTTP, cleartext legacy status/header `read_until`, and TLS block transfer. The gap can follow a TLS non-application record as well as occur mid-record. | Explicitly accepted in the September 28 decision. The four deadline reissues and handshake-only receive shutdown are narrower fixes. A general watchdog/per-operation solution was not chosen. |
| **Deliberate driver close and the same read gap** | A close without a write in flight need not send FIN, so the peer need not receive that wake-up signal either. | Included in the same accepted limit, not omitted from its scope. |
| **BodySource/BodySink readiness and terminal/reset contract** | A temporarily empty non-final source or paused sink lacks the richer readiness/reset contract needed for all resumption/replay cases. A failed streamed request does not receive a universal terminal callback through the frozen sink interface. | Explicit interface deferrals remain. Refusing a spent sink is the chosen current behavior. |
| **Pool admission/dispose and exceptional task paths** | The previously recorded pending-action lifetime, exceptional completion/accounting, and exceptional command-post abandonment limits remain. | No shared task-framework or frozen-interface redesign was part of these changes. |
| **H10's third HPACK hazard** | Dropping an already-encoded header block could invalidate compression state if a future caller makes that engine path reachable. | Still a future/general-session limitation, not shown reachable through current L6 calls. The SETTINGS/order fixes remain in place. |
| **H11** | A missing required HPACK table-size update remains accepted by the chosen decoder leniency. | Deliberate protocol leniency. |
| **H19** | The nominally plain client still needs the OpenSSL-facing header dependency. | Header split accepted/deferred; not closed by the TCP/TLS fixes. |
| **H21** | TLS ALPN fallback to HTTP/1.1 still spends a dispatched retry when preface riding is enabled; with zero retries the first such request can fail. | The known-protocol narrowing remains correct. `ridePreface = false` is the existing caller choice. |
| **H23** | Queued establishment attempts and dispatched-hop attempts remain separately renewed budgets controlled by one setting. | Documented, deliberately separate until a different request-identity/budget design is chosen. |
| **Cookie policy** | The existing no-public-suffix-list boundary remains. | An accepted scope choice; no cookie behavior changes in this comparison. |

The exact, expanded cancellation scope is recorded in [astra-second-review-decisions.md](issues/astra-second-review-decisions.md), lines 618–639, and [the lost-forced-cancel design](issues/astra2-cs6-lost-forced-cancel-design.md). It would be inaccurate to describe CS-6 as fixing every cancellation hang.

## Embedded decompression and browser-impersonation implications

The three requested proposal documents are unchanged across this production comparison:

- [embedded-compression-design.md](embedded-compression-design.md);
- [embedded-decompression-design.md](embedded-decompression-design.md);
- [embedded-decompression-implementation-plan.md](embedded-decompression-implementation-plan.md).

I therefore retain the original C01–C11 analysis rather than marking unchanged proposals remediated by unrelated runtime guards. The new code does close the concrete minimal message-integration problems exposed by application registration: it does not partially decode layered Content-Encoding and then erase the remaining layers' metadata, and it does not invoke a codec on the identified bodyless/failed responses.

That is not full layered or incremental decompression. Unknown or multiple codings remain raw; streaming sinks bypass this buffered decode path; no built-in codec implementation landed in this comparison.

| Original proposal findings | Remaining design obligation |
|---|---|
| **C01** | Resolve C++11-compatible storage/linkage instead of relying on C++17 inline variables. |
| **C02** | Bound decoder workspace and work before output, not only emitted output size/expansion. |
| **C03, C10** | Registration and a successful fetch do not establish an incremental HTTP pipeline or complete impersonation. Wire integration, framing, limits, and adoption criteria still need evidence. |
| **C04, C05** | Specify HTTP deflate framing and a complete incremental input-consumption, completion, trailing-data, and error contract. |
| **C06, C07** | Establish allocator callback exception safety and a real mixed-translation-unit linkage/ODR strategy for transformed codec sources. |
| **C08, C09** | Make the macro-isolation and feasibility probes discriminate the safety/portability properties they claim. |
| **C11** | Define reproducible generated metadata, including timestamps. |

The parked E5 packaging choice also remains separate. Decoder P1/H09 must not be described as latent simply because no built-in decoder ships. Conversely, the minimal P2/P3 implementation should now receive credit; full reverse-order multi-layer decoding remains part of the parked programme.

The fingerprint-facing changes here preserve ordered headers while normalizing invalid protocol-neutral input. They do not establish browser-equivalent compressed-content negotiation, TLS/H2 fingerprints across all supported hosts, or the L7 capture/interop acceptance criteria. Those claims still require their own recorded evidence.

## Validation evidence and what this review did not establish

The implementation records contain substantial focused validation, including red/green controls. I read them as historical claims alongside the test source.

- **CS-1–CS-3:** the decision record lists landed changes, focused lane validation, and orchestrator gates. Relevant source includes the [TLS truncation cases](../../src/utests/utf_baselib_httpclient8/TestHttp1DriverTlsTruncation.h), [plain startup barrier](../../src/utests/utf_baselib_httpclient11/TestHttp1DriverStartup.h), [TLS startup barrier](../../src/utests/utf_baselib_httpclient12/TestHttp1DriverStartupTls.h), and [session outstanding-cap cases](../../src/utests/utf_baselib_httpclient9/TestClientSessionOutstandingCap.h).
- **ThreadSanitizer:** B6 records 22 client modules, with two pre-existing reports leading to I12 and I13. Subsequent records describe focused clean reruns after those fixes and the shared TCP/TLS work. This does not establish coverage of T01's direct-accessor interleaving.
- **CS-4–CS-6 gate:** [the decision record](issues/astra-second-review-decisions.md), lines 968–989, reports all 58 modules rebuilt at `0f6f05d`, in clang release and gcc debug on the Linux a64 host, with 112 passing runs of 56 test executables.
- **Gate qualifications:** JNI reportedly passed its 11 cases in each variant but exited 200 during the existing teardown failure. The plugin is a library exercised by the loader, not an independently runnable test binary. These explanations should accompany “green”; an unqualified claim that every process exited successfully would be false.
- **Raw artifacts unavailable here:** the cited `logs/astra2/` directory is absent from this checkout. I did not inspect the raw gate/TSan logs or verify their checksums. The committed records and test source are the available evidence.

Outstanding work is already identified in the records, not newly discovered or silently waived:

| Check | Why it matters | Current disposition |
|---|---|---|
| **Windows matrix for this remediation, including CS-6** | IOCP cancellation, receive shutdown, TLS truncation teardown, connect/reopen behavior, exception chaining, and x86 module sizes are not established by Linux results. | Open host-dependent obligation in [windows-matrix-handoff.md](issues/windows-matrix-handoff.md), sections beginning at 310 and 381. Older Windows results predate these changes. |
| **Boost before 1.72** | The new empty-list connect post has a separate old-`io_service` branch. | A devenv2/devenv3 compile remains owed in the CS-6 record; it was not run here. |
| **Linux x86-64 / GCC matrix, X1** | Prior defects in this project have depended on compiler argument evaluation and target, so a64 results are not a substitute. | Explicitly deferred by the maintainer; still recorded in the owed list. |
| **Windows socket-open-failure injection** | The POSIX control cannot force the same open failure on Windows without an additional seam. | Explicit test skip, not claimed coverage. |
| **Module size on Windows x86** | The new modules' a64 sizes and x86 estimates are not measured x86 object sizes. | The handoff carries the measurement and the maintainer's condition for reconsidering splits. |

The missing raw logs do not prove a failed gate, and outstanding platform work is not proof of a platform defect. They are the limits on what this static review can confirm.

## Production dependency inventory

This inventory records the changed production surfaces and the adjacent contracts followed, so the review can be resumed without rebuilding its scope from the ledger.

| Changed headers | Dependencies and behavior followed |
|---|---|
| `core/NetUtils.h` | Central peer-close/clean-end predicates; both client drivers and stream-policy-specific TLS truncation classification. |
| `http2/Http2ConnectionTask.h`, `http2/Session.h` | Protocol-neutral request conversion; ordered HeaderList operations; URI authority parsing; exact HEAD semantics; DATA events, DataBlock pooling, flow-control consumption, close/retry mapping. Unchanged framing/HPACK boundaries were checked for interaction, not rewritten. |
| `httpclient/Http1ConnectionTask.h` | Ready/start publication; submit/start/cancel overlap; initial and rearmed reads; write/read ending barrier; parser EOF eligibility; response publication; operation accounting and stream ownership. |
| `httpclient/HttpClientRequestTask.h` | Mailbox admission/drain; per-block charge lifecycle; buffered/streamed body handling; deferred callbacks; timeout/cancel/failure precedence; completion publication; late-event handling; slot release and metadata access. |
| `httpclient/ClientSession.h` | Configuration snapshots; session/wrapper continuation; retry refusal before rewind; redirect method/body changes; custom decoder registry and response-header cleanup; fallback/streaming-upload connection configuration. |
| `httpclient/ConnectionPool.h` | Key/entry state; driver adoption; retirement and total-count retention; queued/dispatched accounting; deferred actions; timeout reasons; replacement admission and stream release. |
| `httpclient/ClientConnectionTaskBase.h` | Connector deadline, first cancel reason, exception chaining, negotiated driver handoff and cancellation lifecycle. |
| `tasks/TcpBaseTasks.h`, `tasks/TcpTunnelStage.h` | Common forced shutdown; own connect iteration; resolver iterator lifetime; direct/proxy paths; cancellation/error precedence; TLS retry suppression; old/new executor APIs. |
| `tasks/TcpSslBaseTasks.h`, `tasks/AsioSslStreamWrapper.h` | Own-handshake flag lifecycle; error classification and truncation observation; alert/shutdown flags; exception enhancement; reset/attach/detach and shutdown result. |
| `tasks/TcpStrandedStreams.h`, `tasks/TcpSslStrandedStreams.h` | Posted cancellation ordering, task-lock/strand ownership, stream detachment after task completion, plain-policy versus stranded-policy reach. |
| `tasks/TaskBase.h`, `tasks/MultiOperationTask.h` | Ready callback and continuation execution; schedule/handler lock rules; cancellation idempotence/reissue; per-run close reset; operation-counted termination and initiation exceptions. |
| `http/SimpleHttpTask.h`, `httpserver/HttpServer.h` | Legacy handshake deadline and retry-time resolver interval; TLS truncation consumption; server idle/response timers; interaction with shared plain/TLS policies. |

The new report leaves implementation decisions and accepted deferrals intact. The actionable review feedback is T01's public-accessor synchronization/contract choice and T02's status correction; the existing Windows, older-Boost, and X1 obligations retain their recorded status.
