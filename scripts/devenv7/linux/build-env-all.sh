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
# Complete DevEnv Build Script for Linux
# This script orchestrates the complete build of all components for devenv7
# It builds GCC or Clang, Boost, OpenSSL, and installs all dependencies in the correct order
#
# Usage: ./build-env-all.sh TOOLCHAIN [COMPILER_VERSION]
#   TOOLCHAIN:        Toolchain to use: gcc or clang (required)
#   COMPILER_VERSION: Compiler version to build (default: 20.1.0 for clang, 15.2.0 for gcc)
#
# Examples:
#   ./build-env-all.sh clang           # Build complete environment with Clang 20.1.0
#   ./build-env-all.sh gcc             # Build complete environment with GCC 15.2.0
#   ./build-env-all.sh clang 20.1.0    # Build complete environment with Clang 20.1.0
#   ./build-env-all.sh gcc 15.2.0      # Build complete environment with GCC 15.2.0
#
# Build order:
#   1. build-gcc-linux.sh or build-clang-linux.sh  - Build compiler toolchain
#   2. build-boost-linux.sh                        - Build Boost with the new compiler
#   3. build-openssl-linux.sh                      - Build OpenSSL with the new compiler
#   4. install-json-spirit-linux.sh                - Install JSON Spirit headers
#   5. install-openjdk-linux.sh                    - Install OpenJDK
#   6. install-gradle-linux.sh                     - Install Gradle
###############################################################################

