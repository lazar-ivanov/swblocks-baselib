ifeq ($(TOOLCHAIN),vc143)
# Cross-compilation support for x86 target
# Prepend Llvm x86 bin directory if using clang-cl
ifdef BL_USE_CLANG_CL
ifeq ($(BL_WIN_ARCH_IS_ARM64),1)
# ARM64 host → x86 target cross-compilation
PATH     := $(MSVC)/VC/Tools/Llvm/bin:$(MSVC)/VC/Tools/MSVC/$(MSVCVERSIONTAG)\bin\Hostarm64\x86:$(MSVC)/VC/Tools/MSVC/$(MSVCVERSIONTAG)\bin\Hostarm64\arm64:$(MSVC)/VC/Redist/MSVC/$(MSVCVERSIONTAG)\x86\$(MSVCRTTAG):$(PATH)
else ifeq ($(BL_WIN_ARCH_IS_X64),1)
# x64 host → x86 target cross-compilation
PATH     := $(MSVC)/VC/Tools/Llvm/bin:$(MSVC)/VC/Tools/MSVC/$(MSVCVERSIONTAG)\bin\Hostx64\x86:$(MSVC)/VC/Tools/MSVC/$(MSVCVERSIONTAG)\bin\Hostx64\x64:$(MSVC)/VC/Tools/MSVC/$(MSVCVERSIONTAG)\x64\$(MSVCRTTAG):$(PATH)
else
# Native x86 build
PATH     := $(MSVC)/VC/Tools/Llvm/bin:$(MSVC)/VC/Tools/MSVC/$(MSVCVERSIONTAG)\bin\Hostx86\x86:$(MSVC)/VC/Redist/MSVC/$(MSVCVERSIONTAG)\x86\$(MSVCRTTAG):$(PATH)
endif
else
ifeq ($(BL_WIN_ARCH_IS_ARM64),1)
# ARM64 host → x86 target cross-compilation
PATH     := $(MSVC)/VC/Tools/MSVC/$(MSVCVERSIONTAG)\bin\Hostarm64\x86:$(MSVC)/VC/Tools/MSVC/$(MSVCVERSIONTAG)\bin\Hostarm64\arm64:$(MSVC)/VC/Redist/MSVC/$(MSVCVERSIONTAG)\x86\$(MSVCRTTAG):$(PATH)
else ifeq ($(BL_WIN_ARCH_IS_X64),1)
# x64 host → x86 target cross-compilation
PATH     := $(MSVC)/VC/Tools/MSVC/$(MSVCVERSIONTAG)\bin\Hostx64\x86:$(MSVC)/VC/Tools/MSVC/$(MSVCVERSIONTAG)\bin\Hostx64\x64:$(MSVC)/VC/Redist/MSVC/$(MSVCVERSIONTAG)\x64\$(MSVCRTTAG):$(PATH)
else
# Native x86 build
PATH     := $(MSVC)/VC/Tools/MSVC/$(MSVCVERSIONTAG)\bin\Hostx86\x86:$(MSVC)/VC/Redist/MSVC/$(MSVCVERSIONTAG)\x86\$(MSVCRTTAG):$(PATH)
endif
endif
else ifeq ($(TOOLCHAIN),vc14)
ifeq ($(BL_WIN_ARCH_IS_X64),1)
PATH     := $(MSVC)/VC/bin/amd64_x86:$(MSVC)/VC/bin/amd64:$(MSVC)/VC/redist/x64/$(MSVCRTTAG):$(PATH)
else
PATH     := $(MSVC)/VC/bin:$(MSVC)/VC/redist/x86/$(MSVCRTTAG):$(PATH)
endif
else
PATH     := $(MSVC)/VC/bin:$(MSVC)/VC/redist/x86/$(MSVCRTTAG):$(PATH)
endif
