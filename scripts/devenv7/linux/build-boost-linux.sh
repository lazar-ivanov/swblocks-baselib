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
# Usage: ./build-boost-linux.sh [TOOLCHAIN] [BOOST_VERSION] [DEVENV_TAG] [COMPILER_VERSION] [DIST_TAG]
#   TOOLCHAIN:        Toolchain to use: gcc or clang (default: gcc)
#   BOOST_VERSION:    Boost version to build (default: 1.90.0)
#   DEVENV_TAG:       devenv tag (default: devenv7)
#   COMPILER_VERSION: GCC/Clang version to use (default: 15.2.0 for gcc, 20.1.0 for clang)
#   DIST_TAG:         Distribution tag for installation directory (default: same as COMPILER_TAG)
#                     Used for dual-toolchain builds (e.g., gcc1520-clang2010)
#
# Examples:
#   ./build-boost-linux.sh                                        # Build with GCC 15.2.0
#   ./build-boost-linux.sh gcc                                    # Build with GCC 15.2.0
#   ./build-boost-linux.sh clang                                  # Build with Clang 20.1.0
#   ./build-boost-linux.sh gcc 1.90.0                             # Build 1.90.0 with GCC 15.2.0
#   ./build-boost-linux.sh clang 1.90.0 devenv7 19.1.0            # Build with Clang 19.1.0
#   ./build-boost-linux.sh gcc 1.90.0 devenv7 15.2.0 gcc1520-clang2010  # Dual toolchain build
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
BOOST_VERSION="${1:-1.90.0}"
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

# Convert version to underscore format (e.g., 1.89.0 -> 1_89_0)
BOOST_VERSION_UNDERSCORE=$(echo "$BOOST_VERSION" | tr '.' '_')
BOOST_ARCHIVE="boost_${BOOST_VERSION_UNDERSCORE}.tar.gz"
BOOST_URL="https://archives.boost.io/release/${BOOST_VERSION}/source/${BOOST_ARCHIVE}"
BOOST_DIR="boost_${BOOST_VERSION_UNDERSCORE}"

# Detect architecture
ARCH=$(uname -m)
if [ "$ARCH" = "aarch64" ] || [ "$ARCH" = "arm64" ]; then
    ARCH_TAG="a64"
    ARCH_TRIPLET="aarch64-unknown-linux-gnu"
elif [ "$ARCH" = "x86_64" ]; then
    ARCH_TAG="x64"
    ARCH_TRIPLET="x86_64-pc-linux-gnu"
elif [ "$ARCH" = "i386" ] || [ "$ARCH" = "i486" ] || [ "$ARCH" = "i586" ] || [ "$ARCH" = "i686" ]; then
    ARCH_TAG="x86"
    ARCH_TRIPLET="i686-pc-linux-gnu"
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

# When building with clang, override ARCH_TRIPLET to use 'unknown' vendor.
# LLVM normalizes all target triples to 'unknown' internally, so clang installs
# its libraries under e.g. lib/x86_64-unknown-linux-gnu/ (not x86_64-pc-linux-gnu/).
# GCC uses 'pc', so ARCH_TRIPLET is only overridden for clang.
if [ "$TOOLCHAIN" = "clang" ]; then
    case "$ARCH_TAG" in
        x64) ARCH_TRIPLET="x86_64-unknown-linux-gnu" ;;
        x86) ARCH_TRIPLET="i686-unknown-linux-gnu" ;;
    esac
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
TOOLCHAIN_BASE_DIR="${HOME}/swblocks/dist-${DEVENV_TAG}-${OS_TAG}-${DIST_TAG}-${ARCH_TAG}/toolchain-${TOOLCHAIN}/${COMPILER_VERSION}"
TOOLCHAIN_INSTALL_DIR="${TOOLCHAIN_BASE_DIR}/${BUILD_TAG}-release"

# Verify toolchain installation exists
if [ ! -d "$TOOLCHAIN_INSTALL_DIR" ]; then
    echo "ERROR: ${TOOLCHAIN} installation not found at: $TOOLCHAIN_INSTALL_DIR"
    echo "Please build ${TOOLCHAIN} first using build-${TOOLCHAIN}-linux.sh"
    exit 1
fi

