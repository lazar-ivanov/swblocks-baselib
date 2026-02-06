ifeq ($(OS),Windows_NT)
  OS := win7
endif

# simple platform detection
# Note: ARCH is set below in platform-specific detection logic
PLAT            = $(OS)-$(ARCH)-$(TOOLCHAIN)-$(VARIANT)
NONSTDARCH     := x86_64-linux-2.6-libc6
# separate variable for external dependencies
# to allow toolchains to rely on compatible platforms
# i.e. clang will use boost built with gcc
EXTPLAT    = $(PLAT)

ifeq (win, $(findstring win, $(OS)))
  NONSTDARCH   := x86_64-nt-6.0
  NONSTDARCH32 := ia32-nt-4.0

  # Save original ARCH from command line (before any conditional assignment)
  # If ARCH was not set on command line, this will be empty
  BL_CMDLINE_ARCH := $(ARCH)

  # Detect host architecture using Windows environment variables
  # PROCESSOR_IDENTIFIER: CPU identification string (contains "ARMv8" or "AArch64" on ARM64)
  # PROCESSOR_ARCHITECTURE: Architecture of the current process
  # PROCESSOR_ARCHITEW6432: Set when process is emulated, contains real host architecture
  #
  # Detection table:
  # | Host OS | Process        | PROCESSOR_ARCHITECTURE | PROCESSOR_ARCHITEW6432 | PROCESSOR_IDENTIFIER |
  # |---------|----------------|------------------------|------------------------|----------------------|
  # | x86     | Native x86     | x86                    | (Not Set)              | x86 Family...        |
  # | x64     | Native x64     | AMD64                  | (Not Set)              | Intel64 Family...    |
  # | x64     | x86 WOW64      | x86                    | AMD64                  | Intel64 Family...    |
  # | ARM64   | Native ARM64   | ARM64                  | (Not Set)              | ARMv8 (64-bit)...    |
  # | ARM64   | x86 Emulation  | x86                    | ARM64                  | ARMv8 (64-bit)...    |
  # | ARM64   | x64 Emulation  | AMD64                  | ARM64 or (Not Set)*    | ARMv8 (64-bit)...    |
  #
  # *Note: MSYS2 x64 binaries on ARM64 don't set PROCESSOR_ARCHITEW6432, so we rely on PROCESSOR_IDENTIFIER
  #
  # Use ?= for ARCH to allow user override via command line (e.g., make ARCH=x64)

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
        # x86 or x64 process emulated on ARM64 host
        BL_WIN_ARCH_IS_ARM64 := 1
        ARCH ?= a64
      else ifeq ($(PROCESSOR_ARCHITEW6432),AMD64)
        # x86 process on x64 host (WOW64)
        BL_WIN_ARCH_IS_X64 := 1
        ARCH ?= x64
      else
        # Priority 4: Native process, use PROCESSOR_ARCHITECTURE directly
        ifeq ($(PROCESSOR_ARCHITECTURE),AMD64)
          BL_WIN_ARCH_IS_X64 := 1
          ARCH ?= x64
        else
          # x86 native
          ARCH ?= x86
        endif
      endif
    endif
  endif

  ifeq ($(ARCH),x86)
    NONSTDARCH := ia32-nt-4.0
  endif

  ifdef SANITIZE
     $(error SANITIZE can only be used with Linux builds)
  endif

  TRUNCATE_COMMAND := echo | set /p= >

  BL_PROP_PLAT := windows
