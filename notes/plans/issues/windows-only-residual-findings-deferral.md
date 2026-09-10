# Windows-Only Residual Findings: Deferral Record

This document records the review findings whose fix can only be validated on a Windows host and
which are therefore **not** applied from the Linux checkout the review was worked from. It is a
risk acceptance with a named trigger, not an assessment that the findings are wrong.

**Findings:** M-19, L-33, L-34, L-16, and the Windows halves of L-17, L-20 and I-17 in
`notes/reviews/major/update_2025/v1/pr_review_analysis_fable51.md`; assessed, with every other
residual of that review, in `notes/plans/issues/pr-review-fable51-residual-findings-status.md`.

---

## Decision

**Date:** 2026-09-04
**Status:** **Picked up and closed on 2026-09-04** on a devenv7 Windows host (ARM64 Windows 11,
`dist-devenv7-windows-hostarch-a64-targets-a64-x64-x86`, MSVC 14.38.33130, clang-cl 16.0.5,
Boost 1.90.0, OpenSSL 3.5.4, JDK 25). Every item below was applied and validated; see
"Outcome (2026-09-04)". The one thing that could not be checked is the OpenSSL 1.1.1w side of
item 8: this dist carries 3.5.4 only.

**How to pick this up:** `windows-only-residual-findings-instructions.md`, next to this file, is
the step-by-step instruction for a Claude Code session on that host (rules, order, verification
per item, and which records to update afterwards).

The development checkout has no Windows host and there is no CI. The makefile items change what
the Windows toolchain is told to do, the code item changes how the JVM is loaded, and none of them
can be exercised or even compiled here. The same "record, do not fix blind" rule the deployment
scripts follow (`scripts/devenv7/docs/supply-chain-verification-deferral.md`) applies.

