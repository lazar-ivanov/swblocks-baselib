# What the Windows matrix owes this work — handoff

**Date:** 2026-09-24. **For:** whoever runs the Windows matrix. **Status:** the complete list at the
end of the astra remediation and the batch that followed it.

**RUN ON WINDOWS 2026-09-24.** Items 2, 3, 6, 8, 9 and 10 are settled, item 4 is measured and its
narrowing taken at `ed0ced5`, items 1 and 5 turned into decisions — taken and done 2026-09-25 —
and item 7 was measured and closed on 2026-09-25. The run also found one product defect none of the
items named, the TLS wrapper's by-value handler, fixed at `9ca4678`. Tier 3 ran on the platform it
was built for, and its baseline was refreshed there on 2026-09-25, over 28 modules. Every result is
in `astra-remediation-owed-work.md`'s Windows rows, 4 to 5c and W1 to W9, which is where to read
them; the text below is kept as the instructions it was.

**SECTIONS A TO D RUN ON WINDOWS 2026-09-29**, at `edd921b`, which contains `5db9ae8` and `f0890bd`.
Every item is answered and settled, three of them through decisions the maintainer took on
2026-09-29: the HTTP/2 test peer's close, whose reset destroyed a response on a loaded Windows host
(`h2client2`); A6's stalled sink, whose short write raced the cap's cancel; and E2's Linux-only
count, which holds on Windows too. The gate ran on its own platform three times, tiers 1 and 2
passing throughout. Tier 3 was red on the first run for W11, W12 and one failure each of two other
modules which then passed 10 of 10; on the second - the fixed tree - for a harness race in
`h2client4`, found and fixed (W18), and one abort of `blobtransfer2`, not reproduced (W19); and on the
third, with W18 fixed, every module exited clean and one case of `utf_baselib_tasks` went unseen, not
reproduced. Every result is in `astra-remediation-owed-work.md`'s rows W10 to W20, which is where to
read them; sections A to D below are kept as the instructions they were.

Everything here was found on Linux and **cannot be settled there**. Two of the items are things a
Linux run *provably* cannot reach — not "has not yet", but cannot. The rest are measurements this
host has no way to take.

Each item says **what to run**, **what to read**, and **what each possible answer means**, so that a
result is a decision rather than a data point. Where an item can only be answered one way, that is
said too.

---

## Start here: the state you are inheriting, and the order to work in

**The branch is `lazari2`. Nothing is pushed** — the maintainer pushes. Do not merge to it from a
lane; commit on your own branch and let the merge be deliberate.

**What Linux has already established, so that anything you find is yours and not inherited:**

| | |
|---|---|
| whole-suite G1 gate | **clean** over `9cd211c`, `gcc1520` `debug` — see `g1-gate-result-2026-09-24.md`, which also explains why it exits 1 and why that means nothing |
| tier 1 | **PASS**, C1–C13, and `selftest_inventory.py` PASS at 76 probes |
| tier 2 | **SKIP — it has never run anywhere.** It needs an x86 debug tree, which only you can make |
| tier 3 | **SKIP on Linux by design.** Its baseline speaks for `win-x86-vc143-debug`, so **your host is the only place it can run** |

**Two of the three tiers have therefore never been exercised on the platform they were built for.**
That is the largest single thing this handoff is asking for, and it is not in the numbered list
because it is not a measurement — it is the gate itself finally running.

**The order that gets the most out of the least work:**

1. **Build `win-x86-vc143-debug` first.** It answers **item 8** as a side effect — if a module is over
   the 75 MB ceiling the build fails and names it, and if it succeeds the sizes are there to record.
   It is also the tree tiers 2 and 3 both need. **Name it exactly that**, or tier 3 will refuse: the
   stamp is the basename of the `--bld` tree and the baseline says `win-x86-vc143-debug`. See §10.
2. **Run the three tiers** against that tree — `check_split.sh --bld <tree> --run`. This is the first
   time tier 2 will have run at all and the first time tier 3 will have run since its baseline was
   captured. Expect the coverage statement naming 17 uncovered modules; that is normal and §10 says
   why.
3. **Then the behavioural items, 1 through 7.** These need runs of specific cases and are
   independent of each other, so take them in whatever order suits the machine. **Item 1 needs dozens
   of iterations** because the abort case is a race — budget for that rather than concluding early.
4. **Item 5 is a decision, not a measurement**, and it is the only one here that is. Read it before
   you build anything extra.

**A build on x86 is where this project has previously been unable to compile at all**, so if step 1
fails, that is a result worth reporting immediately rather than working around — `src/utests/AGENTS.md`
carries the module-size rules and the splitting procedure.

---

## Read this first, or item 1 will mislead you

`notes/plans/issues/windows-peer-close-error-codes-record.md` opens with *"the divergence is
permanent — it is in the operating system, not in this library."* **That status line is superseded by
the section directly below it**, added 2026-09-23: the divergence the record measured was
**self-inflicted**, by this library's own `shutdown_both`. `bb53bdd` changed
`TcpSocketCommonBase::shutdownSocket( )` to `shutdown_send`, and the measurements change with it:

