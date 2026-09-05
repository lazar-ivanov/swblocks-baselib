# Plan: Update CONTRIBUTING.md for devenv7

## Context

The project's development environment has been upgraded from devenv4 to devenv7, introducing significant changes: new OS support (RHEL 9/10, Ubuntu 24.04, macOS Sequoia/Darwin 25), new compilers (GCC 15.2.0, Clang 20.1.0, MSVC vc143 VS2022), ARM64 support across all platforms (including Windows), Clang-CL on Windows, Boost 1.90.0 with Boost.JSON as default, and OpenSSL 3.5.4. The CONTRIBUTING.md needs to be updated to reflect all of this, and the current version archived as CONTRIBUTING.DEVENV4.md.

## Files to Create/Modify

- **Create:** `CONTRIBUTING.DEVENV4.md` — exact copy of current `CONTRIBUTING.md`
- **Modify:** `CONTRIBUTING.md` — rewrite for devenv7

## Step 1: Create CONTRIBUTING.DEVENV4.md

Copy current `CONTRIBUTING.md` to `CONTRIBUTING.DEVENV4.md` verbatim using `cp` command, preserving the devenv4 documentation.

## Step 2: Rewrite CONTRIBUTING.md for devenv7

The new file follows the same overall structure as the current file. Section-by-section changes:

### Section 1: Contributing (intro)

- Change reference from "devenv4" → "devenv7" and from `CONTRIBUTING.DEVENV3.md` → `CONTRIBUTING.DEVENV4.md`
- Keep all other intro text unchanged (JPMC contact, unit tests description, build system overview)

### Section 2: Development environment and build system

**First paragraph:** Update the dependency description — the library now uses Boost.JSON by default instead of JSON Spirit. JSON Spirit 4.08 is still available optionally via `BL_USE_JSON_SPIRIT=1`.

**Second paragraph (versioning):** Change from "three development environment versions - devenv4 (latest) and devenv3 and devenv2" to "two development environment versions - devenv7 (latest) and [devenv4](CONTRIBUTING.DEVENV4.md) (older)". No mention of devenv5 or devenv6 (intermediate, never published).

**devenv7 specification bullet list** (replaces devenv4 list):

```
* **devenv7**
  * Operating Systems
    * Darwin / macOS platforms (ARM64 only)
      * Darwin 24 / macOS Sequoia
      * Darwin 25 / macOS Tahoe
    * Linux platforms
      * RHEL 9 (a64, x64)
      * RHEL 10 (a64)
      * Ubuntu 24.04 LTS (a64, x64)
    * Windows platforms
      * Windows 10+ (a64, x64, x86) — ARM64 (a64) is a notable new capability
  * Compilers
    * GCC 15.2.0 for all supported Linux platforms
    * Clang 20.1.0 for all supported Linux platforms
    * Apple Clang 17.0.0 for Darwin / macOS platforms
    * Microsoft vc143 Visual C++ 2022 (VC 17.08) for Windows platforms
    * Clang-CL ccl16 for Windows platforms (new — see below)
  * C++ standard library implementations
    * libc++ with Clang for Darwin / macOS and Linux platforms
    * libstdc++ with GCC for Linux platforms
    * msvcrt for vc143 for Windows platforms
  * Boost 1.90.0 (with Boost.JSON as the default JSON library)
  * OpenSSL 3.5.4 (optionally OpenSSL 1.1.1w via BL_USE_OPENSSL_1X=1)
  * JSON Spirit 4.08 (optional, not enabled by default — see below)
```

### Section 3: Clang-CL on Windows (NEW)

New subsection documenting:
- What Clang-CL (ccl16) is — clang-cl.exe front-end, MSVC ABI-compatible
- How to use it: `make TOOLCHAIN=ccl16`, `make TOOLCHAIN=ccl16 VARIANT=release`, `make TOOLCHAIN=ccl16 ARCH=x64`
- No additional download needed — included in the devenv7 Windows distribution
- Supports all architectures: a64, x64, x86

### Section 4: JSON library selection (NEW)

New subsection documenting:
- devenv7 uses Boost.JSON by default (included in Boost 1.90.0)
- To use JSON Spirit instead: `make BL_USE_JSON_SPIRIT=1`
- The code abstraction layer allows transparent switching

### Section 5: Cross-compilation on Windows (NEW brief subsection)

Brief note about:
- Each Windows distribution includes support for all target architectures (a64, x64, x86)
- Use `make ARCH=x64` to cross-compile (default is host architecture)

### Section 6: Development environment distributions and links

**Intro paragraph:** Update to remove "Google drive" reference. State these are pre-built distributions served from storage.swblocks.net.

**macOS distributions (2):**
- Darwin 24 (Sequoia) ARM64: `https://storage.swblocks.net/devenv/7/macos/darwin24/a64/dist-devenv7-darwin-24-a64.tar.gz`
- Darwin 25 ARM64: `https://storage.swblocks.net/devenv/7/macos/darwin25/a64/dist-devenv7-darwin-25-a64.tar.gz`

**Linux distributions:** Group by OS, recommend combined (gcc+clang) variant, mention single-compiler variants exist.

RHEL 10 (a64) — recommend: `dist-devenv7-rhel10-gcc1520-clang2010-a64.tar.gz`
RHEL 9 (a64) — recommend: `dist-devenv7-rhel9-gcc1520-clang2010-a64.tar.gz`
RHEL 9 (x64) — recommend: `dist-devenv7-rhel9-gcc1520-clang2010-x64.tar.gz`
Ubuntu 24.04 (a64) — recommend: `dist-devenv7-ub24-gcc1520-clang2010-a64.tar.gz`
Ubuntu 24.04 (x64) — recommend: `dist-devenv7-ub24-gcc1520-clang2010-x64.tar.gz`

