ifndef BOOST_COMMON_INCLUDED
BOOST_COMMON_INCLUDED = 1

ifeq (, $(BOOSTDIR))
# For devenv7, the directory structure includes the variant suffix (e.g., d25-a64-clang1700-debug)
ifeq ($(DEVENV_VERSION_TAG),devenv7)
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

ifeq ($(DEVENV_VERSION_TAG),devenv7)
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
# Boost 1.89+ made boost_system header-only, so skip it for devenv7
ifneq ($(DEVENV_VERSION_TAG),devenv7)
LDLIBS   += boost_system$(LIBTAG)$(ARCHTAG)
endif
LDLIBS   += boost_thread$(LIBTAG)$(ARCHTAG)
LDLIBS   += boost_filesystem$(LIBTAG)$(ARCHTAG)
LDLIBS   += boost_program_options$(LIBTAG)$(ARCHTAG)
LDLIBS   += boost_regex$(LIBTAG)$(ARCHTAG)
LDLIBS   += boost_random$(LIBTAG)$(ARCHTAG)
LDLIBS   += boost_unit_test_framework$(LIBTAG)$(ARCHTAG)

# boost_locale linking: Can be disabled by setting NO_BOOST_LOCALE_LIB
ifndef NO_BOOST_LOCALE_LIB
LDLIBS   += boost_locale$(LIBTAG)$(ARCHTAG)
ifeq ($(BL_PLAT_IS_DARWIN),1)
# It looks like this is not automatically included in Darwin
LDLIBS   += iconv
endif
else
# Define macro to inform code that boost_locale library is not available
CPPFLAGS += -DBL_NO_BOOST_LOCALE_LIB
endif

# boost_json linking: Always enabled by default, disable by setting BL_USE_JSON_SPIRIT=1
ifndef BL_USE_JSON_SPIRIT
LDLIBS   += boost_json$(LIBTAG)$(ARCHTAG)
else
CPPFLAGS += -DBL_USE_JSON_SPIRIT
endif

endif # BOOST_COMMON_INCLUDED
