# What the Windows matrix owes this work — handoff

**Date:** 2026-09-24. **For:** whoever runs the Windows matrix. **Status:** the complete list at the
end of the astra remediation and the batch that followed it.

Everything here was found on Linux and **cannot be settled there**. Two of the items are things a
Linux run *provably* cannot reach — not "has not yet", but cannot. The rest are measurements this
host has no way to take.

Each item says **what to run**, **what to read**, and **what each possible answer means**, so that a
result is a decision rather than a data point. Where an item can only be answered one way, that is
said too.

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
