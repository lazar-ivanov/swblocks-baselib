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
    echo "Usage: $0 TOOLCHAIN [GCC_VERSION] [CLANG_VERSION]"
    echo "  TOOLCHAIN:     Toolchain to use: gcc, clang, or gcc-clang (required)"
    echo "  GCC_VERSION:   GCC version to build (default: 15.2.0, used for gcc or gcc-clang)"
    echo "  CLANG_VERSION: Clang version to build (default: 20.1.0, used for clang or gcc-clang)"
    echo
    echo "Examples:"
    echo "  $0 clang               # Build with Clang 20.1.0"
    echo "  $0 gcc                 # Build with GCC 15.2.0"
    echo "  $0 gcc-clang           # Build with both GCC 15.2.0 and Clang 20.1.0"
    echo "  $0 clang 20.1.0        # Build with Clang 20.1.0"
    echo "  $0 gcc 15.2.0          # Build with GCC 15.2.0"
    echo "  $0 gcc-clang 15.2.0    # Build both with GCC 15.2.0 and Clang 20.1.0"
    echo "  $0 gcc-clang 15.2.0 20.1.0  # Build both with GCC 15.2.0 and Clang 20.1.0"
    echo
    exit 1
fi

# Parse toolchain argument (required)
TOOLCHAIN="$1"
if [ "$TOOLCHAIN" != "gcc" ] && [ "$TOOLCHAIN" != "clang" ] && [ "$TOOLCHAIN" != "gcc-clang" ]; then
    echo "ERROR: Invalid toolchain '$TOOLCHAIN'. Must be 'gcc', 'clang', or 'gcc-clang'"
    echo
    echo "Usage: $0 TOOLCHAIN [GCC_VERSION] [CLANG_VERSION]"
    echo "  TOOLCHAIN:     Toolchain to use: gcc, clang, or gcc-clang (required)"
    echo "  GCC_VERSION:   GCC version to build (default: 15.2.0, used for gcc or gcc-clang)"
    echo "  CLANG_VERSION: Clang version to build (default: 20.1.0, used for clang or gcc-clang)"
    echo
    echo "Examples:"
    echo "  $0 clang               # Build with Clang 20.1.0"
    echo "  $0 gcc                 # Build with GCC 15.2.0"
    echo "  $0 gcc-clang           # Build with both GCC 15.2.0 and Clang 20.1.0"
    echo "  $0 clang 20.1.0        # Build with Clang 20.1.0"
    echo "  $0 gcc 15.2.0          # Build with GCC 15.2.0"
    echo "  $0 gcc-clang 15.2.0    # Build both with GCC 15.2.0 and Clang 20.1.0"
    echo "  $0 gcc-clang 15.2.0 20.1.0  # Build both with GCC 15.2.0 and Clang 20.1.0"
    echo
    exit 1
fi

# Handle version parameters based on toolchain mode
if [ "$TOOLCHAIN" = "gcc-clang" ]; then
    # Both toolchains will be built
    GCC_VERSION="${2:-15.2.0}"
    CLANG_VERSION="${3:-20.1.0}"

    # Generate toolchain tags for both compilers
    GCC_VERSION_NO_DOTS=$(echo "$GCC_VERSION" | tr -d '.')
    GCC_TAG="gcc${GCC_VERSION_NO_DOTS}"

    CLANG_VERSION_NO_DOTS=$(echo "$CLANG_VERSION" | tr -d '.')
    CLANG_TAG="clang${CLANG_VERSION_NO_DOTS}"

    # Create concatenated dist tag (GCC first, then Clang)
    DIST_TAG="${GCC_TAG}-${CLANG_TAG}"
elif [ "$TOOLCHAIN" = "gcc" ]; then
    # Only GCC will be built
    GCC_VERSION="${2:-15.2.0}"
    GCC_VERSION_NO_DOTS=$(echo "$GCC_VERSION" | tr -d '.')
    GCC_TAG="gcc${GCC_VERSION_NO_DOTS}"
    DIST_TAG="${GCC_TAG}"
else
    # Only Clang will be built
    CLANG_VERSION="${2:-20.1.0}"
    CLANG_VERSION_NO_DOTS=$(echo "$CLANG_VERSION" | tr -d '.')
    CLANG_TAG="clang${CLANG_VERSION_NO_DOTS}"
    DIST_TAG="${CLANG_TAG}"
fi

# Default versions for other components
DEVENV_TAG="devenv7"
BOOST_VERSION="1.90.0"
OPENSSL_VERSION="3.5.4"
JDK_VERSION="25"
GRADLE_VERSION="9.2.1"

