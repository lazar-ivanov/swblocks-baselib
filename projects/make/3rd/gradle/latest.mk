# Determine Gradle path based on devenv version
# Use negative filtering: devenv7+ by default, devenv2-6 explicitly handled
ifneq ($(filter devenv2 devenv3 devenv4 devenv5 devenv6,$(DEVENV_VERSION_TAG)),)
# devenv2-6: Use gradle/latest/default structure
GRADLE := $(DIST_ROOT_DEPS3)/gradle/latest/default/bin/gradle
else
# devenv7+: the Linux and macOS installers create gradle/latest/default while the Windows
# installer creates a versioned directory (e.g., gradle/9.2.1/default), so prefer 'latest'
# and otherwise accept any entry which actually contains a Gradle launcher; matching on the
# launcher rather than on the directory name keeps unrelated siblings (e.g. gradle/zip, which
# holds the downloaded archive) from being selected by sort order
GRADLE := $(firstword \
    $(wildcard $(DIST_ROOT_DEPS3)/gradle/latest/default/bin/gradle) \
    $(wildcard $(DIST_ROOT_DEPS3)/gradle/*/default/bin/gradle))
ifeq (, $(GRADLE))
GRADLE := $(DIST_ROOT_DEPS3)/gradle/latest/default/bin/gradle
endif
endif
