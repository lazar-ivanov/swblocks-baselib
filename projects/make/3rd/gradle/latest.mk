# Determine Gradle path based on devenv version
# Use negative filtering: devenv7+ by default, devenv2-6 explicitly handled
ifneq ($(filter devenv2 devenv3 devenv4 devenv5 devenv6,$(DEVENV_VERSION_TAG)),)
# devenv2-6: Use gradle/latest/default structure
GRADLE := $(DIST_ROOT_DEPS3)/gradle/latest/default/bin/gradle
else
# devenv7+: Use versioned gradle directory (e.g., gradle/9.2.1/default)
GRADLE_VERSION := $(firstword $(notdir $(wildcard $(DIST_ROOT_DEPS3)/gradle/*)))
GRADLE := $(DIST_ROOT_DEPS3)/gradle/$(GRADLE_VERSION)/default/bin/gradle
endif