# Detect architecture
ARCH=$(uname -m)
if [ "$ARCH" = "aarch64" ] || [ "$ARCH" = "arm64" ]; then
    ARCH_TAG="a64"
elif [ "$ARCH" = "x86_64" ]; then
    ARCH_TAG="x64"
elif [ "$ARCH" = "i386" ] || [ "$ARCH" = "i486" ] || [ "$ARCH" = "i586" ] || [ "$ARCH" = "i686" ]; then
    ARCH_TAG="x86"
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
echo "Toolchain Mode:    ${TOOLCHAIN}"
if [ "$TOOLCHAIN" = "gcc-clang" ]; then
    echo "GCC Version:       ${GCC_VERSION} (${GCC_TAG})"
    echo "Clang Version:     ${CLANG_VERSION} (${CLANG_TAG})"
    echo "Dist Tag:          ${DIST_TAG}"
elif [ "$TOOLCHAIN" = "gcc" ]; then
    echo "GCC Version:       ${GCC_VERSION} (${GCC_TAG})"
    echo "Dist Tag:          ${DIST_TAG}"
else
    echo "Clang Version:     ${CLANG_VERSION} (${CLANG_TAG})"
    echo "Dist Tag:          ${DIST_TAG}"
fi
echo "Boost Version:     ${BOOST_VERSION}"
echo "OpenSSL Version:   ${OPENSSL_VERSION}"
echo "OpenJDK Version:   ${JDK_VERSION}"
echo "Gradle Version:    ${GRADLE_VERSION}"
echo "==========================================================================="
echo
echo "Build order:"
if [ "$TOOLCHAIN" = "gcc-clang" ]; then
    echo "  1. GCC toolchain"
    echo "  2. Clang/LLVM toolchain"
    echo "  3. Boost libraries (both toolchains)"
    echo "  4. OpenSSL libraries (both toolchains)"
elif [ "$TOOLCHAIN" = "gcc" ]; then
    echo "  1. GCC toolchain"
    echo "  2. Boost libraries"
    echo "  3. OpenSSL libraries"
else
    echo "  1. Clang/LLVM toolchain"
    echo "  2. Boost libraries"
    echo "  3. OpenSSL libraries"
fi
echo "  $([ "$TOOLCHAIN" = "gcc-clang" ] && echo "5" || echo "4"). JSON Spirit headers"
echo "  $([ "$TOOLCHAIN" = "gcc-clang" ] && echo "6" || echo "5"). OpenJDK"
echo "  $([ "$TOOLCHAIN" = "gcc-clang" ] && echo "7" || echo "6"). Gradle"
echo "==========================================================================="
echo

# Trap errors and display which step failed
trap 'echo ""; echo "ERROR: Build failed at step: ${CURRENT_STEP}"; exit 1' ERR

