# What the astra remediation leaves owed

**Date:** 2026-09-23. **Status:** the consolidated list. Items here are **owed, not abandoned** —
each names what picks it up and, where one exists, the condition that unblocks it.

Written because the owed items had scattered across eight records over four days, and a reader
asking "what is left" had no single place to look. Each entry points at the record that holds the
reasoning; none of it is restated here.

---

## Sequenced, and unblocked by something specific

**ALL THREE LANDED — this section is history, kept for its reasoning.** *Marked 2026-09-24; it had
still read as pending.*

| # | Item | Landed | Where the reasoning lives |
|---|---|---|---|
| 1 | **The uncharged-retire bound** | `24db294`, merged | `pool-uncharged-retire-recursion-record.md` |
| 2 | **H04a**, the driver-pointer publication | `08e18b9`, merged at `4d70076` | `s6r3-design.md` §4.1, §12 |
| 3 | **The h1-over-TLS control case** | `42347a6`, merged at `7e2a1a0`, with the four degradation controls it first shipped without | `initiate-close-teardown-design.md` §13, §17 |

**Why H04a waits on the bound, since "deferred" reads as "dropped".** Under today's code a wrong gate
crashes the module with a stack overflow, so the gate cannot be iterated on. Under the bound the
recursion terminates at `maxRetriesPerRequest + 1` and a wrong gate produces a **red assertion**
instead. H04a is an unsynchronised smart-pointer publication and a genuine hazard on a64 — sighted
three times since L5, with a 32-run TSan negative and a recorded reason why that negative is not a
refutation. A tolerable carry for one change-set; not a tolerable carry indefinitely.

**The shape H04a should take** is already specified in §4.1: read the task connection's state **once**
per examine, and make both the poll and the retire decision from that single reading. Both the gate
that crashed and the alternative the design had rejected read it twice.

**And a warning for whoever retries it:** the bound makes H04a safer to attempt and also makes a
wrong gate **quieter**. The crash is what made this defect findable at all. §12's first lesson — read
the gate against the fixture that pins the feature and ask whether it ever opens — is what replaces
the crash as the detector.

## Owed to the Windows matrix, and not closeable here

| # | Item | What settles it |
|---|---|---|
| 4 | **N2's Windows arm** | **RAN 2026-09-24 — no breach.** `win-x86` and `win-x64` vc143 debug, the h1 driver's seven peer-close cases, 40 runs each: every reset spelling was classified as a peer close. Two reds, neither of them the rule's — 6a's Windows premise and R2's harness race, **W2** and **W3** below |
| 5 | **The TLS peer's steps 2 and 3** | **SETTLED 2026-09-24:** the peer reads a 1500-byte hello and the handshake against it ends `asio.ssl.stream:1`, a truncation, with no `10054` — so the 2026-09-21 row was that peer's own reset, and the Windows `connection_reset` arm rests on nothing measured. What to do about those arms is **W5** below |
| 5a | **`m_wasSocketShutdownForcefully`'s own red** — the one piece of the teardown design's §2.3 a Linux run **provably cannot reach**. Dropping the flag left the task clean in all 35 runs that reached the assertion, because the h1 driver task never performed the handshake (the establisher did), so `isExpectedException( )` takes its `! m_isHandshakeCompleted` arm and `isExpectedSocketException( )` lists `broken_pipe` | **EARNED ON WINDOWS 2026-09-24** — with the flag's assignment removed the case is **40 of 40 red** on `win-x64-vc143-debug`, `WSAESHUTDOWN` out of the send of the TLS `shutdown_op`, which the Windows half of `isExpectedSocketException( )` does not list where Linux's `broken_pipe` is; baseline 40/40, revert 10/10. §2.3 holds on Windows. Recorded in §17. *Before:* **only Windows, or the h2 driver**, which handshakes for itself. Recorded at the point of use in `initiate-close-teardown-design.md` §17 |
| 5b | **A1-cleartext's Windows ordering** — §12.5 called the write's recorded code "redundant and harmless" there; it is redundant and **not** harmless in one ordering, because **Winsock has no CLOSE_WAIT→EPIPE rule**, so a write's reset spelling there cannot prove the ending carried no FIN, and a peer that half-closes and only then aborts would have its FIN-framed message reported as a reset rather than completed. *Premise corrected 2026-09-24 by the ledger sweep: this row argued from "Windows collapses an orderly close into the reset spellings", which `windows-peer-close-error-codes-record.md` **withdrew** on 2026-09-23 as self-inflicted by our own `shutdown_both`. The conclusion is unchanged; `windows-matrix-handoff.md` item 4 carries the corrected reasoning* | **MEASURED, AND THE NARROWING TAKEN at `ed0ced5`, reviewed and accepted by the maintainer.** `driver-read-write-arms-design.md` §13.3's own decision rule is met: through the driver, the read completed a reset spelling for itself in every reset run of R1, A2's case and the older reset case, 15 of 15; and R2's peer aborting straight after its FIN is red before and green after wherever the read took the FIN as `eof`. `os::peerResetIsReportedToEveryOperation( )`, true on Windows, gates `isPeerResetOnWriteErrorCode( )`. Must-not-move, 50 runs each of `httpclient7` and `httpclient5` on x64 vc143 debug, x64 ccl16 debug, x86 vc143 debug and x64 vc143 release: every case but three passed every run, and the three are **W2**, **W3** and **W4** — 6a 50 of 50 on every build, R2's record race 10 to 17, the idle-close bound 3 to 8, with the load setting the rates — none of which the narrowing is on the path of. *Before:* the matrix. **Conservative, not a truncation-as-success.** A one-line narrowing exists and is named in the predicate's comment; whether to take it is the reviewer's and the matrix's |
| 5c | **`utf_baselib_httpclient6`'s x86 debug object.** H22's two cases took it from 39.06 to **42.35 MB on a64 clang debug — 2.4 MB over the 40 MB target**, accepted by the maintainer 2026-09-24 with a recorded reason: every alternative measures worse (`httpclient5` 47.43, `httpclient4` 48.98), and a sibling pays the ~21 MB TU floor again for no change in peak because the weight is the session plus the peer, not the cases. The product fix itself is **+3,584 bytes**; the rest is test weight | **MEASURED 2026-09-24 on `win-x86-vc143-debug`:** `httpclient4` **56.4 MB** (75% of the ceiling — the family peak), `httpclient5` 55.6, `httpclient6` **47.9 — 7.9 MB over the 40 MB target** on the calibration platform, not the 2.4 MB the a64 figure gave. Nothing in the family is near the ceiling; the tree's tightest object is `io` at 72.3 MB, and 25 of its 45 modules are over target. *Before:* **the matrix — but it is NOT the deciding number, and this row first said it was.** *Corrected 2026-09-24 by the H22 implementation review:* the ceiling question is the **family peak**, which is `httpclient4` at 49.0 MB, not `httpclient6` at 42.4 — **if 6 were anywhere near 75 on x86, 4 and 5 would already be over it.** So the choice stands on its own arithmetic and x86 would confirm rather than decide. What is genuinely owed is that **x86 debug is unmeasured and unrecorded in `notes/` for any of modules 4, 5 and 6**, and `x86` `release` is governed by optimizer memory rather than object size, so no figure here speaks to that combination at all |

Item 5's step 1 landed at `2a4ad5f`. Both cases pass either way, so **the reported code settles it**:
`eof` or `asio.ssl.stream:1` means the 2026-09-21 row was that peer's own reset and the Windows
`connection_reset` arm rests on nothing measured; a persisting `10054` against a now-orderly peer
means it was measured after all. **Measured 2026-09-24: `asio.ssl.stream:1`** — row 5.

### Found by the Windows matrix, 2026-09-24

**The first run of `windows-matrix-handoff.md` on a Windows host** — `win-x86-vc143-debug` and
`win-x64-vc143-debug`, with `win-x64-ccl16-debug` and `win-x64-vc143-release` where a toolchain or a
variant was the question. One product defect, fixed; three test-harness defects, each a decision; two
decisions that the handoff's own items turned into; and the rest settled. **Every measurement cited
here is in `http2-l0-state/logs/win-handoff/`, outside the repository, under the item it belongs to.**

| # | Item | Status |
|---|---|---|
| W1 | **No HTTP/1.1 request over TLS ever left the client on Windows.** `AsioSslStreamWrapperT` took its completion handler BY VALUE, as it had since the initial commit, and asio's composed write passes that handler in the same call that reads the handler's own buffer member — `stream_.async_write_some( buffers_.prepare( max_size ), std::move( *this ) )` — in an argument order the language leaves unspecified. MSVC and clang-cl move the handler first, on x64, x86 and a64, so a `std::vector< const_buffer >`, which the HTTP/1.1 driver hands `async_write( )` even for a GET, arrives empty and the write completes SUCCESSFULLY with zero octets. Measured under cdb (`bytes_transferred` 0 for a 47-octet head) and proved without TLS by a mock-stream probe on all six Windows configurations. clang's IR for Linux evaluates `prepare( )` first, so no Linux clang run could have seen it; GCC is unmeasured. Latent on `master`; live on `lazari2` since the HTTP/1.1 driver, the first caller with a vector | **FIXED** at `9ca4678`: both forwarding functions take the handler by forwarding reference, as asio's own streams do. `httpclient5` went from 3 of 8 cases and a hang to 8 of 8 on x86 and x64 vc143 and x64 ccl16; the whole x86 tree rebuilt clean — 53 of its 55 objects include the header — and the whole x86 suite ran 1050 of 1050 cases with every tier-3 difference accounted for without it (**W9**). **Owed to Linux: run the probe (`wrapper-fix/asio_probe.cpp`) under GCC**, x64 and a64 — the only toolchain whose order is unknown |
| W2 | **6a cannot arrange its premise on Windows.** `Http1Driver_PeerResetsAfterACompleteKeepAliveResponseTests` relies on Linux handing over octets queued ahead of a RST, and a Windows reset discards them (raw socket, 10 of 10), so the body arrives one chunk short — **40 of 40 red on x86 and on x64**, and 50 of 50 in every must-not-move loop. It is the harness's premise that fails, not the verdict it guards | **DECIDED 2026-09-25 by the maintainer, as recommended, and DONE - see the decisions below:** skip it on Windows with `UTF_SKIP_UNLESS` and the measured reason, which keeps it compiled and records the skip; the verdict logic is platform independent and Linux covers it with a real peer. Reverses if a Windows arrangement is found, without a seam, in which the read completes the message after the write handler has recorded a reset |
| W3 | **R2's peer records are read before the peer writes them.** `runHalfCloseDuringBlockedUpload( )` snapshots `peer.records( )` with nothing ordering it after the peer's `self.record( "fin:sent" )`, which follows the FIN onto the wire — so a client that finishes first reports *"the peer did not half close"*. 2/40 on x86 and 5/40 on x64 lightly loaded, 10/50 and 17/50 under load, **0/50 isolated** | **DECIDED 2026-09-25 by the maintainer, as recommended, and DONE - see the decisions below:** wait for the record before the snapshot — a `PEER_RECORDS_AFTER_THE_FIN` beside the neighbouring cases' own constants — with a delay injected between the FIN and the record as its red. R2 is the deferral's hang-freedom control and part of 5b's must-not-move evidence, so every false red here costs the evidence reading |
| W4 | **`waitForTaskEndWithin( )` counts iterations, not time.** Its absence bound adds the nominal 20 ms per poll to `waited`, so a poll that takes longer — under load, and plausibly from Windows sleep granularity, which is unmeasured — stretches the 125 ms window of `Http1DriverTls_IdleCloseSendsCloseNotifyTests` past the 250 ms idle close it guards, and the case reports *"the connection ended before half its idle lifetime had passed"*. 3/50 and 5/50 under load, 1/50 isolated. The case issues no write, so neither W1 nor 5b is on its path | **DECIDED 2026-09-25 by the maintainer, as recommended, and DONE - see the decisions below:** measure elapsed time against a steady clock, and count only an observation made inside the window — in this helper and in `Http1DriverTestUtils.h`'s `waitForTaskEnd( )`, which its comment calls the same helper |
| W5 | **The Windows arms of `isOrderlyPeerCloseErrorCode( )` rest on nothing measured.** Row 5's re-run was the condition `windows-peer-close-error-codes-record.md` set for keeping them, and it came back clean. **This is the item's second recording, which by AGENTS.md makes it a decision** | **DECIDED 2026-09-25 by the maintainer, as recommended, and DONE - see the decisions below: narrow the orderly predicate alone**, to `eof` on every platform, and keep both reset spellings in the wide one; its own change-set, after W2 to W4. Reverses if a Windows run shows an orderly, non-resetting close arriving as `10054` or `10053`. Its only production caller is the TLS handshake retry (`TcpSslBaseTasks.h:330`), and `isPeerClosedErrorCode( )` is built on it, so removing the `connection_aborted` arm would remove it from the wide predicate too. The shape is the question: narrow the orderly predicate alone and keep both reset spellings in the wide one, or keep the arms as a deliberate, bounded difference and close the item |
| W6 | **Handoff item 5** — `utf_baselib_h2client6/TestHttp2DriverWritePeerClose.h` compiled out on Windows | **Premise MEASURED** at the Winsock level: after a FIN and then an ordinary close over our unread upload, a parked send completes `WSAECONNRESET` — a code the read-side predicate already admits — and never `EPIPE`, 20 of 20. **DECIDED 2026-09-25 by the maintainer, as recommended, and DONE - see the decisions below:** record the exclusion as measured and close the item; a Windows-shaped equivalent would be green before and after for exactly that reason |
| W7 | **Handoff item 6** — the `WSAESHUTDOWN` route through the TLS cancel-close cases | **NOT OBSERVED.** The parked TLS write completed `operation_aborted` (995) in 30 of 30 cdb runs of the two write-in-flight cases on x64. On cleartext R2 the write does complete `WSAESHUTDOWN`, from our own `shutdown_send`, and `isOurOwnTeardown` excuses it |
| W8 | **Handoff item 7** — the TLS spelling of a write's reset | **MEASURED 2026-09-25 and CLOSED, as the accepted recommendation said this answer would: `connection_reset`, `system:10054`, passed through the engine unchanged** — as the write's cancel is, and as `driver-read-write-arms-design.md` §13.9 read in asio. 5 of 5 cdb runs on `win-x64-vc143-debug`, through a temporary edit that had `Http1DriverTls_WriteInFlightCloseSkipsCloseNotifyTests`' peer let the 8 MB upload park for 500 ms and then reset (`SO_LINGER( on, 0 )` and close) instead of answering, reverted byte for byte and the module rebuilt. Every run the write completed 10054 after 188,416 octets and the read completed 10054 too, in either order, and `onPeerClosed( )` ran once, from `onReadCompleted( )` - the read reporting the ending, as `ed0ced5` has it on Windows. Both predicates already admit the code, so nothing changes. W1 is what made it reachable |
| W9 | **Tier 3 on Windows** | **Rendering settled:** a refused comparison prints `tier3  SKIP  <reason>` with no CR under Git Bash, and the tree is spelled `win-x86-vc143-debug`, so tier 3 runs; the Python tools' own lines are CRLF in a captured log, which is cosmetic. **The comparison:** 620 differences before W1 and 626 after, of which 618 are the **baseline's age** — cases registered since its capture, `httpclient5`'s eight among the newly run once W1 let them — and two are cases edited since it (`0da54dc`, `d7f5ef0`), the two assertion shifts both runs showed. The rest: `BaseLib_SortedVectorHelperTests`, whose count varies on an unchanged binary (160605, then 160602 four times) and belongs under `observed`; `BlobTransfer_FilesPackagerInMemoryCancelDownloadTests`, 36 under that run's load and 38 in five light runs; and W2's and W4's cases with their modules. The first run's `IO_SslSimpleConnectAndTransmitDataMessageDispatcherOutgoingTests` abort did not recur and is **unexplained** — the SSL twin of a name already under `observed`. 27 of the 44 modules are uncovered, where the handoff expected 17 — *found 2026-09-25: the other ten are the split's own siblings, uncovered only because the baseline predates them, and holding 135 cases it still checked tree-wide.* **DONE 2026-09-25, by the maintainer's decisions below:** the tool no longer reports an uncovered module's cases as added and no longer calls a module it never saw covered; the baseline is refreshed over **28 modules**, the 17 and their 11 numbered siblings, with both names under `observed`; `uncovered.json` gives the reason for each of the 16 client modules still left out. The io SSL abort was **not seen in 50 runs** and is closed as recommended, still unexplained |

