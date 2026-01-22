ifndef JDK_COMMON_INCLUDED
JDK_COMMON_INCLUDED = 1

# Determine JDK version and path based on devenv
ifeq ($(DEVENV_VERSION_TAG),devenv7)
JDK_VERSION = 25
# devenv7 uses architecture-specific paths on Windows (openjdk/25/a64, openjdk/25/x64)
# Linux/macOS use single default path (openjdk/25/default)
ifeq (win, $(findstring win, $(OS)))
  JDK_BASE_PATH = $(DIST_ROOT_DEPS3)/openjdk/$(JDK_VERSION)/$(ARCH)
else
  JDK_BASE_PATH = $(DIST_ROOT_DEPS3)/openjdk/$(JDK_VERSION)/default
endif
else
JDK_VERSION = 8
# Older devenvs use jdk/open-jdk/8/<os>-<arch> structure
ARCH_JDK = $(OS)-$(ARCH)
JDK_BASE_PATH = $(DIST_ROOT_DEPS3)/jdk/open-jdk/$(JDK_VERSION)/$(ARCH_JDK)
endif

ifneq ("$(wildcard $(JDK_BASE_PATH))","")

BL_JNI_ENABLED   := 1
JAVA_HOME        := $(JDK_BASE_PATH)

$(info Building with BL_JNI_ENABLED = $(BL_JNI_ENABLED))
$(info Building with JAVA_HOME = $(JAVA_HOME))
$(info Building with JDK_VERSION = $(JDK_VERSION))

JAR              = jar
JAVA	         = java
JAVAC            = javac

PATH            := $(JAVA_HOME)/bin:$(PATH)
INCLUDE         += $(JAVA_HOME)/include

ifeq (windows, $(findstring windows, $(BL_PROP_PLAT)))
INCLUDE		+= $(JAVA_HOME)/include/win32
else ifeq (linux, $(findstring linux, $(BL_PROP_PLAT)))
INCLUDE		+= $(JAVA_HOME)/include/linux
else ifeq (darwin, $(findstring darwin, $(BL_PROP_PLAT)))
INCLUDE		+= $(JAVA_HOME)/include/darwin
endif

export JAVA_HOME
export PATH

endif # ifeq ($(BL_JNI_ENABLED),1)

endif # JDK_COMMON_INCLUDED