```
shutdown_both   the reader saw 10054 / 10053 and 0 of 16384 bytes
shutdown_send   the reader saw eof and 16384 of 16384 bytes
```

**The rule still stands** — ask `net::` predicates, never hand-compare a transport code. What was
withdrawn is the claim that Windows collapses a *clean* close into the reset spellings. Whether it
ever does is **not measured**, and several items below exist because of that.

---

## 1. N2's Windows arm — a Linux run provably cannot catch a breach

**What.** S6R.2's N2 changed the h1 driver to ask `net::isPeerClosedErrorCode( )` instead of
comparing codes by hand. That predicate admits `connection_reset` and `connection_aborted`, spellings
that occur only on Windows.

**Why Linux cannot settle it.** On Linux the two `net::` predicates admit the *identical* set; they
diverge only on a POSIX reset, which never carries the Windows spellings. So a Linux run exercises
the arm without ever discriminating it. The Linux half is fully verified; the Windows half has never
executed.

**Run.** The h1 driver's peer-close cases in `utf_baselib_httpclient7`, on `win-x64` and `win-x86`.
Because the abort case is a race, expect to need **dozens of iterations** — AGENTS.md says so and
this project has paid for it three times.

**Read.** Any case that reds intermittently there. **Suspect this rule first.**

---

## 2. The TLS peer's steps 2 and 3 — the reported code decides, not the verdict

**What.** A 2026-09-21 measurement attributed a `10054` to the platform. Step 1 (`2a4ad5f`) found
that peer was manufacturing the reset itself — it read 1024 bytes of a **1500-byte** ClientHello and
tore down over the remainder. It now reads the whole hello.

**Run.** `TcpPreHandshakeStageTls_RetryableHandshakeErrorTests` and
`..._StageRunsOncePerAttemptTests` at `--log_level=message`.

**Read — and this is the point: both cases pass either way, so the verdict tells you nothing. The
reported code is the answer.**

| code | meaning |
|---|---|
| `eof` or `asio.ssl.stream:1` | the 2026-09-21 row was that peer's own reset, and the Windows `connection_reset` arm rests on **nothing measured** |
| a persisting `10054` | it was measured after all, against a now-orderly peer |

---

## 3. `m_wasSocketShutdownForcefully`'s own red — the second thing Linux cannot reach

**What.** The teardown design's §2.3 argues that flag prevents a failure. On Linux, **dropping the
flag leaves the task clean in all 35 runs that reached the assertion** — because the h1 driver task
never performs the handshake (the establisher does), so `isExpectedException( )` takes its
`! m_isHandshakeCompleted` arm and `isExpectedSocketException( )` already lists `broken_pipe`.

**So the flag's red is reachable only on Windows, or through the h2 driver, which handshakes for
itself.** Recorded at the point of use in `initiate-close-teardown-design.md` §17, with the four
degradation controls that *were* produced on Linux.

**Run.** `Http1DriverTls_WriteInFlightCloseSkipsCloseNotifyTests` in `utf_baselib_httpclient5`, with
the flag's assignment removed, on Windows.

**Read.** A red earns the flag. A green says the flag is doing nothing on that platform either, and
§2.3's argument needs rewriting rather than re-running.

---

## 4. A1-cleartext's Windows ordering — a decision waiting on a measurement

**What.** `net::isPeerResetOnWriteErrorCode( )` treats a write's `connection_reset` as proof the
ending was a reset with no FIN before it. That holds on POSIX by the kernel's own rule —
`tcp_reset( )` writes `ECONNRESET` only from `ESTABLISHED` and `EPIPE` from `CLOSE_WAIT`, and
`tcp_fin( )` sets `SOCK_DONE`. **Winsock has no CLOSE_WAIT→EPIPE rule**, so a write's reset spelling
there cannot prove the same thing.

**The consequence is conservative, not dangerous:** a peer that half-closes and *then* aborts would
have its FIN-framed message reported as a reset rather than completed. That is a complete message
called an error — not a truncation called a success.

**A one-line narrowing exists and was deliberately NOT taken.** The reason is worth carrying: the two
candidates fail in **opposite directions**. The landed code fails conservative; the narrowed code
fails as a **truncation-reported-as-success**, the worst class on this project's list. Narrowing on
an unmeasured premise is exactly how the Windows record came to need superseding.

**Run.** A peer that sends a FIN, then aborts, with a close-delimited body in flight, on Windows.

**Read.** If the write there carries a reset spelling *after* a FIN, take the narrowing. If it does
not, leave it and record that it was measured.

---

## 5. Two new h2 cases are compiled out on Windows

`utf_baselib_h2client6/TestHttp2DriverWritePeerClose.h` is wrapped in
`#if ! defined( _WIN32 )`. The reasoning: the FIN-then-`EPIPE` ending is POSIX kernel behaviour, and
on Windows a send takes codes the read-side predicate already admitted, so the cases would be green
before *and* after and prove nothing.

