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
# GCC Build Script for Linux (Ubuntu 24.04 ARM64)
# This script downloads and builds GCC from source for use with swblocks-baselib
#
# Usage: ./build-gcc-linux.sh [GCC_VERSION] [DEVENV_TAG] [DIST_TAG]
#   GCC_VERSION: GCC version to build (default: 15.2.0)
#   DEVENV_TAG:  devenv tag (default: devenv7)
#   DIST_TAG:    Distribution tag for installation directory (default: same as GCC_TAG)
#                Used for dual-toolchain builds (e.g., gcc1520-clang2010)
#
# Examples:
#   ./build-gcc-linux.sh                                  # Build GCC 15.2.0 for devenv7
#   ./build-gcc-linux.sh 15.2.0                           # Build GCC 15.2.0 for devenv7
#   ./build-gcc-linux.sh 14.2.0 devenv7                   # Build GCC 14.2.0 for devenv7
#   ./build-gcc-linux.sh 15.2.0 devenv7 gcc1520-clang2010 # Build for dual toolchain
#
# Prerequisites:
#   sudo apt-get install build-essential libgmp-dev libmpfr-dev libmpc-dev \
#                        flex bison texinfo libzstd-dev
###############################################################################

set -e  # Exit on error
set -u  # Exit on undefined variable

# Parse command line arguments
GCC_VERSION="${1:-15.2.0}"
DEVENV_TAG="${2:-devenv7}"

# Extract version for tag (e.g., 15.2.0 -> gcc1520)
# Remove all dots from version string
GCC_VERSION_NO_DOTS=$(echo "$GCC_VERSION" | tr -d '.')
GCC_TAG="gcc${GCC_VERSION_NO_DOTS}"

# DIST_TAG is used for the top-level dist directory
# If not provided, defaults to GCC_TAG for single-toolchain builds
DIST_TAG="${3:-${GCC_TAG}}"

# Detect architecture
ARCH=$(uname -m)
if [ "$ARCH" = "aarch64" ] || [ "$ARCH" = "arm64" ]; then
    ARCH_TAG="a64"
    ARCH_TRIPLET="aarch64-unknown-linux-gnu"
elif [ "$ARCH" = "x86_64" ]; then
    ARCH_TAG="x64"
    ARCH_TRIPLET="x86_64-unknown-linux-gnu"
elif [ "$ARCH" = "i386" ] || [ "$ARCH" = "i486" ] || [ "$ARCH" = "i586" ] || [ "$ARCH" = "i686" ]; then
    ARCH_TAG="x86"
    ARCH_TRIPLET="i686-unknown-linux-gnu"
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

# Build configuration
BUILD_TAG="${OS_TAG}-${ARCH_TAG}-${GCC_TAG}"
VARIANT="release"

# GCC download configuration
GCC_ARCHIVE="gcc-${GCC_VERSION}.tar.gz"
GCC_URL="https://ftp.gnu.org/gnu/gcc/gcc-${GCC_VERSION}/${GCC_ARCHIVE}"
GCC_DIR="gcc-${GCC_VERSION}"

# Installation paths
# BASE_DIR uses DIST_TAG for dual-toolchain support
BASE_DIR="${HOME}/swblocks/dist-${DEVENV_TAG}-${OS_TAG}-${DIST_TAG}-${ARCH_TAG}"
VERSION_DIR="${BASE_DIR}/toolchain-gcc/${GCC_VERSION}"
ARCHIVE_DIR="${VERSION_DIR}/tar"
SOURCE_DIR="${VERSION_DIR}/source"
BUILD_DIR="${VERSION_DIR}/build"
INSTALL_DIR="${VERSION_DIR}/${BUILD_TAG}-${VARIANT}"

# Number of parallel jobs
JOBS=$(nproc)
JOBS=${BL_MAKE_JOBS:-${JOBS}}

echo "==========================================================================="
echo "GCC ${GCC_VERSION} Build Configuration"
echo "==========================================================================="
echo "Architecture:     $(uname -m) (${ARCH_TAG})"
echo "OS Version:       $(lsb_release -ds) (${OS_TAG})"
echo "GCC Tag:          ${GCC_TAG}"
echo "Build Tag:        ${BUILD_TAG}"
echo "DevEnv Tag:       ${DEVENV_TAG}"
echo "Archive Dir:      ${ARCHIVE_DIR}"
echo "Source Dir:       ${SOURCE_DIR}"
echo "Build Dir:        ${BUILD_DIR}"
echo "Install Dir:      ${INSTALL_DIR}"
echo "Parallel Jobs:    ${JOBS}"
echo "Bootstrap:        $([ "${BL_GCC_DISABLE_BOOTSTRAP:-0}" = "1" ] && echo "disabled" || echo "enabled")"
echo "Sanitizers:       $([ "${BL_GCC_DISABLE_SANITIZERS:-0}" = "1" ] && echo "disabled" || echo "enabled")"
echo "==========================================================================="