Single-compiler variants (gcc-only and clang-only) listed as alternatives.

All URLs under `https://storage.swblocks.net/devenv/7/{platform}/...`

**Windows distributions (3 main + optional):**

Main distributions (one per host architecture, each targets a64+x64+x86):
- Host ARM64 (a64): `dist-devenv7-windows-hostarch-a64-targets-a64-x64-x86.zip`
- Host x64: `dist-devenv7-windows-hostarch-x64-targets-a64-x64-x86.zip`
- Host x86: `dist-devenv7-windows-hostarch-x86-targets-a64-x64-x86.zip`

All under `https://storage.swblocks.net/devenv/7/windows/vs2022-vc143-17.08/`

Note on VS2022 vc143 VC 17.08 compiler and Windows SDK 10.

Optional Windows downloads:
- Downloads-cache archives (3, one per host arch) — optional, for rebuilding/updating the environment offline
- MSVC toolchain package (`msvc-toolchain-17.08-and-sdk.zip`) — optional, for convenience when rebuilding/updating

### Section 7: Setting up the development environment

**Archive extraction:** Archives are structured so they can be extracted with the home directory as root. Once extracted, the distribution will be under `$(HOME)/swblocks` on Darwin/macOS and Linux, or `%USERPROFILE%\swblocks` on Windows. Each archive includes a pre-generated `projects/make/ci-init-env.mk` that can be copied directly into the repo clone if extracted to the home directory; otherwise paths must be adjusted.

**ci-init-env.mk examples** (single-root approach, all 3 DIST_ROOT_DEPS pointing to same dir, using `$(HOME)` on Darwin/Linux and `$(USERNAME)` on Windows):

Windows example:
```make
DIST_ROOT_DEPS1 = /c/Users/$(USERNAME)/swblocks/dist-devenv7-windows-hostarch-x64-targets-a64-x64-x86
DIST_ROOT_DEPS2 = /c/Users/$(USERNAME)/swblocks/dist-devenv7-windows-hostarch-x64-targets-a64-x64-x86
DIST_ROOT_DEPS3 = /c/Users/$(USERNAME)/swblocks/dist-devenv7-windows-hostarch-x64-targets-a64-x64-x86
```

macOS example:
```make
DIST_ROOT_DEPS1 = $(HOME)/swblocks/dist-devenv7-darwin-24-a64
DIST_ROOT_DEPS2 = $(HOME)/swblocks/dist-devenv7-darwin-24-a64
DIST_ROOT_DEPS3 = $(HOME)/swblocks/dist-devenv7-darwin-24-a64
```

Linux example:
```make
DIST_ROOT_DEPS1 = $(HOME)/swblocks/dist-devenv7-ub24-gcc1520-clang2010-x64
DIST_ROOT_DEPS2 = $(HOME)/swblocks/dist-devenv7-ub24-gcc1520-clang2010-x64
DIST_ROOT_DEPS3 = $(HOME)/swblocks/dist-devenv7-ub24-gcc1520-clang2010-x64
```

**Windows PATH setup — recommended approach (setup-env scripts):**

Document auto-generated scripts at `{dist-root}\scripts\ci\`:
- `setup-env-{arch}.bat` — Full environment (MSVC, SDK, Clang-CL, debuggers, Jom, NASM, Git, Python, MSYS2)
- `setup-env-nomsvc-{arch}.bat` — No MSVC compiler (debuggers, Jom, Git, Python, MSYS2)
- `setup-env-minimal-{arch}.bat` — Minimal (Git and MSYS2 only)

Where {arch} is a64, x64, or x86.

Example invocation:
```
call %USERPROFILE%\swblocks\dist-devenv7-windows-hostarch-x64-targets-a64-x64-x86\scripts\ci\setup-env-x64.bat
```

**Windows PATH setup — manual fallback:**

For users who prefer manual setup (paths assume extracted to `%USERPROFILE%\swblocks` with x64 host distribution):
```
%USERPROFILE%\swblocks\dist-devenv7-windows-hostarch-x64-targets-a64-x64-x86\msys2\20251213\msys64\usr\bin
%USERPROFILE%\swblocks\dist-devenv7-windows-hostarch-x64-targets-a64-x64-x86\git\2.48.1\default\bin
%USERPROFILE%\swblocks\dist-devenv7-windows-hostarch-x64-targets-a64-x64-x86\python\3.14.2\default
```

Note: setup-env scripts are recommended as they also configure compiler toolchain paths.

**Build examples:**
```make
make -k -j4
make -k -j4 VARIANT=release
make -k -j4 TOOLCHAIN=ccl16
make -k -j4 ARCH=x64
make utf_baselib
make test_utf_baselib_data
make -k -j4 && make -k -j4 test
make help
```

### Section 8: Using GitHub and creating pull requests

Keep exactly as-is — entirely version-independent.

## Step 3: Save plan for future reference

Save the plan to `notes/plans/how-to-update-contributing.md` (create `notes/plans/` directory if needed) for future reference when the CONTRIBUTING.md needs to be updated again.

## Verification

1. Verify `CONTRIBUTING.DEVENV4.md` is an exact copy of the original `CONTRIBUTING.md` content
2. Verify all distribution URLs are correctly formed by cross-referencing with the provided URL list
3. Verify internal cross-references work: `CONTRIBUTING.md` links to `CONTRIBUTING.DEVENV4.md`
4. Verify `CONTRIBUTING.DEVENV4.md` still links to `CONTRIBUTING.DEVENV3.md` (unchanged)
5. Check that `README.md` references to `CONTRIBUTING.md` still work (they reference it generically)
