# Claude AI Development Guidelines for swblocks-baselib

This document contains project-specific guidelines and best practices for AI-assisted development on the swblocks-baselib project.

## Table of Contents

1. [Windows Batch Script Rules](#windows-batch-script-rules)
2. [Architecture Tags](#architecture-tags)
3. [Directory Structure](#directory-structure)
4. [Build System](#build-system)
5. [Common Pitfalls](#common-pitfalls)

---

## Windows Batch Script Rules

### Critical: Special Character Escaping

**ALWAYS escape special characters in echo statements inside conditional blocks.**

#### Problem Characters

The following characters MUST be escaped with `^` when used in `echo` statements inside `if` blocks or `for` loops:

- `(` → `^(`
- `)` → `^)`
- `<` → `^<`
- `>` → `^>`
- `|` → `^|`
- `&` → `^&`
- `%` → `%%` (use double percent)

#### Examples

**❌ WRONG - Will cause "was unexpected at this time" error:**
```batch
if /i "%ARCH%"=="a64" (
    echo clang-cl found (for assembler):
)
```

**✅ CORRECT:**
```batch
if /i "%ARCH%"=="a64" (
    echo clang-cl found ^(for assembler^):
)
```

**❌ WRONG:**
```batch
for %%A in (a64 x64 x86) do (
    echo Building for architecture: %%A (native)
)
```

**✅ CORRECT:**
```batch
for %%A in (a64 x64 x86) do (
    echo Building for architecture: %%A ^(native^)
)
```

### Batch Script Best Practices

1. **Always use `setlocal enabledelayedexpansion`** at the beginning of scripts that use variables inside loops or conditionals
2. **Quote all paths** that may contain spaces: `"%DIST_ROOT%"`
3. **Use `goto` labels outside `if` blocks** - labels must be at column 1
4. **Avoid special characters in echo** - escape with `^` when necessary
5. **Test batch scripts** after any modifications to echo statements

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

The distribution folder follows this format:

```
dist-devenv7-windows-hostarch-{host}-targets-{target1}-{target2}-{target3}
```

**Examples:**
- `dist-devenv7-windows-hostarch-a64-targets-a64-x64-x86`
- `dist-devenv7-windows-hostarch-x64-targets-x64`

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

### PATH Order

The PATH order is critical to avoid conflicts:

```
MSVC → Clang-CL → Windows SDK → Debuggers → %PATH% → Jom → NASM (x64/x86 only) → Git → Python → MSYS2
```

**Why:** MSYS2 contains `link.exe` and `find.exe` that conflict with Windows native tools, so it must be last.

### OpenSSL Build Configuration Strategy

The OpenSSL build uses a unified configuration approach for both debug and release variants:

#### Unified CRT and Optimization Strategy

**Both debug and release variants:**
- Use `--release` flag (not `--debug`)
- Define `-DNDEBUG`
- Use release CRT (`/MD`, not `/MDd`)

**Only difference between variants:**
- **Debug**: `-Od -Ob0` (no optimizations, no inline expansion)
- **Release**: `-O2 -Ob1 -Ot -Oi` (full optimizations)

**Why this approach:**
1. **Simpler deployment** - Only release CRT required, no debug CRT dependencies
2. **Consistent behavior** - Both variants use same CRT, reducing deployment issues
3. **Better testing** - Debug variant can catch runtime issues without CRT debug checks

#### Debug Information Strategy

**Critical:** Do NOT pass debug flags (`-Zi`, `-Z7`, `-Zo`) to the Configure command.

**Reason:** OpenSSL's Configure script detects debug flags and disables the assembler to avoid historical issues with debug info generation in assembly code. This significantly impacts performance.

**Solution:** Add debug flags AFTER Configure via makefile patching.

**For x64/x86 builds (PowerShell patching):**

```batch
REM Patch makefile to add debug information flags
powershell -NoProfile -ExecutionPolicy Bypass -Command "$content = Get-Content -Raw -Encoding ASCII makefile; $content = $content -replace '/Zi\s+/Fd[^\s]+\s*','/Z7 ' -replace '(LIB_CFLAGS=[^\r\n]+)',('$1 /Z7 /Zo'); Set-Content -Encoding ASCII makefile $content"
```

**For ARM64 builds (Perl patching):**

```batch
REM Patch makefile for ARM64: redirect assembly preprocessor to clang-cl and add debug flags
perl -i.bak -pe "s/\$\(CC\) \/EP -D__ASSEMBLER__/clang-cl.exe \/EP -D__ASSEMBLER__/g; s/^CFLAGS=(.*)/CFLAGS=$1 \/Z7 \/Zo/g" makefile
```

**Why ARM64 requires special handling:**

The Microsoft C Preprocessor (invoked by `cl.exe /EP`) does not understand GNU-style ARM assembly syntax. When it encounters the `#` character (used for comments and directives in ARM assembly), it interprets it as an invalid C preprocessor directive, either stripping the line or mangling the instruction. This breaks ARM64 assembly compilation.

**ARM64 solution components:**

1. **Use MSVC cl.exe for C compilation**: Required for MSVC intrinsics like `_InterlockedAdd64` that OpenSSL relies on. LLVM/Clang ARM64 intrinsics don't fully implement MSVC-compatible aliases.

2. **Add assembly activation macros to Configure command**: Due to quirks in the VC-WIN64-CLANGASM-ARM target configuration, OpenSSL correctly builds assembly objects but fails to tell the C compiler to use them. These macros activate the assembly code paths in the C "glue" code:
   ```batch
   -DOPENSSL_CPUID_OBJ -DOPENSSL_BN_ASM_MONT -DMD5_ASM
   -DVPAES_ASM -DBSAES_ASM -DSHA1_ASM -DSHA256_ASM -DSHA512_ASM
   -DKECCAK1600_ASM -DPOLY1305_ASM -DECP_NISTZ256_ASM
   ```
   **Critical:** Without these macros, the assembly functions are compiled into the library but never called. The C code falls back to slow software implementations. You can verify assembly activation by checking `openssl.exe speed -evp aes-128-gcm` output - the compiler line should show these macros, and `options:` should display `bn(64,64) mont-asm` (not just `bn(64,64)`).

3. **Define ARM architecture macros for compile-time path selection**: MSVC cl.exe does not define `__ARM_ARCH__` or `__ARM_MAX_ARCH__` when compiling C code, unlike Clang/GCC. OpenSSL uses these macros as compile-time guards to enable ARMv8-specific fast paths:
   ```batch
   -D__ARM_ARCH__=8 -D__ARM_MAX_ARCH__=8
   ```

   **Why this is critical:**
   - Files like `aes_platform.h` (line 98) and `gcm128.c` (line 372) check `__ARM_MAX_ARCH__>=8` to enable AES/PMULL intrinsics
   - `cipher_aes_gcm_hw.c` (line 135) only compiles the ARMv8 AES-GCM provider path when these macros are defined
   - Without these macros, even though assembly objects exist and `IsProcessorFeaturePresent(30)` returns true at runtime, the **compile-time guards prevent the fast paths from being compiled into the C code**
   - Result: Code falls back to generic C implementation (~200 MB/s instead of multi-GB/s for AES-GCM)

   **How runtime and compile-time gating interact:**
   - Runtime gating in `armcap.c` (line 32) sets AES/PMULL capability bits based on CPU features
   - But if compile-time guards excluded the fast path code, there's nothing for runtime to call
   - Both conditions must be satisfied: code must be compiled (needs `__ARM_MAX_ARCH__=8`) AND runtime must detect capability

4. **Work around debug optimization flag detection (debug builds only)**: OpenSSL's Configure script detects debug optimization flags (`-Od -Ob0`) and disables assembler to avoid historical issues with debug info generation in assembly code. This causes significant performance degradation.

   **Solution:** Pass release optimization flags (`-O2 -Ob1 -Ot -Oi`) to Configure, then patch the makefile to replace them with debug flags:
   ```batch
   REM Debug build: Pass release optimization flags to Configure to keep assembly enabled
   perl Configure VC-WIN64-CLANGASM-ARM no-shared --release ^
       --prefix=%OPENSSL_ROOT_PATH%\out ^
       --openssldir=%OPENSSL_ROOT_PATH%\out\ssl ^
       -O2 -Ob1 -Ot -Oi -Oy- -EHs -GS -bigobj -DNDEBUG %ASM_MACROS%

   REM Patch makefile to replace release optimization flags with debug flags
   perl -i.bak -pe "s/-O2 -Ob1 -Ot -Oi/-Od -Ob0/g; s/\/O2 /\/Od /g" makefile
   ```
   This allows debug builds to have full assembly support while still compiling with no optimization.

5. **Redirect assembly preprocessing to clang-cl**: The Perl patch replaces `$(CC) /EP -D__ASSEMBLER__` with `clang-cl.exe /EP -D__ASSEMBLER__` throughout the makefile. This ensures ARM assembly files are preprocessed by clang-cl, which understands GNU-style ARM syntax.

6. **Add debug symbols to CFLAGS**: The Perl patch also adds `/Z7 /Zo` to the `CFLAGS` line for debug information.

**Why this hybrid approach works:**
- C code compiled with MSVC cl.exe (provides full intrinsics support)
- ARM architecture macros enable compile-time fast paths in C code (AES/GCM/etc.)
- Assembly activation macros tell C code to call assembly functions at runtime
- Debug builds use optimization flag workaround to prevent Configure from disabling assembler
- ARM assembly preprocessing handled by clang-cl (understands GNU syntax)
- Assembly still assembled with the assembler defined by `$(AS)` in VC-WIN64-CLANGASM-ARM configuration
- Full debugging capability with `/Z7` embedded debug info and `/Zo` optimized debugging

**Performance impact of missing architecture macros:**
- Without `-D__ARM_ARCH__=8 -D__ARM_MAX_ARCH__=8`: ~200 MB/s for AES-128-GCM (generic C fallback)
- With architecture macros: Multi-GB/s (ARM crypto extensions + assembly optimizations)
- The difference is due to compile-time guards preventing fast path compilation, not runtime detection

**Important for PowerShell patching (x64/x86):**
- Order of replacements matters: first convert `/Zi` to `/Z7`, then append `/Z7 /Zo`
- Use `[^\r\n]+` to match to end of line when appending flags

**Why `/Z7` instead of `/Zi`:**
- `/Zi` creates separate PDB files, which causes conflicts with jom parallel builds
- `/Z7` embeds debug info directly in `.obj` files, works perfectly with parallel builds
- Both provide full debugging capability

**Why remove PDB install commands:**
- Since `/Z7` embeds debug info in `.obj` files, no `ossl_static.pdb` file is generated
- Without this removal, the install step fails with "Can't Open ossl_static.pdb" error
- The makefile is patched to replace PDB copy commands with a comment explaining the removal

#### Testing the Configuration

Use the unit test script to validate configuration without running full builds:

```batch
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\devenv7\windows\test-openssl-build-config.ps1
```

This validates:
- Configure command construction for both debug and release
- Assembly activation macros presence in Configure command (ARM64 only)
- Debug optimization flag workaround (passes release flags to Configure)
- Makefile patching logic for debug information flags (x64/x86 PowerShell approach)
- ARM64 assembly preprocessor redirection to clang-cl (Perl approach)
- Debug optimization flag replacement in makefile (debug builds only)
- CFLAGS modification for debug symbols
- Proper flag placement (LIB_CFLAGS vs ASFLAGS)
- PDB install command removal to prevent install failures

Run specific test suites:
```batch
REM Test only ARM64 assembly preprocessing patch
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\devenv7\windows\test-openssl-build-config.ps1 -TestName ARM64PerlPatch

REM Test only debug optimization flag patching
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\devenv7\windows\test-openssl-build-config.ps1 -TestName DebugOptimizationPatch

REM Test only x64/x86 makefile patching
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\devenv7\windows\test-openssl-build-config.ps1 -TestName MakefilePatch
```

#### Verifying Assembly Activation (ARM64 only)

After building OpenSSL for ARM64, verify that assembly optimizations are active:

```batch
cd %USERPROFILE%\swblocks\dist-devenv7-windows-hostarch-a64-targets-a64-x64-x86\openssl\3.5.4\win-a64-vc143-release\bin
openssl.exe speed -evp aes-128-gcm
```

**What to check:**

1. **Compiler flags line** - Should contain assembly activation macros:
   ```
   compiler: ... -DOPENSSL_BN_ASM_MONT -DOPENSSL_CPUID_OBJ -DBSAES_ASM -DVPAES_ASM ...
   ```

2. **Options line** - Should show `mont-asm` indicating assembly is active:
   ```
   options: bn(64,64) mont-asm
   ```
   **NOT:** `options: bn(64,64)` (missing `mont-asm` means assembly is built but not active)

3. **Verify assembly symbols in library**:
   ```batch
   cd %USERPROFILE%\swblocks\dist-devenv7-windows-hostarch-a64-targets-a64-x64-x86\openssl\3.5.4\win-a64-vc143-release\lib
   dumpbin /symbols libcrypto.lib | findstr aes_v8_set_encrypt_key
   ```
   Should find the symbol, proving assembly code is in the library.

If assembly activation macros are missing from the compiler line, or `mont-asm` is absent from options, the assembly functions exist in the library but the C code won't call them, resulting in significantly slower performance.

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

### 3. Build Directory in Repository

**Symptom:** Build artifacts appearing in `<repo>\bld\` directory

**Cause:** Using `%REPO_ROOT%\bld` instead of swblocks folder

**Solution:** Always calculate `BLD_ROOT` from `DIST_ROOT_DEPS1` parent directory

### 4. Comma-Separated Arguments Without Quotes

**Symptom:** `Unknown option: x64` when passing `-targets a64,x64,x86` or `-arch a64,x64,x86`

**Cause:** Windows batch treats commas as argument separators when unquoted, splitting `a64,x64,x86` into three separate arguments

**Solution:**

**For scripts that accept multiple values (build-env-all-windows.bat):**
Use the argument collection loop pattern:

```batch
if /i "%~1"=="-targets" (
    set "TARGET_ARCHS="
    shift
    goto collect_targets_loop
)
goto skip_collect_targets
:collect_targets_loop
if "%~1"=="" goto args_done
set "FIRST_CHAR=%~1"
set "FIRST_CHAR=!FIRST_CHAR:~0,1!"
if "!FIRST_CHAR!"=="-" goto parse_args
if "!TARGET_ARCHS!"=="" (
    set "TARGET_ARCHS=%~1"
) else (
    set "TARGET_ARCHS=!TARGET_ARCHS!,%~1"
)
shift
goto collect_targets_loop
:skip_collect_targets
```

**For single-architecture scripts (build-boost-windows.bat, build-openssl-windows.bat):**
These scripts build ONE architecture at a time. Users should either:
1. Quote comma-separated values: `-arch "a64,x64,x86"` (but script will treat as invalid single value)
2. **Use build-env-all-windows.bat** to build multiple architectures
3. Call the script multiple times with single architecture values

**Important:** Document clearly in help text that the script accepts only ONE architecture.

### 5. Label Scoping

**Symptom:** `The system cannot find the batch label specified`

**Cause:** Label placed inside an `if` block

**Solution:** Labels MUST be at column 1, outside any blocks:

```batch
if condition (
    goto my_label
)
goto skip_label
:my_label
REM Label code here
:skip_label
```

### 6. JSON Spirit Directory Structure

**Symptom:** Double-nested directories like `json-spirit\4.08\source\4.08\source`

**Cause:** The tar.gz archive contains `json-spirit/4.08/source/` structure

**Solution:** Extract to temp, then move `json-spirit/{version}/source/` to final location

### 7. Makefile Patching Regex Breaks Syntax

**Symptom:** `Error: syntax error in Makefile line 151` with flags appearing on separate line:
```makefile
LIB_CFLAGS=/MT /Zl $(CNF_CFLAGS) $(CFLAGS)
 /Z7 /Zo
```

**Cause:** Incorrect regex pattern appends to line without anchoring to end of line, causing flags to appear on next line

**Solution:** Use `[^\r\n]+` to match to end of line when appending:

```powershell
# ❌ WRONG - creates new line
$content = $content -replace '(LIB_CFLAGS=.*)','$1 /Z7 /Zo'

# ✅ CORRECT - appends to existing line
$content = $content -replace '(LIB_CFLAGS=[^\r\n]+)','$1 /Z7 /Zo'
```

**Also important:** Order of replacements matters:
1. First convert `/Zi` to `/Z7` (removes PDB references)
2. Then append `/Z7 /Zo` to `LIB_CFLAGS` line

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

### Multi-Architecture vs Single-Architecture Scripts

The project has two types of build scripts:

#### Multi-Architecture Script: `build-env-all-windows.bat`

**Purpose:** Build complete environment for multiple architectures

**Usage:**
```batch
build-env-all-windows.bat -hostarch a64 -targets a64,x64,x86
build-env-all-windows.bat -hostarch a64 -targets a64 x64 x86
build-env-all-windows.bat -hostarch a64 -targets "a64,x64,x86"
```

**Features:**
- Accepts comma-separated or space-separated architecture lists
- Handles both quoted and unquoted arguments
- Loops over architectures and calls single-arch build scripts
- Uses argument collection loop to handle all formats

#### Single-Architecture Scripts: `build-boost-windows.bat`, `build-openssl-windows.bat`

**Purpose:** Build ONE library for ONE architecture

**Usage:**
```batch
REM ✅ CORRECT
build-boost-windows.bat -arch a64
build-openssl-windows.bat -arch x64

REM ❌ WRONG - Will fail with "Unknown option" error
build-boost-windows.bat -arch a64,x64,x86
build-openssl-windows.bat -arch a64,x64,x86
```

**Why single-arch only:**
- These scripts build and test one variant at a time
- They manage a single build directory
- They load one architecture's environment

**To build multiple architectures:**
1. Use `build-env-all-windows.bat` (recommended)
2. Call the single-arch script multiple times:
   ```batch
   for %%A in (a64 x64 x86) do (
       build-boost-windows.bat -arch %%A
   )
   ```

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

- **2026-01-16**:
  - Added OpenSSL build configuration strategy, debug information handling, and debugging guidelines
  - Added ARM64 assembly preprocessing solution using Perl-based makefile patching
  - Added assembly activation macros for ARM64 to fix VC-WIN64-CLANGASM-ARM quirk
  - Added verification procedures for assembly activation
- **2026-01-15**: Initial version with batch script rules, architecture tags, and build system guidelines

---

## References

- Architecture normalization: `scripts/devenv7/windows/internal/common.ps1`
- VS architecture conversion: `ConvertTo-VSArchitectureName` function
- Build scripts: `scripts/devenv7/windows/build-*.bat`
- Toolchain setup: `scripts/devenv7/windows/internal/toolchain-setup.ps1`
- OpenSSL build config tests: `scripts/devenv7/windows/test-openssl-build-config.ps1`
