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

ifneq (win, $(findstring win, $(OS)))
# JDK 8 and earlier use jre/lib/<arch>/server structure with .so
# JDK 9+ use lib/server structure with .dylib on macOS
ifeq ($(DEVENV_VERSION_TAG),devenv7)
  # JDK 25: Modern structure with lib/server (no arch subdirectory)
  ifeq (darwin, $(findstring darwin, $(BL_PROP_PLAT)))
    $(utf_baselib_jni_ARTIFACT): LDADD += $(JAVA_HOME)/lib/server/libjsig.dylib
    $(utf_baselib_jni_ARTIFACT): LDADD += -Wl,-rpath,$(JAVA_HOME)/lib/server
  else ifeq (linux, $(findstring linux, $(BL_PROP_PLAT)))
    # JDK 25 on Linux uses lib/server for all architectures
    $(utf_baselib_jni_ARTIFACT): LDADD += $(JAVA_HOME)/lib/server/libjsig.so
    $(utf_baselib_jni_ARTIFACT): LDADD += -Wl,-rpath,$(JAVA_HOME)/lib/server
  endif
else
  # JDK 8: Old structure with jre/lib/<arch>/server and .so
  ifeq (x86, $(ARCH))
    $(utf_baselib_jni_ARTIFACT): LDADD += $(JAVA_HOME)/jre/lib/i386/server/libjsig.so
  endif
  ifeq (x64, $(ARCH))
    $(utf_baselib_jni_ARTIFACT): LDADD += $(JAVA_HOME)/jre/lib/amd64/server/libjsig.so
  endif
  ifeq (a64, $(ARCH))
    $(utf_baselib_jni_ARTIFACT): LDADD += $(JAVA_HOME)/jre/lib/aarch64/server/libjsig.so
  endif
endif
endif

$(info Building with Gradle = $(GRADLE))

# Gradle 9+ uses -p (project directory) instead of --build-file
ifeq ($(DEVENV_VERSION_TAG),devenv7)
GRADLE_ARGS := -p $(JAVADIR)
else
GRADLE_ARGS := --build-file $(JAVADIR)/build.gradle
endif
BASELIB_JAR_SRC_FILE := $(BLDDIR)/../../java/utf_baselib_jni/build/libs/utf_baselib_jni.jar
BASELIB_JAR_DEST_DIR := $(BLDDIR)/utests/utf_baselib_jni/utf-baselib-jni-lib

utf_baselib_jni:
	$(GRADLE) $(GRADLE_ARGS) clean
	$(GRADLE) $(GRADLE_ARGS) jar
	@mkdir -p $(BASELIB_JAR_DEST_DIR)
	@cp $(BASELIB_JAR_SRC_FILE) $(BASELIB_JAR_DEST_DIR)
