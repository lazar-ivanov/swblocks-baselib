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
# Usage: ./build-openssl-linux.sh [TOOLCHAIN] [OPENSSL_VERSION] [DEVENV_TAG] [COMPILER_VERSION] [DIST_TAG]
#   TOOLCHAIN:        Toolchain to use: gcc or clang (default: gcc)
#   OPENSSL_VERSION:  OpenSSL version to build (default: 3.5.4 - latest LTS)
#   DEVENV_TAG:       devenv tag (default: devenv7)
#   COMPILER_VERSION: GCC/Clang version to use (default: 15.2.0 for gcc, 20.1.0 for clang)
#   DIST_TAG:         Distribution tag for installation directory (default: same as COMPILER_TAG)
#                     Used for dual-toolchain builds (e.g., gcc1520-clang2010)
#
# Examples:
#   ./build-openssl-linux.sh                                        # Build with GCC 15.2.0
#   ./build-openssl-linux.sh gcc                                    # Build with GCC 15.2.0
#   ./build-openssl-linux.sh clang                                  # Build with Clang 20.1.0
#   ./build-openssl-linux.sh gcc 3.5.4                              # Build 3.5.4 with GCC 15.2.0
#   ./build-openssl-linux.sh clang 3.5.4 devenv7 19.1.0             # Build with Clang 19.1.0
#   ./build-openssl-linux.sh gcc 3.5.4 devenv7 15.2.0 gcc1520-clang2010  # Dual toolchain build
###############################################################################

set -e  # Exit on error
set -u  # Exit on undefined variable

# Parse toolchain argument
TOOLCHAIN="${1:-gcc}"
if [ "$TOOLCHAIN" != "gcc" ] && [ "$TOOLCHAIN" != "clang" ]; then
    echo "ERROR: Invalid toolchain '$TOOLCHAIN'. Must be 'gcc' or 'clang'"
    exit 1
fi

# Shift arguments if toolchain was provided
if [ "$TOOLCHAIN" = "gcc" ] || [ "$TOOLCHAIN" = "clang" ]; then
    shift
fi

# Parse remaining command line arguments
OPENSSL_VERSION="${1:-3.5.4}"
DEVENV_TAG="${2:-devenv7}"

# Set default compiler version based on toolchain
if [ "$TOOLCHAIN" = "gcc" ]; then
    COMPILER_VERSION="${3:-15.2.0}"
else
    COMPILER_VERSION="${3:-20.1.0}"
fi

# Parse DIST_TAG parameter (5th parameter after shift)
# Will be set below after COMPILER_TAG is calculated if not provided
DIST_TAG_ARG="${4:-}"

# Check for required Perl modules and provide a single installation command
# We check for a few common modules and if any are missing, we recommend installing a bundle of packages
# to avoid having to install them one by one.
REQUIRED_PERL_MODULES="FindBin IPC::Cmd Text::Template Pod::Man Time::Piece Test::More"
for module in ${REQUIRED_PERL_MODULES}; do
    if ! perl -M${module} -e 1 &> /dev/null; then
        echo "ERROR: At least one required Perl module ('${module}') is not installed." >&2
        echo "The OpenSSL build process requires several Perl modules." >&2
        echo "To avoid resolving them one-by-one, it's recommended to install a comprehensive set of Perl packages for development." >&2
        echo >&2
        if command -v dnf &> /dev/null; then
            echo "On RHEL-based systems like yours, please run the following command to install them:" >&2
            echo "    sudo dnf install perl-devel perl-core perl-FindBin perl-IPC-Cmd perl-Text-Template perl-podlators perl-Time-Piece perl-Test-Simple perl-Test-Harness" >&2
        elif command -v apt-get &> /dev/null; then
            echo "On Debian/Ubuntu-based systems, please run the following command to install them:" >&2
            echo "    sudo apt-get install perl libtext-template-perl" >&2
        else
            echo "Please use your system's package manager to install the required Perl modules, including:" >&2
            echo "FindBin, IPC::Cmd, Text::Template, Pod::Man, Time::Piece, Test::More" >&2
        fi
        echo >&2
        exit 1
    fi
