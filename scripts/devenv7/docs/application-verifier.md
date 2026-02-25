# Application Verifier Guide for swblocks-baselib

## Overview

This guide explains how to run swblocks-baselib tests under Microsoft Application Verifier, a runtime verification tool that detects memory corruption, handle leaks, lock errors, and other runtime issues in Windows applications.

**Key Point:** The codebase already has built-in Application Verifier support via the `BL_ANALYSIS_TESTING` environment variable. This enables automatic workarounds for known false positives in external libraries.

---

## What is Application Verifier?

**Application Verifier (appverif.exe)** is a Microsoft runtime verification tool for Windows that detects:

- **Memory corruption** - Heap overruns, buffer overflows, use-after-free, double-free
- **Handle issues** - Handle leaks, invalid handle usage, double-close
- **Synchronization errors** - Lock usage errors, potential deadlocks, race conditions
- **Thread pool problems** - Incorrect thread pool API usage
- **DLL loading issues** - DLL load/unload memory leaks and corruption

It's more aggressive than standard debugging tools. It inserts runtime instrumentation that catches bugs that wouldn't necessarily crash in normal execution, making it valuable for finding subtle memory safety issues.

---

## Installation

### Check if Already Installed

Application Verifier is installed into `System32` alongside its shim DLLs:

```cmd
where appverif
REM Expected: C:\Windows\System32\appverif.exe
```

### If Not Installed

