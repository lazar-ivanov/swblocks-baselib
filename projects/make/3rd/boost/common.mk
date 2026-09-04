ifndef BOOST_COMMON_INCLUDED
BOOST_COMMON_INCLUDED = 1

ifeq (, $(BOOSTDIR))
# For devenv7+, the directory structure includes the variant suffix (e.g., d25-a64-clang1700-debug)
ifeq (, $(BL_DEVENV_IS_LEGACY))
BOOSTDIR := $(DIST_ROOT_DEPS3)/boost/$(BL_DEVENV_BOOST_VERSION)/$(EXTPLAT)
else
BOOSTDIR := $(DIST_ROOT_DEPS3)/boost/$(BL_DEVENV_BOOST_VERSION)/$(EXTPLAT:%-$(VARIANT)=%)
endif
endif

CPPFLAGS += -DBOOST_ALL_NO_LIB
INCLUDE  += $(BOOSTDIR)/include
LIBPATH  += $(BOOSTDIR)/lib

ifeq ($(DEVENV_VERSION_TAG),devenv5)
CPPFLAGS += -DBOOST_BIND_GLOBAL_PLACEHOLDERS
endif

ifeq ($(DEVENV_VERSION_TAG),devenv6)
CPPFLAGS += -DBOOST_BIND_GLOBAL_PLACEHOLDERS
endif

ifeq (, $(BL_DEVENV_IS_LEGACY))
CPPFLAGS += -DBOOST_BIND_GLOBAL_PLACEHOLDERS
endif

# ugly hack to get first character of $(VARIANT)
INITIALS := d
V        := $(strip $(foreach v,$(INITIALS),$(if $(VARIANT:$v%=),,$v)))

LIBTAG   := -mt-s$(V)
ifneq ($(DEVENV_VERSION_TAG),devenv3)
ifeq (x86, $(ARCH))
ARCHTAG   := -x32
else
ARCHTAG   := -$(ARCH)
endif
endif

LDLIBS   += boost_date_time$(LIBTAG)$(ARCHTAG)
# Boost 1.89+ made boost_system header-only, so skip it for devenv7+
ifneq (, $(BL_DEVENV_IS_LEGACY))
LDLIBS   += boost_system$(LIBTAG)$(ARCHTAG)
endif
LDLIBS   += boost_thread$(LIBTAG)$(ARCHTAG)
LDLIBS   += boost_filesystem$(LIBTAG)$(ARCHTAG)
LDLIBS   += boost_program_options$(LIBTAG)$(ARCHTAG)
LDLIBS   += boost_regex$(LIBTAG)$(ARCHTAG)
LDLIBS   += boost_random$(LIBTAG)$(ARCHTAG)
LDLIBS   += boost_unit_test_framework$(LIBTAG)$(ARCHTAG)

# User-facing flag validation
#
# GNU make's ifdef / ifndef test whether a variable is DEFINED, not whether it is true. Both of the
# flags below used to be tested that way, so passing the value which plainly means "no" selected
# the opposite of what was asked for:
#
#   make BL_USE_JSON_SPIRIT=0   selected json-spirit
#   make NO_BOOST_LOCALE_LIB=0  disabled boost_locale and defined -DBL_NO_BOOST_LOCALE_LIB
#
# Both are documented user-facing knobs (see CONTRIBUTING.md), so the VALUE is now honoured and an
# unrecognized value is a hard error rather than a silent surprise.
#
# Accepted values are 1, 0 and unset. Note that for BL_USE_JSON_SPIRIT unset is NOT the same as 0:
# unset means "choose from the devenv version" while 0 means "Boost.JSON, even on a legacy devenv".
# Asking for 0 on a devenv which ships no boost_json is allowed and fails loudly at link time; that
# is the caller's explicit request and is left to fail rather than being second-guessed here.

ifneq (,$(filter-out 0 1,$(strip $(BL_USE_JSON_SPIRIT))))
$(error BL_USE_JSON_SPIRIT must be 0, 1 or unset - got '$(BL_USE_JSON_SPIRIT)')
endif

ifneq (,$(filter-out 0 1,$(strip $(NO_BOOST_LOCALE_LIB))))
$(error NO_BOOST_LOCALE_LIB must be 0, 1 or unset - got '$(NO_BOOST_LOCALE_LIB)')
endif

# boost_locale linking: Can be disabled by setting NO_BOOST_LOCALE_LIB=1
ifeq (1,$(strip $(NO_BOOST_LOCALE_LIB)))
# Define macro to inform code that boost_locale library is not available
CPPFLAGS += -DBL_NO_BOOST_LOCALE_LIB
else
LDLIBS   += boost_locale$(LIBTAG)$(ARCHTAG)
ifeq ($(BL_PLAT_IS_DARWIN),1)
# It looks like this is not automatically included in Darwin
LDLIBS   += iconv
endif
endif

# JSON library selection based on devenv version:
# - devenv2-6: Use json-spirit (via BL_USE_JSON_SPIRIT)
# - devenv7+: Use Boost.JSON (default)
# Can be overridden by explicitly setting BL_USE_JSON_SPIRIT

# Auto-enable json-spirit for devenv6 and earlier, unless the caller stated a preference either way
ifeq (,$(strip $(BL_USE_JSON_SPIRIT)))
ifneq ($(filter devenv2 devenv3 devenv4 devenv5 devenv6,$(DEVENV_VERSION_TAG)),)
BL_USE_JSON_SPIRIT := 1
$(info Building with json-spirit for backward compatibility ($(DEVENV_VERSION_TAG)))
endif
endif

# boost_json linking: enabled whenever json-spirit was not selected
ifeq (1,$(strip $(BL_USE_JSON_SPIRIT)))
CPPFLAGS += -DBL_USE_JSON_SPIRIT
else
LDLIBS   += boost_json$(LIBTAG)$(ARCHTAG)
endif

endif # BOOST_COMMON_INCLUDED
