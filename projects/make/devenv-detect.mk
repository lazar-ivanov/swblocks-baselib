ifeq ($(BL_PLAT_IS_RHEL),1)
# clang or gcc may or may not be available on platform, so check first
# Prefer clang2010 (standalone with libc++) for devenv7, fallback to gcc1520
ifneq ("$(wildcard $(DIST_ROOT_DEPS3)/toolchain-clang/20.1.0)","")
  TOOLCHAIN                 ?= clang2010
endif
ifneq ("$(wildcard $(DIST_ROOT_DEPS3)/toolchain-gcc/15.2.0)","")
  TOOLCHAIN                 ?= gcc1520
endif
ifneq ("$(wildcard $(DIST_ROOT_DEPS3)/toolchain-clang/8.0.1)","")
  TOOLCHAIN                 ?= clang801
endif
ifneq ("$(wildcard $(DIST_ROOT_DEPS3)/toolchain-gcc/8.3.0)","")
  TOOLCHAIN                 ?= gcc830
endif
ifneq ("$(wildcard $(DIST_ROOT_DEPS3)/toolchain-gcc/6.3.0)","")
  TOOLCHAIN                 ?= gcc630
else
  TOOLCHAIN                 ?= gcc492
endif
endif

ifeq ($(BL_PLAT_IS_UBUNTU),1)
# clang or gcc may or may not be available on platform, so check first
# Prefer clang2010 (standalone with libc++) for devenv7, fallback to gcc1520
ifneq ("$(wildcard $(DIST_ROOT_DEPS3)/toolchain-clang/20.1.0)","")
  TOOLCHAIN                 ?= clang2010
endif
ifneq ("$(wildcard $(DIST_ROOT_DEPS3)/toolchain-gcc/15.2.0)","")
  TOOLCHAIN                 ?= gcc1520
endif
ifneq ("$(wildcard $(DIST_ROOT_DEPS3)/toolchain-clang/12.0.1)","")
  TOOLCHAIN                 ?= clang1201
endif
ifneq ("$(wildcard $(DIST_ROOT_DEPS3)/toolchain-gcc/11.1.0)","")
  TOOLCHAIN                 ?= gcc1110
endif
ifneq ("$(wildcard $(DIST_ROOT_DEPS3)/toolchain-clang/8.0.1)","")
  TOOLCHAIN                 ?= clang801
endif
ifneq ("$(wildcard $(DIST_ROOT_DEPS3)/toolchain-gcc/8.3.0)","")
  TOOLCHAIN                 ?= gcc830
endif
ifneq ("$(wildcard $(DIST_ROOT_DEPS3)/toolchain-clang/3.9.1)","")
  TOOLCHAIN                 ?= clang391
endif
ifneq ("$(wildcard $(DIST_ROOT_DEPS3)/toolchain-gcc/6.3.0)","")
  TOOLCHAIN                 ?= gcc630
else
  TOOLCHAIN                 ?= gcc492
endif
endif

ifeq (win, $(findstring win, $(OS)))
  TOOLCHAIN_DEFAULT         := msvc-default
ifneq ("$(wildcard $(DIST_ROOT_DEPS3)/toolchain-msvc/vc143/BuildTools)","")
  TOOLCHAIN                 ?= vc143
else ifneq ("$(wildcard $(DIST_ROOT_DEPS3)/toolchain-msvc/vc14.1/BuildTools)","")
  TOOLCHAIN                 ?= vc141
else ifneq ("$(wildcard $(DIST_ROOT_DEPS3)/toolchain-msvc/vc14-update3/default)","")
  TOOLCHAIN                 ?= vc14
else
  TOOLCHAIN                 ?= vc12
endif
else
  TOOLCHAIN_DEFAULT         := gcc-default
ifeq ($(OS),ub14)
  TOOLCHAIN                 ?= clang35
else ifeq ($(OS),ub16)
  TOOLCHAIN                 ?= clang391
else ifeq ($(OS),ub18)
  TOOLCHAIN                 ?= clang801
else ifeq ($(OS),ub20)
  TOOLCHAIN                 ?= clang1201
else ifeq ($(OS),ub24)
  TOOLCHAIN                 ?= clang2010
else ifeq ($(OS),rhel9)
  TOOLCHAIN                 ?= clang2010
else ifeq ($(OS),rhel10)
  TOOLCHAIN                 ?= clang2010
else ifeq ($(OS),d156)
  TOOLCHAIN                 ?= clang730
else ifeq ($(OS),d17)
  TOOLCHAIN                 ?= clang1000
else ifeq ($(OS),d20)
  TOOLCHAIN                 ?= clang1205
else ifeq ($(OS),d22)
  TOOLCHAIN                 ?= clang1500