# Build toolchains based on mode
if [ "$TOOLCHAIN" = "gcc-clang" ]; then
    # Step 1: Build GCC toolchain
    CURRENT_STEP="Building GCC ${GCC_VERSION}"
    echo
    echo "==========================================================================="
    echo "Step 1/7: ${CURRENT_STEP}"
    echo "==========================================================================="
    echo
    "${SCRIPT_DIR}/build-gcc-linux.sh" "${GCC_VERSION}" "${DEVENV_TAG}" "${DIST_TAG}"
    echo
    echo "✓ GCC ${GCC_VERSION} build complete"

    # Step 2: Build Clang/LLVM toolchain
    CURRENT_STEP="Building Clang/LLVM ${CLANG_VERSION}"
    echo
    echo "==========================================================================="
    echo "Step 2/7: ${CURRENT_STEP}"
    echo "==========================================================================="
    echo
    "${SCRIPT_DIR}/build-clang-linux.sh" "${CLANG_VERSION}" "${DEVENV_TAG}" "${DIST_TAG}"
    echo
    echo "✓ Clang/LLVM ${CLANG_VERSION} build complete"

    # Step 3: Build Boost for GCC
    CURRENT_STEP="Building Boost ${BOOST_VERSION} for GCC"
    echo
    echo "==========================================================================="
    echo "Step 3/7: ${CURRENT_STEP}"
    echo "==========================================================================="
    echo
    "${SCRIPT_DIR}/build-boost-linux.sh" "gcc" "${BOOST_VERSION}" "${DEVENV_TAG}" "${GCC_VERSION}" "${DIST_TAG}"
    echo
    echo "✓ Boost ${BOOST_VERSION} for GCC build complete"

    # Build Boost for Clang
    CURRENT_STEP="Building Boost ${BOOST_VERSION} for Clang"
    echo
    echo "==========================================================================="
    echo "Step 3/7 (continued): ${CURRENT_STEP}"
    echo "==========================================================================="
    echo
    "${SCRIPT_DIR}/build-boost-linux.sh" "clang" "${BOOST_VERSION}" "${DEVENV_TAG}" "${CLANG_VERSION}" "${DIST_TAG}"
    echo
    echo "✓ Boost ${BOOST_VERSION} for Clang build complete"

    # Step 4: Build OpenSSL for GCC
    CURRENT_STEP="Building OpenSSL ${OPENSSL_VERSION} for GCC"
    echo
    echo "==========================================================================="
    echo "Step 4/7: ${CURRENT_STEP}"
    echo "==========================================================================="
    echo
    "${SCRIPT_DIR}/build-openssl-linux.sh" "gcc" "${OPENSSL_VERSION}" "${DEVENV_TAG}" "${GCC_VERSION}" "${DIST_TAG}"
    echo
    echo "✓ OpenSSL ${OPENSSL_VERSION} for GCC build complete"

    # Build OpenSSL for Clang
    CURRENT_STEP="Building OpenSSL ${OPENSSL_VERSION} for Clang"
    echo
    echo "==========================================================================="
    echo "Step 4/7 (continued): ${CURRENT_STEP}"
    echo "==========================================================================="
    echo
    "${SCRIPT_DIR}/build-openssl-linux.sh" "clang" "${OPENSSL_VERSION}" "${DEVENV_TAG}" "${CLANG_VERSION}" "${DIST_TAG}"
    echo
    echo "✓ OpenSSL ${OPENSSL_VERSION} for Clang build complete"

    # Step 5: Install JSON Spirit
    CURRENT_STEP="Installing JSON Spirit"
    echo
    echo "==========================================================================="
    echo "Step 5/7: ${CURRENT_STEP}"
    echo "==========================================================================="
    echo
    "${SCRIPT_DIR}/install-json-spirit-linux.sh" "${DIST_TAG}" "${DEVENV_TAG}"
    echo
    echo "✓ JSON Spirit installation complete"

    # Step 6: Install OpenJDK
    CURRENT_STEP="Installing OpenJDK ${JDK_VERSION}"
    echo
    echo "==========================================================================="
    echo "Step 6/7: ${CURRENT_STEP}"
    echo "==========================================================================="
    echo
    "${SCRIPT_DIR}/install-openjdk-linux.sh" "${DIST_TAG}" "${JDK_VERSION}" "${DEVENV_TAG}"
    echo
    echo "✓ OpenJDK ${JDK_VERSION} installation complete"

    # Step 7: Install Gradle
    CURRENT_STEP="Installing Gradle ${GRADLE_VERSION}"
    echo
    echo "==========================================================================="
    echo "Step 7/7: ${CURRENT_STEP}"
    echo "==========================================================================="
    echo
    "${SCRIPT_DIR}/install-gradle-linux.sh" "${DIST_TAG}" "${GRADLE_VERSION}" "${DEVENV_TAG}"
    echo
    echo "✓ Gradle ${GRADLE_VERSION} installation complete"

