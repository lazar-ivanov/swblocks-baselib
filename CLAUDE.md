# swblocks-baselib Development Guide

This document contains design decisions, implementation details, and development workflows for the swblocks-baselib project.

## Core Principles - Always Follow

**Default to research and recommendations over implementation.**

Do not jump into implementation or change files unless clearly instructed to make changes. When the user's intent is ambiguous, default to providing information, doing research, and providing recommendations rather than taking action. Only proceed with edits, modifications, or implementations when the user explicitly requests them.

Do not make any assumptions. Use the AskUserQuestion tool to ask as many follow ups as you need to reach clarity.

**Always use the project's Python virtual environment.**

When running Python commands, tests, or scripts, ALWAYS use the Python interpreter from the project's `.venv` virtual environment located at `/Users/lazar/dev/github/swblocks-baselib/.venv/bin/python` (or `.venv/bin/pytest` for pytest). If the `.venv` directory does not exist, run `make python-install` to create it before proceeding.

**Never commit to git without explicit permission.**

Do NOT attempt to commit changes to the git repository using `git add`, `git commit`, or `git push` unless the user explicitly asks you to do so. Changes should remain uncommitted until the user reviews and decides to commit them.

---

## Code Review Hygiene and File Modification Guidelines

### Critical Rule: Incremental, Intentional Changes Only

**ALL file modifications must be incremental, intentional, and independently reviewable.** Never rewrite entire files or mix unrelated changes.

### File Modification Tools

**Use the correct tool for the task:**

1. **Edit Tool** - For ALL modifications to existing files
   - ✅ Modifying existing code
   - ✅ Adding new content to existing files (append to end)
   - ✅ Fixing bugs in existing code
   - ✅ Refactoring existing functions
   - **Why**: Edit tool shows exact before/after diffs that are reviewable

2. **Write Tool** - ONLY for creating NEW files
   - ✅ Creating a new file that doesn't exist
   - ❌ **NEVER** use to modify existing files
   - ❌ **NEVER** use to append to existing files
   - **Why**: Write tool overwrites entire file content, creating massive diffs with unintended changes

### Rule: Never Mix Changes

**Each PR/commit should contain ONE type of change:**

❌ **WRONG - Mixed changes in same PR:**
```
- Add new feature (logic change)
- Remove shebang lines (style change)
- Fix typos (non-functional change)
- Reformat docstrings (style change)
- Change assertions (functional change)
```

✅ **CORRECT - Separate PRs:**
- PR 1: Add new feature (logic only)
- PR 2: Style cleanup (style only, if needed)

### Git Diff Review Perspective

**Before making ANY file change, ask:**
1. Will the git diff show ONLY the changes I intend to make?
2. Could I explain every line in the diff to a reviewer?
3. Are there any unintended changes (style, formatting, unrelated fixes)?

**If the answer to #3 is YES, STOP and use Edit tool instead.**

### Real Example: What NOT to Do

**Scenario**: Adding new tests to existing test file

❌ **WRONG approach:**
```python
# Using Write tool - overwrites entire file
Write(file_path="test_file.py", content="""
# Entire file content with:
# - New tests (intended)
# - Removed shebang (unintended)
# - Changed assertions (unintended)
# - Modified docstrings (unintended)
""")
```

**Result**: Git shows 1,312 insertions, 1,292 deletions - impossible to review

✅ **CORRECT approach:**
```python
# Using Edit tool - append new tests only
Edit(
    file_path="test_file.py",
    old_string="        assert re.search(timestamp_pattern, captured.out) is not None\n",
    new_string="""        assert re.search(timestamp_pattern, captured.out) is not None


# ====================================================================================
# Phase 4: New Tests
# ====================================================================================

class TestNewFeature:
    def test_new_feature(self):
        ...
"""
)
```

**Result**: Git shows only new lines added - clean, reviewable diff

### Separation of Concerns

**Logic changes and style changes must NEVER be mixed:**

1. **Logic changes** (functional):
   - Bug fixes
   - New features
   - Refactoring
   - Algorithm changes

2. **Style changes** (non-functional):
   - Formatting (whitespace, line breaks)
   - Naming (variable renames for clarity)
   - Comments/docstrings
   - Import order
   - Shebang lines

**Rule**: If you're making a logic change, do NOT touch style. If you're making a style change, do NOT touch logic.

### Pre-Commit Checklist

Before committing, verify:
- [ ] Used Edit tool (not Write) for existing files?
- [ ] Git diff shows ONLY intended changes?
- [ ] No style changes mixed with logic changes?
- [ ] No unintended modifications to existing code?
- [ ] Each modified line has a clear reason?
- [ ] Diff is reviewable (not 1000+ lines)?

### When You Make a Mistake

If you accidentally rewrite a file or mix changes:
1. **STOP immediately**
2. `git checkout <file>` to restore original
3. Use Edit tool to make ONLY intended changes
4. Verify git diff shows only what you meant to change
5. Commit with clean diff

### Summary

**Golden Rule**: Every line in `git diff` should be intentional and explainable. If you can't explain why a line changed, you've made a mistake.

---

## Table of Contents

