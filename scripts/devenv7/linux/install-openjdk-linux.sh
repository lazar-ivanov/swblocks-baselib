#!/bin/bash

###############################################################################
# This file is part of the swblocks-baselib library.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
###############################################################################

###############################################################################
# OpenJDK Installation Script for Linux (Ubuntu 24.04)
# This script downloads and installs OpenJDK for use with swblocks-baselib
#
# Usage: ./install-openjdk-linux.sh TOOLCHAIN_TAG [JDK_VERSION] [DEVENV_TAG]
#   TOOLCHAIN_TAG:  Compiler toolchain tag (required, e.g., gcc1520, clang2010)
#   JDK_VERSION:    OpenJDK version to install (default: 25)
#   DEVENV_TAG:     devenv tag (default: devenv7)
#
# Examples:
#   ./install-openjdk-linux.sh gcc1520              # Install JDK 25 devenv7 with gcc1520
#   ./install-openjdk-linux.sh gcc1520 25           # Install JDK 25 devenv7 with gcc1520
#   ./install-openjdk-linux.sh clang2010 21 devenv6 # Install JDK 21 devenv6 with clang2010
#
# Directory structure created:
#   ${HOME}/swblocks/dist-devenv7-ub24-gcc1520-arm/jdk/open-jdk/25/ub24-a64/
#   ${HOME}/swblocks/dist-devenv7-ub24-gcc1520-arm/jdk/open-jdk/25/ub24-a64/include/
#   ${HOME}/swblocks/dist-devenv7-ub24-gcc1520-arm/jdk/open-jdk/25/ub24-a64/include/linux/
###############################################################################

set -e  # Exit on error
set -u  # Exit on undefined variable