# Installation paths
# VERSION_DIR uses DIST_TAG for dual-toolchain support
# The shared folders (tar, source-linux) are common across toolchains
VERSION_DIR="${HOME}/swblocks/dist-${DEVENV_TAG}-${OS_TAG}-${DIST_TAG}-${ARCH_TAG}/boost/${BOOST_VERSION}"
ARCHIVE_DIR="${VERSION_DIR}/tar"
SOURCE_DIR="${VERSION_DIR}/source-linux"

# Number of parallel jobs
JOBS=$(nproc)
JOBS=${BL_MAKE_JOBS:-${JOBS}}

echo "==========================================================================="
echo "Boost ${BOOST_VERSION} Build Configuration"
echo "==========================================================================="
echo "Toolchain:        ${TOOLCHAIN}"
echo "Architecture:     ${ARCH} (${ARCH_TAG})"
echo "OS Version:       $(lsb_release -ds) (${OS_TAG})"
echo "Compiler Version: ${COMPILER_VERSION} (${COMPILER_TAG})"
echo "Compiler Dir:     ${TOOLCHAIN_INSTALL_DIR}"
echo "Build Tag:        ${BUILD_TAG}"
echo "DevEnv Tag:       ${DEVENV_TAG}"
echo "Dist Tag:         ${DIST_TAG}"
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

# Create user-config.jam based on toolchain
echo "Creating user-config.jam..."
if [ "$TOOLCHAIN" = "gcc" ]; then
    # GCC configuration
    # Note: Not including -lstdc++ in linkflags because libstdc++ will be
    # linked statically at final binary link time with -static-libstdc++
    cat > user-config.jam << EOF
using gcc : ${COMPILER_MAJOR}.${COMPILER_MINOR}
    : ${TOOLCHAIN_INSTALL_DIR}/bin/g++
    : <cxxflags>"-std=c++11 -fPIC"
      <linkflags>"-L${TOOLCHAIN_INSTALL_DIR}/lib64"
    ;
EOF
else
    # Clang configuration with libc++, compiler-rt, libunwind, and lld
    # Note: Not including -lc++ or -lc++abi in linkflags because these static libraries
    # will be linked at final binary link time with -Wl,-Bstatic
    cat > user-config.jam << EOF
using clang : ${COMPILER_MAJOR}.${COMPILER_MINOR}
    : ${TOOLCHAIN_INSTALL_DIR}/bin/clang++
    : <cxxflags>"-std=c++11 -fPIC -stdlib=libc++ -isystem ${TOOLCHAIN_INSTALL_DIR}/include/${ARCH_TRIPLET}/c++/v1"
      <linkflags>"-L${TOOLCHAIN_INSTALL_DIR}/lib/${ARCH_TRIPLET} -L${TOOLCHAIN_INSTALL_DIR}/lib -stdlib=libc++ -fuse-ld=lld -rtlib=compiler-rt --unwindlib=libunwind"
      <archiver>${TOOLCHAIN_INSTALL_DIR}/bin/llvm-ar
      <ranlib>${TOOLCHAIN_INSTALL_DIR}/bin/llvm-ranlib
    ;
EOF
fi

# Set up toolchain environment variables (needed for bootstrap and builds)
if [ "$TOOLCHAIN" = "gcc" ]; then
    export CC="${TOOLCHAIN_INSTALL_DIR}/bin/gcc"
    export CXX="${TOOLCHAIN_INSTALL_DIR}/bin/g++"
    export PATH="${TOOLCHAIN_INSTALL_DIR}/bin:${PATH}"
    export LD_LIBRARY_PATH="${TOOLCHAIN_INSTALL_DIR}/lib64:${LD_LIBRARY_PATH:-}"
    BOOTSTRAP_TOOLSET="gcc"
else
    export CC="${TOOLCHAIN_INSTALL_DIR}/bin/clang"
    export CXX="${TOOLCHAIN_INSTALL_DIR}/bin/clang++"
    export PATH="${TOOLCHAIN_INSTALL_DIR}/bin:${PATH}"
    # Clang libraries are in lib/arch-triplet subdirectory
    export LD_LIBRARY_PATH="${TOOLCHAIN_INSTALL_DIR}/lib/${ARCH_TRIPLET}:${TOOLCHAIN_INSTALL_DIR}/lib:${LD_LIBRARY_PATH:-}"
    BOOTSTRAP_TOOLSET="clang"
