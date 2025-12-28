###############################################################################
# Clang Static and Dynamic Analysis Tools Support
#
# This file provides support for Clang analysis tools:
#   - Runtime Analysis (Sanitizers):
#       * BL_CLANG_ENABLE_RA_ASAN=1     - AddressSanitizer
#       * BL_CLANG_ENABLE_RA_MSAN=1     - MemorySanitizer
#       * BL_CLANG_ENABLE_RA_TSAN=1     - ThreadSanitizer
#       * BL_CLANG_ENABLE_RA_UBSAN=1    - UndefinedBehaviorSanitizer
#       * BL_CLANG_ENABLE_RA_FORCE_O1=1 - Force -O1 optimization (optional)
#   - Static Analysis:
#       * BL_CLANG_ENABLE_SA_SCAN=1     - scan-build
#       * BL_CLANG_ENABLE_SA_TIDY=1     - clang-tidy
#
# Platform support:
#   - Linux: devenv7+, clang2010+ (all tools supported)
#   - macOS: devenv6+, clang1700+ (MSAN and clang-tidy NOT supported)
#
# IMPORTANT: ASAN, MSAN, and TSAN are mutually exclusive.
#            UBSAN can be combined with any of them.
#            MSAN is Linux-only (not supported on macOS).
#
# Usage:
#   make BL_CLANG_ENABLE_RA_ASAN=1                      # Address sanitizer
#   make BL_CLANG_ENABLE_RA_ASAN=1 BL_CLANG_ENABLE_RA_UBSAN=1  # ASAN + UBSAN
#   make BL_CLANG_ENABLE_RA_ASAN=1 BL_CLANG_ENABLE_RA_FORCE_O1=1  # ASAN with -O1
#   make BL_CLANG_ENABLE_RA_MSAN=1                      # Memory sanitizer
#   make BL_CLANG_ENABLE_RA_TSAN=1                      # Thread sanitizer
#   make BL_CLANG_ENABLE_SA_SCAN=1                      # scan-build
#   make BL_CLANG_ENABLE_SA_TIDY=1                      # clang-tidy
###############################################################################

#
# Validation: These features work on Linux (devenv7+/clang2010+) and macOS (devenv6+/clang1700+)
# Note: MSAN and clang-tidy are NOT supported on macOS
#

# Check if any analysis tool is requested
ifneq (, $(BL_CLANG_ENABLE_RA_ASAN)$(BL_CLANG_ENABLE_RA_MSAN)$(BL_CLANG_ENABLE_RA_TSAN)$(BL_CLANG_ENABLE_RA_UBSAN)$(BL_CLANG_ENABLE_SA_SCAN)$(BL_CLANG_ENABLE_SA_TIDY))

# Platform-specific validation
ifeq ($(BL_PLAT_IS_DARWIN),1)
# macOS-specific validation

# MSAN is not supported on macOS
ifneq (, $(BL_CLANG_ENABLE_RA_MSAN))
$(error MSAN (BL_CLANG_ENABLE_RA_MSAN) is not supported on macOS. Use ASAN instead.)
endif

# clang-tidy is not supported on macOS
ifneq (, $(BL_CLANG_ENABLE_SA_TIDY))
$(error clang-tidy (BL_CLANG_ENABLE_SA_TIDY) is not supported on macOS. Use scan-build instead.)
endif

# Validate devenv version is 6 or higher for macOS
ifeq ($(DEVENV_VERSION_TAG),devenv2)
$(error Clang analysis tools require devenv6 or higher on macOS. Current devenv: $(DEVENV_VERSION_TAG))
endif
ifeq ($(DEVENV_VERSION_TAG),devenv3)
$(error Clang analysis tools require devenv6 or higher on macOS. Current devenv: $(DEVENV_VERSION_TAG))
endif
ifeq ($(DEVENV_VERSION_TAG),devenv4)
$(error Clang analysis tools require devenv6 or higher on macOS. Current devenv: $(DEVENV_VERSION_TAG))
endif
ifeq ($(DEVENV_VERSION_TAG),devenv5)
$(error Clang analysis tools require devenv6 or higher on macOS. Current devenv: $(DEVENV_VERSION_TAG))
endif

# Validate toolchain is clang1700 or higher for macOS
ifneq ($(TOOLCHAIN),clang1700)
ifneq ($(TOOLCHAIN),clang2010)
$(error Clang analysis tools require clang1700 (Clang 17.0.0) or higher on macOS. Current toolchain: $(TOOLCHAIN))
endif
endif

else
# Linux-specific validation

