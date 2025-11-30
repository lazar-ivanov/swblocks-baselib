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
# OpenSSL Build Script for Linux (Ubuntu 24.04 ARM64)
# This script builds OpenSSL with static libraries for use with swblocks-baselib
# Both debug and release variants are built automatically
#
# Usage: ./build-openssl-linux.sh [OPENSSL_VERSION] [DEVENV_TAG] [GCC_VERSION]
#   OPENSSL_VERSION: OpenSSL version to build (default: 3.5.4 - latest LTS)
#   DEVENV_TAG:      devenv tag (default: devenv7)
#   GCC_VERSION:     GCC version to use (default: 15.2.0)
#
# Examples:
#   ./build-openssl-linux.sh                       # Build 3.5.4 debug+release devenv7 with GCC 15.2.0
#   ./build-openssl-linux.sh 3.5.4                 # Build 3.5.4 debug+release devenv7 with GCC 15.2.0
#   ./build-openssl-linux.sh 3.0.12 devenv7 14.2.0 # Build 3.0.12 debug+release devenv7 with GCC 14.2.0
###############################################################################

set -e  # Exit on error
set -u  # Exit on undefined variable

# Parse command line arguments
OPENSSL_VERSION="${1:-3.5.4}"
DEVENV_TAG="${2:-devenv7}"
GCC_VERSION="${3:-15.2.0}"

# Convert version to OpenSSL archive format (e.g., 3.5.4 -> openssl-3.5.4)
OPENSSL_ARCHIVE="openssl-${OPENSSL_VERSION}.tar.gz"
OPENSSL_URL="https://www.openssl.org/source/${OPENSSL_ARCHIVE}"
OPENSSL_DIR="openssl-${OPENSSL_VERSION}"

# Detect architecture
ARCH=$(uname -m)
if [ "$ARCH" = "aarch64" ] || [ "$ARCH" = "arm64" ]; then
    ARCH_TAG="a64"
    OPENSSL_ARCH="linux-aarch64"
elif [ "$ARCH" = "x86_64" ]; then
    ARCH_TAG="x64"
    OPENSSL_ARCH="linux-x86_64"
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
    else
        echo "Unsupported OS: $ID"
        exit 1
    fi
else
    echo "Cannot detect OS version"
    exit 1
fi

# Extract GCC major/minor version for tag (e.g., 15.2.0 -> gcc1502)
GCC_MAJOR=$(echo "$GCC_VERSION" | cut -d. -f1)
GCC_MINOR=$(echo "$GCC_VERSION" | cut -d. -f2)
GCC_TAG="gcc${GCC_MAJOR}$(printf "%02d" $GCC_MINOR)"

# Build configuration
BUILD_TAG="${OS_TAG}-${ARCH_TAG}-${GCC_TAG}"

# GCC toolchain path
GCC_BASE_DIR="${HOME}/swblocks/dist-${DEVENV_TAG}-${OS_TAG}-${GCC_TAG}-arm/toolchain-gcc/${GCC_VERSION}"
GCC_INSTALL_DIR="${GCC_BASE_DIR}/${BUILD_TAG}-release"

# Verify GCC installation exists
if [ ! -d "$GCC_INSTALL_DIR" ]; then
    echo "ERROR: GCC installation not found at: $GCC_INSTALL_DIR"
    echo "Please build GCC first using build-gcc-linux.sh"
    exit 1
fi

# Installation paths
VERSION_DIR="${HOME}/swblocks/dist-${DEVENV_TAG}-${OS_TAG}-${GCC_TAG}-arm/openssl/${OPENSSL_VERSION}"
ARCHIVE_DIR="${VERSION_DIR}/tar"
SOURCE_DIR="${VERSION_DIR}/source"
SOURCE_LINUX_DIR="${VERSION_DIR}/source-linux"

# Number of parallel jobs
JOBS=$(nproc)

echo "==========================================================================="
echo "OpenSSL ${OPENSSL_VERSION} Build Configuration"
echo "==========================================================================="
echo "Architecture:     ${ARCH} (${ARCH_TAG})"
echo "OS Version:       $(lsb_release -ds) (${OS_TAG})"
echo "GCC Version:      ${GCC_VERSION} (${GCC_TAG})"
echo "GCC Install Dir:  ${GCC_INSTALL_DIR}"
echo "Build Tag:        ${BUILD_TAG}"
echo "DevEnv Tag:       ${DEVENV_TAG}"
echo "OpenSSL Arch:     ${OPENSSL_ARCH}"
echo "Archive Dir:      ${ARCHIVE_DIR}"
echo "Source Dir:       ${SOURCE_DIR}"
echo "Source Linux Dir: ${SOURCE_LINUX_DIR}"
echo "Parallel Jobs:    ${JOBS}"
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
    wget "${OPENSSL_URL}" -O "${ARCHIVE_DIR}/${OPENSSL_ARCHIVE}"
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
    local WORK_DIR="${VERSION_DIR}/build-${BUILD_TAG}-${VARIANT}"

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

    # Set up environment for GCC toolchain
    export PATH="${GCC_INSTALL_DIR}/bin:${PATH}"
    export LD_LIBRARY_PATH="${GCC_INSTALL_DIR}/lib64:${LD_LIBRARY_PATH:-}"
    export CC="${GCC_INSTALL_DIR}/bin/gcc"
    export CXX="${GCC_INSTALL_DIR}/bin/g++"

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

    # Run tests
    echo "Running tests for ${VARIANT}..."
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
