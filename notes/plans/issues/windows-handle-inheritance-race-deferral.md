# The Windows `createProcess` Handle-Inheritance Race (W-4c): Deferral Record

This document records the decision **not** to fix the handle-inheritance race in the Windows
`createProcess` redirection path, the reasoning behind it, what limits the exposure while it is open,
and the named triggers which should reopen it. **It is a risk acceptance, not an assessment that the
concern is absent: the defect is real, is confirmed, and remains in the shipped product on Windows.**

It also carries the full implementation design, so that whoever picks this up does not have to
re-derive it — and so that this record, rather than a separate plan, is the single place the item
lives.

**Finding:** W-4, third bullet, of
`notes/reviews/major/update_2026/whole-library-cxx-review-fable51.md` ("W. Windows-only"), recorded
as sub-item (c) of item 13 in `notes/plans/issues/windows-only-residual-findings-deferral.md`.

**Sibling items:** W-4 (a), (b) and (d) were all **fixed and verified** on 2026-09-08; see the
"Outcome (2026-09-08)" section of that deferral record. This sub-item is the only part of W-4 left
undone.

**Platform:** Windows only. `OSImplUNIX.h` is not involved and must not be touched.

---

## Decision

**Date:** 2026-09-08
**Status:** **Deferred, closed as a recorded risk acceptance.** Not a to-do item; it should not be
re-reported as a new finding by a future review.

| # | Item | Disposition |
|---|---|---|
| 1 | `_wfopen` creates inheritable CRT descriptors for the redirection pipes (no `N` mode flag) | **Deferred** |
| 2 | `CreateProcessW` is called with `bInheritHandles = TRUE`, which inherits *every* inheritable handle rather than this child's three | **Deferred** — needs a `PROC_THREAD_ATTRIBUTE_HANDLE_LIST`, which needs the spawn path converted to `STARTUPINFOEXW` |
| 3 | A regression test for two threads spawning redirected children concurrently | **Deferred** with items 1 and 2 — it is the only thing which can demonstrate the defect |

**Decided by:** the repository owner, on being shown that the fix is a conversion of the whole spawn
path rather than a localised change, and that nothing in the tree exercises the defect.

---

## The defect

`src/include/baselib/core/detail/OSImplWindows.h`, the `createProcess( commandLine )` overload.

Two facts combine:

1. The redirection pipes are turned into `FILE*` through `_wfopen` (`fopen`), whose mode string
   carries no `N` flag, so the descriptors the CRT creates are **inheritable**.
2. `CreateProcessW` is called with `bInheritHandles = TRUE` whenever anything is redirected, and that
   flag inherits **every** inheritable handle in the process, not the three the caller meant to pass
   in `STARTUPINFOW`.

So when two threads call `createProcess` with redirection concurrently, each child inherits the other
child's pipe write ends. A pipe reports EOF only when the last write handle to it closes, so a reader
blocks until *both* children have exited. The parent's own `outPipe.second.reset()` is not enough,
because the duplicate now lives in the other child.

**Symptom if it ever bites:** a redirected child's output callback does not return when that child
exits, but only when the last concurrently-spawned redirected child exits. It presents as a hang or
as a latency proportional to the longest-running sibling, not as an error.

---

## What limits the exposure while this is open

- **Nothing in the repository spawns two redirected children concurrently.** That is why every
  existing suite is green and why the defect has never been observed. It is a latent hazard for a
  future consumer, not a live failure.
- The non-redirected path is unaffected: `bInheritHandles` is `FALSE` when nothing is redirected.
- Single-threaded sequential use — which is every in-tree caller — is unaffected, because the
  parent's pipe ends are closed before the next spawn.
- UNIX is unaffected: this is a Windows handle-inheritance property with no POSIX counterpart, and the
  O-3/O-4 assessment of the UNIX fd paths was completed and recorded separately on 2026-09-05.

---

## Why it is deferred rather than fixed

**The fix is a rewrite of the spawn path, not a localised change.** Passing
`PROC_THREAD_ATTRIBUTE_HANDLE_LIST` requires `STARTUPINFOEXW`, an allocated and initialised
`PROC_THREAD_ATTRIBUTE_LIST`, `EXTENDED_STARTUPINFO_PRESENT`, and a matching teardown — wrapped
around logic which is already intricate with `JOB_OBJECT` assignment, `CREATE_SUSPENDED` and
`CREATE_BREAKAWAY_FROM_JOB`.

**Correction to the original instruction, which understated this.**
`notes/plans/issues/whole-library-windows-residuals-instructions.md`, item 13(c), says to pass the
attribute list as something "compatible with the `STARTUPINFOEXW` the function already builds".
**The function does not build a `STARTUPINFOEXW`.** It declares a plain `STARTUPINFOW` and passes
`&si`; there is no `STARTUPINFOEX`, no `InitializeProcThreadAttributeList` and no
`EXTENDED_STARTUPINFO_PRESENT` anywhere in `OSImplWindows.h` (verified 2026-09-08 by grep over the
whole file). The review text at `whole-library-cxx-review-fable51.md:581` repeats the same claim
("compatible with the existing `STARTUPINFOEXW`"). That error is the reason the item was originally
sized as a 4-hour job alongside (a), (b) and (d).

**Every spawn in the library goes through this function**, so the blast radius of getting the
attribute list wrong is wide, and one of its two failure modes is quiet:

- a malformed list makes `CreateProcessW` fail outright (loud); or
- a child silently starts without a standard handle it should have had (quiet).

**There is no consumer asking for it.** Fixing a latent hazard with a medium-risk rewrite of the most
heavily used function in the OS layer, with no caller that needs it, is the wrong trade today.

---

## The fix, when it is picked up

