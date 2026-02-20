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
# Prerequisites Check Script for Linux (Ubuntu and RHEL)
# Checks all required build dependencies for the devenv7 development environment.
# Always prints the full install commands for the detected OS, then checks
# whether all packages are installed.
#
# Usage: ./check-prerequisites.sh
#   No parameters — always checks the complete list.
#
# Supports:
#   - Ubuntu (22.04, 24.04, etc.) — checks via dpkg -s
#   - RHEL 9, 10 and clones (Rocky, Alma, CentOS Stream) — checks via rpm -q
#
# Exit codes:
#   0 - All prerequisites are installed
#   1 - Missing prerequisites (details printed to stderr)
###############################################################################

set -e  # Exit on error
set -u  # Exit on undefined variable

###############################################################################
# OS Detection
###############################################################################

if [ ! -f /etc/os-release ]; then
    echo "ERROR: /etc/os-release not found. Cannot detect OS." >&2
    exit 1
fi

. /etc/os-release

# Normalize OS family: rocky, almalinux, centos are treated as RHEL-like
case "$ID" in
    ubuntu)
        OS_FAMILY="debian"
        ;;
    rhel|rocky|almalinux|centos)
        OS_FAMILY="rhel"
        RHEL_VERSION=$(echo "$VERSION_ID" | cut -d. -f1)
        ;;
    *)
        echo "ERROR: Unsupported OS: $ID" >&2
        echo "Supported: ubuntu, rhel, rocky, almalinux, centos" >&2
        exit 1
        ;;
esac

###############################################################################
# Package Lists
###############################################################################

# Ubuntu packages (checked via dpkg -s)
# Note: libz-dev is a virtual package provided by zlib1g-dev, so only zlib1g-dev is listed.
UBUNTU_PACKAGES="
    build-essential
    sudo
    wget
    zlib1g-dev
    libgmp-dev
    libmpfr-dev
    libmpc-dev
    flex
    bison
    texinfo
    libzstd-dev
    perl
    libtext-template-perl
    zip
    unzip
    lsb-release
    python3
    iputils-ping
    ninja-build
    libxml2-dev
    libedit-dev
    swig
    cmake
    gdb
    git
    curl
    vim
    ca-certificates
"

# RHEL base packages (available from BaseOS/AppStream — enabled by default)
# Note: build-essential has no RHEL equivalent; gcc, gcc-c++, make are listed individually.
# Note: RHEL's base perl package is minimal; OpenSSL build requires the additional perl-* packages.
# Note: lsb-release was removed from RHEL 9+; scripts use /etc/os-release instead.
RHEL_PACKAGES_BASE="
    gcc
    gcc-c++
    make
    sudo
    wget
    zlib-devel
    gmp-devel
    mpfr-devel
    libmpc-devel
    flex
    bison
    texinfo
    libzstd-devel
    perl
    perl-devel
    perl-interpreter
    perl-FindBin
    perl-IPC-Cmd
    perl-podlators
    perl-Time-Piece
    perl-Test-Simple
    perl-Test-Harness
    bzip2
    diffutils
    procps-ng
    xz
    zip
    unzip
    python3
    iputils
    libxml2-devel
    cmake
    gdb
    git
    curl
    vim-enhanced
    ca-certificates
"

# RHEL CRB packages (require CodeReady Builder repository to be enabled)
RHEL_PACKAGES_CRB="
    ninja-build
    libedit-devel
    swig
    perl-Text-Template
"

###############################################################################
# Print Guidance (always — even when all packages are installed)
###############################################################################

echo ""
echo "==========================================================================="
echo "Checking build prerequisites..."
echo "==========================================================================="

if [ "$OS_FAMILY" = "debian" ]; then
    echo ""
    echo "Required packages for Ubuntu — install with:"
    echo ""
    echo "  sudo apt-get update && sudo apt-get install -y \\"
    # Print each package indented, one per line for readability
    first=1
    for pkg in ${UBUNTU_PACKAGES}; do
        if [ "$first" = "1" ]; then
            echo "      ${pkg} \\"
            first=0
        else
            echo "      ${pkg} \\"
        fi
    done
    echo ""
