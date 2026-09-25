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

---

## Reopened on Windows (2026-09-21), and closed again

**The 2026-09-18 fix above closed this on POSIX only.** The first Windows run of the suite after the
HTTP/2 and HTTP-client work was pulled found `TcpPreHandshakeStageTls_RetryableHandshakeErrorTests`
and `..._StageRunsOncePerAttemptTests` failing on **every** Windows combination measured -
`a64-vc143-debug`, `a64-ccl16-debug`, `a64-vc143-release`, `a64-ccl16-release` and
`x64-vc143-debug` - with exactly the signature this record describes for the pre-fix state: the
peer's second `acceptAndShutdown()` is never reached and the case aborts at the 30s accept
deadline. The two cases took 32.07s each where their non-retrying neighbours took ~2s.

### Why the fix did not carry over

**A TCP stack property, not a protocol or library one.** A peer which accepts and then goes away
mid-handshake leaves data unread in the local receive buffer. Closing a socket in that state is an
*abortive* close on Windows - it sends RST, and the next local read fails with `WSAECONNRESET`.
POSIX sends FIN for the same sequence and the local side sees an orderly end of stream, which is
`asio::error::eof` or, through the TLS stream, `asio.ssl.stream:1`.

So all three codes the classifier accepted are POSIX spellings of one event, and Windows has a
fourth spelling for the same event which was not in the set. Measured from inside the predicate on
`win-a64-vc143-debug`, against the same peer the POSIX rows are written for:

    DIAGNOSTIC isProtocolHandshakeRetryableError:
        category='system' value=10054
        message='An existing connection was forcibly closed by the remote host'
        retryable=0

### What the distinction costs, and why it is platform-conditional

The retry predicate exists to separate **"the peer went away"** - transient, worth another attempt -
from **"a genuine protocol or certificate failure"** - permanent, not worth one. On POSIX those two
conditions have different codes and the separation is real, which is why
`TestTlsHandshakeRetryClassifier.h` pinned `connection_reset` as non-retryable and why that row
should stay.

**On Windows the separation is not observable.** The stack collapses both into `WSAECONNRESET`
before any library sees it. Refusing the code there is therefore not a stricter policy; it only
makes the retry unreachable, which is the very defect this record was opened for.

Hence the condition is asked of the platform and not spelled as an `#ifdef` at the call site:

    os::peerCloseWithUnreadDataIsReportedAsReset()      core/detail/OSImplPlatformCommon.h

a compile-time predicate beside `onWindows()` / `onLinux()`, named for the behaviour rather than
the operating system so that the call site reads as "can this platform tell me the difference?".
`TcpSslBaseTasks.h` consults it and retries `connection_reset` only where the answer is no.

**The blast radius is bounded and unchanged in kind.** The predicate is reached only while a
handshake is incomplete and only while `m_retries < m_maxRetryCount` (`TcpBaseTasks.h:1437`), so a
peer which resets on every attempt costs `maxRetryCount + 1` attempts and no more - the same bound
already accepted for a peer which truncates on every attempt. Nothing in RFC 9113 speaks to this;
handshake retry is transport policy, and both answers are compliant. What is at stake is design
5.1's establishment contract, which assumes a transient peer-side close is survivable - true on
POSIX since 2026-09-18, and true on Windows only with this change.

### Corrected 2026-09-23 - the mechanism above is withdrawn, and the arm now rests on a measurement owed

*"A TCP stack property, not a protocol or library one ... Closing a socket in that state is an
abortive close on Windows - it sends RST ... POSIX sends FIN for the same sequence"* was withdrawn
on 2026-09-22 by `2b4c61b` and the peer-close record - Linux `close( )` with unread data also
sends RST (RFC 2525 section 2.17) - but the withdrawal never reached this record. What actually
produces the reset, measured by `bb53bdd` on `lazari2` (2026-09-23) through `PeerCloseErrorCodes_*`
on win-x64 and win-x86: shutting down the RECEIVE side. Windows resets the connection when data is
queued at `SD_RECEIVE` or arrives after it, where Linux does not; `shutdown_both` asked for that,
and `TcpSocketCommonBase::shutdownSocket( )` now shuts down the send side only.

