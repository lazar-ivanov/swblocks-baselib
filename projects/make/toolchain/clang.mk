ifneq ($(BL_PLAT_IS_DARWIN),1)
CXX := $(TOOLCHAIN_ROOT)/bin/clang++
LD_LIBRARY_PATH := $(TOOLCHAIN_ROOT)/lib:$(LD_LIBRARY_PATH)
export LD_LIBRARY_PATH
endif

COMPILE.pch = $(COMPILE.cc) -x c++-header
EXTPLAT   = $(OS)-$(ARCH)-$(TOOLCHAIN_GCC_TOOLCHAIN_ID)-$(VARIANT)

ifneq ($(MAKECMDGOALS),clean)
  $(info )
  $(info $(HR))
  $(info Toolchain)
  $(info $(HR))
  $(info $(shell export LD_LIBRARY_PATH=$(LD_LIBRARY_PATH) && $(CXX) --version))
  $(info $(HR))
  $(info )
endif
