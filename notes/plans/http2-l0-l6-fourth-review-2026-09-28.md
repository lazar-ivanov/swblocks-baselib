# HTTP/2 L0–L6: fourth architecture, correctness, and security review

**Date:** 2026-09-28

**Reviewed HEAD:** `045e889ad52becbb9f777706bc4bd2ae90f98647`, branch `lazari2`.

**Comparison base:** `b4d36dfa3c02649199b7616551d677344a8f055e`, the final HEAD of the [third review](http2-l0-l6-third-architecture-security-review-2026-09-28.md).

**Method:** static source, history, contract, and existing-artifact review. No builds, tests, executable probes, implementation changes, or commits were performed. This new report is the only file created by this review.

## Assessment

**T01 and T02 are fixed at this HEAD.** The chosen T01 contract—freeze the public status pair when the request completes, while still delivering the actual late connection verdict to the pool—is implemented consistently. The T02 current-status corrections are present.

**One additional P2 finding is established by source inspection: U01, an early request failure copying negotiated-protocol metadata while connection establishment can still write it.** This is newly discovered, but the defective call predates this remediation. It is related to H04's publication problem at a different reader; neither a recurrence of the repaired pool call nor a regression introduced by CS-7.

No additional confirmed runtime regression introduced by CS-7/CS-8 was found. This conclusion does not close the previously accepted limitations or the outstanding host-dependent validation.

| Item | Current disposition | Classification |
|---|---|---|
| T01: completed request's status getters versus late close | Fixed by `980295e`, merged at `605b7d9`; same-batch behavior retained | Previous defect closed |
| T02: contradictory current-status summaries | Corrected by CS-8 and the subsequent plan-status correction | Previous documentation finding closed |
| U01: failure during establishment reads unpublished negotiated metadata | Present at `045e889` | Newly discovered pre-existing defect; residual of the broader publication-risk family |
| New runtime regressions in the reviewed change-sets | None established | Scoped review result, not a claim that every interleaving has been exercised |

## U01 — P2: early request completion races negotiated-protocol publication