else
    # RHEL-like
    echo ""
    echo "Required packages for ${PRETTY_NAME:-RHEL} — install with:"
    echo ""

    # Step 1: Enable CRB repository
    echo "  Step 1: Enable the CodeReady Builder (CRB) repository:"
    echo ""
    if [ "$ID" = "rhel" ]; then
        echo "    sudo subscription-manager repos --enable codeready-builder-for-rhel-${RHEL_VERSION}-\$(arch)-rpms"
    else
        # Rocky, Alma, CentOS Stream
        echo "    sudo dnf config-manager --set-enabled crb"
    fi
    echo ""

    # Step 2: Install all packages
    echo "  Step 2: Install all required packages:"
    echo ""
    echo "  sudo dnf install -y \\"
    for pkg in ${RHEL_PACKAGES_BASE} ${RHEL_PACKAGES_CRB}; do
        echo "      ${pkg} \\"
    done
    echo ""
fi

echo "==========================================================================="

###############################################################################
# Check Packages
###############################################################################

MISSING=""
MISSING_CRB=""

if [ "$OS_FAMILY" = "debian" ]; then
    for pkg in ${UBUNTU_PACKAGES}; do
        if ! dpkg -s "${pkg}" >/dev/null 2>&1; then
            MISSING="${MISSING} ${pkg}"
        fi
    done
else
    # RHEL-like: check base packages
    for pkg in ${RHEL_PACKAGES_BASE}; do
        if ! rpm -q "${pkg}" >/dev/null 2>&1; then
            MISSING="${MISSING} ${pkg}"
        fi
    done
    # RHEL-like: check CRB packages separately
    for pkg in ${RHEL_PACKAGES_CRB}; do
        if ! rpm -q "${pkg}" >/dev/null 2>&1; then
            MISSING="${MISSING} ${pkg}"
            MISSING_CRB="${MISSING_CRB} ${pkg}"
        fi
    done
fi

###############################################################################
# Report Results
###############################################################################

if [ -z "$MISSING" ]; then
    echo ""
    echo "All prerequisites are installed."
    echo ""
    exit 0
fi

# Missing packages found — report and fail
echo "" >&2
echo "ERROR: The following required packages are NOT installed:" >&2
echo "" >&2
for pkg in ${MISSING}; do
    echo "  - ${pkg}" >&2
done
echo "" >&2

if [ "$OS_FAMILY" = "debian" ]; then
    echo "Install them with:" >&2
    echo "" >&2
    echo "  sudo apt-get update && sudo apt-get install -y${MISSING}" >&2
    echo "" >&2
else
    # RHEL-like: check if CRB repo is needed and not enabled
    if [ -n "$MISSING_CRB" ]; then
        CRB_ENABLED=0
        if dnf repolist --enabled 2>/dev/null | grep -qiE '(crb|codeready)'; then
            CRB_ENABLED=1
        fi

        if [ "$CRB_ENABLED" = "0" ]; then
            echo "NOTE: Some missing packages require the CodeReady Builder (CRB) repository," >&2
            echo "which is NOT currently enabled. Enable it first:" >&2
            echo "" >&2
            if [ "$ID" = "rhel" ]; then
                echo "  sudo subscription-manager repos --enable codeready-builder-for-rhel-${RHEL_VERSION}-\$(arch)-rpms" >&2
            else
                echo "  sudo dnf config-manager --set-enabled crb" >&2
            fi
            echo "" >&2
            echo "CRB packages needed:${MISSING_CRB}" >&2
            echo "" >&2
        fi
    fi

    echo "Install missing packages with:" >&2
    echo "" >&2
    echo "  sudo dnf install -y${MISSING}" >&2
    echo "" >&2
fi

exit 1