# Check prerequisites
echo ""
echo "Checking prerequisites..."
MISSING_DEPS=""
for cmd in make gcc g++ flex bison; do
    if ! command -v $cmd &> /dev/null; then
        MISSING_DEPS="${MISSING_DEPS} $cmd"
    fi
done

if [ -n "$MISSING_DEPS" ]; then
    echo "ERROR: Missing required tools:${MISSING_DEPS}"
    echo "Please install build dependencies:"
    echo "  sudo apt-get install build-essential libgmp-dev libmpfr-dev libmpc-dev flex bison texinfo libzstd-dev"
    exit 1
fi

# Create directory structure
echo ""
echo "Creating directory structure..."
mkdir -p "$ARCHIVE_DIR"
mkdir -p "$SOURCE_DIR"
mkdir -p "$BUILD_DIR"
mkdir -p "$INSTALL_DIR"

# Download GCC source if not already present
if [ ! -f "$ARCHIVE_DIR/$GCC_ARCHIVE" ]; then
    echo ""
    echo "Downloading GCC ${GCC_VERSION}..."
    cd "$ARCHIVE_DIR"
    wget "$GCC_URL"
    if [ $? -ne 0 ]; then
        echo "ERROR: Failed to download GCC from $GCC_URL"
        exit 1
    fi
else
    echo ""
    echo "GCC archive already exists: $ARCHIVE_DIR/$GCC_ARCHIVE"
fi

# Extract source if not already extracted
if [ ! -d "$SOURCE_DIR/$GCC_DIR" ]; then
    echo ""
    echo "Extracting GCC source..."
    cd "$SOURCE_DIR"
    tar -xzf "$ARCHIVE_DIR/$GCC_ARCHIVE"
    if [ $? -ne 0 ]; then
        echo "ERROR: Failed to extract GCC archive"
        exit 1
    fi
else
    echo ""
    echo "GCC source already extracted: $SOURCE_DIR/$GCC_DIR"
fi

# Download and extract prerequisites (GMP, MPFR, MPC, ISL)
echo ""
echo "Checking GCC prerequisites (GMP, MPFR, MPC, ISL)..."

# Check if all prerequisites are already downloaded
PREREQS_MISSING=0
for lib in gmp mpfr mpc isl; do
    if ! ls "$ARCHIVE_DIR"/${lib}-*.tar.* 1> /dev/null 2>&1; then
        PREREQS_MISSING=1
        break
    fi
done

if [ $PREREQS_MISSING -eq 1 ]; then
    echo "Downloading missing prerequisites..."
    # Run download_prerequisites in the GCC source directory
    cd "$SOURCE_DIR/$GCC_DIR"
    ./contrib/download_prerequisites
    if [ $? -ne 0 ]; then
        echo "ERROR: Failed to download prerequisites"
        exit 1
    fi

    # Move only the archives to the tar directory
    echo "Moving prerequisite archives to $ARCHIVE_DIR..."
    mv -f gmp-*.tar.* mpfr-*.tar.* mpc-*.tar.* isl-*.tar.* "$ARCHIVE_DIR/" 2>/dev/null || true

    # Remove any extracted directories that download_prerequisites may have created
    echo "Cleaning up extracted prerequisites from source directory..."
    rm -rf gmp-[0-9]* mpfr-[0-9]* mpc-[0-9]* isl-[0-9]* gmp mpfr mpc isl 2>/dev/null || true
else
    echo "All prerequisite archives already present in $ARCHIVE_DIR"
fi

# Extract prerequisites in the source directory (they will be cleaned up later)
if [ ! -f "$SOURCE_DIR/$GCC_DIR/.prerequisites_extracted" ]; then
    echo "Extracting prerequisites to source directory..."
    cd "$SOURCE_DIR/$GCC_DIR"
    for archive in "$ARCHIVE_DIR"/gmp-*.tar.* "$ARCHIVE_DIR"/mpfr-*.tar.* "$ARCHIVE_DIR"/mpc-*.tar.* "$ARCHIVE_DIR"/isl-*.tar.*; do
        if [ -f "$archive" ]; then
            echo "  Extracting $(basename "$archive")..."
            tar -xf "$archive"
        fi
    done

    # Create symlinks without version numbers as expected by GCC build
    for dir in gmp-* mpfr-* mpc-* isl-*; do
        if [ -d "$dir" ]; then
            base=$(echo "$dir" | sed 's/-[0-9].*//')
            if [ ! -e "$base" ]; then
                ln -sf "$dir" "$base"
            fi
        fi
    done

    touch .prerequisites_extracted
else
    echo "Prerequisites already extracted in source directory"
fi

