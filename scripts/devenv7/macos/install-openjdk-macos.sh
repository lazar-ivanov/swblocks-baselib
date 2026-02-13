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
# OpenJDK Installation Script for macOS
# This script downloads and installs OpenJDK for use with swblocks-baselib
#
# Usage: ./install-openjdk-macos.sh [JDK_VERSION] [DEVENV_TAG]
#   JDK_VERSION:    OpenJDK version to install (default: 25)
#   DEVENV_TAG:     devenv tag (default: devenv7)
#
# Examples:
#   ./install-openjdk-macos.sh                      # Install JDK 25 devenv7
#   ./install-openjdk-macos.sh 25                   # Install JDK 25 devenv7
#   ./install-openjdk-macos.sh 21 devenv6           # Install JDK 21 devenv6
#
# Directory structure created:
#   ${HOME}/swblocks/dist-devenv7-darwin-25-{ARCH_TAG}/jdk/open-jdk/25/d25-a64/
#   ${HOME}/swblocks/dist-devenv7-darwin-25-{ARCH_TAG}/jdk/open-jdk/25/d25-a64/include/
#   ${HOME}/swblocks/dist-devenv7-darwin-25-{ARCH_TAG}/jdk/open-jdk/25/d25-a64/include/darwin/
###############################################################################

set -e  # Exit on error
set -u  # Exit on undefined variable

# Parse command line arguments
JDK_VERSION="${1:-25}"
DEVENV_TAG="${2:-devenv7}"

# Detect macOS version
MACOS_VERSION=$(sw_vers -productVersion | cut -d. -f1)
if [ "$MACOS_VERSION" -ge 15 ]; then
    OS_TAG="d25"  # macOS 15 (Sequoia) and above
elif [ "$MACOS_VERSION" -ge 14 ]; then
    OS_TAG="d24"  # macOS 14 (Sonoma)
elif [ "$MACOS_VERSION" -ge 13 ]; then
    OS_TAG="d23"  # macOS 13 (Ventura)
else
    OS_TAG="d22"  # macOS 12 (Monterey) and below
fi

# Extract OS number from OS_TAG (e.g., d25 -> 25)
OS_NUMBER="${OS_TAG:1}"

# Detect architecture
ARCH=$(uname -m)
if [ "$ARCH" = "arm64" ]; then
    ARCH_TAG="a64"
    JDK_ARCH="aarch64"
else
    ARCH_TAG="x64"
    JDK_ARCH="x64"
fi

# OpenJDK download configuration
# Using Adoptium/Eclipse Temurin as the source for OpenJDK builds
JDK_ARCHIVE="OpenJDK${JDK_VERSION}U-jdk_${JDK_ARCH}_mac_hotspot.tar.gz"
JDK_URL="https://api.adoptium.net/v3/binary/latest/${JDK_VERSION}/ga/mac/${JDK_ARCH}/jdk/hotspot/normal/eclipse"

# Installation paths
JDK_ROOT_DIR="${HOME}/swblocks/dist-${DEVENV_TAG}-darwin-${OS_NUMBER}-${ARCH_TAG}/jdk"
JDK_OPENJDK_DIR="${JDK_ROOT_DIR}/open-jdk"
JDK_VERSION_DIR="${JDK_OPENJDK_DIR}/${JDK_VERSION}"
JDK_INSTALL_DIR="${JDK_VERSION_DIR}/${OS_TAG}-${ARCH_TAG}"
ARCHIVE_DIR="${JDK_ROOT_DIR}/archives"

echo "==========================================================================="
echo "OpenJDK ${JDK_VERSION} Installation Configuration"
echo "==========================================================================="
echo "macOS Version:    $(sw_vers -productVersion) (${OS_TAG})"
echo "Architecture:     ${ARCH} (${ARCH_TAG})"
echo "DevEnv Tag:       ${DEVENV_TAG}"
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
    curl -L "${JDK_URL}" -o "${ARCHIVE_DIR}/${JDK_ARCHIVE}"
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

# Find the JDK directory (usually jdk-<version>+<build>/Contents/Home)
JDK_EXTRACTED=$(find "${TEMP_EXTRACT}" -maxdepth 1 -name "jdk-*" -type d | head -1)
if [ -z "$JDK_EXTRACTED" ]; then
    echo "Error: Could not find extracted JDK directory"
    echo "Contents of ${TEMP_EXTRACT}:"
    ls -la "${TEMP_EXTRACT}"
    rm -rf "${TEMP_EXTRACT}"
    exit 1
fi

# Move the Contents/Home directory to the install location
# macOS JDK structure is: jdk-X.X.X+build/Contents/Home/
if [ -d "${JDK_EXTRACTED}/Contents/Home" ]; then
    mv "${JDK_EXTRACTED}/Contents/Home" "${JDK_INSTALL_DIR}"
else
    echo "Error: Expected JDK structure not found"
    echo "Contents of ${JDK_EXTRACTED}:"
    ls -la "${JDK_EXTRACTED}"
    rm -rf "${TEMP_EXTRACT}"
    exit 1
fi

# Clean up temporary extraction directory
rm -rf "${TEMP_EXTRACT}"

echo "OpenJDK extracted to ${JDK_INSTALL_DIR}"

# Verify the include directories exist
if [ -d "${JDK_INSTALL_DIR}/include" ]; then
    echo "Include directory verified: ${JDK_INSTALL_DIR}/include"

    # Check for darwin-specific headers
    if [ -d "${JDK_INSTALL_DIR}/include/darwin" ]; then
        echo "Darwin headers found: ${JDK_INSTALL_DIR}/include/darwin"
    else
        echo "Warning: darwin headers not found in ${JDK_INSTALL_DIR}/include/"
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
echo "  ${JDK_INSTALL_DIR}/include/darwin"
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
