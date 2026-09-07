# Handoff: the Windows residuals of the whole-library C++ review

**For:** a fresh Claude Code session (Opus 5) on a devenv7 **Windows** host, repository
`swblocks-baselib`, branch `lazari2`.
**Written:** 2026-09-07, from the Linux session that implemented stages 3-11 of the review.
**Goal:** apply and validate the five Windows-only items (W-1..W-4) and audit the four Windows
halves that were written blind on Linux, with proof, without committing.

Read these first, in this order, before touching any source:

1. `notes/plans/issues/windows-only-residual-findings-deferral.md` — the deferral record. Items
   10-14 (section "Whole-library C++ review, 2026-09-06/07") are your work list; items 1-9 are the
   previous Windows round and show the shape of the outcome table you are expected to fill in.
2. `notes/reviews/major/update_2026/whole-library-cxx-review-fable51.md` — the review, section
   "W. Windows-only (static review; verify on a Windows host per the recorded pattern)". Each
   W item gives the exact lines, the failure mechanism and the fix. Implement from that text; do
   not re-review `OSImplWindows.h`.
3. `notes/reviews/major/update_2026/whole-library-cxx-review-fable51-decisions.md` — the decision
   extract. Its "Decided" entry format is the one to reproduce for each item you finish.
4. `AGENTS.md` at the repository root (loaded automatically) and `scripts/devenv7/AGENTS.md` for
   the Windows build system, the environment setup scripts and the JNI notes.

Note that `notes/reviews/` is **untracked** in this repository by deliberate decision, so it may be
absent from a fresh Windows checkout; if it is, ask the user to copy the four review documents
across before starting. The two instruction files the earlier sections of the deferral record cite
(`windows-only-residual-findings-instructions.md` and the O-3/O-4 companion) are gone for the same
reason — the deferral record itself carries everything they contained.

---

## 1. What is already done (do not redo, do not re-review)

| Item | State | Where recorded |
|---|---|---|
| Items 1-8 of the deferral record (M-19, L-33, L-34, L-16, L-17, L-20, I-17, I-8) | applied and validated on Windows, 2026-09-04 | deferral record, "Outcome (2026-09-04)" |
| Item 9 (the two surviving `L-16` byte-widening sites) | **open**; pick it up with item 11, it is the same idiom in the same file | deferral record, "Residual found by the closure audit (2026-09-05)" |
| O-3 / O-4 Windows verification | done, 2026-09-05, commits `e02eb07`, `99804db` | deferral record, "O-3/O-4 Windows verification (2026-09-05)" |
| Stages 3-10 of the whole-library review (A-*, N-*, M-*, R-*, T-*, O-*, S-*) | implemented and tested on Linux, uncommitted at the time of writing | decisions file, "Decided" entries per stage |

The UNIX halves of everything in stages 3-10 are tested on Linux. Do **not** re-implement them and
do not "port" them: the only cross-platform code you touch is listed in item 14.

## 2. Session rules (binding)

- **This document is the explicit instruction to implement.** `AGENTS.md` defaults to research over
  implementation; the user has approved this work list. Anything not on it is still "research and
  ask", not "do".
- **Edit tool only for existing files.** Every hunk must be intentional and explainable in a diff
  review. The Write tool creates new files only.
- **Never commit.** No `git add`, `git commit`, `git push`, no stash. The user commits between
  items. Leave the tree uncommitted when you stop.
- **Logic and style never mix.** A comment-only or documentation-only edit goes in its own hunk.
  Do not reformat, reorder includes or rename while fixing.
- **C++11 only**, and follow the file's idioms: `BL_CHK_T` with `createException( "<API>" )`,
  `handle_ref::attach`, `BL_NOEXCEPT_BEGIN/END`, `SAA_in`/`SAA_inout` annotations, the
  column-aligned member layout, block comments without trailing periods on single lines.
- **Line endings are LF.** Every source and settings file in this repository is LF; do not let an
  editor or a `>` redirect introduce CRLF. Check with `git diff --check` before you stop.
- **Batch files:** if you touch any `.bat`, the escaping and delayed-expansion rules in `AGENTS.md`
  ("Windows Batch File Rules") are mandatory — `^` escaping for `(`, `)`, `<`, `>`, `|`, `&`,
  `!VAR!` for anything set inside an `if`/`for` block, and never `^` line continuation inside a
  quoted `set`. None of the items below is expected to need one.
- **Builds:** focused builds of the affected modules only; `-j1` whenever more than one module is
  built; never a full parallel build. Both toolchains (`TOOLCHAIN=vc143` and `TOOLCHAIN=ccl16`) and
  both variants (`VARIANT=debug`, `VARIANT=release`). `-WX` is in force and external headers are
  passed as system headers since item 1, so any warning you see is yours.
- **Architectures:** `ARCH=a64` (native on the recorded host), plus `ARCH=x64` under emulation for
  the process items (10, 11, 13c) — the argv and handle-inheritance rules are architecture
  independent, but the emulated run has caught layout mistakes before.
