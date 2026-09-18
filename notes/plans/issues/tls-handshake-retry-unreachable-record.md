# The TLS handshake retry is unreachable with a real peer on Boost 1.90 / OpenSSL 3.5

**Found:** 2026-09-17, while implementing slice **S0.2** of the HTTP/2 client plan (the pre-handshake
stage hook). **Status:** OPEN - latent pre-existing defect, deliberately not fixed there.
**Not introduced by that change**, and production code was left untouched.

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