| # | Finding | Location | What is wrong | Fix when picked up | How to validate |
|---|---|---|---|---|---|
| 1 | M-19 | `projects/make/toolchain/msvc-default.mk:346` (`-WX`), `:348-389` (`-Wno-*` list), `:441` (every include directory as `-I`) | Under `ccl16` the code-level suppressions (`-Wno-unused-variable`, `-Wno-unused-but-set-variable`, `-Wno-unused-private-field`, `-Wno-missing-braces`, `-Wno-writable-strings`, `-Wno-macro-redefined`, `-Wno-unused-local-typedef`, `-Wno-deprecated-declarations`) apply to `src/` as well as to the SDK and Boost headers they were added for, so `-WX` no longer catches those classes in project code; the root cause is that the SDK, MSVC, Boost, OpenSSL, JDK and json-spirit include directories are passed as user headers | pass those directories with `-imsvc` (clang-cl's `-isystem`; the `INCLUDE` list is assembled in `msvc-default.mk:155-176`, `3rd/boost/common.mk:14`, `3rd/openssl/common.mk:37`, `3rd/jdk/common.mk:30-37`, `3rd/json-spirit/4.08.mk`, and only `common.mk:157-161` and the per-target `INCLUDE +=` lines are project directories), then delete the code-level `-Wno-*` entries one at a time and fix what `-WX` reports; keep only the Microsoft-extension suppressions (`-Wno-microsoft-cast`, `-Wno-microsoft-template`) | full `ccl16` build of every module, debug and release, on a64 and x64 |
| 2 | L-33 | `msvc-default-x64.mk:1, 31-36`, `msvc-default-x86.mk:1, 30-35`, `msvc-default-a64.mk:4` | the arch makefiles test `ifeq ($(TOOLCHAIN),vc143)` only, so `ccl16` falls through to the VS2013-layout `PATH` entries (nonexistent directories) or, for a64, gets nothing; it works today only because `msvc-default.mk:224-229` prepends `CLANG_CL_DIR` and the correct `bin\<host>\<target>` path first | `ifneq (,$(filter vc143 ccl16,$(TOOLCHAIN)))`, the form `msvc-default.mk:160, 171, 193, 224` already use | `make -pn ... TOOLCHAIN=ccl16 | grep '^PATH '` shows no VS2013 entries; one `ccl16` build per architecture |
| 3 | L-34 | `msvc-default.mk:42, 44, 67, 69` | `$(firstword $(wildcard ...))` picks the lexicographically **lowest** MSVC toolset and Windows SDK directory while `scripts/devenv7/windows/internal/vs-detector.ps1:180-186` picks the **newest** (`Sort-Object Name -Descending`), so a dist which carries two toolsets or two SDKs builds Boost/OpenSSL with the newer one and the project with the older one, silently; the chosen versions are not printed | `$(lastword $(sort ...))` (the same lexicographic order as the detector's string sort) at all four sites, and `$(info Building with MSVCVERSIONTAG = ...)` / `WINSDK10VERSIONTAG` next to the existing `MSVCHOSTARCHTAG` line | a dist with two toolset directories: the log names the newer one |
| 4 | L-16 | `src/include/baselib/core/detail/OSImplWindows.h:941-956` (`loadLibrary`), `:3472`, `:3502` | `std::wstring( name.begin(), name.end() )` widens each byte independently, which is wrong for any non-ASCII UTF-8 path (three sites); `::LoadLibraryExW( path, NULL, 0 )` resolves `jvm.dll`'s dependents through the default search order including `PATH`, which is why `scripts/devenv7/AGENTS.md` demands `export PATH` for the JNI tests and which is also a hijack surface | a small `utf8ToUtf16()` helper over `::MultiByteToWideChar( CP_UTF8, MB_ERR_INVALID_CHARS, ... )` used at the three sites, and `LOAD_WITH_ALTERED_SEARCH_PATH` for the absolute-path load | `utf_baselib_jni` on Windows loads the JVM **without** `PATH` exported; a registry test with a non-ASCII value name |
| 5 | L-17 (Windows half) | `scripts/bl_tool.py`, `is_reparse_link()` | fixed on the Linux side (fail-closed `st_file_attributes` fallback, unit-tested with a monkeypatched `lstat`); the real junction path has not been run | none expected | `make pytest` on a Windows host with a real junction (`mklink /J`) under the hashed tree |
| 6 | L-20 (Windows half) | `scripts/cl.py`, `scripts/clang-cl.py` | fixed on the Linux side (prefix parsing, `\ ` escaping, unit-tested including a GNU make parse); the wrappers have not been run against a real `cl.exe` under a profile path with a space | none expected | one `vc143` build from a checkout under a directory whose path contains a space; confirm no object rebuilds on a second `make` |
| 7 | I-17 (editor settings) | `settings/vscode/linux/x64/c_cpp_properties.json`, `settings/vscode/macos/arm/*.json`, `settings/vscode/windows/*.json` | reference devenv5 (`dist-devenv5-ub20-gcc1110-clang1201`, `dist-devenv5-darwin-20-arm`) and devenv3 (`dist-devenv3-windows`) dists, JDK 8, Boost 1.75 and OpenSSL 1.1.1k; the Linux a64 file was refreshed to the devenv7 layout from this checkout, the other three can only be checked on their platforms | mirror the a64 file with the platform's devenv7 dist name (`scripts/devenv7/AGENTS.md`, "Distribution Folder Naming") | open the workspace in VS Code on that platform: IntelliSense resolves `<boost/version.hpp>` and `<openssl/ssl.h>` |
| 8 | I-8 (applied blind) | `src/include/baselib/crypto/OpenSSLTypes.h`, the `_InterlockedExchangeAdd` shim | the shim's guard gained an upper bound (`OPENSSL_VERSION_NUMBER < 0x30000000L`) from the Linux checkout: the overload exists only for the 1.1.x private header `internal/refcount.h`, which the 3.x branch never includes, so on 3.x it was unreferenced and its removal cannot break a compile; the change could not be compiled here | none expected | one `vc143` and one `ccl16` build against OpenSSL 3.x, and one against 1.1.1w with `BL_USE_OPENSSL_1X=1`, of `utf_baselib_security` |

---

## Outcome (2026-09-04)

Host: ARM64 Windows 11, `dist-devenv7-windows-hostarch-a64-targets-a64-x64-x86`. All builds `-j1`.
"clean" below means the build reported zero warnings and zero errors with `-WX` in force.

| # | Finding | Outcome |
|---|---|---|
| 1 | M-19 | **Applied.** In the `ccl16` branch only, absolute include directories are now passed as `-imsvc <dir>` and the project directories (relative, derived from `TOPDIR`) stay on `-I`, with the same absolute/relative split and the same "`TOPDIR` must be relative" guard `gcc-default.mk` uses; `-imsvc` is emitted as two tokens because MSYS converts a bare `/c/...` argument but not one joined to an option it does not know. With the external headers marked as system headers, a measurement build of all 21 targets with `-WX` and every `-Wno-*` removed produced **zero** warnings from the SDK, MSVC, Boost, OpenSSL, JDK and json-spirit headers, and exactly five warning locations in `src/`. Four were fixed (see below) and only the two Microsoft-extension suppressions were kept, so the other fourteen `-Wno-*` entries are gone. Fixes: `OSImplWindows.h` `JOBOBJECT_BASIC_LIMIT_INFORMATION`/`JOBOBJECT_EXTENDED_LIMIT_INFORMATION` initialisers `= { 0 }` → `= {}` (`-Wmissing-braces`, the nested-subobject case); `AcquireCredentialsHandleW`'s `pszPackage` argument wrapped in `const_cast< LPWSTR >` (`-Wwritable-strings`, the API does not modify it); an unused `bl::time::time_duration timeout` removed from `BaseLib_EndpointSelectorImplTests`; and in `TestIO.h` the `base_type` alias kept but its `-Wunused-local-typedef` false positive suppressed for that one declaration behind `#ifdef __clang__` (clang does not count a member-initializer use as a use, and the alias is genuinely required — naming `connection_base_t` directly in the initializer list fails to compile under clang-cl 16). `-Wno-microsoft-cast` is retained because the shared-library loader's function-pointer cast is a deliberate Microsoft extension. **Verified:** full `ccl16` builds of all 17 test modules, the plugin and the four apps, clean in all six architecture/variant combinations (a64, x64, x86 × debug, release); `vc143` is untouched by the split and also builds clean. |
| 2 | L-33 | **Applied.** `ifneq (,$(filter vc143 ccl16,$(TOOLCHAIN)))` in `msvc-default-x64.mk`, `-x86.mk` and `-a64.mk`. **Verified:** `make -pn` before/after shows the `ccl16` `PATH` for x64 and x86 losing the nonexistent VS2013-layout entries (`VC/bin/x86_amd64`, `VC/bin`, `VC/redist/x86`) and gaining the correct `VC/Tools/Llvm/<arch>/bin` and `Hostarm64` cross entries; a64 gains its native entries. Backed by clean `ccl16` builds on all three architectures. |
| 3 | L-34 | **Applied.** `$(lastword $(sort ...))` at all four sites plus `$(info ...)` for `MSVCVERSIONTAG` and `WINSDK10VERSIONTAG`. **This dist made the finding live:** it carries two Windows SDKs (`10.0.22621.0`, `10.0.26100.0`), so `firstword` was selecting the *older* 22621 while `vs-detector.ps1` selects the newer. Every build now selects **10.0.26100.0** and names both versions in the log. `MSVCVERSIONTAG` is unchanged (a single toolset, 14.38.33130). **Verified:** `make -pn` diff of both tags across all six toolchain/arch combinations, and clean `ccl16` and `vc143` builds against the newer SDK. |
| 4 | L-16 | **Applied.** A `utf8ToUtf16()` helper over `::MultiByteToWideChar( CP_UTF8, MB_ERR_INVALID_CHARS, ... )`, failing through the file's `createException( "MultiByteToWideChar", ... )` pattern, replaces the three byte-widening `std::wstring( s.begin(), s.end() )` sites (`loadLibrary`, `RegOpenKeyExW`, `RegGetValueW`); `LoadLibraryExW` is given `LOAD_WITH_ALTERED_SEARCH_PATH` **only when the path is absolute**, because the flag is undefined for a relative name and `BaseLib_OSSharedLibTests` loads `kernel32.dll` by bare name. `BaseLib_OSRegistryValueTest` was extended to create a key and value whose names contain a non-ASCII character through the wide API and read them back through the UTF-8 helpers, and to require that a name which is not valid UTF-8 is rejected. **Verified on both toolchains:** registry test 7/7 and shared-library test 6/6 assertions; `test_utf_baselib_jni` passes. **The prediction held:** with the JDK absent from `PATH` and only `JAVA_HOME` set, `Jni_CreateJniEnvironments` passes 11/11 (`ccl16` and `vc143`), so the altered search path removed the `PATH` requirement for loading the JVM. The JNI lesson in `scripts/devenv7/AGENTS.md` was updated accordingly; the makefiles still export `PATH` because gradle and the java tests invoke `java` directly. |
| 5 | L-17 | **Confirmed, no change needed.** A directory tree containing a real junction (`mklink /J`) makes `bl_tool.py hash --path <tree>` fail with `[ERROR] Symlink encountered` and exit code 1; the same tree with the junction removed hashes normally and exits 0. `os.path.isjunction()` (Python 3.14) detects it. |
| 6 | L-20 | **Confirmed, no change needed.** Compiling through `scripts/cl.py -M` against a real `cl.exe`, with the header under a directory whose name contains a space, produced a `.d` in which the header path appears whole with the space escaped (`...\l20\inc\ dir\hdr.h`); GNU make parses that file and treats it as a single prerequisite (reports the target up to date rather than re-running the recipe). |
| 7 | I-17 | **Applied.** `settings/vscode/windows/c_cpp_properties.json` rewritten on the `linux/a64` template with `${workspaceFolder}` for project directories and `${env:USERPROFILE}` plus this host's devenv7 dist name for the rest, in three configurations (`windows-msvc-arm64`, `windows-msvc-x64`, `windows-clang-arm64`), carrying the define list the makefiles actually pass. `launch.json` moved to the devenv7 build-tree layout and normalised to LF, which every other settings file in the tree already uses. **Verified:** both files parse, and every `includePath` and `compilerPath` entry exists on disk. Note the makefiles also pass two include roots this dist does not have (`winsdk/8.1/...` and `openssl/3.5.4/source`); a nonexistent `-I` is silently ignored by the compiler, and those two were left out of the editor settings rather than listed as broken paths. The Linux x64 and macOS settings were left alone: they belong to their own platforms. |
| 8 | I-8 | **Confirmed compiling, partially verified.** `utf_baselib_security` builds and `test_utf_baselib_security` reports "No errors detected" against the dist's OpenSSL 3.5.4 with `ccl16` (debug and release) and with `vc143` — the case the new upper bound changes, where the overload is gone. **The 1.1.1w side remains unverified on Windows:** this dist has no `openssl/1.1.1w` directory, so `BL_USE_OPENSSL_1X=1` cannot be built here. |

---

## Residual found by the closure audit (2026-09-05)

The audit of the review's closure (every finding re-checked against the code from the Linux
checkout) found one Windows-only leftover of item 4, recorded here under the same rule as the
original items: it cannot be compiled or exercised from this checkout.

| # | Finding | Location | What is wrong | Fix when picked up | How to validate |
|---|---|---|---|---|---|
| 9 | L-16 (residual) | `src/include/baselib/core/detail/OSImplWindows.h:2806` (`RobustNamedMutex`, name passed to `::CreateMutexW`), `:3396` (`::GetNamedSecurityInfoW`) | two `std::wstring( name.begin(), name.end() )` byte-widening sites survived item 4, which converted only the three sites the review named. The mutex site turns a non-ASCII UTF-8 name into a different kernel object name, so two processes agreeing on the UTF-8 name would not share the mutex; the security-info site is a double conversion (`fs::path::string()` narrows the native wide path, then it is byte-widened back). ASCII names are unaffected. The two `srcDirPath.native()` / `dstDirPath.native()` widenings at `:3280` and `:3307` are copies of an already wide string and are not part of this | `utf8ToUtf16()` at both sites; at `:3396` pass `pathPreferred.native()` directly instead of narrowing and widening | `utf_baselib` on Windows: a named mutex test with a non-ASCII name shared between two `RobustNamedMutex` instances, and the file security tests under a non-ASCII directory |

---

## O-3/O-4 Windows verification (2026-09-05)

Review findings O-3 (detached children: closed standard descriptors, dead `Detach|Redirect`
redirects, zombies, 2 s stall on handle release) and O-4 (non-async-signal-safe code in the forked
child) were fixed on 2026-09-05 in the UNIX spawn path only (`OSImplUNIX.h`, commit `a4833d2`).
The Windows path (`OSImplWindows.h`: `OSImplT::createProcess` and `EncapsulatedProcessJobHandle`)
was assessed from the Linux checkout as needing no change. This section records the confirmation
on the same host as the outcome above (ARM64 Windows 11,
`dist-devenv7-windows-hostarch-a64-targets-a64-x64-x86`, MSVC 14.38.33130, clang-cl 16.0.5).

**How it was picked up:** `windows-process-spawn-o3-o4-instructions.md`, next to this file
(completed; kept as the record of what was checked and how).

**Outcome: the assessment holds. No change to `OSImplWindows.h` and no change to the
`DetachProcess` comment in `OSImplPlatformCommon.h`.** One test-only change was made: a Windows
counterpart of the guarded UNIX tests, `BaseLib_OSCreateProcessDetachedWindowsTests` in
`src/utests/utf_baselib/TestBaselibDefault.h` (inside `#if defined( _WIN32 )`, directly after the
UNIX block), so the parity the comment claims is pinned by a test on both platforms
(commit `e02eb07`).

| # | Claim from the Linux assessment | How it was confirmed | Result |
|---|---|---|---|
| 1 | O-4 does not apply: `CreateProcessW` runs no parent code in the child | reading only (`OSImplWindows.h:1538-1585`: the only work between creation and `ResumeThread` is `AssignProcessToJobObject`, in the parent) | confirmed |
| 2 | a detached, non-redirected child starts with usable standard handles (those of its own hidden console); the POSIX fd-reuse hazard has no counterpart | new test: `cmd.exe /c "ver 3<&0 4>&1 5>&2 >nul && echo hello>file"` under `DetachProcess`. Each `>&N` / `<&N` duplication fails and breaks the `&&` chain when handle N is invalid (checked by hand with `4>&7`: exit 1, no file), so the file exists only if all three standard handles are valid. Also `cmd.exe /c "echo hello"` detached without redirect | confirmed: exit 0, file contains `hello`; exit 0 |
| 3 | `DetachProcess \| RedirectStdout` delivers the output to the callback for the callback's lifetime | new test: the ios callback reads `hello` from `cmd.exe /c "echo hello"`, then `tryAwaitTermination` returns 0 | confirmed |
| 4 | no zombies: the process object goes away with its last handle | reading only; the new test additionally shows `OpenProcess` by pid succeeding after the handle release and the process gone after `TerminateProcess` plus wait | confirmed |
| 5 | releasing the handle of a running detached child neither stalls nor terminates it | new test: `ping -n 31 127.0.0.1` detached, handle released, `elapsed < 1 s` (the whole case runs in 120-440 ms); then `OpenProcess( PROCESS_TERMINATE \| SYNCHRONIZE )` on the pid succeeds, `WaitForSingleObject( h, 0 )` returns `WAIT_TIMEOUT` (still running), and the child is terminated through that handle | confirmed |
| 6 | a detached handle stays waitable | existing `BaseLib_OSCreateProcessTests` Windows block (`cmd.exe /c exit 0` detached, `tryAwaitTermination` == 0) | passed |
| 7 | parent-death kill through the job object (`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`; detached children break away) | existing `BaseLib_OSTerminateProcessTree` and `BaseLib_OSTryAwaitTerminationTests` | passed |

**Builds (all `-k -j1`, zero warnings under `-WX`):** `utf_baselib` on a64 and x64 with `vc143`
and `ccl16`, debug and release (8 builds), plus x86 debug with both toolchains (2);
`utf_baselib_messaging` debug on a64 and x64 with both toolchains (4). The three UNIX-only test
cases compile out on every one of them and the shared `DetachProcess` comment compiles in.

**Tests:** on all 10 `utf_baselib` binaries, `BaseLib_OSCreateProcess*` (the two existing cases,
the two redirected cases and the new one), `BaseLib_OSTryAwaitTerminationTests` and
`BaseLib_OSTerminateProcessTree`: 79/79 assertions, 20/20 of them in the new case. On all 4
`utf_baselib_messaging` binaries, `IO_MessagingMessageProcessingOutboundQueueTests` (new, R-5) and
the four existing `IO_MessagingMessageProcessing*` cases: 50/50 assertions. The x64 and x86
binaries ran under emulation on the ARM64 host. `tasklist` showed no `cmd.exe` or `ping.exe` left
behind by the probes.

**Record update done from the Linux checkout (2026-09-05):** the O-3 and O-4 "Decided" entries in
`notes/reviews/major/update_2026/whole-library-cxx-review-fable51-decisions.md` now carry
"Windows verified on 2026-09-05"; that tree is not tracked in git and did not exist on the
Windows checkout, so the Windows session could not add the line itself.

---

## What limited the exposure while this was open

- Items 1-3 changed nothing about what shipped: they concerned which warnings the Windows build
  reports (1) and how the toolchain is located (2, 3).
- Item 4 was pre-existing code, adjacent to the JNI probing rewritten on this branch; the JNI tests
  passed on Windows with `PATH` exported, which the documentation required.
- Items 5 and 6 were already fixed; only the runtime confirmation on the target platform was
  outstanding.
- Item 7 affected developer convenience files, not the build.

One assumption did not survive contact with the host: item 3 was expected to be dormant on a
"single-toolset, single-SDK" dist, but this dist carries **two** Windows SDKs, so the makefiles had
been silently building against the older 10.0.22621.0 while the provisioning scripts selected
10.0.26100.0. Applying the fix moved every Windows build onto the newer SDK — a real change to what
ships, not a no-op. Both toolchains build and test clean against it.

## Conditions to revisit

- **Closed 2026-09-04**; the trigger fired and the outcome is recorded above. The remaining gaps are
  the OpenSSL 1.1.1w side of item 8, which needs a dist that carries `openssl/1.1.1w`, and the
  L-16 residual (item 9, found 2026-09-05), which waits for the next Windows session.
- **Amended 2026-09-08.** Item 9 is done, and so are items 10, 11, 12, 13(a)(b)(d), 14, 15, 16 and
  17 - see "Outcome (2026-09-08)" below. Nothing from items 10-17 remains open.
  1. **Sub-item 13(c), the handle-inheritance race, is CLOSED as a deferral** - a recorded risk
     acceptance, not a to-do - in
     `notes/plans/issues/windows-handle-inheritance-race-deferral.md`, which carries the decision, the
     reasoning, the full implementation design and the named triggers that should reopen it. **It
     should not be re-reported as a new finding by a future review**; the defect is real and remains
     in the product on Windows, and that is a decision rather than an oversight. Exposure is limited
     because nothing in the repository spawns two redirected children concurrently, which is also why
     every existing suite is green.
  2. **The OpenSSL 1.1.1w leg** is the one item still genuinely open, unchanged and still blocked for
     the cross-platform reason in the 2026-09-07 note below rather than for want of a Windows dist.
  Two further defects were found on the way and **fixed** on the owner's instruction, as items 18
  and 19 of the outcome section: the `os::fread` / `os::fwrite` `errno`-versus-`ferror`
  discriminator in shared code, and the meaningless error code on the registry exceptions.
  **Item 18 still needs a Linux run** - it is unchanged on UNIX by construction and by inspection,
  but it was compiled and executed only on Windows, so that half is the mirror image of what items
  2.1 / 2.2 were before this session. One thing is recorded and not fixed: `fs::TmpDir`'s
  long-file-name prefix versus `cmd.exe`.
- A third Windows SDK or a second MSVC toolset appears in a dist: the selection is now "newest by
  lexicographic sort", matching `vs-detector.ps1`, and the chosen versions are printed in the build
  log, so a mismatch should be visible rather than silent.

**Note on the 1.1.1w gap (2026-09-07).** The Linux dist *does* carry `openssl/1.1.1w`, and the
stage-10 attempt to build `utf_baselib_http` and `utf_baselib_security` with `BL_USE_OPENSSL_1X=1`
was made from this checkout. It fails before reaching any of the reviewed code, with
`-Werror,-Wdeprecated-declarations` on `RSA_free` (`crypto/OpenSSLTypes.h:131`), `SHA512_*` /
`SHA384_*` (`crypto/HashCalculator.h:54-122`), `RSA_new` and `EVP_PKEY_get1_RSA` /
`EVP_PKEY_set1_RSA` (`crypto/RsaKey.h:88-194`) and further sites. None of those files was touched by
the review implementation, so this is the tree's pre-existing state, not a regression: the
`OPENSSL_API_COMPAT=0x10100000L` the makefiles pass silences the 3.x deprecations against the 3.5.4
headers, and the 1.1.1w configuration does not get the equivalent treatment. **The 1.1.1w side of
item 8 therefore needs the build restored before it can be verified on any platform**, which is a
separate piece of work from acquiring a Windows dist that carries 1.1.1w.

## References

- Review: `notes/reviews/major/update_2025/v1/pr_review_analysis_fable51.md` (M-19, L-33, L-34,
  L-16, L-17, L-20, I-17)
- Per-round status record: `notes/plans/issues/pr-review-fable51-residual-findings-status.md`
- Deployment-script deferrals: `scripts/devenv7/docs/supply-chain-verification-deferral.md`

---

## Whole-library C++ review, 2026-09-06/07: items 10-14

The whole-library review recorded in `notes/reviews/major/update_2026/whole-library-cxx-review-fable51.md`
was implemented from this Linux checkout in stages 3-10. Its Windows-only section (W-1..W-4) was
established by *reading* `OSImplWindows.h`; none of it can be compiled here. Item 14 is the
converse case: code that was **written** here for the Windows half of a fix whose UNIX half is
tested, and which therefore ships unverified on Windows.

The step-by-step handoff for a Windows session is
`notes/plans/issues/whole-library-windows-residuals-instructions.md`, next to this file. (Note that
the two instruction files the sections above cite - `windows-only-residual-findings-instructions.md`
and its O-3/O-4 companion - are not present in this checkout; `notes/reviews/` is untracked and the
handoffs were consumed by the sessions that ran them.)

| # | Finding | Location | What is wrong | Fix when picked up | How to validate |
|---|---|---|---|---|---|
| 10 | W-1 (Medium) | `src/include/baselib/core/detail/OSImplWindows.h:1636-1656`, the `createProcess( vector )` overload | the argv-to-command-line quoting does not implement the CRT rules: it quotes only when the argument contains a space, doubles **every** backslash inside quotes, and escapes `"` unconditionally. So an empty argument vanishes (every later `argv` index shifts), an argument containing a tab is split in the child, an argument containing `"` but no space merges with its neighbours, and `C:\dir x\f` arrives as `C:\\dir x\\f` | the documented algorithm: quote when the argument is empty or contains space, tab, `"` or newline; before a `"` emit `2n+1` backslashes, before the closing quote `2n`, otherwise `n` | round-trip through a child that prints its `argv` - the scaffold is at `src/utests/utf_baselib/TestBaselibDefault.h:1577`. Cases: empty argument, embedded tab, embedded quote without a space, a trailing backslash before the closing quote, a path with single backslashes |
| 11 | W-2 (Medium) | `OSImplWindows.h:1443-1444`, the `createProcess( commandLine )` overload | `std::copy` byte-widens the UTF-8 command line into `WCHAR`, so any non-ASCII byte reaches `CreateProcessW` as mojibake. Same class as item 4 / item 9 but a different idiom, which is why it survived both | `utf8ToUtf16( commandLine )` (the helper added by item 4, `OSImplWindows.h:640`) into a writable NUL-terminated buffer, since `CreateProcessW` may modify the command line in place | a child launched by command line with a non-ASCII argument prints it back unchanged; the existing `BaseLib_OSCreateProcess*` cases stay green |
| 12 | W-3 (Medium) | `OSImplWindows.h:3530-3565` (`tryGetRegistryValue`), deleter at `:227-242` | the `HKEY` is attached to the RAII holder before `RegOpenKeyExW` is checked, so on the ordinary "key not found" path an **indeterminate** handle is read (UB) and, when it is non-null, `RegCloseKey( garbage )` fails inside a `NOEXCEPT` deleter and RIPs the process. The `location` text passed to the exception tests the constant `HKEY_CURRENT_USER` instead of the `currentUser` parameter, so it always says `HKEY_CURRENT_USER` | initialise the `HKEY` to `NULL`, attach only on `ERROR_SUCCESS`, and use `currentUser ? ... : ...` for the location text | `BaseLib_OSRegistryValueTest` extended with a lookup of a key that does not exist (must return "not found", not abort) and one under `HKEY_LOCAL_MACHINE` whose failure message names the right hive |
| 13 | W-4 (Low-Medium, four independent sub-items) | `OSImplWindows.h:1555-1585`; `:2047-2053`; `:1687-1708`, `:898-939`, `:1536-1548`; `:2361-2384`, `:2457` and `:546-551` | (a) `pi.hThread` is closed only on the `assignNewJob` path, so every spawn that is already in a job or detached leaks a thread handle. (b) `LSA_UNICODE_STRING::Buffer` is treated as NUL-terminated and non-NULL; it is neither by contract. (c) `_wfopen` handles are inheritable (no `N` mode flag) and `bInheritHandles=TRUE` inherits *every* inheritable handle, so two concurrent `createProcess` calls cross-inherit each other's pipe ends and a reader sees EOF only when the *other* child exits. (d) `createJunction` strips the last character of the print name unconditionally - correct only with the LFN prefix, and `back()` on an empty string is UB; `tryTimedAwaitTermination` compares the wait result rather than `ec` against `STILL_ACTIVE`, so that check is dead | (a) attach `pi.hThread` to a holder unconditionally. (b) `std::wstring( Buffer, Length / sizeof( WCHAR ) )` with a NULL check. (c) the `N` flag on `_wfopen`, and `PROC_THREAD_ATTRIBUTE_HANDLE_LIST` - compatible with the `STARTUPINFOEXW` already used. (d) validate `to.is_absolute()` and build the print name from the path rather than by trimming; compare `ec` | (a) handle count of the parent (`GetProcessHandleCount`) stable across 100 detached spawns. (b) exercised by the existing user/SID paths. (c) **two threads each spawning a redirected child concurrently**: each must read only its own child's output and see EOF when *its* child exits. (d) `createJunction` to a relative path must be rejected; a junction created with a plain absolute path reads back with its full print name |
| 14 | N-2 / S-5 / O-7 Windows halves (written here, unverified) | `OSImplWindows.h:1687-1701` (`getPhysicalMemorySize`), `:1703-1711` (`getFileDescriptorSoftLimit`), `:3705-3727` (`createNewFilePrivate`), `:2841-2869` (`RobustNamedMutex` permissions parameter) | these four were added from this checkout as the Windows counterparts of UNIX code that is tested here, and compile only on Windows. `getPhysicalMemorySize` returns `ullTotalPhys` from `GlobalMemoryStatusEx` and `0` on failure; `getFileDescriptorSoftLimit` returns `0` ("not applicable" - Windows bounds handles by kernel memory, not by a per-process soft limit), which makes the derived connection cap fall back to the RAM term and the 4096 ceiling; `createNewFilePrivate` uses `CREATE_NEW` with `dwShareMode = 0`, which gives exclusive *sharing* during creation but **not** a restrictive DACL - a Windows file inherits its directory's ACL, so the S-5 "not readable by other local users" property is **not** delivered on Windows; `RobustNamedMutex` gained the `permissions` parameter for signature parity and ignores it (`BL_UNUSED`), `defaultPermissions()` returning 0 | compile and sanity-check the first two; for `createNewFilePrivate`, decide whether to build an explicit DACL (owner + SYSTEM + Administrators) via `InitializeSecurityDescriptor`/`SetSecurityDescriptorDacl` into `lpSecurityAttributes`, or to record the weaker Windows guarantee in the S-5 decision entry. No change is expected for `RobustNamedMutex` | `GlobalMemoryStatusEx` value sanity (non-zero, within a factor of two of Task Manager) and the derived cap logged once by `HttpServer` on Windows with the RAM term active; `createNewFilePrivate` returns false for an existing file and true otherwise, and - if the DACL is added - a second local user cannot open the file; `utf_baselib` `BaseLib_OS*` and `utf_baselib_http` green on `vc143` and `ccl16`, debug and release |

**Also open from the same review, and not Windows-only:** the OpenSSL 1.1.1w build required for
S-6/S-7 could not be run from this checkout either, for a reason that is not platform-specific -
see the 2026-09-07 note under "Conditions to revisit" above.

## C++ test enhancement, 2026-09-08: items 15-17 (test-enhancement residuals)

Added from item 5 of
`notes/reviews/major/update_2026/whole-library-cxx-test-enhancement-outstanding-issues-plan.md`. They
are independent of items 10-14 above and share the same Windows session; the step-by-step
instructions are in section 4b of
`notes/plans/issues/whole-library-windows-residuals-instructions.md`.

| # | Finding | Location | State on Linux | What the Windows session must do |
|---|---|---|---|---|
| 15 | The two inspection-only fixes of the test-enhancement work | `OSImplWindows.h:3151` (`tryGetUserDomain` reading `USERDNSDOMAIN` twice where it meant `USERDOMAIN`) and `:1402` (`BL_ASSERT( out )` where the merged-redirect branch needs only the merged pipe) | **fixed by inspection, never compiled or executed.** The UNIX twin of the second one aborted the test binary with `SIGABRT` before it was fixed there | run `utf_baselib` on `vc143` and `ccl16`, debug and release, so both become execution-proven. The cases already exist: the T061 oracle `expectedUserDomainFromEnvironment()` in `TestBaselibDefault.h`, and the T059 file-callback block of `BaseLib_OSCreateProcessRedirectedMergedTests` |
| 16 | `core/specific/ComUtils.h` and `core/specific/WindowsShellShortcut.h` are not self-contained | both headers | **include fixes applied on Linux 2026-09-08 and uncompiled** - `ComUtils.h` gained `<baselib/core/BaseIncludes.h>`, `WindowsShellShortcut.h` gained `<baselib/core/FsUtils.h>` and `<baselib/core/Logging.h>` | build `utf_baselib` and confirm the `_WIN32` block of `TestPublicHeaderInstantiation.cpp` (T373, `:57-59` and `:308-323`) compiles both standalone; add any further include in the same shape and say which and why |
| 17 | Seven Windows-only test tasks have never compiled | T054's Windows arm, T058, T060, T061, T062, T253, T357 (plan section 13.4.1) | written, never built | compile and run once, repairing **test code only**. T253's dropped HKLM hive assertion becomes live with item 12 (W-3), and T058's disabled argv case (`TestBaselibDefault5.h:649`, `productionArgvQuotingIsFixed = false`) with item 10 (W-1) - flip each flag in the same change as the fix it waits for |

---

## Outcome (2026-09-08): items 9-19 applied, 13(c) deferred and closed

Run on the recorded Windows host (ARM64 Windows 11, devenv7 dist
`dist-devenv7-windows-hostarch-a64-targets-a64-x64-x86`, MSVC 14.38.33130, Windows SDK
10.0.26100.0, OpenSSL 3.5.4, Boost 1.90.0), from branch `lazari2`. Nothing was committed.

**Sub-item 13(c), the handle-inheritance race, is the one item not implemented, and it is CLOSED as a
deferral rather than left open.** The decision, the reasoning, the full implementation design and the
named triggers which should reopen it are in
`notes/plans/issues/windows-handle-inheritance-race-deferral.md`. It is a recorded risk acceptance:
the defect is real and remains in the product on Windows, exposure being limited because nothing in
the repository spawns two redirected children concurrently. **A future review should treat it as
decided and not re-report it as a new finding.**

Two things drove the decision. The fix is a conversion of the whole spawn path rather than a localised
change, on the most heavily used function in the OS layer, with no consumer asking for it; and the
instruction text it was to be implemented from is wrong about the starting point - the function builds
a plain `STARTUPINFOW`, not the `STARTUPINFOEXW` that both the handoff and the review claim it
"already builds", which is why the item was originally sized alongside (a), (b) and (d). See section 0
of `whole-library-windows-residuals-instructions.md` for that and two further errata.

| # | Finding | Outcome |
|---|---|---|
| 9 | L-16 residual, the two surviving byte-widening sites | **Applied.** `RobustNamedMutex` (`OSImplWindows.h:2960`) now converts its name with `utf8ToUtf16( )`; the `::GetNamedSecurityInfoW` site in `setWindowsPathPermissions` (`:3549`) passes `pathPreferred.native()` straight through instead of narrowing to `string()` and byte-widening back. **New cases:** `BaseLib_OSNamedMutexNonAsciiNameWindowsTests` creates a mutex whose UTF-8 name carries a non-ASCII character and then asks `::OpenMutexW` for that name converted through the wide-string converter - the kernel namespace itself is the oracle, so the test cannot encode the same mistake; before the fix the object existed only under the mojibake name and the open failed with ERROR_FILE_NOT_FOUND. `BaseLib_OSPathPermissionsNonAsciiWindowsTests` sets permissions on a file inside a directory whose name carries a non-ASCII character, which before the fix failed with ERROR_FILE_NOT_FOUND. The cross-process form the record asked for was not used: proving the NAME is what the defect is about, and `::OpenMutexW` proves it in one process without a child-mode harness. |
| 10 | W-1, `createProcess( vector )` argv quoting | **Applied.** The heuristic is replaced by `appendQuotedArgument( )` (`:1679`), implementing the documented rule: quote when the argument is empty or contains a space, tab, newline, vertical tab or `"`; emit `2n + 1` backslashes before a literal `"`, `2n` before the closing quote and `n` otherwise. **The verification method had to change, and the record's and handoff's suggestion is wrong:** a batch harness cannot be the oracle, because `cmd.exe` applies its own parsing rather than `CommandLineToArgvW`'s and its `shift` loop cannot represent an empty argument at all. `::CommandLineToArgvW` - the parser the rule is *defined* by - is used instead, driven directly against the public static helper, exactly as `BaseLib_OSShellEscapeForRunAsUserTests` drives the UNIX twin `transformCommandLineArguments`. `BaseLib_OSCreateProcessArgvQuotingWindowsTests` round-trips ten arguments both together and one at a time (empty, embedded tab, embedded quote without a space, `C:\Program Files\x`, a trailing backslash, `a\\b`, `a\\"b`, a fully quoted argument), pins the exact text the algorithm must emit for five of them, and keeps an end-to-end half through a real child. `productionArgvQuotingIsFixed` is gone with the gate it guarded. Two things learned on the way, both recorded in the case: a trailing backslash is **not** by itself a reason to quote, and `::CreateProcessW` does not hand a `.bat` to the command interpreter, so the end-to-end half names `cmd.exe` explicitly. |
| 11 | W-2, the byte-widened command line | **Applied.** `utf8ToUtf16( commandLine )` into the writable NUL-terminated `std::vector< WCHAR >` the call already used (`:1469-1481`). **New case:** `BaseLib_OSCreateProcessNonAsciiCommandLineWindowsTests` asks a child to test a path carrying a non-ASCII character for existence and answer with a pure ASCII word, so the console code page cannot influence the result; a negative control asks about a path which really is absent. Before the fix the child was handed the byte-widened path and answered nothing. |
| 12 | W-3, `tryGetRegistryValue` | **Applied.** `HKEY regKeyHandle = NULL`; the holder is attached only when `RegOpenKeyExW` returned `ERROR_SUCCESS`; `location` is derived from the `currentUser` parameter instead of from the constant. **New case:** `BaseLib_OSRegistryHiveDiagnosticsWindowsTests` reaches the `HKEY_LOCAL_MACHINE` diagnostic deterministically and without administrator rights through `HKEY_LOCAL_MACHINE\SECURITY`, which is readable by SYSTEM only, and the `HKEY_CURRENT_USER` one through a volatile key carrying a `REG_SZ` longer than the fixed 1023-character buffer - so **both** directions of the corrected expression are pinned, and both places the text is used (the key open and the value read). It also drives the not-found path 200 times, which is the path that used to attach an indeterminate handle. **Note on the assertion method:** `SystemException` composes its `what( )` from the API name and the system error text, so the `BL_MSG( )` text is carried by `errinfo_message` and has to be read through `BaseException::message( )`; `UTF_REQUIRE_THROW_MESSAGE` matches `what( )` and never sees it. **The UB itself could not be shown failing:** on this Windows build `RegOpenKeyExW` does set `*phkResult` to NULL on failure, so the pre-fix code read a null rather than a garbage handle and the deleter's `if( hkey )` absorbed it. The fix is correct by construction; the crash the record predicts needs a Windows version which leaves the parameter untouched. |
| 13 | W-4, sub-items (a), (b) and (d) - **(c) deferred to its own plan** | **(a) Applied.** `pi.hThread` is attached to its holder unconditionally (`:1591-1601`) rather than only on the `assignNewJob` path. **New case:** `BaseLib_OSCreateProcessThreadHandleLeakWindowsTests` measures `::GetProcessHandleCount` across 50 **detached** spawns - detached is the form which takes neither branch and therefore leaked unconditionally - and the count was **244 before and 244 after**. The assertion is on the absence of growth proportional to the spawn count rather than on an exactly equal number, because the process is multi-threaded. **(b) Applied.** A `lsaString2wstring( )` helper (`:697-712`) builds the string from `Length / sizeof( WCHAR )` and returns empty for a NULL `Buffer`, replacing two sites which passed `LSA_UNICODE_STRING::Buffer` to a wide-string comparison and to `to_bytes` as though it were NUL-terminated. Covered by `BaseLib_OSLoggedInUserNamesTests`, green. **(d) Applied.** `createJunction( )` now ensures the substitute name's trailing backslash for **every** spelling of the path rather than only inside the long-file-name branch, which is what made the print name one character short for a plain absolute target; `BL_CHK_ARG( fs::path( targetClean ).is_absolute(), to )` rejects a relative target and, with it, the empty path the old `back()` could be asked about; and `tryTimedAwaitTermination` compares `ec` rather than the wait result against `STILL_ACTIVE` (`:546-556`). **New case:** `BaseLib_OSJunctionPrintNameWindowsTests` reads the reparse point directly - `os::getJunctionTarget( )` reads the *substitute* name, which is why `BaseLib_OSJunctionsTests` never saw this - and got print name `C:\Users\...\print-name-target` with substitute name `\??\C:\Users\...\print-name-target\`. **The defect was live on every junction the library creates:** `fs::TmpDir` paths are not long-file-name prefixed at the point `createJunction` sees them, so the pre-fix print name was truncated to `...\print-name-targe`. A relative target is rejected with `ArgumentException`. |
| 14 | N-2 / S-5 / O-7 Windows halves | **Verified; the S-5 DACL question answered "record the weaker guarantee".** `getPhysicalMemorySize( )` returns 8 583 315 456 bytes (8185 MB) on this host, which matches what the OS reports; `getFileDescriptorSoftLimit( )` returns 0; `createNewFilePrivate( )` returns true once and false for an existing path, and false rather than throwing when the parent directory is absent; `RobustNamedMutex`'s ignored `permissions` parameter compiles and `defaultPermissions( )` is 0. **New case:** `BaseLib_OSResourceLimitsWindowsTests` for all four, plus an extension of the existing `BaseLib_HttpServerDerivedConnectionCapTest` in `utf_baselib_http` which feeds the **real** host values into `getDerivedMaxConnections` and asserts that on Windows the descriptor term is skipped, i.e. the cap equals the same arithmetic with that term forced to zero. **The DACL was not built.** The decision is recorded in the S-5 entry of the decisions file as a property of the shipped product: on UNIX the dump is mode `0600` and unreadable by other local users, on Windows it inherits the ACL of the temp directory and a local user who can read that directory can read the dump. What Windows does guarantee is the creation ordering (`CREATE_NEW` fails if the path exists) and `dwShareMode = 0` during creation. The test asserts the creation semantics only and carries the same note, so the absence of an ACL assertion is not mistaken for a gap. |
| 15 | The two inspection-only fixes | **Both execution-proven.** The merged-redirect assertion (`BL_ASSERT( ! callbackIos || out )`) is exercised green by `BaseLib_OSCreateProcessRedirectedMergedTests` (2/2) and `BaseLib_OSCreateProcessMergedWithFileCallbackTests` (8/8) - no abort, where the UNIX twin had aborted with `SIGABRT` before its fix. **The `USERDOMAIN` fix needed a new case, and this is an erratum against section 4b:** the existing `BaseLib_GetUserDomainTests` compares against an oracle derived from the same environment, which is the right shape, but on a host which is not domain joined (`USERDNSDOMAIN` unset, `USERDOMAIN` equal to `COMPUTERNAME` - the state of this host) the expected answer is the empty string and **the pre-fix implementation also returned the empty string**, so that case cannot tell a fixed implementation from a broken one. `BaseLib_OSUserDomainEnvironmentWindowsTests` drives the three variables itself and pins every leg of the contract, including the discriminating one - `USERDNSDOMAIN` unset and `USERDOMAIN` naming something other than the computer name, which returned empty before the fix and returns the domain after it. One guard stays covered by inspection only: `os::setEnvironmentVariable( )` refuses an empty value (`putenvWrapper` fails with EINVAL), so an empty `USERDOMAIN` cannot be created through the public API; the case asserts that refusal instead. |
| 16 | `ComUtils.h` and `WindowsShellShortcut.h` self-containment | **Confirmed, no further include needed.** The `_WIN32` block of `TestPublicHeaderInstantiation.cpp` (T373) compiles both headers standalone and `BaseLib_PublicHeadersAreSelfContainedAndInstantiable` passes 34/34 on both toolchains. The three includes applied on Linux on 2026-09-08 - `<baselib/core/BaseIncludes.h>` in `ComUtils.h`, `<baselib/core/FsUtils.h>` and `<baselib/core/Logging.h>` in `WindowsShellShortcut.h` - are exactly sufficient. |
| 17 | The seven never-compiled Windows test tasks | **Compiled and run; two real repairs, both test-code only.** (1) **A Windows-only compile break in two places:** a local named `small` does not compile, because `rpcndr.h` - which the SDK headers pull in - defines `small` as a macro for `char`. `TestBaselibDefault.h:8878` (`BaseLib_TextFilesEncodingTests`) and thirteen references in `TestBaselibDefault7.h` (`BaseLib_DataBlockReadWriteCodecTests`) were renamed to `smallPath` / `smallBlock`, each with a comment saying why, and both cases then pass (44/44 and 21/21). **This is why `utf_baselib` did not build on Windows at all**, so nothing else in the module could run until it was fixed. (2) `BaseLib_OSStdioReadWriteErrorTests` (`TestBaselibDefault4.h:135`) asserted a glibc-specific outcome; see the out-of-scope finding below. With both repaired, the module's Windows-only cases are green. T253's dropped hive assertion is restored by the new case listed under item 12, and T058's disabled argv case by the one under item 10 - each flipped in the same change as the fix it waited for, as instructed. |

### Build and run matrix actually executed

A cross-section rather than the full matrix, at the user's direction. `-j1` throughout, `-WX` in
force with external headers passed as system headers, so every warning would have been ours -
**there were none, in any row.**

Every row below was built and run **after** the last source change, items 18 and 19 included.

| Module | Toolchain | Variant | Arch | Build | Run |
|---|---|---|---|---|---|
| `utf_baselib` | `vc143` | debug | a64 | clean, 0 warnings | **6922 / 6922 assertions, 54 cases, 0 failures** |
| `utf_baselib_http` | `vc143` | debug | a64 | clean, 0 warnings | **23 / 23 assertions** (derived cap + policy hysteresis) |
| `utf_baselib` | `ccl16` | release | a64 | clean, 0 warnings | **6922 / 6922 assertions, 54 cases, 0 failures** |
| `utf_baselib_http` | `ccl16` | release | a64 | clean, 0 warnings | **23 / 23 assertions** |
| `utf_baselib` | `vc143` | debug | **x64 (emulated)** | clean, 0 warnings | **6727 / 6727 assertions, 31 cases, 0 failures** - the process, registry, junction, mutex and stdio suites plus `FsUtils_*` |

The case filter was `BaseLib_OS*` plus `BaseLib_GetUserDomainTests`,
`BaseLib_PublicHeadersAreSelfContainedAndInstantiable`, `BaseLib_DataBlockReadWriteCodecTests`,
`BaseLib_TextFilesEncodingTests`, `FsUtils_*` and `BaseLib_copyDirectoryPermissions_Tests`; the x64
row narrowed to the process items plus every new Windows case. `FsUtils_*` is in the filter because
of item 18: `FsUtils_SafeFileStreamWrapperTests` alone contributes 3010 assertions over the
Boost.Iostreams device path whose `checkStream( )` that item refactored, which is the regression
cover for it. The derived connection cap logged `Derived maximum connections on this host is 4096
from physical memory 8583315456 and descriptor soft limit 0` on both toolchains, which is the N-2
fall-back of item 14 observed rather than inferred. `tasklist` afterwards showed no `ping.exe` and
no test child left behind.

Items 18 and 19 are nonetheless only **compiled and run on Windows** - see the note under item 18
about the Linux run its UNIX half still needs.

Not run, and deliberately so: `x86`, the `vc143` release and `ccl16` debug corners, the other
fifteen test modules, and the OpenSSL 1.1.1w leg (still blocked for the pre-existing
`-Wdeprecated-declarations` reason recorded under "Conditions to revisit").

### Every defect shown failing before its fix

Reverting `OSImplWindows.h` wholesale does not compile against the new cases, because one of them
names the new `appendQuotedArgument( )` helper. So a **compilable pre-fix variant** was built
instead: the fixed file with each of the ten logic changes reverted in place and the new symbols
kept. The new cases were run against it, and these are the failures, verbatim. The variant was
discarded afterwards and the tree holds the fixed file.

| Item | Failure against the unfixed logic |
|---|---|
| 10 | `TestBaselibDefault5.h(732): fatal error: ... critical check EQUAL( arguments.size(), parsed.size() ) has failed` with `lhs value: 10` / `rhs value: 6` - ten arguments went in and **six** came back: the empty one produced no slot at all and the tab and quote cases merged with their neighbours |
| 9a | `OpenMutexW failed for the converted name with error 2` then `TestBaselibDefault5.h(1064): fatal error: ... critical check ( 0 != opened ) has failed` - error 2 is ERROR_FILE_NOT_FOUND: the mutex existed only under the byte-widened name |
| 9b | `TestBaselibDefault5.h(1127): fatal error: ... unexpected exception thrown by bl::os::setWindowsPathPermissions( filePath, bl::fs::perms::owner_all, bl::str::empty() , bl::fs::perms::group_all )` |
| 11 | `TestBaselibDefault5.h(1231): fatal error: ... critical check EQUAL( 1U, lines.size() ) has failed` with `lhs value: 1` / `rhs value: 0` - the child was handed the byte-widened path and printed nothing |
| 12 | `TestBaselibDefault5.h(1321): error: ... check EQUAL( std::string( "Cannot open registry key HKEY_LOCAL_MACHINE\\SECURITY" ), openFailureMessage( "SECURITY", false ) ) has failed` with `rhs value[size=51]: Cannot open registry key HKEY_CURRENT_USER\SECURITY` - the wrong hive, exactly as the review described |
| 13d | Three failures. The logged names were `substitute name '\??\C:\...\print-name-target'` (no trailing backslash) and `print name 'C:\...\print-name-targe'` - **one character short**, 100 bytes against the expected 101; and `TestBaselibDefault5.h(1700): fatal error: ... exception bl::ArgumentException expected but not raised` for the relative target |
| 13a | Not in this run: the handle-count case needs 50 spawns and the pre-fix count grew by one per spawn, which the post-fix run shows as **244 before and 244 after**. Left out of the pre-fix pass only because it is slow, not because it does not discriminate |
| 15 | `BaseLib_OSUserDomainEnvironmentWindowsTests` passes 10/10 in **both** passes, correctly: the `USERDOMAIN` fix was already in `HEAD` (it is one of the two inspection-only fixes of item 15), so the revert script left it alone. Its discriminating power is recorded under item 15 above |

### Items 18 and 19: two further defects found on the way, and fixed on the owner's instruction

Both were found while doing items 9-17, and both are outside the W-1..W-4 list. They were first
recorded here as "not fixed"; the owner then asked for both, so they are done. Added 2026-09-08.

| # | Finding | Outcome |
|---|---|---|
| 18 | **`os::fread( )` and `os::fwrite( )` decide "real failure" versus "short transfer at end of file" with `errno`, which the Windows CRT does not set.** `core/OS.h`. Measured on this host with vc143 / UCRT: `std::fread( )` on a stream opened `"wb"` returns 0 with **`errno == 0` and `std::ferror( ) == 1`**, so a genuine read error was reported as `"Reading past the end of file"` carrying `EPERM` instead of its own message. `std::fwrite( )` carried the identical defect | **Applied, and NOT as a platform abstraction.** The question was asked whether to hide this behind a per-platform API in the `OSImpl*.h` headers. It should not be, for two reasons: `std::ferror` is standard C and behaves the same on both platforms (it is set on Windows for exactly the case where `errno` is not), so `OSImplWindows.h` and `OSImplUNIX.h` would have held two identical copies; and **the library already had the rule written down**, in platform-common code - `stdio_file_device_base::checkStream( )`, whose comment reads "std::ferror returns a flag and not an error code, so the real cause has to come from errno; when it is not available the generic io_error is reported instead". `os::fread`/`os::fwrite` simply were not following the house convention. So one `detail::getStdioTransferErrorCode( fileptr, capturedErrno )` was added at the top of the `detail` namespace in `OSImplPlatformCommon.h` - `std::ferror` decides whether there is a failure, the captured `errno` carries the cause where the platform provides one and `eh::errc::io_error` where it does not - and **all three** sites now use it, `checkStream( )` included, so there is exactly one copy of the rule in the tree instead of two. **UNIX behaviour is unchanged by construction:** glibc sets both `ferror` and `errno` for this failure, so the same branch is taken and the same `EBADF` is attached. **The test got simpler rather than more conditional:** the `bl::os::onWindows() ? ... : ...` pin is gone from `BaseLib_OSStdioReadWriteErrorTests`, which now asserts the single correct message plus `UTF_REQUIRE( EPERM != *errNo )` - precisely the assertion that fails against the old discriminator. 13/13 on Windows, and the stream-path cases `FsUtils_SafeFileStreamWrapperSeekTests`, `BaseLib_OSLargeFileSupportTests` and `BaseLib_TextFilesEncodingTests` are green, which is the regression cover for the `checkStream( )` refactor. **Needs a Linux run to be execution-proven on the UNIX side** - it is unchanged by construction and by inspection, but it was compiled and run only here |
| 19 | **`tryGetRegistryValue( )` attached a meaningless error code to its exceptions.** `createException( "RegOpenKeyExW" )` built the code from `::GetLastError()`, but `RegOpenKeyExW` and `RegGetValueW` report through their **return value** and are not documented to set the last error at all, so the code was whatever unrelated call had set it last. The `HKEY_LOCAL_MACHINE\SECURITY` failure reported `system:6` (ERROR_INVALID_HANDLE) where the real cause is ERROR_ACCESS_DENIED (5) | **Applied.** `createException( )` gained an overload taking an explicit error code, and the existing `::GetLastError()` form now delegates to it, so there is no duplicated `errinfo_file_name` handling. Both registry sites pass the status they actually received. This is the idiom the file already used for the same class of API elsewhere (`createSystemErrorCode( rc )` at the four `*SecurityInfo*` sites); the registry pair were the outliers. `BaseLib_OSRegistryHiveDiagnosticsWindowsTests` now asserts the code as well as the message and logs it: **`tryGetRegistryValue failed with error code 5 (Access is denied)`**, where the pre-fix run reported 6 |

### Still out of scope, recorded and not fixed

1. **`fs::TmpDir` hands out long-file-name-prefixed paths, and `cmd.exe` will not execute a program named by one.** It resolves a `\\?\` path perfectly well in, say, `if exist`, but answers "The system cannot find the path specified" when asked to run one. Not a library defect - recorded because it cost a debugging cycle and because any future Windows test which spawns a script out of a `TmpDir` will hit it. The argv case strips the prefix explicitly and says why.