#### The decisions of 2026-09-25, as they were put and as they were taken

Each was presented in AGENTS.md's shape - what it is, what happens if it is not done, the risk and the
blast radius, the part left undecided, a recommendation and what would reverse it - and each was
accepted as recommended. They are kept here whole because a decision's reasoning is what its reversal
condition gets read against later. Ordered as they were put: the two harness races first, because
every false red they produce is noise in every other piece of Windows evidence, including 5b's.

**W3 - R2's peer-record race.** *What it is:* `Http1Driver_PeerHalfClosesWithAWriteInFlightTests` pins
that the driver cannot hang when a peer stops sending while our upload is still in flight. Its peer
writes "fin:sent" only after the FIN is on the wire, and the harness read the peer's records with
nothing ordering that read after the write, so a client that finished first reported a half close
that had happened as one that had not. *If not done:* 10 to 17 false reds in 50 runs under load on
Windows (0 of 50 isolated), in a case that is part of 5b's evidence. *Risk and blast radius:* test
only - one named constant and one bounded wait, the neighbouring cases' own pattern; one helper, one
case; it cannot change product behaviour, and a peer which never records still fails with the same
text. *Decided:* fix it, shown red first. *Reverses:* nothing foreseen. **Done** at
`900fe23`: `PEER_RECORDS_AFTER_THE_FIN` and a `waitForRecords( )` before the snapshot. Red 3 of 3 with a 300 ms
delay between the peer's FIN and its record and the wait disabled; green 3 of 3 with the wait
restored - certain rather than lucky, since the client finishes in milliseconds. `httpclient7` 50 of
50 on x64 vc143 under load, where it had been 0 of 50.

**W4 - the absence bound.** *What it is:* `Http1DriverTls_IdleCloseSendsCloseNotifyTests` checks that
a connection is still alive halfway through its 250 ms idle lifetime, and its helper added the nominal
20 ms per poll instead of measuring time, so a busy machine stretched the "halfway" window past the
close it guards. *If not done:* 3 to 8 false reds in 50 under load, 1 isolated, in `httpclient5`.
*Risk and blast radius:* test only - the helper and its cleartext twin. *Undecided was:* whether to
change the shared twin too; yes. *Decided:* elapsed time against a steady clock, counting only an
observation made inside the window. *Reverses:* nothing foreseen. **Done at `1184eb4`, in all THREE copies** - the
third, `utf_baselib_h2client6/TestHttp2DriverWriteBarrier.h`, was found while doing it: the same code
with the same defect. Red 3 of 3 with every sleep made to overrun threefold under the old loop, green
3 of 3 under the new one - certain, since the overrun alone stretches the old window past the 250 ms
close. The other callers ask whether a task ENDED within a bound an order of magnitude above what
they wait on, which the new rule makes at most one poll stricter.

**W2 - 6a on Windows.** *What it is:* the case guards a fixed defect - a connection offered for reuse
after the peer reset our upload - and arranges it by having its peer send the last chunk of a complete
response and then reset, which relies on Linux delivering octets queued ahead of a RST. Windows
discards them, so the response arrives short and the case fails at its premise, before the verdict it
guards. *If not done:* `httpclient7` red on every Windows run, so it can never flag a real regression
in its other cases there. *Risk and blast radius:* test only, one case, Windows only. What is given up
is Windows coverage of that ordering, which Windows reaches only through two I/O threads racing into
the strand - arrangeable by no real-peer test - while the verdict logic is platform independent and
keeps its real-peer red and green on Linux. *Undecided was the shape:* a runtime skip, compiling it out
as `utf_baselib_h2client6` does, or a seam-based Windows variant, which is more code and against the
case's own no-seam design. *Decided:* the runtime skip, with the measured reason. *Reverses:* a Windows
arrangement without a seam in which the read completes the message after the write handler has
recorded a reset. **Done** at `900fe23`: `UTF_SKIP_UNLESS( ! os::peerResetIsReportedToEveryOperation( ), ... )`, so
the case still builds on Windows and the skip is recorded where tier 3 counts it; on Linux the
condition is false and it runs as before.

**W6 - item 5's exclusion.** *What it is:* `utf_baselib_h2client6/TestHttp2DriverWritePeerClose.h` is
compiled out on Windows on the claim that a send there takes codes the read side already admits.
*Measured:* after a peer's FIN and then its RST, or its ordinary close over our unread upload, a
parked send completes `WSAECONNRESET`, never `EPIPE`. *Decided:* record the exclusion as measured and
close the item. *Reverses:* the h2 cases' premise changing. **Done** at `bb67eaf`: the comment at the exclusion
carries the measurement, and `NetUtils.h`'s `isPeerClosedOnWriteErrorCode( )` no longer calls the
Windows send spelling owed - there is no third one.

**W5 - the orderly predicate's Windows arms.** *What it is:* `isOrderlyPeerCloseErrorCode( )` made the
TLS handshake retry treat a reset as an orderly close on Windows only, on a premise withdrawn on
2026-09-23 that row 5's re-run then found nothing to support. *If not done:* a genuine reset during a
TLS handshake retried on Windows, up to the retry limit, and refused elsewhere - harmless, unjustified,
and recorded a third time. *Risk and blast radius:* the retry decision of every TLS handshake on
Windows, and the definition of the wide predicate every Windows peer-close classification asks, which
had to keep accepting `10053`. *Undecided was the shape:* narrow the orderly predicate alone, or keep
the arms as a deliberate, bounded difference and close the item. *Decided:* narrow, as its own change
after W2 to W4. *Reverses:* a Windows run showing an orderly, non-resetting close arriving as `10054`
or `10053`. **Done** at `3dce6ae`: the orderly predicate admits `eof` alone everywhere; the wide one asks
`os::peerCloseCanBeReportedAsConnectionAborted( )` for `connection_aborted` directly, so its set is
unchanged on every platform; `os::peerCloseWithUnreadDataIsReportedAsReset( )` is removed, having
existed only to gate the arm. `TlsHandshakeRetryClassifier_RetryableErrorSetTests` and
`PeerCloseErrorCodes_CleanEndOfStreamSetTests` assert the new rows on every platform: with the old
arms put back both fail on Windows at exactly those rows, and without them both pass. The two TLS
retry cases still pass on Windows in about 5 s, through the truncation - the retry is reachable
without the arm. Linux is
unchanged by construction: POSIX never had the arms, and the wide predicate's set is the same.

**Validated by focused testing, as the maintainer asked, with the full matrix left for later.** The
whole `win-x86-vc143-debug` tree rebuilt clean, nothing referencing the removed fact; the five
modules these changes reach - `http2`, `httpclient3`, `httpclient5`, `httpclient7` and `h2client6` -
pass on x86 vc143 debug, x64 ccl16 debug and x64 vc143 release, and 50 of 50 each on x64 vc143 debug
under the load of that rebuild, where `httpclient7` had been 0 of 50 and `httpclient5` 42 to 47.

Every change to the test tree above is blessed by the companion inventory refresh, `8ee047d`, whose manifest diff
names exactly the three case bodies, the one doc comment and the helper members these edits touched.
Evidence in `http2-l0-state/logs/win-handoff/w3-control/`, `.../w4-control/`, `.../w5-control/` and
`.../decisions/`.

#### The decisions of 2026-09-25, second round, as they were put and as they were taken

Four owed items, put together in AGENTS.md's shape and accepted as recommended, with the full matrix
left for the maintainer to authorise separately and everything validated by focused testing. Doing
them turned up three more questions - the refresh's shape, the tool's closing note and the io runs'
load - each put as a decision of its own and accepted as recommended too. Ordered as they were put,
cheapest first. **No product behaviour changed in this round.**

**The stale comments.** *What it is:* comments in `src/` still stated as a platform property the
mechanism `windows-peer-close-error-codes-record.md` withdrew on 2026-09-23 as our own `shutdown_both`,
and that record listed them. *If not done:* the next reader reasons from a false premise, as §13.3 of
`driver-read-write-arms-design.md` records a lane already did. *Risk and blast radius:* none in
behaviour; `NetUtils.h` recompiles nearly everything. *Undecided was:* only when. *Decided:* now, as
one comment-only change-set. **Done** at `3bf42e4`: the four sites `3dce6ae` had not touched, the
third predicate's comment, which `3dce6ae` itself left stale, and one site the list missed, folded in
as the same decision - `Http2TestServer.h`'s `isPeerClosed( )`, "a client which closes while one is
pending renames the close". Every changed line is a comment line;
the five files compile clean under `-WX` on x64 vc143 debug and release and x64 ccl16 debug, through
`httpclient5`, `http2` and `h2client2`, which between them include all five; tier 1 reported exactly
those edits, blessed by the companion refresh `e6fc02b`.

**The io SSL abort.** *What it is:* `IO_SslSimpleConnectAndTransmitDataMessageDispatcherOutgoingTests`
aborted once in the first x86 run, at 8183 of its 8195 assertions, and the runner kept no output.
*If not done:* a rare Windows failure in network code shows up as random red `io` runs with nothing
to diagnose from. *Risk and blast radius:* runs only. *Undecided was:* now, or on a second sighting.
*Decided:* 50 runs under load on x86, keeping every log, closing on "not seen" if it does not recur.
**Done - 50 of 50 passed, every one at 8195, and closed.** Only 8 ran under load that slowed the case:
beside `-j1` compiles it took 61 s on average against 51 s unloaded, and the 41 run beside a one-core
CPU spinner took 48 s, so on this two-vCPU VM the spinner loaded nothing. That shortfall was put as its
own decision - close now, with the full matrix supplying loaded `io` runs on twelve builds, or run 42
more beside a real compile - and *decided:* close now. W1 is ruled out as the cause: the only
`std::vector< const_buffer >` in the library is the HTTP/1.1 driver's, and the `io` path never hands
the wrapper one. It stays unexplained; a second sighting reopens it.

**Item 7, the TLS spelling of a write's reset (W8).** *What it is:* the code a TLS write reports when
the peer resets, never measured because no Linux arrangement produces one. *If not done:* the TLS
half of the peer-reset handling rests on a reading of asio. *Risk and blast radius:* a measurement
changes no code. *Undecided was:* measure once, or add a permanent case, whose home `httpclient5` is
over target. *Decided:* measure once, and close on `connection_reset` or `connection_aborted`.
*Reverses:* any other spelling, which would be a defect. **Done:** `connection_reset`, 5 of 5 - W8.

**W9, tier 3 on Windows.** *What it is:* tier 3 reported 620 differences on the first Windows run
and 626 on the next, 618 of them the cases of modules its baseline did not cover, reported as added
and newly run while its own coverage statement said they were not compared. *If not done:* the gate
for a case that still passes while checking less stays switched off on the one platform it covers. *Risk and blast radius:* the
tier-3 tool and its baseline files; Linux declines tier 3 by design. *Undecided was:* change the tool
and re-take the 17-module snapshot; take the client modules in now; or leave it. *Decided:* the first,
with `BaseLib_SortedVectorHelperTests` listed and `httpclient5`'s exclusion reason corrected. **Done**
at `76d0764`: an uncovered module's cases are no longer reported as added, newly run or newly
skipped, while losses stay tree-wide, so a case moved into one is neither lost nor added. A synthetic
check passes 8 of 8, and replaying the post-W1 run turns 626 differences into 12, each accounted for.

**The refresh's shape - found doing it.** *What it is:* the 17 modules were the whole of what
`91d5c2c` captured, as the split's step 0, and the split then moved 135 of their 741 cases into ten
numbered siblings. The old capture still checked all 135 only because a comparison matches cases
tree-wide, and each has the same count in its two passes and in the two new ones; a re-take of the 17
alone would not contain them, and a `--family` comparison of any split family would then report every
moved case as added. The write-up that proposed re-taking the 17 called all 27 unlisted modules
excluded for flakes; eleven were the split's successors. *If not done:* 18% of what tier 3 checks
drops out on Windows, silently. *Risk and blast radius:* data and prose only; the risk of a wider
shape is a flaky case in a newly covered module, and the pair derives the same ten unstable names over
28 modules as over 17, while the siblings match the previous full run on all 175 cases but the one W5
changed. *Undecided was:* 28 modules, 27 without `utf_baselib_http2` - on the refused list, and holding
19 of the moved cases - or 17. *Decided:* 28. *Reverses:* a Windows run showing a failure or an
unexplained count change in `http2` or `tasks2`, which then returns to uncovered with the measurement
as its reason. **Done** at `cbbc2bb`: two passes over 28 modules and 783 cases, every module exit 0
in both; the list gains `IO_MessagingClientObjectDispatchTcpDispatcherTests` from the pair and the two
names under `observed`; `uncovered.json` drops `http2`, and the reasons that read false on a Windows
host now say what is true; `src/utests/AGENTS.md` says what the baseline covers, and the rule this
found. Against the old baseline the new passes differ in exactly six lines - two cases added to
`basictask`, and the two cases edited since it (`0da54dc`, `d7f5ef0`) - which is what the refresh
blesses. Replaying the previous full-tree run against it reports five: the W5 edit in `http2`,
15 → 13, which leaving `http2` out would have hidden, and W2's and W4's failures, fixed since.

