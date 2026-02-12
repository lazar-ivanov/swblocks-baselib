# Boost clang-win.jam i686 Patch: Root Cause Analysis

This document contains the detailed root cause analysis and technical explanation of the OpenWith.exe dialog issue when building Boost with the ccl16 (clang-cl) toolchain. For operational guidance (what commands to run), see the parent [CLAUDE.md](../CLAUDE.md).

---

## Issue Description

When building Boost 1.90.0 with the ccl16 toolchain (clang-cl compiler), Windows displays OpenWith.exe dialogs asking how to open `.cpp` files. The dialog appears twice per build variant (4 times total for debug and release builds).

**Symptoms:**
- OpenWith.exe dialog shows during Boost configuration checks
- Dialog displays ".cpp" with no path information
- Build completes successfully only after manually closing dialogs
- No compilation errors, but configuration checks fail silently

**Impact:**
- Automated builds hang indefinitely waiting for user interaction
- CI/CD pipelines cannot complete without manual intervention

## Root Cause Analysis

**Environment context:**
- **Host architecture:** x86 32-bit (i686)
- **Target architecture:** ARM64 (aarch64) — cross-compilation scenario
- **Compiler:** clang-cl.exe from MSVC Build Tools (ccl16 toolchain)

**The bug sequence:**

1. **clang-cl reports target triple as "i686-pc-windows-msvc"** — When queried with `clang-cl -v`, the compiler identifies itself as targeting i686 (x86 32-bit). This is correct for the host architecture.

2. **Boost's clang-win.jam only recognizes specific target triples** — File: `tools/build/src/tools/clang-win.jam` (lines 92-100). Switch statement checks for: `x86_64`, `i386`, `aarch64`, `arm`. **Missing:** `i686` case (even though i686 and i386 are both x86 32-bit).

3. **Mismatch causes default detection to fail** — When "i686" doesn't match any case, `default-arch` and `default-addr` remain unset. Boost.Build cannot determine correct architecture/address-model combination.

4. **Fallback to incorrect architecture** — Without proper detection, Boost falls back to trying arm/32 (32-bit ARM) configuration. This is wrong — we're cross-compiling from x86/32 (host) to ARM64/64 (target).

5. **arm/32 toolchain initialization fails** — Boost tries to initialize arm/32 tools but gets generic tool names without full paths. Returns "link.exe" instead of full path to linker, "ml.exe" instead of full path to assembler.

6. **Configuration checks attempt to compile with broken paths** — Boost runs configuration tests to check compiler features. Tests try to compile small .cpp programs using arm/32 properties. Without proper compiler paths, compilation fails.

7. **b2 tries to execute .cpp files directly** — When compilation fails, b2 attempts to run the .cpp file as an executable. This is a fallback behavior when the build system can't figure out how to handle a file. Windows doesn't know how to execute a .cpp file.

8. **OpenWith.exe dialog appears** — Windows shows "How do you want to open this .cpp file?" dialog. User must manually close the dialog for each failed configuration check. Dialog appears twice per variant (debug and release), 4 times total.

**Why the build completes successfully:** The configuration checks are optional feature detection tests. After checks complete (or fail), the actual build uses the correctly specified `toolset=clang-win` parameter. The main build works because the clang-win toolset is properly initialized via command-line options.

## The Fix: Patching clang-win.jam

**Solution:** Add "i686" case to Boost's target triple detection in clang-win.jam.

**Location:** `tools/build/src/tools/clang-win.jam` lines 92-100

**Before patch:**
```jam
local default-addr ;
local default-arch ;
switch $(target) {
case x86_64  : default-arch = x86 ; default-addr = 64 ;
case i386    : default-arch = x86 ; default-addr = 32 ;
case aarch64 : default-arch = arm ; default-addr = 64 ;
case arm     : default-arch = arm ; default-addr = 32 ;
}
```

**After patch:**
```jam
local default-addr ;
local default-arch ;
switch $(target) {
case x86_64  : default-arch = x86 ; default-addr = 64 ;
case i686    : default-arch = x86 ; default-addr = 32 ;
case i386    : default-arch = x86 ; default-addr = 32 ;
case aarch64 : default-arch = arm ; default-addr = 64 ;
case arm     : default-arch = arm ; default-addr = 32 ;
}
```

