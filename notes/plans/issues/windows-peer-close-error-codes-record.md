# Windows reports a peer closing the connection with different error codes than POSIX

**Status:** the divergence is permanent - it is in the operating system, not in this library. The
library's answer is `net::isPeerClosedErrorCode()` and `net::isOrderlyPeerCloseErrorCode()` in
`src/include/baselib/core/NetUtils.h`. **Networking code must ask those and must not compare
transport error codes by hand.** **Superseded in part 2026-09-23 - see the section directly
below: the divergence this record measured was self-inflicted; the rule stands.**

This has been paid for three times. Each time the symptom was different, the diagnosis took a full
Windows matrix run, and the cause was the same.

## Superseded in part, 2026-09-23 - the divergence this record measured was self-inflicted

**What changed.** `bb53bdd` on `lazari2` (*tasks: stop resetting our own peers when a task tears
its socket down*) changed `TcpSocketCommonBase::shutdownSocket( )` from `shutdown_both` to
`shutdown_send` (`TcpBaseTasks.h:357`). Shutting down the RECEIVE side makes the close abortive on
Windows - anything queued at `SD_RECEIVE` or arriving afterwards resets the connection, and the
reset discards the peer's unread receive buffer. Measured through the two control cases below, on
win-x64 and win-x86:

    shutdown_both   the reader saw 10054 / 10053 and 0 of 16384 bytes
    shutdown_send   the reader saw eof and 16384 of 16384 bytes

Linux reported `eof` with all 16384 either way. Every task in this library ended its socket through
that function, so every measurement in this record taken against a peer of this library was a
measurement of that call.

**Withdrawn.**

- *"the divergence is permanent - it is in the operating system, not in this library"* (the status
  line above). The divergence the control measured was manufactured here. What the operating
  system contributes is narrower: on Windows `SD_RECEIVE` resets on a later arrival where Linux
  does not, and a reset discards the unread receive buffer where Linux hands it over - both real,
  neither reachable by an orderly close once nothing here asks for `SD_RECEIVE`.
- *"The control is a hypothesis test ... its Windows arm requires the code to be one of the two AND
  not `eof`"*. Inverted by `bb53bdd`: `PeerCloseErrorCodes_PeerShutsDownWithUnreadDataTests` and
  `..._ReaderSendsAfterPeerShutdownTests` now assert `eof`, the orderly predicate and every byte on
  EVERY platform, and are a regression test for the teardown rather than a description of Windows.
- **The third row of "The three defects"** - the HTTP/2 driver's `connection_aborted`, ~1 run in 8 -
  was this mechanism exactly. The peer in those runs is the h2 test server, which ends a connection
  through `beginClose( )` and the policy's `onTaskStoppedNothrow( )` -> `shutdownSocket( )`
  (`Http2TestServer.h:446`, `:842`, `:898`); after `bb53bdd` its close arrives as `eof`.
  `H2Driver_OpeningWriteIsOneWriteTests` on win-x86-vc143-debug was the same mechanism seen from
  the other side: the peer's own teardown destroyed the 204 it had just sent.
- **The second row** - the TLS handshake retry's `connection_reset` - **was measured against a test
  peer which manufactured the reset itself, and what the platform does with an orderly close there
  is still not measured.** That peer is not `shutdownSocket( )`: `acceptAndShutdown( )`
  (`utf_baselib_http2/TestTcpPreHandshakeStageTls.h`) did one `async_read_some( )` of at most 1024
  bytes and then its own `shutdown_both` + `close( )`. Its comment said reading the hello "keeps
  this an orderly end of the stream rather than a reset"; a 10054 after that is consistent only
  with hello bytes still unread at the `shutdown_both` - one read need not take the whole
  ClientHello - which is the control's first scenario in the test peer instead of the library.
  **Now measured:** the peer reports what it reads and the hello is **1500 bytes**, so at least 476
  of them were always still queued at that `close( )`, which is abortive on BOTH platforms (RFC 2525
  section 2.17) before Windows adds the `SD_RECEIVE` reset on top. `111e3f9` on `tls-peer-fix`
  changes the peer to read the hello whole - the load-bearing half, since `shutdown_send` alone
  leaves the unread bytes - and to shut the send side down only. The Windows re-run which says what
  the code becomes is owed. See the 2026-09-23 correction in
  `tls-handshake-retry-unreachable-record.md` and the section after it.
