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
# Usage: ./build-openssl-linux.sh [OPTIONS] [TOOLCHAIN] [OPENSSL_VERSION] [DEVENV_TAG] [COMPILER_VERSION] [DIST_TAG]
#   OPTIONS:
#     --test-hw-acceleration-speed-only  Skip build/test/install, only run verification
#                                        checks (HW acceleration speed, optimization flags,
#                                        debug info flags) on already-built binaries.
#                                        Assumes build completed successfully.
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
#   ./build-openssl-linux.sh --test-hw-acceleration-speed-only clang       # Verify Clang build only
#   ./build-openssl-linux.sh --test-hw-acceleration-speed-only clang 3.5.4 devenv7 20.1.0 clang2010
###############################################################################

set -e  # Exit on error
set -u  # Exit on undefined variable

# Parse optional flags (before positional arguments)
TEST_HW_ACCEL_ONLY=0
if [ "${1:-}" = "--test-hw-acceleration-speed-only" ]; then
    TEST_HW_ACCEL_ONLY=1
    shift
fi

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

# Check prerequisites (includes Perl module packages for OpenSSL build)
# Skip when only running verification checks on existing builds
if [ "${TEST_HW_ACCEL_ONLY}" != "1" ]; then
    "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/check-prerequisites.sh"
fi

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
    ARCH_TRIPLET="x86_64-pc-linux-gnu"
    OPENSSL_ARCH="linux-x86_64"
elif [ "$ARCH" = "i386" ] || [ "$ARCH" = "i486" ] || [ "$ARCH" = "i586" ] || [ "$ARCH" = "i686" ]; then
    ARCH_TAG="x86"
    ARCH_TRIPLET="i686-pc-linux-gnu"
    OPENSSL_ARCH="linux-x86"
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
# The shared folders (tar, source, source-linux) are common across toolchains
VERSION_DIR="${HOME}/swblocks/dist-${DEVENV_TAG}-${OS_TAG}-${DIST_TAG}-${ARCH_TAG}/openssl/${OPENSSL_VERSION}"
ARCHIVE_DIR="${VERSION_DIR}/tar"
SOURCE_DIR="${VERSION_DIR}/source"
SOURCE_LINUX_DIR="${VERSION_DIR}/source-linux"

# Number of parallel jobs (3x CPU count)
NCPUS=$(nproc)
JOBS=$((NCPUS * 3))
JOBS=${BL_MAKE_JOBS:-${JOBS}}

echo "==========================================================================="
echo "OpenSSL ${OPENSSL_VERSION} Build Configuration"
echo "==========================================================================="
echo "Toolchain:        ${TOOLCHAIN}"
echo "Architecture:     ${ARCH} (${ARCH_TAG})"
echo "OS Version:       ${PRETTY_NAME} (${OS_TAG})"
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

###############################################################################
# OpenSSL Build Verification
# Verifies that an OpenSSL build has been configured correctly:
#   1. Hardware acceleration performance (AES-128-GCM speed test)
#   2. Optimization flags (debug: -O0 -g, release: -O3)
#   3. Debug information flags (debug only: -g or -ggdb)
# Matches the Windows verify-openssl-build.ps1 verification pattern.
###############################################################################

# Set up toolchain environment (PATH, LD_LIBRARY_PATH) for running openssl binary.
# Extracted from build_variant() so it can be reused by --test-hw-acceleration-speed-only.
setup_toolchain_env() {
    if [ "$TOOLCHAIN" = "gcc" ]; then
        export PATH="${TOOLCHAIN_INSTALL_DIR}/bin:${PATH}"
        export LD_LIBRARY_PATH="${TOOLCHAIN_INSTALL_DIR}/lib64:${LD_LIBRARY_PATH:-}"
    else
        export PATH="${TOOLCHAIN_INSTALL_DIR}/bin:${PATH}"
        export LD_LIBRARY_PATH="${TOOLCHAIN_INSTALL_DIR}/lib/${ARCH_TRIPLET}:${TOOLCHAIN_INSTALL_DIR}/lib:${LD_LIBRARY_PATH:-}"
    fi
}