**That reasoning is plausible and unverified.** Either confirm it — and record that the exclusion is
measured rather than assumed — or build the Windows-shaped equivalent. **An exclusion nobody has
checked is the same shape as a control that does not control**, which this batch hit three times.

---

## 6. A1-tls's `WSAESHUTDOWN` route

`cancelTask( )`'s `shutdown_send` makes a parked write complete `WSAESHUTDOWN` on Windows, and
`net::isPeerClosedOnWriteErrorCode( )` **refuses** it — a second route into the write handler's
`CHK_EC( )` that Linux does not have. It is still behind an external cancel, so face 2's gate covers
it, but nobody has measured that.

**Run.** `utf_baselib_httpclient5`'s two TLS cancel-close cases on Windows, and check whether the
composed-read window has the same shape under IOCP as it does under epoll — the "only two windows"
derivation is for the cleartext policy on POSIX.

---

## 7. The TLS spelling of a *write's* reset

Owed since the teardown design's §13.9 and still owed. The lane measured the TLS spelling of a
write's **cancel** (`operation_aborted`, passed through the engine unchanged, confirming asio's
`map_error_code( )` touches only `eof`). **No arrangement on this host produces a write's reset under
TLS.**

**Measured on Windows 2026-09-25, and closed:** `connection_reset`, `system:10054`, passed through the
engine unchanged, 5 of 5 on `win-x64-vc143-debug` - see `astra-remediation-owed-work.md` W8.

---

## 8. x86 debug object sizes for `utf_baselib_httpclient4`, `5` and `6`

**Unmeasured and unrecorded in `notes/` for all three.** `win-x86-*-debug` is the platform the 40 MB
target is calibrated on and the only one that **fails the build** at the 75 MB ceiling.

Current a64 clang debug: `httpclient4` **49.0 MB**, `httpclient5` **47.4**, `httpclient6` **42.4**.

`httpclient6` is 2.4 MB over the 40 MB target, accepted 2026-09-24 with a recorded reason — no
under-target home exists for a session-level case needing a peer, and a sibling pays the ~21 MB TU
floor again for no change in peak. **The deciding number is the family peak, `httpclient4`, not
`httpclient6`:** if 6 were near 75 on x86, 4 and 5 would already be over.

**Also note** `x86` `release` is governed by peak memory in the *optimizer*, not object size — its
release object is *smaller* than its debug object — so no figure here speaks to it. The only check is
whether that combination builds.

---

## 9. A byte-order mark in any file you add will red the gate

**Added 2026-09-24, and it is the one item here that a Windows *agent* can cause rather than measure.**

`utf_inventory.py`'s `read_lines` opens files as `utf-8`, not `utf-8-sig`. A UTF-8 **BOM** is not
whitespace, so the licence block at the top of a file stops being recognised as a comment — and
**C11 is the only invariant that reads line 1.** Measured: a new `Main.cpp` saved UTF-8-with-BOM
reports `C11 file-scope text ADDED: <BOM>/*`; strip the BOM and it passes.

**This is invisible to every control in the repository, because no file in the tree has a BOM.** It
was found by a reviewer constructing one deliberately.

**FIXED 2026-09-24 at `c35b7e0`** — `read_lines` now opens `utf-8-sig`, so the BOM is stripped and the
licence block is recognised again. **This item is closed**; you no longer need to police your editor.
It is kept because the symptom is baffling on an older checkout, and because it is still the shape to
suspect if a file you merely *saved* reds tier 1.

---

## 10. The gate changed under you on 2026-09-24 — read this before running anything

Everything above was written before a day of work on the verification tooling. Four things are new,
and the first one will look like a broken gate if you do not expect it.

**Tier 3 now refuses to compare across platforms, and Windows is the platform it speaks for.**
`runlog.json` was always a `win-x86-vc143-debug` capture and nothing recorded that; it now carries
`__platform__`, and `utf_runlog.py --compare` **refuses** — exit **3**, rendered by `check_split.sh`
as `SKIP` with the reason named — when the baseline's stamp does not match the tree. On Linux that is
why tier 3 now skips. **On your host it should match and tier 3 should actually run, for the first
time in this project's history on any machine but yours.**

**So the first thing to check is your tree's name.** The stamp is the basename of the `--bld` tree,
and the baseline says exactly `win-x86-vc143-debug`. If your tree is named anything else — a
different toolchain version, a different arch — **tier 3 will refuse and it will not be a defect**.
Nobody has been able to verify your tree's name from here, and it is the one thing that decides
whether tier 3 works for you.

**The unstable list is stamped too, and now carries provenance.** `nondeterministic.json` became an
object: `__platform__`, `unstable` (the ten names a derivation produces), and **`observed`** — names
added from a single sighting rather than from a pair of captures, which a refresh can no longer drop.
There is one, `IO_SimpleConnectAndTransmitDataMessageDispatcherOutgoingTests`, added at `77ef537`
after re-running the binary gave 8199 then 8194. **If you re-derive the list, merge it with what is
there; never let it replace.** A bare array is still read, so an old list is not an error.

