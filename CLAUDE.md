# swblocks-baselib Development Guide

This document contains design decisions, implementation details, and development workflows for the swblocks-baselib project.

## Table of Contents

- [Build Commands](#build-commands)
- [Development Environment Setup](#development-environment-setup)
- [devenv Version Gating Pattern](#devenv-version-gating-pattern)
- [Windows JNI Support (devenv7+)](#windows-jni-support-devenv7)

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

Before running make commands on Windows, you must load the development environment setup script:

```cmd
# For ARM64 architecture
C:\Users\lazar\swblocks\dist-devenv7-windows-hostarch-a64-targets-a64-x64-x86\scripts\ci\setup-env-a64.bat

# For x64 architecture
C:\Users\lazar\swblocks\dist-devenv7-windows-hostarch-a64-targets-a64-x64-x86\scripts\ci\setup-env-x64.bat

# For x86 architecture
C:\Users\lazar\swblocks\dist-devenv7-windows-hostarch-a64-targets-a64-x64-x86\scripts\ci\setup-env-x86.bat
```

**Why is this required?**
- The setup script configures PATH to include MSYS2 tools (make, bash, etc.)
- Sets up MSVC compiler and linker paths
- Configures JDK paths for JNI builds
- Sets required environment variables for the build system

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

**Document Version:** 1.3
**Last Updated:** 2026-01-21
**devenv Version:** 7

**Changelog:**
- v1.3 (2026-01-21): Changed to architecture-specific JDK paths for Windows (openjdk/25/a64, openjdk/25/x64), documented that x86 is not supported
- v1.2 (2026-01-19): Updated troubleshooting section to reflect all issues as resolved, clarified that PATH export and path normalization are production fixes
- v1.1 (2026-01-19): Added comprehensive Windows JNI troubleshooting section, documented path normalization fix for mixed separators, detailed JVM loading mechanism and PATH export requirements
- v1.0 (2026-01-19): Initial documentation of Windows JNI support for devenv7+