# Verify an OpenSSL build: HW acceleration speed, optimization flags, debug info flags.
# Arguments: OPENSSL_EXE BUILD_TYPE(debug|release) ARCH_TAG_ARG(x86|x64|a64)
# Returns: 0 if all passed, 1 if any failed
verify_openssl_build() {
    local OPENSSL_EXE="$1"
    local BUILD_TYPE="$2"
    local ARCH_TAG_ARG="$3"

    local VERIFICATIONS_PASSED=0
    local VERIFICATIONS_FAILED=0

    # =========================================================================
    # Verification 1: Hardware Acceleration Performance
    # =========================================================================
    echo ""
    echo "[Step 1/3] Checking hardware acceleration performance..."
    echo "  Running: openssl speed -evp aes-128-gcm"

    local SPEED_OUTPUT
    SPEED_OUTPUT=$("${OPENSSL_EXE}" speed -evp aes-128-gcm 2>&1) || true

    local PERF_LINE
    PERF_LINE=$(echo "$SPEED_OUTPUT" | grep -E '^\s*AES-128-GCM\s+' || true)

    if [ -z "$PERF_LINE" ]; then
        echo "  [FAIL] Could not find AES-128-GCM performance line in output"
        VERIFICATIONS_FAILED=$((VERIFICATIONS_FAILED + 1))
    else
        # Extract last column (16384-byte block performance) and convert to GB/sec
        local PERF_GBPS
        PERF_GBPS=$(echo "$PERF_LINE" | awk '{
            val = $NF;
            sub(/k$/, "", val);
            printf "%.2f", val / 1024 / 1024
        }')

        echo "  AES-128-GCM Speed (16384 bytes): ${PERF_GBPS} GB/sec"

        # Threshold: x86=0.5 GB/sec, x64/a64=1.0 GB/sec
        # Rosetta emulation (x86_64 on ARM64 host) has lower throughput — use 0.5 GB/sec
        # Detection: check for VirtualApple in /proc/cpuinfo (Docker on Apple Silicon)
        # or rosetta binfmt_misc entry (Linux VMs on Apple Silicon)
        local THRESHOLD
        local IS_ROSETTA=0
        if [ "$(uname -m)" = "x86_64" ]; then
            if [ -f /proc/sys/fs/binfmt_misc/rosetta ] || grep -qi 'VirtualApple' /proc/cpuinfo 2>/dev/null; then
                IS_ROSETTA=1
            fi
        fi
        if [ "$ARCH_TAG_ARG" = "x86" ] || [ "$IS_ROSETTA" = "1" ]; then
            THRESHOLD="0.50"
        else
            THRESHOLD="1.00"
        fi

        local PASS
        PASS=$(awk "BEGIN { print (${PERF_GBPS} > ${THRESHOLD}) }")
        if [ "$PASS" = "1" ]; then
            echo "  [PASS] Performance exceeds ${THRESHOLD} GB/sec threshold"
            VERIFICATIONS_PASSED=$((VERIFICATIONS_PASSED + 1))
        else
            echo "  [FAIL] Performance ${PERF_GBPS} GB/sec is below ${THRESHOLD} GB/sec threshold"
            echo "  This indicates hardware acceleration is not working correctly"
            VERIFICATIONS_FAILED=$((VERIFICATIONS_FAILED + 1))
        fi
    fi

    # =========================================================================
    # Verification 2: Optimization Flags
    # =========================================================================
    echo ""
    echo "[Step 2/3] Checking optimization flags..."
    echo "  Running: openssl version -a"

    local VERSION_OUTPUT
    VERSION_OUTPUT=$("${OPENSSL_EXE}" version -a 2>&1)

    local COMPILER_LINE
    COMPILER_LINE=$(echo "$VERSION_OUTPUT" | grep -E '^compiler:' || true)

    if [ -z "$COMPILER_LINE" ]; then
        echo "  [FAIL] Could not find compiler line in version output"
        VERIFICATIONS_FAILED=$((VERIFICATIONS_FAILED + 1))
    else
        if [ "$BUILD_TYPE" = "debug" ]; then
            echo "  Expected flags (debug): -O0 and -g"
            # Debug variant: check for -O0 AND -g
            local HAS_O0 HAS_G
            HAS_O0=$(echo "$COMPILER_LINE" | grep -c '\-O0' || true)
            HAS_G=$(echo "$COMPILER_LINE" | grep -c '\-g ' || true)
            if [ "$HAS_O0" -gt 0 ] && [ "$HAS_G" -gt 0 ]; then
                echo "  [PASS] Optimization flags correct for debug variant"
                VERIFICATIONS_PASSED=$((VERIFICATIONS_PASSED + 1))
            else
                echo "  Compiler line: ${COMPILER_LINE}"
                echo "  [FAIL] Missing or incorrect debug optimization flags"
                VERIFICATIONS_FAILED=$((VERIFICATIONS_FAILED + 1))
            fi
        else
            echo "  Expected flags (release): -O3"
            # Release variant: check for -O3
            local HAS_O3
            HAS_O3=$(echo "$COMPILER_LINE" | grep -c '\-O3' || true)
            if [ "$HAS_O3" -gt 0 ]; then
                echo "  [PASS] Optimization flags correct for release variant"
                VERIFICATIONS_PASSED=$((VERIFICATIONS_PASSED + 1))
            else
                echo "  Compiler line: ${COMPILER_LINE}"
                echo "  [FAIL] Missing or incorrect release optimization flags"
                VERIFICATIONS_FAILED=$((VERIFICATIONS_FAILED + 1))
            fi
        fi
    fi

    # =========================================================================
    # Verification 3: Debug Information Flags
    # =========================================================================
    echo ""
    echo "[Step 3/3] Checking debug information flags..."

    if [ "$BUILD_TYPE" = "release" ]; then
        # Release does not pass -g to Configure, so this check is not applicable
        echo "  [SKIP] Not applicable for release variant (debug symbols not used)"
        VERIFICATIONS_PASSED=$((VERIFICATIONS_PASSED + 1))
    else
        echo "  Expected flags: -g or -ggdb"
        if [ -z "$COMPILER_LINE" ]; then
            echo "  [FAIL] Could not find compiler line in version output"
            VERIFICATIONS_FAILED=$((VERIFICATIONS_FAILED + 1))
        else
            # Check for -g or -ggdb in compiler line
            local HAS_DEBUG_FLAG
            HAS_DEBUG_FLAG=$(echo "$COMPILER_LINE" | grep -cE '(\-ggdb|\-g )' || true)
            if [ "$HAS_DEBUG_FLAG" -gt 0 ]; then
                echo "  [PASS] Debug flags present"
                VERIFICATIONS_PASSED=$((VERIFICATIONS_PASSED + 1))
            else
                echo "  Compiler line: ${COMPILER_LINE}"
                echo "  [FAIL] Missing debug information flags (-g or -ggdb)"
                VERIFICATIONS_FAILED=$((VERIFICATIONS_FAILED + 1))
            fi
        fi
    fi

    # =========================================================================
    # Summary
    # =========================================================================
    local TOTAL=$((VERIFICATIONS_PASSED + VERIFICATIONS_FAILED))
    echo ""
    echo "Total Verifications: ${TOTAL}, Passed: ${VERIFICATIONS_PASSED}, Failed: ${VERIFICATIONS_FAILED}"

    if [ "$VERIFICATIONS_FAILED" -gt 0 ]; then
        return 1
    fi
    return 0
}