**Primary location:** [HttpClientRequestTask.h](../../src/include/baselib/httpclient/HttpClientRequestTask.h#L1890), `completeResponse()`, especially the unconditional connection getter at line 1906.

**Confidence:** high, from the concrete caller/writer paths below. The interleaving is source-derived; this review did not reproduce it under ThreadSanitizer or observe a crash.

### What goes wrong

`completeResponse()` copies `m_connection->negotiated()` whenever a connection pointer is present. It does not first establish that the connection has published its negotiated value.

That is safe for an immutable, factory-built driver's value, and ordinary response events provide ordering from the establishing strand. It is unsafe when the request fails independently while still riding the establishing HTTP/2 driver. Cancellation and the request's response-headers deadline can finish the request before that driver's handshake finishes.

The HTTP/2 driver explicitly documents the required synchronization: read its atomic `state()` first, and **do not read `negotiated()` after observing `Connecting`**. Its establishing base writes an ordinary `NegotiatedProtocol`; only afterwards does the driver publish `Ready`. The request completion path skips this rule.

`NegotiatedProtocol` contains both a plain protocol field and a `std::string` holding the ALPN identifier. The response setter takes this object by value. The conflicting operation is therefore a read/copy of ordinary mutable storage, including the string, against the handshake's assignment—not merely a stale atomic observation. It is a C++ data race and undefined behavior. No particular corruption, exploit, or TLS-verification bypass is claimed.

### Cross-file evidence

Line numbers below refer to the reviewed HEAD.

| Responsibility | Source and relevant behavior |
|---|---|
| Session's normal connection factory | [ClientSession.h](../../src/include/baselib/httpclient/ClientSession.h#L1982), `makeConnectionFactory()`: creates an `Http2ConnectionTaskT` as the connection attempt, including for TLS negotiation/fallback. |
| Establishing driver exposed to the pool | [ConnectionPool.h](../../src/include/baselib/httpclient/ConnectionPool.h#L2192), `startConnection()`: obtains `ClientConnection` directly from the attempt task, records it, schedules establishment, and examines waiting requests. |
| Dispatch before negotiation | [ConnectionPool.h](../../src/include/baselib/httpclient/ConnectionPool.h#L1693), `findDispatchable()`: records a `Connecting` candidate and returns it for a replayable request when `ridePreface` is enabled (line 1759). The policy defaults to true (line 373). The session disables it only when its configured transport cannot produce HTTP/2 (line 1859). |
| Request retains that connection | [HttpClientRequestTask.h](../../src/include/baselib/httpclient/HttpClientRequestTask.h#L887), `applyAcquired()`: stores the connection at line 923, submits at 938, and arms the response-headers timer at 1044. Successful submission does not mean negotiation has finished. |
| Submission/cancellation do not wait for negotiation | [Http2ConnectionTask.h](../../src/include/baselib/http2/Http2ConnectionTask.h#L477), `postCommand()`: before `m_isStrandReady`, commands are queued without posting a drain. `submit()` at 2996 returns the allocated handle; `cancel()` at 3039 also only posts a command. |
| Independent early completion | [HttpClientRequestTask.h](../../src/include/baselib/httpclient/HttpClientRequestTask.h#L1795), `applyStopped()`: queues a stream cancellation, then calls `failWith()`. The latter calls `completeResponse()` at 1988 before publishing request completion. `requestCancel()` at 2297 marks this request cancelled and posts its event. |
| Unsynchronized writer | [ClientConnectionTaskBase.h](../../src/include/baselib/httpclient/ClientConnectionTaskBase.h#L646), `continueAfterConnected()`: assigns `m_negotiated` at 656, then calls `onProtocolNegotiated()`. The member at line 371 is not atomic or immutable. |
| Required publication | [Http2ConnectionTask.h](../../src/include/baselib/http2/Http2ConnectionTask.h#L197), class contract; `onProtocolNegotiated()` publishes `Ready` at 2808; `state()` loads the atomic at 3099; `negotiated()` returns the underlying reference at 3111. |
| Actual copy | [ClientTypes.h](../../src/include/baselib/httpclient/ClientTypes.h#L104), `NegotiatedProtocol` fields; the by-value `ClientResponse::negotiated()` setter at 645 copies the source before assigning the response's own value. |

A supported execution is:

1. A replayable TLS request acquires the establishing driver while its state is `Connecting`. This is the intentional preface-riding optimization.
2. The request submits successfully; the driver queues that command until establishment finishes.
3. After acquisition, but before connection publication, the caller cancels the request or a configured request deadline expires.
4. The request's drain handles the failure and copies `m_connection->negotiated()`.
5. Independently, the connection's I/O/strand handler assigns `m_negotiated` while finishing establishment.

The request mutex and connection task/strand are separate synchronization domains. The request's deferred stream-cancel call does not wait for establishment and does not protect the copy made before it. A cancellation arriving before acquisition would take a different, already-completed acquire path; this finding specifically requires acquisition first.

A throwing `submit()` has the same missing precondition: its catch calls `failWith()` while retaining the establishing driver. That is another route to the same completion helper, not a separate finding.

### Why existing corrections do not cover it

The original [H04 finding](http2-l0-l6-architecture-security-review-2026-09-21.md#h04--p1-the-pool-reads-fallback-driver-and-negotiated-state-before-synchronized-publication) identified the pool's driver-pointer publication and its `effectiveMaxConnectionsPerKey()` read. The latter now correctly checks `entry->isReady` before calling `negotiated()` at [ConnectionPool.h](../../src/include/baselib/httpclient/ConnectionPool.h#L1120). The [H04b correction record](issues/s6r1-design.md#7-h04b--test-isready-before-reading-negotiated) is explicitly about that pool expression.

The request's independent reader was not covered by that correction. Repository searches find the three production `connection->negotiated()` call sites in the pool and request task; this finding concerns the unconditional response-copy site. The request's separate unsuitable-connection check is conditional on submission refusal and is not used as evidence for an additional race here.

CS-7 fixes writes made **after request completion**. U01 occurs **while constructing the response before completion**, while the other task is still publishing its connection metadata. Freezing the response afterwards does not make its source copy safe. The accepted application-phase cancellation limitation is also a different problem: U01 needs neither a composed-read cancellation failure nor a stuck teardown.

The new CS-7 tests deliberately exercise the T01 contract with [ProbeConnectionT](../../src/utests/utf_baselib_httpclient/TestHttpClientRequestTask.h#L116), whose negotiated value is `const`, fixed by its constructor, and initially associated with a `Ready` state. Its later state changes do not mutate that value. The adjacent failing-establishment probe also stores a const negotiated value. These probes cannot expose a real establishing driver's concurrent metadata assignment. Their clean TSan results do not establish coverage of U01.

### Provenance and classification

`git blame` attributes `completeResponse()`, including the unchecked getter and its “read at the END” assumption, to **`0c27dc33a5002dee1409b78faee48b46c2b62885` (2026-09-19)**, the original request-task implementation. The same code is present at the comparison base, `b4d36df`.

Therefore:

- **Newly identified:** yes.
- **Introduced by the latest fixes:** no.
- **Related to a prior issue:** yes, H04's requirement for synchronized publication, at another caller.
- **Already deliberately accepted/deferred:** no such disposition was found for this request-completion read.

### Design intent and recommended correction

The intent of storing one coherent protocol/ALPN value is sound. So are preface riding and prompt stream-scoped cancellation. The incorrect assumption is that the *end of a request* necessarily follows the *end of connection negotiation*. A failed request can end first.

The default `NegotiatedProtocol` already represents that situation: `Unknown` with an empty identifier. Using it for a request that failed before publication fits the existing value type and avoids delaying cancellation to obtain metadata that did not yet exist.

- **What it is:** make the failure-completion snapshot obey the establishing driver's publication contract.
- **Consequence if unchanged:** a legitimate cancellation or deadline during establishment can execute an unsynchronized protocol/string copy even though the request failure itself is handled normally.
- **Risk, complexity, and reach:** a small change at the common single-hop completion path, used by both HTTP protocols and by the session. Review success, failure, fallback, and already-closed connections because that helper serves them all. No new queue/task lock edge or interface change is inherently needed.
- **Implementation choice:** enforce the state precondition at this reader, or redesign the getter to supply a separately published immutable snapshot. The former is the smaller change under the existing driver contract.
- **Recommendation:** read connection state first; if it is still `Connecting`, leave the response's negotiated value at its default. Otherwise copy the published value. Do not read first and check state afterwards. Preserve the driver's guarantee that non-`Connecting` states publish a final stable value, including establishment failure. Reconsider the larger getter/snapshot design only if callers must obtain safely readable provisional metadata during establishment.

This recommendation does not require disabling preface riding, waiting for handshake completion before answering cancellation, changing the chosen T01 freeze contract, or omitting late stream cleanup.

Suggested future verification, **not executed here**:

1. A deterministic request/connection contract case: acquire a `Connecting` driver, complete the request by cancellation and by headers timeout before allowing establishment to finish, and verify that its unsafe negotiated getter was not called. The completed response remains `Unknown`/empty, the original failure remains intact, and the later close returns exactly one slot.
2. A corresponding throwing-submit case before publication, plus controls which verify that a published protocol/ALPN pair is copied correctly and remains unchanged after request completion.
3. A focused TSan case with a mutable establishing driver, or the real H2 establishment path, overlapping request failure with negotiated-value publication. This complements the deterministic contract check; an unchanged immutable probe cannot exercise the race.

## Verification of T01 and its neighboring contracts

The [maintainer's D1 decision](issues/astra-third-review-decisions.md#d1--t01-what-isretryable--and-outcome--promise-a-caller-after-the-request-completes) selected freezing at completion. That is a valid resolution of the previous review's contract choice.

The complete runtime delta was checked, including all writers and callers affected by the new outcome parameter:

- [`applyClosed()`](../../src/include/baselib/httpclient/HttpClientRequestTask.h#L1557) computes a local connection outcome, updates the public pair only before `m_isCompleted`, and still performs late close cleanup. It does not discard the late event.
- [`releaseConnectionSlot()`](../../src/include/baselib/httpclient/HttpClientRequestTask.h#L2167) receives that local outcome by value and captures it by value in the deferred pool call. A frozen request result therefore cannot conceal a later unusable connection from the pool.
- The three acquire/throw/refusal call sites explicitly pass the same `m_outcome` they previously used. They retain the acquire/release pairing even where no stream handle was created.
- Other writes to the public pair occur in construction or in `applyAcquired()`, which exits after completion or pending completion. The completion guard in `applyClosed()` closes the remaining late-write path.
- `m_isCompleted` is set in the final drain phase before `notifyReady()` ([request lines 712–733](../../src/include/baselib/httpclient/HttpClientRequestTask.h#L712)). [TaskBase](../../src/include/baselib/tasks/TaskBase.h#L726) publishes its atomic completion state before invoking the ready callback. Thus the chosen post-completion accessor contract has both publication of prior writes and absence of subsequent writes.
- The guard intentionally tests only `m_isCompleted`, not `m_isCompletionPending`. A close in the same drained batch as the request's own failure still sets the connection-status pair. A later batch cannot. That is the explicitly chosen contract, not an omission to harmonize with neighboring guards.
- [`ClientSession::chkPrepareRetry()`](../../src/include/baselib/httpclient/ClientSession.h#L1200) rejects sink/own-failure replay before consulting the pair. The retained same-batch connection verdict does not turn a winning request timeout into permission to replay.
- Headers, trailers, body delivery, upload pulls, failure publication, and sink bookkeeping were checked around this path. No additional post-completion mutation of their published result fields was established. U01 is the separate problem of reading the response's source metadata too early.

The added tests in [TestHttpClientRequestTaskAfterTheFailure.h](../../src/utests/utf_baselib_httpclient/TestHttpClientRequestTaskAfterTheFailure.h#L781) cover cancellation and headers timeout crossed with connection loss and retryable close. They inspect the frozen status while the late close can be draining, then verify the actual pool verdict and single release. The separate same-batch case at line 920 uses the manual thread pool to put expiry and close in one batch; its recorded control demonstrates that adding a pending-completion guard would break this contract.

## Verification of T02 and the remaining changes

The documentation sweep was checked against commits and current source rather than treating a “done” row as proof.

The canonical [HTTP/2 design](http2-design.md), [decoder deferral](issues/http-content-decoders-deferral.md), [L6 record](issues/http2-l6-review-record.md), [owed-work record](issues/astra-remediation-owed-work.md), and second/third review decision records now distinguish landed changes from accepted deferrals. The R01/R02/R03 security summaries and minimal decoder integration corrections are no longer presented as unimplemented. The B6 and E1–E4 statuses are reconciled. Older design-time text is retained with dated corrections instead of silently rewritten as if it described today's code.

The [implementation plan](http2-implementation-plan.md) now correctly says L0–L6 and their recorded remediation have landed, with host validation and accepted deferrals remaining; L7/L8 have not started. The `ConnectionPool.h` changes in this range only correct its explanation of the connect loop. There is no transport or pool runtime change hidden in that diff.

D3's clarification that the module-size policy uses MB of 2^20 bytes changes documentation, not enforcement. Current size notes distinguish measured byte counts from x86 estimates; the Windows handoff still requires actual x86 measurements. The expanded test module's documented proximity to the target is acknowledged rather than reported as measured Windows headroom.

The three embedded compression/decompression proposal files are unchanged across this comparison. The previous C01–C11 analysis remains applicable to those parked proposals. CS-7/CS-8 neither ship codecs nor implement the proposed layered/incremental decompression pipeline. The already-landed eligibility checks for application-provided decoders remain a separate, real runtime correction; this review does not revive the invalid premise that no built-in codec means no caller can reach decoder integration.

## Existing validation evidence inspected

The earlier review could not locate the cited evidence inside the checkout. The current records clarify its location: **`/home/lazar/dev/github/http2-l0-state/logs/`**, in a sibling directory, intentionally outside Git. That directory is available in this workspace. Selected original logs and summaries were read during this review; the location limitation is resolved for those artifacts.

These are **previous implementation runs**, not builds or tests performed by this review:

| Archived evidence | What was checked and what it establishes |
|---|---|
| [CS-7 gate summary](../../../http2-l0-state/logs/astra3/gate-astra3-cs7-summary.log) and [independent check](../../../http2-l0-state/logs/astra3/gate-astra3-cs7-independent-check.log) | Gate at `083898d`: seven affected modules under clang release and gcc debug, 14/14 passing. The independent artifact records matching case entry/exit counts, success markers, and no failure/leak markers. The separate case-count comparison matches the inventory. |
| [TSan red analysis](../../../http2-l0-state/logs/astra3/cs7/tsan-red-7a664cf-READ.txt), its run logs and 50-run summary | The old `applyClosed()` writes race the public getter reads in the new T01 case. Selected raw frames identify the claimed source accesses. All 50 archived runs report; the archived positive control also reports its known race. |
| [TSan green analysis](../../../http2-l0-state/logs/astra3/cs7/tsan-green-980295e-READ.txt) and 50-run summary | The corresponding archived 50 runs at `980295e` complete without race reports. These counts were checked against the stored summaries. This supports T01's correction; it does not cover U01's mutable establishment metadata. |
| [Same-batch control](../../../http2-l0-state/logs/astra3/cs7/f2-634fd4a-READ.txt) and associated raw results | The correct code passes the 78-case module; the intentionally over-restrictive guard fails the new case's two sub-runs. It pins the selected distinction between pending completion and completed state. |

The archived 50-run pairs are focused regression checks, not a measured failure-rate acceptance criterion. The single-threaded same-batch control is deterministic. Neither kind of result establishes that all asynchronous paths are race-free.

## Accepted residuals and validation boundaries

These items are not counted as new findings or reopened solely because this review encounters them:

| Existing boundary | Current consequence/status retained |
|---|---|
| H09 and continuation-path caller work | A blocking/re-entrant continuation, custom decoder work, or synchronous rewind can still encounter the recorded lock/execution limits. Documentation is not a structural elimination of those paths. |
| Application-phase cancellation and composed reads | The accepted cancellation gap for silent peers, including the recorded TLS/application-read and related plain-read cases, remains. The establishment metadata race in U01 is independent of it. |
| Stream interfaces and exceptional lifecycle paths | The accepted body source/sink readiness, terminal callback/reset limitations, pool admission/dispose lifetime limits, and exceptional scheduling/accounting/posting paths retain their existing dispositions. |
| Protocol and policy choices | H11 leniency, H19 header coupling, H21's remaining TLS/ALPN retry cost, H23's separate retry budgets, the deferred unreachable H10 hazard, and the cookie jar's lack of a public-suffix policy are not newly introduced here. |
| Windows | The remediation handoff remains open, including CS-7 C1's contract cases and C2's actual x86 module-size measurements. Linux gate results do not close IOCP, transport error-code, TLS teardown, or size obligations. |
| Other hosts/toolchains | The pre-Boost-1.72/devenv2–3 compile obligation and maintainer-deferred Linux x86-64 GCC matrix remain. No portability conclusion was inferred from the existing a64 results. |
| Future layers and decompression | L7/L8 and the parked decoder proposals remain outside completed L0–L6 implementation. No claim of complete browser impersonation or complete decompression support follows from closing T01/T02. |

The current obligations were cross-checked with [astra-remediation-owed-work.md](issues/astra-remediation-owed-work.md), [astra-third-review-decisions.md](issues/astra-third-review-decisions.md), and [windows-matrix-handoff.md](issues/windows-matrix-handoff.md). Those records were not edited by this review.

## Review coverage and limits

The comparison contains 34 changed files. Only `HttpClientRequestTask.h` changes production behavior; `ConnectionPool.h` changes comments. The remainder consists of tests, inventory, size notes, instructions, plans, and review/decision records.

The complete production delta and its relevant unchanged dependencies were traced through request event batching and completion, `TaskBase`/execution-queue publication, session replay, pool acquisition/release, H2 establishment and command dispatch, negotiated-value storage, and the affected probes/tests. History was used to distinguish inherited defects from regressions. Status identifiers and current-status claims were also searched beyond the consolidated ledger.

This is a comprehensive review of that delta and its integration, building on the earlier L0–L6 reviews. It is not a claim to have freshly re-audited every unchanged HPACK, frame, TLS, URI, cookie, and decompression-proposal line. No new runtime or cross-platform evidence was generated.

**Actionable result:** T01/T02 can remain closed. U01 is a separate, concrete pre-existing publication defect in request completion; its correction should preserve the existing preface-riding, prompt-failure, frozen-result, and late-cleanup contracts.