**Tier 1 grew from C1–C10 to C1–C13**, and now folds the enclosing `#if` stack into C6's and C11's
identity. Two consequences for you: a helper or a file-scope span that moves under a *different*
preprocessor condition reports `GUARD STACK CHANGED` rather than a loss, and **the condition is
compared as written**, so re-spelling `#if ! defined( X )` as `#ifndef X` reports. That is deliberate.
It also now reads `src/utests/include/`, which no invariant used to.

**Exit code 3 is not reserved, and you will meet all three.** `utf_runlog.py --compare` returns 3 for
a platform refusal; `utf_inventory.py` returns 3 for a PARSE PROBLEM, which `check_split.sh` renders
as a tier-1 **FAIL**; and `build-slot.sh` exits 3 when free disk is below its floor. If you wrap a
tier-3 run in the slot and switch on 3, **a disk stop renders as a platform skip and the gate reads
GREEN.** Nothing does that today. Distinguish them by the printed reason, not the code.

**One thing genuinely unmeasured, and it is yours to settle.** `check_split.sh` extracts the refusal
reason with `sed`, and on a Windows checkout a `\r` would survive into the SKIP line. Cosmetic if it
happens, but nobody has run `check_split.sh` on Windows at all, so the rendering of the three new
states there is unverified in general — not only the carriage return.

## Traps this batch paid for, which apply to any run

- **A running process is not a progressing process.** Check the log's mtime moving, the phase files
  appearing, the binary under execution changing — not that a PID exists.
- **`pgrep -f PATTERN` matches the watcher's own command line**, because `bash -c` carries the whole
  script as its argv. Write `[g]1-gate.sh`, not `g1-gate.sh`.
- **Bound every module invocation with `timeout` and treat `rc=124` as a result.** A hang sends no
  completion notification, which is exactly when a watcher is needed. One hang cost twenty minutes of
  an invisible build slot here.
- **Boost.Test refuses an unrecognised `--` argument outright.** A batch that looked 5-of-5 red was
  an argument error. Driver logging needs `-- --bl-logging-level=6`, and the `--` is not optional.
- **A control adjacent to the risk is not a control.** Three times in this batch an instrument
  measured something one step to the side of what it claimed: a seam subject to the race it measured,
  a relocation control that moved a helper rather than a file, a probe whose cut window started
  inside a namespace.

---

## What to do with the results

Each item above names what its answers mean. Record them where the reasoning lives — the file each
item cites — and update `astra-remediation-owed-work.md`'s Windows table, which is the single place a
reader is told to look for what is left.

**If a run reds something not on this list**, that is more valuable than anything on it. Everything
here is a known unknown.

---

## Astra's second review, 2026-09-27 — what CS-1 to CS-3 owe Windows

**Added 2026-09-27.** The decisions and the change-sets are in `astra-second-review-decisions.md`:
§3 for each decision, §9 for those taken during the run, and §10 for how each change-set landed.
**Run only from a tip the maintainer has pushed** that contains the merge commits named here, and
record the tip in the result.

**What Linux established:**
- **CS-3**, merged at `f48acd2`: gated green, 24 of 24, clang release and gcc debug.
- **CS-1**, merged at `a04f29c`: gated green, 18 of 18, on the same two combinations.
- **CS-2**, merged at `bc431f2`: gated green, 18 of 18, on the same two combinations.
- Every red was deterministic, and tier 1 passes after each re-capture.

**What Linux cannot settle:**

- **A1. D1 — how a TLS ending is classified.** Run `utf_baselib_httpclient8`, four cases. The red
  case's peer ends its transport with no close_notify, and the case asserts that the request fails.
  - The control asserts that a close_notify completes the body.
  - Three cases also assert the ending the peer's script records, `client-ended:asio.misc:2`: it saw
    our end of stream.
  - AGENTS.md's networking rule applies. On Windows a peer's close can arrive as `connection_reset` or
    `connection_aborted`. So if the recorded ending is spelled differently while the request's verdict
    holds, that is a spelling to record, not a D1 regression.
  - If the verdict itself differs — a truncated body reported complete — that is D1 failing on
    Windows. Report it at once.
- **A2. The A1-tls gates, re-pinned** (`TestHttp1DriverTlsCancelClose.h` in `…5`). The three cases'
  peer now seals a close_notify and answers our FIN with it, so the ending is clean and only the
  gates stop a cut-short body completing.
  - Run `…5` whole.
  - A red here is the peer-close divergence meeting the re-pin's exchange. Read the peer's recorded
    ending before concluding anything.
