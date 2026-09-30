# HTTP/2 L0–L6: sixth review — V01 remediation

**Date:** 2026-09-29

**Reviewed HEAD:** `edd921b991b1406af2eafb238130c2bbc068b436`, branch `lazari2`.

**Comparison base:** `c454aacc9ba1ede5c59051c9e86c0fd66182aa5b`, reviewed in the [fifth report](http2-l0-l6-fifth-review-2026-09-29.md).

**Method:** static code, dependency, history, status-record, and archived-evidence review. No builds, tests, executable implementation probes, source edits, or commits were performed. This new uncommitted report is the only workspace change made by this review.

## Assessment

**V01 is fixed. No new findings were established in this change-set or the adjacent paths examined.**

The fix is `ba52d2fb96320ec64e9239cd665e3fb42580cab6`, merged through `f0890bd7e97f66054c7df6172c8b6c822eeaadcf`. The test now reads the probe's plain handle only when the submission wait actually observes the publication signal. A failed wait leaves the local invalid-handle default in place, retains the test failure, and preserves cleanup.

There are **no production-code changes** between the comparison base and this HEAD. U01's connection-owned publication fix and T01's completed-result contract remain as previously reviewed. This is closure of the last reported test defect, not a claim that the earlier accepted limitations or outstanding host obligations have disappeared.

| Item | Result |
|---|---|
| V01: handle read after timed-out submission wait | Closed |
| New or residual defect in the V01 correction | None found |
| Production regression introduced by CS-10 | None; production sources are unchanged |
| Previously accepted limitations and host validation | Unchanged |

## Verification of the fix

The reviewed source is [TestNegotiatedPublication.h](../../src/utests/utf_baselib_h2client11/TestNegotiatedPublication.h#L1123).

### Successful wait

The synchronization chain remains valid:

1. `RidingDriverProbe::submit()` writes `m_handle` at line 575.
2. It calls `m_submitted.signal()` at 577.
3. [OneShotSignal](../../src/utests/include/utests/baselib/TlsTeardownTestUtils.h#L111) publishes its flag under a mutex. Its predicate wait observes that flag under the same mutex.
4. `runRide()` calls `submittedHandle()` only inside `if (result.isSubmitted)`, at line 1132.

The successful wait therefore orders the handle write before its read. The helper still has one request/submission in each composed case; no later writer was introduced.

### Failed wait

When the wait returns false, the getter is not called. [RideResult's constructor](../../src/utests/utf_baselib_h2client11/TestNegotiatedPublication.h#L973) already initialized its separate local handle to `INVALID_STREAM_HANDLE`. Retaining that value does not read the probe's shared storage.

The helper then follows its existing driver scheduling, request cancellation/completion, slot-release, and teardown paths. The change adds no early return or assertion that could skip cleanup.

[`chkRodeAndReleasedOnce()`](../../src/utests/utf_baselib_h2client11/TestNegotiatedPublication.h#L1207) still requires both an observed submission and a valid handle. Thus the correction does not make a timed-out submission pass the test, including when submission eventually happens during cleanup.

The member comment and the comment at the guard now state the synchronization precondition accurately. They do not assume that elapsed time, or a wait returning false, publishes the member.

### Adjacent reads

The surrounding helper and its shared synchronization utilities were checked for the same pattern:

- The off-strand reader's `seen()` access remains conditional on a successful `waitForRead()`.
- The connection-state read after a handshake/quiet wait uses the driver's atomic state accessor.
- Request results are read after the execution queue has observed request completion.
- Pool acquire/release records are read through mutex-protected accessors.
- The shared driver probe's quiet and stopped signals are used as observations; no new unprotected state read was added after a failed wait.

No additional occurrence requiring a finding was established in these paths.

## Archived validation evidence

The following are **earlier implementation runs**, read from `/home/lazar/dev/github/http2-l0-state/`. This review did not execute them.

| Artifact | Evidence inspected |
|---|---|
| [Failed-wait diagnostic patch](../../../http2-l0-state/logs/astra5/cs10/diag-v01.patch) | Holds the request before its handle write, forces the submission wait to fail, counts getter calls after that failure, then releases the request so cleanup can complete. It checks the violated precondition without needing an uncontrolled data race. |
| [Diagnostic before the fix](../../../http2-l0-state/logs/astra5/cs10/red-diag-258b1c7-run.log) | One failed submission wait and one handle read after it; the corresponding assertion fails, rc=201. The request and driver nevertheless reach completion and one slot is released. |
| [Diagnostic after the fix](../../../http2-l0-state/logs/astra5/cs10/green-diag-4af42a5-run.log) | One failed submission wait and zero handle reads after it; the invalid default is retained, cleanup completes, and the diagnostic succeeds, rc=0. |
| [Ordinary 50-run batch](../../../http2-l0-state/logs/astra5/cs10/x50-4af42a5/summary.log) and [independent recount](../../../http2-l0-state/logs/astra5/cs10/x50-4af42a5-recount.log) | Archived results report 50/50 clean runs of all five cases. These are default regression checks, not failure-rate acceptance measurements. |
| [Final binary identity record](../../../http2-l0-state/logs/astra5/cs10/binary-identity-5695381.log) | The recorded hashes of the final clang debug binary and debug file match those used for the 50-run batch, after the later comment corrections. This review did not rebuild them. |
| [Integration gate](../../../http2-l0-state/logs/astra5/gate-astra5-cs10-summary.log) | At `3f77e4e`, the affected module passes clang release and GCC debug, 2/2. Both raw logs were checked for all five case entries/exits and the success verdict. |

The diagnostic restoration records also show the source returning to its committed hash after each probe. No diagnostic instrumentation remains in the current file.

This is sufficient evidence for the narrow branch correction when combined with the source synchronization proof. The diagnostic is not represented as a new TSan reproduction, and the ordinary successful runs alone are not the proof that the failed-wait branch is safe.

## Other changes and status reconciliation

The six changed files consist of the test helper, its inventory, the previous review report, and three plan/status records. The helper is included only by `utf_baselib_h2client11`; there is no change to a shared test helper or production header.

The inventory updates correspond to shifted case line numbers and the changed helper/namespace digest. They remove no case and change no existing test-case body.

The [implementation plan](http2-implementation-plan.md) and [decision record](issues/astra-fourth-review-decisions.md#4-astras-fifth-review--v01-a-defect-in-cs-9s-test-fixed-by-cs-10) identify V01 as fixed by CS-10, with the actual merge and gate. Searches for V01/CS-10 found the previous report's dated finding and the current closure records; no material status contradiction was established.

Moving the older S6R acceptance/ordering paragraphs back beside the S6R work separates their historical gate description from CS-10's focused gate. The [Windows handoff](issues/windows-matrix-handoff.md) retains the pending Windows checks and records both pre- and post-CS-10 a64 sizes, without treating an x86 estimate as a measurement.

## Limits retained

The test's two execution-queue waits remain unbounded. That previously recorded choice is unchanged; the updated helper comment now says so. The handle-read guard fixes V01 without claiming to harden every possible cancellation or test-teardown failure.

The recorded Windows D1–D3 obligations, the earlier older-Boost compile obligation, and the deferred Linux x86-64 GCC matrix remain. Previously accepted L0–L6 limitations remain as described in the preceding reports. The three embedded compression/decompression proposals and production implementation are unchanged in this comparison; L7/L8 remain outside completed L0–L6 work.

This review covers the complete CS-10 delta and the relevant surrounding paths, building on the previous reviews. It is not a fresh line-by-line audit of all unchanged L0–L6 code.

**Result:** V01 can be closed. No further correction is recommended from this review.