- **Out of bounds:** no edits to `scripts/devenv7/**` (record defects in
  `scripts/devenv7/docs/supply-chain-verification-deferral.md`); no edits to the UNIX
  implementation (`OSImplUNIX.h`) or to any Linux-tested logic.
- **Never `make clean`** (it does not work here; `rd /s /q .\bld`), never a `taskkill` by image
  name that could hit the user's own processes.
- **When in doubt, ask.** Use `AskUserQuestion` where two readings lead to materially different
  code — in particular for item 14's DACL question, which is a product decision.

## 3. Order

`11 → 10 → 12 → 13 → 14`.

- Item 11 (and item 9 with it) is the smallest and lands the `utf8ToUtf16` idiom at the remaining
  sites; do it first so item 10's test scaffold can use non-ASCII arguments freely.
- Item 10 is the largest single rewrite and its test scaffold is reused by item 13c.
- Item 13's four sub-items are independent; (c) needs item 10's scaffold.
- Item 14 is an audit plus one open decision, and it is easiest to answer once the process items
  have exercised the file.

## 4. Per item: location, fix, verification

### Item 11 — W-2, the byte-widened command line (plus item 9)

- **Location.** `src/include/baselib/core/detail/OSImplWindows.h:1443-1444` (the
  `createProcess( commandLine )` overload). Item 9's two sites: `:2806` (`RobustNamedMutex`, the
  name handed to `::CreateMutexW`) and `:3396` (`::GetNamedSecurityInfoW`).
- **Fix.** `utf8ToUtf16( ... )` — the helper added by item 4, at `:640` — at all three sites.
  `CreateProcessW` may write to the command-line buffer, so copy the result into a writable
  NUL-terminated `std::vector< wchar_t >` rather than passing `c_str()` of a `const std::wstring`.
  At `:3396` pass `pathPreferred.native()` straight through instead of narrowing to `string()` and
  widening back.
- **Verification.** A child launched through the command-line overload with a non-ASCII argument
  prints it back byte-identical; `BaseLib_OSRegistryValueTest` (extended by item 4 with non-ASCII
  names) stays green; a new case constructs two `RobustNamedMutex` instances in two processes on a
  non-ASCII name and shows they exclude each other (before the fix they are different kernel
  objects and both acquire).

### Item 10 — W-1, `createProcess( vector )` argv quoting

- **Location.** `OSImplWindows.h:1636-1656`.
- **Fix.** The documented CRT algorithm, not the current heuristic:
  quote the argument when it is empty or contains a space, tab, `"` or newline; walk it counting
  consecutive backslashes, emit `2n+1` backslashes before a literal `"`, `2n` before the closing
  quote, and `n` otherwise.
- **Verification.** The round-trip scaffold is at `src/utests/utf_baselib/TestBaselibDefault.h:1577`
  — a child that prints its own `argv`, one entry per line. Required cases, each of which fails on
  the current code: an **empty** argument (today it vanishes and every later index shifts); an
  argument containing a **tab** (today it is split); an argument containing `"` but **no space**
  (today it merges with its neighbour); `C:\dir x\f` (today it arrives as `C:\\dir x\\f`); an
  argument ending in a **backslash** inside a quoted argument. Show each failing before the fix.

### Item 12 — W-3, `tryGetRegistryValue`

- **Location.** `OSImplWindows.h:3530-3565`; the deleter at `:227-242`.
- **Fix.** Initialise the `HKEY` to `NULL`; attach it to the RAII holder only after
  `RegOpenKeyExW` returned `ERROR_SUCCESS`; build the location text from the `currentUser`
  parameter (`currentUser ? "HKEY_CURRENT_USER" : "HKEY_LOCAL_MACHINE"`) instead of the constant.
- **Verification.** `BaseLib_OSRegistryValueTest` extended with a lookup of a key that does not
  exist — it must return "not found" rather than abort — and one against `HKEY_LOCAL_MACHINE`
  whose exception message names that hive. The pre-fix behaviour is UB, so demonstrate it under a
  debugger or with the holder's deleter instrumented rather than relying on a crash.

### Item 13 — W-4, four independent sub-items

- **(a) `pi.hThread` leak, `:1555-1585`.** Attach the thread handle to a holder unconditionally,
  not only on the `assignNewJob` path. Verify with `GetProcessHandleCount` on the parent, stable
  across 100 spawns of each kind (in a job, already in a job, detached).
- **(b) `LSA_UNICODE_STRING`, `:2047-2053`.** `std::wstring( Buffer, Length / sizeof( WCHAR ) )`
  with a NULL check on `Buffer`. Covered by the existing user/SID paths.
- **(c) handle inheritance race, `:1687-1708`, `:898-939`, `:1536-1548`.** Add the `N` flag to the
  `_wfopen` mode strings so the returned handles are not inheritable, and pass an explicit
  `PROC_THREAD_ATTRIBUTE_HANDLE_LIST` — compatible with the `STARTUPINFOEXW` the function already
  builds — listing only this child's three standard handles. **The test is the point of this
  item:** two threads each spawn a redirected child concurrently; each must read only its own
  child's output and must see EOF as soon as *its own* child exits. Before the fix the reader
  blocks until the other child exits as well. Give the case a generous timeout and assert on the
  ordering, not on absolute durations.
