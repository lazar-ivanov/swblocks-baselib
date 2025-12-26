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
# Clang/LLVM Build Script for Linux (Ubuntu 24.04 ARM64)
# This script downloads and builds Clang/LLVM from source as a standalone
# toolchain with libc++, compiler-rt, libunwind, and lld
#
# Usage: ./build-clang-linux.sh [CLANG_VERSION] [DEVENV_TAG] [DIST_TAG]
#   CLANG_VERSION: Clang/LLVM version to build (default: 20.1.0)
#   DEVENV_TAG:    devenv tag (default: devenv7)
#   DIST_TAG:      Distribution tag for installation directory (default: same as CLANG_TAG)
#                  Used for dual-toolchain builds (e.g., gcc1520-clang2010)
#
# Examples:
#   ./build-clang-linux.sh                                  # Build Clang 20.1.0 for devenv7
#   ./build-clang-linux.sh 20.1.0                           # Build Clang 20.1.0 for devenv7
#   ./build-clang-linux.sh 19.1.0 devenv7                   # Build Clang 19.1.0 for devenv7
#   ./build-clang-linux.sh 20.1.0 devenv7 gcc1520-clang2010 # Build for dual toolchain
#
# Prerequisites:
#   sudo apt-get install build-essential cmake ninja-build python3 \
#                        libxml2-dev libz-dev libedit-dev swig
###############################################################################

set -e  # Exit on error
set -u  # Exit on undefined variable

# Parse command line arguments
CLANG_VERSION="${1:-20.1.0}"
DEVENV_TAG="${2:-devenv7}"

# Extract version for tag (e.g., 20.1.0 -> clang2010)
# Remove all dots from version string
CLANG_VERSION_NO_DOTS=$(echo "$CLANG_VERSION" | tr -d '.')
CLANG_TAG="clang${CLANG_VERSION_NO_DOTS}"

# Extract major version for use in paths (e.g., 20.1.0 -> 20)
CLANG_MAJOR=$(echo "$CLANG_VERSION" | cut -d. -f1)

# Allow custom DIST_TAG or use CLANG_TAG as default
DIST_TAG="${3:-$CLANG_TAG}"

# Detect architecture
ARCH=$(uname -m)
if [ "$ARCH" = "aarch64" ] || [ "$ARCH" = "arm64" ]; then
    ARCH_TAG="a64"
    ARCH_TRIPLET="aarch64-unknown-linux-gnu"
elif [ "$ARCH" = "x86_64" ]; then
    ARCH_TAG="x64"
    ARCH_TRIPLET="x86_64-unknown-linux-gnu"
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
BUILD_TAG="${OS_TAG}-${ARCH_TAG}-${CLANG_TAG}"
VARIANT="release"

# LLVM download configuration
LLVM_ARCHIVE="llvm-project-${CLANG_VERSION}.src.tar.xz"
LLVM_URL="https://github.com/llvm/llvm-project/releases/download/llvmorg-${CLANG_VERSION}/${LLVM_ARCHIVE}"
LLVM_DIR="llvm-project-${CLANG_VERSION}.src"

# Installation paths following devenv5 structure
BASE_DIR="${HOME}/swblocks/dist-${DEVENV_TAG}-${OS_TAG}-${DIST_TAG}-arm"
VERSION_DIR="${BASE_DIR}/toolchain-clang/${CLANG_VERSION}"
ARCHIVE_DIR="${VERSION_DIR}/tar"
SOURCE_DIR="${VERSION_DIR}/source"
BUILD_DIR="${VERSION_DIR}/build"
INSTALL_DIR="${VERSION_DIR}/${BUILD_TAG}-${VARIANT}"

