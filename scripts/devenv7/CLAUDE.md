# devenv7 Build System Guidelines

## Rules for Modifying This File

- **Keep under 1,000 lines.** If it grows beyond, condense or move content.
- **Rules and operational guidance only.** No root cause analyses or implementation walkthroughs — those go in `docs/`.
- **State each rule once.** Reference root `CLAUDE.md` for shared rules (batch escaping, delayed expansion).
- **Code snippets: 10 lines max.** Reference implementation files for longer examples.
- **Use the Edit tool** (never Write) when modifying this file.
- **Every addition must justify its presence.** When adding content, consider removing something.

---

## Table of Contents

1. [Windows Batch Script Rules](#windows-batch-script-rules)
2. [Architecture Tags](#architecture-tags)
3. [Directory Structure](#directory-structure)
4. [Build System](#build-system)
5. [devenv Version Gating Pattern](#devenv-version-gating-pattern)
6. [Windows JNI Support (devenv7+)](#windows-jni-support-devenv7)
7. [Common Pitfalls](#common-pitfalls)
8. [Archive Distribution Script](#archive-distribution-script)
9. [Tool Versions and Compatibility](#tool-versions-and-compatibility)

---

## Windows Batch Script Rules

See root `CLAUDE.md` for the full rules on special character escaping, delayed expansion (`!VAR!` inside control structures), line continuation in set commands, and the decision matrix.

**File-specific delayed expansion guidance:**

- **build-env-all-windows.bat:** ALL variables inside `if not "!SKIP_*!"=="1"` blocks MUST use `!VAR!`
- **build-msvc-toolchain.bat:** ALL variables inside normalization loop MUST use `!VAR!`
- **build-boost-windows.bat, build-openssl-windows.bat:** ALL variables inside architecture loops and nested `if` blocks MUST use `!VAR!`

---

## Architecture Tags

### Standard Tags

The project uses the following **canonical architecture tags**:

- `a64` - ARM64/AArch64 (canonical)
- `x64` - x86-64/AMD64 (canonical)
- `x86` - x86-32 (canonical)

### Architecture Aliases

For backward compatibility, the following aliases are accepted and automatically normalized:

- `arm64` → `a64` (alias)
- `amd64` → `x64` (alias)

### Visual Studio Naming vs Project Tags

**Important:** Visual Studio uses different folder naming conventions:

| Project Tag | VS Folder Name | Usage |
|-------------|----------------|-------|
| `a64` | `arm64` | VS uses `arm64` in paths like `VC\Tools\MSVC\*\bin\Hostarm64\arm64` |
| `x64` | `x64` | Same in both |
| `x86` | `x86` | Same in both |

**Rule:** When constructing Visual Studio paths or looking up file patterns, convert `a64` → `arm64` using the `ConvertTo-VSArchitectureName` function in PowerShell scripts.

---

## Directory Structure

### Distribution Folder Naming

Distribution folder naming is platform-specific. The architecture suffix uses the canonical tags (`a64`, `x64`, `x86`) — never hardcoded values like `-arm`.

**Windows:**
```
dist-devenv7-windows-hostarch-{host}-targets-{target1}-{target2}-{target3}
```
Examples: `dist-devenv7-windows-hostarch-a64-targets-a64-x64-x86`, `dist-devenv7-windows-hostarch-x64-targets-x64`

**Linux:**
```
dist-{DEVENV_TAG}-{OS_TAG}-{DIST_TAG}-{ARCH_TAG}
```
Architecture detected via `uname -m`: `aarch64`/`arm64` → `a64`, `x86_64` → `x64`, `i386`/`i686` → `x86`.
Examples: `dist-devenv7-ub24-gcc1520-a64`, `dist-devenv7-rhel9-gcc1520-x64`

**macOS:**
```
dist-{DEVENV_TAG}-darwin-{OS_NUMBER}-{ARCH_TAG}
```
Architecture detected via `uname -m`: `arm64` → `a64`, `x86_64` → `x64`.
Examples: `dist-devenv7-darwin-25-a64`, `dist-devenv7-darwin-25-x64`

### Build Directory Location

**CRITICAL:** Build directories MUST be created in the swblocks folder **parallel to the dist folder**, NOT in the repository.

**❌ WRONG:**
```batch
set "BUILD_ROOT=%REPO_ROOT%\bld\..."
```

**✅ CORRECT:**
```batch
REM Determine swblocks build root (parallel to dist folder)
for %%I in ("%DIST_ROOT_DEPS1%") do set "SWBLOCKS_ROOT=%%~dpI"
set "SWBLOCKS_ROOT=%SWBLOCKS_ROOT:~0,-1%"
set "BLD_ROOT=%SWBLOCKS_ROOT%\bld"
```

### Expected Directory Layout

```
%USERPROFILE%\swblocks\
├── dist-devenv7-windows-hostarch-a64-targets-a64-x64-x86\
│   ├── boost\
│   ├── openssl\
│   ├── toolchain-msvc\
│   ├── winsdk\
│   └── ...
├── bld\
│   ├── boost\
│   │   └── 1.90.0\
│   │       ├── win-a64-vc143-debug\
│   │       └── win-a64-vc143-release\
│   └── openssl\
│       └── 3.5.4\
│           ├── win-a64-vc143-debug\
│           └── win-a64-vc143-release\
└── logs\
```

---

## Build System

### Assembler Configuration

Different architectures use different assemblers:

| Architecture | Assembler | Tool to Check | Configuration |
|--------------|-----------|---------------|---------------|
| a64 (ARM64) | clang-cl | `where clang-cl` | OpenSSL: `VC-WIN64-CLANGASM-ARM` |
| x64 | NASM | `where nasm` | OpenSSL: `VC-WIN64A` |
| x86 | NASM | `where nasm` | OpenSSL: `VC-WIN32` |

**Important Notes:**
- For ARM64, clang-cl is used **ONLY for assembler**, MSVC cl.exe is still used for C/C++ compilation
- NASM must be in PATH for x64 and x86 builds
- Clang-CL must be in PATH for a64 builds

### Environment Setup Scripts

Three environment setup scripts are **always generated** regardless of target architectures:
- `setup-env-a64.bat`
- `setup-env-x64.bat`
- `setup-env-x86.bat`

### Environment Setup Script Variants

Three variants are generated for each architecture (9 scripts total, all architectures regardless of `-targets`). Location: `{dist-root}\scripts\ci\`.

- **`setup-env-{arch}.bat`** — Full environment: MSVC compiler, SDK, Clang-CL, debuggers, Jom, NASM, Git, Python, MSYS2, INCLUDE/LIB/LIBPATH.
- **`setup-env-nomsvc-{arch}.bat`** — No MSVC: debuggers, Jom, NASM (x64/x86), Git, Python, MSYS2. No compiler, SDK, or INCLUDE/LIB/LIBPATH.
- **`setup-env-minimal-{arch}.bat`** — Minimal: Git and MSYS2 only.

| Component | Full | No MSVC | Minimal |
|-----------|------|---------|---------|
| MSVC/SDK/Clang-CL | Yes | No | No |
| Debuggers/Jom | Yes | Yes | No |
| NASM | Yes (x64/x86) | Yes (x64/x86) | No |
| Git/MSYS2 | Yes | Yes | Yes |
| Python | Yes | Yes | No |
| INCLUDE/LIB/LIBPATH | Yes | No | No |

---

### PATH Order

The PATH order is critical to avoid conflicts:

```
MSVC → Clang-CL → Windows SDK → Debuggers → %PATH% → Jom → NASM (x64/x86 only) → Git → Python → MSYS2
```

**Why:** MSYS2 contains `link.exe` and `find.exe` that conflict with Windows native tools, so it must be last.

### Cross-Compilation and ARCH Parameter (devenv7+)

Windows builds support cross-compilation using the `ARCH` parameter:

| Architecture | ARCH Value | Status |
|--------------|------------|--------|
| ARM64        | `a64`      | Supported |
| x64          | `x64`      | Supported |
| x86          | `x86`      | Limited (no JDK 25) |

```bash
# Default: builds for detected host architecture
make -k -j4

# Cross-compile for x64 from ARM64 host
make -k -j4 ARCH=x64
```

**Host vs Target Architecture:**
- **Host architecture:** Auto-detected, determines which compiler binaries to use (e.g., `Hostarm64`)
- **Target architecture:** Controlled by `ARCH` parameter, determines output binary architecture
- `BL_WIN_ARCH_IS_ARM64` / `BL_WIN_ARCH_IS_X64` flags represent HOST architecture
- `ARCH` variable represents TARGET architecture

**Host Architecture Detection Algorithm:**

1. If `PROCESSOR_IDENTIFIER` contains "ARMv8" or "AArch64" → ARM64 host (works in all environments including MSYS2)
2. Else if `PROCESSOR_ARCHITECTURE=ARM64` → ARM64 host (native ARM64 process)
3. Else if `PROCESSOR_ARCHITEW6432=ARM64` → ARM64 host (emulated x86 process)
4. Else if `PROCESSOR_ARCHITEW6432=AMD64` → x64 host (x86 WOW64)
5. Else if `PROCESSOR_ARCHITECTURE=AMD64` → x64 host (native x64)
6. Else → x86 host

**Note:** `PROCESSOR_IDENTIFIER` is checked first because MSYS2 x64 binaries (including make.exe) running on ARM64 don't set `PROCESSOR_ARCHITEW6432`, but `PROCESSOR_IDENTIFIER` reliably contains "ARMv8" or "AArch64".

**Key Design Principles:**

1. **Environment variables are ignored:** `LIB`, `LIBPATH`, and `INCLUDE` from environment (setup-env scripts) are completely ignored. Makefiles construct paths from scratch based on ARCH. Implemented in `projects/make/common.mk` (lines 145-146).
2. **Command-line override priority:** Detection uses `ARCH ?=` (conditional assignment) so command-line `ARCH=x64` takes precedence. Implemented in `projects/make/platform.mk`.
3. **Lazy evaluation for ARCH mapping:** `ARCH_LIBPATH` uses `=` (not `:=`) to evaluate when used, not when assigned, reflecting current ARCH value. Implemented in `projects/make/toolchain/msvc-default.mk`.

**Common ARCH Pitfalls:**

| Problem | Symptom | Solution |
|---------|---------|----------|
| Env contaminating paths | Mixed arm64/x64 paths in LIBPATH | Ensure `LIBPATH :=` in common.mk |
| ARCH parameter ignored | `make ARCH=x64` still builds for a64 | Use `ARCH ?=` in platform.mk |
| ARCH_LIBPATH not updating | ARCH=x64 but paths still use arm64 | Use `=` (lazy eval) not `:=` |
| Duplicate paths | Both arm64 and x64 in LIBPATH | Init LIBPATH to empty; use lazy eval |

**Setup scripts are optional.** Makefiles configure all compiler paths internally. Setup scripts only needed for interactive tool use or adding MSYS2 to PATH.

### OpenSSL Build Configuration Strategy

#### Unified CRT and Optimization Strategy

Both debug and release use `--release` flag, `-DNDEBUG`, and release CRT (`/MD`). Only difference: debug uses `-Od -Ob0`, release uses `-O2 -Ob1 -Ot -Oi`. This simplifies deployment (no debug CRT dependencies).

#### Debug Information Strategy

**Critical rule:** Do NOT pass debug flags (`-Zi`, `-Z7`, `-Zo`) to the Configure command. OpenSSL's Configure detects debug flags and disables the assembler, severely impacting performance. Add debug flags AFTER Configure via makefile patching.

**x64/x86 (PowerShell):** Replace `/Zi` with `/Z7`, then append `/Z7 /Zo` to `LIB_CFLAGS`. Use `/Z7` (not `/Zi`) to avoid PDB conflicts with jom parallel builds. Remove PDB install commands since `/Z7` embeds debug info in `.obj` files (no `ossl_static.pdb` generated).

**ARM64 (Perl):** Redirect assembly preprocessor from `$(CC) /EP` to `clang-cl.exe /EP` (MSVC cl.exe cannot preprocess GNU-style ARM assembly), add `/Z7 /Zo` to CFLAGS, and replace release optimization flags with debug flags for debug builds.

#### ARM64 Build — Key Requirements

ARM64 uses a hybrid approach: MSVC cl.exe for C compilation + clang-cl for assembly preprocessing. Three additional requirements:

1. **Assembly activation macros** — OpenSSL builds assembly objects but the C "glue" code doesn't call them without explicit macros (`-DOPENSSL_CPUID_OBJ -DOPENSSL_BN_ASM_MONT` etc.)
2. **ARM architecture macros** — `-D__ARM_ARCH__=8 -D__ARM_MAX_ARCH__=8` (MSVC doesn't define these). Without them, compile-time guards exclude ARMv8 fast paths (~200 MB/s vs multi-GB/s)
3. **Debug optimization workaround** — Pass release flags to Configure (keeps assembly enabled), then patch makefile to restore debug flags

**Deep dive:** See [docs/openssl-arm64-build.md](docs/openssl-arm64-build.md) for full technical analysis.

#### Testing

```batch
REM OpenSSL build config tests
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\devenv7\windows\test-openssl-build-config.ps1

REM Environment setup and PATH validation
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\devenv7\windows\test-setup-env-paths.ps1
```

#### Build Verification

Verification runs automatically after build (before copying to dist). Checks: hardware acceleration (>1 GB/s), correct optimization flags, debug info flags (`/Z7 /Zo`). Skip with `-skip-verification`.

```batch
REM Test verification logic independently
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\devenv7\windows\test-openssl-verification.ps1
```

---

### Boost Build Configuration

#### clang-win.jam i686 Patch (ccl16 toolchain)

**Problem:** Building Boost with ccl16 (clang-cl) on x86 32-bit host causes OpenWith.exe dialogs — builds hang waiting for user interaction.

**Root cause:** Boost's `clang-win.jam` recognizes `i386` but not `i686` in its target triple detection. clang-cl on x86 32-bit reports `i686`, causing architecture detection to fail and fall back to broken arm/32 toolchain configuration.

**Fix:** The build script patches `clang-win.jam` after bootstrap (ccl16 only), adding an `i686` case mapping to x86/32. Patch is applied to extracted source in temporary build directory — does not modify the original archive.

**Verify:** `type tools\build\src\tools\clang-win.jam | findstr /C:"i686"` in the build directory.

**Impact:** Only affects ccl16 cross-compilation from x86 32-bit host. No impact on vc143 or other architectures.

**Deep dive:** See [docs/boost-clang-win-patch.md](docs/boost-clang-win-patch.md) for full root cause analysis, before/after patch code, implementation details, and rejected alternatives.

---

### OpenSSL Cross-Compilation Test Skipping

The `build-openssl-windows.bat` script requires `-hostarch` to distinguish host architecture (where tools run) from target architecture (what binaries are built for). This drives test execution decisions — tests are skipped when the host processor cannot execute target binaries.

#### ARM64 Perl Compatibility Fix

OpenSSL tests hang on ARM64 Windows because Strawberry Perl (x86/x64 only) running under ARM64 emulation deadlocks in `fork()` emulation when modern process creation optimizations interact with the ARM64 translation layer.

**Fix:** The script auto-detects ARM64 processor hardware and applies Win8RTM compatibility mode via registry (`HKCU\...\AppCompatFlags\Layers`), disabling the problematic optimizations. If registry commands fail, the build aborts — tests will hang without the fix.

**Verify:** `reg query "HKCU\Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers" | findstr perl`

**Remove:** `reg delete "HKCU\...\AppCompatFlags\Layers" /v "C:\path\to\perl.exe" /f`

#### Test Execution Matrix

Test execution depends on **processor architecture** (not hostarch parameter):

| Processor | Target Arch | Tests Run? | Reason |
|-----------|-------------|------------|--------|
| ARM64 | x86 | ✅ Yes | Win8 compat mode applied, x86 via WoW64 |
| ARM64 | x64 | ✅ Yes | Win8 compat mode applied, x64 via emulation |
| ARM64 | a64 | ✅ Yes | Win8 compat mode applied, native ARM64 |
| x64 | x86 | ✅ Yes | x86 via WoW64 |
| x64 | x64 | ✅ Yes | Native execution |
| x64 | a64 | ❌ Skip | x64 cannot execute ARM64 binaries |
| x86 | x86 | ✅ Yes | Native execution |
| x86 | x64 | ❌ Skip | x86 cannot execute x64 binaries |
| x86 | a64 | ❌ Skip | x86 cannot execute ARM64 binaries |

**Key Points:**
- **ARM64 processor:** All tests run (Win8 compat mode prevents Perl deadlock)
- **x64 processor:** Skip only a64 targets
- **x86 processor:** Skip x64 and a64 targets
- Test skipping based on **processor capability**, not hostarch value
- Strawberry Perl architecture varies by dist folder (x86 or x64), always runs under emulation on ARM64

---

## devenv Version Gating Pattern

When implementing features for **devenv7+ and all future versions**, use the **negative filtering pattern**. This explicitly lists old devenv versions that should NOT have the feature, so new versions are automatically supported.

```makefile
# Use negative filtering: devenv7+ by default, devenv2-6 explicitly handled
ifneq ($(filter devenv2 devenv3 devenv4 devenv5 devenv6,$(DEVENV_VERSION_TAG)),)
  # Old devenv behavior (devenv2-6)
else
  # New devenv behavior (devenv7, devenv8, devenv9, ...)
  # No code changes needed when devenv8+ are added
endif
```

**Why negative filtering (not positive checks):** A positive check like `ifeq ($(DEVENV_VERSION_TAG),devenv7)` would require code changes when devenv8 is added. Negative filtering makes new versions automatically get the new behavior.

**Current usage in codebase:**
- Windows JNI support: `projects/make/utests/utf_baselib_jni/Makefile`
- Gradle path configuration: `projects/make/3rd/gradle/latest.mk`
- Boost.JSON support: `projects/make/3rd/boost/common.mk`

**Best practices:**
- Always use same filter list order: `devenv2 devenv3 devenv4 devenv5 devenv6`
- Always use `ifneq` (not `ifeq`) with `$(filter ...)`
- Comment intent: `# Use negative filtering: devenv7+ by default, devenv2-6 explicitly handled`

---

## Windows JNI Support (devenv7+)

Starting with devenv7, Windows builds of `utf_baselib_jni` are fully supported alongside Linux and macOS.

**Version gating:** Uses negative filtering pattern (see above). `BL_WIN_JNI_DISABLED` flag set for devenv2-6 on Windows only. Windows enabled by default for devenv7+.

**Signal handling differences:**
- **Unix/Linux/macOS:** POSIX signal handling requires `libjsig` library for signal chaining. Must link against `libjsig.so` (Linux) or `libjsig.dylib` (macOS) with rpath configured.
- **Windows:** Uses Structured Exception Handling (SEH). No `libjsig` linking needed — signal/exception chaining works through SEH automatically.

**JDK directory structure (devenv7):**
- **Windows:** `${DIST_ROOT_DEPS3}/openjdk/25/{arch}/bin/server/jvm.dll` (arch-specific: `a64`, `x64`)
- **Linux:** `${DIST_ROOT_DEPS3}/openjdk/25/default/lib/server/libjvm.so`
- **macOS:** `${DIST_ROOT_DEPS3}/openjdk/25/default/lib/server/libjvm.dylib`
- x86 is not supported (no JDK 25 available for this architecture)

**JVM runtime loading:** The JVM is loaded dynamically via `LoadLibrary()` (Windows) or `dlopen()` (Unix). Code in `src/include/baselib/jni/JavaVirtualMachine.h` checks multiple path candidates for backward compatibility across JDK 8, 9-24, and 25+.

**JDK configuration:** `projects/make/3rd/jdk/common.mk` auto-detects JDK and sets `BL_JNI_ENABLED`, `JAVA_HOME`, and include paths.

| Architecture | JDK Path (devenv7) | JNI Support |
|--------------|---------------------|-------------|
| ARM64 (`a64`) | `openjdk/25/a64` | Supported |
| x64           | `openjdk/25/x64` | Supported |
| x86           | N/A                | Not available |

### JNI Lessons Learned (All Resolved)

1. **PATH must be exported:** Without `export PATH` in makefiles, test executables fail with error code 200 because `LoadLibrary()` can't find `jvm.dll`. Fixed in `projects/make/3rd/jdk/common.mk` and `projects/make/toolchain/msvc-default.mk`.

2. **Path normalization on Windows:** `JAVA_HOME` from MSYS/Cygwin contains forward slashes, but Boost.Filesystem appends with backslashes, creating mixed separators that break `fs::exists()`. Fixed by normalizing `JAVA_HOME` with `fs::normalize()` before constructing paths. See `JavaVirtualMachine.h:195-197`.

3. **JNI not enabled:** Verify JDK exists at expected path (`openjdk/25/{arch}`), check `DEVENV_VERSION_TAG` is `devenv7`, and ensure architecture is not in the disabled filter list.

4. **JVM exception warnings:** Warnings like "JNI call made without checking exceptions" are expected during exception handling tests when running with `-Xcheck:jni`. No action needed.

5. **Wrong JVM architecture:** Each target architecture needs its own JDK installation. ARM64 builds require ARM64 JDK, x64 builds require x64 JDK.

---

## Common Pitfalls

### 1. Batch Script Echo Statements

**Symptom:** Error message: `: was unexpected at this time`

**Cause:** Unescaped special characters in echo statements inside conditional blocks

**Solution:** Escape parentheses, colons, and other special characters with `^`

### 2. Architecture Tag Confusion

**Symptom:** Script looks for `arm64` folder but path uses `a64`, or vice versa

**Cause:** Mixing project tags with Visual Studio naming conventions

**Solution:**
- Use `a64` in all script logic and user-facing messages
- Convert to `arm64` only when constructing Visual Studio paths
- Use `ConvertTo-VSArchitectureName` function in PowerShell

**Specific case - Windows SDK Debuggers path:**
- **Incorrect**: `%WINSDK_ROOT%\Debuggers\a64` (using normalized tag)
- **Correct**: `%WINSDK_ROOT%\Debuggers\arm64` (using VS naming)
- **Solution**: Always use `ConvertTo-VSArchitectureName` before constructing debugger paths
- **Test**: Run `test-setup-env-paths.ps1` to detect this issue

### 3. Build Directory in Repository

**Symptom:** Build artifacts appearing in `<repo>\bld\` directory

**Cause:** Using `%REPO_ROOT%\bld` instead of swblocks folder

**Solution:** Always calculate `BLD_ROOT` from `DIST_ROOT_DEPS1` parent directory

### 4. Comma-Separated Arguments Without Quotes

**Symptom:** `Unknown option: x64` when passing `-targets a64,x64,x86`

**Cause:** Windows batch treats commas as argument separators, splitting into three separate arguments.

**Solution:** Multi-value scripts (`build-env-all-windows.bat`) use an argument collection loop — see implementation in that script. Single-arch scripts (`build-boost-windows.bat`, `build-openssl-windows.bat`) only accept ONE architecture.

### 5. Label Scoping

**Symptom:** `The system cannot find the batch label specified`

**Cause:** Label placed inside an `if` block. Labels MUST be at column 1, outside any blocks.

### 6. JSON Spirit Directory Structure

**Symptom:** Double-nested directories like `json-spirit\4.08\source\4.08\source`

**Cause:** The tar.gz archive contains `json-spirit/4.08/source/` structure

**Solution:** Extract to temp, then move `json-spirit/{version}/source/` to final location

### 7. Makefile Patching Regex Breaks Syntax

**Symptom:** `Error: syntax error in Makefile` — flags appear on separate line instead of appending.

**Cause:** Regex `(LIB_CFLAGS=.*)` matches to end of line including newline. Use `[^\r\n]+` instead: `(LIB_CFLAGS=[^\r\n]+)`. Also: order matters — convert `/Zi` to `/Z7` first, then append `/Z7 /Zo`.

---

## Debugging Build Issues

### Preserving Build Artifacts

When debugging build failures, use the `-no-cleanup` parameter to preserve intermediate build files:

```batch
build-openssl-windows.bat -arch a64 -no-cleanup
build-boost-windows.bat -arch a64 -no-cleanup
```

**What this does:**
- Skips deletion of build directories after completion
- Preserves all intermediate files, makefiles, and logs
- Allows inspection of generated configurations

**Build artifacts location:**
```
%USERPROFILE%\swblocks\bld\
├── boost\
│   └── 1.90.0\
│       ├── win-a64-vc143-debug\
│       └── win-a64-vc143-release\
└── openssl\
    └── 3.5.4\
        ├── win-a64-vc143-debug\
        └── win-a64-vc143-release\
```

**Useful files for debugging:**
- `makefile` - Generated makefile (inspect CFLAGS, ASFLAGS, etc.)
- `log_bootstrap.log` - Configure command output
- `log_openssl_build.log` or `log_boost_build.log` - Build output
- `log_openssl_test.log` or `log_boost_test.log` - Test output

---

## Script Usage Guidelines

**Multi-architecture:** Use `build-env-all-windows.bat` — accepts comma-separated or space-separated targets (`-targets a64,x64,x86` or `-targets a64 x64 x86`).

**Single-architecture:** `build-boost-windows.bat` and `build-openssl-windows.bat` build ONE architecture at a time (`-arch a64`). Passing multiple architectures causes "Unknown option" errors. To build multiple architectures, use `build-env-all-windows.bat` instead.

---

## Archive Distribution Script

The `archive-dists-windows.bat` script creates compressed archives of the distribution folder and downloads cache.

**Usage:**
```batch
archive-dists-windows.bat -dist-root <folder-name> [-delete-target-if-exists]
```

**Creates two archives** (names match folder names exactly, no timestamps):

| Archive | Content |
|---------|---------|
| `{dist-root}.zip` | Complete distribution (toolchains, libraries, tools) |
| `{dist-root}-downloads-cache.zip` | Downloaded source archives (offline rebuilds) |

**Path derivation** (all auto-calculated from `-dist-root`):

| Path | Location |
|------|----------|
| Dist folder | `%USERPROFILE%\swblocks\{dist-root}` |
| Cache folder | `%USERPROFILE%\swblocks\{dist-root}-downloads-cache` |
| Output | `%USERPROFILE%\swblocks\zip` |
| 7-zip | Auto-detected from `{dist-root}\7zip\*\7za.exe` |

**Compression:** `-mx=5` (normal) — 2-3x faster than maximum with ~60-70% size reduction.

**Archives preserve relative paths** from `%USERPROFILE%\swblocks`. Extract with:
```batch
cd %USERPROFILE%\swblocks
7za.exe x zip\{archive-name}.zip
```

**Validation:** Checks dist/cache folders exist, 7-zip available, archives don't already exist (use `-delete-target-if-exists` to overwrite).

---

## Testing Checklist

Before committing changes to batch scripts:

- [ ] Test with all architecture tags: `a64`, `x64`, `x86`
- [ ] Test with architecture aliases: `arm64`, `amd64`
- [ ] Test with comma-separated arguments: `-targets a64,x64,x86`
- [ ] Test with space-separated arguments: `-targets a64 x64 x86`
- [ ] Verify no `bld` folder created in repository
- [ ] Verify all echo statements with special characters are escaped
- [ ] Verify PATH order in generated setup-env scripts

---

## Version History

- **2026-02-13**: Added Linux and macOS dist folder naming conventions to Directory Structure section
- **2026-02-11**: Restructured — condensed from ~1,800 lines, moved deep technical content to `docs/`
- **2026-02-04**: Added Boost clang-win.jam patch documentation (ccl16 toolchain)
- **2026-01-18**: Added OpenSSL build verification, archive distribution script
- **2026-01-16**: Added OpenSSL ARM64 build configuration and assembly activation
- **2026-01-15**: Initial version

---

## Tool Versions and Compatibility

### Git

**Version:** 2.48.1 — last version with x86 32-bit portable installer. Versions 2.49.0+ dropped x86 builds. Can upgrade if x86 support is dropped.

### OpenJDK

**Version:** OpenJDK 25 (Microsoft Build). Installed **per target architecture** (not host-only).

| Architecture | Supported | Install path |
|--------------|-----------|-------------|
| a64 (ARM64) | Yes | `openjdk/25/a64` |
| x64 | Yes | `openjdk/25/x64` |
| x86 | No | All major vendors dropped x86 32-bit JDK builds |

**Why per-architecture:** Unlike host-only tools (Git, Python), JDK must match the target because build tools may invoke Java code that needs native execution on the target architecture.

**Naming:** `openjdk/{version}/{arch}` (no `default` prefix — reflects per-architecture installation).

### NASM and Strawberry Perl

**Host-only tools** installed to a single `default` folder with host-matching binary.

| Host | NASM binary | Perl binary |
|------|-------------|-------------|
| x86 | win32 (x86) | 32bit-portable |
| x64 | win64 (x64) | 64bit-portable |
| ARM64 | win64 (x64, emulated) | 64bit-portable (emulated) |

**Perl version:** 5.32.1.1 — downgraded from 5.40.x because 5.32.1.1 is the last release with x86 32-bit builds. Ensures x86 hosts can run Perl for OpenSSL configuration.

---

## References

- Architecture normalization: `scripts/devenv7/windows/internal/common.ps1`
- VS architecture conversion: `ConvertTo-VSArchitectureName` function
- Windows build scripts: `scripts/devenv7/windows/build-*.bat`
- Linux build scripts: `scripts/devenv7/linux/build-*.sh`, `scripts/devenv7/linux/install-*.sh`
- macOS build scripts: `scripts/devenv7/macos/build-*.sh`, `scripts/devenv7/macos/install-*.sh`
- Toolchain setup: `scripts/devenv7/windows/internal/toolchain-setup.ps1`
- Archive distribution: `scripts/devenv7/windows/archive-dists-windows.bat`, `scripts/devenv7/linux/archive-dists-linux.sh`, `scripts/devenv7/macos/archive-dists-macos.sh`
- OpenSSL ARM64 deep dive: [docs/openssl-arm64-build.md](docs/openssl-arm64-build.md)
- Boost clang-win.jam patch deep dive: [docs/boost-clang-win-patch.md](docs/boost-clang-win-patch.md)
