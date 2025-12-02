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
# Gradle Installation Script for Linux (Ubuntu 24.04)
# This script downloads and installs Gradle for use with swblocks-baselib
#
# Usage: ./install-gradle-linux.sh TOOLCHAIN_TAG [GRADLE_VERSION] [DEVENV_TAG]
#   TOOLCHAIN_TAG:  Compiler toolchain tag (required, e.g., gcc1502, clang2010)
#   GRADLE_VERSION: Gradle version to install (default: 9.2.1 - latest)
#   DEVENV_TAG:     devenv tag (default: devenv7)
#
# Examples:
#   ./install-gradle-linux.sh gcc1502              # Install 9.2.1 devenv7 with gcc1502
#   ./install-gradle-linux.sh gcc1502 9.2.1        # Install 9.2.1 devenv7 with gcc1502
#   ./install-gradle-linux.sh clang2010 8.5 devenv6 # Install 8.5 devenv6 with clang2010
###############################################################################

set -e  # Exit on error
set -u  # Exit on undefined variable

# Check if toolchain tag is provided
if [ $# -lt 1 ]; then
    echo "ERROR: Compiler toolchain tag is required"
    echo
    echo "Usage: $0 TOOLCHAIN_TAG [GRADLE_VERSION] [DEVENV_TAG]"
    echo
    echo "Examples:"
    echo "  $0 gcc1502              # Install Gradle 9.2.1 devenv7 with gcc1502"
    echo "  $0 gcc1502 9.2.1        # Install Gradle 9.2.1 devenv7 with gcc1502"
    echo "  $0 clang2010 8.5 devenv6 # Install Gradle 8.5 devenv6 with clang2010"
    echo
    exit 1
fi

# Parse command line arguments
TOOLCHAIN_TAG="$1"
GRADLE_VERSION="${2:-9.2.1}"
DEVENV_TAG="${3:-devenv7}"

# Gradle download configuration
GRADLE_ARCHIVE="gradle-${GRADLE_VERSION}-all.zip"
GRADLE_URL="https://services.gradle.org/distributions/${GRADLE_ARCHIVE}"
GRADLE_DIR="gradle-${GRADLE_VERSION}"

# Detect architecture
ARCH=$(uname -m)
if [ "$ARCH" = "aarch64" ] || [ "$ARCH" = "arm64" ]; then
    ARCH_TAG="a64"
elif [ "$ARCH" = "x86_64" ]; then
    ARCH_TAG="x64"
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
    echo "distribution directory exists before installing Gradle."
    echo
    exit 1
fi

# Installation paths
GRADLE_ROOT_DIR="${DIST_ROOT_DIR}/gradle"
ZIP_DIR="${GRADLE_ROOT_DIR}/zip"
LATEST_DIR="${GRADLE_ROOT_DIR}/latest"
DEFAULT_DIR="${LATEST_DIR}/default"

echo "==========================================================================="
echo "Gradle ${GRADLE_VERSION} Installation Configuration"
echo "==========================================================================="
echo "Architecture:     ${ARCH} (${ARCH_TAG})"
echo "OS Version:       $(lsb_release -ds) (${OS_TAG})"
echo "Toolchain Tag:    ${TOOLCHAIN_TAG}"
echo "DevEnv Tag:       ${DEVENV_TAG}"
echo "Gradle Root:      ${GRADLE_ROOT_DIR}"
echo "Archive Dir:      ${ZIP_DIR}"
echo "Install Dir:      ${DEFAULT_DIR}"
echo "==========================================================================="
echo

# Create directories
echo "Creating directories..."
mkdir -p "${GRADLE_ROOT_DIR}"
mkdir -p "${ZIP_DIR}"
mkdir -p "${LATEST_DIR}"

# Download Gradle if not already present
if [ ! -f "${ZIP_DIR}/${GRADLE_ARCHIVE}" ]; then
    echo "Downloading Gradle ${GRADLE_VERSION}..."
    wget "${GRADLE_URL}" -O "${ZIP_DIR}/${GRADLE_ARCHIVE}"
    echo "Download complete."
else
    echo "Gradle archive already downloaded."
fi

# Remove existing default installation if present
if [ -d "${DEFAULT_DIR}" ]; then
    echo "Removing existing Gradle installation..."
    # Need to remove read-only protection first if it exists
    chmod -R u+w "${DEFAULT_DIR}" 2>/dev/null || true
    rm -rf "${DEFAULT_DIR}"
fi

# Extract Gradle
echo "Extracting Gradle ${GRADLE_VERSION}..."
TEMP_EXTRACT="${LATEST_DIR}/temp_extract"
mkdir -p "${TEMP_EXTRACT}"

# Unzip the Gradle archive
unzip -q "${ZIP_DIR}/${GRADLE_ARCHIVE}" -d "${TEMP_EXTRACT}"

# Move contents to default directory (without the gradle-x.x.x subdirectory)
mv "${TEMP_EXTRACT}/${GRADLE_DIR}" "${DEFAULT_DIR}"

# Clean up temporary extraction directory
rm -rf "${TEMP_EXTRACT}"

echo "Gradle extracted to ${DEFAULT_DIR}"

# Verify installation
echo
echo "==========================================================================="
echo "Installation Complete!"
echo "==========================================================================="
echo "Gradle Root:      ${GRADLE_ROOT_DIR}"
echo "Installation:     ${DEFAULT_DIR}"
echo "Gradle Version:   $(${DEFAULT_DIR}/bin/gradle --version | grep '^Gradle' || echo 'Unable to determine')"
echo
echo "Directory contents:"
ls -lh "${DEFAULT_DIR}" | tail -n +2 | awk '{print "  " $9, $5}'
echo
echo "To use this Gradle installation, add to your PATH:"
echo "  export PATH=\"${DEFAULT_DIR}/bin:\$PATH\""
echo
echo "Or update your project's DIST_ROOT_DEPS paths to:"
echo "  ${GRADLE_ROOT_DIR}"
echo "==========================================================================="
echo

# Make the entire gradle directory read-only
echo "Making ${GRADLE_ROOT_DIR} read-only recursively..."
chmod -R a-w "${GRADLE_ROOT_DIR}"
echo "Done! All files in ${GRADLE_ROOT_DIR} are now read-only."
echo "==========================================================================="
