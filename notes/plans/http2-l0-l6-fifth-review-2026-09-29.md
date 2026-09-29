# HTTP/2 L0–L6: fifth review of the implementation and latest changes

**Date:** 2026-09-29

**Reviewed HEAD:** `c454aacc9ba1ede5c59051c9e86c0fd66182aa5b`, branch `lazari2`.

**Comparison:** `045e889ad52becbb9f777706bc4bd2ae90f98647..c454aacc9ba1ede5c59051c9e86c0fd66182aa5b`.

**Previous report:** [Fourth review](http2-l0-l6-fourth-review-2026-09-28.md).

**Method:** static source, commit, dependency, design, and archived-evidence review. No builds, tests, executable implementation probes, source edits, or commits were performed. This new uncommitted report is the only workspace change made by this review.

## Assessment

**U01 is fixed.** CS-9 implements the maintainer's chosen connection-owned publication mechanism, rather than leaving each reader responsible for observing connection state. Both the establishment-base getter and the HTTP/2 interface getter now return either a separate immutable default or the fully published negotiated value. The source paths examined uphold the single-publication premise.

**No additional production correctness, security, or concurrency defect was established in the reviewed delta.** T01's completion snapshot and late cleanup remain intact. The earlier accepted limitations and host-validation obligations remain.

There is **one new P3 finding in the added test harness**, V01: after the submission wait times out, the case still reads a plain handle whose writer has not been synchronized with it. This is confined to the test's failure path; it does not reopen U01 or identify a production HTTP/2 regression.

| Item | Result | Classification |
|---|---|---|
| U01, unpublished negotiated metadata | Fixed by `a8d5427`, merged at `5db9ae8` | Previous production finding closed |
| T01/T02 | No recurrence established in this delta | Remain closed |
| V01, timed-out test wait followed by an unsynchronized handle read | Present in the new `utf_baselib_h2client11` helper | Newly introduced test-only defect, `65cf086` |
| New production regressions | None established | Scoped static-review conclusion |

## V01 — P3: the new test reads the submitted handle even when its publication wait fails