**The tool's closing note - found doing the refresh.** *What it is:* on a comparison that did not
include every module, "these modules ARE covered now" listed the modules it had simply not run. *If
not done:* a false coverage claim, in the note read before trusting a green. *Risk and blast radius:*
that note only, never the verdict. *Undecided was:* now, in the tool's own change-set, or the owed
list. *Decided:* now. **Done** at `76d0764`: a reason naming a covered module is still called stale,
and one naming a module the comparison never saw gets a note of its own - not run, outside the
`--family`, or gone from the tree - so a module gone from the tree is still flagged. Red with the
old tool, both false claims reproduced; green with the new, 6 of 6.

Over the whole of it the gate's static tiers pass - tier 1, line endings and tier 2 - and tier 3 was
compared as above rather than run over the tree, a full-suite run being the matrix's.

Evidence in `http2-l0-state/logs/win-handoff/w9-tier3/`, `.../item7-tls-write-reset/`,
`.../io-ssl-abort/` and `.../comments-check/`.

## Defects found during the remediation, recorded and not fixed

**These were sorted by where they were found and not by what they cost, and that is the wrong axis.**
The remediation's scope was astra's 29 findings; everything below was found *by us* while
implementing them, so each was written into the design of the change-set that found it and inherited
that provenance when this list was consolidated. **None of them was ever triaged against an astra
finding.** Where a design says "its own change-set", the deferral was recorded and the successor slot
was never created — that is the gap, and it is what **Status** now makes visible. The column
distinguishes three states that the first version of this table ran together: *unscheduled* (owed a
slot nobody created), *decided* (the reason survives re-reading), and *not a defect*.

| # | Item | Status | Where |
|---|---|---|---|
| 6 | **H01's spurious reuse refusal** — `httpclient3` fails ~10% of full-module runs (2/20 with the teardown fix, 3/20 without, so **pre-existing**), and the `tls-h1-control` lane independently measured **~1 in 8** on TLS | **folded into this batch 2026-09-23**, because it degrades the gate every other change-set is judged against. Redesigned in [`h01-reuse-verdict-design.md`](h01-reuse-verdict-design.md) after the recorded shape failed re-derivation; lands **after A2** (§5.3). Its evidence standard is a **scoped exception**, below | teardown design §16.6 |
| 7 | **h1's write path has no peer-close arm** — h2 has one; which handler notices the peer first still decides whether the task fails | **FIXED and merged** as **A2** (`04bcb6f`), shape (ii): the write declines to fail the task and the still-armed read classifies the ending | teardown design §4.4, §13 |
| 8 | **A composed TLS read can slip a cancel** the same way a composed write does — pre-existing, neither created nor closed by the teardown fix | **FIXED and merged** as **A1-tls** faces 1 and 2 (`db30f53`, `5941a50`). Face 1's gate has a red of its own, which a review had concluded nothing in this tree could give it | teardown design §13 |
| 9 | **The third HPACK hazard** — `encode( )` commits its dynamic-table transaction at queue time, so a dropped block leaves our encoder holding entries the peer never saw | **decided** — the fix is shape **(D)**, which §3.3 rejected for that slice; unreachable today by four properties re-verified at the source on this tip | `s6r3-design.md` §3.3, §3.7; `s6r3-h10-record.md` §5 |
| 10 | **A control frame queued after the SETTINGS ACK now leaves before it** | **not a defect** — nothing in RFC 9113 orders a SETTINGS acknowledgement against a PING acknowledgement. Recorded because it is certain from the code and invisible to the suite, not because it is wrong | `s6r3-h10-record.md` §2 |
| 11 | **A sixth terminal situation** where `onComplete( )` is not truthful: a clean close with no final header block | **DEFERRED by the maintainer 2026-09-24**, having been re-examined on its own merits. The fix is `onComplete( outcome )` — a terminal callback whatever happened — which changes `ClientTypes.h`, a **published IID** whose own header says such a change *"is negotiated rather than made"*. It changes every implementer of that interface for one edge no caller has asked about. **Reverses if anything else needs `ClientTypes.h` touched**, at which point it rides along nearly free; `s6r3-design.md` §10 lists two other deferrals waiting on exactly that. *Label corrected 2026-09-24: this row said "deferred as **B4**". §1.3 says "deferred **with** B4", and §10 lists `onComplete( outcome )` and B4's reset-capable sink as two separate `ClientTypes.h` deferrals — **B4 is `canReset( )`/`reset( )` on `BodySink`**, a different option, recorded in `body-sink-terminal-callback-and-reset-deferral.md`.* | `s6r3-design.md` §1.3, §10; `s6r3-decisions.md` §2 |
| 12 | **`TaskBase::scheduleNothrow( )` calls `scheduleTask( )` under the task lock**, so h1's `scheduleRead( )` catch can reach `notifyReady( )` with the task lock held. `m_lock` is a non-recursive `std::mutex` and `notifyReadyImpl( )` re-acquires it, so this is a **self-deadlock**, not only a breach of `MultiOperationTask.h:62-67` | **scheduled as A4**, driver-local, in `driver-read-write-arms-design.md` §10.2. The **core** question — whether the library itself has a design problem here — is deliberately left open in [`taskbase-schedule-lock-scope-deferral.md`](taskbase-schedule-lock-scope-deferral.md) | teardown design §13 |
| 13 | **h2's `scheduleRead( )` has no accounting guard** — `beginOperation( )` then `async_read_some( )` with no `catch` completing the operation, where h1's read, write and timer each carry one. A throw out of the initiator leaves the count one high and the task unable to take its terminal path | **FIXED and merged** as **A3** (`372a397`, `13aac19`, `0a2a2e3`) — eight sites, not one, via the additive `abandonOperation( )` | teardown design §13 |

**The one scoped exception to the negative-control rule, decided by the maintainer 2026-09-23.**
`src/utests/AGENTS.md` requires that a fix be shown red against the unfixed code and green after, and
says in as many words that *"a negative control is the evidence; the run count is not."* **For item 6
only, a rate-based result is accepted instead.** The reason is not convenience: the design's §4
establishes that this defect's window is internal to our own strand scheduling and is reachable by
**no peer-side lever at all**, which the `tls-h1-control` lane demonstrated by trying — a 32 KB
request body moved the rate from 1-in-8 to 2-in-30 and then stopped moving, where a timing race would
have collapsed. A deterministic control remains preferred if one can be built.

**This is scoped to item 6 and is not a relaxation of the rule.** Any other change-set citing it is
citing it wrongly. The standard the rate must meet, and the residual risk it leaves, are in
`h01-reuse-verdict-design.md` §8 — in particular that the standard distinguishes *eliminated* from
*residual ≥ ~1 %*, so a fix that took 10 % to 0.3 % would likely pass as eliminated, and §4's own
caveat names exactly the mechanism that would produce such a partial result.

### Found by 13a's reading pass, 2026-09-24 — and this one is live

| # | Item | Status |
|---|---|---|
| 13e | **h2 tells the caller "do not retry" for a request the server answered.** In the one window where h2's write arm can fire — no read armed — the peer ends, the **write** handler runs first, `onPeerClosed( )` → `closeAllStreamsUnwrittenRetryable( )` empties `m_streams`/`m_handles` and reports every live stream `connection_aborted`, **non-retryable**. The read handler then arrives carrying the peer's **actual response** (Linux does not purge the receive queue on RST) and feeds it into a session with no streams, so `sinkOf( )` returns nullptr and the answer is dropped | **LIVE TODAY** on `connection_reset`. This is h2's counterpart of the h1 misreport §12.5 closed, by a different mechanism — **a wrong answer to the caller, the worst class on this list.** Fixing item 13a's predicate *alone* **widens** it to `broken_pipe` as well |

**It is closed by the shape, not by the predicate.** h2's `onPeerClosed( )` takes no argument and is
code-free, so the write side contributes **no information** by calling it — whatever it would decide,
the read decides identically one strand turn later. The only thing the call changes is *when*, and
earlier is strictly worse, because it empties the stream table before the answer arrives. So the
write declining and doing nothing closes 13a and 13e together.

### A blind spot in tier 1, found 2026-09-23

**`check_split.sh --tier1` passed over a manifest with no `notes_cases` entry for a case that has a
recipe.** The capture was taken before `notes.txt` was edited, and **C8 checks that every recipe
resolves to a real case — not that every case's recipe was seen.** So the gap is invisible in that
direction, and a green tier 1 is not evidence that a module's recipes are all recorded.

Caught by the lane re-capturing, not by the gate. It is the same lesson as the rest of this batch:
**a gate saying "clean" is a claim about what it looks at.**

**CLOSED 2026-09-24 by C9** on `tier1-c8` — and the rule is not the obvious one. *"Every case must
have a recipe"* is **wrong by a wide margin**: 481 of 1075 cases have none, across 27 of the 46
modules, because `notes.txt` is a curated list of the hard-to-reproduce cases almost everywhere. A
universal rule would have fired on 481 legitimate cases. **But the narrower rule was already written
in the tree** — fifteen `notes.txt` files open with *"each slice appends the recipes for the cases it
lands here"*. *Corrected 2026-09-24: this said those fifteen are **exactly** the modules at 100 %
coverage. They are not — **18** modules are complete and **15** declare it, the undeclared three
being `h2profiles` (14/14), `messaging4` (1/1) and `setprio` (1/1). The property that holds, and the
one C9 actually rests on, is **`declared ⊆ complete`**; the step-1 measurement said 18 all along.*
The convention existed
in the team's own words and the tool had never read it. C9 is therefore **opt-in**, keyed on a claim
a module makes about itself, in three halves: intrinsic, no-loss, and no-withdrawal.

### And the second blind spot is larger than the first — found 2026-09-24, NOT fixed

| # | Item | Status |
|---|---|---|
| C-2 | **`check_against( )` never reads `manifest['modules']` at all.** The entire per-module half of the manifest is captured on every run and **never compared**, so every differential claim tier 1 makes is about `cases` and `members` only. C8's one-wayness was one instance of a structural gap | **CLOSED 2026-09-24** on `tier1-modules`, four commits, one invariant each: **C10** (the `#include` list), **C7's differential half** (data-file content, and a file newly unreferenced), **C6's ADDED direction**, and the refresh docs. *Superseded 2026-09-24: this said "tier 1 is now C1–C10". **C11** (file-scope text), **C12** (a member's namespace) and **C13** (the shared `include/` tree) followed on `tier1-filescope` and `tier1-shared-tree`, both accepted on review, so the range is **C1–C13*** |

Measured on a tree copy, all four **PASS** tier 1 today: a data file's content changed; **all five**
copies of a shared data file changed identically (ruling out C7 as accidental cover); an `#include`
added to a header holding live cases; a helper member invented.

**The `#include` one is the sharpest.** `utf_inventory.py`'s own docstring claims C2+C3+C4 establish
*"the text of every test, **and the compilation context that text sees**, is unchanged"*. Includes
are part of that context, they are captured per file, and **no check reads them** — the tool's
central claim is stated broader than it holds.

Per invariant: C1 is bidirectional; C2/C3/C4 are symmetric equalities; C5 has no converse;
**C6's no-loss half is one-way exactly as C8 was**; **C7 is intrinsic only and never differenced**,
and its ref→file direction leaves an orphan data file unreported.

#### Review of `tier1-c8` (`16b33f9`) and `tier1-modules` (`ae97a50`), 2026-09-24

Every number below was re-measured, not read from the lane's logs: `--compare` on the current tree
PASSes (46 modules, 1075 cases, 550 members, 182 include lists, 32 data hashes, 4 accepted orphans),
the selftest is red against the tool at `lazari2` on exactly the twelve new mutations and green
against `tier1-modules`, both on the frozen baseline and on a fresh capture. The arming claim holds:
`36ec522`, the tool's first commit, already captured `files[*].includes` and `data_files`, and the
baseline refreshed at `6767d8f` carries 182 of 182 include lists and all 32 hashes. The guards fail
rather than warn, and the selftest exercises both.

**Verdict: accept `tier1-c8`, with one wording correction. Do not accept `tier1-modules` as it
stands.** C7's differential half and C6's ADDED direction are right and were measured silent on
real relocations; **C10 reds on every legitimate relocation**, which is the one thing the lane itself
said this gate cannot afford. Since the four commits are chained, the branch lands after C10 is
narrowed and re-proven; the control that proves it is named below and is already in hand.

**1. C10 fires on the split itself, through the source module's `Main.cpp`.** Every module's
`Main.cpp` is a roster of `#include "Test*.h"` lines, and a relocation *must* edit it. That file is
present on both sides, so C10 judges it. Measured three ways on tree copies, each green at
`16b33f9` and red at `tier1-modules` with the only C10 line on that roster: `TestTasks8.h` moved
whole to a new `utf_baselib_tasks9`, recipes with it (*case 1 of "Reducing a module"*) — one line,
`removed "TestTasks8.h"`; a 143-line helper block cut verbatim from `TestTasks.h` into a sibling
header included from the same `Main.cpp` (*step A of case 2*) — one line, `added "TestTasksCut.h"`;
and **the one real split in history replayed**, `f992e2f`'s pre- and post-trees from `git archive`
— `16b33f9` PASS, `tier1-modules` three lines, one of them the roster. None of the lane's controls
was a relocation: the "file removed" selftest covers the header that vanished, not the roster line
that named it. The scope exclusion is therefore not over-broad but *not broad enough*, and the
principled extension of the lane's own rule is one clause: **a quoted include naming a file this
relocation added to or removed from the same module is not judged, exactly as that file is not.**
That keeps what matters — an `#include <…>` added to `Main.cpp` still fires, and a *reorder* of the
surviving roster still fires as REORDERED, which is worth keeping because include order there is
registration order is run order, and `src/utests/AGENTS.md` records a cold-start contract that run
order can silently neuter. The `f992e2f` replay is the live control for the fix: it should report
exactly the two `C7 NEWLY UNREFERENCED` lines and nothing else. Not implemented here.