Application Verifier is an optional component of the [Windows SDK](https://developer.microsoft.com/en-us/windows/downloads/windows-sdk/). Run the SDK installer and select **"Application Verifier"** from the list of optional features. Once installed, `appverif.exe` and its shim DLLs will be placed in `System32`.

---

## Quick Start

### 1. Build Tests

```bash
# Build debug variant (Application Verifier works best with debug builds)
make -k -j4

# Verify test executable exists
dir bld\debug\bin\utf_baselib_tests.exe
```

### 2. Enable Application Verifier

**GUI Method (Recommended for first-time setup):**

```cmd
appverif.exe
```

1. Click **"Add Application"** button
2. Navigate to and select: `C:\Users\lazar\dev\github\swblocks-baselib\bld\debug\bin\utf_baselib_tests.exe`
3. Select verification checks (see [Recommended Configurations](#recommended-configurations))
4. Click **"Save"**

**Command-Line Method:**

```cmd
# Enable basic verification
appverif -enable Heaps Locks Handles Memory -for utf_baselib_tests.exe

# Enable with full page heap (more thorough)
appverif -enable Heaps -with PageHeap=Full -for utf_baselib_tests.exe
```

### 3. Run Tests with Workarounds Enabled

```cmd
# CRITICAL: Set this environment variable to enable workarounds for known false positives
set BL_ANALYSIS_TESTING=1

# Run all tests
bld\debug\bin\utf_baselib_tests.exe

# Or run specific test suite
bld\debug\bin\utf_baselib_tests.exe --run_test=baselib_tests/os_tests
```

### 4. Review Results

If Application Verifier detects issues:
- The application will break into the debugger (if attached) or show a dialog
- Logs are written to: `%LOCALAPPDATA%\Microsoft\Application Verifier\Logs\`

**View logs:**

```cmd
# Open Application Verifier log viewer
appverif -r

# Or manually browse logs
explorer %LOCALAPPDATA%\Microsoft\Application Verifier\Logs\
```

---

## Automated Testing via Make

The build system supports automated Application Verifier integration. This enables appverif for each test binary before execution, runs the test with workarounds enabled, and disables appverif after completion.

### Prerequisites

- **Windows only** — the macros error out on non-Windows platforms
- **Elevated (Administrator) command prompt** — appverif requires admin privileges; the build validates this at parse time and errors out if not elevated

### Basic Usage

```bash
# Run all tests with Application Verifier (default: full page heap)
make test BL_APP_VERIFIER_ENABLED=1

# Run a single test target
make test_utf_baselib BL_APP_VERIFIER_ENABLED=1
```

### Custom Verification Checks

Two macros control the appverif configuration:

| Macro | Default | Description |
|-------|---------|-------------|
| `BL_APP_VERIFIER_CHECKS` | `Handles Locks Memory` | Additional checks to enable (Heaps is always enabled regardless) |
| `BL_APP_VERIFIER_PAGE_HEAP` | `Full` | Page heap mode: `Full` (default) or `Light` |
| `BL_APP_VERIFIER_PAGE_HEAP_BACKWARD` | _(not set)_ | Set to `1` to enable underflow detection (`Heaps.Backward=true`) |

```bash
# Light page heap (faster, fewer false positives)
make test BL_APP_VERIFIER_ENABLED=1 BL_APP_VERIFIER_PAGE_HEAP=Light

# Heaps only (no extra checks), full page heap
make test BL_APP_VERIFIER_ENABLED=1 BL_APP_VERIFIER_CHECKS=""

# Heaps + Handles only, light page heap
make test BL_APP_VERIFIER_ENABLED=1 BL_APP_VERIFIER_CHECKS="Handles" BL_APP_VERIFIER_PAGE_HEAP=Light

# Full page heap with underflow (backward overrun) detection
make test BL_APP_VERIFIER_ENABLED=1 BL_APP_VERIFIER_PAGE_HEAP_BACKWARD=1
```

### What Happens Automatically

For each test binary, the build system:

1. **Validates** platform (Windows) and admin privileges at makefile parse time
2. **Enables** appverif: `appverif -enable Heaps <BL_APP_VERIFIER_CHECKS> -for <binary>.exe -with Heaps.Full=true|false [Heaps.Backward=true]`
3. **Runs** the test with `BL_ANALYSIS_TESTING=1` set in the environment (activates all false-positive workarounds)
4. **Disables** appverif: `appverif -disable * -for <binary>.exe`

### Debug Harness Interaction

Tests run through the debug harness ([scripts/debug_harness.py](../../debug_harness.py)) as **plain processes** — no debugger is attached. This means:

- Application Verifier hooks in at process load time via IFEO registry and works correctly
- Violations crash the process → Windows Error Reporting writes a dump → the debug harness analyzes it post-mortem with `cdb -c '!analyze -v'`
- First-chance breaks (like the `apphelp.dll` issue) are silently swallowed without a debugger, which is beneficial for automated runs

---

## Known False Positives & Workarounds

When `BL_ANALYSIS_TESTING=1` is set, the following workarounds are **automatically enabled** to avoid false positives in external libraries:

| Issue | Root Cause | Automatic Workaround | Code Location |
|-------|------------|----------------------|---------------|
| **Boost.Filesystem heap corruption** | Boost's string-to-wide-character conversion triggers heap corruption detection with page heap | Custom `PathImplT` constructors that perform safe conversion | [OSImplPlatformCommon.h:1238](../../../src/include/baselib/core/detail/OSImplPlatformCommon.h#L1238) |
| **OpenSSL DLL unload leak** | OpenSSL dynamically loads/unloads `wkscli.dll` and `netapi32.dll`, which have memory allocations not freed before unload | Pin DLLs in memory so they're never unloaded | [CryptoBase.h:514](../../../src/include/baselib/crypto/CryptoBase.h#L514) |
| **GetUserNameW/GetUserNameExW break** | Windows user name APIs trigger false positives when page heap with unaligned flag is enabled | Use `USERNAME` environment variable instead of Win32 API | [OSImplWindows.h:1875](../../../src/include/baselib/core/detail/OSImplWindows.h#L1875) |
| **CreateProcess apphelp.dll break** | `CreateProcess` triggers breaks in `apphelp.dll` with unaligned page heap | Skip test entirely when analysis is enabled | [TestBaselibDefault.h:2255](../../../src/utests/utf_baselib/TestBaselibDefault.h#L2255) |

### Why These Workarounds Exist

All documented workarounds address **false positives in external code**, not bugs in swblocks-baselib:

- **Boost.Filesystem** has a known bug in wide character conversion that's only exposed under Application Verifier
- **OpenSSL** intentionally uses dynamic DLL loading with memory that outlives the DLL's lifetime
- **Windows APIs** (`GetUserNameW`, `CreateProcess`) trigger spurious breaks, especially with unaligned page heap enabled

**Important:** If you encounter Application Verifier breaks in swblocks-baselib code (not external libraries), these may be real bugs and should be investigated.

---

## Recommended Configurations

### Daily Testing (Fewer False Positives)

Best for regular development testing. Enables basic checks with light page heap:

```cmd
# Configure Application Verifier
appverif -enable Heaps Handles Locks -with PageHeap=Light -for utf_baselib_tests.exe

# Run tests
set BL_ANALYSIS_TESTING=1
bld\debug\bin\utf_baselib_tests.exe
```

**Verification checks:**
- ✅ Heaps (light page heap)
- ✅ Handles
- ✅ Locks
- ❌ Memory (skip to reduce false positives)
- ❌ TLS, DangerousAPIs, Threadpool (optional, can enable if needed)

### Thorough Analysis (More Comprehensive)

Use before major releases or when investigating suspected memory issues. More false positives expected:

```cmd
# Configure Application Verifier with full page heap
appverif -enable Heaps Handles Locks Memory -with PageHeap=Full -for utf_baselib_tests.exe

# Run tests
set BL_ANALYSIS_TESTING=1
bld\debug\bin\utf_baselib_tests.exe
```

**Verification checks:**
- ✅ Heaps (full page heap)
- ✅ Handles
- ✅ Locks
- ✅ Memory
- ❌ Unaligned page heap flag (causes many false positives - avoid unless specifically testing alignment issues)

### Page Heap Settings

Application Verifier supports two page heap modes:

- **Light Page Heap** (default) - Faster, fewer false positives, catches most issues
- **Full Page Heap** - Slower, more thorough, catches alignment issues but triggers more false positives

**⚠️ Warning:** Enabling **unaligned page heap flag** causes significant false positives in Windows APIs and Boost.Filesystem. Only enable if specifically investigating alignment issues.

---

## Disable Application Verifier

To remove verification settings for the test executable:

```cmd
# Disable all checks
appverif -disable * -for utf_baselib_tests.exe

# Or completely delete settings
appverif -delete settings -for utf_baselib_tests.exe

# Verify settings removed
appverif -export log -for utf_baselib_tests.exe
```

---

## Troubleshooting

### Tests Crash Immediately or Fail with External Library Errors

**Symptom:** Tests crash or fail immediately when Application Verifier is enabled, often with errors in `kernel32.dll`, `ntdll.dll`, `wkscli.dll`, or `apphelp.dll`.

**Solution:**
1. Ensure `BL_ANALYSIS_TESTING=1` is set before running tests
2. Verify you're running the correct executable (the one Application Verifier is configured for)

```cmd
# Check which executable is configured
appverif -export log -for utf_baselib_tests.exe

# Ensure environment variable is set
echo %BL_ANALYSIS_TESTING%

# Should output: 1
```

### Too Many Breaks in External DLLs (Boost, OpenSSL, Windows APIs)

**Symptom:** Frequent breaks in `boost_filesystem.dll`, `libssl.dll`, `wkscli.dll`, `netapi32.dll`, or Windows system DLLs.

**Solutions:**
1. Use **Light Page Heap** instead of **Full Page Heap**
2. **Disable unaligned page heap flag** if enabled
3. Ensure `BL_ANALYSIS_TESTING=1` is set (enables automatic workarounds)

```cmd
# Reconfigure with light page heap
appverif -disable * -for utf_baselib_tests.exe
appverif -enable Heaps Handles Locks -with PageHeap=Light -for utf_baselib_tests.exe
```

### Specific Test Always Fails Under Application Verifier

**Symptom:** A particular test fails reliably when Application Verifier is enabled but passes in normal execution.

**Check if it's a known false positive:**
- Review the [Known False Positives](#known-false-positives--workarounds) table
- Check if `BL_ANALYSIS_TESTING=1` is set

**If it's a new false positive:**
1. Investigate whether it's a real bug or external library issue
2. If confirmed as external false positive, consider adding a workaround similar to existing ones
3. If unsure, run with `--run_test` filter to skip the problematic test temporarily:

```cmd
# Skip specific test
bld\debug\bin\utf_baselib_tests.exe --run_test=!problematic_test_name
```

### Want to Test Only Specific Functionality

**Symptom:** Application Verifier applies to the entire executable, making it hard to isolate specific areas.

**Solution:** Use Boost.Test's test filtering to run only specific tests:

```cmd
# Run only crypto tests
bld\debug\bin\utf_baselib_tests.exe --run_test=baselib_tests/crypto_tests

# Run only specific test case
bld\debug\bin\utf_baselib_tests.exe --run_test=baselib_tests/os_tests/test_process_creation

# List all available tests
bld\debug\bin\utf_baselib_tests.exe --list_content
```

### Application Verifier Slows Down Tests Significantly

**Symptom:** Tests take 10x-100x longer to run under Application Verifier.

**Expected Behavior:** This is normal. Application Verifier adds significant runtime overhead.

**Solutions:**
1. Use **Light Page Heap** instead of Full Page Heap (faster)
2. Run only critical test suites, not the entire test suite
3. Run Application Verifier testing on a subset of platforms (e.g., only x64, not ARM64)
4. Use Application Verifier selectively before releases, not in daily development

---

## Technical Details: How BL_ANALYSIS_TESTING Works

### Test Infrastructure

The test framework infrastructure that supports Application Verifier mode:

**UtfArgsParser** ([src/utests/include/utests/baselib/UtfArgsParser.h](../../../src/utests/include/utests/baselib/UtfArgsParser.h)):

1. **Command-line option** (line 334):
   ```cpp
   ( "analysis-enabled", "Runtime analysis, i.e. appverif, is enabled (on Windows only)" )
   ```

2. **Environment variable check** (lines 456-460):
   ```cpp
   if( vm.count( "analysis-enabled" )
       || bl::os::tryGetEnvironmentVariable( "BL_ANALYSIS_TESTING" ) )
   {
       g_isAnalysisEnabled = true;
   }
   ```

3. **Static flag** (line 572):
   ```cpp
   BL_DEFINE_STATIC_MEMBER( UtfArgsParserBaseT, bool, g_isAnalysisEnabled ) = false;
   ```

### Runtime Checks in Code

Code throughout the codebase checks whether analysis mode is enabled and adjusts behavior:

**Preferred method:**
```cpp
if( test::UtfArgsParser::isAnalysisEnabled() )
{
    // Skip test or enable workaround
}
```

**Direct environment variable check:**
```cpp
if( OSImplBase::tryGetEnvironmentVariable( "BL_ANALYSIS_TESTING" ) )
{
    // Enable workaround
}
```

### Two Ways to Enable

| Method | How to Use | Scope |
|--------|-----------|-------|
| **Environment Variable** (Recommended) | `set BL_ANALYSIS_TESTING=1` before running tests | All test code that checks the env var or `isAnalysisEnabled()` |
| **Command-line Flag** | `utf_baselib_tests.exe --analysis-enabled` | Only test code that checks `isAnalysisEnabled()` (not direct env var checks) |

**Best Practice:** Use the environment variable (`BL_ANALYSIS_TESTING=1`) as it activates all workarounds consistently.

---

## See Also

- **Main project documentation**: [CLAUDE.md](../../../CLAUDE.md) - Build commands and development guidelines
- **Build system details**: [scripts/devenv7/CLAUDE.md](../CLAUDE.md) - Build system internals and cross-compilation
- **Microsoft Application Verifier Documentation**: [Microsoft Docs](https://docs.microsoft.com/en-us/windows-hardware/drivers/devtest/application-verifier)
- **Boost.Filesystem path workaround**: [OSImplPlatformCommon.h](../../../src/include/baselib/core/detail/OSImplPlatformCommon.h#L1238)
- **OpenSSL DLL pinning**: [CryptoBase.h](../../../src/include/baselib/crypto/CryptoBase.h#L514)
- **Test infrastructure**: [UtfArgsParser.h](../../../src/utests/include/utests/baselib/UtfArgsParser.h)

---

**Document Version:** 1.1
**Last Updated:** 2026-02-25
**Applies to:** swblocks-baselib devenv7+ (Windows only)
