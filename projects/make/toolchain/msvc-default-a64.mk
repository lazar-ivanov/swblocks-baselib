# ARM64 architecture-specific toolchain configuration
# This file is included by common.mk when ARCH=a64

ifeq ($(TOOLCHAIN),vc143)
# Native ARM64 build (ARM64 host → ARM64 target)
ifeq ($(BL_WIN_ARCH_IS_ARM64),1)
# Native build: use Hostarm64/arm64 compiler paths
PATH     := $(MSVC)/VC/Tools/MSVC/$(MSVCVERSIONTAG)\bin\Hostarm64\arm64:$(MSVC)/VC/Redist/MSVC/$(MSVCVERSIONTAG)\arm64\$(MSVCRTTAG):$(PATH)
endif
endif