# Number of parallel jobs - LLVM builds are memory-intensive
# Calculate based on available memory (assume ~4GB per job for LLVM)
NPROC=$(nproc)
TOTAL_MEM_GB=$(free -g | awk '/^Mem:/{print $2}')
if [ "$TOTAL_MEM_GB" -gt 0 ]; then
    MEM_BASED_JOBS=$((TOTAL_MEM_GB / 4))
    # Use the minimum of CPU cores and memory-based jobs, but at least 1
    JOBS=$((MEM_BASED_JOBS < NPROC ? MEM_BASED_JOBS : NPROC))
    JOBS=$((JOBS < 1 ? 1 : JOBS))
else
    # Fallback: use half the cores for safety
    JOBS=$((NPROC / 2))
    JOBS=$((JOBS < 1 ? 1 : JOBS))
fi

echo "==========================================================================="
echo "Clang/LLVM ${CLANG_VERSION} Build Configuration"
echo "==========================================================================="
echo "Architecture:     $(uname -m) (${ARCH_TAG})"
echo "OS Version:       $(lsb_release -ds) (${OS_TAG})"
echo "Clang Tag:        ${CLANG_TAG}"
echo "Dist Tag:         ${DIST_TAG}"
echo "Build Tag:        ${BUILD_TAG}"
echo "DevEnv Tag:       ${DEVENV_TAG}"
echo "Archive Dir:      ${ARCHIVE_DIR}"
echo "Source Dir:       ${SOURCE_DIR}"
echo "Build Dir:        ${BUILD_DIR}"
echo "Install Dir:      ${INSTALL_DIR}"
echo "CPU Cores:        ${NPROC}"
echo "Total Memory:     ${TOTAL_MEM_GB} GB"
echo "Parallel Jobs:    ${JOBS} (memory-limited)"
echo "==========================================================================="

# Check prerequisites
echo ""
echo "Checking prerequisites..."
MISSING_DEPS=""
for cmd in make cmake ninja python3; do
    if ! command -v $cmd &> /dev/null; then
        MISSING_DEPS="${MISSING_DEPS} $cmd"
    fi
done

if [ -n "$MISSING_DEPS" ]; then
    echo "ERROR: Missing required tools:${MISSING_DEPS}"
    echo "Please install build dependencies:"
    if [ "$ID" = "ubuntu" ]; then
        echo "  sudo apt-get install build-essential cmake ninja-build python3 libxml2-dev libz-dev libedit-dev swig"
    elif [ "$ID" = "rhel" ]; then
        echo "  Note: ninja-build requires CodeReady Builder repository:"
        echo "    sudo subscription-manager repos --enable codeready-builder-for-rhel-${RHEL_VERSION}-\$(arch)-rpms"
        echo "  Then install:"
        echo "    sudo dnf install gcc gcc-c++ make cmake ninja-build python3 libxml2-devel zlib-devel libedit-devel swig"
    fi
    exit 1
fi

# Create directory structure
echo ""
echo "Creating directory structure..."
mkdir -p "$ARCHIVE_DIR"
mkdir -p "$SOURCE_DIR"
mkdir -p "$BUILD_DIR"
mkdir -p "$INSTALL_DIR"

# Download LLVM source if not already present
if [ ! -f "$ARCHIVE_DIR/$LLVM_ARCHIVE" ]; then
    echo ""
    echo "Downloading LLVM/Clang ${CLANG_VERSION}..."
    cd "$ARCHIVE_DIR"
    wget "$LLVM_URL"
    if [ $? -ne 0 ]; then
        echo "ERROR: Failed to download LLVM from $LLVM_URL"
        exit 1
    fi
else
    echo ""
    echo "LLVM archive already exists: $ARCHIVE_DIR/$LLVM_ARCHIVE"
fi

# Extract source if not already extracted
if [ ! -d "$SOURCE_DIR/$LLVM_DIR" ]; then
    echo ""
    echo "Extracting LLVM source..."
    cd "$SOURCE_DIR"
    tar -xf "$ARCHIVE_DIR/$LLVM_ARCHIVE"
    if [ $? -ne 0 ]; then
        echo "ERROR: Failed to extract LLVM archive"
        exit 1
    fi
