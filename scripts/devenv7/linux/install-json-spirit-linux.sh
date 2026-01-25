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
# JSON Spirit Installation Script for Linux
# This script downloads and installs JSON Spirit source code for use with swblocks-baselib
#
# Usage: ./install-json-spirit-linux.sh DIST_TAG [DEVENV_TAG]
#   DIST_TAG:    Distribution tag for installation directory (required, e.g., gcc1520 or gcc1520-clang2010)
#   DEVENV_TAG:  devenv tag (default: devenv7)
#
# Examples:
#   ./install-json-spirit-linux.sh gcc1520                    # Install devenv7 with gcc1520
#   ./install-json-spirit-linux.sh gcc1520 devenv6            # Install devenv6 with gcc1520
#   ./install-json-spirit-linux.sh gcc1520-clang2010 devenv7  # Install for dual toolchain
###############################################################################

set -e  # Exit on error
set -u  # Exit on undefined variable

# Check if dist tag is provided
if [ $# -lt 1 ]; then
    echo "ERROR: Distribution tag is required"
    echo
    echo "Usage: $0 DIST_TAG [DEVENV_TAG]"
    echo
    echo "Examples:"
    echo "  $0 gcc1520                    # Install JSON Spirit devenv7 with gcc1520"
    echo "  $0 gcc1520 devenv6            # Install JSON Spirit devenv6 with gcc1520"
    echo "  $0 clang2010-gcc1520 devenv7  # Install for dual toolchain"
    echo
    exit 1
fi

# Parse command line arguments
DIST_TAG="$1"
DEVENV_TAG="${2:-devenv7}"

# JSON Spirit download configuration
JSON_SPIRIT_ARCHIVE="json-spirit.tar.gz"
JSON_SPIRIT_URL="https://github.com/lazar-ivanov/swblocks-assets/releases/download/json-spirit-4.08/${JSON_SPIRIT_ARCHIVE}"

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

# Create distribution directory if it doesn't exist
DIST_ROOT_DIR="${HOME}/swblocks/dist-${DEVENV_TAG}-${OS_TAG}-${DIST_TAG}-arm"
if [ ! -d "$DIST_ROOT_DIR" ]; then
    echo "Distribution directory not found. Creating: $DIST_ROOT_DIR"
    mkdir -p "$DIST_ROOT_DIR"
    echo "Distribution directory created successfully."
    echo
fi

echo "==========================================================================="
echo "JSON Spirit Installation Configuration"
echo "==========================================================================="
echo "OS Version:       $(lsb_release -ds) (${OS_TAG})"
echo "Dist Tag:         ${DIST_TAG}"
echo "DevEnv Tag:       ${DEVENV_TAG}"
echo "Installation Dir: ${DIST_ROOT_DIR}"
echo "==========================================================================="
echo

# Check if json-spirit already exists and make it writable if needed
if [ -d "${DIST_ROOT_DIR}/json-spirit" ]; then
    echo "Removing existing JSON Spirit installation..."
    chmod -R u+w "${DIST_ROOT_DIR}/json-spirit" 2>/dev/null || true
    rm -rf "${DIST_ROOT_DIR}/json-spirit"
fi

# Create temporary directory for download
TEMP_DIR=$(mktemp -d)
trap "rm -rf ${TEMP_DIR}" EXIT

# Download JSON Spirit
echo "Downloading JSON Spirit..."
wget "${JSON_SPIRIT_URL}" -O "${TEMP_DIR}/${JSON_SPIRIT_ARCHIVE}"
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
echo

# Make the json-spirit directory read-only
if [ -d "${DIST_ROOT_DIR}/json-spirit" ]; then
    echo "Making json-spirit directory read-only recursively..."
    chmod -R a-w "${DIST_ROOT_DIR}/json-spirit"
    echo "Done! JSON Spirit files are now read-only."
else
    echo "Warning: json-spirit directory not found after extraction"
fi
echo "==========================================================================="