- **A3. D2 — the first I/O on every HTTP/1.1 connection.**
  - The first read now starts inside `onStartConnection( )`, an accounted strand handler, and a
    request's first write can no longer overlap it.
  - A cancel before the start now ends the connection with our own orderly close — over TLS, a
    close_notify exchange.
  - Run `utf_baselib_httpclient11` and `…12`, D2's cases, then `…7` (A4's route) and `…3`, which
    start drivers throughout. **Each case is deterministic on Linux.** A Windows red is therefore a
    platform difference in the start's ordering or in the close exchange, and worth reporting as found.
- **A4. x86 debug sizes of the new modules.** The 75 MB ceiling is enforced on `win-x86-*-debug`.
  Record each size; a module over the 40 MB target carries its reason in its `Main.cpp` already.
  Estimates, from a64 clang debug and `…7`'s ratio:

  | Module | a64 clang debug | x86 estimate |
  |---|---|---|
  | `utf_baselib_httpclient8` | 36.8 MB | about 41 MB |
  | `utf_baselib_httpclient11` | 32.4 MB | not estimated |
  | `utf_baselib_httpclient12` | 35.6 MB | about 39.7 MB |
  | `utf_baselib_h2client8` | 35.4 MB | about 39 MB |

  The a64 figures here are MB of 10^6 bytes, not the 2^20 which `utf_objsize.py`, the size gate and
  `src/utests/AGENTS.md` use, in which each reads about 4.6% lower: `…8`'s 36,844,520 bytes are
  35.1 MB. The estimates are unaffected: each scales by `…7`'s ratio, whose a64 figure, 34.1, is
  10^6 too, so that unit cancels.

  `…5` grew 9,088 bytes with the re-pin, and was already over the target.
- **A5. CS-3's `utf_baselib_h2client8`.** Run it whole, five cases. Nothing in it is expected to
  differ on Windows: D6 and D7 are boundary inputs, and the two DATA-block cases run a driver against
  a scripted peer. Run it because it is new.

- **A6. CS-2, merged at `bc431f2`.**
  - **Run `utf_baselib_httpclient9` whole.**
    - E2 asserts `establishmentTimeouts == 2` on Linux only. Its other assertions on Windows are
      derived, not run. On Linux the full-queue drop it relies on assumes `tcp_abort_on_overflow = 0`,
      and Winsock resets a full queue instead, which is why the count is Linux-only. On Windows the
      case must still fail with the establishment's chained cause.
    - D3's stalled-sink case rests on two premises: socket buffers far smaller than 64 MiB, and the
      client's socket closing with unread data, so the peer's blocked write is released by a RST. On
      Linux the peer wrote 84 of its 1,024 chunks. Record what Windows does. A green with a different
      count is a result; a hang in the peer's write is a finding.
  - **Run `…10` whole.**
  - **Record both modules' x86 debug sizes:** 39.8 and 46.6 MB at a64 clang debug. Both carry their
    reason already. `…10`'s x86 size is the one most likely to be large.

---

## Astra's second review, continued, 2026-09-28 — what CS-4 to CS-6 owe Windows

**Added 2026-09-28.** The decisions are in `astra-second-review-decisions.md` §11, and how each
change-set landed is in §10. CS-6's two design notes say what Windows must measure:
`astra2-cs6-tls-shutdown-after-truncation-design.md` and `astra2-cs6-lost-forced-cancel-design.md`,
whose §6 has the list. **Run only from a tip the maintainer has pushed** that contains `f2baa2f`, and
record the tip in the result.

**What Linux established:**
- **CS-5** is merged at `31e6365`, **CS-4** at `988544a` and **CS-6** at `f2baa2f`.
- **One whole-suite gate covers all three: green on `0f6f05d`.** It ran all 58 modules in clang
  release and gcc debug. The two exceptions are explained in the decision record §10:
  - `utf_baselib_jni`'s pre-existing teardown exit, after all 11 of its cases passed;
  - `utf_baselib_plugin`, which is a library, not an executable.
- **CS-6 changed transport teardown**, so `AGENTS.md`'s networking rule applies to every item below.

**What Linux cannot settle:**

- **B1. I13, the forced cancel's `SO_LINGER` write, now deleted.** Run `utf_baselib_tasks3`'s
  `Tcp_ForcedCancel*` cases. Windows has no ThreadSanitizer, so these cases stand in for I13's TSan
  evidence.
- **B2. I2 on IOCP: a truncated TLS stream's shutdown does not wait for the close_notify.**
  - **The cases:**
    - `tasks3`'s `TlsShutdown_ATruncationDoesNotWaitForTheCloseNotifyTests`;
    - `utf_baselib_httpclient13`'s `Http1DriverTls_*` and both `SimpleHttpTls_*`;
    - `utf_baselib_h2client9`'s `Http2DriverTls_*`.
  - **The hang is epoll-specific (INFERRED)**, so on IOCP a case may already be green before the fix
    (`a5d9d9a`). After it, every one must be green.
