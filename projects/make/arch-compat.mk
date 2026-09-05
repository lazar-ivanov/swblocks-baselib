#
# Architecture compatibility layer
#
# Applies architecture downgrades for older devenv versions that don't support
# certain architectures. This file is included after devenv-detect.mk so that
# DEVENV_VERSION_TAG is available.
#
# IMPORTANT: This only affects auto-detected architecture values. User-specified
# ARCH parameter (e.g., make ARCH=a64) is always preserved.
#

# Windows ARM64 compatibility: downgrade a64 to x64 for devenv2-6
#
# devenv2-6 do not support ARM64 builds on Windows because:
# - No ARM64-specific toolchain configurations
# - No ARM64-specific dependency paths
# - Microsoft Visual Studio ARM64 support introduced in VS 2017 (vc141) but
#   full ecosystem support (dependencies, libraries) requires newer versions
#
# Use negative filtering: devenv7+ supports ARM64 by default, devenv2-6 need downgrade
ifndef BL_ARCH_DOWNGRADE_APPLIED
ifeq (win, $(findstring win, $(OS)))
  ifneq ($(filter devenv2 devenv3 devenv4 devenv5 devenv6,$(DEVENV_VERSION_TAG)),)
    # Only downgrade if:
    # 1. Current ARCH is a64
    # 2. ARCH was NOT specified on command line (BL_CMDLINE_ARCH is empty)
    ifeq ($(ARCH),a64)
      ifeq ($(BL_CMDLINE_ARCH),)
        # Auto-detected a64 - safe to downgrade
        override ARCH := x64
        BL_ARCH_DOWNGRADE_APPLIED := 1

        # Display info message to user
        $(info )
        $(info ========================================================================)
        $(info ARM64 architecture detected but not supported in $(DEVENV_VERSION_TAG))
        $(info )
        $(info Automatically downgrading build architecture: a64 -> x64)
        $(info )
        $(info The build will produce x64 binaries that run under emulation on ARM64.)
        $(info For native ARM64 builds, use devenv7 or later.)
        $(info )
        $(info To override this behavior, explicitly specify ARCH on command line:)
        $(info   make ARCH=a64  (may fail - ARM64 not fully supported))
        $(info ========================================================================)
        $(info )
      else
        # User explicitly requested a64 - respect their choice but warn
        $(warning )
        $(warning ========================================================================)
        $(warning WARNING: Building for ARM64 (a64) on $(DEVENV_VERSION_TAG))
        $(warning )
        $(warning ARM64 is not officially supported in $(DEVENV_VERSION_TAG).)
        $(warning Build may fail due to missing toolchain or dependency support.)
        $(warning )
        $(warning For reliable ARM64 builds, use devenv7 or later.)
        $(warning To build for x64 instead, use: make ARCH=x64)
        $(warning ========================================================================)
        $(warning )
      endif
    endif
  endif
endif
endif
