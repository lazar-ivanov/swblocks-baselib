ifndef JSON_SPIRIT_4_08_INCLUDED
JSON_SPIRIT_4_08_INCLUDED = 1

# json-spirit is only needed when BL_USE_JSON_SPIRIT is set (not the default)
#
# Tested by value rather than by definedness so that BL_USE_JSON_SPIRIT=0 means Boost.JSON; see the
# note on flag validation in projects/make/3rd/boost/common.mk, which is included first and which
# also normalizes the value
ifeq (1,$(strip $(BL_USE_JSON_SPIRIT)))

# use json-spirit source directory for header-only implementation
# try DIST_ROOT_DEPS3 root first and if not there then fallback to
# DIST_ROOT_DEPS2

ifneq ("$(wildcard $(DIST_ROOT_DEPS3)/json-spirit/4.08)","")
INCLUDE  += $(DIST_ROOT_DEPS3)/json-spirit/4.08/source
else
INCLUDE  += $(DIST_ROOT_DEPS2)/json-spirit/4.08/source
endif

endif # BL_USE_JSON_SPIRIT

endif # JSON_SPIRIT_4_08_INCLUDED