else
  UNAME_R := $(shell uname -r)
  UNAME_S := $(shell uname -s)
  UNAME_MERGED=$(UNAME_S)-$(UNAME_R)

  UNAME_M := $(shell uname -m)
  # on MacOS this is arm64
  ifeq ($(UNAME_M),arm64)
    ARCH := a64
  endif
  # on Ubuntu this is aarch64
  ifeq ($(UNAME_M),aarch64)
    ARCH := a64
  endif

  ifeq (Darwin-15.,$(findstring Darwin-15.,$(UNAME_MERGED)))
    OS := d156
    BL_PROP_PLAT := darwin-d156
    BL_PLAT_IS_DARWIN := 1
    $(info Detected OS is $(UNAME_MERGED) - i.e. OS X El Capitan)
  else ifeq (Darwin-16.,$(findstring Darwin-16.,$(UNAME_MERGED)))
    # for macOS Sierra we can safely fallback to the El Capitan binaries / devenv3
    OS := d156
    BL_PROP_PLAT := darwin-d156
    BL_PLAT_IS_DARWIN := 1
    $(info Detected OS is $(UNAME_MERGED) - i.e. mscOS Sierra)
  else ifeq (Darwin-17.,$(findstring Darwin-17.,$(UNAME_MERGED)))
    ifneq ("$(wildcard $(DIST_ROOT_DEPS3)/boost/1.72.0)","")
      # for macOS High Sierra and devenv4
      OS := d17
      BL_PROP_PLAT := darwin-d17
      BL_PLAT_IS_DARWIN := 1
      $(info Detected OS is $(UNAME_MERGED) - i.e. mscOS High Sierra; devenv4)
    else
      # for macOS High Sierra we can safely fallback to the El Capitan binaries / devenv3
      OS := d156
      BL_PROP_PLAT := darwin-d156
      BL_PLAT_IS_DARWIN := 1
      $(info Detected OS is $(UNAME_MERGED) - i.e. mscOS High Sierra; devenv3)
    endif
  else ifeq (Darwin-18.,$(findstring Darwin-18.,$(UNAME_MERGED)))
    ifneq ("$(wildcard $(DIST_ROOT_DEPS3)/boost/1.72.0)","")
      # for macOS Mojave and devenv4
      OS := d17
      BL_PROP_PLAT := darwin-d17
      BL_PLAT_IS_DARWIN := 1
      $(info Detected OS is $(UNAME_MERGED) - i.e. mscOS Mojave; devenv4)
    else
      # for macOS Mojave without devenv4 we can safely fallback to the El Capitan binaries / devenv3
      OS := d156
      BL_PROP_PLAT := darwin-d156
      BL_PLAT_IS_DARWIN := 1
      $(info Detected OS is $(UNAME_MERGED) - i.e. mscOS Mojave; devenv3)
    endif
  else ifeq (Darwin-19.,$(findstring Darwin-19.,$(UNAME_MERGED)))
    ifneq ("$(wildcard $(DIST_ROOT_DEPS3)/boost/1.72.0)","")
      # for macOS Catalina and devenv4
      OS := d17
      BL_PROP_PLAT := darwin-d17
      BL_PLAT_IS_DARWIN := 1
      $(info Detected OS is $(UNAME_MERGED) - i.e. mscOS Catalina; devenv4)
    else
      # for macOS Catalina without devenv4 we can safely fallback to the El Capitan binaries / devenv3
      OS := d156
      BL_PROP_PLAT := darwin-d156
      BL_PLAT_IS_DARWIN := 1
      $(info Detected OS is $(UNAME_MERGED) - i.e. mscOS Catalina; devenv3)
    endif
  else ifeq (Darwin-20.,$(findstring Darwin-20.,$(UNAME_MERGED)))
    ifneq ("$(wildcard $(DIST_ROOT_DEPS3)/boost/1.75.0)","")
      # for macOS Big Sur and devenv5
      OS := d20
      BL_PROP_PLAT := darwin-d20
      BL_PLAT_IS_DARWIN := 1
      $(info Detected OS is $(UNAME_MERGED) - i.e. mscOS Big Sur; devenv5)
    else ifneq ("$(wildcard $(DIST_ROOT_DEPS3)/boost/1.72.0)","")
      # for macOS Catalina and devenv4
      OS := d17
      BL_PROP_PLAT := darwin-d17
      BL_PLAT_IS_DARWIN := 1
      $(info Detected OS is $(UNAME_MERGED) - i.e. mscOS Catalina; devenv4)
    else
      # for macOS Catalina without devenv4 we can safely fallback to the El Capitan binaries / devenv3
      OS := d156
      BL_PROP_PLAT := darwin-d156
      BL_PLAT_IS_DARWIN := 1
      $(info Detected OS is $(UNAME_MERGED) - i.e. mscOS Catalina; devenv3)
    endif
  else ifeq (Darwin-22.,$(findstring Darwin-22.,$(UNAME_MERGED)))
    ifneq ("$(wildcard $(DIST_ROOT_DEPS3)/boost/1.84.0)","")
      # for macOS Ventura and devenv6
      OS := d22
      BL_PROP_PLAT := darwin-d22
      BL_PLAT_IS_DARWIN := 1
      $(info Detected OS is $(UNAME_MERGED) - i.e. mscOS Ventura; devenv6)
    endif
  else ifeq (Darwin-23.,$(findstring Darwin-23.,$(UNAME_MERGED)))
    ifneq ("$(wildcard $(DIST_ROOT_DEPS3)/boost/1.84.0)","")
      # for macOS Sonoma and devenv6
      OS := d22
      BL_PROP_PLAT := darwin-d22
      BL_PLAT_IS_DARWIN := 1
      $(info Detected OS is $(UNAME_MERGED) - i.e. mscOS Sonoma; devenv6)
    endif
  else ifeq (Darwin-24.,$(findstring Darwin-24.,$(UNAME_MERGED)))
    ifneq ("$(wildcard $(DIST_ROOT_DEPS3)/boost/1.90.0)","")
      # for macOS Sequoia and devenv7
      OS := d25
      BL_PROP_PLAT := darwin-d25
      BL_PLAT_IS_DARWIN := 1
      $(info Detected OS is $(UNAME_MERGED) - i.e. mscOS Sequoia; devenv7)
    endif
  else ifeq (Darwin-25.,$(findstring Darwin-25.,$(UNAME_MERGED)))
    ifneq ("$(wildcard $(DIST_ROOT_DEPS3)/boost/1.90.0)","")
      # for macOS Tahoe and devenv7
      OS := d25
      BL_PROP_PLAT := darwin-d25
      BL_PLAT_IS_DARWIN := 1
      $(info Detected OS is $(UNAME_MERGED) - i.e. mscOS Tahoe; devenv7)
    endif
  else ifeq (el5,$(findstring el5,$(UNAME_R)))
    OS := rhel5
    BL_PROP_PLAT := linux-rhel5
    BL_PLAT_IS_RHEL := 1
  else ifeq (el6,$(findstring el6,$(UNAME_R)))
    OS := rhel6
    BL_PROP_PLAT := linux-rhel6
    BL_PLAT_IS_RHEL := 1
  else ifeq (el7,$(findstring el7,$(UNAME_R)))
    OS := rhel7
    BL_PROP_PLAT := linux-rhel7
    BL_PLAT_IS_RHEL := 1
    ifeq ("$(wildcard $(DIST_ROOT_DEPS3)/toolchain-gcc/8.3.0)","")
       # devenv3 or below; remap to rhel6
       EXTPLAT = rhel6-$(ARCH)-$(TOOLCHAIN)-$(VARIANT)
    endif
  else ifeq (el8,$(findstring el8,$(UNAME_R)))
    OS := rhel8
    BL_PROP_PLAT := linux-rhel8
    BL_PLAT_IS_RHEL := 1
    ifeq ("$(wildcard $(DIST_ROOT_DEPS3)/toolchain-gcc/8.3.0)","")
       # devenv3 or below; remap to rhel6
       EXTPLAT = rhel6-$(ARCH)-$(TOOLCHAIN)-$(VARIANT)
    endif
  else ifeq (el9,$(findstring el9,$(UNAME_R)))
    OS := rhel9
    BL_PROP_PLAT := linux-rhel9
    BL_PLAT_IS_RHEL := 1
  else ifeq (el10,$(findstring el10,$(UNAME_R)))
    OS := rhel10
    BL_PROP_PLAT := linux-rhel10
    BL_PLAT_IS_RHEL := 1
  else
    #
    # assume Ubuntu, but generally any other flavor which
    # will be detected via lsb_release command
    #
    LSB_RELEASE_DIST_ID := $(shell lsb_release -i)
    LSB_RELEASE_VERSION := $(shell lsb_release -r)

    ifeq (Ubuntu,$(findstring Ubuntu,$(LSB_RELEASE_DIST_ID)))
        ifeq (12.04,$(findstring 12.04,$(LSB_RELEASE_VERSION)))
            OS := ub12
        else ifeq (14.04,$(findstring 14.04,$(LSB_RELEASE_VERSION)))
            OS := ub14
        else ifeq (16.04,$(findstring 16.04,$(LSB_RELEASE_VERSION)))
            OS := ub16
        else ifeq (18.04,$(findstring 18.04,$(LSB_RELEASE_VERSION)))
            ifneq ("$(wildcard $(DIST_ROOT_DEPS3)/toolchain-gcc/8.3.0)","")
                # This is devenv4
                OS := ub18
            else
                # TODO: temporary to make devenv3 work on Ubuntu 18.04
                OS := ub16
            endif
        else ifeq (20.04,$(findstring 20.04,$(LSB_RELEASE_VERSION)))
            ifneq ("$(wildcard $(DIST_ROOT_DEPS3)/toolchain-gcc/11.1.0)","")
                # This is devenv5; use ubuntu 20.04 binaries for now
                OS := ub20
            else
                # TODO: temporary to make devenv4 work on Ubuntu 20.04
                OS := ub18
            endif
        else ifeq (22.04,$(findstring 22.04,$(LSB_RELEASE_VERSION)))
            ifneq ("$(wildcard $(DIST_ROOT_DEPS3)/toolchain-gcc/11.1.0)","")
                # This is devenv5; use ubuntu 22.04 binaries for now
                OS := ub20
            else
                # TODO: temporary to make devenv4 work on Ubuntu 22.04
                OS := ub18
            endif
        else ifeq (24.04,$(findstring 24.04,$(LSB_RELEASE_VERSION)))
            ifneq ("$(wildcard $(DIST_ROOT_DEPS3)/toolchain-gcc/15.2.0)","")
                # This is devenv7; use ubuntu 24.04 binaries
                OS := ub24
            else ifneq ("$(wildcard $(DIST_ROOT_DEPS3)/toolchain-clang/20.1.0)","")
                # This is devenv7 with clang2010; use ubuntu 24.04 binaries
                OS := ub24
            else ifneq ("$(wildcard $(DIST_ROOT_DEPS3)/toolchain-gcc/11.1.0)","")
                # This is devenv5; use ubuntu 22.04 binaries for now
                OS := ub20
            else
                # TODO: temporary to make devenv4 work on Ubuntu 22.04
                OS := ub18
            endif
        else
            $(error Unsupported Ubuntu Version)
        endif
        DPKGDEB = fakeroot dpkg-deb --build
        UNAME_I := $(shell uname -i)
        ifeq ($(UNAME_I),i686)
        BL_PROP_PLAT_IS_32BIT := 1
        ARCH := x86
        endif
        BL_PROP_PLAT := linux-$(OS)
        BL_PLAT_IS_UBUNTU := 1
    else
        $(error Unsupported Linux Release Distributor ID)
    endif
  endif

  # configure rpmbuild command on redhat machines
  ifeq (rhel, $(findstring rhel,$(PLAT)))
    RPMBUILD = rpmbuild -bb \
      --define "_builddir $(realpath $(BLDDIR))" \
      --define "_rpmdir %{_builddir}/rpms" \
      --define "_build_id $(BUILD_ID)" \
      --define "_release $(OS)"
  endif

  # Default ARCH to x64 if not set by platform detection above
  ARCH ?= x64

  TRUNCATE_COMMAND := echo -n >
endif

#
# Verify that platform was successfully detected
#
ifeq ($(OS),)
  $(error Platform detection failed: Unable to detect a supported OS. Please ensure you are running on a supported platform and that the required development environment is installed in DIST_ROOT_DEPS3 ($(DIST_ROOT_DEPS3)))
endif