- *"Linux hands queued bytes over before reporting the error"* was never measured. The Linux runs
  of the control never saw a reset; they measured a FIN - all bytes, then `eof`. That a Linux read
  returns queued data ahead of `ECONNRESET` is a reading of the kernel, and stays marked as such.
  **Measured since, on both platforms, 2026-09-24.** `Http1Driver_PeerResetsAfterACompleteKeepAliveResponseTests`
  (6a, `utf_baselib_httpclient7`) sends the last chunk of a complete response and only then resets,
  and asserts the chunk arrives: green on Linux (`edb6d96`, 15 of 15), so Linux does hand the queued
  octets over first; red on `win-x86` and `win-x64` vc143, 40 of 40 each, the body one chunk short,
  and a raw-socket probe agrees - a read with octets queued ahead of the RST is handed `WSAECONNRESET`
  instead of them, 10 of 10. On Windows a reset discards what arrived and was not yet read.

**Standing.**

- **The rule.** Do not compare transport codes by hand; ask `net::`. A real server, a middlebox, or
  any peer that calls `close( )` with our bytes unread (RFC 2525 section 2.17 - an origin answering
  413 from the head of a body it will not read does this) still resets us, on every platform, and
  the driver must still call that a peer close. `bb53bdd` removes the condition this library
  manufactured; it does not remove resets.
- **`isPeerClosedErrorCode( )` as coded.** `connection_reset` on every platform,
  `connection_aborted` where the stack spells a reset that our own send drew that way.
- **The Windows discard.** Measured on a reset we manufactured, but a reset is a reset: a caller on
  Windows does not get the bytes a reset threw away. `isCleanEndOfStreamErrorCode( )` (S6R.2's
  N2, branch `s6r2`) refuses to complete a message on either reset spelling, and that refusal is
  right on POSIX as well, where the bytes may have arrived but nothing can say the message did.

**Owed, and to whom.**

- **The Windows arms of `isOrderlyPeerCloseErrorCode( )`** - through
  `os::peerCloseWithUnreadDataIsReportedAsReset( )` and
  `os::peerCloseCanBeReportedAsConnectionAborted( )` - rest on *"On Windows the separation is not
  observable"*. For a peer that closes in an orderly way it now is: `eof`, as on POSIX. What the
  arms do today is make a genuine reset retryable on Windows and not on POSIX, bounded by
  `maxRetryCount + 1`. Not harmful; no longer justified by anything measured. **Keep them until
  the TLS test peer is changed to `shutdown_send` (or reads the whole hello) and the two retry
  cases are re-run on Windows with the diagnostic** - narrowing on a guess is how this record was
  opened. `TlsHandshakeRetryClassifier_RetryableErrorSetTests` pins both arms and changes with them.
  **The peer half was done on 2026-09-23 by `111e3f9` - it needed both alternatives and not either,
  because `shutdown_send` does not remove the unread bytes a close resets over. The re-run is still
  owed, and it no longer needs a diagnostic edit: `RetryableHandshakeErrorTests` reports the code
  the predicate was handed, so `--log_level=message` on the Windows matrix is the whole
  measurement.** **Re-run 2026-09-24 on `win-x86-vc143-debug`:** the peer reads a 1500-byte hello
  whole and the handshake against it ends `asio.ssl.stream:1`, a truncation - no `10054`. So the
  2026-09-21 `10054` was that peer's own reset, and these two Windows arms now rest on nothing
  measured. Whether to remove them is a decision, not a measurement, and it is recorded as one in
  `astra-remediation-owed-work.md`'s Windows rows. **Removed 2026-09-25 at `3dce6ae`, by the maintainer's
  decision (W5):** the orderly predicate admits `eof` alone on every platform,
  `os::peerCloseWithUnreadDataIsReportedAsReset( )` went with the arm it gated, and
  `isPeerClosedErrorCode( )` asks `os::peerCloseCanBeReportedAsConnectionAborted( )` directly, so the
  set it admits is unchanged. The retry stays reachable on Windows through the truncation - see
  `tls-handshake-retry-unreachable-record.md`, its last section.
