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
# Gradle Installation Script for macOS
# This script downloads and installs Gradle for use with swblocks-baselib
#
# Usage: ./install-gradle-macos.sh [GRADLE_VERSION] [DEVENV_TAG]
#   GRADLE_VERSION: Gradle version to install (default: 9.2.1 - latest)
#   DEVENV_TAG:     devenv tag (default: devenv7)
#
# Examples:
#   ./install-gradle-macos.sh                      # Install 9.2.1 devenv7
#   ./install-gradle-macos.sh 9.2.1                # Install 9.2.1 devenv7
#   ./install-gradle-macos.sh 8.5 devenv6          # Install 8.5 devenv6
###############################################################################

set -e  # Exit on error
set -u  # Exit on undefined variable

# Parse command line arguments
GRADLE_VERSION="${1:-9.2.1}"
DEVENV_TAG="${2:-devenv7}"

# Gradle download configuration
GRADLE_ARCHIVE="gradle-${GRADLE_VERSION}-all.zip"
GRADLE_URL="https://services.gradle.org/distributions/${GRADLE_ARCHIVE}"
GRADLE_DIR="gradle-${GRADLE_VERSION}"

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
elif [ "$ARCH" = "x86_64" ]; then
    ARCH_TAG="x64"
else
    echo "Unsupported architecture: $ARCH"
    exit 1
fi

# Installation paths
GRADLE_ROOT_DIR="${HOME}/swblocks/dist-${DEVENV_TAG}-darwin-${OS_NUMBER}-${ARCH_TAG}/gradle"
ZIP_DIR="${GRADLE_ROOT_DIR}/zip"
LATEST_DIR="${GRADLE_ROOT_DIR}/latest"
DEFAULT_DIR="${LATEST_DIR}/default"

echo "==========================================================================="
echo "Gradle ${GRADLE_VERSION} Installation Configuration"
echo "==========================================================================="
echo "macOS Version:    $(sw_vers -productVersion) (${OS_TAG})"
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
    curl -L "${GRADLE_URL}" -o "${ZIP_DIR}/${GRADLE_ARCHIVE}"
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