fi

# Bootstrap b2 if not already done, or if toolchain changed
# Create a marker file to track which toolchain was used for bootstrap
BOOTSTRAP_MARKER=".bootstrap_${TOOLCHAIN}"
if [ ! -f "./b2" ] || [ ! -f "$BOOTSTRAP_MARKER" ]; then
    echo "Bootstrapping Boost.Build with ${TOOLCHAIN}..."
    # Clean up any previous bootstrap artifacts
    rm -f ./b2 ./bjam .bootstrap_* 2>/dev/null || true

    ./bootstrap.sh \
        --with-toolset=${BOOTSTRAP_TOOLSET}

    # Create marker file to track this bootstrap
    touch "$BOOTSTRAP_MARKER"
else
    echo "Boost.Build already bootstrapped with ${TOOLCHAIN}."
fi

# Patch Boost.Locale Jamfile to fix iconv detection
# The iconv detection fails with static runtime linking on RHEL 9 and when using
# Clang with libc++, so we bypass it by forcing iconv to be found
if [[ "$OS_TAG" == rhel* ]] || [ "$TOOLCHAIN" = "clang" ]; then
    echo
    if [ "$TOOLCHAIN" = "clang" ]; then
        echo "Applying Clang-specific patch to Boost.Locale Jamfile..."
    else
        echo "Applying RHEL-specific patch to Boost.Locale Jamfile..."
    fi

    JAMFILE="libs/locale/build/Jamfile.v2"
    if [ ! -f "${JAMFILE}.orig" ]; then
        cp "${JAMFILE}" "${JAMFILE}.orig"
    fi

    # Modify the configure-full function to force found-iconv = true
    # This replaces the iconv detection logic (lines 254-267) that fails with static linking
    sed -i '/local found-iconv ;/,/^    }$/c\
    local found-iconv ;\
\
    # Patch: Force iconv to be found (bypasses broken static link detection)\
    found-iconv = true ;\
    flags-result += <define>BOOST_LOCALE_WITH_ICONV=1 ;' "${JAMFILE}"

    echo "  ✓ Jamfile patched to force iconv support"
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

    # Set toolset name and flags based on toolchain (environment already set globally)
    if [ "$TOOLCHAIN" = "gcc" ]; then
        TOOLSET_NAME="gcc-${COMPILER_MAJOR}.${COMPILER_MINOR}"
        EXTRA_CXX_FLAGS="-std=c++11 -fPIC -fvisibility=hidden"
        EXTRA_LINK_FLAGS="-L${TOOLCHAIN_INSTALL_DIR}/lib64 -Wl,-rpath,${TOOLCHAIN_INSTALL_DIR}/lib64"
    else
        TOOLSET_NAME="clang-${COMPILER_MAJOR}.${COMPILER_MINOR}"
        EXTRA_CXX_FLAGS="-std=c++11 -fPIC -fvisibility=hidden -stdlib=libc++ -isystem ${TOOLCHAIN_INSTALL_DIR}/include/${ARCH_TRIPLET}/c++/v1"
        EXTRA_LINK_FLAGS="-L${TOOLCHAIN_INSTALL_DIR}/lib/${ARCH_TRIPLET} -L${TOOLCHAIN_INSTALL_DIR}/lib -stdlib=libc++ -lc++abi -fuse-ld=lld -rtlib=compiler-rt --unwindlib=libunwind -Wl,-rpath,${TOOLCHAIN_INSTALL_DIR}/lib/${ARCH_TRIPLET}"
    fi

    # Build Boost libraries
    echo "Building Boost libraries for ${VARIANT}..."
    ./b2 \
        --user-config=user-config.jam \
        --prefix="${BUILD_DIR}" \
        --build-dir="${TEMP_BUILD_DIR}" \
        toolset=${TOOLSET_NAME} \
        address-model=64 \
        variant=${VARIANT_FLAG} \
        link=static \
        runtime-link=static \
        threading=multi \
        cxxflags="${CXX_FLAGS} ${EXTRA_CXX_FLAGS}" \
        linkflags="${EXTRA_LINK_FLAGS}" \
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
        --with-json \
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
echo "Toolchain:   ${TOOLCHAIN} ${COMPILER_VERSION}"
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
echo "==========================================================================="