# Configure GCC
if [ ! -f "$BUILD_DIR/Makefile" ]; then
    echo ""
    echo "Configuring GCC ${GCC_VERSION}..."
    cd "$BUILD_DIR"

    # Build optional configure flags
    EXTRA_CONFIGURE_FLAGS=""
    if [ "${BL_GCC_DISABLE_BOOTSTRAP:-0}" = "1" ]; then
        EXTRA_CONFIGURE_FLAGS="$EXTRA_CONFIGURE_FLAGS --disable-bootstrap"
    else
        EXTRA_CONFIGURE_FLAGS="$EXTRA_CONFIGURE_FLAGS --enable-bootstrap"
    fi
    if [ "${BL_GCC_DISABLE_SANITIZERS:-0}" = "1" ]; then
        EXTRA_CONFIGURE_FLAGS="$EXTRA_CONFIGURE_FLAGS --disable-libsanitizer"
    fi

    "$SOURCE_DIR/$GCC_DIR/configure" \
        --prefix="$INSTALL_DIR" \
        --build="$ARCH_TRIPLET" \
        --host="$ARCH_TRIPLET" \
        --target="$ARCH_TRIPLET" \
        --enable-languages=c,c++ \
        --disable-multilib \
        --enable-threads=posix \
        --enable-checking=release \
        --with-system-zlib \
        --enable-__cxa_atexit \
        --disable-libunwind-exceptions \
        --enable-gnu-unique-object \
        --enable-linker-build-id \
        --with-linker-hash-style=gnu \
        --enable-plugin \
        --enable-initfini-array \
        --enable-gnu-indirect-function \
        --with-tune=generic \
        --enable-lto \
        --with-pkgversion="swblocks-baselib ${DEVENV_TAG} build" \
        $EXTRA_CONFIGURE_FLAGS

    if [ $? -ne 0 ]; then
        echo "ERROR: GCC configuration failed"
        exit 1
    fi
else
    echo ""
    echo "GCC already configured"
fi

# Build GCC
if [ ! -f "$BUILD_DIR/.build_complete" ]; then
    echo ""
    echo "Building GCC ${GCC_VERSION} (this will take a while)..."
    echo "Using ${JOBS} parallel jobs"
    cd "$BUILD_DIR"

    make -j${JOBS}
    if [ $? -ne 0 ]; then
        echo "ERROR: GCC build failed"
        exit 1
    fi

    touch .build_complete
else
    echo ""
    echo "GCC already built"
fi

# Install GCC
if [ ! -f "$INSTALL_DIR/.install_complete" ]; then
    echo ""
    echo "Installing GCC ${GCC_VERSION} to ${INSTALL_DIR}..."
    cd "$BUILD_DIR"

    make install
    if [ $? -ne 0 ]; then
        echo "ERROR: GCC installation failed"
        exit 1
    fi

    touch "$INSTALL_DIR/.install_complete"
else
    echo ""
    echo "GCC already installed"
fi

# Verify installation
echo ""
echo "Verifying GCC installation..."
if [ -f "$INSTALL_DIR/bin/gcc" ] && [ -f "$INSTALL_DIR/bin/g++" ]; then
    echo ""
    echo "GCC installation successful!"
    echo ""
    echo "GCC version:"
    "$INSTALL_DIR/bin/gcc" --version | head -1
    echo ""
    echo "G++ version:"
    "$INSTALL_DIR/bin/g++" --version | head -1
    echo ""
    echo "Installation directory:"
    echo "  $INSTALL_DIR"
    echo ""
    echo "To use this GCC, add to your PATH:"
    echo "  export PATH=\"$INSTALL_DIR/bin:\$PATH\""
    echo "  export LD_LIBRARY_PATH=\"$INSTALL_DIR/lib64:\$LD_LIBRARY_PATH\""
else
    echo "ERROR: GCC binaries not found in $INSTALL_DIR/bin"
    exit 1
fi

# Cleanup intermediate files
echo ""
echo "Cleaning up intermediate build files..."
if [ -d "$BUILD_DIR" ]; then
    echo "Removing build directory: $BUILD_DIR"
    rm -rf "$BUILD_DIR"
fi

if [ -d "$SOURCE_DIR" ]; then
    echo "Removing source directory: $SOURCE_DIR"
    rm -rf "$SOURCE_DIR"
fi

echo ""
echo "Cleanup complete. Keeping only:"
echo "  - Archives in: $ARCHIVE_DIR"
echo "  - Installation in: $INSTALL_DIR"

# Make the entire toolchain-gcc directory read-only
GCC_ROOT_DIR="${BASE_DIR}/toolchain-gcc"
echo ""
echo "Making ${GCC_ROOT_DIR} read-only recursively..."
chmod -R a-w "${GCC_ROOT_DIR}"
echo "Done! All files in ${GCC_ROOT_DIR} are now read-only."

echo ""
echo "==========================================================================="
echo "GCC ${GCC_VERSION} Build Complete!"
echo "==========================================================================="