elif [ "$TOOLCHAIN" = "gcc" ]; then
    # Step 1: Build GCC toolchain
    CURRENT_STEP="Building GCC ${GCC_VERSION}"
    echo
    echo "==========================================================================="
    echo "Step 1/6: ${CURRENT_STEP}"
    echo "==========================================================================="
    echo
    "${SCRIPT_DIR}/build-gcc-linux.sh" "${GCC_VERSION}" "${DEVENV_TAG}" "${DIST_TAG}"
    echo
    echo "✓ GCC ${GCC_VERSION} build complete"

    # Step 2: Build Boost
    CURRENT_STEP="Building Boost ${BOOST_VERSION}"
    echo
    echo "==========================================================================="
    echo "Step 2/6: ${CURRENT_STEP}"
    echo "==========================================================================="
    echo
    "${SCRIPT_DIR}/build-boost-linux.sh" "gcc" "${BOOST_VERSION}" "${DEVENV_TAG}" "${GCC_VERSION}" "${DIST_TAG}"
    echo
    echo "✓ Boost ${BOOST_VERSION} build complete"

    # Step 3: Build OpenSSL
    CURRENT_STEP="Building OpenSSL ${OPENSSL_VERSION}"
    echo
    echo "==========================================================================="
    echo "Step 3/6: ${CURRENT_STEP}"
    echo "==========================================================================="
    echo
    "${SCRIPT_DIR}/build-openssl-linux.sh" "gcc" "${OPENSSL_VERSION}" "${DEVENV_TAG}" "${GCC_VERSION}" "${DIST_TAG}"
    echo
    echo "✓ OpenSSL ${OPENSSL_VERSION} build complete"

    # Step 4: Install JSON Spirit
    CURRENT_STEP="Installing JSON Spirit"
    echo
    echo "==========================================================================="
    echo "Step 4/6: ${CURRENT_STEP}"
    echo "==========================================================================="
    echo
    "${SCRIPT_DIR}/install-json-spirit-linux.sh" "${DIST_TAG}" "${DEVENV_TAG}"
    echo
    echo "✓ JSON Spirit installation complete"

    # Step 5: Install OpenJDK
    CURRENT_STEP="Installing OpenJDK ${JDK_VERSION}"
    echo
    echo "==========================================================================="
    echo "Step 5/6: ${CURRENT_STEP}"
    echo "==========================================================================="
    echo
    "${SCRIPT_DIR}/install-openjdk-linux.sh" "${DIST_TAG}" "${JDK_VERSION}" "${DEVENV_TAG}"
    echo
    echo "✓ OpenJDK ${JDK_VERSION} installation complete"

    # Step 6: Install Gradle
    CURRENT_STEP="Installing Gradle ${GRADLE_VERSION}"
    echo
    echo "==========================================================================="
    echo "Step 6/6: ${CURRENT_STEP}"
    echo "==========================================================================="
    echo
    "${SCRIPT_DIR}/install-gradle-linux.sh" "${DIST_TAG}" "${GRADLE_VERSION}" "${DEVENV_TAG}"
    echo
    echo "✓ Gradle ${GRADLE_VERSION} installation complete"

else
    # Step 1: Build Clang/LLVM toolchain
    CURRENT_STEP="Building Clang/LLVM ${CLANG_VERSION}"
    echo
    echo "==========================================================================="
    echo "Step 1/6: ${CURRENT_STEP}"
    echo "==========================================================================="
    echo
    "${SCRIPT_DIR}/build-clang-linux.sh" "${CLANG_VERSION}" "${DEVENV_TAG}" "${DIST_TAG}"
    echo
    echo "✓ Clang/LLVM ${CLANG_VERSION} build complete"

    # Step 2: Build Boost
    CURRENT_STEP="Building Boost ${BOOST_VERSION}"
    echo
    echo "==========================================================================="
    echo "Step 2/6: ${CURRENT_STEP}"
    echo "==========================================================================="
    echo
    "${SCRIPT_DIR}/build-boost-linux.sh" "clang" "${BOOST_VERSION}" "${DEVENV_TAG}" "${CLANG_VERSION}" "${DIST_TAG}"
    echo
    echo "✓ Boost ${BOOST_VERSION} build complete"

    # Step 3: Build OpenSSL
    CURRENT_STEP="Building OpenSSL ${OPENSSL_VERSION}"
    echo
    echo "==========================================================================="
    echo "Step 3/6: ${CURRENT_STEP}"
    echo "==========================================================================="
    echo
    "${SCRIPT_DIR}/build-openssl-linux.sh" "clang" "${OPENSSL_VERSION}" "${DEVENV_TAG}" "${CLANG_VERSION}" "${DIST_TAG}"
    echo
    echo "✓ OpenSSL ${OPENSSL_VERSION} build complete"

    # Step 4: Install JSON Spirit
    CURRENT_STEP="Installing JSON Spirit"
    echo
    echo "==========================================================================="
    echo "Step 4/6: ${CURRENT_STEP}"
    echo "==========================================================================="
    echo
    "${SCRIPT_DIR}/install-json-spirit-linux.sh" "${DIST_TAG}" "${DEVENV_TAG}"
    echo
    echo "✓ JSON Spirit installation complete"

    # Step 5: Install OpenJDK
    CURRENT_STEP="Installing OpenJDK ${JDK_VERSION}"
    echo
    echo "==========================================================================="
    echo "Step 5/6: ${CURRENT_STEP}"
    echo "==========================================================================="
    echo
    "${SCRIPT_DIR}/install-openjdk-linux.sh" "${DIST_TAG}" "${JDK_VERSION}" "${DEVENV_TAG}"
    echo
    echo "✓ OpenJDK ${JDK_VERSION} installation complete"

    # Step 6: Install Gradle
    CURRENT_STEP="Installing Gradle ${GRADLE_VERSION}"
    echo
    echo "==========================================================================="
    echo "Step 6/6: ${CURRENT_STEP}"
    echo "==========================================================================="
    echo
    "${SCRIPT_DIR}/install-gradle-linux.sh" "${DIST_TAG}" "${GRADLE_VERSION}" "${DEVENV_TAG}"
    echo
    echo "✓ Gradle ${GRADLE_VERSION} installation complete"