# If --test-hw-acceleration-speed-only, run verification on existing builds and exit
if [ "${TEST_HW_ACCEL_ONLY}" = "1" ]; then
    echo "Running verification checks only (skipping build)..."
    echo

    # Set up toolchain environment so openssl can find its runtime libraries
    setup_toolchain_env

    OVERALL_RESULT=0
    for VARIANT in debug release; do
        VERIFY_BUILD_DIR="${VERSION_DIR}/${BUILD_TAG}-${VARIANT}"
        VERIFY_OPENSSL_EXE="${VERIFY_BUILD_DIR}/bin/openssl"
        if [ ! -f "${VERIFY_OPENSSL_EXE}" ]; then
            echo "ERROR: OpenSSL binary not found: ${VERIFY_OPENSSL_EXE}" >&2
            exit 1
        fi
        echo "==========================================================================="
        echo "Verifying ${VARIANT} variant: ${VERIFY_OPENSSL_EXE}"
        echo "==========================================================================="
        if ! verify_openssl_build "${VERIFY_OPENSSL_EXE}" "${VARIANT}" "${ARCH_TAG}"; then
            OVERALL_RESULT=1
        fi
        echo
    done

    if [ "${OVERALL_RESULT}" = "0" ]; then
        echo "==========================================================================="
        echo "All Verification Checks Passed"
        echo "==========================================================================="
    else
        echo "==========================================================================="
        echo "ERROR: Some verification checks failed"
        echo "==========================================================================="
    fi

    exit ${OVERALL_RESULT}
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
    setup_toolchain_env
    if [ "$TOOLCHAIN" = "gcc" ]; then
        export CC="${TOOLCHAIN_INSTALL_DIR}/bin/gcc"
        export CXX="${TOOLCHAIN_INSTALL_DIR}/bin/g++"
    else
        export CC="${TOOLCHAIN_INSTALL_DIR}/bin/clang"
        export CXX="${TOOLCHAIN_INSTALL_DIR}/bin/clang++"
        # Set Clang-specific flags for libc++, compiler-rt, libunwind, and lld
        # Note: Not including -lc++abi in LDFLAGS because libc++/libc++abi will be
        # linked statically at final binary link time with -Wl,-Bstatic
        # -fno-strict-float-cast-overflow: OpenSSL's crypto/params.c has undefined
        # behavior in OSSL_PARAM_set_double() — it casts negative doubles to uint64_t
        # and out-of-range doubles to int64_t (lines ~1265, ~1296). At -O3, Clang
        # exploits this UB, causing 04-test_params_conversion.t to fail. This flag
        # makes out-of-range float-to-int casts produce defined results instead of UB.
        export CFLAGS="-stdlib=libc++ -rtlib=compiler-rt --unwindlib=libunwind -fno-strict-float-cast-overflow"
        export CXXFLAGS="-stdlib=libc++ -rtlib=compiler-rt --unwindlib=libunwind -fno-strict-float-cast-overflow"
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
        --openssldir="${BUILD_DIR}/openssl" \
        --libdir=lib

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

    # Verify build (HW acceleration, optimization flags, debug info flags)
    echo ""
    echo "==========================================================================="
    echo "Verifying OpenSSL Build Configuration (${VARIANT})"
    echo "==========================================================================="
    verify_openssl_build "${BUILD_DIR}/bin/openssl" "${VARIANT}" "${ARCH_TAG}"
    echo ""
    echo "==========================================================================="
    echo "Verification Passed Successfully (${VARIANT})"
    echo "==========================================================================="

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
echo "==========================================================================="
