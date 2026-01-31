ifeq ($(TOOLCHAIN),vc143)
# Cross-compilation support for x64 target
# Prepend Llvm x64 bin directory if using clang-cl
ifdef BL_USE_CLANG_CL
ifeq ($(BL_WIN_ARCH_IS_ARM64),1)
# ARM64 host → x64 target cross-compilation
PATH     := $(MSVC)/VC/Tools/Llvm/x64/bin:$(MSVC)/VC/Tools/MSVC/$(MSVCVERSIONTAG)\bin\Hostarm64\x64:$(MSVC)/VC/Tools/MSVC/$(MSVCVERSIONTAG)\bin\Hostarm64\arm64:$(MSVC)/VC/Redist/MSVC/$(MSVCVERSIONTAG)\x64\$(MSVCRTTAG):$(PATH)
else ifeq ($(BL_WIN_ARCH_IS_X64),1)
# Native x64 build
PATH     := $(MSVC)/VC/Tools/Llvm/x64/bin:$(MSVC)/VC/Tools/MSVC/$(MSVCVERSIONTAG)\bin\Hostx64\x64:$(MSVC)/VC/Redist/MSVC/$(MSVCVERSIONTAG)\x64\$(MSVCRTTAG):$(PATH)
else
# x86 host → x64 target cross-compilation
PATH     := $(MSVC)/VC/Tools/Llvm/x64/bin:$(MSVC)/VC/Tools/MSVC/$(MSVCVERSIONTAG)\bin\Hostx86\x64:$(MSVC)/VC/Tools/MSVC/$(MSVCVERSIONTAG)\bin\Hostx86\x86:$(MSVC)/VC/Redist/MSVC/$(MSVCVERSIONTAG)\x86\$(MSVCRTTAG):$(PATH)
endif
else
ifeq ($(BL_WIN_ARCH_IS_ARM64),1)
# ARM64 host → x64 target cross-compilation
PATH     := $(MSVC)/VC/Tools/MSVC/$(MSVCVERSIONTAG)\bin\Hostarm64\x64:$(MSVC)/VC/Tools/MSVC/$(MSVCVERSIONTAG)\bin\Hostarm64\arm64:$(MSVC)/VC/Redist/MSVC/$(MSVCVERSIONTAG)\x64\$(MSVCRTTAG):$(PATH)
else ifeq ($(BL_WIN_ARCH_IS_X64),1)
# Native x64 build
PATH     := $(MSVC)/VC/Tools/MSVC/$(MSVCVERSIONTAG)\bin\Hostx64\x64:$(MSVC)/VC/Redist/MSVC/$(MSVCVERSIONTAG)\x64\$(MSVCRTTAG):$(PATH)
else
# x86 host → x64 target cross-compilation
PATH     := $(MSVC)/VC/Tools/MSVC/$(MSVCVERSIONTAG)\bin\Hostx86\x64:$(MSVC)/VC/Tools/MSVC/$(MSVCVERSIONTAG)\bin\Hostx86\x86:$(MSVC)/VC/Redist/MSVC/$(MSVCVERSIONTAG)\x86\$(MSVCRTTAG):$(PATH)
endif
endif
else ifeq ($(TOOLCHAIN),vc14)
ifeq ($(BL_WIN_ARCH_IS_X64),1)
PATH     := $(MSVC)/VC/bin/amd64:$(MSVC)/VC/redist/x64/$(MSVCRTTAG):$(PATH)
else
PATH     := $(MSVC)/VC/bin/x86_amd64:$(MSVC)/VC/bin:$(MSVC)/VC/redist/x86/$(MSVCRTTAG):$(PATH)
endif
else
PATH     := $(MSVC)/VC/bin/x86_amd64:$(MSVC)/VC/bin:$(MSVC)/VC/redist/x86/$(MSVCRTTAG):$(PATH)
endif