fi

# Final summary
echo
echo "==========================================================================="
echo "Complete DevEnv Build SUCCESS!"
echo "==========================================================================="
echo
echo "All components have been successfully built and installed:"
echo
if [ "$TOOLCHAIN" = "gcc-clang" ]; then
    echo "  ✓ GCC ${GCC_VERSION}"
    echo "  ✓ Clang/LLVM ${CLANG_VERSION}"
elif [ "$TOOLCHAIN" = "gcc" ]; then
    echo "  ✓ GCC ${GCC_VERSION}"
else
    echo "  ✓ Clang/LLVM ${CLANG_VERSION}"
fi
echo "  ✓ Boost ${BOOST_VERSION}"
echo "  ✓ OpenSSL ${OPENSSL_VERSION}"
echo "  ✓ JSON Spirit"
echo "  ✓ OpenJDK ${JDK_VERSION}"
echo "  ✓ Gradle ${GRADLE_VERSION}"
echo
echo "Installation directory:"
echo "  ${HOME}/swblocks/dist-${DEVENV_TAG}-${OS_TAG}-${DIST_TAG}-${ARCH_TAG}"
echo
if [ "$TOOLCHAIN" = "gcc-clang" ]; then
    echo "To use this environment with GCC, set the following environment variables:"
    echo "  export PATH=\"${HOME}/swblocks/dist-${DEVENV_TAG}-${OS_TAG}-${DIST_TAG}-${ARCH_TAG}/toolchain-gcc/${GCC_VERSION}/${OS_TAG}-${ARCH_TAG}-${GCC_TAG}-release/bin:\$PATH\""
    echo "  export LD_LIBRARY_PATH=\"${HOME}/swblocks/dist-${DEVENV_TAG}-${OS_TAG}-${DIST_TAG}-${ARCH_TAG}/toolchain-gcc/${GCC_VERSION}/${OS_TAG}-${ARCH_TAG}-${GCC_TAG}-release/lib64:\$LD_LIBRARY_PATH\""
    echo
    echo "To use this environment with Clang, set the following environment variables:"
    echo "  export PATH=\"${HOME}/swblocks/dist-${DEVENV_TAG}-${OS_TAG}-${DIST_TAG}-${ARCH_TAG}/toolchain-clang/${CLANG_VERSION}/${OS_TAG}-${ARCH_TAG}-${CLANG_TAG}-release/bin:\$PATH\""
    echo "  export LD_LIBRARY_PATH=\"${HOME}/swblocks/dist-${DEVENV_TAG}-${OS_TAG}-${DIST_TAG}-${ARCH_TAG}/toolchain-clang/${CLANG_VERSION}/${OS_TAG}-${ARCH_TAG}-${CLANG_TAG}-release/lib:\$LD_LIBRARY_PATH\""
elif [ "$TOOLCHAIN" = "gcc" ]; then
    echo "To use this environment, set the following environment variables:"
    echo "  export PATH=\"${HOME}/swblocks/dist-${DEVENV_TAG}-${OS_TAG}-${DIST_TAG}-${ARCH_TAG}/toolchain-gcc/${GCC_VERSION}/${OS_TAG}-${ARCH_TAG}-${GCC_TAG}-release/bin:\$PATH\""
    echo "  export LD_LIBRARY_PATH=\"${HOME}/swblocks/dist-${DEVENV_TAG}-${OS_TAG}-${DIST_TAG}-${ARCH_TAG}/toolchain-gcc/${GCC_VERSION}/${OS_TAG}-${ARCH_TAG}-${GCC_TAG}-release/lib64:\$LD_LIBRARY_PATH\""
else
    echo "To use this environment, set the following environment variables:"
    echo "  export PATH=\"${HOME}/swblocks/dist-${DEVENV_TAG}-${OS_TAG}-${DIST_TAG}-${ARCH_TAG}/toolchain-clang/${CLANG_VERSION}/${OS_TAG}-${ARCH_TAG}-${CLANG_TAG}-release/bin:\$PATH\""
    echo "  export LD_LIBRARY_PATH=\"${HOME}/swblocks/dist-${DEVENV_TAG}-${OS_TAG}-${DIST_TAG}-${ARCH_TAG}/toolchain-clang/${CLANG_VERSION}/${OS_TAG}-${ARCH_TAG}-${CLANG_TAG}-release/lib:\$LD_LIBRARY_PATH\""
fi
echo
echo "==========================================================================="
