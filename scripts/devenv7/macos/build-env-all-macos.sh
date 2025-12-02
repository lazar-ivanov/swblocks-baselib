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
# Complete DevEnv Build Script for macOS
# This script orchestrates the complete build of all components for devenv7
# It builds Boost, OpenSSL, and installs all dependencies in the correct order
#
# Usage: ./build-env-all-macos.sh [DEVENV_TAG]
#   DEVENV_TAG: devenv tag (default: devenv7)
#
# Examples:
#   ./build-env-all-macos.sh           # Build complete environment for devenv7
#   ./build-env-all-macos.sh devenv7   # Build complete environment for devenv7
#   ./build-env-all-macos.sh devenv6   # Build complete environment for devenv6
#
# Build order:
#   1. build-boost-macos.sh        - Build Boost with system clang
#   2. build-openssl-macos.sh      - Build OpenSSL with system clang
#   3. install-json-spirit-macos.sh - Install JSON Spirit headers
#   4. install-openjdk-macos.sh    - Install OpenJDK
#   5. install-gradle-macos.sh     - Install Gradle
###############################################################################

set -e  # Exit on error
set -u  # Exit on undefined variable

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Parse command line arguments
DEVENV_TAG="${1:-devenv7}"

# Default versions for components
BOOST_VERSION="1.89.0"
OPENSSL_VERSION="3.5.4"
JDK_VERSION="25"
GRADLE_VERSION="9.2.1"

# Detect architecture
ARCH=$(uname -m)
if [ "$ARCH" = "arm64" ]; then
    ARCH_TAG="arm"
elif [ "$ARCH" = "x86_64" ]; then
    ARCH_TAG="x64"
else
    echo "Unsupported architecture: $ARCH"
    exit 1
fi

# Detect macOS version
MACOS_VERSION=$(sw_vers -productVersion | cut -d. -f1)
if [ "$MACOS_VERSION" -ge 15 ]; then
    OS_TAG="d25"
    OS_NUMBER="25"
elif [ "$MACOS_VERSION" -ge 14 ]; then
    OS_TAG="d24"
    OS_NUMBER="24"
elif [ "$MACOS_VERSION" -ge 13 ]; then
    OS_TAG="d23"
    OS_NUMBER="23"
else
    OS_TAG="d22"
    OS_NUMBER="22"
fi

# Detect clang version
CLANG_VERSION=$(clang++ --version | head -1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1 | cut -d. -f1)
CLANG_TAG="clang${CLANG_VERSION}00"

# Display configuration
echo "==========================================================================="
echo "Complete DevEnv Build Configuration"
echo "==========================================================================="
echo "Architecture:      ${ARCH} (${ARCH_TAG})"
echo "macOS Version:     $(sw_vers -productVersion) (${OS_TAG})"
echo "Clang Version:     ${CLANG_VERSION} (${CLANG_TAG})"
echo "DevEnv Tag:        ${DEVENV_TAG}"
echo "Boost Version:     ${BOOST_VERSION}"
echo "OpenSSL Version:   ${OPENSSL_VERSION}"
echo "OpenJDK Version:   ${JDK_VERSION}"
echo "Gradle Version:    ${GRADLE_VERSION}"
echo "==========================================================================="
echo
echo "Build order:"
echo "  1. Boost libraries"
echo "  2. OpenSSL libraries"
echo "  3. JSON Spirit headers"
echo "  4. OpenJDK"
echo "  5. Gradle"
echo "==========================================================================="
echo


# Trap errors and display which step failed
trap 'echo ""; echo "ERROR: Build failed at step: ${CURRENT_STEP}"; exit 1' ERR

# Step 1: Build Boost
CURRENT_STEP="Building Boost ${BOOST_VERSION}"
echo
echo "==========================================================================="
echo "Step 1/5: ${CURRENT_STEP}"
echo "==========================================================================="
echo
"${SCRIPT_DIR}/build-boost-macos.sh" "${BOOST_VERSION}" "${DEVENV_TAG}"
echo
echo "✓ Boost ${BOOST_VERSION} build complete"

# Step 2: Build OpenSSL
CURRENT_STEP="Building OpenSSL ${OPENSSL_VERSION}"
echo
echo "==========================================================================="
echo "Step 2/5: ${CURRENT_STEP}"
echo "==========================================================================="
echo
"${SCRIPT_DIR}/build-openssl-macos.sh" "${OPENSSL_VERSION}" "${DEVENV_TAG}"
echo
echo "✓ OpenSSL ${OPENSSL_VERSION} build complete"

# Step 3: Install JSON Spirit
CURRENT_STEP="Installing JSON Spirit"
echo
echo "==========================================================================="
echo "Step 3/5: ${CURRENT_STEP}"
echo "==========================================================================="
echo
"${SCRIPT_DIR}/install-json-spirit-macos.sh" "${DEVENV_TAG}"
echo
echo "✓ JSON Spirit installation complete"

# Step 4: Install OpenJDK
CURRENT_STEP="Installing OpenJDK ${JDK_VERSION}"
echo
echo "==========================================================================="
echo "Step 4/5: ${CURRENT_STEP}"
echo "==========================================================================="
echo
"${SCRIPT_DIR}/install-openjdk-macos.sh" "${JDK_VERSION}" "${DEVENV_TAG}"
echo
echo "✓ OpenJDK ${JDK_VERSION} installation complete"

# Step 5: Install Gradle
CURRENT_STEP="Installing Gradle ${GRADLE_VERSION}"
echo
echo "==========================================================================="
echo "Step 5/5: ${CURRENT_STEP}"
echo "==========================================================================="
echo
"${SCRIPT_DIR}/install-gradle-macos.sh" "${GRADLE_VERSION}" "${DEVENV_TAG}"
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
echo "  ✓ Boost ${BOOST_VERSION}"
echo "  ✓ OpenSSL ${OPENSSL_VERSION}"
echo "  ✓ JSON Spirit"
echo "  ✓ OpenJDK ${JDK_VERSION}"
echo "  ✓ Gradle ${GRADLE_VERSION}"
echo
echo "Installation directory:"
echo "  ${HOME}/swblocks/dist-${DEVENV_TAG}-darwin-${OS_NUMBER}-${ARCH_TAG}"
echo
echo "Component locations:"
echo "  Boost:       ${HOME}/swblocks/dist-${DEVENV_TAG}-darwin-${OS_NUMBER}-${ARCH_TAG}/boost/${BOOST_VERSION}"
echo "  OpenSSL:     ${HOME}/swblocks/dist-${DEVENV_TAG}-darwin-${OS_NUMBER}-${ARCH_TAG}/openssl/${OPENSSL_VERSION}"
echo "  JSON Spirit: ${HOME}/swblocks/dist-${DEVENV_TAG}-darwin-${OS_NUMBER}-${ARCH_TAG}/json-spirit"
echo "  OpenJDK:     ${HOME}/swblocks/dist-${DEVENV_TAG}-darwin-${OS_NUMBER}-${ARCH_TAG}/jdk/open-jdk/${JDK_VERSION}"
echo "  Gradle:      ${HOME}/swblocks/dist-${DEVENV_TAG}-darwin-${OS_NUMBER}-${ARCH_TAG}/gradle/latest/default"
echo
echo "To use this environment, set DIST_ROOT_DEPS paths in your project to:"
echo "  ${HOME}/swblocks/dist-${DEVENV_TAG}-darwin-${OS_NUMBER}-${ARCH_TAG}"
echo
echo "==========================================================================="