# Check if toolchain tag is provided
if [ $# -lt 1 ]; then
    echo "ERROR: Compiler toolchain tag is required"
    echo
    echo "Usage: $0 TOOLCHAIN_TAG [JDK_VERSION] [DEVENV_TAG]"
    echo
    echo "Examples:"
    echo "  $0 gcc1520              # Install JDK 25 devenv7 with gcc1520"
    echo "  $0 gcc1520 25           # Install JDK 25 devenv7 with gcc1520"
    echo "  $0 clang2010 21 devenv6 # Install JDK 21 devenv6 with clang2010"
    echo
    exit 1
fi

# Parse command line arguments
TOOLCHAIN_TAG="$1"
JDK_VERSION="${2:-25}"
DEVENV_TAG="${3:-devenv7}"

# Detect architecture
ARCH=$(uname -m)
if [ "$ARCH" = "aarch64" ] || [ "$ARCH" = "arm64" ]; then
    ARCH_TAG="a64"
    JDK_ARCH="aarch64"
elif [ "$ARCH" = "x86_64" ]; then
    ARCH_TAG="x64"
    JDK_ARCH="x64"
else
    echo "Unsupported architecture: $ARCH"
    exit 1
fi

# Detect OS version
if [ -f /etc/os-release ]; then
    . /etc/os-release
    if [ "$ID" = "ubuntu" ]; then
        UBUNTU_VERSION=$(echo "$VERSION_ID" | cut -d. -f1)
        OS_TAG="ub${UBUNTU_VERSION}"
    elif [ "$ID" = "rhel" ]; then
        RHEL_VERSION=$(echo "$VERSION_ID" | cut -d. -f1)
        OS_TAG="rhel${RHEL_VERSION}"
    else
        echo "Unsupported OS: $ID"
        exit 1
    fi
else
    echo "Cannot detect OS version"
    exit 1
fi

# Verify distribution directory exists
DIST_ROOT_DIR="${HOME}/swblocks/dist-${DEVENV_TAG}-${OS_TAG}-${TOOLCHAIN_TAG}-arm"
if [ ! -d "$DIST_ROOT_DIR" ]; then
    echo "ERROR: Distribution directory not found: $DIST_ROOT_DIR"
    echo "Please ensure the toolchain tag '${TOOLCHAIN_TAG}' is correct and the corresponding"
    echo "distribution directory exists before installing OpenJDK."
    echo
    exit 1
fi

# OpenJDK download configuration
# Using Adoptium/Eclipse Temurin as the source for OpenJDK builds
JDK_ARCHIVE="OpenJDK${JDK_VERSION}U-jdk_${JDK_ARCH}_linux_hotspot.tar.gz"
JDK_URL="https://api.adoptium.net/v3/binary/latest/${JDK_VERSION}/ga/linux/${JDK_ARCH}/jdk/hotspot/normal/eclipse"

# Build tag for directory naming (JDK install dir does not include toolchain tag)
BUILD_TAG="${OS_TAG}-${ARCH_TAG}"

# Installation paths
JDK_ROOT_DIR="${DIST_ROOT_DIR}/jdk"
JDK_OPENJDK_DIR="${JDK_ROOT_DIR}/open-jdk"
JDK_VERSION_DIR="${JDK_OPENJDK_DIR}/${JDK_VERSION}"
JDK_INSTALL_DIR="${JDK_VERSION_DIR}/${BUILD_TAG}"
ARCHIVE_DIR="${JDK_ROOT_DIR}/archives"

echo "==========================================================================="
echo "OpenJDK ${JDK_VERSION} Installation Configuration"
echo "==========================================================================="
echo "Architecture:     ${ARCH} (${ARCH_TAG})"
echo "OS Version:       $(lsb_release -ds) (${OS_TAG})"
echo "Toolchain Tag:    ${TOOLCHAIN_TAG}"
echo "DevEnv Tag:       ${DEVENV_TAG}"
echo "Build Tag:        ${BUILD_TAG}"
echo "JDK Root:         ${JDK_ROOT_DIR}"
echo "Archive Dir:      ${ARCHIVE_DIR}"
echo "Install Dir:      ${JDK_INSTALL_DIR}"
echo "==========================================================================="
echo

# Create directories
echo "Creating directories..."
mkdir -p "${JDK_ROOT_DIR}"
mkdir -p "${JDK_OPENJDK_DIR}"
mkdir -p "${JDK_VERSION_DIR}"
mkdir -p "${ARCHIVE_DIR}"

# Download OpenJDK if not already present
if [ ! -f "${ARCHIVE_DIR}/${JDK_ARCHIVE}" ]; then
    echo "Downloading OpenJDK ${JDK_VERSION}..."
    wget "${JDK_URL}" -O "${ARCHIVE_DIR}/${JDK_ARCHIVE}"
    echo "Download complete."
else
    echo "OpenJDK archive already downloaded."
fi

# Remove existing installation if present
if [ -d "${JDK_INSTALL_DIR}" ]; then
    echo "Removing existing OpenJDK installation..."
    # Need to remove read-only protection first if it exists
    chmod -R u+w "${JDK_INSTALL_DIR}" 2>/dev/null || true
    rm -rf "${JDK_INSTALL_DIR}"
fi

# Extract OpenJDK
echo "Extracting OpenJDK ${JDK_VERSION}..."
TEMP_EXTRACT="${JDK_VERSION_DIR}/temp_extract"
mkdir -p "${TEMP_EXTRACT}"

# Extract the archive
tar -xzf "${ARCHIVE_DIR}/${JDK_ARCHIVE}" -C "${TEMP_EXTRACT}"

# Find the JDK directory (usually jdk-<version>+<build>)
JDK_EXTRACTED=$(find "${TEMP_EXTRACT}" -maxdepth 1 -name "jdk-*" -type d | head -1)
if [ -z "$JDK_EXTRACTED" ]; then
    echo "Error: Could not find extracted JDK directory"
    echo "Contents of ${TEMP_EXTRACT}:"
    ls -la "${TEMP_EXTRACT}"
    rm -rf "${TEMP_EXTRACT}"
    exit 1
fi

# Move the JDK directory to the install location
# Linux JDK structure is: jdk-X.X.X+build/ (no Contents/Home like macOS)
mv "${JDK_EXTRACTED}" "${JDK_INSTALL_DIR}"

# Clean up temporary extraction directory
rm -rf "${TEMP_EXTRACT}"

echo "OpenJDK extracted to ${JDK_INSTALL_DIR}"

# Verify the include directories exist
if [ -d "${JDK_INSTALL_DIR}/include" ]; then
    echo "Include directory verified: ${JDK_INSTALL_DIR}/include"

    # Check for linux-specific headers
    if [ -d "${JDK_INSTALL_DIR}/include/linux" ]; then
        echo "Linux headers found: ${JDK_INSTALL_DIR}/include/linux"
    else
        echo "Warning: linux headers not found in ${JDK_INSTALL_DIR}/include/"
    fi
else
    echo "Warning: Include directory not found at ${JDK_INSTALL_DIR}/include"
fi

# Verify installation
echo
echo "==========================================================================="
echo "Installation Complete!"
echo "==========================================================================="
echo "JDK Root:         ${JDK_ROOT_DIR}"
echo "Installation:     ${JDK_INSTALL_DIR}"
echo "JDK Version:      $(${JDK_INSTALL_DIR}/bin/java --version 2>/dev/null | head -1 || echo 'Unable to determine')"
echo
echo "Include paths for C/C++ compilation:"
echo "  ${JDK_INSTALL_DIR}/include"
echo "  ${JDK_INSTALL_DIR}/include/linux"
echo
echo "Directory structure:"
ls -lh "${JDK_INSTALL_DIR}" | tail -n +2 | awk '{print "  " $9, $5}'
echo
if [ -d "${JDK_INSTALL_DIR}/include" ]; then
    echo "Include directory contents:"
    ls -1 "${JDK_INSTALL_DIR}/include" | awk '{print "  " $0}'
    echo
fi
echo "To use this JDK installation, add to your environment:"
echo "  export JAVA_HOME=\"${JDK_INSTALL_DIR}\""
echo "  export PATH=\"\$JAVA_HOME/bin:\$PATH\""
echo
echo "Or update your project's DIST_ROOT_DEPS paths to:"
echo "  ${JDK_ROOT_DIR}"
echo "==========================================================================="
echo

# Make the entire JDK directory read-only
echo "Making ${JDK_ROOT_DIR} read-only recursively..."
chmod -R a-w "${JDK_ROOT_DIR}"
echo "Done! All files in ${JDK_ROOT_DIR} are now read-only."
echo "==========================================================================="