Two independent halves. Either alone narrows the race; both together close it. Land them as two
separate changes so a regression bisects to one of them.

### Half 1 — stop creating inheritable CRT descriptors

`fopen` builds its `_wfopen` mode by widening the caller's `mode` string. Add the CRT's `N` flag
("no-inherit") for the redirection path. Two ways, and the choice matters:

- give `convert2StdioFile` / the redirection call sites a mode that carries `N`, leaving the
  general-purpose `os::fopen( path, mode )` contract alone; or
- append `N` inside `fopen` for every caller.

**Prefer the first.** `os::fopen` is cross-platform API whose UNIX twin has no equivalent flag, and a
file a caller opens deliberately before a spawn is the caller's business. Check every
`convert2StdioFile` call site and the `createPipe` / `convert2StdioFile` pair.

Note that the pipe **handles** themselves come from `CreatePipe` with an explicit
`SECURITY_ATTRIBUTES` whose `bInheritHandle` is set deliberately for the end which must reach the
child; that is correct and must stay. It is the *other* end — the parent's end, wrapped into a
`FILE*` — that must not be inheritable.

### Half 2 — pass an explicit handle list

Convert the spawn to `STARTUPINFOEXW`:

- declare `STARTUPINFOEXW siex`, `ZeroMemory`, `siex.StartupInfo.cb = sizeof( siex )`, and keep
  filling `siex.StartupInfo` where the code fills `si` today;
- size the attribute list with `InitializeProcThreadAttributeList( NULL, 1, 0, &size )`, allocate it
  (`std::vector< BYTE >` or the file's `cpp::SafeUniquePtr< BYTE[] >` idiom, as `createJunction`
  uses), initialise it, and `UpdateProcThreadAttribute` with `PROC_THREAD_ATTRIBUTE_HANDLE_LIST` over
  an array of exactly the handles this child needs;
- `BL_SCOPE_EXIT( ::DeleteProcThreadAttributeList( ... ); )`;
- add `EXTENDED_STARTUPINFO_PRESENT` to `creationFlags` and pass `&siex.StartupInfo`.

Three constraints that are easy to get wrong:

1. The handle list must contain **only** inheritable handles, and every handle in
   `STARTUPINFO.hStdInput/hStdOutput/hStdError` must appear in it, or `CreateProcessW` fails with
   `ERROR_INVALID_PARAMETER`.
2. The merged-stdout-and-stderr branch puts the **same** handle in two slots. A duplicate entry in the
   list is rejected, so de-duplicate before building the array.
3. The non-redirected path (`stdioRedirected == false`, `bInheritHandles = FALSE`) must keep working
   unchanged — do not build an attribute list for it at all, or a detached child loses the standard
   handles of its own console, which `BaseLib_OSCreateProcessDetachedWindowsTests` asserts it has.

### The test is the point of the item

A new case in `src/utests/utf_baselib/TestBaselibDefault5.h`, next to the other Windows
`createProcess` cases:

- two threads, each calling `createProcess` with `RedirectStdout` on a child whose lifetime the test
  controls — for example `cmd.exe /c "echo mine && ping -n <n> 127.0.0.1 > nul"`, with a **short** `n`
  for thread A and a **long** one for thread B;
- each thread reads its own callback stream to EOF and records when EOF arrived;
- assert that A's reader saw EOF **while B's child was still running**, i.e. before B's reader
  finished. Assert the *ordering*, never absolute durations — the recorded host is a 2-core ARM64 VM
  and the case must also run under x64 emulation.

Before the fix, A's reader blocks until B's child exits as well, so the ordering assertion fails and
A's EOF timestamp collapses onto B's. Show that failing first.

Also assert that each reader saw only its **own** child's output — `mine` exactly once — which is the
second half of the property and fails independently of timing.

Give the case a generous timeout and make sure a failure cannot hang the suite; `scripts/debug_harness.py`
has no timeout of its own.

### Verification matrix when it lands

`utf_baselib` on `vc143` and `ccl16`, debug and release, `ARCH=a64`, plus one `ARCH=x64` debug run —
the handle-list layout is the kind of thing the emulated run has caught before. `-WX` is in force and
external headers are system headers, so any warning is ours.

Re-run at minimum: `BaseLib_OSCreateProcess*` (all of them — this touches the path every one uses),
`BaseLib_OSTryAwaitTerminationTests`, `BaseLib_OSTerminateProcessTree`,
`BaseLib_OSValidateProcessRedirectFlagsTests` and the new case. Check with `tasklist` afterwards that
no `cmd.exe` or `ping.exe` child is left behind.

---

## Conditions to revisit

Any one of these should reopen it:

- **A consumer spawns two redirected children concurrently**, or a redirected spawn moves onto a
  thread pool where two can overlap. This is the trigger that turns the hazard into a live defect,
  and it is the one to watch: the symptom is a hang, not an error.
- **The spawn path is reworked for another reason.** If `createProcess` is being changed anyway, Half 2
  costs far less than it does standalone, and the two should land together.
- **A security review takes an interest in handle leakage into child processes.** Inheriting every
  inheritable handle is a confidentiality question as well as a correctness one: a child inherits
  handles it was never meant to see, which matters more if a child is ever less trusted than its
  parent.
- Half 1 on its own becomes attractive — it is small, low-risk and independently useful, and it
  narrows the race even without the attribute list.

---

## Records to update when it lands

- The outcome table of `notes/plans/issues/windows-only-residual-findings-deferral.md`, item 13,
  sub-item (c).
- `notes/reviews/major/update_2026/whole-library-cxx-review-fable51-decisions.md`: the W-4 "Decided"
  entry, which records (a), (b) and (d) as done and (c) as deferred to this record.
- This document: replace the Decision section with the outcome, in the shape the sibling records use.
