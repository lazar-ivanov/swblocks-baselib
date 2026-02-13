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
# OpenSSL Build Script for macOS (Apple Silicon / Intel)
# This script builds OpenSSL with static libraries for use with swblocks-baselib
# Both debug and release variants are built automatically
#
# Usage: ./build-openssl-macos.sh [OPENSSL_VERSION] [DEVENV_TAG]
#   OPENSSL_VERSION: OpenSSL version to build (default: 3.5.4 - latest LTS)
#   DEVENV_TAG:      devenv tag (default: devenv7)
#
# Examples:
#   ./build-openssl-macos.sh                       # Build 3.5.4 debug+release devenv7
#   ./build-openssl-macos.sh 3.5.4                 # Build 3.5.4 debug+release devenv7
#   ./build-openssl-macos.sh 3.0.12 devenv6        # Build 3.0.12 debug+release devenv6
###############################################################################

set -e  # Exit on error
set -u  # Exit on undefined variable

# Parse command line arguments
OPENSSL_VERSION="${1:-3.5.4}"
DEVENV_TAG="${2:-devenv7}"

# Convert version to OpenSSL archive format (e.g., 3.5.4 -> openssl-3.5.4)
OPENSSL_ARCHIVE="openssl-${OPENSSL_VERSION}.tar.gz"
OPENSSL_URL="https://www.openssl.org/source/${OPENSSL_ARCHIVE}"
OPENSSL_DIR="openssl-${OPENSSL_VERSION}"

# Detect architecture
ARCH=$(uname -m)
if [ "$ARCH" = "arm64" ]; then
    ARCH_TAG="a64"
    OPENSSL_ARCH="darwin64-arm64-cc"
elif [ "$ARCH" = "x86_64" ]; then
    ARCH_TAG="x64"
    OPENSSL_ARCH="darwin64-x86_64-cc"
else
    echo "Unsupported architecture: $ARCH"
    exit 1
fi

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

# Detect clang version
CLANG_VERSION=$(clang++ --version | head -1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1 | cut -d. -f1)
CLANG_TAG="clang${CLANG_VERSION}00"

# Build configuration
BUILD_TAG="${OS_TAG}-${ARCH_TAG}-${CLANG_TAG}"

# Extract OS number from OS_TAG (e.g., d25 -> 25)
OS_NUMBER="${OS_TAG:1}"

# Installation paths
VERSION_DIR="${HOME}/swblocks/dist-${DEVENV_TAG}-darwin-${OS_NUMBER}-${ARCH_TAG}/openssl/${OPENSSL_VERSION}"
ARCHIVE_DIR="${VERSION_DIR}/tar"
SOURCE_DIR="${VERSION_DIR}/source"
SOURCE_LINUX_DIR="${VERSION_DIR}/source-linux"

# Number of parallel jobs (3x CPU count)
NCPUS=$(sysctl -n hw.ncpu)
JOBS=$((NCPUS * 3))
JOBS=${BL_MAKE_JOBS:-${JOBS}}

echo "==========================================================================="
echo "OpenSSL ${OPENSSL_VERSION} Build Configuration"
echo "==========================================================================="
echo "Architecture:     ${ARCH} (${ARCH_TAG})"
echo "macOS Version:    $(sw_vers -productVersion) (${OS_TAG})"
echo "Clang Version:    ${CLANG_VERSION} (${CLANG_TAG})"
echo "Build Tag:        ${BUILD_TAG}"
echo "DevEnv Tag:       ${DEVENV_TAG}"
echo "OpenSSL Arch:     ${OPENSSL_ARCH}"
echo "Archive Dir:      ${ARCHIVE_DIR}"
echo "Source Dir:       ${SOURCE_DIR}"
echo "Source Linux Dir: ${SOURCE_LINUX_DIR}"
echo "CPU Count:        ${NCPUS}"
echo "Parallel Jobs:    ${JOBS} (${NCPUS} CPUs x 3)"
echo "Building:         debug and release variants"
echo "==========================================================================="
echo

# Create directories
echo "Creating directories..."
mkdir -p "${VERSION_DIR}"
mkdir -p "${ARCHIVE_DIR}"
mkdir -p "${SOURCE_DIR}"
mkdir -p "${SOURCE_LINUX_DIR}"

# Download OpenSSL if not already present
if [ ! -f "${ARCHIVE_DIR}/${OPENSSL_ARCHIVE}" ]; then
    echo "Downloading OpenSSL ${OPENSSL_VERSION}..."
    curl -L "${OPENSSL_URL}" -o "${ARCHIVE_DIR}/${OPENSSL_ARCHIVE}"
else
    echo "OpenSSL archive already downloaded."
fi