else
    echo ""
    echo "LLVM source already extracted: $SOURCE_DIR/$LLVM_DIR"
fi

# Configure LLVM/Clang with CMake (Stage 1: Bootstrap with libstdc++)
# Note: Stage 1 uses system libstdc++ so the binaries can run independently
# Stage 2 will use the newly built libc++ for a fully standalone toolchain
if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    echo ""
    echo "Configuring LLVM/Clang ${CLANG_VERSION} Stage 1 (bootstrap) with CMake..."
    cd "$BUILD_DIR"

    cmake -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
        -DCMAKE_C_COMPILER_TARGET="$ARCH_TRIPLET" \
        -DCMAKE_CXX_COMPILER_TARGET="$ARCH_TRIPLET" \
        -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra;lld;compiler-rt" \
        -DLLVM_ENABLE_RUNTIMES="libcxx;libcxxabi;libunwind" \
        -DLLVM_TARGETS_TO_BUILD="host" \
        -DLLVM_ENABLE_ASSERTIONS=OFF \
        -DLLVM_INCLUDE_TESTS=OFF \
        -DLLVM_INCLUDE_EXAMPLES=OFF \
        -DLLVM_INCLUDE_BENCHMARKS=OFF \
        -DLLVM_ENABLE_BINDINGS=OFF \
        -DLLVM_ENABLE_OCAMLDOC=OFF \
        -DLLVM_ENABLE_Z3_SOLVER=OFF \
        -DCOMPILER_RT_DEFAULT_TARGET_ONLY=ON \
        -DLLVM_INSTALL_TOOLCHAIN_ONLY=ON \
        -DLLVM_TOOLCHAIN_TOOLS="llvm-ar;llvm-ranlib;llvm-objdump;llvm-objcopy;llvm-strip;llvm-nm;llvm-size;llvm-readelf;llvm-addr2line;llvm-symbolizer" \
        -DCLANG_VENDOR="swblocks-baselib ${DEVENV_TAG} build" \
        -DPACKAGE_VERSION="${CLANG_VERSION}" \
        "$SOURCE_DIR/$LLVM_DIR/llvm"

    if [ $? -ne 0 ]; then
        echo "ERROR: LLVM/Clang configuration failed"
        exit 1
    fi
else
    echo ""
    echo "LLVM/Clang already configured"
fi

# Build LLVM/Clang
if [ ! -f "$BUILD_DIR/.build_complete" ]; then
    echo ""
    echo "Building LLVM/Clang ${CLANG_VERSION} (this will take a while)..."
    echo "Using ${JOBS} parallel jobs"
    cd "$BUILD_DIR"

    ninja -j${JOBS}
    if [ $? -ne 0 ]; then
        echo "ERROR: LLVM/Clang build failed"
        exit 1
    fi

    touch .build_complete
else
    echo ""
    echo "LLVM/Clang already built"
fi

# Install LLVM/Clang
if [ ! -f "$INSTALL_DIR/.install_complete" ]; then
    echo ""
    echo "Installing LLVM/Clang ${CLANG_VERSION} to ${INSTALL_DIR}..."
    cd "$BUILD_DIR"

    ninja install
    if [ $? -ne 0 ]; then
        echo "ERROR: LLVM/Clang installation failed"
        exit 1
    fi

    touch "$INSTALL_DIR/.install_complete"
else
    echo ""
    echo "LLVM/Clang already installed"
fi

# Build stage 2: Rebuild with the newly built Clang to create a fully standalone toolchain
STAGE2_BUILD_DIR="${VERSION_DIR}/build-stage2"
STAGE2_INSTALL_DIR="${INSTALL_DIR}-stage2"