**2. The three-halves judgement on C9 stands; "exactly" does not.** Intrinsic covers the declared
modules including a *new* case, no-loss is the only coverage the 31 undeclared modules have,
no-withdrawal is what keeps the intrinsic half armed — none is redundant, and the only overlap is
cosmetic (a recipe deleted in a declared module prints two lines). But *"those fifteen are exactly
the modules at 100 % coverage"* — written in the paragraph above, in `utf_inventory.py:128-129` and
in `16b33f9`'s message — is false, and the lane's own step-1 log says so (*"modules exhaustively
indexed: 18"*). Re-counted independently: **18 modules with cases are complete, 15 declare it**;
`utf_baselib_h2profiles` (14/14), `utf_baselib_messaging4` (1/1) and `utf_baselib_setprio` (1/1)
are complete and undeclared. What makes the rule defensible is **declared ⊆ complete**, which holds,
not equality. `h2profiles` is the one that matters: an h2 module living by the convention without
saying so, where a new case landing unindexed is uncaught. Declaring it is a one-line `notes.txt`
edit under `src/`, owed separately. `AGENTS.md`'s *"Fifteen modules say this today"* is correct as
written.

**DONE 2026-09-24** at `a15694b`: `h2profiles` declares its index, matched by md5 against an existing
declaration rather than by eye, so the phrase is byte-identical. C9 moves from 15 modules / 255 cases
to **16 / 269**, exactly the 14 it brings. *`AGENTS.md`'s "fifteen" is now sixteen.* **Still open, and
now a decision rather than an observation:** `messaging4` (1/1) and `setprio` (1/1) are also complete
and undeclared. They were left out deliberately — `declared ⊆ complete` holds either way — but this is
the second time they have been recorded, which under the sweep rule is the signal to decide them.

**3. The orphan rule's premise is wrong for three of its four files, and the rule is still right.**
*"This tree carries such files today and always has"*: `git grep` at `f992e2f^` shows
`utf_baselib_messaging` referencing both `async_rpc_response.json` and
`async_rpc_response_with_exception.json`; that split moved the referencing headers out and left the
copies, and gave `utf_baselib_messaging3` a copy of the second it never names. Only
`utf_baselib_rest/data/async_rpc_response.json` is 2017-era. So three of the four "legitimate"
orphans are **exactly the defect the rule exists to catch, eleven days old, grandfathered.** The
differential shape is still correct — a rule red on the untouched tree is still wrong — but the
replay says two more things: the rule *works* (it names both messaging leftovers), and it **misses
`messaging3`'s, because a module absent from the baseline is skipped entirely** (`was is None →
continue`), a scope the comment does not state. Owed: a `src/` commit deleting the three leftovers,
so the accepted set is honest; and judging a *new* module intrinsically for orphans — nothing to
grandfather there, so no false positive is possible, and it would have caught the third.

**Both DONE.** The intrinsic-orphan judgement landed with `tier1-modules`; the three leftovers were
deleted 2026-09-24 at `dd00619`, each verified unreferenced two independent ways — grep over the
owning module, and the tool's own `unreferenced_data_files( )` over a fresh capture, which named
exactly four tree-wide. **C7's accepted-orphan count is 4 → 1**, the survivor being
`utf_baselib_rest/data/async_rpc_response.json`, which is 2017-era and genuinely orphaned. *Worth
recording because it surprised: C7 reports* **nothing** *as they go — a file removed outright is not
judged for content, and the orphan half compares only files still present. The line that moves is
C7's own scope, 32 data files to 29.*

**4. The narrowed docstring is still one notch broader than the checks.** *"C10 adds the remaining
part of the compilation context"* — but text at file scope outside any column-0 namespace block is
hashed by nothing: `struct ManifestFixture` (`utf_baselib_loader/TestManifest.h:28`, the fixture of
three `UTF_FIXTURE_TEST_CASE`s), nine column-0 `static` helpers such as
`utf_baselib2/TestTimeZoneData.h:28`, the `BL_IID_DECLARE`s, `UTF_GLOBAL_FIXTURE( LoaderInit )` —
fifteen files carry such lines. Measured: a member injected into `ManifestFixture` and an edit to
`logTestName( )` both PASS tier 1 at `tier1-modules`. Pre-existing, not introduced here, but the
rewritten docstring asserts a completeness it does not have. Minimum: say so in the docstring. The
closing shape is to treat file scope as an implicit block and run `split_members( )` over its residue,
so a fixture that moves with its cases stays a move; whether the member precondition holds at file
scope is unmeasured and is the lane's to decide.

**5. The selftest's orphan silence control is complete and vacuous.** It runs
`check_against( baseline, baseline )`, which is empty for *any* differential definition, so the
copied `unreferenced( )` feeds only the printed count and nothing asserts on it — the control is
subsumed by the *"unmutated baseline is clean both ways"* line above it. The second reason for the
copy (running against the older tool) is real. Minimum: assert the count is nonzero, since that is
the premise that forced the differential shape; a real silence control mutates the *after* side of a
module that carries a baseline orphan and asserts the old orphan is still not reported.

**6. Smaller findings.** C6's identity is text *with leading indentation* (`normalize( )` strips
trailing whitespace only), so a hoist across nesting depth re-indents and reads as LOST + ADDED per
member — the "hoist reads as a move" comment holds for same-depth hoists; a verbatim move measured
clean both for the 143-line block and for a header carrying eleven data files and a helper block, so
this is refresh-workflow noise, not a false positive. All fifteen declarations sit on `#` lines, as
`NOTES_INDEX_RE` requires; the phrase form is a substring match, so a quoting mention would opt a
module *in* — a visible red, not a silent off, and `notes-index: complete` is the robust spelling.
The no-withdrawal half is not in force until the baseline is refreshed, which the tool says on every
run; that refresh is the companion commit owed the moment the branch lands. C5 needs no converse:
it is a uniqueness property of one set, and "at least once" is C1.

**7. The docs describe less than the tool does, and more than C10 should.** The tiers table says
*"helper members neither lost nor duplicated"* — C6's ADDED direction, the whole of `1c7003e`, is not
in it; it says *"no case loses one"* and omits C9's intrinsic and no-withdrawal halves, which only the
checklist carries; and *"every file keeps the `#include` list it had"* overstates C10 twice — files
present on both sides, and, once fixed, minus the roster lines of files the relocation added or
removed. The refresh paragraph is right and was the missing piece; the checklist's C9 item is
accurate. *"Tier 1 is a relocation gate"* is true again only after finding 1 is fixed.

**Could not settle here:** whether the roster exclusion should also silence REORDERED on `Main.cpp`
(argued no, above); the file-scope residue shape; and whether the three split-leftover orphans are
deleted before or after the branch lands — before is cleaner, because the replay control then
reports nothing at all.

### Found by H01's implementation and its review, 2026-09-23

| # | Item | Status |
|---|---|---|
| 6a | **§13.8's write-first half** — `Ready` is still published on a connection whose write the peer reset, on the interleaving where the write handler runs first and the deferral is therefore never taken. H01's outcome term **prevents a regression on the read-first half rather than closing the defect**: there the verdict was `Draining` by accident before, and would have become a spurious `Ready` had the continuation asked only the flag | **FIXED** on `sync-verdict` (`edb6d96`), reviewed and pending merge. The synchronous verdict now asks `! m_writeEndingCode` too, so the tree no longer carries two verdict computations differing by one term. **15/15 red without it, 0/15 with it**, against a real peer with a real RST and **no seam** — and `deferrals=0` in all thirty runs proves the evidence is about the synchronous path rather than the deferred one. The predicate was deliberately NOT `isPeerResetOnWriteErrorCode( )`: that one asks whether a read's `eof` is the residue of a reset the write consumed, where admitting `broken_pipe` would be wrong; reusability has no such asymmetry, since every code a write can end on means this peer will not finish reading the request. **This also settles §13.8's own open question — the Ready-on-reset shape does occur against a real origin, deterministically.** |
| 6b | **`NetUtils.h:401` still carries the withdrawn "the stack has collapsed" premise** — the claim `windows-peer-close-error-codes-record.md` retracted on 2026-09-23 as self-inflicted by `shutdown_both`. `:348` carries it too and **the record lists that one; it does not list `:401`** | **FIXED** (`58aa106`). Both sites now say what is known: the spellings are admitted so a retry is not refused, and whether a clean close ever arrives as one is **not measured** |
| 6c | **`h01-coverage`'s seam is unusable and must not be reused** — line for line the pre-rendezvous hold and release, so it hangs at H01's own rate. Superseded by `TestHttp1DriverStrandSeam.h` on `h01-fix`. **Nothing from that branch should merge**; its 1370-run measurement stands because it ran on `httpclient3`, where there is no seam | recorded; no action beyond not reusing it |
| 6d | **H01's handle-identity guard is implemented and unexercised**, and it decides something the design did not spell out: a `cancel( )` landing in the one-hop window now wins over a completed response, and the sink gets the cancel's code | **DONE** — `Http1Driver_StrandSeamCancelInTheWindowWinsTests` (`TestHttp1DriverStrandSeam.h:941`) takes exactly the recipe this row specified: a sink calling `cancel( handle )` from inside `onHeaders( )` puts `onCancelStream( )` ahead of the continuation by strand FIFO. *Status corrected 2026-09-24 by the ledger sweep* |

### Found by A2's implementation, 2026-09-23 — and this one is measured

| # | Item | Status |
|---|---|---|
| 7a | **A truncated close-delimited response is reported to the caller as a complete, successful 200.** Measured: a close-delimited body (no `Content-Length`) cut short by a peer RST yields a 16-octet body reported `closed:ok`, no error — **7/8 without A2's arm, 8/8 with it**, so **pre-existing**; A2 does not create it | **FIXED and merged** as **face 3 of A1-cleartext** (`6632469`), `driver-read-write-arms-design.md` §12.5, against the 8/8 red it had in hand. *Status corrected 2026-09-24 by the ledger sweep, which found this row still reading "SCHEDULED" — the highest-severity item on this list reported as open after it landed* |

**The mechanism, established by a standalone raw-socket probe and confirmed in the driver.**
`sk_stream_error( )` takes the pending socket error with an **exchange**, so whichever syscall reaches
the reset first consumes `ECONNRESET` and the other gets the leftovers: `send( )` first → the read
sees a plain **`eof`**; `recv( )` first → the write sees **`EPIPE`**. **That is deterministic, not
timing**, and it is why item 7's predicate has to admit both spellings.

On the `send( )`-first ordering the read is handed `eof`, `isCleanEndOfStream( eof )` is true,
`parseEof( )` declares the close-delimited message complete, and the truncation is invisible — while
**the write side held the only evidence that the ending was a reset.**

**A2 removes nothing the caller ever saw** — *corrected 2026-09-23 by A2's implementation review,
which read the request task rather than reasoning from the driver.* `answerOnClosed( )` fails the
request only on the stream's own `event.errorCode`, and the connection task's failure is **chained,
never substituted**. So before A2 the caller already received the truncated body as a complete 200
while the task failed unread. This entry previously said A2 removed an "accidental signal"; that was
wrong — the signal never reached the answer.

**The fix, and why the obvious shape is not enough.** The write's code is the only evidence the
ending was a reset, so the write must record it and the read must consult it — but that alone
**fails**, because the read's `eof` handler can run *before* the write's handler, and a record not
yet written cannot be consulted. §12.5's step 3 supplies the missing half: when the read observes an
end of stream with a live parser **and a write in flight**, it defers delivery to the write's
handler, which `initiateClose( )` guarantees will run. Both are on the strand, so the order the
dispatcher chose stops mattering.

**The rule that makes it decidable, derived from the kernel and verified independently:**
`tcp_reset( )` writes `ECONNRESET` only from `ESTABLISHED` and `EPIPE` from `CLOSE_WAIT`, and
`tcp_fin( )` sets `SOCK_DONE`. So **a write that completed `connection_reset` proves the ending was
a reset with no FIN before it; a write that completed `broken_pipe` is not evidence against the
read's own code.** `net::isPeerClosedErrorCode( )` already asks exactly that of a write's code — no
new comparison is needed, which is what AGENTS.md's networking rule wants.

| 8a | **A1's recorded gate no longer reaches the defect it was specified for.** Today `isClosing( )` is *true* in that read handler because the write's `CHK_EC` set it, so the `! isClosing( )` gate would have closed the close-delimited misreport **by accident**. With A2 landed the write sets nothing, `isClosing( )` is false, and the recorded gate misses it | **A1 must be re-specified before it is implemented** |

### Found by the 2026-09-23 design review of items 7, 8 and 13

| # | Item | Status |
|---|---|---|
| 13a | **h2's `onWrite( )` has the same write-side hole as h1's** — it asks `isPeerClosed( )`, which is the **read-side** pair, so a peer close reaching the write first as `broken_pipe` goes to `CHK_EC` and fails the task. `H2Driver_PeerHalfClosesWithAWriteInFlightTests` does not see it because its peer holds its end and the read notices first; a peer that RSTs would | **FIXED and merged** with **13e** (`bc1f13a`, `09e22d8`) — the shape, not only the predicate |
| 13b | **The TLS composed read's hang face** — a partial TLS record and a silent peer, in *both* drivers | **CLOSED 2026-09-24 — it is a consequence of a decision already taken**, not owed work. `initiate-close-teardown-design.md` §2.4.3 rejected per-operation cancellation on two grounds that survive — it changes how every write is issued, and it introduces a second cancellation mechanism — and that section already records that a `terminal` emit *"would close §13's TLS-read item as well as this defect"*. So this is the cost of that rejection, written where the decision lives. **Revisit only if §2.4.3 is revisited** |
| 13c | **Item 13 undercounts.** h2 has **eight** unguarded `beginOperation( )` sites — read, write pump, five timers and the command-drain post — where h1 guards all three of its own | **fixed** by A3, `372a397`; the count was measured, not taken from the design |
| 13d | **The post-negotiation `scheduleRead( )` escapes to the establisher's plain `BL_TASKS_HANDLER_END( )` and then takes a second terminal from the opening write** | **CLOSED 2026-09-24 — folded into [`taskbase-schedule-lock-scope-deferral.md`](taskbase-schedule-lock-scope-deferral.md) §6.1.** It is an instance of that section's general statement, its fix is that section's to make (the additive *"fail while operations are pending"* primitive), and it changes nothing an L0–L6 caller can observe: bounded to redundant work and possibly a second log line by three verified properties, with the pool seeing one completion. **Not owed work; part of a deferred question** |

**Items 12 and 13 were dropped when this list was first consolidated.** Both are in teardown §13 and
neither reached the first version of this table. They are added here rather than quietly, because a
consolidated list that silently loses entries is worse than the eight scattered records it replaced,
and because this is the second time this feature has lost something by summarising it.

**Owed comment corrections, from §16.9.** `httpclient7/notes.txt`'s stale "known red" block is
**done** — rewritten rather than deleted, per §16.8.1. `TestHttp2DriverWriteBarrier.h` still
contradicts itself on §8.3's wrong lever in four places (`:59-60`, `:82-84`, `:639-641`, and the two
failure messages at `:655` and `:708`) while `BIG_WRITE_THRESHOLD`'s own comment is correct — a
comment pass, still **owed**, and it matters because those are the words a failing run prints.