- [Code Review Hygiene and File Modification Guidelines](#code-review-hygiene-and-file-modification-guidelines)
- [Windows Batch File Scripting Conventions](#windows-batch-file-scripting-conventions)
- [Build Commands](#build-commands)
  - [Cross-Compilation (Windows devenv7+)](#cross-compilation-windows-devenv7)
- [Development Environment Setup](#development-environment-setup)
- [devenv Version Gating Pattern](#devenv-version-gating-pattern)
- [Windows JNI Support (devenv7+)](#windows-jni-support-devenv7)

---

## Windows Batch File Scripting Conventions

**CRITICAL: Always escape special characters in batch files.**

When writing or modifying Windows batch files (`.bat`), special characters **MUST** be escaped with `^` when used literally inside control structures (`if`, `for`, etc.).

### Special Characters That Require Escaping

The following characters have special meaning in batch files and must be escaped with `^` when used literally:

- `(` and `)` - Parentheses (delimit code blocks)
- `<` and `>` - Redirection operators
- `|` - Pipe operator
- `&` - Command separator
- `%` - Variable expansion (use `%%` in batch files, `%` in command line)

### Common Mistake: Parentheses in Echo Statements

**WRONG:**
```batch
if /i "%TOOLCHAIN%"=="ccl16" (
    echo Using clang-cl compiler (version 16)
)
```

This will fail because the unescaped `(` in the echo statement is interpreted as starting a new code block.

**CORRECT:**
```batch
if /i "%TOOLCHAIN%"=="ccl16" (
    echo Using clang-cl compiler ^(version 16^)
)
```

### Examples from This Codebase

From `build-boost-windows.bat`:
```batch
REM Verify clang-cl is available for ccl16 toolchain
if /i "%TOOLCHAIN_NAME%"=="ccl16" (
    echo.
    echo Checking for clang-cl compiler ^(required for ccl16 toolchain^)...
    where clang-cl 1>nul 2>nul
    if errorlevel 1 (
        echo ERROR: clang-cl.exe not found in PATH but required for ccl16 toolchain
        echo.
        echo Please ensure clang-cl is installed as part of MSVC Build Tools
        echo and is available in the PATH set by setup-env-%ARCH%.bat
        goto error
    )
    echo clang-cl found:
    where clang-cl
)
```

### Rule of Thumb

**If you're inside an `if` block, `for` loop, or any other control structure, escape all special characters in echo statements and other commands with `^`.**

### CRITICAL: Line Continuation Does NOT Work with Quoted Strings in Set Commands

**IMPORTANT:** Using `^` for line continuation in batch files **breaks quoted string context** in `set` commands. The subsequent lines after `^` are **NOT** part of the quoted string and will be interpreted as separate commands.

**WRONG:**
```batch
if /i "%TOOLCHAIN_NAME%"=="ccl16" (
    set "ARCH_MACROS=%ARCH_MACROS% -D_InterlockedAdd64=_InterlockedExchangeAdd64 ^
-Wno-error=implicit-function-declaration ^
-Wno-error=incompatible-pointer-types-discards-qualifiers"
)
```

This will fail with an error like: `'-Wno-error' is not recognized as an internal or external command`

**Why it fails:** The `^` line continuation doesn't preserve the quoted string context. The lines after `^` are executed as separate commands, not as part of the `set "ARCH_MACROS=..."` statement.

**CORRECT Solution 1 - Single Line:**
```batch
if /i "%TOOLCHAIN_NAME%"=="ccl16" (
    set "ARCH_MACROS=%ARCH_MACROS% -D_InterlockedAdd64=_InterlockedExchangeAdd64 -Wno-error=implicit-function-declaration -Wno-error=incompatible-pointer-types-discards-qualifiers"
)
```

**CORRECT Solution 2 - Multiple Set Commands:**
```batch
if /i "%TOOLCHAIN_NAME%"=="ccl16" (
    set "ARCH_MACROS=%ARCH_MACROS% -D_InterlockedAdd64=_InterlockedExchangeAdd64"
    set "ARCH_MACROS=%ARCH_MACROS% -Wno-error=implicit-function-declaration"
    set "ARCH_MACROS=%ARCH_MACROS% -Wno-error=incompatible-pointer-types-discards-qualifiers"
)
```

**Key Rule:** For `set` commands with quoted strings, either:
1. Keep everything on a single line (preferred for short strings)
2. Use multiple `set` commands to build up the value incrementally
3. **NEVER** use `^` line continuation inside quoted strings

### CRITICAL: Delayed Expansion Inside Control Structures

**IMPORTANT:** Variables set or modified inside `if` blocks, `for` loops, or other control structures require **delayed expansion** syntax (`!VAR!`) instead of regular expansion (`%VAR%`).

#### The Problem

When a batch file parses a control structure (like an `if` block), ALL variable expansions using `%VAR%` are evaluated **at parse time** (before the block executes). This means:

- Variables set INSIDE the block will appear empty when referenced with `%VAR%`
- String substitutions like `%VAR:old=new%` will use the value from BEFORE the block
- The block behaves as if variables are "frozen" at their pre-block values

#### The Solution

Use delayed expansion (`!VAR!`) for variables that are:
1. Set or modified inside the same control structure
2. Used in string substitutions inside control structures
3. Referenced after being changed in the same block

#### Real Bug Example from This Codebase

**WRONG - Produces Empty String:**
```batch
if not "!SKIP_TOOLCHAIN!"=="1" (
    set "TARGETS_SPACED=%TARGET_ARCHS:,= %"
    echo Targets: %TARGETS_SPACED%
    call script.bat -targets %TARGETS_SPACED%
)
```

**Why it fails:**
- Line 2: String substitution `%TARGET_ARCHS:,= %` is evaluated at parse time
- If `TARGET_ARCHS` was set before the block, the substitution might fail
- Result: `TARGETS_SPACED` becomes empty
- Line 4: Passes empty string to script, causing it to use defaults

**CORRECT - Uses Delayed Expansion:**
```batch
if not "!SKIP_TOOLCHAIN!"=="1" (
    set "TARGETS_SPACED=!TARGET_ARCHS:,= !"
    echo Targets: !TARGETS_SPACED!
    call script.bat -targets !TARGETS_SPACED!
)
```

**Why it works:**
- Line 2: String substitution `!TARGET_ARCHS:,= !` evaluates at execution time
- Uses current value of `TARGET_ARCHS`
- Result: `TARGETS_SPACED` correctly contains "a64 x64 x86"
- Line 4: Passes correct value to script

#### When to Use Each Syntax

| Syntax | When to Use | Example |
|--------|-------------|---------|
| `%VAR%` | Variables set BEFORE the control structure | `%DIST_ROOT%`, `%HOST_ARCH%` |
| `!VAR!` | Variables set INSIDE the control structure | `!TARGETS_SPACED!`, `!CURR_ARCH!` |
| `!VAR!` | String substitutions inside control structures | `!TARGET_ARCHS:,= !` |
| `!VAR!` | Loop variables in `for` loops | `for %%A in (...) do echo !%%A!` |

#### Enabling Delayed Expansion

Delayed expansion is enabled with:
```batch
setlocal enabledelayedexpansion
```

This is typically placed near the beginning of the script, after capturing script directory and before any control structures.

#### Common Mistakes and Symptoms

**Symptom 1: Variable appears empty inside loop**
```batch
for %%A in (a64 x64 x86) do (
    set "CURRENT=%%A"
    echo Current: %CURRENT%  REM Shows nothing or wrong value!
)
```
**Fix:** Use `!CURRENT!` instead of `%CURRENT%`

**Symptom 2: String substitution produces empty result**
```batch
set "TARGET_ARCHS=a64,x64,x86"
if condition (
    set "SPACED=%TARGET_ARCHS:,= %"  REM Results in empty string!
)
```
**Fix:** Use `!TARGET_ARCHS:,= !` instead

**Symptom 3: Variable has old value, not updated value**
```batch
set "COUNT=5"
if condition (
    set /a COUNT=%COUNT%+1
    echo Count is: %COUNT%  REM Still shows 5, not 6!
)
```
**Fix:** Use `!COUNT!` in the echo statement

#### Rule of Thumb

**Inside ANY control structure (`if`, `for`, `while`), use `!VAR!` for:**
1. Variables you just set or modified in the same structure
2. Any string substitutions (`:search=replace`)
3. Variables that change during loop iterations

**Use `%VAR%` only for:**
1. Variables that were set BEFORE entering the control structure
2. Environment variables that don't change
3. Parameters passed to the script (`%1`, `%2`, etc.)

#### Testing for Delayed Expansion Issues

When debugging, add diagnostic echo statements:
```batch
echo DEBUG: Before substitution - TARGET_ARCHS=!TARGET_ARCHS!
set "TARGETS_SPACED=!TARGET_ARCHS:,= !"
echo DEBUG: After substitution - TARGETS_SPACED=!TARGETS_SPACED!
```

If `TARGETS_SPACED` is empty but `TARGET_ARCHS` is not, you have a delayed expansion issue.

---

## Build Commands

### Building the Entire Project

```bash
# Build debug variant (all targets)
make -k -j4

# Build release variant (all targets)
make -k -j4 VARIANT=release

# Build both debug and release
make -k -j4 && make -k -j4 VARIANT=release
```

### Building Specific Targets

To build only a specific library or test target:

```bash
# Build only a specific target (e.g., utf_baselib_jni)
make -k -j1 utf_baselib_jni

# Build release variant of specific target
make -k -j1 utf_baselib_jni VARIANT=release
```

**Note:** The `-j1` flag ensures single-threaded builds which can be helpful for debugging build issues.

### Cross-Compilation (Windows devenv7+)

Starting with **devenv7**, Windows builds support cross-compilation to different target architectures using the `ARCH` parameter.

#### Architecture Support

| Architecture | ARCH Value | Status |
|--------------|------------|--------|
| ARM64        | `a64`      | ✅ Supported |
| x64          | `x64`      | ✅ Supported |
| x86          | `x86`      | ⚠️ Limited (no JDK 25 available) |

#### How Cross-Compilation Works

The build system distinguishes between:
- **Host Architecture:** The architecture of the compiler binaries (determined by MSVCHOSTARCHTAG)
- **Target Architecture:** The architecture of the output binaries (controlled by ARCH parameter)

**Host architecture** is auto-detected using Windows environment variables (`PROCESSOR_ARCHITECTURE` and `PROCESSOR_ARCHITEW6432`) and determines which compiler binaries to use:
- ARM64 host → uses `Hostarm64` compiler binaries
- x64 host → uses `Hostx64` compiler binaries
- x86 host → uses `Hostx86` compiler binaries

**Target architecture** is controlled by the `ARCH` parameter:
- If not specified: defaults to host architecture
- If specified: overrides detection and targets that architecture

#### Cross-Compilation Examples

```bash
# On ARM64 host, build for ARM64 (default - no ARCH needed)
make -k -j4
# → Host: Hostarm64, Target: a64

# On ARM64 host, cross-compile for x64
make -k -j4 ARCH=x64
# → Host: Hostarm64, Target: x64

# On x64 host, cross-compile for ARM64
make -k -j4 ARCH=a64
# → Host: Hostx64, Target: a64

# Build JNI tests for specific architecture
make -k -j1 utf_baselib_jni ARCH=x64
# → Uses openjdk/25/x64
```

#### Important Notes

- **JDK Availability:** Each target architecture requires its own JDK installation in `openjdk/25/{arch}`:
  - `openjdk/25/a64` for ARM64 builds
  - `openjdk/25/x64` for x64 builds
  - `openjdk/25/x86` not available (no JDK 25 for x86)
- **Automatic Skipping:** If JDK not found for target architecture, JNI targets automatically skip
- **Setup Scripts Optional:** The makefiles configure all compiler paths internally based on ARCH parameter. Setup scripts are optional and only needed for interactive tool use.

#### ARCH Parameter Implementation Details

The ARCH parameter system was designed to support cross-compilation while maintaining backward compatibility with older devenvs. Understanding the implementation is crucial for maintaining the build system.

##### Key Design Principles

1. **Environment Variables Must Be Ignored**
   - `LIB`, `LIBPATH`, and `INCLUDE` from environment (e.g., setup-env scripts) are completely ignored
   - Makefiles construct these paths from scratch based on ARCH parameter
   - `PATH` from environment is preserved but compiler paths are prepended

2. **Host vs Target Architecture Separation**
   - **Host architecture:** Detected using Windows environment variables (`PROCESSOR_ARCHITECTURE` and `PROCESSOR_ARCHITEW6432`), determines compiler binaries
   - **Target architecture:** Controlled by ARCH parameter, determines libraries and output architecture
   - `BL_WIN_ARCH_IS_ARM64` / `BL_WIN_ARCH_IS_X64` flags represent HOST architecture
   - `ARCH` variable represents TARGET architecture

4. **Host Architecture Detection (Windows)**

   The build system uses Windows environment variables to reliably detect the host architecture. Detection works across all environments including cmd.exe, PowerShell, and MSYS2 shells.

   | Host OS | Process        | PROCESSOR_ARCHITECTURE | PROCESSOR_ARCHITEW6432 | PROCESSOR_IDENTIFIER |
   |---------|----------------|------------------------|------------------------|----------------------|
   | x86     | Native x86     | x86                    | (Not Set)              | x86 Family...        |
   | x64     | Native x64     | AMD64                  | (Not Set)              | Intel64 Family...    |
   | x64     | x86 WOW64      | x86                    | AMD64                  | Intel64 Family...    |
   | ARM64   | Native ARM64   | ARM64                  | (Not Set)              | ARMv8 (64-bit)...    |
   | ARM64   | x86 Emulation  | x86                    | ARM64                  | ARMv8 (64-bit)...    |
   | ARM64   | x64 Emulation  | AMD64                  | ARM64 or (Not Set)*    | ARMv8 (64-bit)...    |

   *Note: MSYS2 x64 binaries (including make.exe) running on ARM64 don't set `PROCESSOR_ARCHITEW6432`, but `PROCESSOR_IDENTIFIER` reliably contains "ARMv8" or "AArch64".

   **Detection Algorithm:**
   1. If `PROCESSOR_IDENTIFIER` contains "ARMv8" or "AArch64" → ARM64 host (works in all environments)
   2. Else if `PROCESSOR_ARCHITECTURE=ARM64` → ARM64 host (native ARM64 process in cmd.exe)
   3. Else if `PROCESSOR_ARCHITEW6432=ARM64` → ARM64 host (emulated x86 process in cmd.exe)
   4. Else if `PROCESSOR_ARCHITEW6432=AMD64` → x64 host (x86 WOW64)
   5. Else if `PROCESSOR_ARCHITECTURE=AMD64` → x64 host (native x64)
   6. Else → x86 host (native x86)

3. **Command-Line Override Priority**
   - User-specified `ARCH=x64` on command line takes absolute precedence
   - Detection logic uses `ARCH ?=` (conditional assignment) to respect command-line values
   - No early defaults that would prevent command-line override

##### Critical Implementation Files

**[projects/make/common.mk](projects/make/common.mk#L145-L146)**
```makefile
# Ignore LIB, LIBPATH, and INCLUDE from environment - makefiles set these explicitly
LIBPATH  :=
INCLUDE  :=
```
**Why:** Initializes these variables to empty, completely ignoring environment values from setup scripts. Without this, environment variables would be inherited and cause duplicate or incorrect paths.

**[projects/make/platform.mk](projects/make/platform.mk#L18-L67)**
```makefile
# Detect host architecture using Windows environment variables
# PROCESSOR_IDENTIFIER: CPU identification string (contains "ARMv8" or "AArch64" on ARM64)
# PROCESSOR_ARCHITECTURE: Architecture of the current process
# PROCESSOR_ARCHITEW6432: Set when process is emulated, contains real host architecture

# Priority 1: Check PROCESSOR_IDENTIFIER for ARM64 hardware (works in all environments including MSYS2)
ifneq ($(findstring ARMv8,$(PROCESSOR_IDENTIFIER)),)
  BL_WIN_ARCH_IS_ARM64 := 1
  ARCH ?= a64
else ifneq ($(findstring AArch64,$(PROCESSOR_IDENTIFIER)),)
  BL_WIN_ARCH_IS_ARM64 := 1
  ARCH ?= a64
else
  # Priority 2: Check if PROCESSOR_ARCHITECTURE is ARM64 (native ARM64 process in cmd.exe)
  ifeq ($(PROCESSOR_ARCHITECTURE),ARM64)
    BL_WIN_ARCH_IS_ARM64 := 1
    ARCH ?= a64
  else
    # Priority 3: Check PROCESSOR_ARCHITEW6432 for emulated processes (cmd.exe only)
    ifeq ($(PROCESSOR_ARCHITEW6432),ARM64)
      BL_WIN_ARCH_IS_ARM64 := 1
      ARCH ?= a64
    else ifeq ($(PROCESSOR_ARCHITEW6432),AMD64)
      BL_WIN_ARCH_IS_X64 := 1
      ARCH ?= x64
    else
      # Priority 4: Native process, use PROCESSOR_ARCHITECTURE directly
      ifeq ($(PROCESSOR_ARCHITECTURE),AMD64)
        BL_WIN_ARCH_IS_X64 := 1
        ARCH ?= x64
      else
        ARCH ?= x86
      endif
    endif
  endif
endif
```
**Why:** Uses Windows environment variables for reliable host architecture detection across all environments. `PROCESSOR_IDENTIFIER` is checked first because it works in MSYS2 (where x64 make.exe runs under emulation and doesn't set `PROCESSOR_ARCHITEW6432`). For native cmd.exe processes, `PROCESSOR_ARCHITECTURE` and `PROCESSOR_ARCHITEW6432` provide the architecture info. Uses `?=` (conditional assignment) so command-line `ARCH=x64` takes precedence over detection.

**[projects/make/toolchain/msvc-default.mk](projects/make/toolchain/msvc-default.mk#L68-L70)**
```makefile
# Map ARCH to MSVC-specific directory names
# Use = (not :=) to ensure evaluation happens when used, not when assigned
ARCH_LIBPATH = $(if $(filter a64,$(ARCH)),arm64,$(if $(filter x64,$(ARCH)),x64,$(if $(filter x86,$(ARCH)),x86,$(ARCH))))
ARCH_BINPATH = $(if $(filter a64,$(ARCH)),arm64,$(if $(filter x64,$(ARCH)),x64,$(if $(filter x86,$(ARCH)),x86,$(ARCH))))
ARCH_REDIST = $(if $(filter a64,$(ARCH)),arm64,$(if $(filter x64,$(ARCH)),x64,$(if $(filter x86,$(ARCH)),x86,$(ARCH))))
```
**Why:** Uses `=` (recursive assignment, lazy evaluation) instead of `:=` (immediate assignment). This ensures ARCH_LIBPATH evaluates when *used*, not when *assigned*, so it reflects the current value of ARCH (including command-line overrides). With `:=`, the value would be frozen at makefile parse time before command-line parameters take effect.

**[projects/make/toolchain/msvc-default.mk](projects/make/toolchain/msvc-default.mk#L130-L131)**
```makefile
LIBPATH  += $(MSVC)/VC/Tools/MSVC/$(MSVCVERSIONTAG)/lib/$(ARCH_LIBPATH)
LIBPATH  += $(MSVC)/VC/Tools/MSVC/$(MSVCVERSIONTAG)/atlmfc/lib/$(ARCH_LIBPATH)
```
**Why:** All LIBPATH assignments use `$(ARCH_LIBPATH)` (not `$(ARCH)` directly) to ensure consistent architecture mapping. ARCH_LIBPATH maps `a64→arm64`, `x64→x64`, `x86→x86` to match MSVC directory naming conventions.

##### Common Pitfalls and Solutions

**Problem 1: Environment Variables Contaminating Paths**
- **Symptom:** Link errors with mixed arm64 and x64 paths, duplicate library paths
- **Root Cause:** Setup-env scripts set LIB/LIBPATH environment variables, which get inherited by makefiles
- **Solution:** Initialize `LIBPATH :=` and `INCLUDE :=` to empty in common.mk (line 145-146)

**Problem 2: ARCH Parameter Ignored**
- **Symptom:** `make ARCH=x64` still builds for detected architecture (a64)
- **Root Cause:** Detection logic uses `:=` (immediate assignment) instead of `?=` (conditional)
- **Solution:** Use `ARCH ?= a64` in platform.mk so command-line value takes precedence

**Problem 3: ARCH_LIBPATH Not Updating**
- **Symptom:** ARCH=x64 but ARCH_LIBPATH still evaluates to arm64
- **Root Cause:** ARCH_LIBPATH defined with `:=` (immediate evaluation at parse time)
- **Solution:** Use `=` (lazy evaluation) so ARCH_LIBPATH evaluates when used, reflecting current ARCH value

**Problem 4: Duplicate Paths in LIBPATH**
- **Symptom:** LIBPATH contains both arm64 and x64 paths
- **Root Cause:** Makefile included multiple times, or environment contamination
- **Solution:** Ensure common.mk initializes LIBPATH to empty; use lazy evaluation for ARCH_LIBPATH

##### Testing Cross-Compilation

```bash
# Test 1: Default build (should use host architecture)
make -k -j1 utf_baselib
# Verify: ARCH should match host (e.g., a64 on ARM64 machine)

# Test 2: Cross-compile to different architecture
make -k -j1 utf_baselib ARCH=x64
# Verify: ARCH=x64, MSVCHOSTARCHTAG=Hostarm64 (host), paths use x64 libraries

# Test 3: Verify library paths
make -k -j1 utf_baselib ARCH=x64 2>&1 | grep "lib/x64"
# Should show x64 library paths, NO arm64 paths

# Test 4: Architecture not available
make -k -j1 utf_baselib ARCH=x86
# Should skip or fail gracefully (no x86 libraries available)
```

### Cleaning Build Artifacts

**Important:** The standard `make clean` command **does not work** in this project.

To clean build artifacts, use:

```cmd
# Windows (Command Prompt or PowerShell)
rd /s /q .\bld

# Linux/macOS/MSYS2
rm -rf ./bld
```

This removes the entire build directory. After cleaning, you can rebuild from scratch.

---

## Development Environment Setup

### Windows devenv7 Setup

The makefiles automatically configure all compiler paths (PATH, INCLUDE, LIB, LIBPATH) based on the `ARCH` parameter. **Setup scripts are optional** and only needed for:
- Adding MSYS2 tools (make, bash, etc.) to PATH
- Interactive use of compiler tools (cl.exe, link.exe)
- Running test executables manually

**Optional setup scripts:**

```cmd
# For ARM64 architecture
C:\Users\lazar\swblocks\dist-devenv7-windows-hostarch-a64-targets-a64-x64-x86\scripts\ci\setup-env-a64.bat

# For x64 architecture
C:\Users\lazar\swblocks\dist-devenv7-windows-hostarch-a64-targets-a64-x64-x86\scripts\ci\setup-env-x64.bat

# For x86 architecture
C:\Users\lazar\swblocks\dist-devenv7-windows-hostarch-a64-targets-a64-x64-x86\scripts\ci\setup-env-x86.bat
```

**What these scripts provide:**
- Adds MSYS2 tools (make, bash, etc.) to PATH
- Pre-configures MSVC and JDK environment variables for interactive use
- **Note:** The makefiles do NOT rely on these environment variables - they construct all paths internally based on ARCH parameter

**Example workflow:**

```cmd
# 1. Load the environment
C:\Users\lazar\swblocks\dist-devenv7-windows-hostarch-a64-targets-a64-x64-x86\scripts\ci\setup-env-a64.bat

# 2. Navigate to project
cd C:\Users\lazar\dev\github\swblocks-baselib

# 3. Build
make -k -j4

# 4. Build specific target
make -k -j1 utf_baselib_jni
```

---

## devenv Version Gating Pattern

### Overview

When implementing features that should be enabled **by default for devenv7 and all future versions** (devenv8, devenv9, etc.), use the **negative filtering pattern**. This pattern explicitly lists old devenv versions that should NOT have the feature, making new versions automatically supported without code changes.

### Standard Pattern

```makefile
# Use negative filtering: devenv7+ by default, devenv2-6 explicitly handled
ifneq ($(filter devenv2 devenv3 devenv4 devenv5 devenv6,$(DEVENV_VERSION_TAG)),)
  # Old devenv behavior (devenv2-6)
  # This block executes ONLY for devenv2, devenv3, devenv4, devenv5, or devenv6
else
  # New devenv behavior (devenv7, devenv8, devenv9, ...)
  # This block executes for devenv7+ by default
  # No code changes needed when devenv8+ are added
endif
```

### Rationale

**Why use negative filtering instead of positive checks?**

❌ **Avoid this (positive check):**
```makefile
ifeq ($(DEVENV_VERSION_TAG),devenv7)
  # devenv7 behavior
else
  # Old behavior
endif
```

**Problem:** When devenv8 is added, it will fall into the "old behavior" block, requiring code changes.

✅ **Use this (negative filtering):**
```makefile
ifneq ($(filter devenv2 devenv3 devenv4 devenv5 devenv6,$(DEVENV_VERSION_TAG)),)
  # Old behavior (explicitly listed)
else
  # New behavior (applies to devenv7, devenv8, devenv9, ...)
endif
```

**Benefit:** When devenv8 is added, it automatically gets the "new behavior" without any code changes.

### Pattern Explanation

**How the filter works:**

```makefile
$(filter devenv2 devenv3 devenv4 devenv5 devenv6,$(DEVENV_VERSION_TAG))
```

- Returns the value of `DEVENV_VERSION_TAG` **only if** it matches one of the listed versions
- Returns empty string if `DEVENV_VERSION_TAG` is not in the list

**Examples:**
- If `DEVENV_VERSION_TAG=devenv3` → filter returns `devenv3` → condition is true → old behavior
- If `DEVENV_VERSION_TAG=devenv7` → filter returns `` (empty) → condition is false → new behavior
- If `DEVENV_VERSION_TAG=devenv8` → filter returns `` (empty) → condition is false → new behavior ✅

### Current Usage in Codebase

This pattern is currently used in:

1. **Windows JNI Support** (`projects/make/utests/utf_baselib_jni/Makefile`)
   ```makefile
   # Disable Windows JNI for devenv2-6, enable by default for devenv7+
   ifndef BL_WIN_JNI_DISABLED
   ifneq ($(filter devenv2 devenv3 devenv4 devenv5 devenv6,$(DEVENV_VERSION_TAG)),)
     ifeq (win, $(findstring win, $(OS)))
       BL_WIN_JNI_DISABLED := 1
     endif
   endif
   endif
   ```

2. **Gradle Path Configuration** (`projects/make/3rd/gradle/latest.mk`)
   ```makefile
   # Use versioned Gradle for devenv7+, legacy path for devenv2-6
   ifneq ($(filter devenv2 devenv3 devenv4 devenv5 devenv6,$(DEVENV_VERSION_TAG)),)
     GRADLE := $(DIST_ROOT_DEPS3)/gradle/latest/default/bin/gradle
   else
     GRADLE_VERSION := $(firstword $(notdir $(wildcard $(DIST_ROOT_DEPS3)/gradle/*)))
     GRADLE := $(DIST_ROOT_DEPS3)/gradle/$(GRADLE_VERSION)/default/bin/gradle
   endif
   ```

3. **Boost.JSON Support** (`projects/make/3rd/boost/common.mk`) - Original reference pattern
   ```makefile
   # Enable json-spirit for devenv2-6, use Boost.JSON for devenv7+
   ifneq ($(filter devenv2 devenv3 devenv4 devenv5 devenv6,$(DEVENV_VERSION_TAG)),)
     BL_USE_BOOST_JSON := 0
   else
     BL_USE_BOOST_JSON := 1
   endif
   ```

### When to Use This Pattern

**Use negative filtering when:**
- ✅ Feature should be enabled **by default** for devenv7+
- ✅ Feature requires different behavior for old devenvs (devenv2-6)
- ✅ You want future devenvs (devenv8+) to automatically get the new behavior

**Use positive checks when:**
- ❌ Feature is **specific to one version only** (rarely needed)
- ❌ Behavior changes every version (use case-by-case logic)

### Best Practices

1. **Always comment your intent:**
   ```makefile
   # Use negative filtering: devenv7+ by default, devenv2-6 explicitly handled
   ifneq ($(filter devenv2 devenv3 devenv4 devenv5 devenv6,$(DEVENV_VERSION_TAG)),)
   ```

2. **Document what each branch does:**
   ```makefile
   ifneq ($(filter devenv2 devenv3 devenv4 devenv5 devenv6,$(DEVENV_VERSION_TAG)),)
     # devenv2-6: Old behavior (explain why it's different)
   else
     # devenv7+: New behavior (explain the improvement)
   endif
   ```

3. **Keep the pattern consistent across the codebase:**
   - Always use the same filter list order: `devenv2 devenv3 devenv4 devenv5 devenv6`
   - Always use `ifneq` (not `ifeq`) with `$(filter ...)`

4. **Reference this pattern in commit messages:**
   ```
   Enable feature X for devenv7+ using negative filtering pattern

   - Uses ifneq($(filter devenv2-6)) pattern for backward compatibility
   - Feature automatically enabled for future devenv versions
   - See CLAUDE.md for pattern documentation
   ```

---

## Windows JNI Support (devenv7+)

### Overview

Starting with **devenv7**, Windows builds of the `utf_baselib_jni` library are fully supported. This enables Java Native Interface (JNI) integration on Windows alongside existing Linux and macOS support.

### Design Rationale

**Historical Context:**
- Prior to devenv7 (devenv2-6), `utf_baselib_jni` was only built on Linux and macOS
- Windows builds were explicitly excluded in the Makefile
- The exclusion was due to differences in signal handling and JNI library linking between Windows and Unix platforms

**devenv7+ Changes:**
- Windows support enabled by default for devenv7+
- Uses negative filtering pattern to maintain backward compatibility
- Leverages Microsoft Build of OpenJDK 25

### Technical Implementation

#### 1. Version Gating Pattern

The implementation uses a **negative filtering pattern** (same approach as Boost.JSON for backward compatibility):

```makefile
# Windows JNI support: disabled for devenv2-6, enabled by default for devenv7+
ifndef BL_WIN_JNI_DISABLED
ifneq ($(filter devenv2 devenv3 devenv4 devenv5 devenv6,$(DEVENV_VERSION_TAG)),)
  # Disable Windows JNI for old devenvs
  ifeq (win, $(findstring win, $(OS)))
    BL_WIN_JNI_DISABLED := 1
  endif
endif
endif
```

**Key Points:**
- `BL_WIN_JNI_DISABLED` flag set for devenv2-6 on Windows only
- Windows enabled **by default** for devenv7+ (no explicit flag needed)
- Maintains backward compatibility - old devenvs unchanged

#### 2. Signal Handling Differences: libjsig vs SEH

**Unix/Linux/macOS:**
- Use POSIX signal handling (SIGINT, SIGTERM, SIGSEGV, etc.)
- Require `libjsig` library for signal chaining between native code and JVM
- Must link against `libjsig.so` (Linux) or `libjsig.dylib` (macOS)
- Configure rpath for runtime library discovery

**Windows:**
- Uses **Structured Exception Handling (SEH)** instead of POSIX signals
- SEH is a Windows-native exception handling mechanism
- Modern JDK builds on Windows **do not require** explicit libjsig linking
- Signal/exception chaining works through SEH automatically
- **No linker flags needed** for Windows JNI builds

**Implementation in Makefile:**

```makefile
ifndef BL_WIN_JNI_DISABLED
  ifeq (win, $(findstring win, $(OS)))
    # Windows: No libjsig linking needed (uses SEH instead of POSIX signals)
    # Runtime loads jvm.dll via PATH (configured in jdk/common.mk)
    # JavaVirtualMachine.h handles dynamic loading from bin/server/jvm.dll
  else ifeq (darwin, $(findstring darwin, $(BL_PROP_PLAT)))
    $(utf_baselib_jni_ARTIFACT): LDADD += $(JAVA_HOME)/lib/server/libjsig.dylib
    $(utf_baselib_jni_ARTIFACT): LDADD += -Wl,-rpath,$(JAVA_HOME)/lib/server
  else ifeq (linux, $(findstring linux, $(BL_PROP_PLAT)))
    $(utf_baselib_jni_ARTIFACT): LDADD += $(JAVA_HOME)/lib/server/libjsig.so
    $(utf_baselib_jni_ARTIFACT): LDADD += -Wl,-rpath,$(JAVA_HOME)/lib/server
  endif
endif
```

#### 3. JDK Directory Structure Differences

**JDK 25 on Windows (devenv7):**
```
${DIST_ROOT_DEPS3}/openjdk/25/
├── a64/                     # ARM64 architecture
│   ├── bin/
│   │   └── server/
│   │       └── jvm.dll      # JVM runtime library
│   ├── include/
│   │   ├── jni.h            # Main JNI header
│   │   └── win32/
│   │       └── jni_md.h     # Windows-specific JNI definitions
│   └── lib/
│       └── (import libraries if needed)
└── x64/                     # x64 architecture
    └── (same structure as a64)
```

**Note:** devenv7 Windows uses architecture-specific paths (`openjdk/25/a64`, `openjdk/25/x64`). x86 architecture is not supported (no JDK 25 available).

**JDK 25 on Linux (devenv7):**
```
${DIST_ROOT_DEPS3}/openjdk/25/
└── default/                 # Shared installation (architecture-specific build)
    ├── lib/
    │   └── server/
    │       ├── libjvm.so    # JVM runtime library
    │       └── libjsig.so   # Signal chaining library
    └── include/
        ├── jni.h
        └── linux/
            └── jni_md.h
```

**Key Differences:**
- Windows uses `bin/server/jvm.dll`, Unix uses `lib/server/libjvm.so`
- Windows has `include/win32/`, Unix has `include/linux/` or `include/darwin/`
- Windows may have `lib/jvm.lib` for static linking (not required for dynamic loading)
- Only Unix requires explicit `libjsig` linking

#### 4. Runtime JVM Loading

The JVM is loaded dynamically at runtime, not linked statically. This is handled in `JavaVirtualMachine.h`.

##### Path Normalization on Windows

**Critical Issue:** On Windows, the `JAVA_HOME` environment variable may contain forward slashes (MSYS/Cygwin format like `C:/Users/...`), but when Boost.Filesystem appends path components using the `/` operator, it uses the native separator (backslash). This creates **mixed separators** like:

```
C:/Users/lazar/swblocks/dist-devenv7-windows-a64/openjdk/25/a64\bin\server\jvm.dll
                                                                  ↑ mixed separators cause fs::exists() to fail
```

**Solution:** Normalize the `JAVA_HOME` base path on Windows before constructing full paths:

```cpp
/*
 * On Windows, normalize the JAVA_HOME path to ensure consistent separators.
 * This is critical because JAVA_HOME may come from environment with forward slashes,
 * but boost::filesystem uses backslashes on Windows, causing mixed separators
 * which breaks fs::exists() checks.
 */
const fs::path javaHomeBase = os::onWindows()
    ? fs::normalize( fs::path( *javaHome ) )
    : fs::path( *javaHome );
```

This ensures the entire path uses consistent backslashes on Windows:
```
C:\Users\lazar\swblocks\dist-devenv7-windows-a64\openjdk\25\a64\bin\server\jvm.dll
```

##### Path Discovery Logic

**Windows Path Candidates (JavaVirtualMachine.h:199-203):**
```cpp
if( os::onWindows() )
{
    jvmPathCandidates.push_back( javaHomeBase / "bin" / "server" / "jvm.dll" );
    jvmPathCandidates.push_back( javaHomeBase / "jre" / "bin" / "server" / "jvm.dll" );
}
```

**Linux Path Candidates (JavaVirtualMachine.h:220-243):**
```cpp
// JDK 25+ structure - no arch subdirectory
jvmPathCandidates.push_back( javaHomeBase / "lib" / "server" / "libjvm.so" );

// JDK 9-24 structure - with arch subdirectory
const auto archDir = BuildInfo::arch == "a64" ? "aarch64" : "amd64";
jvmPathCandidates.push_back( javaHomeBase / "lib" / archDir / "server" / "libjvm.so" );

// JDK 8 structure - old jre/lib/<arch>/server
jvmPathCandidates.push_back( javaHomeBase / "jre" / "lib" / archDir / "server" / "libjvm.so" );
```

**macOS Path Candidates (JavaVirtualMachine.h:204-218):**
```cpp
// macOS JDK 9+ structure
jvmPathCandidates.push_back( javaHomeBase / "lib" / "server" / "libjvm.dylib" );

// macOS JDK 8 structure
const auto archDir = BuildInfo::arch == "a64" ? "aarch64" : "amd64";
jvmPathCandidates.push_back( javaHomeBase / "jre" / "lib" / archDir / "server" / "libjvm.dylib" );
```

##### JVM Path Detection Evolution

**JDK 25 (devenv7) - Modern Structure:**
- **Linux:** `lib/server/libjvm.so` (no arch subdirectory)
- **macOS:** `lib/server/libjvm.dylib` (no arch subdirectory)
- **Windows:** `bin/server/jvm.dll` (no arch subdirectory)

**JDK 9-24 - Transitional Structure:**
- **Linux:** `lib/{aarch64|amd64}/server/libjvm.so` (arch-specific)
- **macOS:** `lib/{aarch64|amd64}/server/libjvm.dylib` (arch-specific)
- **Windows:** `bin/server/jvm.dll` (no arch subdirectory)

**JDK 8 (devenv2-6) - Legacy Structure:**
- **Linux:** `jre/lib/{aarch64|amd64|i386}/server/libjvm.so`
- **macOS:** `jre/lib/{aarch64|amd64|i386}/server/libjvm.dylib`
- **Windows:** `jre/bin/server/jvm.dll`

The code checks all candidate paths in order and uses the first one that exists.

##### Loading Mechanism

**Dynamic Library Loading:**
- **Windows:** Uses `os::loadLibrary()` → `LoadLibrary()` Win32 API
- **Unix:** Uses `os::loadLibrary()` → `dlopen()` POSIX API

**Runtime Library Discovery:**
- **Windows:**
  - PATH environment variable configured by `jdk/common.mk` includes `${JAVA_HOME}/bin`
  - PATH is **explicitly exported** in makefiles to ensure child processes (test executables) inherit it
  - `LoadLibrary()` searches PATH for dependent DLLs (jvm.dll dependencies)
- **Unix:**
  - Rpath configured in linker flags points to `${JAVA_HOME}/lib/server`
  - Dynamic linker uses rpath for runtime library discovery

**Export PATH for Windows:**
In `projects/make/3rd/jdk/common.mk`:
```makefile
PATH := $(JAVA_HOME)/bin:$(PATH)
export PATH  # Critical: ensures test executables can find jvm.dll
```

In `projects/make/toolchain/msvc-default.mk`:
```makefile
export PATH  # Ensures modified PATH is inherited by child processes
```

Without `export PATH`, test executables would fail with error code 200 (library not found) because `LoadLibrary()` wouldn't be able to locate `jvm.dll` and its dependencies.

#### 5. Architecture Support

Windows JNI builds support ARM64 and x64 architectures in devenv7:

| Architecture | Makefile ARCH | Windows MSVC Path Suffix | JDK Path (devenv7) | JNI Support |
|--------------|---------------|--------------------------|---------------------|-------------|
| ARM64        | `a64`         | `arm64`                  | `openjdk/25/a64`    | ✅ Supported |
| x64          | `x64`         | `x64`                    | `openjdk/25/x64`    | ✅ Supported |
| x86          | `x86`         | `x86`                    | `openjdk/25/x86`    | ❌ Not Available |

**JDK Distribution:**
- Microsoft Build of OpenJDK 25 available for ARM64 and x64
- x86 is **not supported** - no JDK 25 available for this architecture
- Each architecture requires a separate OpenJDK installation in architecture-specific directories
- Build system auto-detects JDK using architecture-specific paths (`openjdk/25/{arch}`)
- If JDK not found for an architecture, JNI is automatically disabled for that build

**Architecture Detection (from platform.mk):**
```makefile
# Detect from dist root path (e.g., "dist-devenv7-windows-hostarch-a64-targets-a64-x64-x86")
ifneq ($(findstring -a64,$(DIST_ROOT_DEPS3)),)
  BL_WIN_ARCH_IS_ARM64 := 1
  ARCH := a64
else ifneq ($(findstring -x64,$(DIST_ROOT_DEPS3)),)
  BL_WIN_ARCH_IS_X64 := 1
  ARCH := x64
else ifneq ($(findstring -x86,$(DIST_ROOT_DEPS3)),)
  ARCH := x86
endif
```

### Dependencies

#### JDK Configuration (`projects/make/3rd/jdk/common.mk`)

The JDK is auto-detected and configured based on devenv version:

```makefile
# Determine JDK version and path based on devenv
ifeq ($(DEVENV_VERSION_TAG),devenv7)
JDK_VERSION = 25
# devenv7 uses architecture-specific paths on Windows (openjdk/25/a64, openjdk/25/x64)
# Linux/macOS use single default path (openjdk/25/default)
ifeq (win, $(findstring win, $(OS)))
  JDK_BASE_PATH = $(DIST_ROOT_DEPS3)/openjdk/$(JDK_VERSION)/$(ARCH)
else
  JDK_BASE_PATH = $(DIST_ROOT_DEPS3)/openjdk/$(JDK_VERSION)/default
endif
else
JDK_VERSION = 8
# Older devenvs use jdk/open-jdk/8/<os>-<arch> structure
ARCH_JDK = $(OS)-$(ARCH)
JDK_BASE_PATH = $(DIST_ROOT_DEPS3)/jdk/open-jdk/$(JDK_VERSION)/$(ARCH_JDK)
endif

# Auto-enable JNI if JDK found
ifneq ("$(wildcard $(JDK_BASE_PATH))","")
  BL_JNI_ENABLED   := 1
  JAVA_HOME        := $(JDK_BASE_PATH)
  PATH            := $(JAVA_HOME)/bin:$(PATH)

  # Configure include paths
  INCLUDE         += $(JAVA_HOME)/include
  ifeq (windows, $(findstring windows, $(BL_PROP_PLAT)))
    INCLUDE       += $(JAVA_HOME)/include/win32
  endif
endif
```

**Key Points:**
- `BL_JNI_ENABLED` flag automatically set if JDK found
- `JAVA_HOME` points to architecture-specific JDK installation

#### Gradle Configuration (`projects/make/3rd/gradle/latest.mk`)

Gradle paths differ between devenv versions. The configuration uses the negative filtering pattern to support future devenv versions automatically:

```makefile
# Determine Gradle path based on devenv version
# Use negative filtering: devenv7+ by default, devenv2-6 explicitly handled
ifneq ($(filter devenv2 devenv3 devenv4 devenv5 devenv6,$(DEVENV_VERSION_TAG)),)
  # devenv2-6: Use gradle/latest/default structure
  GRADLE := $(DIST_ROOT_DEPS3)/gradle/latest/default/bin/gradle
else
  # devenv7+: Use versioned gradle directory (e.g., gradle/9.2.1/default)
  GRADLE_VERSION := $(firstword $(notdir $(wildcard $(DIST_ROOT_DEPS3)/gradle/*)))
  GRADLE := $(DIST_ROOT_DEPS3)/gradle/$(GRADLE_VERSION)/default/bin/gradle
endif
```

**Directory Structure:**
- devenv7+: `${DIST_ROOT_DEPS3}/gradle/9.2.1/default/bin/gradle` (auto-detected version)
- devenv2-6: `${DIST_ROOT_DEPS3}/gradle/latest/default/bin/gradle` (legacy path)

**Auto-Detection:**
- Uses `wildcard` to detect Gradle version dynamically (e.g., `9.2.1`)
- Future Gradle upgrades work automatically without code changes
- devenv8+ will automatically use the new structure

#### JDK Download Scripts

Windows JDK downloads are handled by PowerShell scripts in `scripts/devenv7/windows/internal/download-tools.ps1`:

```powershell
OpenJDK = @{
    BaseUrl = "https://aka.ms/download-jdk"
    VersionPattern = ""
    FilePatterns = @{
        arm64 = "microsoft-jdk-{VERSION}.0.1-windows-aarch64.zip"
        x64   = "microsoft-jdk-{VERSION}.0.1-windows-x64.zip"
    }
}
```

**Download URLs:**
- ARM64: `https://aka.ms/download-jdk/microsoft-jdk-25.0.1-windows-aarch64.zip`
- x64: `https://aka.ms/download-jdk/microsoft-jdk-25.0.1-windows-x64.zip`

### Testing

#### Verifying the Build

**1. Check if JNI is enabled:**

```cmd
# Load environment
C:\Users\lazar\swblocks\dist-devenv7-windows-hostarch-a64-targets-a64-x64-x86\scripts\ci\setup-env-a64.bat

# Build with verbose output
cd C:\Users\lazar\dev\github\swblocks-baselib
make -k -j1 utf_baselib_jni

# Look for these lines in output:
# Building with BL_JNI_ENABLED = 1
# Building with JAVA_HOME = /c/Users/lazar/swblocks/dist-devenv7-windows-hostarch-a64-targets-a64-x64-x86/jdk/open-jdk/25/win-a64
# Building with JDK_VERSION = 25
```

**2. Verify build artifacts:**

After successful build, check for:
```
bld/win-a64-vc143-debug/utests/utf_baselib_jni/
├── utf_baselib_jni.exe              # Test executable
├── utf_baselib_jni.pdb              # Debug symbols
└── utf-baselib-jni-lib/
    └── utf_baselib_jni.jar          # Java JAR file
```

**3. Run the tests:**

```cmd
# The JNI tests should now run on Windows
cd bld/win-a64-vc143-debug/utests/utf_baselib_jni
utf_baselib_jni.exe
```

#### Platform Compatibility Matrix

| Platform | devenv2-6 | devenv7+ |
|----------|-----------|----------|
| Linux (a64, x64, x86) | ✅ Supported | ✅ Supported |
| macOS (a64, x64) | ✅ Supported | ✅ Supported |
| Windows (a64, x64, x86) | ❌ **Excluded** | ✅ **Supported** |

### Future Considerations

#### Adding devenv8+

When adding future devenv versions, ensure the negative filter is updated:

```makefile
# In projects/make/utests/utf_baselib_jni/Makefile
ifneq ($(filter devenv2 devenv3 devenv4 devenv5 devenv6,$(DEVENV_VERSION_TAG)),)
  # Add devenv8 here if Windows should NOT be supported
  # If Windows SHOULD be supported in devenv8, DO NOT add it to this list
```

**Recommendation:** Keep Windows enabled by default for devenv7+ unless there's a specific reason to disable it.

#### JDK Version Upgrades

When upgrading JDK versions (e.g., JDK 26+):

1. Update `projects/make/3rd/jdk/common.mk` JDK_VERSION mapping
2. Update download scripts in `scripts/devenv7/windows/internal/download-tools.ps1`
3. Verify directory structure matches expectations (`bin/server/jvm.dll`)
4. Test on all supported architectures

#### Troubleshooting

This section documents issues encountered during Windows JNI implementation and their solutions. All issues listed here have been resolved in the current codebase.

##### 1. Test Fails with Error Code 200 - "Library Not Found" ✅ FIXED

**Symptoms:**
- Test executable exits immediately with error code 200
- Log shows "Test is aborted" without detailed error messages
- No JVM logging appears

**Cause:** The test executable cannot find `jvm.dll` or its dependencies at runtime.

**Root Cause:** PATH environment variable not exported to child processes (test executables).

**Status:** ✅ **FIXED** - PATH is now explicitly exported in both locations.

**Implementation:**

In [projects/make/3rd/jdk/common.mk:40-41](projects/make/3rd/jdk/common.mk#L40-L41):
```makefile
export JAVA_HOME
export PATH  # Ensures test executables can find jvm.dll
```

In [projects/make/toolchain/msvc-default.mk:214](projects/make/toolchain/msvc-default.mk#L214):
```makefile
# Export PATH so child processes (like test executables) inherit it
export PATH
```

**Verification:**
```cmd
# From MSYS shell, verify PATH includes JAVA_HOME/bin
echo $PATH | grep -o "openjdk/25/default/bin"
```

##### 2. Test Fails with "Test setup error" - JVM Path Not Found ✅ FIXED

**Symptoms:**
- Debug logs show: `Checking JVM path candidate: "..." (exists: no)`
- All path candidates fail the existence check
- Paths have mixed separators like `C:/Users/.../default\bin\server\jvm.dll`

**Cause:** Mixed path separators (forward and backward slashes) prevent `fs::exists()` from finding the file.

**Root Cause:** `JAVA_HOME` environment variable contains forward slashes (MSYS/Cygwin format), but Boost.Filesystem appends path components with native separators (backslashes on Windows).

**Status:** ✅ **FIXED** - Path normalization implemented in [JavaVirtualMachine.h:195-197](src/include/baselib/jni/JavaVirtualMachine.h#L195-L197).

**Implementation:**

```cpp
const fs::path javaHomeBase = os::onWindows()
    ? fs::normalize( fs::path( *javaHome ) )
    : fs::path( *javaHome );
```

This ensures consistent backslashes throughout the constructed path on Windows.

**Verification:** Run test and check debug logs for:
```
DEBUG: Checking JVM path candidate: "C:\Users\...\openjdk\25\a64\bin\server\jvm.dll" (exists: yes)
                                       ↑ all backslashes, exists: yes
```

**Before Fix (wrong):**
```
C:/Users/.../a64\bin\server\jvm.dll  ← mixed separators cause failure
```

**After Fix (correct):**
```
C:\Users\...\a64\bin\server\jvm.dll  ← consistent separators work
```

##### 3. JNI Not Enabled for Windows Build

**Symptoms:**
- Build output shows `BL_JNI_ENABLED = 0` or no JNI messages
- `utf_baselib_jni` target not built on Windows

**Cause:** Either JDK not found, or `BL_WIN_JNI_DISABLED` flag incorrectly set.

**Solutions:**

a) Verify JDK installation:
```cmd
# Check if JDK exists at expected path for your architecture
# For ARM64:
dir %DIST_ROOT_DEPS3%\openjdk\25\a64\bin\server\jvm.dll
# For x64:
dir %DIST_ROOT_DEPS3%\openjdk\25\x64\bin\server\jvm.dll
```

b) Check devenv version gating:
```makefile
# In projects/make/utests/utf_baselib_jni/Makefile
# Ensure devenv7 is NOT in the disabled list:
ifneq ($(filter devenv2 devenv3 devenv4 devenv5 devenv6,$(DEVENV_VERSION_TAG)),)
  # devenv7 should fall into the else branch
```

c) Verify DEVENV_VERSION_TAG:
```cmd
make -k -j1 utf_baselib_jni | grep "DEVENV_VERSION_TAG"
# Should show: devenv7
```

##### 4. JVM Warnings About Exception Checking

**Symptoms:**
- Test runs successfully but shows warnings like:
  ```
  WARNING in native method: JNI call made without checking exceptions when required to from CallObjectMethodV
  ```

**Cause:** JVM running with `-Xcheck:jni` option (enabled by default in debug builds).

**Impact:** These are expected warnings during exception handling tests. They do not indicate test failure.

**Action:** These warnings are informational and part of the test validation. No action needed unless tests actually fail.

##### 5. JDK Not Found

**Symptoms:**
- Build output shows no `JAVA_HOME` or `BL_JNI_ENABLED` messages
- JDK path not detected

**Solutions:**

a) Check JDK installation location for devenv7:
```cmd
# Should exist for your target architecture:
# For ARM64:
dir %DIST_ROOT_DEPS3%\openjdk\25\a64
# For x64:
dir %DIST_ROOT_DEPS3%\openjdk\25\x64
# For x86: Not supported (no JDK available)
```

b) Verify devenv version detection:
```cmd
# From build directory
make -k -j1 | grep "DEVENV_VERSION_TAG"
```

c) Check architecture detection:
```cmd
# Should match your build architecture
make -k -j1 | grep "ARCH ="
# Should show: a64, x64, or x86
```

##### 6. Wrong JVM Architecture

**Symptoms:**
- Test fails with "incompatible architecture" or similar error
- Building for ARM64 but JDK is x64, or vice versa

**Cause:** JDK architecture doesn't match build architecture.

**Solution:**
- For ARM64 builds: Install Microsoft Build of OpenJDK 25 for ARM64 to `${DIST_ROOT_DEPS3}/openjdk/25/a64`
- For x64 builds: Install Microsoft Build of OpenJDK 25 for x64 to `${DIST_ROOT_DEPS3}/openjdk/25/x64`
- Each architecture requires its own separate directory

**Verification:**
```cmd
# Check JVM architecture for ARM64:
file %DIST_ROOT_DEPS3%\openjdk\25\a64\bin\server\jvm.dll
# Check JVM architecture for x64:
file %DIST_ROOT_DEPS3%\openjdk\25\x64\bin\server\jvm.dll
```

##### Debugging Tips

**Enable Debug Logging:**

The code includes comprehensive debug logging at level 5 (DEBUG). To see JVM loading details:

```cmd
# Run test with debug logging enabled (default in debug builds)
cd bld\win-a64-vc143-debug\utests\utf_baselib_jni
utf_baselib_jni.exe --log_level=all

# Check log file for detailed output
type ..\..\..\utflogs\utf_baselib_jni.log
```

**Key Log Messages to Look For:**

1. JAVA_HOME detection:
   ```
   DEBUG: JAVA_HOME = 'C:/Users/.../openjdk/25/default'
   ```

2. Path candidate checks:
   ```
   DEBUG: Checking JVM path candidate: "..." (exists: yes)
   DEBUG: Found JVM at: "..."
   ```

3. JVM library loading:
   ```
   DEBUG: Loading JVM library from: ...
   DEBUG: JVM library loaded successfully, getting JNI_CreateJavaVM address
   ```

4. JVM creation:
   ```
   DEBUG: Creating JVM with N options:
   DEBUG: -Djava.class.path=...
   ```

**Manual Test Verification:**

```cmd
# Verify JVM can be loaded manually
cd %JAVA_HOME%\bin\server
java -version  # Should work if PATH is correct
```

---

## Additional Resources

- **JDK Configuration:** `projects/make/3rd/jdk/common.mk`
- **JNI Test Makefile:** `projects/make/utests/utf_baselib_jni/Makefile`
- **JVM Loading Logic:** `src/include/baselib/jni/JavaVirtualMachine.h`
- **Platform Detection:** `projects/make/platform.mk`
- **Devenv Detection:** `projects/make/devenv-detect.mk`

---

**Document Version:** 1.8
**Last Updated:** 2026-02-06
**devenv Version:** 7

**Changelog:**
- v1.8 (2026-02-06): Added Core Principles for Python virtual environment usage and git commit restrictions. All Python commands must use the project's .venv virtual environment (create with `make python-install` if missing). Git commits are forbidden without explicit user permission.
- v1.7 (2026-02-06): Added "Code Review Hygiene and File Modification Guidelines" section. Documents critical rules for incremental file changes, proper use of Edit vs Write tools, separation of logic and style changes, and git diff review best practices. Prevents common mistakes like rewriting entire files or mixing unrelated changes in the same PR.
- v1.6 (2026-01-23): Changed host architecture detection to use PROCESSOR_IDENTIFIER (first priority), PROCESSOR_ARCHITECTURE, and PROCESSOR_ARCHITEW6432 environment variables. This provides robust detection across all environments including cmd.exe, PowerShell, and MSYS2 shells. PROCESSOR_IDENTIFIER check is critical for MSYS2 x64 binaries running under emulation on ARM64 where PROCESSOR_ARCHITEW6432 is not set.
- v1.5 (2026-01-22): Added comprehensive ARCH parameter implementation details section documenting environment variable handling, lazy evaluation patterns, host vs target architecture separation, and common pitfalls with solutions
- v1.4 (2026-01-22): Fixed ARCH parameter handling to support cross-compilation - ARCH now overrides auto-detection, documented cross-compilation workflow, clarified that setup scripts are optional
- v1.3 (2026-01-21): Changed to architecture-specific JDK paths for Windows (openjdk/25/a64, openjdk/25/x64), documented that x86 is not supported
- v1.2 (2026-01-19): Updated troubleshooting section to reflect all issues as resolved, clarified that PATH export and path normalization are production fixes
- v1.1 (2026-01-19): Added comprehensive Windows JNI troubleshooting section, documented path normalization fix for mixed separators, detailed JVM loading mechanism and PATH export requirements
- v1.0 (2026-01-19): Initial documentation of Windows JNI support for devenv7+