- **B3. `WSARecv` after the peer's FIN and then a reset: does the read get the reset?** On Linux it
  gets end of stream, and the error is left pending. If it gets the reset,
  `TlsShutdown_ATruncationThenACloseEndsCleanTests` is the red on Windows.
- **B4. D-L3-1's gap on IOCP.** `CancelIoEx` reaps only outstanding I/O (INFERRED). Measure `tasks3`'s
  reds — W1 stranded, W1 plain, W2 stranded — before and after the fix.
- **B5. (c)'s premise: `shutdown( SD_RECEIVE )`.**
  - Does a `WSARecv` started after it complete at once, and with which code? `WSAESHUTDOWN` is the
    documented one, NOT VERIFIED.
  - Does one already outstanding complete?
- **B6. (c)'s effect on the peer.**
  - A peer which sends after our receive shutdown is reset. On Linux that holds once our FIN is out,
    and Windows is expected to reset it always.
  - Record which code the cancelled handshake fails with: end of stream, `WSAESHUTDOWN` or
    `WSAECONNRESET`. Only end of stream reaches the retry path, so the retry guard's red there must
    come from the orderly-close peer.
- **B7. The four (a2) reds and the HTTP/2 application-phase characterization,** in `tasks3`,
  `utf_baselib_h2client10` and `utf_baselib_http3`.
  - Their logic is platform-independent, and their transport codes are not.
  - Check that `http3`'s response-timer case's 1 MB write stays blocked on Windows, as it does on
    Linux.
- **B8. D2, the SimpleHttpTask handshake timer:** `http3`'s red and its two controls. Expected
  platform-independent.
- **B9. D3, the per-endpoint connect loop, on IOCP (`ConnectEx`).**
  - Run the non-TSan red in `utf_baselib_tasks4`: a cancel during a multi-address connect ends at
    once.
  - Run the loop's ordinary cases: endpoints tried in order, the last error when none connects, and
    the proxy tunnel's connect through the same helper.
  - The socket closed and re-opened for each endpoint must re-associate with the IOCP. asio's open
    does that; verify it.
- **B10. Does a full accept queue drop a SYN on Windows, or reset it?** D3's prompt-cancel red
  depends on it. If Windows resets, that red is Linux-only, and the case should say so.
- **B11. D-A's `TcpConnectOpenFailure_*` cases in `tasks4` skip on Windows.** Nothing can make the
  open fail there without a seam. Record the skip, and nothing more.
- **B12. I6 off Linux.** `utf_baselib_h2client`'s `H2Connect_SilentProxyHitsTheConnectDeadlineTests`
  is the first MSVC check of CS-4's premise: editing the exception in place reaches the recorded
  failure, because a rethrown copy shares boost::exception's error-info container.
- **B13. Run the six new modules whole:** `tasks3`, `tasks4`, `http3`, `httpclient13`, `h2client9` and
  `h2client10`.
- **B14. x86 debug sizes.** The 75 MB ceiling is enforced on `win-x86-*-debug`. Every new module
  records its size and reason in its `Main.cpp` (D-B).

  | Module | a64 clang debug | x86 estimate |
  |---|---|---|
  | `utf_baselib_tasks3` | 36.9 MB | about 41–43 MB |
  | `utf_baselib_tasks4` | 31.9 MB | about 35–37 MB |
  | `utf_baselib_http3` | 36.1 MB | about 40–42 MB |
  | `utf_baselib_httpclient13` | 36.9 MB | about 41–43 MB |
  | `utf_baselib_h2client9` | 37.5 MB | about 42–44 MB |
  | `utf_baselib_h2client10` | 37.6 MB | about 42–44 MB |
  | `utf_baselib_httpclient9` | 40.1 MB, over the target since CS-4 | not estimated |
  | `utf_baselib_httpclient5`, `…10` | as CS-5 left them | not estimated |

  The a64 figures here are MB of 10^6 bytes, except `…9`'s, which is already 2^20 (42,023,512
  bytes) - the unit `utf_objsize.py`, the size gate and `src/utests/AGENTS.md` use. In it the others
  read about 4.6% lower: `…13` is 36,931,648 bytes at `d13859c`, 35.2 MB. The estimates scale by
  1.11 to 1.17, and only the upper ratio is measured in one unit: row 5c of
  `astra-remediation-owed-work.md` measures 1.13 to 1.17, while 1.11 is `…7`'s x86 figure over its
  10^6 a64 one. In one unit, 1.13 to 1.17 puts `…13` at about 40 to 41 MB.

  *(2026-09-29, CS-9: each of these modules' `Main.cpp` now records its size in 2^20 bytes, with row
  5c's ratio. `tasks3` 35.2 MB, about 40–41 MB on x86; `tasks4` 30.4 MB at `8866065`, about 34–36;
  `http3` 34.4, about 39–40; `httpclient13` 35.2, about 40–41; `h2client9` 35.7, about 40–42;
  `h2client10` 35.9, about 41–42. Bytes and sources: `logs/astra4/cs9/sizes-units.txt`.)*
  *(Measured on Windows the same day, `win-x86-vc143-debug` and `ccl16`:
  - `tasks3` 44.0 and 43.8 MB;
  - `httpclient13` 42.8 and 42.9;
  - `http3` 41.8 and 42.2;
  - `h2client9` 41.3 and 41.4;
  - `h2client10` 41.4 and 41.6.

  None is above 45 MB, so D-B stands. The ratio ran 1.15 to 1.25, above row 5c's. The `Main.cpp`
  notes now carry the measured figures.)*

  **If any module measures above about 45 MB,** the maintainer's D-B reverses for the splittable
  ones: `tasks3`, `httpclient13` and `http3`. Report it.