### Item 6, re-derived 2026-09-23 — the recorded shape is wrong

§16.6's *facts* were re-verified against the tree and all hold: the mechanism, "pre-existing",
"benign", "the safe direction", and that the case is right to be red on it. **Its fix shape does not
hold**, and the reason is that it was derived from `httpclient3` alone.

The shape defers the reuse verdict when the write is still in flight, the connection is otherwise
reusable, and we are not closing. **Those three conjuncts are exactly what
`utf_baselib_httpclient7`'s two barrier cases were built to make true** —
`TestHttp1DriverWriteBarrier.h:216-225` says so in its own words — so the predicate cannot separate
the defect from the cases that forbid the fix, and teardown §8.1 protects those four assertions
explicitly. Both readings break them: leaving the state un-downgraded publishes a connection as
reusable *and immediately submittable* with a write outstanding, re-opening H01's first face; ending
the whole stream hangs both barrier cases at `waitForClosed( )`.

**The only separator is time**, which makes this a design decision and not an implementation detail —
one strand `post` (free, but only fixes the window when the handler is already queued, and the
continuation must be an *accounted* handler or `taskEndedUnaided` goes red), or a short deadline
timer (deterministically testable, but a bounded drain, which `finishStream( )` rejects in
principle). Three further gaps the re-derivation surfaced and §16.6 does not mention: the write's own
failure path would have to widen §4's freshly-gated arm; `chkArmIdleTimer( )` is not idempotent-free;
and three synchronous readers of the verdict sit inside `onClosed`, one of which dispatches a queued
request.

## Astra findings deliberately not taken

| # | Item | Why |
|---|---|---|
| 13f | **H19's actual remedy** — moving the TLS specialization behind a TLS-specific header, so a plain client does not reach OpenSSL at all | **DEFERRED by the maintainer 2026-09-24** with its own record, [`h19-tls-specialization-header-deferral.md`](h19-tls-specialization-header-deferral.md) — it buys a dependency nobody has asked to be rid of, and the change is a header-topology split rather than the ~10 lines the estimate suggests. **Reverses on a consumer needing a genuinely OpenSSL-free build.** It was missing from this list until 2026-09-24. H19 itself is discharged as *recorded and accepted*: the `OPENSSL_VERSION_NUMBER` guard, the comment explaining why it is keyed on the capability rather than `BL_DEVENV_VERSION`, and the consequence written into `httpclient/PreCompiled.h`. But astra's remedy is a real piece of work recorded only in the verification record — **not in the one place a reader is told to look for what is left** |
| 14 | **H11** | closed by the maintainer: leniency is chosen, the justification rewritten, the reopen trigger recorded. **Landed in S6R.4** (`38ed037`), which the plan's slice table did not list |
| 15 | **H24, H25** | latent until a content codec ships, and prerequisites of it — now recorded as P2 and P3 in the decoder deferral |
| 16 | **H21** — successful HTTP/1 selection consumes a retry, so `maxRetriesPerRequest = 0` makes the first request to every HTTP/1.1 origin fail | **already deferred, properly** — it is L6's **finding 4a**, recorded in [`http2-l6-review-record.md`](http2-l6-review-record.md) with its cost, both candidate shapes and what each does to the control case, and its handover condition **met**: `ConnectionPool.h`'s `maxRetriesPerRequest` comment opens with *"AND ZERO SWITCHES OFF HTTP/1.1 THROUGH A SESSION RATHER THAN MERELY TIGHTENING THE BUDGET"*. Needs no new document; this row exists so a reader of one list finds the other |
| 16a | **H22** — cancellation between redirect hops can report the **intermediate** response as a success | **FIXED and merged** (`03680cb`, review record `d35188b`, manifest refresh `21b0c37`) — the chain decides before the latch is read, so a cancel between hops is one outcome. Was L6's finding 9, **a wrong answer to the caller**, the same class as 7a and 13e. *Status corrected 2026-09-24 by the ledger sweep* |

## Not part of this remediation at all

**The embedded decompression work** is parked with its design and plan committed. Decision **E5**
(generated headers in the repo include tree versus the devenv dist) is unmade and blocks its first
layer, and astra's **C01** — the design specifies `inline constexpr`, which is C++17, while baselib
compiles `-std=c++11` — must be resolved before any of it is attempted.

## Review of `tier1-filescope` (`9c029df`..`c1689ca`), 2026-09-24 — the third blind spot closed, and what closing it turned up

This is finding 4 of the `tier1-modules` review above, implemented. Every number here was re-measured
on tree copies with the branch's tool and with `lazari2`'s, not read from the lane's logs.

**Verdict: accept, once two small corrections are folded in — one token, and a terminator regex with
its assertion — and the docstring says what those corrections make true.** The rule is the right one,
its identity and direction are C6's for C6's reasons, and each of the three exclusions is shown
load-bearing by removal rather than argued. The real-history bar holds. The arming departure is
accepted on one condition, and for a different reason than the lane gives. C5's second subject should
be implemented, separately.

**What re-measures.** A fresh capture's four compared keys are byte-identical to the committed
baseline, so nothing tier 1 already judged has moved; C11 sees **39 spans in 16 files**, a 423-line
extent (361 non-blank), 38 distinct shas — `using namespace bl;` twice. `--compare` passes against
the committed baseline with the arming note, passes armed against the fresh capture, and the tier-1
gate is green. The selftest is red at `lazari2` on exactly the five C11 lines and green here on both
baselines. The five filesystem probes reproduce logs 02 and 03 line for line. The four replays give
**zero C11 lines**, with C1 and C6 at 7+3, 11+22, 4+2 and 4+2; `f992e2f` gives exactly the three C7
lines. Removing exclusion 1 reds control C on the roster and an include (3 lines) and control E on
the roster and `#define UTF_TEST_MODULE utf_baselib_tasks9` (4); removing exclusion 2 makes `f992e2f`
report the four blocks — three `Utf…Main.cpp` heads and `ImplTestMessagingUtils.cpp`; the prose
filter takes 46 intra-module duplicate groups to 0, dropping 352 spans, every one comment-led. The
hoist is acceptable: `lazari2`'s two C6 LOST lines read as a deletion, and C11's third line says
where the text went; a hoist to file scope changes linkage, so the red is honest, and same-depth
hoists stay silent as C6's comment promises.

**1. Four of the 39 spans are not file-scope text, and they are a C2 defect the lane walked past.**
`JsonPrettyPrintNestedLayout` (`utf_baselib_data/TestJsonAbstraction.h:2458`) holds a raw string
whose closing line `})";` sits at column 0 (`:2507`); `CLOSE_RE` is `^\}`, so the case walk ends
there, and the **32 lines** to the real brace at `:2539` — the `#else` branch,
`UTF_REQUIRE_EQUAL( pretty, expected )`, `verifyDeepEqual( )`, two `UTF_REQUIRE`s — are outside C2.
Measured: editing that assertion **passes at `lazari2`**; once C11 is armed it reds, labelled
*file-scope text*. It is the only such case of 1082 — every other one ends on a bare `}` — while
**144 of 165 namespace closers carry `} // __unnamed`**, so the fix is scoped to the case terminator:
require `^\}\s*$` there and assert it, as the precondition already asserts the opening brace. The
docstring names the 39 as the fixtures, the column-0 statics, the IIDs, the global fixture and the
using-directives; **35 are.** The lane's own log 01 lists *"TestJsonAbstraction.h 4 member(s) 18
lines e.g. `const std::string expected =`"* without seeing that the span is indented — a right count
on a wrong premise. After the fix: 35 spans in 15 files, one C2 BODY CHANGED against the old
baseline, blessed by the refresh that is owed anyway.

**2. A BOM reds a legitimate relocation — the one new false-positive class, and no control could
have found it.** Control E with the new entry point saved UTF-8-with-BOM, as a Windows editor does:
`C11 file-scope text ADDED: /* (utf_baselib_tasks9/UtfBaselibTasks9Main.cpp:1)` with the BOM in
front of the `/*`; strip it and the tree passes; `lazari2`'s tool passes the BOM'd tree.
`read_lines( )` opens `utf-8`, the BOM survives as `﻿` on line 1, and `COMMENT_LINE_RE` and
`DIRECTIVE_RE` both anchor on `^\s*`, which `﻿` is not — so the licence block stops being prose.
In this tree line 1 is always the licence comment, and C11 is the only invariant that reads it, which
is why it alone is exposed. No file in the tree carries a BOM, the `eol` tier does not look for one,
and the Windows agent writes files here. Fix: `utf-8-sig` in `read_lines( )`, a no-op on every file
in the tree (the four keys stay byte-identical); control E with a BOM is the control.

**3. What exclusion 1 gives up is more than the docstring says.** The file-scope `#define`s are 48
include guards, 45 `UTF_TEST_MODULE`s and **eleven others**, eight of them multi-line. Measured
silent: dropping `#define UTF_TEST_APP_INIT_DEACTIVATE_THREAD_POOLS ( true )` from
`utf_baselib_basictask`'s entry point — `UtfMain.h:201` reads it to deactivate the thread pools at
app init, and `TestBaselibBasicTask.h:194` says its cases depend on that — and editing the body of
`UTEST_REQUIRE_PRESERVED`, expanded 111 times in `TestServerErrorHelpers.h`. The docstring discloses
the multi-line case (*"unhashed in full"*) and not the one-line behavioural one. The principled
narrowing keeps the exclusion for exactly what the measurement showed a new module must write — an
include guard, recognised by `is_include_guard( )`, and `UTF_TEST_MODULE`, by name — and hashes every
other `#define` with its continuations; a new module writing
`UTF_TEST_APP_INIT_DEACTIVATE_THREAD_POOLS ( true )` matches `basictask`'s text tree wide and stays
silent, and a define with new text is ADDED exactly as a new helper is. Decision below.

**4. `101eee7` is right and complete; its stated control is a latency control, not a fix control.**
Disabling C1 reds both C1 expectations under the *old* harness too, because neither mutation yields a
C10 or C11 line — that measurement establishes the bug was masking nothing, which is the "latent"
claim, and says nothing about the fix. The discriminating control: the old `expect( 'C1' )` on
`['C10 file INCLUDES CHANGED: x']` returns True, and True on a C11 line; the new returns False on
both and True on a C1 line. Every failure string the tool emits carries the space (2, 2, 1, 1, 1, 7,
6, 2, 3, 3, 4 strings across C1–C11, all spaced). The other matchers — `startswith( 'C8' )` in the
clean-check exclusion, `'C6'`, `'C7'`, `'C9'` and `'C10'` in the silence filters — are bare prefixes
with no longer sibling; the two C11 silence filters carry the space. Nothing else is wrongly
prefix-matched.