# Validate devenv version is 7 or higher for Linux
ifeq ($(DEVENV_VERSION_TAG),devenv2)
$(error Clang analysis tools require devenv7 or higher on Linux. Current devenv: $(DEVENV_VERSION_TAG))
endif
ifeq ($(DEVENV_VERSION_TAG),devenv3)
$(error Clang analysis tools require devenv7 or higher on Linux. Current devenv: $(DEVENV_VERSION_TAG))
endif
ifeq ($(DEVENV_VERSION_TAG),devenv4)
$(error Clang analysis tools require devenv7 or higher on Linux. Current devenv: $(DEVENV_VERSION_TAG))
endif
ifeq ($(DEVENV_VERSION_TAG),devenv5)
$(error Clang analysis tools require devenv7 or higher on Linux. Current devenv: $(DEVENV_VERSION_TAG))
endif
ifeq ($(DEVENV_VERSION_TAG),devenv6)
$(error Clang analysis tools require devenv7 or higher on Linux. Current devenv: $(DEVENV_VERSION_TAG))
endif

# Validate toolchain is clang2010 or higher for Linux
ifneq ($(TOOLCHAIN),clang2010)
$(error Clang analysis tools require clang2010 (Clang 20.1.0) or higher on Linux. Current toolchain: $(TOOLCHAIN))
endif

endif # BL_PLAT_IS_DARWIN

endif # Any analysis tool requested

###############################################################################
# Conflict Detection for Mutually Exclusive Sanitizers
###############################################################################

# Count how many of ASAN/MSAN/TSAN are enabled
BL_SANITIZER_COUNT := 0
ifneq (, $(BL_CLANG_ENABLE_RA_ASAN))
BL_SANITIZER_COUNT := $(shell expr $(BL_SANITIZER_COUNT) + 1)
endif
ifneq (, $(BL_CLANG_ENABLE_RA_MSAN))
BL_SANITIZER_COUNT := $(shell expr $(BL_SANITIZER_COUNT) + 1)
endif
ifneq (, $(BL_CLANG_ENABLE_RA_TSAN))
BL_SANITIZER_COUNT := $(shell expr $(BL_SANITIZER_COUNT) + 1)
endif

# Error if more than one is enabled
ifeq ($(shell test $(BL_SANITIZER_COUNT) -gt 1; echo $$?),0)
$(error ASAN, MSAN, and TSAN are mutually exclusive. Only one can be enabled at a time. \
Currently enabled: $(if $(BL_CLANG_ENABLE_RA_ASAN),ASAN )$(if $(BL_CLANG_ENABLE_RA_MSAN),MSAN )$(if $(BL_CLANG_ENABLE_RA_TSAN),TSAN))
endif

###############################################################################
# Runtime Analysis: AddressSanitizer (ASAN)
###############################################################################

ifneq (, $(BL_CLANG_ENABLE_RA_ASAN))

$(info ========================================================================)
$(info Clang AddressSanitizer (ASAN) ENABLED)
$(info ========================================================================)
$(info Detects: memory errors, buffer overflows, use-after-free, etc.)
$(info Platform: $(if $(BL_PLAT_IS_DARWIN),macOS,Linux))
$(info DevEnv:   $(DEVENV_VERSION_TAG))
$(info Toolchain: $(TOOLCHAIN))
$(info ========================================================================)

# AddressSanitizer flags
CXXFLAGS += -fsanitize=address
LDFLAGS  += -fsanitize=address

# Enable additional options for better debugging
CXXFLAGS += -fno-omit-frame-pointer
CXXFLAGS += -fno-optimize-sibling-calls

# Adjust optimization level if BL_CLANG_ENABLE_RA_FORCE_O1 is set
ifneq (, $(BL_CLANG_ENABLE_RA_FORCE_O1))
$(info Forcing optimization level to -O1 for better diagnostics)
CXXFLAGS := $(filter-out -O0 -O1 -O2 -O3,$(CXXFLAGS))
CXXFLAGS += -O1
endif

# Export sanitizer options for better error reporting
export ASAN_OPTIONS=check_initialization_order=1:detect_stack_use_after_return=1:strict_string_checks=1:detect_invalid_pointer_pairs=2:strict_init_order=1

endif # BL_CLANG_ENABLE_RA_ASAN

###############################################################################
# Runtime Analysis: MemorySanitizer (MSAN)
###############################################################################

ifneq (, $(BL_CLANG_ENABLE_RA_MSAN))