done

# Convert version to OpenSSL archive format (e.g., 3.5.4 -> openssl-3.5.4)
OPENSSL_ARCHIVE="openssl-${OPENSSL_VERSION}.tar.gz"
OPENSSL_URL="https://www.openssl.org/source/${OPENSSL_ARCHIVE}"
OPENSSL_DIR="openssl-${OPENSSL_VERSION}"

# Detect architecture
ARCH=$(uname -m)
if [ "$ARCH" = "aarch64" ] || [ "$ARCH" = "arm64" ]; then
    ARCH_TAG="a64"
    ARCH_TRIPLET="aarch64-unknown-linux-gnu"
    OPENSSL_ARCH="linux-aarch64"
elif [ "$ARCH" = "x86_64" ]; then
    ARCH_TAG="x64"
    ARCH_TRIPLET="x86_64-unknown-linux-gnu"
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

# Generate toolchain tag based on selected toolchain
if [ "$TOOLCHAIN" = "gcc" ]; then
    # Extract GCC version for tag (e.g., 15.2.0 -> gcc1520)
    # Remove all dots from version string
    COMPILER_VERSION_NO_DOTS=$(echo "$COMPILER_VERSION" | tr -d '.')
    COMPILER_TAG="gcc${COMPILER_VERSION_NO_DOTS}"
    COMPILER_MAJOR=$(echo "$COMPILER_VERSION" | cut -d. -f1)
    COMPILER_MINOR=$(echo "$COMPILER_VERSION" | cut -d. -f2)
else
    # Extract Clang version for tag (e.g., 20.1.0 -> clang2010)
    # Remove all dots from version string
    COMPILER_VERSION_NO_DOTS=$(echo "$COMPILER_VERSION" | tr -d '.')
    COMPILER_TAG="clang${COMPILER_VERSION_NO_DOTS}"
    COMPILER_MAJOR=$(echo "$COMPILER_VERSION" | cut -d. -f1)
    COMPILER_MINOR=$(echo "$COMPILER_VERSION" | cut -d. -f2)
fi

# Set DIST_TAG - defaults to COMPILER_TAG if not provided
# DIST_TAG is used for the top-level dist directory
if [ -n "$DIST_TAG_ARG" ]; then
    DIST_TAG="$DIST_TAG_ARG"
else
    DIST_TAG="$COMPILER_TAG"
fi

# Build configuration
BUILD_TAG="${OS_TAG}-${ARCH_TAG}-${COMPILER_TAG}"

# Toolchain path - uses DIST_TAG for dual-toolchain support
TOOLCHAIN_BASE_DIR="${HOME}/swblocks/dist-${DEVENV_TAG}-${OS_TAG}-${DIST_TAG}-arm/toolchain-${TOOLCHAIN}/${COMPILER_VERSION}"
TOOLCHAIN_INSTALL_DIR="${TOOLCHAIN_BASE_DIR}/${BUILD_TAG}-release"

# Verify toolchain installation exists
if [ ! -d "$TOOLCHAIN_INSTALL_DIR" ]; then
    echo "ERROR: ${TOOLCHAIN} installation not found at: $TOOLCHAIN_INSTALL_DIR"
    echo "Please build ${TOOLCHAIN} first using build-${TOOLCHAIN}-linux.sh"
    exit 1
fi

# Installation paths
# VERSION_DIR uses DIST_TAG for dual-toolchain support
# The shared folders (tar, source, source-linux) are common across toolchains
VERSION_DIR="${HOME}/swblocks/dist-${DEVENV_TAG}-${OS_TAG}-${DIST_TAG}-arm/openssl/${OPENSSL_VERSION}"
ARCHIVE_DIR="${VERSION_DIR}/tar"
SOURCE_DIR="${VERSION_DIR}/source"
SOURCE_LINUX_DIR="${VERSION_DIR}/source-linux"