else ifeq ($(OS),d24)
  TOOLCHAIN                 ?= clang1700
else ifeq ($(OS),d25)
  TOOLCHAIN                 ?= clang1700
else
  TOOLCHAIN                 ?= gcc492
endif
endif

#
# The TOOLS_PLATFORM macro is used to execute tools from the tree
# and since we don't dist the binaries for clang on ubXX platforms we need to force
# the toolchain to be gccXXX, so clang builds can work by default on ubXX platforms
#

ifeq ($(OS),ub12)
TOOLS_PLATFORM ?= $(OS)-$(ARCH)-gcc492-$(VARIANT)
else ifeq ($(OS),ub14)
TOOLS_PLATFORM ?= $(OS)-$(ARCH)-clang35-$(VARIANT)
# Due to a bug upload using platform = linux-ub14 fails. Putting temporary workaround
# till issue is fixed.
BL_PROP_PLAT := linux-ub12
else ifeq ($(OS),win7)
TOOLS_PLATFORM ?= $(OS)-$(ARCH)-vc12-$(VARIANT)
else
TOOLS_PLATFORM ?= $(PLAT)
endif

DEVENV_VERSION_TAG := invalid

ifeq ($(TOOLCHAIN),gcc492)
DEVENV_VERSION_TAG := devenv2
endif

ifeq ($(TOOLCHAIN),gcc630)
DEVENV_VERSION_TAG := devenv3
endif

ifeq ($(TOOLCHAIN),gcc830)
DEVENV_VERSION_TAG := devenv4
endif

ifeq ($(TOOLCHAIN),gcc1110)
DEVENV_VERSION_TAG := devenv5
endif

ifeq ($(TOOLCHAIN),gcc1520)
DEVENV_VERSION_TAG := devenv7
endif

ifeq ($(TOOLCHAIN),clang35)
DEVENV_VERSION_TAG := devenv2
endif

ifeq ($(TOOLCHAIN),clang391)
DEVENV_VERSION_TAG := devenv3
endif

ifeq ($(TOOLCHAIN),clang801)
DEVENV_VERSION_TAG := devenv4
endif

ifeq ($(TOOLCHAIN),clang1201)
DEVENV_VERSION_TAG := devenv5
endif

ifeq ($(TOOLCHAIN),clang380)
DEVENV_VERSION_TAG := devenv3
endif

ifeq ($(TOOLCHAIN),clang730)
DEVENV_VERSION_TAG := devenv3
endif

ifeq ($(TOOLCHAIN),clang1000)
DEVENV_VERSION_TAG := devenv4
endif

ifeq ($(TOOLCHAIN),clang1205)
DEVENV_VERSION_TAG := devenv5
endif

ifeq ($(TOOLCHAIN),clang1500)
DEVENV_VERSION_TAG := devenv6
endif

ifeq ($(TOOLCHAIN),clang1700)
DEVENV_VERSION_TAG := devenv7
endif

ifeq ($(TOOLCHAIN),clang2010)
DEVENV_VERSION_TAG := devenv7
endif

ifeq ($(TOOLCHAIN),vc143)
DEVENV_VERSION_TAG := devenv7
endif

ifeq ($(TOOLCHAIN),ccl16)
DEVENV_VERSION_TAG := devenv7
endif

ifeq ($(TOOLCHAIN),vc141)
DEVENV_VERSION_TAG := devenv4
endif

ifeq ($(TOOLCHAIN),vc14)
DEVENV_VERSION_TAG := devenv3
endif

ifeq ($(TOOLCHAIN),vc12)
DEVENV_VERSION_TAG := devenv2
endif

# Use 'win' prefix for devenv7, keep 'win7' for older devenvs for backward compatibility
ifeq ($(DEVENV_VERSION_TAG),devenv7)
ifeq ($(OS),win7)
OS := win
endif
endif

ifneq (devenv, $(findstring devenv, $(DEVENV_VERSION_TAG)))
$(error The value '$(TOOLCHAIN)' of the TOOLCHAIN parameter is either invalid or the toolchain specified is no \
longer supported; the supported toolchains are: vc12, vc143, gcc492, gcc630, gcc830, gcc1110, gcc1520, \
clang35, clang391, clang380, clang801, clang730, clang1000, clang1201, clang1205, clang1500, clang1700, clang2010)
endif

# Legacy devenv predicate; expressed as an explicit finite set of the old environments, so
# devenv7 and any future devenv version take the current code paths by default
# Non-empty for devenv2-6 and empty for devenv7+
BL_DEVENV_IS_LEGACY := $(filter devenv2 devenv3 devenv4 devenv5 devenv6,$(DEVENV_VERSION_TAG))