**Primary location:** [TestNegotiatedPublication.h](../../src/utests/utf_baselib_h2client11/TestNegotiatedPublication.h#L1122), `runRide()`, lines 1122–1123.

**Supporting locations:**

- [The probe's handle and writer](../../src/utests/utf_baselib_h2client11/TestNegotiatedPublication.h#L535): `m_handle` is a plain `stream_handle_t`; `submit()` assigns it at line 574 and then signals `m_submitted` at 576.
- [The wait and getter](../../src/utests/utf_baselib_h2client11/TestNegotiatedPublication.h#L581): `waitForSubmit()` returns the bounded wait's boolean; `submittedHandle()` returns the member with no lock.
- [OneShotSignal](../../src/utests/include/utests/baselib/TlsTeardownTestUtils.h#L111): `signal()` publishes under its mutex; `waitFor()` returns the result of the timed predicate wait. The shared bound is 30 seconds.
- [The handle type](../../src/include/baselib/httpclient/ClientConnection.h#L70) is `std::uint64_t`, not an atomic wrapper.

### Failure path

The helper executes:

```cpp
result.isSubmitted = driver -> waitForSubmit();
result.handle = driver -> submittedHandle();
```

On success, the signal and successful wait order the handle write before the read. That is the intended contract described by the member's comment.

On timeout, the wait returns false because it has not observed that signal. The helper nevertheless reads the member. The request task remains active on its own thread and can still be inside `submit()`, or reach it later.

A concrete source-derived interleaving is:

1. The test schedules the request, then waits for its submission signal.
2. The request thread is delayed before writing the probe's handle, or between that write and the signal.
3. The 30-second wait returns false.
4. The test calls `submittedHandle()` while the request thread's plain handle write has no happens-before relationship with that read.
5. The request signals later; a later signal cannot retroactively order the earlier read.

The handle's initialized invalid value does not make a read safe against a possible concurrent write. A timed wait having returned is also not equivalent to the awaited event having occurred.

**Consequence:** a delayed or failing test setup can itself execute a C++ data race, potentially adding a sanitizer report or unreliable diagnostic to the original timeout. It affects the three composed cases using `runRide()`. Ordinary successful waits are correctly synchronized. No occurrence was observed in the archived successful runs, and no runtime reproduction was performed in this review.

### Classification and design intent

`git blame` attributes the two lines to **`65cf086141eced352aa000d5446d3d7fca661def`**, the new CS-9 test module. This is a **newly introduced test-only issue**, not a residual production race.

The helper intentionally gathers a result and performs cleanup before making its assertions. That approach is compatible with fixing this issue: it need not inspect an unpublished member to report that submission timed out.

The checkpoint review's accepted unbounded final queue wait and optional C5/C6 test polish concern different paths. No recorded disposition was found for reading `m_handle` after a failed submission wait.

### Recommended correction

Read `submittedHandle()` only when `result.isSubmitted` is true. On false, retain `RideResult`'s existing invalid-handle default and carry out the existing cleanup/reporting path. This preserves the evidence that submission was never observed.

- **Reach and complexity:** a small conditional in the new test helper; no production behavior, API, or synchronization hierarchy changes.
- **If unchanged:** the failure path can race precisely when the test is trying to diagnose delayed submission.
- **Alternative:** if diagnostics must inspect the latest handle before submission has been observed, make that storage/read independently synchronized. There is no such requirement in the current helper, so gating the read is preferable.
- **Verification:** inspect both outcomes of the wait: only the true branch may read the plain handle. Existing successful cases should retain their behavior. Any future controlled timeout exercise should preserve cleanup and should not depend on reproducing a 30-second scheduler stall.

## Why U01 is closed

The implementation follows [D1, shape (a″)](issues/astra-fourth-review-decisions.md) and the [CS-9 design note](issues/astra4-cs9-negotiated-publication-design.md).

### Publication and reference lifetime

[ClientConnectionTaskBase.h](../../src/include/baselib/httpclient/ClientConnectionTaskBase.h#L373) now owns three distinct pieces of state: the negotiated value, an atomic published flag initialized false, and a const default value.

[`publishNegotiated()`](../../src/include/baselib/httpclient/ClientConnectionTaskBase.h#L685) writes the whole negotiated value before storing true to the flag. [The getter](../../src/include/baselib/httpclient/ClientConnectionTaskBase.h#L810) loads the flag first and only reads the mutable member if publication has occurred. Default sequentially consistent operations provide the required release/acquire ordering; the flag covers both the protocol and ALPN string.

A reader observing false returns `m_unsettled`, which is a different, const object. It does not retain a reference to storage the handshake will subsequently overwrite. A reader observing true receives the settled member, which has no subsequent production writer. Both objects have the connection task's lifetime; no shared static or exit-time destruction dependency was added.

[Http2ConnectionTask::negotiated()](../../src/include/baselib/http2/Http2ConnectionTask.h#L3116) delegates to this getter. It no longer bypasses publication by returning the protected member directly. The [ClientConnection contract](../../src/include/baselib/httpclient/ClientConnection.h#L357) states that a retained pre-publication reference stays Unknown and that a caller wanting the current value calls again.

### The single-write premise was checked independently

An atomic “published” flag alone would be insufficient if a later retry could overwrite the member. The current paths prevent that:

| Path | Evidence and result |
|---|---|
| TLS success | [AsioSslStreamWrapper.h](../../src/include/baselib/tasks/AsioSslStreamWrapper.h#L311) records handshake success before transferring to the task handler. [TcpSslBaseTasks.h](../../src/include/baselib/tasks/TcpSslBaseTasks.h#L652) then invokes the connection continuation. Publication follows successful handshake and the existing TLS-floor check. |
| TLS establishment retry | [TcpBaseTasks.h](../../src/include/baselib/tasks/TcpBaseTasks.h#L1590) permits retry only while the channel is open and the wrapper reports that handshake has not completed successfully. A failure after publication cannot satisfy that gate. The wrapper's success flag is the relevant gate, not merely the task's similarly named field. |
| HTTP/1.1 fallback | [ClientConnectionTaskBase.h](../../src/include/baselib/httpclient/ClientConnectionTaskBase.h#L646) detaches the stream and passes the negotiated value to the factory. Detachment makes the open-channel retry prerequisite false. Factory success or failure does not cause a second publication. |
| Cleartext/prior knowledge | The plain policy calls the continuation after connect; the TLS-handshake retry branch is disabled for that policy. The same publication helper is used. |
| Per-endpoint connect and tunnel | A connected endpoint proceeds into the pre-handshake stage; failed endpoints do not publish protocol state. The tunnel completes its stage before invoking the stored handshake continuation. No added publication occurs in either loop. |
| Re-scheduling a completed task | [TcpBaseTasks.h](../../src/include/baselib/tasks/TcpBaseTasks.h#L844) rejects a second establishment once its resolver exists. The only resolver reset is the pre-success retry path. A task cancelled before establishment can later run, but that cancelled run has published nothing. |
| Other writes | Repository searches found one production assignment to the establishment base's negotiated member, inside the new helper. Direct assignments in the new unstarted test probes intentionally model “written, not published”; they do not create another production writer. |

The debug assertion at the helper is a useful tripwire; the actual safety argument rests on these call paths. The flag is never cleared. Failed establishment before publication therefore continues to expose Unknown without making partially initialized storage readable.

### Behavior at the surrounding boundaries

- **Publication precedes Ready.** The value is published before `onProtocolNegotiated()` builds the H2 session and publishes Ready. A caller can safely obtain the settled protocol while state is still Connecting, as D1 requires. The fix does not conflate “protocol known” with “connection ready for dispatch.”
- **TLS checks remain in place.** The existing floor check precedes publication. This diff introduces no certificate-verification, trust-policy, ALPN-offer, or transport-error-handling change.
- **Pool admission remains state-based.** The pool retains its `entry->isReady` condition. A fallback establisher reporting Http11 before driver adoption does not prematurely raise the per-key capacity.
- **Both request readers are covered.** `completeResponse()` and the unsuitable-connection check use the now-safe interface getter. Early cancel, deadline, and throwing-submit routes need no new reader-side lock or wait.
- **T01 remains intact.** CS-9 changes only comments in `HttpClientRequestTask.h`. Frozen public results, the actual late verdict passed to the pool, and exactly-once cleanup retain the previously reviewed behavior.
- **The HTTP/1.1 driver remains immutable.** Its const negotiated member and reference-returning getter are unchanged.
- **Library conventions remain consistent.** This is a header implementation using the existing atomic idiom. No public signature, IID, continuation contract, or queue-lock edge changes. Derived drivers must follow the newly explicit getter contract; the sole production H2 implementation does.

## Review of the new coverage and archived evidence

The five cases in [TestNegotiatedPublication.h](../../src/utests/utf_baselib_h2client11/TestNegotiatedPublication.h) were read, along with their probes, shared signal/join helpers, module registration, recipes, and inventory additions.

The coverage distinguishes different claims:

1. **Deterministic getter contract:** both unstarted test types receive an unpublished value; neither getter may expose it. After publication, both return it. The H2 driver remains Connecting in this control, so an implementation incorrectly gated on Ready would fail.
2. **Actual publication race:** a reader thread predates establishment and is released through a relaxed signal; its private mutex does not synchronize it with the writer. The corrected getter provides the ordering under test. The reader holds a connection reference until it is joined.
3. **Request behavior:** the real request task and real H2 establisher run over a one-driver test pool. Cancellation during a held handshake reports Unknown; cancellation after h2 negotiation reports h2; fallback reports Http11 and a retryable bounce. These are explicitly characterizations which can pass before the fix; they are not mislabeled as independent race reproductions.

The real pool's dispatch policy is not exercised by the one-driver stub; existing pool/session cases cover that separate boundary. The publication mechanism protects every getter caller, so a separate new test for every request-side failure trigger is not required to establish the fix's mechanism.

Selected existing artifacts under `/home/lazar/dev/github/http2-l0-state/` were inspected. **These are implementation-time runs, not tests run by this review.**

| Evidence | What was verified |
|---|---|
| [Gate summary](../../../http2-l0-state/logs/astra4/gate-astra4-cs9-summary.log) and [independent check](../../../http2-l0-state/logs/astra4/gate-astra4-cs9-independent-check.txt) | Archived gate at `bb8d320`: 22 modules under clang release and gcc debug, 44 PASS entries, zero FAIL entries, final rc=0. Selected raw logs for the new module contain the success verdict. The independent check's “fail-lines” counter includes expected diagnostic text; it is not itself a failing-test count. |
| [Committed reader's red batch](../../../http2-l0-state/logs/astra4/cs9/tsan-red-4451fd0-x50/summary.log) | Independently counted 50 runs, all rc=66 and all reporting. Selected raw frames identify the off-strand protocol read against the establishment assignment. |
| [Fixed reader's green batch](../../../http2-l0-state/logs/astra4/cs9/tsan-green-e55a92d-x50/summary.log) | Independently counted 50 runs, all rc=0 with zero warning counts. The positive-control log identifies its known `TestBaselibBasicTask.h:127` race. |
| [Whole instrumented module](../../../http2-l0-state/logs/astra4/cs9/tsan-wholemodule-e55a92d-run.log) | Archived run reports no errors. It exercises the composed cases with successful submission waits; it does not force V01's timeout branch. |

The default 50-run pairs are regression evidence, not a guarantee that TSan detects every possible race. The deterministic contract case provides a separate check of the selected behavior. This review did not independently re-establish every claim about sanitizer internals in the design history.

## Records, accepted limitations, and scope

The change range contains **27 files**. Production behavior changes only in `ClientConnectionTaskBase.h` and the delegating getter in `Http2ConnectionTask.h`. The other three production-header edits are comments. The new test module, inventory, design/decision records, instructions, and size-note corrections account for the rest.

Current-status searches for U01/CS-9 agree with the implementation and merge history. The plan and decision record identify CS-9 as landed. H04b's historical record explains why the pool's readiness test remains even though it no longer protects the getter from a race. The size corrections distinguish 2^20-byte units and inferred x86 sizes from actual host measurements. No material recurrence of T02 was established.

The following remain qualifications, not additional findings from this change:

- **Windows D1–D3:** the new module still needs the recorded Windows run, measured x86 debug size, and x86 clang release build. A64 size estimates are not those results.
- **Known harness wait limitation:** the final execution-queue wait can remain unbounded if a driver cancellation fails. The checkpoint explicitly considered hardening it and retained the existing form with the Windows handoff warning. V01 concerns the earlier unsynchronized handle read, not that accepted wait limitation.
- **Existing portability obligations:** the earlier pre-1.72/devenv2–3 connect-loop compile and maintainer-deferred Linux x86-64 GCC matrix remain in their records.
- **Accepted L0–L6 limits:** continuation-lock/caller-work constraints, the application-phase cancellation gap, streaming-interface limitations, and the previously accepted protocol/retry/header-dependency choices are unchanged.
- **Decompression and future layers:** the three embedded compression/decompression proposal files are unchanged across this comparison. Their earlier C01–C11 analysis and parked status are not closed by CS-9. L7/L8 remain unstarted in the current plan.

The source review traced the complete production delta through its unchanged establishment, TLS, retry, tunnel, task-lifecycle, pool, and request consumers. It also examined the full new test module and relevant records. It does not claim a fresh line-by-line audit of every unchanged L0–L6 parser or every prior proposal.

**Result:** U01 can remain closed. No new production regression was established. The actionable new feedback is V01, the test-only timeout-path read, which can be corrected without changing the publication design.