**This test's peer is not that function, and `bb53bdd` does not change it.** `acceptAndShutdown( )`
(`TestTcpPreHandshakeStageTls.h:133-148`) does one `async_read_some( )` of at most 1024 bytes and
then its own `shutdown_both` + `close( )`. Its comment says reading the hello "keeps this an orderly
end of the stream rather than a reset"; the 10054 measured above is consistent only with hello
bytes still unread at that `shutdown_both` - one `read_some( )` need not take the whole ClientHello
- which is the control's first scenario, in the test peer instead of the library. **Not measured.**
So the Windows row of this section was, plausibly, the same self-inflicted shape, and *"On Windows
the separation is not observable"* now rests on nothing measured against a peer that closes in an
orderly way: after `bb53bdd`, such a peer arrives as `eof` on Windows exactly as on POSIX. What
`os::peerCloseWithUnreadDataIsReportedAsReset( )`'s arm does today is make a genuine reset
retryable on Windows and not on POSIX, bounded as the paragraph above says.

**Owed before the arm is kept on a true premise or removed:** change `acceptAndShutdown( )` to
`shutdown_send` (or read until the hello is whole), re-run the two retry cases on Windows with the
diagnostic, and record which code the retry then sees. Until then the arm stays - removing it on a
guess is how this record was opened, twice. `TlsHandshakeRetryClassifier_RetryableErrorSetTests`
pins both arms and changes with the answer. **The first half was done on 2026-09-23 - the section
directly below - and it needed BOTH of the alternatives in that parenthesis, not either.**

### The peer was changed, 2026-09-23 - and it took both halves, not either

`111e3f9` on `tls-peer-fix`.

**What the peer used to do, and what it claimed.** `acceptAndShutdown( )` read once, with
`async_read_some( )` into a 1024 byte buffer, and then called `shutdown_both` + `close( )`. Its
comment said that reading the hello *"before shutting down is what keeps this an orderly end of the
stream rather than a reset - a reset would silently turn this into a different case"*. It did not.
The peer now reports what it reads, and on `ub24-a64-clang2010-debug`, boost 1.90 / OpenSSL 3.5.4:

    the pre-handshake peer read a client hello of 1500 bytes

1500 does not fit in 1024, and an `async_read_some( )` returns as soon as any bytes are there
anyway. At least 476 bytes of the hello were still queued when that socket was closed, on every run
of both cases, on every platform. The comment described an intention the code did not carry out.

**Why `shutdown_send` alone would not have been the fix.** A close with bytes still in the receive
queue is an abortive close on BOTH platforms - RFC 2525 section 2.17, and `bb53bdd`'s own message
says the same of the library: *"Our own close( ) with unread data can still reset"*. Shutting down
only the send side removes the `SD_RECEIVE` hazard; it does not remove the unread bytes underneath
it. Reading the hello whole is the load-bearing half. The send-side shutdown is the cheap second
half - this peer never reads again, so it costs nothing - and it is what
`TcpSocketCommonBase::shutdownSocket( )` now does, for the same reason.

**What it does now.** It reads the TLS record header, takes the body length from it, reads exactly
that many bytes, and asserts that what arrived is one complete `SSL3_MT_CLIENT_HELLO` and nothing
else - so *the client's first flight has been taken entirely* is checked rather than assumed. Then
`shutdown_send` + `close( )`. A client which ever fragmented its hello across records would fail
that assertion loudly instead of putting the case quietly back to closing with bytes unread.

**What the cases assert is unchanged**, and so is what they exercise. The peer still goes away
mid-handshake without answering, so the retry classifier must still call the failure retryable or
the second `acceptAndShutdown( )` is never reached. Only the code it is called on changes, and only
on Windows.

**What the Linux run establishes, and what it cannot.** `utf_baselib_http2` on
`ub24-a64-clang2010-debug`: 34 cases, no failure, no leak; the two retry cases at 3.5ms and 3.3ms,
nowhere near the 30s accept deadline, so both attempts are served and the retry happens. The case
now reports the code it ended on:

    the handshake against a peer which went away ended with category='asio.ssl.stream' value=1
    ('stream truncated')