if [ ! -f "$STAGE2_INSTALL_DIR/.install_complete" ]; then
    echo ""
    echo "==========================================================================="
    echo "Stage 2: Building standalone Clang with libc++ and compiler-rt"
    echo "==========================================================================="

    mkdir -p "$STAGE2_BUILD_DIR"
    cd "$STAGE2_BUILD_DIR"

    # Use the stage 1 compiler to build stage 2
    export CC="$INSTALL_DIR/bin/clang"
    export CXX="$INSTALL_DIR/bin/clang++"

    # Add stage 1 library path so built tools can find libc++.so.1
    export LD_LIBRARY_PATH="$INSTALL_DIR/lib:${LD_LIBRARY_PATH:-}"

    echo ""
    echo "Configuring stage 2 build..."
    cmake -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$STAGE2_INSTALL_DIR" \
        -DCMAKE_C_COMPILER="$CC" \
        -DCMAKE_CXX_COMPILER="$CXX" \
        -DCMAKE_C_COMPILER_TARGET="$ARCH_TRIPLET" \
        -DCMAKE_CXX_COMPILER_TARGET="$ARCH_TRIPLET" \
        -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra;lld;compiler-rt" \
        -DLLVM_ENABLE_RUNTIMES="libcxx;libcxxabi;libunwind" \
        -DLLVM_TARGETS_TO_BUILD="host" \
        -DLLVM_ENABLE_ASSERTIONS=OFF \
        -DLLVM_INCLUDE_TESTS=OFF \
        -DLLVM_INCLUDE_EXAMPLES=OFF \
        -DLLVM_INCLUDE_BENCHMARKS=OFF \
        -DLLVM_ENABLE_BINDINGS=OFF \
        -DLLVM_ENABLE_OCAMLDOC=OFF \
        -DLLVM_ENABLE_Z3_SOLVER=OFF \
        -DCLANG_DEFAULT_CXX_STDLIB=libc++ \
        -DCLANG_DEFAULT_RTLIB=compiler-rt \
        -DCLANG_DEFAULT_UNWINDLIB=libunwind \
        -DCLANG_DEFAULT_LINKER=lld \
        -DCOMPILER_RT_DEFAULT_TARGET_ONLY=ON \
        -DLIBCXX_USE_COMPILER_RT=ON \
        -DLIBCXXABI_USE_COMPILER_RT=ON \
        -DLIBCXXABI_USE_LLVM_UNWINDER=ON \
        -DLIBUNWIND_USE_COMPILER_RT=ON \
        -DLLVM_INSTALL_TOOLCHAIN_ONLY=ON \
        -DLLVM_TOOLCHAIN_TOOLS="llvm-ar;llvm-ranlib;llvm-objdump;llvm-objcopy;llvm-strip;llvm-nm;llvm-size;llvm-readelf;llvm-addr2line;llvm-symbolizer" \
        -DCLANG_VENDOR="swblocks-baselib ${DEVENV_TAG} build" \
        -DPACKAGE_VERSION="${CLANG_VERSION}" \
        "$SOURCE_DIR/$LLVM_DIR/llvm"

    if [ $? -ne 0 ]; then
        echo "ERROR: Stage 2 configuration failed"
        exit 1
    fi

    echo ""
    echo "Building stage 2 (this will take a while)..."
    ninja -j${JOBS}
    if [ $? -ne 0 ]; then
        echo "ERROR: Stage 2 build failed"
        exit 1
    fi

    echo ""
    echo "Installing stage 2..."
    ninja install
    if [ $? -ne 0 ]; then
        echo "ERROR: Stage 2 installation failed"
        exit 1
    fi

    touch "$STAGE2_INSTALL_DIR/.install_complete"

    # Replace stage 1 with stage 2
    echo ""
    echo "Replacing stage 1 with stage 2..."
    rm -rf "$INSTALL_DIR"
    mv "$STAGE2_INSTALL_DIR" "$INSTALL_DIR"
else
    echo ""
    echo "Stage 2 already complete"
fi