- **Comments in `src/` that state the old mechanism as a platform property**, not edited here:
  `NetUtils.h`'s block above the predicates ("because the divergence is in the TCP stack and in the
  I/O model, below anything this library writes"; "confirmed by the PeerCloseErrorCodes_* control
  cases: shutdown_both leaves the receive side shut"); both `os::` predicate comments in
  `OSImplPlatformCommon.h` ("CONFIRMED by a control ... shutdown_both"); the header comment of
  `TestPeerCloseErrorCodes.h` ("shuts down both directions", "asserted in two arms", "REPORTED and
  not asserted" - all three false after `bb53bdd`'s own edit of that file);
  `TcpSslBaseTasks.h:308-330` and `Http2ConnectionTask.h:1477-1501`. On `s6r2`, N2's comments in
  `Http1ConnectionTask.h`, the third predicate's, and `PeerCloseErrorCodes_CleanEndOfStreamSetTests`'s
  prose ("the case above measured that and reports the number") attribute to Windows what is a
  property of any reset.
- **Provenance worth keeping.** This exact change was proposed on 2026-09-09 as candidate 1 of
  `windows-blobtransfer-cancel-handle-and-http-reset-flakes-plan.md` (remedy item 2: "use
  `shutdown( shutdown_send )` for the graceful path ... must not be made without the capture") and
  set aside when candidate 2 explained those flakes. The hazard was real on its own account; the
  measurement that settled it came two weeks later, from a different symptom.

## The divergence

"The peer went away" is one event. It reaches a caller under **four** different error codes
depending on the platform, the I/O model and what the connection was doing at the time.

| What was observed | POSIX | Windows |
|---|---|---|
| Peer closed, ordinary case | `eof` | `eof` |
| Peer went away during a TLS handshake | `eof` / truncation | **`connection_reset`** (WSAECONNRESET, 10054) |
| Peer closed during a full-duplex transfer | `eof` | **`connection_aborted`** (WSAECONNABORTED, 10053) |
| Orderly close of a TLS stream | `asio.ssl.stream:1` or `SSL_R_SHORT_READ` | same |

The left column is what was MEASURED in each case, not a claim about every close. The two
Windows-only rows are two observables and may well be **one mechanism seen twice** - see below.

**1. A peer close can arrive as a RESET.** Measured: a peer which accepted and then went away
mid-handshake produced `WSAECONNRESET` on Windows where Linux reported `eof` or a truncation, and
the handshake retry was unreachable there until the code was accepted. **The peer which produced
that was closing with at least 476 bytes of the client hello unread; `111e3f9` stopped it, and
what a peer that closes in an orderly way produces there is unmeasured - see the second-row bullet
above.**

An earlier version of this record explained it as "Windows sends RST where POSIX sends FIN when
data is unread". **That is not a platform difference** - Linux `close()` with unread data also sends
RST (RFC 2525 section 2.17, `LINUX_MIB_TCPABORTONCLOSE`). The mechanism is **not settled**, and the
hypothesis below covers this row too.

**2. A peer close can arrive as an ABORT rather than an end of stream.** Measured
(`category='system' value=10053`), mechanism **not settled**. An earlier version of this record
claimed it was a pending overlapped read being completed by the local stack on the peer's FIN.
**That is wrong**: Asio maps a stream-oriented receive completing with no error and zero bytes to
`eof` (`asio/detail/impl/socket_ops.ipp`, the "Check for connection closed" branch), which is the
ordinary graceful path. `10053` requires the connection to have been genuinely aborted.

**CONFIRMED by a control**, and it explains both Windows rows as ONE mechanism. Two cases in
`utf_baselib_http2` - `PeerCloseErrorCodes_PeerShutsDownWithUnreadDataTests` and
`PeerCloseErrorCodes_ReaderSendsAfterPeerShutdownTests` - stand up a loopback pair and tear the peer
down with this library's own `TcpSocketCommonBase::shutdownSocket()`, which is `shutdown_both`.
Measured on `win-x64`, identically under `ccl16` and `vc143`:

| Scenario | Windows | Linux |
|---|---|---|
| Peer shuts down with bytes still unread | **`system:10054`**, **0 of 16384 bytes delivered** | `eof`, 16384 of 16384 |
| Reader sends after the peer's shutdown | **`system:10053`**, **0 of 16384 bytes delivered** | `eof`, 16384 of 16384 |

So: shutting down the receive side resets the connection on Windows when anything is left to
arrive, and whether the RST surfaces as `10054` or `10053` depends only on whether a send of ours
was outstanding when it landed. Not two mechanisms - one, seen twice.

**The data loss is total, not partial.** An earlier version of this record said a RST "discards
unread receive data", which understates what was measured: with 16KB sent and none of it yet read,
Windows delivered **nothing at all** before reporting the code. Classifying the close as a peer
close ends the connection gracefully; it does not recover those bytes. A caller which needed them
has lost them, and on the HTTP/2 driver's read loop that means a response body can be short while
the task reports success.

The control is a hypothesis test, not a rubber stamp: its Windows arm requires the code to be one
of the two AND not `eof`, so a stack which reported an orderly end here would fail the case loudly
rather than pass it.

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
