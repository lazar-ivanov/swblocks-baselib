# Clang Analysis Tools Support

This document describes the Clang static and dynamic analysis tools integration for swblocks-baselib.

## Requirements

### Linux
- **Platform**: Linux (RHEL or Ubuntu)
- **DevEnv**: devenv7 or higher
- **Toolchain**: clang2010 (Clang 20.1.0) or higher
- **Supported tools**: All sanitizers (ASAN, MSAN, TSAN, UBSAN), scan-build, clang-tidy

### macOS
- **Platform**: macOS (Darwin)
- **DevEnv**: devenv7 or higher
- **Toolchain**: clang1700 (Clang 17.0.0) or higher
- **Supported tools**: ASAN, TSAN, UBSAN
- **⚠️ Not supported on macOS**: MSAN, scan-build, clang-tidy. On macOS the compiler is the
  one provided by the OS, so there is no toolchain root from which the `scan-build` and
  `clang-tidy` binaries could be located.

### Windows
- Not supported.

Attempting to use unsupported features will result in a build error.

## Runtime Analysis (Sanitizers)

Sanitizers are runtime analysis tools that detect various types of bugs during program execution.

### Available Sanitizers

1. **AddressSanitizer (ASAN)** - `BL_CLANG_ENABLE_RA_ASAN=1`
   - Detects: memory errors, buffer overflows, use-after-free, use-after-return, memory leaks
   - Performance: ~2x slowdown
   - Memory: ~3x increase

2. **MemorySanitizer (MSAN)** - `BL_CLANG_ENABLE_RA_MSAN=1`
   - Detects: uninitialized memory reads
   - Performance: ~3x slowdown
   - **⚠️ CRITICAL LIMITATION**: MSAN will NOT work with the standard build configuration
   - **Requires ALL code and libraries to be instrumented** - this includes Boost, OpenSSL, and all system libraries
   - The current build uses non-instrumented Boost/OpenSSL, so MSAN will report false positives
   - Only use MSAN if you can rebuild all dependencies with `-fsanitize=memory`

3. **ThreadSanitizer (TSAN)** - `BL_CLANG_ENABLE_RA_TSAN=1`
   - Detects: data races, deadlocks, thread synchronization issues
   - Performance: ~5-15x slowdown
   - Memory: ~5-10x increase

4. **UndefinedBehaviorSanitizer (UBSAN)** - `BL_CLANG_ENABLE_RA_UBSAN=1`
   - Detects: undefined behavior (integer overflow, null pointer dereference, etc.)
   - Performance: minimal overhead
   - **Can be combined** with ASAN, MSAN, or TSAN

5. **Force O1 Optimization** - `BL_CLANG_ENABLE_RA_FORCE_O1=1`
   - Forces optimization level to `-O1` when used with any sanitizer
   - `-O1` is the sweet spot for sanitizers: good diagnostics with reasonable performance
   - Without this flag, the build uses the optimization level of the current variant
   - **Can be combined** with any sanitizer

### Important Constraints

- **ASAN, MSAN, and TSAN are mutually exclusive** - only one can be enabled at a time
- **UBSAN can be combined** with any of the above
- Enabling multiple mutually exclusive sanitizers will result in a build error

### Usage Examples

```bash
# Address sanitizer only (uses variant's optimization level)
make utf_baselib BL_CLANG_ENABLE_RA_ASAN=1

# Address sanitizer with forced -O1 optimization
make utf_baselib BL_CLANG_ENABLE_RA_ASAN=1 BL_CLANG_ENABLE_RA_FORCE_O1=1

# Address sanitizer + undefined behavior sanitizer
make utf_baselib BL_CLANG_ENABLE_RA_ASAN=1 BL_CLANG_ENABLE_RA_UBSAN=1

# ASAN + UBSAN with forced -O1 (recommended for best diagnostics)
make utf_baselib BL_CLANG_ENABLE_RA_ASAN=1 BL_CLANG_ENABLE_RA_UBSAN=1 BL_CLANG_ENABLE_RA_FORCE_O1=1

# Memory sanitizer only
make utf_baselib BL_CLANG_ENABLE_RA_MSAN=1

# Memory sanitizer with forced -O1
make utf_baselib BL_CLANG_ENABLE_RA_MSAN=1 BL_CLANG_ENABLE_RA_FORCE_O1=1

# Thread sanitizer + undefined behavior sanitizer
make utf_baselib BL_CLANG_ENABLE_RA_TSAN=1 BL_CLANG_ENABLE_RA_UBSAN=1

# TSAN + UBSAN with forced -O1
make utf_baselib BL_CLANG_ENABLE_RA_TSAN=1 BL_CLANG_ENABLE_RA_UBSAN=1 BL_CLANG_ENABLE_RA_FORCE_O1=1

# UB sanitizer standalone
make utf_baselib BL_CLANG_ENABLE_RA_UBSAN=1

# UB sanitizer standalone with forced -O1
make utf_baselib BL_CLANG_ENABLE_RA_UBSAN=1 BL_CLANG_ENABLE_RA_FORCE_O1=1
```

