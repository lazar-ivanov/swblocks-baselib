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
**Status:** Deferred to the next Windows build session (a devenv7 Windows host with the `vc143`
and `ccl16` toolchains); every item below is applied and validated there in one pass.

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

## What limits the exposure

- Items 1-3 change nothing about what ships: they concern which warnings the Windows build
  reports (1) and how the toolchain is located (2, 3), and both currently work by accident on the
  single-toolset, single-SDK dists in use.
- Item 4 is pre-existing code, adjacent to the JNI probing rewritten on this branch; the JNI tests
  pass on Windows with `PATH` exported, which the documentation requires.
- Items 5 and 6 are fixed; only the runtime confirmation on the target platform is outstanding.
- Item 7 affects developer convenience files, not the build.

## Conditions to revisit

- The next Windows build session, which is the expected trigger: whoever has the host applies the
  table above in one pass and records the outcome here.
- A second MSVC toolset or Windows SDK appears in a dist (item 3 becomes live).
- A Windows consumer runs the JNI code from a process which does not export `PATH` (item 4).

## References

- Review: `notes/reviews/major/update_2025/v1/pr_review_analysis_fable51.md` (M-19, L-33, L-34,
  L-16, L-17, L-20, I-17)
- Per-round status record: `notes/plans/issues/pr-review-fable51-residual-findings-status.md`
- Deployment-script deferrals: `scripts/devenv7/docs/supply-chain-verification-deferral.md`
