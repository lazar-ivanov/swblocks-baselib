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
# Boost Build Script for Linux (Ubuntu 24.04 ARM64)
# This script builds Boost with static libraries for use with swblocks-baselib
# Both debug and release variants are built automatically
#
# Usage: ./build-boost-linux.sh [BOOST_VERSION] [DEVENV_TAG] [GCC_VERSION]
#   BOOST_VERSION: Boost version to build (default: 1.89.0)
#   DEVENV_TAG:    devenv tag (default: devenv7)
#   GCC_VERSION:   GCC version to use (default: 15.2.0)
#
# Examples:
#   ./build-boost-linux.sh                         # Build 1.89.0 debug+release devenv7 with GCC 15.2.0
#   ./build-boost-linux.sh 1.89.0                  # Build 1.89.0 debug+release devenv7 with GCC 15.2.0
#   ./build-boost-linux.sh 1.84.0 devenv7 14.2.0   # Build 1.84.0 debug+release devenv7 with GCC 14.2.0
###############################################################################

set -e  # Exit on error
set -u  # Exit on undefined variable

# Parse command line arguments
BOOST_VERSION="${1:-1.89.0}"
DEVENV_TAG="${2:-devenv7}"
GCC_VERSION="${3:-15.2.0}"

# Convert version to underscore format (e.g., 1.89.0 -> 1_89_0)
BOOST_VERSION_UNDERSCORE=$(echo "$BOOST_VERSION" | tr '.' '_')
BOOST_ARCHIVE="boost_${BOOST_VERSION_UNDERSCORE}.tar.gz"
BOOST_URL="https://archives.boost.io/release/${BOOST_VERSION}/source/${BOOST_ARCHIVE}"
BOOST_DIR="boost_${BOOST_VERSION_UNDERSCORE}"

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
VERSION_DIR="${HOME}/swblocks/dist-${DEVENV_TAG}-${OS_TAG}-${GCC_TAG}-arm/boost/${BOOST_VERSION}"
ARCHIVE_DIR="${VERSION_DIR}/tar"
SOURCE_DIR="${VERSION_DIR}/source-linux"

# Number of parallel jobs
JOBS=$(nproc)

echo "==========================================================================="
echo "Boost ${BOOST_VERSION} Build Configuration"
echo "==========================================================================="
echo "Architecture:     ${ARCH} (${ARCH_TAG})"
echo "OS Version:       $(lsb_release -ds) (${OS_TAG})"
echo "GCC Version:      ${GCC_VERSION} (${GCC_TAG})"
echo "GCC Install Dir:  ${GCC_INSTALL_DIR}"
echo "Build Tag:        ${BUILD_TAG}"
echo "DevEnv Tag:       ${DEVENV_TAG}"
echo "Archive Dir:      ${ARCHIVE_DIR}"
echo "Source Dir:       ${SOURCE_DIR}"
echo "Parallel Jobs:    ${JOBS}"
echo "Building:         debug and release variants"
echo "==========================================================================="
echo

# Create directories
echo "Creating directories..."
mkdir -p "${VERSION_DIR}"
mkdir -p "${ARCHIVE_DIR}"
mkdir -p "${SOURCE_DIR}"

# Download Boost if not already present
if [ ! -f "${ARCHIVE_DIR}/${BOOST_ARCHIVE}" ]; then
    echo "Downloading Boost ${BOOST_VERSION}..."
    wget "${BOOST_URL}" -O "${ARCHIVE_DIR}/${BOOST_ARCHIVE}"
else
    echo "Boost archive already downloaded."
fi

