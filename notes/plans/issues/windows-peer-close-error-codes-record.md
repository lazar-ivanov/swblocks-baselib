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

**2. A peer close can arrive as an ABORT rather than an end of stream.** Measured
(`category='system' value=10053`), mechanism **not settled**. An earlier version of this record
claimed it was a pending overlapped read being completed by the local stack on the peer's FIN.
**That is wrong**: Asio maps a stream-oriented receive completing with no error and zero bytes to
`eof` (`asio/detail/impl/socket_ops.ipp`, the "Check for connection closed" branch), which is the
ordinary graceful path. `10053` requires the connection to have been genuinely aborted.

The leading hypothesis, **not yet confirmed by a control**: every task shuts down with
`shutdown_both` (`TcpBaseTasks.h:325`), and on Windows shutting down the RECEIVE side resets the
connection if data arrives afterwards. A frame sent after the peer did that - a late
`WINDOW_UPDATE` - would draw a RST, and a RST in reply to our own send completes an outstanding
receive with `10053`. If that holds it carries a consequence: a RST also **discards unread receive
data** on Windows, where Linux hands queued bytes over before reporting the error - so accepting
the code ends the connection gracefully but does not recover what the reset threw away.

**Neither code means on Windows what its POSIX namesake means for a read.** On POSIX a reset
reaching a read still hands over whatever was already queued before reporting the error, and
`ECONNABORTED` is an `accept()` error a read never produces at all.

An earlier version of this record said "Windows sends RST where POSIX sends FIN when data is
unread". **That is not a Windows/POSIX difference** - Linux `close()` with unread data also sends
RST (RFC 2525 section 2.17, `LINUX_MIB_TCPABORTONCLOSE`). What differs is `shutdown(SD_RECEIVE)`
resetting on subsequent arrivals, and the receive-buffer discard above.

**Mechanism 2 is a race**, which is what makes it expensive to find: whether a read happens to be
outstanding at the instant the peer closes varies run to run, so code which does not expect the
code fails INTERMITTENTLY rather than every time. The HTTP/2 driver defect below sat at roughly one
run in eight and was measured at 7 failures in 60 idle runs and 8 in 60 under load. Those rates are
close enough that the measurement found no evidence of load sensitivity. That is weaker than it
first reads: each rate carries a 95% interval of roughly 5-24%, so even a twofold difference would
survive these samples. What the pairing does support is that the race is between the peer's close
and our own traffic rather than between the process and the machine.

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
- `os::peerCloseCanBeReportedAsConnectionAborted()` - mechanism 2

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

**Not an outlier, contrary to an earlier version of this record.**
`tasks/TcpBaseTasks.h::isExpectedSocketException` uses the PORTABLE comparison -
`eh::isErrorCondition( eh::errc::connection_aborted, ec )` at :190 - which already matches 10053 on
Windows. The numeric WSA block beneath it is belt-and-braces, redundant rather than wrong. It
answers a different question anyway (is this exception worth logging), so it needs no change.