That is the POSIX answer, and it is the answer the OLD peer gave here too - on Linux the FIN of
`shutdown_both` completes the client's read before the RST of the close arrives, which is why this
was only ever a Windows row. **A green Linux run therefore verifies nothing about Windows.** What it
verifies is that the case still exercises the retry after the change.

**Still owed, and now cheap.** Re-run `TcpPreHandshakeStageTls_RetryableHandshakeErrorTests` and
`..._StageRunsOncePerAttemptTests` on the Windows matrix at `--log_level=message` and read the two
reported lines. No diagnostic edit of `TcpSslBaseTasks.h` is needed any more: the code the predicate
was handed is reported by the case itself, read the same way the predicate reads it. If it is `eof`
or `asio.ssl.stream:1`, the 2026-09-21 row was this peer's own reset and the Windows
`connection_reset` arm rests on nothing measured; if it is still 10054 against a peer which now
closes in an orderly way, the arm is measured after all and the Windows row of this section stands.
Until that run says which, the arm stays. `TlsHandshakeRetryClassifier_RetryableErrorSetTests` opens
no socket, is untouched by `111e3f9`, and still pins both arms.

### The test asserts both arms

`TlsHandshakeRetryClassifier_RetryableErrorSetTests` now asserts `connection_reset` retryable where
the platform reports an orderly close as a reset and non-retryable where it does not, rather than
skipping the row on one of them. A skipped row goes vacuous silently; two arms cannot.

### Evidence

`win-a64-vc143-debug`, boost 1.90 / OpenSSL 3.5.4:

| | the two retry cases |
|---|---|
| before | **abort** at the peer's 30s accept deadline, 32.07s each |
| after | pass, **4.08s each** - the retry happens instead of the deadline firing |

The drop from 32s to 4s is the load-bearing evidence: it is the deadline no longer being reached,
not merely an assertion no longer being evaluated.

### Re-run on Windows 2026-09-24, and the arm removed 2026-09-25

**The re-run this record was waiting for came back clean.** `win-x86-vc143-debug`, both retry cases at
`--log_level=message`: the peer reads a 1500-byte hello whole, and the handshake against it ends

    the handshake against a peer which went away ended with category='asio.ssl.stream' value=1
    ('stream truncated')

That is a truncation, with no `10054`. So the 2026-09-21 row was that peer's own reset, and the Windows
`connection_reset` arm rested on nothing measured, which is the outcome the section above said would
settle it.

**Decided by the maintainer 2026-09-25, and done at `3dce6ae`:** `net::isOrderlyPeerCloseErrorCode( )` admits `eof`
alone on every platform; `os::peerCloseWithUnreadDataIsReportedAsReset( )`, which existed only to gate
that arm, is removed; `net::isPeerClosedErrorCode( )` keeps both reset spellings, asking
`os::peerCloseCanBeReportedAsConnectionAborted( )` directly for `connection_aborted`, so the set it
admits is unchanged on every platform. A handshake a peer RESETS is now refused on Windows as on POSIX,
bounded as before where it was retried.

**The retry is still reachable on Windows, and this is why that matters.** With the arm gone, both
cases pass on `win-x64-vc143-debug` in about 5s each - the peer reads the hello twice, so the second
attempt happens - ending on the truncation above, which `isStreamTruncationError( )` admits. That is
the load-bearing check: the 2026-09-21 fix measured a drop from a 32s deadline abort to 4s, and
removing the arm did not put the deadline back.

**"The test asserts both arms" is superseded.** `TlsHandshakeRetryClassifier_RetryableErrorSetTests`
now asserts `connection_reset` and `connection_aborted` non-retryable on every platform, and
discriminates on Windows for the first time: with the old arms put back, both of its new rows fail
there, and `PeerCloseErrorCodes_CleanEndOfStreamSetTests` fails with them. Evidence in
`http2-l0-state/logs/win-handoff/w5-control/` and `.../decisions/`, and the decision's reasoning in
`astra-remediation-owed-work.md`, row W5.