### Error Example

```bash
# This will ERROR - ASAN and MSAN are mutually exclusive
make utf_baselib BL_CLANG_ENABLE_RA_ASAN=1 BL_CLANG_ENABLE_RA_MSAN=1
```

### Runtime Environment Variables

The build automatically exports environment variables for better error reporting:

- **ASAN_OPTIONS**: `check_initialization_order=1:detect_stack_use_after_return=1:strict_string_checks=1:detect_invalid_pointer_pairs=2:strict_init_order=1`
- **MSAN_OPTIONS**: `poison_in_dtor=1`
- **TSAN_OPTIONS**: `second_deadlock_stack=1`
- **UBSAN_OPTIONS**: `print_stacktrace=1:halt_on_error=0`

You can override these by setting the environment variables before running your program.

## Static Analysis

Static analysis tools analyze source code without executing it.

### scan-build

`scan-build` is a static analyzer that uses the Clang Static Analyzer to find bugs.

#### Usage

```bash
# Enable scan-build
make utf_baselib BL_CLANG_ENABLE_SA_SCAN=1
```

#### Output

- Reports are generated in: `$(BLDDIR)/scan-build-results`
- Open `index.html` in the results directory to view the report

#### Features

- Detects: null pointer dereferences, use of uninitialized values, memory leaks, etc.
- No runtime overhead (analysis happens during compilation)
- Generates HTML reports with source code highlighting

### clang-tidy

`clang-tidy` is a linter that provides code quality and style checking.

#### Usage

```bash
# Enable clang-tidy and build
make utf_baselib BL_CLANG_ENABLE_SA_TIDY=1

# After building, create compilation database
cd bld/ub24-a64-clang2010-debug
echo '[' > compile_commands.json && cat *.o.json | sed 's/,$//' >> compile_commands.json && echo ']' >> compile_commands.json
cd ../..

# Run clang-tidy on specific source files (replace with your actual file paths)
$DIST_ROOT_DEPS1/toolchain-clang/20.1.0/ub24-a64-clang2010-release/bin/clang-tidy \
  src/utests/utf_baselib_data/UtfBaselibDataMain.cpp -p bld/ub24-a64-clang2010-debug

# Or analyze all source files in parallel (recommended)
find src -name "*.cpp" | xargs -P8 -n1 \
  $DIST_ROOT_DEPS1/toolchain-clang/20.1.0/ub24-a64-clang2010-release/bin/clang-tidy \
  -p bld/ub24-a64-clang2010-debug
```

#### Output

- Compilation database JSON fragments generated in: `$(BLDDIR)/`
- Must be merged into `compile_commands.json` before running clang-tidy
- clang-tidy output shows warnings and suggestions inline

#### Features

- Checks: modernization, readability, performance, bug-proneness
- Customizable via `.clang-tidy` configuration file
- Can apply automatic fixes with `--fix` option

## Combined Usage Warnings

### Sanitizers + scan-build

```bash
make utf_baselib BL_CLANG_ENABLE_RA_ASAN=1 BL_CLANG_ENABLE_SA_SCAN=1
```

**Warning**: scan-build may not work correctly with sanitizer flags. Consider running them separately.

### Sanitizers + clang-tidy

```bash
make utf_baselib BL_CLANG_ENABLE_RA_ASAN=1 BL_CLANG_ENABLE_SA_TIDY=1
```