**5. "Two named things … and no others" is exactly true only under a reading it does not state** —
within a scanned module file, and for text. Outside that: `src/utests/include/` is **27 files,
15,400 lines, scanned by nothing** — the lane's own gap 1, 36 times what C11 closed, and its log 07
measures a shared fixture edited there and `UTF_AUTO_TEST_CASE` itself redefined, both PASS; a
directive *inside* a file-scope body is blanked out of that span's hash (zero instances; hashing
`lines` rather than `shadow` over the extent closes it, at the bracket-balance precondition
`split_members( )` already rests on); the guard stack of file-scope text (zero genuine instances —
the one my count flagged is finding 1's artefact); the eleven `#define`s; and the prose rule is a
house-style test — 20 interior lines in two files lack the leading `*`, all commented-out code inside
cases, so a file-scope block in that style would be hashed, and *"a comment block standing on its
own"* should read *"every line of which opens with a comment token"*. Measured true: no case is
documented by a `//` comment, no prose span carries code after `*/`, no BOM, no `namespace a::b`, no
nested column-0 namespace, and the 17 non-source files are `devenv7_only` and `jni_enabled` markers.
The count moved from 343 (`a40a734`) to 423 with the method unstated; 361 non-blank today. Minimum:
one clause for `include/`, one word for the prose rule, and finding 1's correction.

**6. The arming departure — accepted, on one condition, and the lane's own argument for it is
wrong.** *"The two hard failures keep the note from becoming a permanent silence"*: neither fires on
a baseline *missing* the key, which is the state every baseline is in. What actually bounds the
silence is that `capture( )` always writes `file_members`, so the next refresh for any reason arms
C11 — and all four replays carry C1 ADDED, i.e. a refresh, so the bound is days, not a policy. The
edge the other way: a refresh by a pre-C11 tool, from a lane branched before the merge, would
*disarm* it with only the note to say so; the conflict on `inventory.json` is what would surface
that. The condition is that the orchestrator refreshes the baseline in the integration itself — the
fold-in rule: the refresh was decided, blocked only by concurrent lanes, and the merge is where that
blocker lifts — as one refresh carrying C11, finding 1's corrected body and finding 2 together.
`check_split.sh` prints *"C1-C11"* while C11 is not in force, a claim about what ran; it resolves
with the refresh and is not worth a change.

**7. C5's second subject — implement, its own commit, not blocking.** End to end on a tree copy: a
case copied into `utf_baselib2/TestDuplicate.h`; `--capture` exits 1 **and writes the baseline
first** (`main( )` writes at `:1307` and judges at `:1313`); delete the original — the entry
`index_cases( )` discards, since modules sort `utf_baselib` before `utf_baselib2` and the last wins —
and `--compare` is **PASS**, for a verbatim copy and for a copy with an assertion changed, the second
being an edit smuggled through a relocation gate. Delete the copy instead and C2 fires by accident,
comparing two different entries. Two shapes: (i) C5 on `before` as a hard failure — *"broken rather
than merely old"*, C11's EMPTY precedent, about six lines; (ii) `--capture` refusing to write on an
intrinsic failure, the control *on* the risk (a red baseline cannot be born) where (i) sits adjacent
to it. Recommend (i) now, having the precedent and the size; (ii) changes the capture's contract and
is the maintainer's. Reverses if the refresh workflow ever gates on the exit code, which nothing does
today.

**8. The in-repo selftest cannot detect the loss of either exclusion.** Its C11 section mutates
`file_members`; the extractor runs on the pristine tree only, so the probes that found both
exclusions live in `live.sh` outside the repo, as the runbook rule wants. Once armed, the tree is the
control for the shapes it has — 352 ADDED lines if the prose filter goes — so this closes with the
refresh except for shapes the tree lacks. A `scan_file( )` control on a temp file — a guard, a
`#define`, a standalone block, a documented declaration — would put the exclusions under the repo's
own selftest. Recorded, not owed.

**Decisions, in the order to take them.** *(1) The case terminator*, folded into this branch:
otherwise the docstring is wrong about four of its 39 spans and a 32-line C2 hole stays open until
armed, then closes by accident under the wrong label. Risk low — 1081 cases already satisfy the rule
and the 144 namespace closers are untouched; blast radius is `scan_file( )`, the extractor every
invariant reads, so the selftest and the five probes re-run. Undecided: a stricter regex that finds
the real brace, versus a parse problem (exit 3) that forces the raw string to be re-indented;
recommend the regex with the precondition asserted, reversing if a case is ever found whose real
closer is not bare. *(2) `utf-8-sig`*, folded in: one token, a no-op on the tree, control E BOM'd.
*(3) Narrow exclusion 1* to guards and `UTF_TEST_MODULE`: otherwise a dropped behavioural define and
an edited 111-use helper macro are silent for good; the risk is a define legitimately differing per
module, and none of the 45 `UTF_TEST_MODULE`s counts since it is excluded by name; blast radius is
the directive pre-pass; controls C, E and `f992e2f` re-run. Recommend yes, after (1) and (2);
reverses if a replay shows one. *(4) C5 on the baseline*, own commit. *(5) The refresh at merge*,
finding 6's condition, once — after (1), so the corrected body is what is blessed.

**Could not settle here:** whether the BOM behaviour is the same under `core.autocrlf` on a Windows
checkout — measured on Linux copies, and the tree has none; whether (3) should wait for the
`include/` gap's design, since a module's own `#define`s are a small part of what `UtfMain.h`
consumes; and `src/utests/include/` and the never-differenced namespaces — the lane's gaps 1 and 2,
both measured PASS on an edit in logs 07 and 08 — which are the next owed slots and not this
branch's.

## Review of `tier1-filescope` (`c1689ca`..`6de041e`) and `tier1-shared-tree` (`6de041e`..`04d6fe4`), 2026-09-24 — the corrections, C5's second subject, C12 and C13

The fourth and fifth rounds: the previous review's findings implemented, and three more invariants.
Every number here was re-measured on tree copies through the capture path, with this branch's tool
and with the tools at `lazari2`, `c1689ca` and `6de041e`, not read from the lane's logs; the lane's
own probe scripts were re-run as well, and where a lane log is the source it is named.

**Verdict: accept both branches, in order, and land one more clause before the integration refresh
— the narrowed exclusion's guard classifier accepts a shape that is not an include guard, and two
behavioural defines, one of them in `UtfMain.h`, are still silent because of it.** The lane's
correction of the previous review's finding 3 is right and is measured below; the record of that
finding should read *right in direction, wrong in both of its examples*. C13's replay cost is
acceptable and no exemption is missing. The disarm edge is tolerable for this integration, and the
guard that sits on it is about ten lines. The seventh gap is real, its severe form is a relocation
accident rather than an edit, and one of the lane's two probes for it does not reproduce where the
lane says it ran.

**What re-measures.** The selftest is red against `lazari2`'s tool on exactly ten lines (C5,
C11 ×5, C12, C13 ×3) and against `c1689ca` and `6de041e` on five each (C5, C12, C13 ×3); green
here on the committed 1082-case baseline and on `lazari2`'s refreshed 1083-case one, C4's control
reading 0 of either. `--compare` on the current tree reports the one expected `C2 case BODY
CHANGED: JsonPrettyPrintNestedLayout` against the committed baseline and **PASS** against a fresh
armed capture — 212 files, 717 members, 78 file-scope spans in 24 files, 27 shared files, 164 shared
members, 38 shared spans; `check_split.sh --tier1` is red on that one line. `live-c12.sh`,
`live-c13.sh` and `replay.sh` reproduce logs 02, 20 and 31 line for line; the hoist into the shared
tree reports `C6 helper member LOST` at `6de041e` and passes here; control E — `TestTasks8.h` moved
whole from `utf_baselib_tasks` into a new module with its own entry point — passes with and without
a BOM against the narrowed exclusion. The fresh manifest confirms the docstring's census: exactly two
file-scope shas with more than one copy tree wide, `using namespace bl;` twice and the THREAD_POOLS
define three times; 0 member or span shas shared between the shared tree and any module; 0 quoted
includes into or out of the shared tree; 0 cases in it.

**1. Finding 3 of the previous review: right in direction, wrong in both of its examples — the lane
is right.** Dropping `#define UTF_TEST_APP_INIT_DEACTIVATE_THREAD_POOLS ( true )` from
`utf_baselib_basictask` passes at `c1689ca` **and** here: three entry points carry that line, C11
keys `old_scope` and `new_scope` on the sha alone, and a sha with two surviving copies is not LOST.
Dropping it from all three reports C11 LOST here and passes at `c1689ca`, which is what the
narrowing bought; counting copies would close the single drop and red control E, whose entry point
writes the third `using namespace bl;` — two copies today, by grep. And all seven `UTEST_*` macros
sit inside case bodies — five cases in `TestServerErrorHelpers.h` and `TestAsyncRpcDataModel.h` —
where C2 hashes `lines`, not `shadow`, so the directive pre-pass never touched them: weakening
`UTEST_REQUIRE_PRESERVED` reports `C2 case BODY CHANGED: ServerErrorHelpersRedactionTests` at both
tools. *"Measured silent"* in that finding was not measured, or was measured on the wrong thing, and
*"eleven others, eight of them multi-line"* counted case-body macros as file scope; the census is
five at file scope in module files. Recorded here rather than edited in place, so the correction
stays visible.

**2. The narrowed exclusion's classifier accepts a shape that is not an include guard, and the
lane's proof sits beside that risk rather than on it.** `include_guard_define( )` accepts `#ifndef X`
followed by `#define X` by shape alone, and the define-if-not-defined idiom has exactly that shape.
Census over all 212 files: 74 guard-shaped conditionals, 72 of which close on the file's last
directive and are include guards, and **two which do not**: `utf_baselib_cmdline/TestCmdLineEhUtils.h:31`
— `#define SSL_R_SHORT_READ 219`, the packed reason code newer OpenSSL no longer defines — and
`include/utests/baselib/UtfMain.h:179` — `#define UTF_TEST_APP_INIT_UTF_ARGS_PARSER test::UtfArgsParser`,
the default argument-parser class of every module's app init. Both are among the lane's *"48
include guards"* and neither is hashed. Measured armed, on tree copies: `219` → `220` PASS; the whole
`SSL_R_SHORT_READ` block deleted PASS; the `UtfMain.h` default edited PASS; the `UtfMain.h` block
deleted reports only through the enclosing `template` span's line count at `:52` — by accident of
extent, not by reading the define. The two probes that proved the narrowing, `UTF_TEST_NORMALIZE`
and `BL_PLUGINS_CLASS_IMPLEMENTATION`, are *unguarded* defines: they establish that an unguarded
define is hashed and say nothing about what the classifier calls a guard. The fix is one clause — an
include guard's matching `#endif` is the last directive of the file — which separates exactly these
two; no guard in this tree is spelled `#if !defined( X )` and no file uses `#pragma once`, so the
`ifndef`-only shape has no false negative today. Blast radius: `is_include_guard( )` also feeds C3's
condition stack, and no case sits under either block, each being three lines; after the fix both
defines are C11 spans, ADDED against any armed baseline captured before it and blessed by the refresh
that is owed anyway — nil cost if the fix lands before that refresh. The docstring's *"five
behavioural #defines"* becomes six in module files and one more in the shared tree, and the tiers
table's *"the behavioural `#define`s"* is true only after this lands.

**3. C12 — agree with the anchor, and its controls sit on the risk.** P1–P3 reproduce (three, three
and one `CHANGED NAMESPACE` lines); the partition and the whole-block cut both PASS with nothing
reported, which is the property C6 was moved down to members for, and the block-sha argument holds
by reading — the sha covers the opening line. The set comparison is right for the same text under
two namespaces in one file; I looked for such a pair in the baseline to exercise the label under a
twin deletion and found none, so that label is unmeasured and unimportant. The selftest's C4 control
asserts the empty subject on whatever baseline it is handed, which is the right answer to a count
in a comment that `lazari2` had already made stale. **C5's second subject** is shape (i) as
recommended, red on the unfixed tools in both the selftest and the lane's capture-path probe, and
`main( )` still writes before it judges, so the shape is the adjacent one by choice, as recorded.

**4. C13 — every shape decision holds against its measurement, and spanning C6's identity across
both trees is the right resolution.** Not a module: `module_file_churn( )` and the C7 loops read
`manifest[ 'modules' ]` only, `<shared>` paths begin with `include/` so the roster clause can never
match them, and the accepted orphans, the quoted includes and the data hashes are unchanged at 4, 0
and 32. One tree-wide list: the hoist from `utf_baselib_loader/TestResolver.h` into
`include/utests/baselib` is LOST at `6de041e` and PASS here, reproduced — and hoisting a helper into
the shared tree is the one sanctioned way to share it across the modules a split creates, since
`AGENTS.md` forbids the cross-module include, so a C6 red on it would be the gate failing its
purpose; the price is C6's existing copy-count blindness extended across the boundary, and 0 shas
cross it today. `<shared>` as its own module label keeps the duplication check asking only whether
the shared tree redefines something inside itself, which is enough: a module copying a shared helper
verbatim lands both copies in one translation unit, where the compiler reports it. Unarmed,
`without_shared( )` trims the current side and the differential half is the one that ran at
`6de041e` — the same single C2 line, and the scope lines count `judged` rather than the whole
manifest — while `check_intrinsic( )` still runs over the whole manifest, so the intrinsic half is
strictly stronger and passes. The two hard C13 failures fire in the selftest. P1–P4 report under C6,
C11, C10 and C12 respectively, exactly as the commit says.

**5. The `f992e2f` cost — acceptable, and no exemption is missing.** The eleven new lines are one
C10 (`<utests/baselib/UtfCrypto.h>` added to `TestMessagingUtils.h`), one C6 LOST and one ADDED for
`TestMessagingUtilsT` — the class is one member, and its declaration changed when the bodies left —
and eight C6 ADDED for the out-of-line bodies in `TestMessagingUtilsImpl.cpp`. Every one is a true
statement about text every module compiles. An exemption that silenced them would have to recognise
*declaration minus bodies ↔ bodies elsewhere*, which is a semantic match rather than a textual one,
and a smuggling path: an edit inside an out-lined body would ride through it. Reduction option 3 is
not a relocation, `AGENTS.md` now says so where it is first met, the refresh is where its diff is
reviewed, and tier 3's per-case assertion counts are what stands behind an out-lining's behavioural
equivalence. The module side of option 3 — the forwarding `ImplTestMessagingUtils.cpp` per module —
was already silent, being a new file whose head is prose. The three earlier rounds each asked this
question of lines that sat *on a relocation* — the roster, the four prose heads, and finding 3's
supposed hole — and each needed a narrowing or a correction; these lines sit on an edit, and the
answer is different: leave them.

**6. The disarm edge — tolerable for this integration, and the guard that sits on the risk is about
ten lines.** `lazari2`'s baseline at `b01f98e` carries `cases`, `members`, `modules`, `namespaces`
and nothing else — 1083 cases, no `file_members`, no `shared` — after three refreshes by pre-C11
tools (`3c57955`, `6e97b86`, `fd6d80a`). Nothing was disarmed, since nothing was armed; after the
integration refresh, the paths to a *persisting* disarm are an `inventory.json` conflict resolved by
taking a lane's copy, and a direct commit to `lazari2` from a checkout carrying the old tool — every
lane refresh rewrites the same file, so the conflict is the surface, and its resolution is the risk.
Three guards, cheapest first: *(a)* the verdict line — `PASS - all invariants hold` should say `C11
and C13 not armed` when they are not, and `check_split.sh`'s summary should print `C1-C10; C11, C13
unarmed` rather than `C1-C13`, so the note becomes part of the one line a reader looks at; *(b)*
`--capture` refusing to overwrite an existing baseline whose top-level keys are a strict superset of
what the running tool writes — schema-free, and from that commit on no tool can disarm a later tool's
baseline; it cannot help against the pre-C11 tools that exist today, which only *(c)* covers; *(c)*
the rule, one line in the refresh paragraph: an `inventory.json` conflict is resolved by re-capturing
with the integrated tool, never by picking a side.

**7. The seventh gap — real, at file scope exactly as the lane says; at member level the lane's probe
does not reproduce where it says it ran, and the severe form is a relocation accident, not an
edit.** `TestBaselibDefault5.h` carries `#if ! defined( _WIN32 )` twice: at `:38`, file scope,
around `#include <pthread.h>` and a comment; and at `:396`, inside the anonymous namespace opened at
`:393`, around the named-mutex helpers. Inverting `:38` passes — that is the measurement log 40
describes, mislocated as *"around FIVE helper members"*. Inverting `:396` reports `C6 helper member
LOST: #if ! defined( _WIN32 )` and `ADDED: #if defined( _WIN32 )`; a unique condition there likewise;
deleting the `#if`/`#endif` pair reports two LOST. The reason is an accident: `split_members( )`
knows nothing about directives, so a directive line separated by blank lines is a one-line member —
six exist tree wide (`#if defined( _WIN32 )`, `#else`, `#endif`, `#if ! defined( _WIN32 )`,
`#endif // ! defined( _WIN32 )`, `#if 0`) — protected by text identity with its copy-count
blindness, so a second copy of a bare `#else` or `#endif` member would leave both unprotected.
Narrowing `Utf.h`'s `main( )` gate passes, as the lane says, and `UtfMain.h`'s default is finding 2.
So the file-scope half — 10 spans — is fully blind, and the member half — 19 members by the lane's
count, 21 by mine, the difference being the directive lines themselves — is guarded by accident.
**The severe form:** `namedMutexSemaphoreKey( )` cut from under `:396` into a sibling header without
the guard, roster edited — eight lines moved verbatim with `split_members( )`'s own extent, which is
how every split here is cut and how the lane's own controls cut — **passes tier 1**, and the new
header now declares a `::key_t` on Windows. That is a relocation accident in the class the gate
exists for, and it is silent. Severity: medium — the consequence on the other platform is usually a
compile error, which is loud, but a condition that selects behaviour rather than availability
changes behaviour silently, and the accident needs no intent. Closing cost: a condition stack
recorded per member and per file-scope span — a per-line precomputation, since the walk's
`cond_stack` is the stack at the namespace *opening*, not at each member — and folded into the
**identity** C6 and C11 compare, sha with guards, so that a helper moved out from under a guard
reads LOST and ADDED; a per-file anchor like C12's would be silent on exactly the accident, since
the accident is a move to another file. Plus a selftest section, a refresh, and the re-spelling
question C3 already lives with. Blast radius: both identities, the selftest, the baseline; the
false-positive surface is a new header written under a guard the old one lacked, which is the
surface C3 already accepts for cases. It is the maintainer's shape decision, as the lane says, and it
belongs after the classifier fix and the refresh, or the refresh happens twice.

