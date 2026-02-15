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
# JSON Spirit Installation Script for macOS
# This script downloads and installs JSON Spirit source code for use with swblocks-baselib
#
# Usage: ./install-json-spirit-macos.sh [DEVENV_TAG]
#   DEVENV_TAG:     devenv tag (default: devenv7)
#
# Examples:
#   ./install-json-spirit-macos.sh              # Install devenv7
#   ./install-json-spirit-macos.sh devenv6      # Install devenv6
###############################################################################

set -e  # Exit on error
set -u  # Exit on undefined variable

# Parse command line arguments
DEVENV_TAG="${1:-devenv7}"

# JSON Spirit download configuration
JSON_SPIRIT_ARCHIVE="json-spirit.tar.gz"
JSON_SPIRIT_URL="https://github.com/lazar-ivanov/swblocks-assets/releases/download/json-spirit-4.08/${JSON_SPIRIT_ARCHIVE}"

# Detect macOS version
MACOS_VERSION=$(sw_vers -productVersion | cut -d. -f1)
if [ "$MACOS_VERSION" -ge 15 ]; then
    OS_TAG="25"  # macOS 15 (Sequoia) and above
elif [ "$MACOS_VERSION" -ge 14 ]; then
    OS_TAG="24"  # macOS 14 (Sonoma)
elif [ "$MACOS_VERSION" -ge 13 ]; then
    OS_TAG="23"  # macOS 13 (Ventura)
else
    OS_TAG="22"  # macOS 12 (Monterey) and below
fi

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

# Create distribution directory if it doesn't exist
DIST_ROOT_DIR="${HOME}/swblocks/dist-${DEVENV_TAG}-darwin-${OS_TAG}-${ARCH_TAG}"
if [ ! -d "$DIST_ROOT_DIR" ]; then
    echo "Distribution directory not found. Creating: $DIST_ROOT_DIR"
    mkdir -p "$DIST_ROOT_DIR"
    echo "Distribution directory created successfully."
    echo
fi

echo "==========================================================================="
echo "JSON Spirit Installation Configuration"
echo "==========================================================================="
echo "macOS Version:    $(sw_vers -productVersion) (OS tag: ${OS_TAG})"
echo "Architecture:     ${ARCH} (${ARCH_TAG})"
echo "DevEnv Tag:       ${DEVENV_TAG}"
echo "Installation Dir: ${DIST_ROOT_DIR}"
echo "==========================================================================="
echo

# Check if json-spirit already exists, remove it
if [ -d "${DIST_ROOT_DIR}/json-spirit" ]; then
    echo "Removing existing JSON Spirit installation..."
    rm -rf "${DIST_ROOT_DIR}/json-spirit"
fi

# Create temporary directory for download
TEMP_DIR=$(mktemp -d)
trap "rm -rf ${TEMP_DIR}" EXIT

# Download JSON Spirit
echo "Downloading JSON Spirit..."
curl -L "${JSON_SPIRIT_URL}" -o "${TEMP_DIR}/${JSON_SPIRIT_ARCHIVE}"
echo "Download complete."

# Extract JSON Spirit directly to dist directory
echo "Extracting JSON Spirit to ${DIST_ROOT_DIR}..."
tar -xzf "${TEMP_DIR}/${JSON_SPIRIT_ARCHIVE}" -C "${DIST_ROOT_DIR}"
echo "JSON Spirit extracted successfully."

# Verify installation
echo
echo "==========================================================================="
echo "Installation Complete!"
echo "==========================================================================="
echo "Installation Dir: ${DIST_ROOT_DIR}"
echo
echo "JSON Spirit contents:"
ls -lh "${DIST_ROOT_DIR}/json-spirit" 2>/dev/null | tail -n +2 | awk '{print "  " $9}' || echo "  (json-spirit directory structure may vary)"
echo
echo "To use this JSON Spirit installation, update your project's DIST_ROOT_DEPS paths to:"
echo "  ${DIST_ROOT_DIR}"
echo "==========================================================================="
echo "==========================================================================="