set -e  # Exit on error
set -u  # Exit on undefined variable

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Check if toolchain argument is provided
if [ $# -lt 1 ]; then
    echo "ERROR: Toolchain parameter is required"
    echo
    echo "Usage: $0 TOOLCHAIN [COMPILER_VERSION]"
    echo "  TOOLCHAIN:        Toolchain to use: gcc or clang (required)"
    echo "  COMPILER_VERSION: Compiler version to build (default: 20.1.0 for clang, 15.2.0 for gcc)"
    echo
    echo "Examples:"
    echo "  $0 clang           # Build with Clang 20.1.0"
    echo "  $0 gcc             # Build with GCC 15.2.0"
    echo "  $0 clang 20.1.0    # Build with Clang 20.1.0"
    echo "  $0 gcc 15.2.0      # Build with GCC 15.2.0"
    echo
    exit 1
fi

# Parse toolchain argument (required)
TOOLCHAIN="$1"
if [ "$TOOLCHAIN" != "gcc" ] && [ "$TOOLCHAIN" != "clang" ]; then
    echo "ERROR: Invalid toolchain '$TOOLCHAIN'. Must be 'gcc' or 'clang'"
    echo
    echo "Usage: $0 TOOLCHAIN [COMPILER_VERSION]"
    echo "  TOOLCHAIN:        Toolchain to use: gcc or clang (required)"
    echo "  COMPILER_VERSION: Compiler version to build (default: 20.1.0 for clang, 15.2.0 for gcc)"
    echo
    echo "Examples:"
    echo "  $0 clang           # Build with Clang 20.1.0"
    echo "  $0 gcc             # Build with GCC 15.2.0"
    echo "  $0 clang 20.1.0    # Build with Clang 20.1.0"
    echo "  $0 gcc 15.2.0      # Build with GCC 15.2.0"
    echo
    exit 1
fi

# Set default compiler version based on toolchain
if [ "$TOOLCHAIN" = "gcc" ]; then
    COMPILER_VERSION="${2:-15.2.0}"
else
    COMPILER_VERSION="${2:-20.1.0}"
fi

# Extract compiler version for toolchain tag (e.g., 15.2.0 -> gcc1520, 20.1.0 -> clang2010)
# Remove all dots from version string
COMPILER_VERSION_NO_DOTS=$(echo "$COMPILER_VERSION" | tr -d '.')
COMPILER_TAG="${TOOLCHAIN}${COMPILER_VERSION_NO_DOTS}"

# Default versions for other components
DEVENV_TAG="devenv7"
BOOST_VERSION="1.89.0"
OPENSSL_VERSION="3.5.4"
JDK_VERSION="25"
GRADLE_VERSION="9.2.1"

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

# Display configuration
echo "==========================================================================="
echo "Complete DevEnv Build Configuration"
echo "==========================================================================="
echo "Architecture:      ${ARCH} (${ARCH_TAG})"
echo "OS Version:        $(lsb_release -ds 2>/dev/null || echo "Unknown") (${OS_TAG})"
echo "DevEnv Tag:        ${DEVENV_TAG}"
echo "Toolchain:         ${TOOLCHAIN}"
echo "Compiler Version:  ${COMPILER_VERSION} (${COMPILER_TAG})"
echo "Boost Version:     ${BOOST_VERSION}"
echo "OpenSSL Version:   ${OPENSSL_VERSION}"
echo "OpenJDK Version:   ${JDK_VERSION}"
echo "Gradle Version:    ${GRADLE_VERSION}"
echo "==========================================================================="
echo
echo "Build order:"
if [ "$TOOLCHAIN" = "gcc" ]; then
    echo "  1. GCC toolchain"
else
    echo "  1. Clang/LLVM toolchain"
fi
echo "  2. Boost libraries"
echo "  3. OpenSSL libraries"
echo "  4. JSON Spirit headers"
echo "  5. OpenJDK"
echo "  6. Gradle"
echo "==========================================================================="
echo

# Trap errors and display which step failed
trap 'echo ""; echo "ERROR: Build failed at step: ${CURRENT_STEP}"; exit 1' ERR

# Step 1: Build compiler toolchain
if [ "$TOOLCHAIN" = "gcc" ]; then
    CURRENT_STEP="Building GCC ${COMPILER_VERSION}"
    echo
    echo "==========================================================================="
    echo "Step 1/6: ${CURRENT_STEP}"
    echo "==========================================================================="
    echo
    "${SCRIPT_DIR}/build-gcc-linux.sh" "${COMPILER_VERSION}" "${DEVENV_TAG}"
    echo
    echo "✓ GCC ${COMPILER_VERSION} build complete"
else
    CURRENT_STEP="Building Clang/LLVM ${COMPILER_VERSION}"
    echo
    echo "==========================================================================="
    echo "Step 1/6: ${CURRENT_STEP}"
    echo "==========================================================================="
    echo
    "${SCRIPT_DIR}/build-clang-linux.sh" "${COMPILER_VERSION}" "${DEVENV_TAG}"
    echo
    echo "✓ Clang/LLVM ${COMPILER_VERSION} build complete"
fi

# Step 2: Build Boost
CURRENT_STEP="Building Boost ${BOOST_VERSION}"
echo
echo "==========================================================================="
echo "Step 2/6: ${CURRENT_STEP}"
echo "==========================================================================="
echo
"${SCRIPT_DIR}/build-boost-linux.sh" "${TOOLCHAIN}" "${BOOST_VERSION}" "${DEVENV_TAG}" "${COMPILER_VERSION}"
echo
echo "✓ Boost ${BOOST_VERSION} build complete"

# Step 3: Build OpenSSL
CURRENT_STEP="Building OpenSSL ${OPENSSL_VERSION}"
echo
echo "==========================================================================="
echo "Step 3/6: ${CURRENT_STEP}"
echo "==========================================================================="
echo
"${SCRIPT_DIR}/build-openssl-linux.sh" "${TOOLCHAIN}" "${OPENSSL_VERSION}" "${DEVENV_TAG}" "${COMPILER_VERSION}"
echo
echo "✓ OpenSSL ${OPENSSL_VERSION} build complete"

# Step 4: Install JSON Spirit
CURRENT_STEP="Installing JSON Spirit"
echo
echo "==========================================================================="
echo "Step 4/6: ${CURRENT_STEP}"
echo "==========================================================================="
echo
"${SCRIPT_DIR}/install-json-spirit-linux.sh" "${COMPILER_TAG}" "${DEVENV_TAG}"
echo
echo "✓ JSON Spirit installation complete"

# Step 5: Install OpenJDK
CURRENT_STEP="Installing OpenJDK ${JDK_VERSION}"
echo
echo "==========================================================================="
echo "Step 5/6: ${CURRENT_STEP}"
echo "==========================================================================="
echo
"${SCRIPT_DIR}/install-openjdk-linux.sh" "${COMPILER_TAG}" "${JDK_VERSION}" "${DEVENV_TAG}"
echo
echo "✓ OpenJDK ${JDK_VERSION} installation complete"

# Step 6: Install Gradle
CURRENT_STEP="Installing Gradle ${GRADLE_VERSION}"
echo
echo "==========================================================================="
echo "Step 6/6: ${CURRENT_STEP}"
echo "==========================================================================="
echo
"${SCRIPT_DIR}/install-gradle-linux.sh" "${COMPILER_TAG}" "${GRADLE_VERSION}" "${DEVENV_TAG}"
echo
echo "✓ Gradle ${GRADLE_VERSION} installation complete"

# Final summary
echo
echo "==========================================================================="
echo "Complete DevEnv Build SUCCESS!"
echo "==========================================================================="
echo
echo "All components have been successfully built and installed:"
echo
if [ "$TOOLCHAIN" = "gcc" ]; then
    echo "  ✓ GCC ${COMPILER_VERSION}"
else
    echo "  ✓ Clang/LLVM ${COMPILER_VERSION}"
fi
echo "  ✓ Boost ${BOOST_VERSION}"
echo "  ✓ OpenSSL ${OPENSSL_VERSION}"
echo "  ✓ JSON Spirit"
echo "  ✓ OpenJDK ${JDK_VERSION}"
echo "  ✓ Gradle ${GRADLE_VERSION}"
echo
echo "Installation directory:"
echo "  ${HOME}/swblocks/dist-${DEVENV_TAG}-${OS_TAG}-${COMPILER_TAG}-arm"
echo
echo "To use this environment, set the following environment variables:"
if [ "$TOOLCHAIN" = "gcc" ]; then
    echo "  export PATH=\"${HOME}/swblocks/dist-${DEVENV_TAG}-${OS_TAG}-${COMPILER_TAG}-arm/toolchain-gcc/${COMPILER_VERSION}/${OS_TAG}-${ARCH_TAG}-${COMPILER_TAG}-release/bin:\$PATH\""
    echo "  export LD_LIBRARY_PATH=\"${HOME}/swblocks/dist-${DEVENV_TAG}-${OS_TAG}-${COMPILER_TAG}-arm/toolchain-gcc/${COMPILER_VERSION}/${OS_TAG}-${ARCH_TAG}-${COMPILER_TAG}-release/lib64:\$LD_LIBRARY_PATH\""
else
    echo "  export PATH=\"${HOME}/swblocks/dist-${DEVENV_TAG}-${OS_TAG}-${COMPILER_TAG}-arm/toolchain-clang/${COMPILER_VERSION}/${OS_TAG}-${ARCH_TAG}-${COMPILER_TAG}-release/bin:\$PATH\""
    echo "  export LD_LIBRARY_PATH=\"${HOME}/swblocks/dist-${DEVENV_TAG}-${OS_TAG}-${COMPILER_TAG}-arm/toolchain-clang/${COMPILER_VERSION}/${OS_TAG}-${ARCH_TAG}-${COMPILER_TAG}-release/lib:\$LD_LIBRARY_PATH\""
fi
echo
echo "==========================================================================="