# Number of parallel jobs (3x CPU count)
NCPUS=$(nproc)
JOBS=$((NCPUS * 3))

echo "==========================================================================="
echo "OpenSSL ${OPENSSL_VERSION} Build Configuration"
echo "==========================================================================="
echo "Toolchain:        ${TOOLCHAIN}"
echo "Architecture:     ${ARCH} (${ARCH_TAG})"
echo "OS Version:       $(lsb_release -ds) (${OS_TAG})"
echo "Compiler Version: ${COMPILER_VERSION} (${COMPILER_TAG})"
echo "Compiler Dir:     ${TOOLCHAIN_INSTALL_DIR}"
echo "Build Tag:        ${BUILD_TAG}"
echo "DevEnv Tag:       ${DEVENV_TAG}"
echo "Dist Tag:         ${DIST_TAG}"
echo "OpenSSL Arch:     ${OPENSSL_ARCH}"
echo "Archive Dir:      ${ARCHIVE_DIR}"
echo "Source Dir:       ${SOURCE_DIR}"
echo "Source Linux Dir: ${SOURCE_LINUX_DIR}"
echo "CPU Count:        ${NCPUS}"
echo "Parallel Jobs:    ${JOBS} (${NCPUS} CPUs x 3)"
echo "Building:         debug and release variants"
echo "==========================================================================="
echo

# Make openssl directory writable if it exists (for dual-toolchain builds)
OPENSSL_ROOT_DIR="${VERSION_DIR%/*}"
if [ -d "${OPENSSL_ROOT_DIR}" ]; then
    echo "OpenSSL directory exists, making it writable..."
    chmod -R u+w "${OPENSSL_ROOT_DIR}"
fi

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

    # Set up environment based on toolchain
    if [ "$TOOLCHAIN" = "gcc" ]; then
        export PATH="${TOOLCHAIN_INSTALL_DIR}/bin:${PATH}"
        export LD_LIBRARY_PATH="${TOOLCHAIN_INSTALL_DIR}/lib64:${LD_LIBRARY_PATH:-}"
        export CC="${TOOLCHAIN_INSTALL_DIR}/bin/gcc"
        export CXX="${TOOLCHAIN_INSTALL_DIR}/bin/g++"
    else
        # Clang configuration
        export PATH="${TOOLCHAIN_INSTALL_DIR}/bin:${PATH}"
        # Clang libraries are in lib/arch-triplet subdirectory
        export LD_LIBRARY_PATH="${TOOLCHAIN_INSTALL_DIR}/lib/${ARCH_TRIPLET}:${TOOLCHAIN_INSTALL_DIR}/lib:${LD_LIBRARY_PATH:-}"
        export CC="${TOOLCHAIN_INSTALL_DIR}/bin/clang"
        export CXX="${TOOLCHAIN_INSTALL_DIR}/bin/clang++"
        # Set Clang-specific flags for libc++, compiler-rt, libunwind, and lld
        # Note: Not including -lc++abi in LDFLAGS because libc++/libc++abi will be
        # linked statically at final binary link time with -Wl,-Bstatic
        export CFLAGS="-stdlib=libc++ -rtlib=compiler-rt --unwindlib=libunwind"
        export CXXFLAGS="-stdlib=libc++ -rtlib=compiler-rt --unwindlib=libunwind"
        export LDFLAGS="-fuse-ld=lld -stdlib=libc++ -rtlib=compiler-rt --unwindlib=libunwind -L${TOOLCHAIN_INSTALL_DIR}/lib/${ARCH_TRIPLET} -L${TOOLCHAIN_INSTALL_DIR}/lib"
    fi

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
echo "Toolchain:   ${TOOLCHAIN} ${COMPILER_VERSION}"
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