# Verify installation
echo ""
echo "Verifying Clang installation..."
if [ -f "$INSTALL_DIR/bin/clang" ] && [ -f "$INSTALL_DIR/bin/clang++" ]; then
    echo ""
    echo "Clang installation successful!"
    echo ""
    echo "Clang version:"
    "$INSTALL_DIR/bin/clang" --version | head -1
    echo ""
    echo "Clang++ version:"
    "$INSTALL_DIR/bin/clang++" --version | head -1
    echo ""
    echo "Checking for libc++:"
    if [ -f "$INSTALL_DIR/lib/libc++.a" ] || [ -f "$INSTALL_DIR/lib/${ARCH_TRIPLET}/libc++.a" ]; then
        echo "  ✓ libc++ found"
    else
        echo "  ✗ WARNING: libc++ not found"
    fi
    echo ""
    echo "Checking for compiler-rt:"
    if ls "$INSTALL_DIR/lib/clang/${CLANG_MAJOR}"/lib/*/libclang_rt.builtins*.a 1> /dev/null 2>&1; then
        echo "  ✓ compiler-rt found"
    else
        echo "  ✗ WARNING: compiler-rt not found"
    fi
    echo ""
    echo "Checking for libunwind:"
    if [ -f "$INSTALL_DIR/lib/libunwind.a" ] || [ -f "$INSTALL_DIR/lib/${ARCH_TRIPLET}/libunwind.a" ]; then
        echo "  ✓ libunwind found"
    else
        echo "  ✗ WARNING: libunwind not found"
    fi
    echo ""
    echo "Checking for lld:"
    if [ -f "$INSTALL_DIR/bin/lld" ] || [ -f "$INSTALL_DIR/bin/ld.lld" ]; then
        echo "  ✓ lld linker found"
    else
        echo "  ✗ WARNING: lld linker not found"
    fi
    echo ""
    echo "Installation directory:"
    echo "  $INSTALL_DIR"
    echo ""
    echo "To use this Clang, add to your PATH:"
    echo "  export PATH=\"$INSTALL_DIR/bin:\$PATH\""
    echo "  export LD_LIBRARY_PATH=\"$INSTALL_DIR/lib:\$LD_LIBRARY_PATH\""
else
    echo "ERROR: Clang binaries not found in $INSTALL_DIR/bin"
    exit 1
fi

# Cleanup intermediate files
echo ""
echo "Cleaning up intermediate build files..."
if [ -d "$BUILD_DIR" ]; then
    echo "Removing stage 1 build directory: $BUILD_DIR"
    rm -rf "$BUILD_DIR"
fi

if [ -d "$STAGE2_BUILD_DIR" ]; then
    echo "Removing stage 2 build directory: $STAGE2_BUILD_DIR"
    rm -rf "$STAGE2_BUILD_DIR"
fi

if [ -d "$SOURCE_DIR" ]; then
    echo "Removing source directory: $SOURCE_DIR"
    rm -rf "$SOURCE_DIR"
fi

echo ""
echo "Cleanup complete. Keeping only:"
echo "  - Archives in: $ARCHIVE_DIR"
echo "  - Installation in: $INSTALL_DIR"

# Make the entire toolchain-clang directory read-only
CLANG_ROOT_DIR="${BASE_DIR}/toolchain-clang"
echo ""
echo "Making ${CLANG_ROOT_DIR} read-only recursively..."
chmod -R a-w "${CLANG_ROOT_DIR}"
echo "Done! All files in ${CLANG_ROOT_DIR} are now read-only."

echo ""
echo "==========================================================================="
echo "Clang/LLVM ${CLANG_VERSION} Build Complete!"
echo "==========================================================================="
echo ""
echo "The toolchain is fully standalone and uses:"
echo "  - libc++ as the C++ standard library"
echo "  - compiler-rt for runtime support"
echo "  - libunwind for stack unwinding"
echo "  - lld as the default linker"
echo ""
echo "No dependency on GCC libstdc++ or other GCC components."
echo "==========================================================================="
