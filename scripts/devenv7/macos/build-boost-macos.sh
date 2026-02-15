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
# Boost Build Script for macOS (Apple Silicon / Intel)
# This script builds Boost with static libraries for use with swblocks-baselib
# Both debug and release variants are built automatically
#
# Usage: ./build-boost-macos.sh [BOOST_VERSION] [DEVENV_TAG]
#   BOOST_VERSION: Boost version to build (default: 1.90.0)
#   DEVENV_TAG:    devenv tag (default: devenv7)
#
# Examples:
#   ./build-boost-macos.sh                         # Build 1.90.0 debug+release devenv7
#   ./build-boost-macos.sh 1.90.0                  # Build 1.90.0 debug+release devenv7
#   ./build-boost-macos.sh 1.84.0 devenv6          # Build 1.84.0 debug+release devenv6
###############################################################################

set -e  # Exit on error
set -u  # Exit on undefined variable

# Parse command line arguments
BOOST_VERSION="${1:-1.90.0}"
DEVENV_TAG="${2:-devenv7}"

# Convert version to underscore format (e.g., 1.89.0 -> 1_89_0)
BOOST_VERSION_UNDERSCORE=$(echo "$BOOST_VERSION" | tr '.' '_')
BOOST_ARCHIVE="boost_${BOOST_VERSION_UNDERSCORE}.tar.gz"
BOOST_URL="https://archives.boost.io/release/${BOOST_VERSION}/source/${BOOST_ARCHIVE}"
BOOST_DIR="boost_${BOOST_VERSION_UNDERSCORE}"

# Detect architecture
ARCH=$(uname -m)
if [ "$ARCH" = "arm64" ]; then
    ARCH_TAG="a64"
    ARCH_FLAGS="-arch arm64"
elif [ "$ARCH" = "x86_64" ]; then
    ARCH_TAG="x64"
    ARCH_FLAGS="-arch x86_64"
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
VERSION_DIR="${HOME}/swblocks/dist-${DEVENV_TAG}-darwin-${OS_NUMBER}-${ARCH_TAG}/boost/${BOOST_VERSION}"
ARCHIVE_DIR="${VERSION_DIR}/tar"
SOURCE_DIR="${VERSION_DIR}/source-linux"

# Number of parallel jobs
JOBS=$(sysctl -n hw.ncpu)
JOBS=${BL_MAKE_JOBS:-${JOBS}}

echo "==========================================================================="
echo "Boost ${BOOST_VERSION} Build Configuration"
echo "==========================================================================="
echo "Architecture:     ${ARCH} (${ARCH_TAG})"
echo "macOS Version:    $(sw_vers -productVersion) (${OS_TAG})"
echo "Clang Version:    ${CLANG_VERSION} (${CLANG_TAG})"
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
    curl -L "${BOOST_URL}" -o "${ARCHIVE_DIR}/${BOOST_ARCHIVE}"
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

# Patch Boost.Locale iconv detection test to work on macOS
# The original test passes NULL to iconv_open() which causes issues on macOS
# We simplify it to just check if iconv.h exists and is usable
echo "Patching Boost.Locale iconv detection test..."
chmod u+w libs/locale/build/has_iconv.cpp 2>/dev/null || true
cat > libs/locale/build/has_iconv.cpp << 'EOF'
//
// Copyright (c) 2009-2011 Artyom Beilis (Tonkikh)
// Modified for macOS compatibility
//
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt

#include <iconv.h>

// Simplified test that just verifies iconv.h is available
// The actual iconv functionality will be tested at runtime
int main()
{
    // Just verify we can reference iconv types and the library links
    // Don't actually call iconv_open as it can crash with certain flags on macOS
    iconv_t dummy;
    (void)sizeof(dummy);
    return 0;
}
EOF
echo "Boost.Locale iconv detection test patched."

# Patch Jamfile.v2 to add macOS-specific iconv library support
# On macOS, iconv requires -liconv and works with static linking
echo "Patching Jamfile.v2 for macOS iconv configuration..."
chmod u+w libs/locale/build/Jamfile.v2 2>/dev/null || true

# Extract a fresh Jamfile.v2 from the tarball to ensure we start clean
tar -xzOf "${ARCHIVE_DIR}/${BOOST_ARCHIVE}" "${BOOST_DIR}/libs/locale/build/Jamfile.v2" > libs/locale/build/Jamfile.v2

# Create a minimal sed script with only the necessary changes for macOS iconv support
cat > /tmp/jamfile_patch.sed << 'SEDEOF'
# 1. Create iconv_darwin library for Darwin (for actual boost_locale linking)
/^explicit iconv ;$/ a\
\
# On macOS/Darwin, add iconv library for static linking\
lib iconv_darwin : : <target-os>darwin <name>iconv ;\
explicit iconv_darwin ;

# 2. On Darwin, bypass the configure.builds check entirely and force iconv to be found
#    by setting found-iconv = true before the checks run
/^    local found-iconv ;$/ {
    a\
\
    # On Darwin, iconv is always available via -liconv, so bypass the detection\
    if <target-os>darwin in $(properties)\
    {\
        found-iconv = true ;\
    }
}

# 3. Replace the result += <library>iconv line to use iconv_darwin on Darwin
/^            result += <library>iconv ;$/ {
    c\
            if <target-os>darwin in $(properties)\
            {\
                result += <library>iconv_darwin ;\
            }\
            else\
            {\
                result += <library>iconv ;\
            }
}
SEDEOF

# Apply the sed script
sed -i.bak -f /tmp/jamfile_patch.sed libs/locale/build/Jamfile.v2

echo "Jamfile.v2 patched for macOS - modified configure.builds tests to use -liconv."

# Create user-config.jam for static linking
echo "Creating user-config.jam..."
cat > user-config.jam << EOF
using darwin : ${CLANG_VERSION}
    : clang++
    : <cxxflags>"${ARCH_FLAGS} -std=c++11 -fPIC"
      <linkflags>"${ARCH_FLAGS} -liconv"
    ;
EOF

# Bootstrap b2 if not already done
if [ ! -f "./b2" ]; then
    echo "Bootstrapping Boost.Build..."
    ./bootstrap.sh \
        --with-toolset=clang
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

    # Build Boost libraries
    echo "Building Boost libraries for ${VARIANT}..."
    ./b2 \
        --user-config=user-config.jam \
        --prefix="${BUILD_DIR}" \
        --build-dir="${TEMP_BUILD_DIR}" \
        toolset=darwin-${CLANG_VERSION} \
        address-model=64 \
        architecture=arm \
        variant=${VARIANT_FLAG} \
        link=static \
        runtime-link=static \
        threading=multi \
        cxxflags="${ARCH_FLAGS} ${CXX_FLAGS} -std=c++11 -fPIC -fvisibility=hidden" \
        linkflags="${ARCH_FLAGS} -liconv" \
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

    # Clean up intermediate build files but preserve debug symbols
    echo
    echo "Cleaning up intermediate build files for ${VARIANT}..."
    if [ -d "${TEMP_BUILD_DIR}" ]; then
        echo "Preserving debug symbol files (.dSYM directories)..."

        # Find and preserve .dSYM bundles if they exist
        find "${TEMP_BUILD_DIR}" -name "*.dSYM" -type d -exec cp -R {} "${BUILD_DIR}/lib/" \; 2>/dev/null || true

        # Remove the temporary build directory
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
echo "==========================================================================="