---

## Astra's third review, 2026-09-28 — what CS-7 owes Windows

**Run only from a tip the maintainer has pushed** that contains `605b7d9`, CS-7's merge, and record the
tip in the result. The decisions are in `astra-third-review-decisions.md`.

**What Linux established:**
- CS-7 is merged at `605b7d9`, and its tier-1 refresh is `083898d`.
- It is gated green on `083898d`: clang release and gcc debug over the seven modules whose objects
  include `HttpClientRequestTask.h` (`utf_baselib_httpclient`, `…4`, `…5`, `…6`, `…8`, `…9` and
  `…10`), 14 of 14.
- Its reds were deterministic, and its ThreadSanitizer pair was red in 50 of 50 runs and green after.
- CS-8, merged at `28f7026`, is text only and owes Windows nothing.

- **C1. T01, the status pair frozen at completion:** run `utf_baselib_httpclient`'s
  `HttpClientRequestTask_ACloseAfterTheFailureChangesNoStatusTests` and
  `HttpClientRequestTask_ACloseInTheFailuresBatchStillSetsTheStatusTests` once. Both are
  deterministic, and they are all of CS-7 that Windows can run: its ThreadSanitizer pair is
  Linux-only. CS-7 changes no transport error handling.
- **C2. `utf_baselib_httpclient`'s x86 debug size.** Record it. CS-7 added 95,056 bytes at a64 clang
  debug, to 36,786,320 - 35.1 MB as `utf_objsize.py` counts - which is about 39.6 to 41 MB on x86 by
  the ratio row 5c measured: inferred, not measured. Its reason is in its `Main.cpp`.

---

## Astra's fourth review, 2026-09-29 — what CS-9 owes Windows

**Run only from a tip the maintainer has pushed** that contains CS-9's merge, and record the tip in the
result. CS-9's merge is `5db9ae8`, and its Linux gate is green on `bb8d320`. The decision is D1 of
`astra-fourth-review-decisions.md`, shape (a″), and the mechanism is in
`astra4-cs9-negotiated-publication-design.md`, agreed.

**What CS-9 changed:** the HTTP client's connection publishes its negotiated protocol itself.
`ClientConnectionTaskBaseT::continueAfterConnected( )` writes the value once, then sets an atomic flag,
and both getters - the base's and the HTTP/2 driver's - read the flag first: `Unknown` with no
identifier until it is set, the settled value from then on. **It changes no transport error handling.**

**What Linux established** (lane 1, clang debug; the orchestrator's gate added clang release and gcc
debug over the 22 affected modules, green 44 of 44 on `bb8d320`):
- The getter case's red was deterministic, and it is green after the fix.
- The race it closes was red under ThreadSanitizer in 102 of 102 runs of the committed reader, and in
  none of 50 after the fix, with the positive control reporting in both trees.
- The three request cases were green on both sides of the fix, and `utf_baselib_h2client11` ran 50
  times clean.
- The driver and contract modules the lane may build - `utf_baselib_h2client`, `…2`, `…3` and
  `utf_baselib_httpclient` - are green at clang debug: 12, 14, 3 and 78 cases.
- `ClientConnection.h`'s comment edit is preprocess-identical, so it changes no object on any platform.

**What Linux cannot settle:**