# Extract Boost if not already extracted
if [ ! -f "${SOURCE_DIR}/bootstrap.sh" ]; then
    echo "Extracting Boost..."
    # Extract to a temporary location first
    TEMP_EXTRACT="${SOURCE_DIR}/temp_extract"
    mkdir -p "${TEMP_EXTRACT}"
    tar -xzf "${ARCHIVE_DIR}/${BOOST_ARCHIVE}" -C "${TEMP_EXTRACT}"

    # Move contents directly to source-linux (without the boost_x_xx_x subdirectory)
    mv "${TEMP_EXTRACT}/${BOOST_DIR}"/* "${SOURCE_DIR}/"
    mv "${TEMP_EXTRACT}/${BOOST_DIR}"/.* "${SOURCE_DIR}/" 2>/dev/null || true

    # Clean up temporary extraction directory
    rm -rf "${TEMP_EXTRACT}"
    echo "Boost extracted to ${SOURCE_DIR}"
else
    echo "Boost already extracted."
fi

cd "${SOURCE_DIR}"

# Create user-config.jam for GCC toolchain
echo "Creating user-config.jam..."
cat > user-config.jam << EOF
using gcc : ${GCC_MAJOR}.${GCC_MINOR}
    : ${GCC_INSTALL_DIR}/bin/g++
    : <cxxflags>"-std=c++11 -fPIC"
      <linkflags>"-L${GCC_INSTALL_DIR}/lib64 -Wl,-rpath,${GCC_INSTALL_DIR}/lib64"
    ;
EOF

# Bootstrap b2 if not already done
if [ ! -f "./b2" ]; then
    echo "Bootstrapping Boost.Build..."
    # Use the custom GCC for bootstrapping
    export CC="${GCC_INSTALL_DIR}/bin/gcc"
    export CXX="${GCC_INSTALL_DIR}/bin/g++"
    export PATH="${GCC_INSTALL_DIR}/bin:${PATH}"
    export LD_LIBRARY_PATH="${GCC_INSTALL_DIR}/lib64:${LD_LIBRARY_PATH:-}"

    ./bootstrap.sh \
        --with-toolset=gcc
else
    echo "Boost.Build already bootstrapped."
fi

# Function to build a specific variant
build_variant() {
    local VARIANT=$1
    local BUILD_DIR="${VERSION_DIR}/${BUILD_TAG}-${VARIANT}"
    local TEMP_BUILD_DIR="${SOURCE_DIR}/build-${BUILD_TAG}-${VARIANT}"

    echo
    echo "==========================================================================="
    echo "Building ${VARIANT} variant..."
    echo "==========================================================================="
    echo "Install Dir:      ${BUILD_DIR}"
    echo "Temp Build Dir:   ${TEMP_BUILD_DIR}"
    echo "==========================================================================="

    mkdir -p "${BUILD_DIR}"

    # Determine variant-specific flags
    if [ "$VARIANT" = "debug" ]; then
        VARIANT_FLAG="debug"
        CXX_FLAGS="-g -O0"
    else
        VARIANT_FLAG="release"
        CXX_FLAGS="-O3"
    fi

    # Set up environment for GCC toolchain
    export PATH="${GCC_INSTALL_DIR}/bin:${PATH}"
    export LD_LIBRARY_PATH="${GCC_INSTALL_DIR}/lib64:${LD_LIBRARY_PATH:-}"

    # Build Boost libraries
    echo "Building Boost libraries for ${VARIANT}..."
    ./b2 \
        --user-config=user-config.jam \
        --prefix="${BUILD_DIR}" \
        --build-dir="${TEMP_BUILD_DIR}" \
        toolset=gcc-${GCC_MAJOR}.${GCC_MINOR} \
        address-model=64 \
        variant=${VARIANT_FLAG} \
        link=static \
        runtime-link=static \
        threading=multi \
        cxxflags="${CXX_FLAGS} -std=c++11 -fPIC -fvisibility=hidden" \
        linkflags="-L${GCC_INSTALL_DIR}/lib64 -Wl,-rpath,${GCC_INSTALL_DIR}/lib64" \
        -j${JOBS} \
        --layout=tagged \
        --build-type=complete \
        --with-date_time \
        --with-system \
        --with-thread \
        --with-filesystem \
        --with-program_options \
        --with-regex \
        --with-random \
        --with-test \
        --with-locale \
        boost.locale.iconv=on \
        boost.locale.icu=off \
        boost.locale.posix=on \
        install

    # Clean up intermediate build files
    echo
    echo "Cleaning up intermediate build files for ${VARIANT}..."
    if [ -d "${TEMP_BUILD_DIR}" ]; then
        echo "Removing ${TEMP_BUILD_DIR}..."
        rm -rf "${TEMP_BUILD_DIR}"
        echo "Intermediate build files removed."
    else
        echo "No intermediate build directory to clean."
    fi

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
echo "  Headers:   ${VERSION_DIR}/${BUILD_TAG}-debug/include"
echo "  Libraries: ${VERSION_DIR}/${BUILD_TAG}-debug/lib"
if [ -d "${VERSION_DIR}/${BUILD_TAG}-debug/lib" ]; then
    ls -lh "${VERSION_DIR}/${BUILD_TAG}-debug/lib"/*.a 2>/dev/null | wc -l | xargs echo "  Library count:"
fi
echo
echo "Release variant:"
echo "  Headers:   ${VERSION_DIR}/${BUILD_TAG}-release/include"
echo "  Libraries: ${VERSION_DIR}/${BUILD_TAG}-release/lib"
if [ -d "${VERSION_DIR}/${BUILD_TAG}-release/lib" ]; then
    ls -lh "${VERSION_DIR}/${BUILD_TAG}-release/lib"/*.a 2>/dev/null | wc -l | xargs echo "  Library count:"
fi
echo
echo "To use this Boost build, update your project's DIST_ROOT_DEPS paths to:"
echo "  ${VERSION_DIR%/*}"
echo "==========================================================================="
echo

# Make the entire boost directory read-only (parent of VERSION_DIR)
BOOST_ROOT_DIR="${VERSION_DIR%/*}"
echo "Making ${BOOST_ROOT_DIR} read-only recursively..."
chmod -R a-w "${BOOST_ROOT_DIR}"
echo "Done! All files in ${BOOST_ROOT_DIR} are now read-only."
echo "==========================================================================="