**Info**: This combination is supported but will slow down the build significantly.

### scan-build + clang-tidy

```bash
make utf_baselib BL_CLANG_ENABLE_SA_SCAN=1 BL_CLANG_ENABLE_SA_TIDY=1
```

**Warning**: This combination will slow down the build significantly.

## Error Messages

### MSAN on macOS Error

```
make: *** MSAN (BL_CLANG_ENABLE_RA_MSAN) is not supported on macOS. Use ASAN instead.
```

**Solution**: MSAN is Linux-only. Use ASAN on macOS for memory error detection.

### clang-tidy on macOS Error

```
make: *** clang-tidy (BL_CLANG_ENABLE_SA_TIDY) is not supported on macOS.
```

**Solution**: clang-tidy is Linux-only, as is scan-build (`BL_CLANG_ENABLE_SA_SCAN` fails the same
way on macOS). Run the static analysis on a Linux devenv7 host.

### DevEnv Version Error (Linux)

```
make: *** Clang analysis tools require devenv7 or higher on Linux. Current devenv: devenv5
```

**Solution**: Upgrade to devenv7 or higher on Linux.

### DevEnv Version Error (macOS)

```
make: *** Clang analysis tools require devenv7 or higher on macOS. Current devenv: devenv5
```

**Solution**: Upgrade to devenv7 or higher on macOS.

### Toolchain Error (Linux)

```
make: *** Clang analysis tools require clang2010 (Clang 20.1.0) or higher on Linux. Current toolchain: gcc1520
```

**Solution**: Use `TOOLCHAIN=clang2010` on Linux.

### Toolchain Error (macOS)

```
make: *** Clang analysis tools require clang1700 (Clang 17.0.0) or higher on macOS. Current toolchain: gcc910
```

**Solution**: Use `TOOLCHAIN=clang1700` or higher on macOS.

### Mutual Exclusion Error

```
make: *** ASAN, MSAN, and TSAN are mutually exclusive. Only one can be enabled at a time.
Currently enabled: ASAN MSAN
```

**Solution**: Only enable one of ASAN, MSAN, or TSAN (UBSAN can be combined with any).

## Best Practices

1. **Start with ASAN + UBSAN + Force O1**: Most comprehensive coverage with best diagnostics
   ```bash
   make test BL_CLANG_ENABLE_RA_ASAN=1 BL_CLANG_ENABLE_RA_UBSAN=1 BL_CLANG_ENABLE_RA_FORCE_O1=1
   ```

2. **Optimization Level Recommendations**:
   - **Use `BL_CLANG_ENABLE_RA_FORCE_O1=1`** for best error diagnostics (recommended)
   - `-O1` is the sweet spot: good stack traces, reasonable performance, accurate line numbers
   - Without `FORCE_O1`, debug builds use `-O0` (slow) and release builds use `-O2/-O3` (harder to debug)
   - Example with debug variant:
     ```bash
     # Without FORCE_O1: uses -O0 (very slow)
     make test VARIANT=debug BL_CLANG_ENABLE_RA_ASAN=1

     # With FORCE_O1: uses -O1 (better performance, still good diagnostics)
     make test VARIANT=debug BL_CLANG_ENABLE_RA_ASAN=1 BL_CLANG_ENABLE_RA_FORCE_O1=1
     ```

3. **Use TSAN for threading issues**: If you suspect race conditions or deadlocks
   ```bash
   make test BL_CLANG_ENABLE_RA_TSAN=1 BL_CLANG_ENABLE_RA_FORCE_O1=1
   ```

4. **⚠️ MSAN has critical limitations**:
   - MSAN will NOT work with standard builds - requires rebuilding ALL dependencies (Boost, OpenSSL) with `-fsanitize=memory`
   - MSAN with non-instrumented libraries will produce false positives from library boundaries
   - For most use cases, prefer ASAN which works with standard builds

5. **Run static analysis separately**: Don't combine with runtime sanitizers
   ```bash
   make clean
   make utf_baselib BL_CLANG_ENABLE_SA_SCAN=1
   # Review scan-build results
   make clean
   make utf_baselib BL_CLANG_ENABLE_SA_TIDY=1
   # Review clang-tidy results
   ```