- **D1. Run `utf_baselib_h2client11` whole.** Its five cases, and what each means on Windows:
  - `NegotiatedPublication_AValueWrittenButNotPublishedIsNotReadTests` - the contract. One thread, a
    pure input, no I/O: it is expected green anywhere. It is the one case which separates the decided
    shape from the withdrawn (a′), since it reads the published value from a driver still `Connecting`.
  - `NegotiatedPublication_ACancelDuringAHeldHandshakeReportsUnknownTests` - U01's own route. Its
    handshake is held by a listener which never accepts, as `utf_baselib_h2client10`'s connect-deadline
    reds hold one (B7), so it rests on two things this host has not measured:
    - the connect completing from the listener's backlog before any accept, as it does on Linux - the
      same arrangement as those reds;
    - the driver's cancel ending a held TLS handshake on IOCP, which is B4 to B6.

    It asserts the request's own cancel and its `Unknown` value, which are platform-independent. Its
    slot comes back only from the driver's terminal, so if the driver's cancel does not end the held
    handshake, neither the release nor the stop arrives: the case waits 30 s for each and then waits
    without a bound for the driver task, which only the driver's own 60 s connect deadline, re-issuing
    the cancel (CS-6's (a2)), can end. It then fails first on "the request did not give its one slot
    back exactly once", with "released no" and "driver stopped no" in its description - read B4
    first. If it hangs instead, the re-issued cancel did not end the handshake either. Every case here
    which cancels its driver ends in such a wait, so a hang in any of them points the same way: B4
    before the handshake, B7 after it.
  - `NegotiatedPublication_ACancelAfterTheHandshakeReportsH2Tests` - rests on the driver's
    application-phase cancel, which B7 characterizes in `utf_baselib_h2client10`.
  - `NegotiatedPublication_AFallbackToHttp11ReportsHttp11Tests` - the http/1.1 fallback, with a stub
    driver which keeps the stream. Expected platform-independent.
  - `NegotiatedPublication_AReaderOffTheStrandSeesTheSettledValueTests` - on Windows only a functional
    check. It passes on both sides of the fix by design; its verdict is ThreadSanitizer's, which this
    host lacks.
- **D2. `utf_baselib_h2client11`'s x86 debug size.** Record it. It is 39,802,696 bytes at a64 clang
  debug at CS-9's merge, and 39,802,744 after CS-10's (`f0890bd`) - 38.0 MB either way, as
  `utf_objsize.py` counts - which is about 43 to 44 MB on x86 by the ratio row 5c
  measured, 1.13 to 1.17: inferred, not measured. That is over the 40 MB target and under the 75 MB
  ceiling, and its reason is in its `Main.cpp`: the HTTP/2 driver over the stranded TLS policy, which
  every case instantiates.
- **D3. Whether it builds at x86 `ccl16` release.** Object size does not govern that combination, so
  the size above says nothing about it (`src/utests/AGENTS.md`, "The x86 release caveat"). The only
  check is the build.

## L7-D, 2026-09-30 — what the HTTP/2 fingerprint owes Windows

**Run only from a tip the maintainer has pushed** that contains L7-D's merge, and record the tip in
the result. L7-D's merge is `5fcb700`, and its tier-1 refresh is `e514fe2`.

**What L7-D changed:**
- It added `http2/Fingerprint.h`, which is pure computation over bytes, with no socket and no
  OpenSSL.
- It added a new module for its ten cases, `utf_baselib_h2core2`.
- It edited comments in `http2/FrameCodec.h` and `http2/Session.h`, line for line.

**What Linux established:**
- The gate is green: `utf_baselib_h2core2` at clang release and gcc debug, 10 cases each, and 50 runs
  clean at clang debug.
- The comment edits change no object. `utf_baselib_h2core`, rebuilt after the merge, is byte-identical
  at gcc debug and at clang release. So no other module needs a Windows run for them.

**What Linux cannot settle:**
- **E1. Run `utf_baselib_h2core2` whole.** Its cases are pure inputs, so they are expected green on
  every flavor.
- **E2. Its x86 debug size.** Record it. It is 24,009,840 bytes (22.9 MB) at a64 clang debug. That is
  about 26.3 to 28.6 MB on x86 by the measured 1.15 to 1.25 ratio: inferred, not measured.
- **E3. Whether it builds at x86 `ccl16` release.** As D3 above, only the build can tell.

## L7-C, 2026-09-30 — what the browser profile loader owes Windows

**Run only from a tip the maintainer has pushed** that contains L7-C's merge, and record the tip in
the result. L7-C's merge is `05b6ab6`, and its tier-1 refresh is `79d3d3d`.

**What L7-C changed:** three new headers:
- `crypto/TlsNameRules.h`;
- `httpclient/BrowserProfiles.h`;
- `httpclient/BrowserProfile.h`.

Only `utf_baselib_h2profiles` includes them, and it gained 16 cases. No existing header includes them
yet. L7-B's part 2 will make `crypto/CryptoBase.h` include the first.

**What Linux established:**
- The gate is green: `utf_baselib_h2profiles` at clang release and gcc debug.
- 50 runs are clean at clang debug, and 10 of 10 negative controls are caught.

**What Linux cannot settle:**
- **F1. Run `utf_baselib_h2profiles` whole.** MSVC and clang-cl compile the new headers here for the
  first time.
  - `TlsNameRules_AgreesWithTheLinkedOpenSslTests` compares the rule with the linked OpenSSL's own
    classification of every suite it knows. Against the same 3.5.4 it is expected green.
  - A failure there means the Windows dist's OpenSSL is configured differently. Report which suites
    differ.
- **F2. Its x86 debug size.** Record it. It is 28,142,808 bytes (26.839 MB) at a64 clang debug. That is
  about 30.9 to 33.5 MB on x86 by the measured 1.15 to 1.25 ratio: inferred, not measured. Either way
  it is under the 40 MB target.
- **F3. Whether it builds at x86 `ccl16` release.** As D3 above, only the build can tell.