BL_DEVENV_JSON_SPIRIT_VERSION=4.08
BL_DEVENV_BOOST_VERSION=1.58.0-devenv2
BL_DEVENV_OPENSSL_VERSION=1.0.2d

ifeq ($(DEVENV_VERSION_TAG),devenv3)
BL_DEVENV_BOOST_VERSION=1.63.0
BL_DEVENV_OPENSSL_VERSION=1.1.0d
endif

ifeq ($(DEVENV_VERSION_TAG),devenv4)
BL_DEVENV_BOOST_VERSION=1.72.0
BL_DEVENV_OPENSSL_VERSION=1.1.1d
endif

ifeq ($(DEVENV_VERSION_TAG),devenv5)
BL_DEVENV_BOOST_VERSION=1.75.0
BL_DEVENV_OPENSSL_VERSION=1.1.1k
endif

ifeq ($(DEVENV_VERSION_TAG),devenv6)
BL_DEVENV_BOOST_VERSION=1.84.0
ifneq (, $(BL_USE_OPENSSL_1X))
BL_DEVENV_OPENSSL_VERSION=1.1.1w
$(info Building with BL_USE_OPENSSL_1X = $(BL_USE_OPENSSL_1X))
$(info Building with OpenSSL 1.1.1+; BL_DEVENV_OPENSSL_VERSION = $(BL_DEVENV_OPENSSL_VERSION))
else
BL_DEVENV_OPENSSL_VERSION=3.0.12
# make sure OpenSSL doesn't declare old APIs depreciated
CPPFLAGS += -DOPENSSL_API_COMPAT=0x10100000L
$(info Building with OpenSSL 3.0+; BL_DEVENV_OPENSSL_VERSION = $(BL_DEVENV_OPENSSL_VERSION))
endif
endif

ifeq ($(DEVENV_VERSION_TAG),devenv7)
BL_DEVENV_BOOST_VERSION=1.90.0
BL_DEVENV_PYTHON_VERSION=3.14.2
ifneq (, $(BL_USE_OPENSSL_1X))
BL_DEVENV_OPENSSL_VERSION=1.1.1w
$(info Building with BL_USE_OPENSSL_1X = $(BL_USE_OPENSSL_1X))
$(info Building with OpenSSL 1.1.1+; BL_DEVENV_OPENSSL_VERSION = $(BL_DEVENV_OPENSSL_VERSION))
else
BL_DEVENV_OPENSSL_VERSION=3.5.4
# make sure OpenSSL doesn't declare old APIs depreciated
CPPFLAGS += -DOPENSSL_API_COMPAT=0x10100000L
$(info Building with OpenSSL 3.5+; BL_DEVENV_OPENSSL_VERSION = $(BL_DEVENV_OPENSSL_VERSION))
endif
endif

ifeq ($(DEVENV_VERSION_TAG),devenv3)
CPPFLAGS += -DBL_DEVENV_VERSION=3
endif

ifeq ($(DEVENV_VERSION_TAG),devenv4)
CPPFLAGS += -DBL_DEVENV_VERSION=4
endif

ifeq ($(DEVENV_VERSION_TAG),devenv5)
CPPFLAGS += -DBL_DEVENV_VERSION=5
endif

ifeq ($(DEVENV_VERSION_TAG),devenv6)
CPPFLAGS += -DBL_DEVENV_VERSION=6
endif

ifeq ($(DEVENV_VERSION_TAG),devenv7)
CPPFLAGS += -DBL_DEVENV_VERSION=7
endif

# For devenv7, Boost directories include the variant suffix (e.g., -debug, -release) like OpenSSL and others
ifeq ($(DEVENV_VERSION_TAG),devenv7)
BL_EXPECTED_BOOSTDIR = $(DIST_ROOT_DEPS3)/boost/$(BL_DEVENV_BOOST_VERSION)/$(PLAT)
else
BL_EXPECTED_BOOSTDIR = $(DIST_ROOT_DEPS3)/boost/$(BL_DEVENV_BOOST_VERSION)/$(PLAT:%-$(VARIANT)=%)
endif
BL_EXPECTED_OPENSSLDIR = $(DIST_ROOT_DEPS3)/openssl/$(BL_DEVENV_OPENSSL_VERSION)/$(PLAT)

# ccl16 toolchain uses vc143 OpenSSL artifacts (ABI-compatible)
ifeq ($(TOOLCHAIN),ccl16)
BL_EXPECTED_OPENSSLDIR = $(DIST_ROOT_DEPS3)/openssl/$(BL_DEVENV_OPENSSL_VERSION)/$(subst ccl16,vc143,$(PLAT))
endif
