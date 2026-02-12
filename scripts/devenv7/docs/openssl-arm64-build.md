# OpenSSL ARM64 Build: Technical Deep Dive

This document contains detailed technical analysis of the ARM64-specific OpenSSL build challenges and solutions for Windows devenv7. For operational guidance (what commands to run), see the parent [CLAUDE.md](../CLAUDE.md).

---

## ARM64 Preprocessor Problem

The Microsoft C Preprocessor (invoked by `cl.exe /EP`) does not understand GNU-style ARM assembly syntax. When it encounters the `#` character (used for comments and directives in ARM assembly), it interprets it as an invalid C preprocessor directive, either stripping the line or mangling the instruction. This breaks ARM64 assembly compilation.

## ARM64 Solution Components

### 1. MSVC cl.exe for C Compilation

Required for MSVC intrinsics like `_InterlockedAdd64` that OpenSSL relies on. LLVM/Clang ARM64 intrinsics don't fully implement MSVC-compatible aliases.

### 2. Assembly Activation Macros

Due to quirks in the VC-WIN64-CLANGASM-ARM target configuration, OpenSSL correctly builds assembly objects but fails to tell the C compiler to use them. These macros activate the assembly code paths in the C "glue" code:

```batch
-DOPENSSL_CPUID_OBJ -DOPENSSL_BN_ASM_MONT -DMD5_ASM
-DVPAES_ASM -DBSAES_ASM -DSHA1_ASM -DSHA256_ASM -DSHA512_ASM
-DKECCAK1600_ASM -DPOLY1305_ASM -DECP_NISTZ256_ASM
```

**Critical:** Without these macros, the assembly functions are compiled into the library but never called. The C code falls back to slow software implementations. You can verify assembly activation by checking `openssl.exe speed -evp aes-128-gcm` output — the compiler line should show these macros, and `options:` should display `bn(64,64) mont-asm` (not just `bn(64,64)`).

### 3. ARM Architecture Macros for Compile-Time Path Selection

MSVC cl.exe does not define `__ARM_ARCH__` or `__ARM_MAX_ARCH__` when compiling C code, unlike Clang/GCC. OpenSSL uses these macros as compile-time guards to enable ARMv8-specific fast paths:

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

### 4. Debug Optimization Flag Workaround

OpenSSL's Configure script detects debug optimization flags (`-Od -Ob0`) and disables assembler to avoid historical issues with debug info generation in assembly code. This causes significant performance degradation.

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

### 5. Assembly Preprocessing Redirect

The Perl patch replaces `$(CC) /EP -D__ASSEMBLER__` with `clang-cl.exe /EP -D__ASSEMBLER__` throughout the makefile. This ensures ARM assembly files are preprocessed by clang-cl, which understands GNU-style ARM syntax.

### 6. Debug Symbols

The Perl patch also adds `/Z7 /Zo` to the `CFLAGS` line for debug information.

## Why This Hybrid Approach Works

- C code compiled with MSVC cl.exe (provides full intrinsics support)
- ARM architecture macros enable compile-time fast paths in C code (AES/GCM/etc.)
- Assembly activation macros tell C code to call assembly functions at runtime
- Debug builds use optimization flag workaround to prevent Configure from disabling assembler
- ARM assembly preprocessing handled by clang-cl (understands GNU syntax)
- Assembly still assembled with the assembler defined by `$(AS)` in VC-WIN64-CLANGASM-ARM configuration
- Full debugging capability with `/Z7` embedded debug info and `/Zo` optimized debugging

## Performance Impact

- Without `-D__ARM_ARCH__=8 -D__ARM_MAX_ARCH__=8`: ~200 MB/s for AES-128-GCM (generic C fallback)
- With architecture macros: Multi-GB/s (ARM crypto extensions + assembly optimizations)
- The difference is due to compile-time guards preventing fast path compilation, not runtime detection

## Debug Information Details

**PowerShell patching (x64/x86):**
- Order of replacements matters: first convert `/Zi` to `/Z7`, then append `/Z7 /Zo`
- Use `[^\r\n]+` to match to end of line when appending flags

**Why `/Z7` instead of `/Zi`:**
- `/Zi` creates separate PDB files, which causes conflicts with jom parallel builds
- `/Z7` embeds debug info directly in `.obj` files, works perfectly with parallel builds

**Why remove PDB install commands:**
- Since `/Z7` embeds debug info in `.obj` files, no `ossl_static.pdb` file is generated
- Without this removal, the install step fails with "Can't Open ossl_static.pdb" error
- The makefile is patched to replace PDB copy commands with a comment explaining the removal

## Build Verification Details

The OpenSSL build script automatically verifies the build configuration. Verification output:

```
================================================================================
Verifying OpenSSL Build Configuration
================================================================================

[Step 1/3] Checking hardware acceleration performance...
  Running: openssl.exe speed -evp aes-128-gcm
  AES-128-GCM Speed (16384 bytes): 7.98 GB/sec
  [PASS] Performance exceeds 1.00 GB/sec threshold

[Step 2/3] Checking optimization flags...
  Running: openssl.exe version -a
  Expected flags (debug): /Od -Od -Ob0
  [PASS] Optimization flags correct for debug variant

[Step 3/3] Checking debug information flags...
  Expected flags: /Z7 /Zo
  [PASS] Debug flags present

Total Verifications: 3, Passed: 3, Failed: 0
================================================================================
```

**What verification catches:**
- Missing hardware acceleration (performance < 1 GB/sec — assembly not activated)
- Wrong optimization flags (debug/release mismatch — makefile patching failed)
- Missing debug info (`/Z7` or `/Zo` absent — debugging impaired)

**Unit test coverage:** 34 tests covering performance parsing, compiler flag detection, debug info detection, edge cases.

## Manual Assembly Verification (ARM64)

After building OpenSSL for ARM64, verify that assembly optimizations are active:

```batch
cd %USERPROFILE%\swblocks\dist-devenv7-windows-hostarch-a64-targets-a64-x64-x86\openssl\3.5.4\win-a64-vc143-release\bin
openssl.exe speed -evp aes-128-gcm
```

**What to check:**

1. **Compiler flags line** — Should contain assembly activation macros:
   ```
   compiler: ... -DOPENSSL_BN_ASM_MONT -DOPENSSL_CPUID_OBJ -DBSAES_ASM -DVPAES_ASM ...
   ```

2. **Options line** — Should show `mont-asm` indicating assembly is active:
   ```
   options: bn(64,64) mont-asm
   ```
   **NOT:** `options: bn(64,64)` (missing `mont-asm` means assembly is built but not active)

3. **Verify assembly symbols in library:**
   ```batch
   dumpbin /symbols libcrypto.lib | findstr aes_v8_set_encrypt_key
   ```

If assembly activation macros are missing from the compiler line, or `mont-asm` is absent from options, the assembly functions exist in the library but the C code won't call them, resulting in significantly slower performance.

---

**Related files:**
- Build script: `scripts/devenv7/windows/build-openssl-windows.bat`
- Build config tests: `scripts/devenv7/windows/test-openssl-build-config.ps1`
- Verification script: `scripts/devenv7/windows/verify-openssl-build.ps1`
- Verification tests: `scripts/devenv7/windows/test-openssl-verification.ps1`
