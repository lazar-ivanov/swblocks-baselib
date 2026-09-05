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

- **Closed 2026-09-04**; the trigger fired and the outcome is recorded above. The remaining gap is
  the OpenSSL 1.1.1w side of item 8, which needs a dist that carries `openssl/1.1.1w`.
- A third Windows SDK or a second MSVC toolset appears in a dist: the selection is now "newest by
  lexicographic sort", matching `vs-detector.ps1`, and the chosen versions are printed in the build
  log, so a mismatch should be visible rather than silent.

## References

- Review: `notes/reviews/major/update_2025/v1/pr_review_analysis_fable51.md` (M-19, L-33, L-34,
  L-16, L-17, L-20, I-17)
- Per-round status record: `notes/plans/issues/pr-review-fable51-residual-findings-status.md`
- Deployment-script deferrals: `scripts/devenv7/docs/supply-chain-verification-deferral.md`