**What the patch does:**
- Adds explicit recognition of "i686" target triple
- Maps i686 → x86/32 (same as i386)
- Ensures proper default architecture and address-model detection
- Configuration checks now use x86/32 properties (which ARE properly initialized)
- No more fallback to broken arm/32 toolchain
- No more OpenWith.exe dialogs

## Implementation in build-boost-windows.bat

**Script location:** `scripts/devenv7/windows/build-boost-windows.bat` lines 396-467

**Patch timing:** After bootstrap completes, before running b2 build command

**Patch method:** PowerShell string replacement with proper newline handling

**Key implementation details:**

1. **Applied only for ccl16 toolchain:**
   ```batch
   if /i "%TOOLCHAIN_NAME%"=="ccl16" (
       REM Apply patch
   )
   ```

2. **PowerShell patch command:**
   ```batch
   powershell -NoProfile -Command "$content = Get-Content 'tools\build\src\tools\clang-win.jam' -Raw; $newline = [Environment]::NewLine; $replacement = \"case i686    : default-arch = x86 ; default-addr = 32 ;$newline    case i386    : default-arch = x86 ; default-addr = 32 ;\"; $content = $content -replace 'case i386    : default-arch = x86 ; default-addr = 32 ;', $replacement; Set-Content 'tools\build\src\tools\clang-win.jam' -Value $content -NoNewline"
   ```

3. **Uses `[Environment]::NewLine`** for proper CRLF line breaks on Windows, maintaining proper indentation.

4. **Validates patch success** with `if errorlevel 1` check.

**Temporary nature:** Patch is applied to extracted Boost source in temporary build directory. Does not modify the original distribution archive. Each build gets a fresh extraction and fresh patch.

## Target Triple Naming

- Both "i686" and "i386" refer to Intel x86 32-bit architecture
- "i686" is the Pentium Pro (P6) microarchitecture and later (introduced 1995)
- "i386" is the Intel 80386 (introduced 1985)
- Modern compilers typically report "i686" for generic x86 32-bit
- "i386" is retained for maximum compatibility

Boost.Build's clang-win.jam only checks for "i386". The missing "i686" case causes a cascading failure in architecture detection when the host is x86 32-bit.

## Verification and Testing

**Test the build:**
```batch
cd C:\Users\lazar\dev\github\swblocks-baselib
.\scripts\devenv7\windows\build-boost-windows.bat -arch a64 -toolchain-name ccl16 -dist-root C:\Users\lazar\swblocks\dist-devenv7-windows-hostarch-x86-targets-a64-x64-x86
```

**Verify patch was applied:**
```batch
cd C:\Users\lazar\swblocks\bld\boost\1.90.0\win-a64-ccl16-debug
type tools\build\src\tools\clang-win.jam | findstr /C:"i686"
```

**Debugging with -no-cleanup:**
```batch
.\scripts\devenv7\windows\build-boost-windows.bat -arch a64 -toolchain-name ccl16 -no-cleanup
```

Preserves build artifacts for inspection:
- Build directory: `%USERPROFILE%\swblocks\bld\boost\1.90.0\win-a64-ccl16-debug`
- Patched clang-win.jam: `tools\build\src\tools\clang-win.jam`
- Build logs: `log_boost_build.log` and `log_boost_test.log`

## Rejected Alternatives

1. **Modify bootstrap.bat to recognize clang-win** — Rejected: requires patching Boost's bootstrap.bat, fragile across versions, more invasive.

2. **Use project-config.jam to configure clang-win** — Rejected: does not fix target triple detection, configuration checks still fail.

3. **Pass explicit compiler paths to b2** — Rejected: more complex, does not fix configuration check failures, does not address root cause.

**Why the selected patch approach:** Minimal and targeted (adds one line), correct root cause fix, non-invasive (temporary build directory), maintainable, effective.

## Impact on Other Toolchains

- **No impact on vc143 (MSVC):** Uses different code paths in Boost.Build, does not use clang-win.jam.
- **No impact on other architectures:** x64 and x86 native builds use x86_64 or i386 target triples, both already recognized.
- **Only affects ccl16 cross-compilation from x86 32-bit host** (i686 target triple).

---

**Related files:**
- Build script: `scripts/devenv7/windows/build-boost-windows.bat` (patch at lines 396-467)
