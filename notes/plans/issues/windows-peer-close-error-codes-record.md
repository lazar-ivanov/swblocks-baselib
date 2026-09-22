# Windows reports a peer closing the connection with different error codes than POSIX

**Status:** the divergence is permanent - it is in the operating system, not in this library. The
library's answer is `net::isPeerClosedErrorCode()` and `net::isOrderlyPeerCloseErrorCode()` in
`src/include/baselib/core/NetUtils.h`. **Networking code must ask those and must not compare
transport error codes by hand.**

This has been paid for three times. Each time the symptom was different, the diagnosis took a full
Windows matrix run, and the cause was the same.

## The divergence

"The peer went away" is one event. It reaches a caller under **four** different error codes
depending on the platform, the I/O model and what the connection was doing at the time.

| How the peer ended it | POSIX | Windows |
|---|---|---|
| Orderly close, nothing unread | `eof` | `eof` |
| Orderly close, data still unread locally | `eof` | **`connection_reset`** (WSAECONNRESET, 10054) |
| Close landing while a read is outstanding | `eof` | **`connection_aborted`** (WSAECONNABORTED, 10053) |
| Orderly close of a TLS stream | `asio.ssl.stream:1` or `SSL_R_SHORT_READ` | same |

Two independent mechanisms produce the two Windows-only rows:

**1. The close is abortive when data is unread.** Closing a socket whose receive buffer still holds
bytes the peer sent makes Windows send **RST** where POSIX sends FIN. The local side's next read
then fails with `WSAECONNRESET` rather than reporting an orderly end of stream.

**2. A pending overlapped receive is completed by the local stack.** Windows I/O is overlapped, so a
read is lodged with the completion port before the peer's bytes arrive. A close which lands while
one is outstanding is completed by the **local** stack with `WSAECONNABORTED`, not by the peer's
FIN.

**Neither code means on Windows what its POSIX namesake means.** On POSIX a reset really is a reset
- a refusal, distinguishable from a clean close and worth treating differently - and `ECONNABORTED`
is an `accept()` error which a read never produces at all.

**Mechanism 2 is a race**, which is what makes it expensive to find: whether a read happens to be
outstanding at the instant the peer closes varies run to run, so code which does not expect the
code fails INTERMITTENTLY rather than every time. The HTTP/2 driver defect below sat at roughly one
run in eight and was measured at 7 failures in 60 idle runs and 8 in 60 under load. Those rates are
close enough to rule OUT load sensitivity, which is what the measurement was for - the race is
between the peer's close and our own read, not between us and the machine. They are not identical
and 60 runs cannot resolve a difference that small.

## The three defects

| Found | Symptom | Missing code |
|---|---|---|
| 2026-09-17 | TLS handshake retry unreachable against any real peer, everywhere | `asio.ssl.stream:1` - the modern truncation spelling |
| 2026-09-21 | TLS handshake retry unreachable on Windows only | `connection_reset` |
| 2026-09-22 | HTTP/2 driver failed a connection its peer had closed normally, ~1 run in 8 on Windows | `connection_aborted` |

The first is recorded in
[tls-handshake-retry-unreachable-record.md](tls-handshake-retry-unreachable-record.md) and is the
same shape without the platform angle: one event, more than one spelling, a predicate which knew
only some of them.

Each was found by a matrix run rather than by review, because on the platform the code was written
on it is correct.

## What the library does about it

**One place knows the codes:** `bl::net`, in `core/NetUtils.h`. It offers two predicates, and which
one a call site wants depends on the question it is asking - this distinction is the whole design
and getting it wrong reintroduces a defect at the other call site:

| Predicate | The question | POSIX admits | Windows admits |
|---|---|---|---|
| `isOrderlyPeerCloseErrorCode` | *Did the peer finish cleanly, so is this transient and worth another attempt?* | `eof` | `eof`, `connection_reset`, `connection_aborted` |
| `isPeerClosedErrorCode` | *Is the conversation over, however it ended?* | `eof`, `connection_reset` | `eof`, `connection_reset`, `connection_aborted` |

They differ on exactly one thing: **a reset on POSIX.** It ends the connection, so the second
admits it; it is not a clean finish, so the first does not. Collapsing them into one predicate would
either make resets retryable on POSIX - the attempt storm the retry classifier's negatives exist to
prevent - or make the HTTP/2 read loop fail a reset connection it should simply stop reading.

**The handshake retry admits `connection_aborted` too, on Windows, and that was NOT one of the three
defects above.** It followed from sharing `isOrderlyPeerCloseErrorCode()` between the two call
sites, and it is kept deliberately rather than narrowed: a handshake has a read outstanding exactly
as a steady-state connection does, so a peer closing under it is renamed by mechanism 2 there too.
Refusing it on Windows would be the 2026-09-21 defect - "retry unreachable on Windows" - in a second
form. It is pinned by `TlsHandshakeRetryClassifier_RetryableErrorSetTests` rather than left implied.

**Neither predicate can be checked on Windows alone.** On Windows the two admit the IDENTICAL set,
because both platform facts are true there; they diverge only on a POSIX reset. So no Windows run,
of any length, can tell whether a call site picked the right predicate - only the POSIX arm
discriminates. Assert both arms of every platform-dependent row, as that test does, so an ordinary
Linux build checks what the Windows matrix structurally cannot.

**The platform facts live in `bl::os`**, beside `onWindows()` and `onLinux()`, named for the
behaviour rather than the operating system:

- `os::peerCloseWithUnreadDataIsReportedAsReset()` - mechanism 1
- `os::pendingReceiveOnPeerCloseIsReportedAsAborted()` - mechanism 2

Both are compile-time constants, so the branches fold away and neither platform pays for the other.
Call sites should not ask these directly; they ask `net::`, which is where a fact about a TCP stack
becomes a decision about an asio error code.

**What is deliberately NOT in either set:** `operation_aborted`. That is what our own
`initiateClose()` and an external `cancelTask()` produce, and telling a deliberate close from a peer
close is what the accounting of http2 design 3.2 exists for. Admitting it would make a cancelled
task look like a finished one.

**TLS truncation is not in either set either.** Which codes mean a truncated TLS stream is knowledge
the stream policy owns - it depends on the ASIO and OpenSSL versions baselib was built against - so
ask `STREAM::isStreamTruncationError()` alongside the `net::` predicate, as both call sites do.

## The rule

**Do not compare an asio transport error code by hand in networking code.** Not
`connection_reset`, not `connection_aborted`, not `eof`. Ask `net::isPeerClosedErrorCode()` or
`net::isOrderlyPeerCloseErrorCode()`.

If neither fits, add a third predicate **in `core/NetUtils.h`** with its reasoning, rather than
open-coding the comparison at the call site. The point is not that the two predicates cover every
case forever; it is that there is one file to read and one file to change when a fourth spelling
turns up.

**A Linux-only run cannot catch a breach of this rule**, and a Windows run may need dozens of
iterations because mechanism 2 is a race. A change to transport error handling should be run on the
Windows matrix, and an intermittent network failure there should be suspected of this before
anything else.

**Known remaining, not cleaned up by this change.** `tasks/TcpBaseTasks.h::isExpectedSocketException`
still compares these codes by hand AND hard-codes the numeric 10053/10054 for Windows. It predates
this work and answers a different question - whether an exception is worth logging - so it was left
alone rather than changed without a test to justify it. It is, however, exactly the pattern this
rule forbids going forward, and it is the obvious next candidate if anyone applies the rule
retroactively.