$(info ========================================================================)
$(info Clang MemorySanitizer (MSAN) ENABLED)
$(info ========================================================================)
$(info Detects: uninitialized memory reads)
$(info Platform: $(if $(BL_PLAT_IS_DARWIN),macOS,Linux))
$(info DevEnv:   $(DEVENV_VERSION_TAG))
$(info Toolchain: $(TOOLCHAIN))
$(info WARNING: MSAN requires all code and libraries to be instrumented)
$(info ========================================================================)

# MemorySanitizer flags
CXXFLAGS += -fsanitize=memory
LDFLAGS  += -fsanitize=memory

# MSAN-specific options
CXXFLAGS += -fsanitize-memory-track-origins=2
CXXFLAGS += -fno-omit-frame-pointer
CXXFLAGS += -fno-optimize-sibling-calls

# Adjust optimization level if BL_CLANG_ENABLE_RA_FORCE_O1 is set
ifneq (, $(BL_CLANG_ENABLE_RA_FORCE_O1))
$(info Forcing optimization level to -O1 for better diagnostics)
CXXFLAGS := $(filter-out -O0 -O1 -O2 -O3,$(CXXFLAGS))
CXXFLAGS += -O1
endif

# Export sanitizer options
export MSAN_OPTIONS=poison_in_dtor=1

endif # BL_CLANG_ENABLE_RA_MSAN

###############################################################################
# Runtime Analysis: ThreadSanitizer (TSAN)
###############################################################################

ifneq (, $(BL_CLANG_ENABLE_RA_TSAN))

$(info ========================================================================)
$(info Clang ThreadSanitizer (TSAN) ENABLED)
$(info ========================================================================)
$(info Detects: data races, deadlocks, thread synchronization issues)
$(info Platform: $(if $(BL_PLAT_IS_DARWIN),macOS,Linux))
$(info DevEnv:   $(DEVENV_VERSION_TAG))
$(info Toolchain: $(TOOLCHAIN))
$(info ========================================================================)

# ThreadSanitizer flags
CXXFLAGS += -fsanitize=thread
LDFLAGS  += -fsanitize=thread

# TSAN-specific options
CXXFLAGS += -fno-omit-frame-pointer

# Adjust optimization level if BL_CLANG_ENABLE_RA_FORCE_O1 is set
ifneq (, $(BL_CLANG_ENABLE_RA_FORCE_O1))
$(info Forcing optimization level to -O1 for better diagnostics)
CXXFLAGS := $(filter-out -O0 -O1 -O2 -O3,$(CXXFLAGS))
CXXFLAGS += -O1
endif

# Export sanitizer options
export TSAN_OPTIONS=second_deadlock_stack=1

endif # BL_CLANG_ENABLE_RA_TSAN

###############################################################################
# Runtime Analysis: UndefinedBehaviorSanitizer (UBSAN)
###############################################################################

ifneq (, $(BL_CLANG_ENABLE_RA_UBSAN))

$(info ========================================================================)
$(info Clang UndefinedBehaviorSanitizer (UBSAN) ENABLED)
$(info ========================================================================)
$(info Detects: undefined behavior (integer overflow, null dereference, etc.))
ifneq (, $(BL_CLANG_ENABLE_RA_ASAN)$(BL_CLANG_ENABLE_RA_MSAN)$(BL_CLANG_ENABLE_RA_TSAN))
$(info Combined with: $(if $(BL_CLANG_ENABLE_RA_ASAN),ASAN )$(if $(BL_CLANG_ENABLE_RA_MSAN),MSAN )$(if $(BL_CLANG_ENABLE_RA_TSAN),TSAN))
else
$(info Running standalone)
endif
$(info Platform: $(if $(BL_PLAT_IS_DARWIN),macOS,Linux))
$(info DevEnv:   $(DEVENV_VERSION_TAG))
$(info Toolchain: $(TOOLCHAIN))
$(info ========================================================================)

# UndefinedBehaviorSanitizer flags
CXXFLAGS += -fsanitize=undefined
LDFLAGS  += -fsanitize=undefined

# Additional UBSAN options for comprehensive checking
CXXFLAGS += -fno-sanitize-recover=undefined

# Only adjust optimization if UBSAN is standalone (not combined with other sanitizers)
# and BL_CLANG_ENABLE_RA_FORCE_O1 is set
ifeq (, $(BL_CLANG_ENABLE_RA_ASAN)$(BL_CLANG_ENABLE_RA_MSAN)$(BL_CLANG_ENABLE_RA_TSAN))
CXXFLAGS += -fno-omit-frame-pointer
ifneq (, $(BL_CLANG_ENABLE_RA_FORCE_O1))
$(info Forcing optimization level to -O1 for better diagnostics)
CXXFLAGS := $(filter-out -O0 -O1 -O2 -O3,$(CXXFLAGS))
CXXFLAGS += -O1
endif
endif