6. **Debug mode**: Use with `VARIANT=debug` for better stack traces
   ```bash
   make utf_baselib VARIANT=debug BL_CLANG_ENABLE_RA_ASAN=1 BL_CLANG_ENABLE_RA_FORCE_O1=1
   ```

## Implementation Details

The Clang analysis tools support is implemented in:
- `projects/make/toolchain/clang-analysis.mk`

This file is automatically included by `projects/make/common.mk` after all toolchain and variant
makefiles, so that it can override the optimization flags they set.

### Compiler Flags Added

**ASAN**:
- `-fsanitize=address`
- `-fno-omit-frame-pointer`
- `-fno-optimize-sibling-calls`
- If `BL_CLANG_ENABLE_RA_FORCE_O1=1`: Optimization forced to `-O1`
- Otherwise: Uses variant's optimization level (-O0 for debug, -O2/-O3 for release)

**MSAN**:
- `-fsanitize=memory`
- `-fsanitize-memory-track-origins=2`
- `-fno-omit-frame-pointer`
- `-fno-optimize-sibling-calls`
- If `BL_CLANG_ENABLE_RA_FORCE_O1=1`: Optimization forced to `-O1`
- Otherwise: Uses variant's optimization level

**TSAN**:
- `-fsanitize=thread`
- `-fno-omit-frame-pointer`
- If `BL_CLANG_ENABLE_RA_FORCE_O1=1`: Optimization forced to `-O1`
- Otherwise: Uses variant's optimization level

**UBSAN** (standalone):
- `-fsanitize=undefined`
- `-fno-sanitize-recover=undefined`
- `-fno-omit-frame-pointer`
- If `BL_CLANG_ENABLE_RA_FORCE_O1=1`: Optimization forced to `-O1`
- Otherwise: Uses variant's optimization level

**UBSAN** (combined with ASAN/MSAN/TSAN):
- `-fsanitize=undefined`
- `-fno-sanitize-recover=undefined`
- Optimization controlled by the primary sanitizer

**BL_CLANG_ENABLE_RA_FORCE_O1**:
- Removes all `-O0`, `-O1`, `-O2`, `-O3` flags
- Adds `-O1` flag
- Works with any sanitizer (ASAN, MSAN, TSAN, UBSAN)

**clang-tidy**:
- `-MJ$(BLDDIR)/$(@F).json` - Generates compilation database fragments

### Linker Flags Added

All sanitizers add their corresponding linker flags:
- ASAN: `-fsanitize=address`
- MSAN: `-fsanitize=memory`
- TSAN: `-fsanitize=thread`
- UBSAN: `-fsanitize=undefined`

## Troubleshooting

### Sanitizer Reports False Positives

Use suppressions files:
```bash
export ASAN_OPTIONS="suppressions=/path/to/asan_suppressions.txt"
export LSAN_OPTIONS="suppressions=/path/to/lsan_suppressions.txt"
export TSAN_OPTIONS="suppressions=/path/to/tsan_suppressions.txt"
```

### MSAN Reports Errors in Third-Party Libraries

MSAN requires all code to be instrumented. The current build uses non-instrumented Boost and OpenSSL libraries, which will cause MSAN to report errors at library boundaries.

**To debug MSAN errors with lldb:**
```bash
# Build with MSAN
make utf_baselib BL_CLANG_ENABLE_RA_MSAN=1

# Run under lldb with abort on error to get stack trace
export MSAN_OPTIONS=abort_on_error=1
lldb ./bld/ub24-a64-clang2010-debug/utests/utf_baselib_data/utf-baselib-data
(lldb) run
(lldb) bt
```

**To properly use MSAN, you must rebuild ALL dependencies with MSAN:**
```bash
# This is complex and requires modifying build scripts to add -fsanitize=memory
# to ALL compilation and linking of Boost, OpenSSL, and other dependencies
# For most use cases, prefer ASAN which works with standard builds
```

### Performance Too Slow

- Try UBSAN alone (minimal overhead)
- Use `-O2` instead of `-O0` for sanitizer builds
- Run tests on a subset of code

### clang-tidy Too Verbose

Create a `.clang-tidy` configuration file to disable specific checks:
```yaml
Checks: '-*,readability-*,modernize-*,performance-*'
```