**8. Docs.** The tiers table's *"the behavioural `#define`s"* is one notch broader than true until
finding 2 lands; *"all of it over `src/utests/include/` too"* is qualified correctly in the paragraph
below it. The reduction paragraph's *"eleven such lines"* is exact and worth keeping, since it is
what a reader of a red gate will see. `check_split.sh`'s `C1-C13` is a claim about what ran, as
before; finding 6(a) resolves it.

**Decisions, in the order to take them.** *(1) The guard classifier*, one clause, a commit on top of
`tier1-shared-tree` — not on `tier1-filescope`, since its successor is chained on its tip. What it
is: an include guard is `#ifndef X` / `#define X` whose matching `#endif` is the file's last
directive. If not done: two behavioural defines, one of them the default parser of every module,
stay unhashed for good, and the refresh blesses their absence. Risk low, blast radius the classifier
C3 and C11 share, 72 real guards unchanged; the two probes above are the controls, and control E
re-runs. Undecided: `#endif`-last alone, or first-conditional-and-`#endif`-last; recommend the
former, reversing if a header in this tree ever carries a directive after its guard's `#endif` —
none does. *(2) The verdict line and the summary line* when C11 or C13 is unarmed: no behaviour
change, and it is where the note has to live to be read; recommend yes. *(3) `--capture` refusing to
write fewer keys than the file it would overwrite*: in plain English, a tool that does not know
about a check cannot erase a baseline that does. If not done: the next stale checkout to refresh
disarms C11 and C13 with only a note to say so, and the note is the thing this tool has been caught
behind three times. Risk low, about ten lines, blast radius `--capture` only; the refusal must say to
delete the file first if a downgrade is ever meant. Recommend yes; reverses if the refresh workflow
ever legitimately writes from an older tool, which nothing does. *(4) The refresh at integration*,
once, after (1): it blesses C11's list including the two classifier spans, finding 1's corrected
case body and C13's shared tree, and it arms all three. *(5) The seventh gap*, to the maintainer,
shape recommended above, after (4). Both branches are accepted as they stand; (1)–(3) are
follow-ons, none of which changes what the reviewed commits already prove.

**Could not settle here:** whether the lane's fifth-gap inversion ran at `:38` — no script was
kept, and the description fits `:38` and not `:396`; the `core.autocrlf` BOM question, carried from
the previous review; whether the six directive-line members should be excluded from
`split_members( )` once a guard stack exists, since they would then be redundant and are the only
protection today; and the C12 label under a same-file twin deletion, for which the tree offers no
pair.

## Review of `tier3-platform-stamp` (`3079edd`), 2026-09-24 — the tier-3 baseline says which platform it speaks for

This closes the finding `tier3-client-modules-not-baselineable-record.md` §4 recorded as *"tier 3 is
already unusable on Linux against this baseline … **not** fixed here"*. That finding never reached
this list as a row; it is recorded here as **landed**, and the record wants a one-line pointer at
merge. Every number below was re-measured: the lane's `controls.sh` and `rendering.sh` regenerate
`controls.txt` and `rendering.txt` **byte-for-byte**; three further checks the lane did not make are
in `evidence/tier3-platform/review-checks.sh` and its `.txt`, regenerable the same way.

**Verdict: accept, with two things folded in before it merges and one correction to the record.**
The `tee` the commit adds to keep the progress lines streaming is what stops them — a correct
conclusion on a false premise, measured — and two `flush` tokens close it. The platform check on
`--nondeterministic` is the decision already taken applied to the other comparison the tool makes,
and folds in at five lines. The correction: **251 and 813, the commit message's own "before" numbers
for the risk, are not measurements of the platform risk** — 43 of the 251 are, and none of the 813.
The behaviour is not in doubt: the mismatch is refused, the refusal reaches the gate as a SKIP on
both paths, and a real regression on a matching platform still reds it.

**1. Refusing an unstamped baseline is right, and "costs nothing" is false for one real flow.**
Unknown is not a match; the only unstamped baseline in existence is also cross-platform; and
"accepted with a note" is the note this tool has been caught behind three times. But the cost does
not land only *"where the platform is unknown"*: `g1-gate.sh` captures the parent with the **parent
checkout's** tool (`RUNLOG` is relative and phases 2–3 `cd "$BASEWT"` at `PARENT`) and compares with
the tip's, so any `PARENT` before `3079edd` yields unstamped base captures and a phase-7 `REFUSED`,
exit 3 — with the platform perfectly known to the operator (`bld/$PLAT`) and unknown only to the
file. What the refusal is catching there is a second thing worth catching: **two tool versions in one
comparison**, across `fd0b6f1` and `266bc95`, which changed what the parser sees. The fix is one
runbook line outside the repo — `RUNLOG=$REPO/scripts/utests/utf_runlog.py` for every phase — not a
`--platform` override; the *"cannot be laundered"* comment is right, since a hand edit to the JSON
shows in a diff and a flag would be used by reflex. `lane1-t3-analyse.py` reads captures raw and will
now `AttributeError` on a stamped one: loud, two lines, the runbook's.

**2. The stripping is complete inside the tool, and the mixed states are loud.** Every snapshot read
goes through `load_snapshot( )` — `--nondeterministic` twice, `--against`, `--compare` — and the two
remaining `json.load`s read the unstable list and the reasons map, neither a snapshot.
`collect_by_running( )` and `collect_by_parsing( )` key on binary and log names, so cannot produce
the key; `stamp( )` copies, so the summary's `len( snapshot )` is the module count; `restrict( )`
runs after the refusal on stripped dicts. Control 2's proof is sound for a reason worth stating: a
leaked key reaches `union( )` as a string, and the old tool handed the new baseline shows exactly what
that looks like (`AttributeError` at the summary line, `old-tool-new-baseline.txt`), so 57 byte-identical
outputs are not consistent with a leak. Old tool with new baseline fails before comparing — the safe
direction. One cosmetic: `--against <unstamped> --bld <tree> --capture` writes unstamped, because the
file's `None` wins by design, and then says *"no --bld given"*.

**3. The status reaching the switch is the tool's on both paths — and the `tee` defeats its own
reason.** `check_split.sh` is `set -u` only (line 39): no `-e`, no `pipefail`, so the pipeline never
aborts the script and `PIPESTATUS[0]` at line 224, the very next simple command, is the tool's. The
rendering's `regress` variant is the discriminating control — were `tee`'s 0 what reached the switch
it would have printed PASS and GREEN; it prints FAIL and RED on both paths — and it reproduces. But
the comment says *"piped through tee rather than captured, because --run prints a line per module as
it goes and that is the only progress signal a long tier 3 has"*, and with stdout a pipe CPython
block-buffers and the tool never flushes. Measured with six stand-ins sleeping 2 s each: **through
the pipe all six progress lines arrive at 12.11 s, at exit; on a pty they arrive at 2.04, 4.05, 6.07,
8.08, 10.10, 12.12.** So on the interactive `--run` path this commit removes the signal its comment
preserves, and it matters most in the case the record documents — a module hanging to its 600 or
1800 s timeout now shows a blank screen rather than the last module that finished. `$( )` and `tee`
are equally silent; the variable is the flush. `flush = True` on the two progress prints
(`collect_by_running( )`, `collect_by_parsing( )`) closes it and also makes `g1-gate.sh`'s
`>>log` phases move, which they never did — the monitoring rule's trap 2 for this tool. Windows,
cosmetic: CPython there writes `\r\n` into the pipe and the `sed` leaves the `\r` in `reason`, so the
SKIP summary line would carry a trailing CR; `| tr -d '\r'`. No runbook shows the Windows agent
running `check_split.sh`, so this is unmeasured.

**Exit 3 is not a reserved code.** `utf_inventory.py` returns 3 for a PARSE PROBLEM, which the same
gate script renders as tier-1 **FAIL**; `build-slot.sh`, which every lane run goes through, exits 3
for *stopped before running, low disk*. So 3 means fail in the sibling, skip here, and never-ran in
the wrapper. Not a defect today — `check_split.sh` does not run through the slot and its tier-1
branch reads only zero/nonzero — but a `--run --compare` wrapped in the slot and switched on 3 would
render a low-disk stop as GREEN. One line in the runbook, or a different code in one of the two.

**4. The 57 pairs do exercise matching comparisons.** Under the old tool the 56 consecutive client
pairs are **41 PASS and 15 FAIL**, plus the win-x86 pair PASS, so byte-identity spans outputs
carrying real `OUTCOME CHANGED` and `NO LONGER RUNS` lines from the flakes the record documents; and
since the new tool reaches `compare( )` only past the stamp check, identical FAIL text proves the
check passed and the comparison ran, not that the stamp is inert. The rendering's last block covers
the two gate paths, where the after-side stamp comes from `--bld` rather than a file. The gap: the
win-x86 pair is the only one whose stamp is the committed string — every Linux pair was restamped by
the harness — and no pair runs real binaries; the stand-ins take the same code path. Acceptable.

**5. `--nondeterministic` — agree it folds in; "three lines" is the first half.** `nondeterministic( )`
compares two captures' assertion counts, which across two platforms is precisely *"a comparison
across a platform mismatch"*, and the predicate, the exit code and the message function already
exist: read the two stamps, `platform_refusal( )`, print, `return 3` — five lines and one control.
The second half is the list itself. `nondeterministic.json` is a bare list with no stamp, so
`--compare` cannot check that the list it applies was derived on the baseline's platform. The
committed list is the win-x86 pair's and today every comparison that passes the stamp check is
win-x86, so it is consistent **by accident**. The moment a Linux baseline is captured — the record's
own reversal condition for twelve modules — a stale list compares eleven cases on outcome only where
they may be stable: the silent weakening the orchestrator names. And the refresh sentence this commit
adds to `AGENTS.md` — *"the answer to either is `utf_runlog.py --run --bld <tree> --capture …`"* —
is one capture, where `91d5c2c`'s own message says pass 2 is kept so the list can be re-derived. That
half is a shape decision, below.

**6. Refusing after collection — the justification does not apply to the path everyone runs.**
*"--run still writes the capture the operator needs"* holds only where `--capture` is given, and
`check_split.sh`'s `--run` branch (lines 247–248) does not give it: on a mismatched tree every binary
runs, up to 1800 s per module, then nothing is kept and the operator is told to run again. Both
inputs to the refusal — `args.bld` and the baseline's stamp — are known before the first binary
starts. Cost, not correctness; but the trade as recorded is not the trade the gate makes.

**7. Leaving 37 in `AGENTS.md` is right; 251 and 813 must not be cited as platform noise.** The
record names its branch, tree and date, and 24 over ten modules is consistent with 37 over
seventeen, both `gcc1520 debug`. What I classified against the tree: of the **251**, **43 are
platform** — nine `#if defined( _WIN32 )`-guarded registrations in `TestBaselibDefault*.h`, twice
(LOST and NO LONGER RUNS), 24 assertion counts, one skipped-set line — and **208 are the baseline's
age**: 86 cases relocated by splits landed *after* `91d5c2c` on the same day (38 to `utf_baselib2` at
`1629478`, 42 to `security2`/`3` at `c18829c`, 5 to `io2` at `77ef537`, 1 to `apps2`), twice, and
18 cases added since, twice — the lane's per-family method sees a case moved out of the family as
lost. And **813 = 741 + 36 + 36**: every baseline case NO LONGER RUNS because the six-module log tree
contains none of the 17 baseline modules, the six modules' 36 cases NEWLY RUNS, the baseline's 36
skips — **zero platform**; that tree reds identically on Windows. The conclusion is right (the
platform *is* mismatched and refusal is correct); the evidence line for *"the risk itself"* is mostly
something else. Recorded here so the next reader cites 43 or 37, not 251 or 813. One word in
`AGENTS.md`: *"run against a Linux tree it **reports** 37"* is now *reported* — the tool refuses.

**8. What the tree name does and does not identify.** `ub24-a64-clang2010-debug` encodes OS, arch,
toolchain and variant, and one baseline per name is what per-platform means: under the lane and
orchestrator split, a Linux tier 3 in force needs three captures (clang debug, clang release, gcc
debug). It does not encode SVE on a virtualised a64 host, JNI availability or a Rosetta container —
within-name variation is the unstable list's job and the name is necessary, not sufficient. A cost to
know, not a defect.

