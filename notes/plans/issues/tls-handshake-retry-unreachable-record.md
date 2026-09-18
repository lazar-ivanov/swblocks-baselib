# The TLS handshake retry is unreachable with a real peer on Boost 1.90 / OpenSSL 3.5

**Found:** 2026-09-17, while implementing slice **S0.2** of the HTTP/2 client plan (the pre-handshake
stage hook). **Status:** **CLOSED 2026-09-18** - fixed as its own gated change-set, see "How it was
closed" at the end. **Not introduced by that change**, and production code was left untouched there.

## The defect

`TcpConnectionEstablisherConnector` retries the whole resolve-and-connect transaction when the
protocol handshake fails with a retryable error (`tasks/TcpBaseTasks.h:1389`). Whether an error is
retryable is decided by `TcpSslSocketAsyncBaseT::isProtocolHandshakeRetryableError`
(`tasks/TcpSslBaseTasks.h:287`), which accepts exactly two codes:

    return ( e.code() == g_sslErrorShortRead || e.code() == asio::error::eof );

On Boost 1.90 with OpenSSL 3.5, a peer closing the connection during the handshake does not produce
either of those. It produces `asio.ssl.stream:1` - `asio::ssl::error::stream_truncated`. This was
**measured, not inferred**: a diagnostic run printed the error and the task failed after a single
attempt, with no retry.

The library already knows that code means truncation. `isExpectedSslErrorCode`
(`tasks/TcpSslBaseTasks.h:702`) special-cases it by category name and value, with a comment
explaining that the constant is hard-coded to stay compatible with older ASIO and OpenSSL:

    if( std::string( "asio.ssl.stream" ) == ec.category().name() && ec.value() == 1 ) return true;

So the same truncation is **expected** by one classifier and **not retryable** by the other. The
retry path is therefore dead code against a real peer on the current dependency versions; it can
still be reached by a peer that produces a bare `eof`, which is why this went unnoticed.

## Why it matters for HTTP/2

Connection establishment for the HTTP/2 client relies on that retry (design §1.1, and the
establishment path of §5.1). A client which does not retry a truncated handshake will surface a
transient peer-side close as a hard connection failure, which for a pooled, long-lived connection is
a materially worse failure mode than for a one-shot request. Slices S3.5, S4.1 and S5.2 all inherit
it.

## Why it was not fixed in S0.2

Widening the classifier changes the behavior of every existing TLS client task in the library - the
same class of change as the four commits of design §3.8, which the author accepted only on the
condition that they are tested comprehensively and gated on the whole suite (D19, D26). Doing it
inside a slice whose brief was the pre-handshake hook would have smuggled a second behavioral change
into a gated change-set, and would have invalidated that gate's own premise that nothing else rides
with it.

The S0.2 tests therefore reach the retry path through a **test-only** `STREAM` policy which widens
the classification for the test alone, by hiding a static interface member - the mechanism design
§3.1 itself names. `tasks/TcpSslBaseTasks.h` was not modified.

## The decision owed

Whether to widen `isProtocolHandshakeRetryableError` to accept what `isExpectedSslErrorCode` already
recognizes as truncation, and if so, whether it lands as its own gated change-set before the HTTP/2
I/O shell is built on top of it, or is left as-is with the HTTP/2 connection task handling truncation
itself.

The narrow fix is to call the existing classifier rather than to duplicate its knowledge:

    return ( isExpectedSslErrorCode( e.code() ) || e.code() == asio::error::eof );

which is a one-line change with a suite-wide blast radius - hence a gated change, not an amendment.

---

## How it was closed (2026-09-18)

Three commits on `http2-lane-1`, in this order, each independently reviewable.

**1. `7ee3e1d` - characterize.** New `src/utests/utf_baselib_http2/TestTlsHandshakeRetryClassifier.h`
pins what the predicate answers for every code a handshake can fail with, reaching it *through* the
establisher because template composition is how the retry site resolves it. It recorded the defect
as a passing assertion: `asio.ssl.stream:1` not retryable, and `isExpectedSslErrorCode` saying
*expected* about the same code.

**2. `9182fd0` - the change.** One returned expression:

    return ( isStreamTruncationError( e.code() ) || e.code() == asio::error::eof );

**Not `isExpectedSslErrorCode` as proposed above**, although the two admit the same codes -
`isStreamTruncationError` is a one-line forwarder to it. The reason to go through the forwarder is
that it, and not `isExpectedSslErrorCode`, is what the STREAM policy's static interface declares
(`TcpSslBaseTasks.h:172`, `TcpBaseTasks.h:461`). The retry therefore follows whatever the policy in
use calls a truncation; the plain TCP policy answers `false` there and keeps answering `false` here.

**Does the borrowed classifier admit anything the retry should not? No.** It accepts exactly two
codes - a category *named* `asio.ssl.stream` with value exactly 1, and exactly `g_sslErrorShortRead`
- and `utf_baselib_io2/TestIO2.h:1110` already pins that with its negatives. One of the two was
already retryable. So the behavioural delta is **one spelling of a condition the predicate already
retried**, and no narrowing was called for. What it does create is a coupling: anything later added
to `isExpectedSslErrorCode` becomes retryable. `TestTlsHandshakeRetryClassifier.h` guards it by
pinning `connection_reset` and `operation_aborted` as non-retryable in the *retry* classifier.

**3. `7960078` - the evidence.** S0.2's test-only `RetryableHandshakeStream` was deleted, so
`TcpPreHandshakeStageTls_RetryableHandshakeErrorTests` and
`..._StageRunsOncePerAttemptTests` now drive `TcpSslSocketAsyncBase` itself. Measured on all three
sides, clang debug on Linux a64, boost 1.90 / OpenSSL 3.5.4:

| classifier | the two retry cases |
|---|---|
| production, before the fix | **abort** at the peer's 30s accept deadline - no second connection is ever made |
| test-only widening, after the fix | pass, 12/12 and 13/13 assertions |
| production, after the fix | pass, **12/12 and 13/13** - identical |

So the retry is reachable with a real peer, and it is reachable *without* any test-only policy.

Validated clang debug on `utf_baselib_http2` (27 cases), `utf_baselib_http` (37) and
`utf_baselib_tasks2` (13); all green, no leak line, no `FATAL`, no ThreadSanitizer report. The gate
of design 3.8 - the whole suite on the full matrix - is the orchestrator's, not this lane's.