# Export sanitizer options
export UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=0

endif # BL_CLANG_ENABLE_RA_UBSAN

###############################################################################
# Static Analysis: scan-build
###############################################################################

ifneq (, $(BL_CLANG_ENABLE_SA_SCAN))

$(info ========================================================================)
$(info Clang Static Analysis (scan-build) ENABLED)
$(info ========================================================================)
$(info Platform: $(if $(BL_PLAT_IS_DARWIN),macOS,Linux))
$(info DevEnv:   $(DEVENV_VERSION_TAG))
$(info Toolchain: $(TOOLCHAIN))
$(info ========================================================================)

# Verify scan-build is available
SCAN_BUILD := $(TOOLCHAIN_ROOT)/bin/scan-build
ifeq ("$(wildcard $(SCAN_BUILD))","")
$(error scan-build not found at $(SCAN_BUILD). Please ensure Clang tools are properly installed.)
endif

# Use STATIC_ANALYZER_CMD to wrap compilation (but not linking)
# This variable is used in the compilation rule in gcc-default.mk
STATIC_ANALYZER_CMD := $(SCAN_BUILD) \
       --use-cc=$(TOOLCHAIN_ROOT)/bin/clang \
       --use-c++=$(TOOLCHAIN_ROOT)/bin/clang++ \
       -o $(BLDDIR)/scan-build-results \
       --status-bugs \
       --keep-going

$(info scan-build will generate reports in: $(BLDDIR)/scan-build-results)

endif # BL_CLANG_ENABLE_SA_SCAN

###############################################################################
# Static Analysis: clang-tidy
###############################################################################

ifneq (, $(BL_CLANG_ENABLE_SA_TIDY))

$(info ========================================================================)
$(info Clang Static Analysis (clang-tidy) ENABLED)
$(info ========================================================================)
$(info Platform: $(if $(BL_PLAT_IS_DARWIN),macOS,Linux))
$(info DevEnv:   $(DEVENV_VERSION_TAG))
$(info Toolchain: $(TOOLCHAIN))
$(info ========================================================================)

# Verify clang-tidy is available
CLANG_TIDY := $(TOOLCHAIN_ROOT)/bin/clang-tidy
ifeq ("$(wildcard $(CLANG_TIDY))","")
$(error clang-tidy not found at $(CLANG_TIDY). Please ensure Clang tools are properly installed.)
endif

# Generate compilation database for clang-tidy
# clang-tidy cannot be run inline during compilation like scan-build
# Instead, we generate a compilation database and run clang-tidy as a post-build step
CXXFLAGS += -MJ$(BLDDIR)/$(@F).json

$(info clang-tidy enabled - compilation database fragments will be generated in $(BLDDIR))
$(info )
$(info After build completes:)
$(info   1. Create compile_commands.json:)
$(info      cd $(BLDDIR) && echo '[' > compile_commands.json && cat *.o.json | sed 's/,$$//' >> compile_commands.json && echo ']' >> compile_commands.json)
$(info   2. Then run clang-tidy on the actual source file(s):)
$(info      $(CLANG_TIDY) src/utests/utf_baselib_data/UtfBaselibDataMain.cpp -p $(BLDDIR))
$(info   3. Or analyze all source files in parallel:)
$(info      find src -name "*.cpp" | xargs -P8 -n1 $(CLANG_TIDY) -p $(BLDDIR))
$(info )

endif # BL_CLANG_ENABLE_SA_TIDY

###############################################################################
# Warnings for Combined Usage
###############################################################################

# Warn if runtime sanitizers are combined with static analysis
ifneq (, $(BL_CLANG_ENABLE_RA_ASAN)$(BL_CLANG_ENABLE_RA_MSAN)$(BL_CLANG_ENABLE_RA_TSAN)$(BL_CLANG_ENABLE_RA_UBSAN))
ifneq (, $(BL_CLANG_ENABLE_SA_SCAN))
$(warning Runtime sanitizers and scan-build are both enabled. \
scan-build may not work correctly with sanitizer flags. Consider running them separately.)
endif
ifneq (, $(BL_CLANG_ENABLE_SA_TIDY))
$(info Runtime sanitizers and clang-tidy are both enabled. This will slow down the build significantly.)
endif
endif

# Warn if both static analysis tools are enabled
ifneq (, $(BL_CLANG_ENABLE_SA_SCAN))
ifneq (, $(BL_CLANG_ENABLE_SA_TIDY))
$(warning Both scan-build and clang-tidy are enabled. This will slow down the build significantly.)
endif
endif