# Extract OpenSSL to both source and source-linux if not already extracted
if [ ! -f "${SOURCE_DIR}/Configure" ]; then
    echo "Extracting OpenSSL to source directory..."
    # Extract to a temporary location first
    TEMP_EXTRACT="${SOURCE_DIR}/temp_extract"
    mkdir -p "${TEMP_EXTRACT}"
    tar -xzf "${ARCHIVE_DIR}/${OPENSSL_ARCHIVE}" -C "${TEMP_EXTRACT}"

    # Move contents directly to source (without the openssl-x.x.x subdirectory)
    mv "${TEMP_EXTRACT}/${OPENSSL_DIR}"/* "${SOURCE_DIR}/"
    mv "${TEMP_EXTRACT}/${OPENSSL_DIR}"/.* "${SOURCE_DIR}/" 2>/dev/null || true

    # Clean up temporary extraction directory
    rm -rf "${TEMP_EXTRACT}"
    echo "OpenSSL extracted to ${SOURCE_DIR}"
else
    echo "OpenSSL already extracted to source directory."
fi

if [ ! -f "${SOURCE_LINUX_DIR}/Configure" ]; then
    echo "Extracting OpenSSL to source-linux directory..."
    # Extract to a temporary location first
    TEMP_EXTRACT="${SOURCE_LINUX_DIR}/temp_extract"
    mkdir -p "${TEMP_EXTRACT}"
    tar -xzf "${ARCHIVE_DIR}/${OPENSSL_ARCHIVE}" -C "${TEMP_EXTRACT}"

    # Move contents directly to source-linux (without the openssl-x.x.x subdirectory)
    mv "${TEMP_EXTRACT}/${OPENSSL_DIR}"/* "${SOURCE_LINUX_DIR}/"
    mv "${TEMP_EXTRACT}/${OPENSSL_DIR}"/.* "${SOURCE_LINUX_DIR}/" 2>/dev/null || true

    # Clean up temporary extraction directory
    rm -rf "${TEMP_EXTRACT}"
    echo "OpenSSL extracted to ${SOURCE_LINUX_DIR}"
else
    echo "OpenSSL already extracted to source-linux directory."
fi

# Function to build a specific variant
build_variant() {
    local VARIANT=$1
    local BUILD_DIR="${VERSION_DIR}/${BUILD_TAG}-${VARIANT}"
    local WORK_DIR="${SOURCE_LINUX_DIR}/build-${BUILD_TAG}-${VARIANT}"

    echo
    echo "==========================================================================="
    echo "Building ${VARIANT} variant..."
    echo "==========================================================================="
    echo "Install Dir:      ${BUILD_DIR}"
    echo "Work Dir:         ${WORK_DIR}"
    echo "==========================================================================="

    # Create a separate build directory by copying source
    if [ -d "${WORK_DIR}" ]; then
        echo "Removing existing work directory..."
        rm -rf "${WORK_DIR}"
    fi

    echo "Creating work directory from source-linux..."
    cp -R "${SOURCE_LINUX_DIR}" "${WORK_DIR}"
    cd "${WORK_DIR}"

    # Determine variant-specific flags
    if [ "$VARIANT" = "debug" ]; then
        CONFIG_OPTIONS="--debug -g -O0"
    else
        CONFIG_OPTIONS="-O3"
    fi

    # Configure OpenSSL
    echo "Configuring OpenSSL for ${VARIANT}..."
    ./Configure \
        ${OPENSSL_ARCH} \
        ${CONFIG_OPTIONS} \
        no-shared \
        --prefix="${BUILD_DIR}" \
        --openssldir="${BUILD_DIR}/openssl"

    # Build OpenSSL
    echo "Building OpenSSL for ${VARIANT}..."
    make -j${JOBS}

    # Run tests before installing
    echo "Running tests for ${VARIANT} with ${JOBS} parallel test jobs..."
    export HARNESS_JOBS=${JOBS}
    make test

    # Install OpenSSL
    echo "Installing OpenSSL for ${VARIANT}..."
    make install

    # Clean up work directory
    echo "Cleaning up work directory..."
    cd "${SOURCE_LINUX_DIR}"
    rm -rf "${WORK_DIR}"

    echo "${VARIANT} variant build complete!"
}

# Build both debug and release variants
build_variant "debug"
build_variant "release"

# Verify installation
echo
echo "==========================================================================="
echo "All Builds Complete!"
echo "==========================================================================="
echo "Version Dir: ${VERSION_DIR}"
echo
echo "Debug variant:"
echo "  Installation: ${VERSION_DIR}/${BUILD_TAG}-debug"
echo "  Binaries:     ${VERSION_DIR}/${BUILD_TAG}-debug/bin"
echo "  Headers:      ${VERSION_DIR}/${BUILD_TAG}-debug/include"
echo "  Libraries:    ${VERSION_DIR}/${BUILD_TAG}-debug/lib"
if [ -d "${VERSION_DIR}/${BUILD_TAG}-debug/lib" ]; then
    echo "  Library files:"
    ls -lh "${VERSION_DIR}/${BUILD_TAG}-debug/lib"/*.a 2>/dev/null | awk '{print "    " $9, $5}'
fi
echo
echo "Release variant:"
echo "  Installation: ${VERSION_DIR}/${BUILD_TAG}-release"
echo "  Binaries:     ${VERSION_DIR}/${BUILD_TAG}-release/bin"
echo "  Headers:      ${VERSION_DIR}/${BUILD_TAG}-release/include"
echo "  Libraries:    ${VERSION_DIR}/${BUILD_TAG}-release/lib"
if [ -d "${VERSION_DIR}/${BUILD_TAG}-release/lib" ]; then
    echo "  Library files:"
    ls -lh "${VERSION_DIR}/${BUILD_TAG}-release/lib"/*.a 2>/dev/null | awk '{print "    " $9, $5}'
fi
echo
echo "To use this OpenSSL build, update your project's DIST_ROOT_DEPS paths to:"
echo "  ${VERSION_DIR%/*}"
echo "==========================================================================="
echo

# Make the entire openssl directory read-only (parent of VERSION_DIR)
OPENSSL_ROOT_DIR="${VERSION_DIR%/*}"
echo "Making ${OPENSSL_ROOT_DIR} read-only recursively..."
chmod -R a-w "${OPENSSL_ROOT_DIR}"
echo "Done! All files in ${OPENSSL_ROOT_DIR} are now read-only."
echo "==========================================================================="