**Decisions, in the order to take them.** *(1) `flush = True` on the two progress prints*, folded
into this branch. What it is: the per-module line reaches the operator as each module ends, through
`tee` and through a log redirect alike. If not: the interactive `--run` path is blind for the whole
run, and blind exactly when a module hangs. Risk nil, blast radius two print statements; the control
is `review-checks.sh` §1, which should show the pty timings through the pipe. Undecided: two
`flush = True` versus one `sys.stdout.reconfigure( line_buffering = True )`; recommend the two, since
`reconfigure` is 3.7+ and the dist interpreter's version is not pinned in the docs. *(2) The
two-run stamp check on `--nondeterministic`*, folded in — a consequence of the decision, five lines,
and one line in `controls.sh`. *(3) Refuse before collecting when `--compare` is given and
`--capture` is not*: about eight lines in `main( )`; if not done, a mismatched `check_split.sh --run`
costs a full run for a SKIP it could print at once. Recommend yes; reverses if anything reads the
module-count summary of a refused run, which nothing does. *(4) Stamp the unstable list*: it becomes
`{ "__platform__": …, "unstable": [ … ] }`, `--nondet` reads the bare list as unstamped, and an
unstamped or mismatched list is refused as the baseline is; the committed list is converted once.
About twenty lines and a file-shape change, which is why it is a decision and not a fold-in. If not
done: the first Linux baseline inherits a Windows unstable list silently. Recommend yes, before that
first capture, since that is when it goes live; reverses if the committed list is retired for lists
derived at gate time as `g1-gate.sh` does, in which case delete it instead. *(5) Docs*: *reported*;
the refresh sentence says capture twice and re-derive the list; the record's §4 points here.
*(6) Runbook, outside the repo, the orchestrator's*: one tool for every `g1-gate.sh` phase,
`load_snapshot( )` in `lane1-t3-analyse.py`, and the exit-3 aliasing with `build-slot.sh`.

**Could not settle here:** the Windows rendering, never run on this host — the `\r`, and whether
the Windows tree the agent builds is spelled `win-x86-vc143-debug` exactly, since anything else
gets a SKIP there too; the record's own 105/37 method — whether its Linux capture was `--only` the
17 or a full tree is not stated, and it is not reproducible here without seven network-heavy modules,
so it stands on attribution; and which of `build-slot.sh` or the tool should give up the code 3.

## Review of `tier1-guards` (`df9470c`, `41e6b03`), 2026-09-24 — the capture guard, and the seventh gap closed

The sixth round: decisions (3) and (5) of the previous review, implemented. Every number here was
re-measured — an independent walk with its own include-guard rule and both boundary rules, the
lane's mutated trees re-judged with both tools, two relocation controls of my own, the probe fix
applied alone at `99aaff7`, and the baseline's history across every ref — not read from the logs.

**Verdict: accept both, as a re-commit of three rather than two — the probe fix out and first,
commit 1's message and its `AGENTS.md` sentence corrected, since they rest on a loss that never
happened, and one wording in commit 2 corrected.** Nothing in either design or either implementation
needs to change. The identity C6 and C11 now compare on is the right one, its boundary rule is C3's,
its raw-text spelling is the right price, the control that was missing now sits on the risk, and
"not in force" is bit-identical by construction and by measurement.

**What re-measures.** The refreshed baseline is 854 insertions and 0 deletions: 767 `"guards": []`
and 29 non-empty (29 openers, 29 conditions, 29 closers). My own walk over all 796 members and
spans gives **18 and 11 under a condition with 0 mismatches** against what the baseline records; the
*after*-line rule gives 19 and 11. The selftest is red at `99aaff7` on the C13 probe, and **green with
the three-line fix applied alone** — exit 0, nothing else touched. Controls 1 and 2 re-judged with
both tools reproduce log 06 line for line. My own: `namedMutexSemaphoreKey( )` cut into a sibling
under a *different* guard reports two `GUARD STACK CHANGED` lines, *was under `if ! defined( _WIN32
)`, now under `if defined( __linux__ )`*, plus two ADDED for the sibling's blank-separated
directives — the old tool gives the two ADDED alone; the same guard *re-spelled* in the sibling
reports the same shape. No ref ever carried a six-key baseline before `adc00c8`. Every list in the
manifest carries one key set per entry; below entry level there are only `modules[].files` and
`modules[].data_files`, a map keyed by file name. Eight `#else // …` lines exist tree wide and no
case, member or span sits under one. One file-scope span holds a conditional inside its extent —
`UtfMain.h:52`–`370`, 319 lines — and inverting `#if BOOST_VERSION < 105900` at `:96` **passes**.

**1. The probe fix — split it out and land it first; the lane's defence is an ordering argument,
and ordering satisfies it.** The fix is three lines at a different place in the selftest from the
seven probes, it depends on nothing in the guard work, and applied alone at `99aaff7` it turns the
selftest green. So "could not land green probes on a red selftest" is true and is answered by landing
the fix *before* the probes, not *with* them. Two rules say so, not one: unrelated changes are not
mixed, because a selftest that reds later would bisect to `41e6b03` for two reasons; and a live
defect that hands a caller a wrong answer — the whole selftest FAIL for everyone since `adc00c8` — is
scheduled on sight as its own change-set, which is a stronger reason than the mixing rule. Order:
fix, then 1, then 2.

**2. The census — the lane is right, and the brief's numbers were counted under a different rule
and before the classifier fix.** The boundary rule is C3's by code, not by analogy: `condition_stack(
)` and the case walk push after reading a `#if`, append `| else` on reading a `#else`, and pop on
reading the `#endif`, so the stack a line sees is the one in force before it is read. A case can
never sit on a directive line, so C3 never faced the boundary; this is the consistent extension. The
brief's **19** is the *after*-line count, where a `#if` member carries its own condition and an
`#endif` member none — 18 + 3 − 2. The brief's **10** predates the classifier fix: `#define
SSL_R_SHORT_READ 219` at `TestCmdLineEhUtils.h:32` became a span under `ifndef SSL_R_SHORT_READ`
when that `#ifndef` stopped being a guard, and its twin at `UtfMain.h:179` sits inside the span at
`:52`, so it adds none. One correction: *"six one-line directive members"* is three. `UtfPluginFixture.h:105`,
`TestBaselibDefault5.h:396` and `:427` are one line; `UtfPluginFixture.h:91` and `:98` carry the
`.dll` and `.so` definitions, and `TestMessagingDefault.h:491`'s `#if 0` carries
`exceptionThrowHook1( )` — multi-line members whose *first* line is a directive. The three-under,
three-not split is exactly right; the word is wrong in the commit message, the `scan_file( )`
comment and log 09, and it matters for decision B below.

**3. Raw text — worth it, and the honest boundary is that the identity is the line, not the
condition.** All four reasons hold, and the tree supplies the fifth: `Utf.h` spells one condition two
ways, `defined(UTF_TEST_MODULE)` at `:20` and `defined( UTF_TEST_MODULE )` at `:107`, which is
exactly what a whitespace normaliser would tempt, and normalising here alone would part the members'
spelling from C3's. The cost measured on the shape it bites: a split that re-spells the guard in
the sibling reds — and it *already* reddened under the old tool when the directives were
blank-separated, by the one-liner accident; the guard half makes that shape-independent. The cost
nobody named: `group( 2 )` is everything after the keyword, comment included, so the eight `#else //
…` lines put a comment into the identity of whatever sits under them. Zero instances today on
either side, and C3 has carried it since it existed. A trailing-comment strip is lexical rather than
a normaliser and blesses nothing, but it changes C3's stored text for a refresh's worth of nothing.
Recorded, not acted on.

**4. The control — the distinction is real, and control 1 is the one that sits on the risk.** LOST
and ADDED come from a sha present on one side only; `GUARD STACK CHANGED` from a sha present on both
under different stacks — disjoint by construction in `span_comparison( )`, `key[ 0 ] not in new_text`
on the one and `& new_text` on the other, and the selftest asserts the disjunction on the severe
form. Control 2's two-plus-five verified, the fifth being the `#endif` one-liner at `:427`, which is
the boundary rule showing. But control 2 is an edit, a step to the side of the risk; **control 1 is
the relocation** — PASS to two `GUARD STACK CHANGED` with no directive moving at all — and that is
the control that was missing before and is on the risk now. My different-guard relocation puts the
*was/now* on the accident's most likely form. What the selftest does not pin: every probe mutates a
manifest, so `condition_stack( )` and the boundary rule that decides identity are asserted nowhere in
the repo — the earlier finding 8, now with a subject. One probe on a synthetic line list (a `#if`
one-liner, an `#endif` one-liner, an `#else` branch, an include guard) closes it.

**5. Bit-identical — the right property, proven by construction, exercised where it has teeth.**
With `armed` false the keys are `( sha, () )`, `setdefault( )` keeps the first entry, LOST and ADDED
are sorted by sha in that order, and `reguarded` is provably empty — the old loops verbatim. Of the
six trees, c1, c3 and c4 are the ones with teeth: an armed run reports and an unarmed must not, and
c1 is exactly the case the guard half exists for. c2 and c5 exercise LOST plus ADDED; c4b exercises
nothing in `span_comparison( )`, its two lines being the intrinsic half. A LOST of a sha with two
copies is not exercised and is the same by construction; the md5 is of the *sorted* list, so it
proves the set and not the order, which is also the same by construction. One asymmetry, minor:
`guarded` is read from `before` alone, so `--compare NEW --against OLD` would print 29 spurious
guard changes — that direction is already broken for C11 by the "extraction failed" hard failure,
so this is consistent rather than new; one `and carries_guards( after… )` if anyone ever runs it.

**Commit 1 on its own — accept the code; the premise is false, and it is written into `AGENTS.md`.**
The refusal is right: strict superset only, both-ways disagreement written, exit 4 distinct from the
parse code 3, `check_split.sh` treats any non-zero as FAIL, and an unparseable file — a
conflict-marked one — is overwritten, which is the rule's own remedy when the writing tool is the
integrated one. Controls A–E sit on the risk. But *"each of those writes dropped `file_members` and
`shared`"* did not happen: no ref ever carried those keys before `adc00c8`, the four refreshes
(`21b0c37`, `3c57955`, `6e97b86`, `fd6d80a`) were pre-arming refreshes that dropped nothing, and the
previous review's finding 6 says exactly *"nothing was disarmed, since nothing was armed"*. The
guard is preventive, and the trigger it prevents is real — `s6r3-1` carries its own baseline commit
`b814ed9` with a tool that predates all of this — so keep it; but the `AGENTS.md` sentence *"That is
not hypothetical — …"* must be corrected in the re-commit, because the next reader will cite it.

**Decision A — one level deeper is not moving the hole; it is the deepest level a schema-free
comparison is sound at for this manifest.** Depth 3 is data-keyed: `modules` by module name,
`modules[].data_files` by file name, so a path union there would refuse the ordinary refresh that
removes a module or a data file, calling the tool older. Every field this tool has ever added lives
at depth 1 or 2. Recommend: top-level keys, plus the key set of the first entry of each list and of
the first value of `modules`, the same strict-superset rule at each level, the depth-3 residual named
in the docstring, and a selftest assertion that every entry of a list carries one key set — measured
true — so "first entry" is a checked precondition rather than an assumption. Reject the generation
stamp: hand-maintained, and "forgot to bump" is the silent failure this tool has been caught behind.
Reverses the day a field is added below entry level, when the stamp becomes the only exact option.

**Decision B — a fix, not papering, provided it is scoped to what it measured.** The duplication half
asks *is this an ODR risk*, and a bare conditional directive never is, whatever `split_members( )`
ought to emit — so excluding it there is the half's own answer. Scope it to members whose *every*
non-blank line is a conditional directive: three today, not six, since three of the six carry code
and must stay judged. It is a live wrong answer — a red gate on a legal split, identical before and
after — and is scheduled on sight by the project's own exception. The root question is now
decidable and separate: with the stack in the identity, a directive-only member reports nothing the
guard half does not, except the text of an *empty* region where nothing compiles differently, and
it costs double reporting — seven lines for five facts in controls 2 and 5. Dropping them at
extraction changes the member population (717 → 714), needs a refresh and touches the extractor
every invariant reads: take it at the next refresh-bearing change, not now. The exclusion is a
strict subset of it and needs no reversal.

**Residuals, recorded against C11's shadow rather than against these commits.** (i) A conditional
that opens *inside* a bracketed file-scope span is invisible to both halves — its directive is
blanked from the shadow and the stack is recorded at the span's first line. One instance:
`UtfMain.h:52`–`370`, the app-init template, with `#if BOOST_VERSION < 105900` at `:96` and
`#ifndef UTF_TEST_APP_INIT_UTF_ARGS_PARSER` at `:179`; inverting `:96` passes. The earlier finding 5
said zero instances — the shared tree was not scanned then. The fix shape recorded there, hashing
`lines` over the extent, still stands; members are not exposed, their sha being over `lines`
already. (ii) The `--against` asymmetry above. (iii) The baseline refresh rides in commit 2 as the
arming refresh; if `lazari2`'s baseline moves before merge, the orchestrator re-captures with the
integrated tool — commit 1's own rule — and gets the same pure insertion.

**Decisions, in the order to take them.** *(1) Re-commit as three*: the fix; commit 1 with its
message and the `AGENTS.md` sentence corrected; commit 2 with *"six one-line"* corrected. If not
done: a bisect lands on one commit for two reasons, and project documentation cites a loss that did
not occur. Risk nil, blast radius the history of one lane branch; undecided only whether the fix
precedes commit 1 or follows it — recommend precedes, since it is independent of both and is what
makes every later selftest run green. *(2) Decision B's exclusion*, its own commit on top, no
refresh, with control 4b as the control and the three one-liners as the census. *(3) Decision A*,
its own commit, same tool, after (2). *(4) The `condition_stack( )` probe*, with (2). *(5) The
extraction-level drop and the interior-conditional hash*, one decision round at the next refresh.

**What moved on the list.** From the previous review: (3) the `--capture` refusal — **landed** in
`df9470c`, pending the re-commit; (5) the seventh gap — **landed** in `41e6b03`, pending the
re-commit; (1) the classifier and (4) the refresh — **landed** before `99aaff7`, `adc00c8` arming all
three. Two "could not settle" items are now observed a second time and are converted: *whether the
directive-line members should leave `split_members( )`* is decision (5) above, and *the selftest
cannot see the extractor* is decision (4).

**Could not settle here:** nothing was run on Windows, and the tool runs there — `keys_a_capture_would_drop(
)` opens the baseline with no encoding argument exactly as `--compare` already does, so nothing is
new, but nothing is measured; whether the orchestrator's merge conflicts on the baseline, which
depends on the state of `lazari2` at that hour; and the trailing-comment strip, zero-instance today
on both sides and left with C3.