- **(d) `createJunction` print name and the dead `STILL_ACTIVE` check, `:2361-2384`, `:2457`,
  `:546-551`.** Validate `to.is_absolute()` and stop trimming the last character unconditionally
  (`back()` on an empty string is UB); compare `ec` rather than the wait result against
  `STILL_ACTIVE`. Verify that a junction created from a plain absolute path reads back with its
  full print name and that a relative target is rejected.

### Item 14 — the four Windows halves written on Linux

These compile only here; nothing about them has ever run.

- **`getPhysicalMemorySize`, `OSImplWindows.h:1687-1701`.** `GlobalMemoryStatusEx` →
  `ullTotalPhys`, `0` on failure. Check the value is non-zero and within a factor of two of what
  Task Manager reports.
- **`getFileDescriptorSoftLimit`, `:1703-1711`.** Returns `0` = "not applicable". Confirm the
  derived connection cap in `HttpServer` then falls back to the RAM term and the 4096 ceiling, and
  that the effective value is logged once at startup. The cap arithmetic is
  `min( 4096, <descriptor term, skipped when 0>, 0.8 * physical RAM / per-connection footprint )`.
- **`createNewFilePrivate`, `:3705-3727`.** `CREATE_NEW` with `dwShareMode = 0`. **Open decision:**
  this gives exclusive *sharing* during creation but not a restrictive DACL — a Windows file
  inherits its directory's ACL, so S-5's property ("the exception dump in the shared temp directory
  is not readable by other local users") is **not** delivered on Windows as written. Either build
  an explicit DACL (owner + SYSTEM + Administrators) through
  `InitializeSecurityDescriptor` / `SetSecurityDescriptorDacl` into `lpSecurityAttributes`, or
  record the weaker Windows guarantee in the S-5 "Decided" entry. Ask the user which; do not decide
  it silently. Either way verify that the function returns `false` for an existing file and `true`
  otherwise, and that `cmdline/EhUtils.h` still writes its dump.
- **`RobustNamedMutex` permissions parameter, `:2841-2869`.** Added for signature parity with the
  UNIX side (O-7, which made the SysV semaphore 0600); the Windows implementation ignores it via
  `BL_UNUSED` and `defaultPermissions()` returns 0. No change expected — confirm it compiles and
  that the mutex tests are green.

## 5. Build and test matrix to run

- `utf_baselib` (items 10-13 and the `RobustNamedMutex` half of 14) and `utf_baselib_http`
  (the N-2 cap of item 14): `vc143` and `ccl16`, debug and release, on `ARCH=a64`; plus `ARCH=x64`
  debug for both toolchains for the process items. All `-j1`, zero warnings under `-WX`.
- Cases: `BaseLib_OSCreateProcess*` (including the extended argv round-trip),
  `BaseLib_OSTryAwaitTerminationTests`, `BaseLib_OSTerminateProcessTree`,
  `BaseLib_OSRegistryValueTest`, `BaseLib_OSSharedLibTests`, the new concurrent-redirect case, and
  the `HttpServer` cap/timeout cases in `utf_baselib_http`.
- After the runs, check with `tasklist` that no `cmd.exe`, `ping.exe` or test child is left behind.
- If a dist carrying `openssl/1.1.1w` is available, the 1.1.1w side of item 8 is still open — but
  read the 2026-09-07 note in the deferral record first: the build currently fails on **any**
  platform for a pre-existing `-Wdeprecated-declarations` reason, so that has to be fixed before
  the item-8 question can even be asked.

## 6. Records to update afterwards

- `notes/plans/issues/windows-only-residual-findings-deferral.md`: an outcome table for items
  10-14 in the shape of "Outcome (2026-09-04)", and the "Conditions to revisit" section amended.
- `notes/reviews/major/update_2026/whole-library-cxx-review-fable51-decisions.md`: a "Decided"
  entry per W item, and "Windows verified on <date>" appended to the S-5 and N-2 entries (item 14).
  If that file is absent from the Windows checkout, hand the text back to the user to apply on
  Linux, the way the 2026-09-05 session did.
- If the S-5 DACL decision goes the "record the weaker guarantee" way, say so in the S-5 entry
  explicitly — it is a property of the shipped product, not a note about the test.

## 7. What to report at the end

The user reads only the final message of your turn; make it self-contained:

- A table with one row per item: what changed (function and member names), the test added, whether
  it was shown failing before the fix (with the failing assertion text), and any deviation from
  the review text with the reason.
- The build and run matrix actually executed (module × toolchain × variant × arch), with every
  failure quoted verbatim. Do not summarise a red run as green.
- Anything found on the way that is out of scope: do not fix it; record it and list it.
- The `git status --short` and `git diff --stat` output, and the statement that nothing was
  committed.
