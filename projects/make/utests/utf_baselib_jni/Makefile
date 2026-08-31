ifneq ($(DEVENV_VERSION_TAG),devenv3)
include $(MKDIR)/3rd/jdk/1.8.mk
else
include $(MKDIR)/3rd/jdk/common.mk
endif

ifeq ($(DEVENV_VERSION_TAG),devenv2)
include $(MKDIR)/3rd/gradle/1.6.mk
else
include $(MKDIR)/3rd/gradle/latest.mk
endif

# Configure JNI library linking based on platform and devenv version
#
# Windows JNI support is a devenv7+ capability; for devenv2-6 the JNI targets are excluded
# automatically because the JDK 8 dist publishes no win7-<arch> layout, so BL_JNI_ENABLED
# is never set (see 3rd/jdk/common.mk and the target filter in common.mk)
#
# Use negative filtering pattern (same as json-spirit for backward compatibility)
ifeq (win, $(findstring win, $(OS)))
  # Windows: No libjsig linking needed (uses SEH instead of POSIX signals)
  # Runtime loads jvm.dll via PATH (already configured in jdk/common.mk)
  # JavaVirtualMachine.h handles dynamic loading from bin/server/jvm.dll
else ifneq ($(filter devenv2 devenv3 devenv4 devenv5 devenv6,$(DEVENV_VERSION_TAG)),)
  # devenv2-6: JDK 8 uses the old jre/lib structure
  ifeq (darwin, $(findstring darwin, $(BL_PROP_PLAT)))
    # macOS JDK 8: jre/lib/server with .dylib (no arch subdirectory)
    $(utf_baselib_jni_ARTIFACT): LDADD += $(JAVA_HOME)/jre/lib/server/libjsig.dylib
  else ifeq (x86, $(ARCH))
    $(utf_baselib_jni_ARTIFACT): LDADD += $(JAVA_HOME)/jre/lib/i386/server/libjsig.so
  else ifeq (x64, $(ARCH))
    $(utf_baselib_jni_ARTIFACT): LDADD += $(JAVA_HOME)/jre/lib/amd64/server/libjsig.so
  else ifeq (a64, $(ARCH))
    $(utf_baselib_jni_ARTIFACT): LDADD += $(JAVA_HOME)/jre/lib/aarch64/server/libjsig.so
  endif
else ifeq (darwin, $(findstring darwin, $(BL_PROP_PLAT)))
  # devenv7+ macOS JDK 25: Modern structure with lib/server (no arch subdirectory)
  $(utf_baselib_jni_ARTIFACT): LDADD += $(JAVA_HOME)/lib/server/libjsig.dylib
  $(utf_baselib_jni_ARTIFACT): LDADD += -Wl,-rpath,$(JAVA_HOME)/lib/server
else ifeq (linux, $(findstring linux, $(BL_PROP_PLAT)))
  # devenv7+ Linux JDK 25: Modern structure with lib/server for all architectures
  $(utf_baselib_jni_ARTIFACT): LDADD += $(JAVA_HOME)/lib/server/libjsig.so
  $(utf_baselib_jni_ARTIFACT): LDADD += -Wl,-rpath,$(JAVA_HOME)/lib/server
endif

$(info Building with Gradle = $(GRADLE))

# Gradle daemon control: disabled by default for CI stability
# Set BL_GRADLE_START_SERVER=1 to enable daemon for faster local builds
ifdef BL_GRADLE_START_SERVER
  # Local development mode - use daemon for speed (omit --no-daemon)
  GRADLE_DAEMON_FLAG :=
else
  # CI mode (default) - no daemon for predictability
  GRADLE_DAEMON_FLAG := --no-daemon
endif

# Gradle 9+ uses -p (project directory) instead of --build-file
ifeq (, $(BL_DEVENV_IS_LEGACY))
GRADLE_ARGS := -p $(JAVADIR) $(GRADLE_DAEMON_FLAG)
else
GRADLE_ARGS := --build-file $(JAVADIR)/build.gradle $(GRADLE_DAEMON_FLAG)
endif
BASELIB_JAR_SRC_FILE := $(BLDDIR)/../../java/utf_baselib_jni/build/libs/utf_baselib_jni.jar
BASELIB_JAR_DEST_DIR := $(BLDDIR)/utests/utf_baselib_jni/utf-baselib-jni-lib

utf_baselib_jni:
	$(GRADLE) $(GRADLE_ARGS) clean
	$(GRADLE) $(GRADLE_ARGS) jar
	@mkdir -p $(BASELIB_JAR_DEST_DIR)
	@cp $(BASELIB_JAR_SRC_FILE) $(BASELIB_JAR_DEST_DIR)
