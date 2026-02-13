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
# DevEnv7 Distribution Archiver for Linux
# This script creates tar.gz archives for dist directories under ~/swblocks
#
# Usage:
#   ./archive-dists-linux.sh [--devenv TAG] [--gcc VERSION] [--clang VERSION] DIST_TAG [DIST_TAG ...]
#
#   DIST_TAG: Required dist tag(s). Valid values: gcc, clang, gcc-clang
#
# Defaults:
#   DEVENV_TAG:  devenv7
#   GCC_VERSION: 15.2.0
#   CLANG_VERSION: 20.1.0
#
# Examples:
#   ./archive-dists-linux.sh gcc
#   ./archive-dists-linux.sh gcc clang
#   ./archive-dists-linux.sh --gcc 15.2.0 --clang 20.1.0 gcc-clang
#   ./archive-dists-linux.sh --devenv devenv7 gcc clang gcc-clang
#
# Environment:
#   SWBLOCKS_ROOT: Root path for distributions (default: ~/swblocks)
###############################################################################

set -e  # Exit on error
set -u  # Exit on undefined variable

usage() {
    cat <<'EOF'
Usage:
  ./archive-dists-linux.sh [--devenv TAG] [--gcc VERSION] [--clang VERSION] DIST_TAG [DIST_TAG ...]

  DIST_TAG: Required dist tag(s). Valid values: gcc, clang, gcc-clang

Defaults:
  DEVENV_TAG:    devenv7
  GCC_VERSION:   15.2.0
  CLANG_VERSION: 20.1.0

Examples:
  ./archive-dists-linux.sh gcc
  ./archive-dists-linux.sh gcc clang
  ./archive-dists-linux.sh --gcc 15.2.0 --clang 20.1.0 gcc-clang
  ./archive-dists-linux.sh --devenv devenv7 gcc clang gcc-clang

Environment:
  SWBLOCKS_ROOT: Root path for distributions (default: ~/swblocks)
EOF
}

DEVENV_TAG="devenv7"
GCC_VERSION="15.2.0"
CLANG_VERSION="20.1.0"
DIST_TAGS=()

while [ "$#" -gt 0 ]; do
    case "$1" in
        --devenv)
            if [ "$#" -lt 2 ]; then
                echo "ERROR: --devenv requires a value"
                usage
                exit 1
            fi
            DEVENV_TAG="$2"
            shift 2
            ;;
        --gcc)
            if [ "$#" -lt 2 ]; then
                echo "ERROR: --gcc requires a value"
                usage
                exit 1
            fi
            GCC_VERSION="$2"
            shift 2
            ;;
        --clang)
            if [ "$#" -lt 2 ]; then
                echo "ERROR: --clang requires a value"
                usage
                exit 1
            fi
            CLANG_VERSION="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --)
            shift
            while [ "$#" -gt 0 ]; do
                DIST_TAGS+=("$1")
                shift
            done
            break
            ;;
        -*)
            echo "ERROR: Unknown option: $1"
            usage
            exit 1
            ;;
        *)
            DIST_TAGS+=("$1")
            shift
            ;;
    esac
done

if [ "${#DIST_TAGS[@]}" -eq 0 ]; then
    echo "ERROR: At least one dist tag is required"
    usage
    exit 1
fi

SWBLOCKS_ROOT="${SWBLOCKS_ROOT:-${HOME}/swblocks}"
TAR_DIR="${SWBLOCKS_ROOT}/tar"

if [ ! -d "${SWBLOCKS_ROOT}" ]; then
    echo "ERROR: SWBLOCKS_ROOT not found: ${SWBLOCKS_ROOT}"
    exit 1
fi

mkdir -p "${TAR_DIR}"

GCC_VERSION_NO_DOTS=$(echo "${GCC_VERSION}" | tr -d '.')
CLANG_VERSION_NO_DOTS=$(echo "${CLANG_VERSION}" | tr -d '.')
GCC_TAG="gcc${GCC_VERSION_NO_DOTS}"
CLANG_TAG="clang${CLANG_VERSION_NO_DOTS}"

if [ -f /etc/os-release ]; then
    . /etc/os-release
    if [ "${ID}" = "ubuntu" ]; then
        UBUNTU_VERSION=$(echo "${VERSION_ID}" | cut -d. -f1)
        OS_TAG="ub${UBUNTU_VERSION}"
    elif [ "${ID}" = "rhel" ]; then
        RHEL_VERSION=$(echo "${VERSION_ID}" | cut -d. -f1)
        OS_TAG="rhel${RHEL_VERSION}"
    else
        echo "Unsupported OS: ${ID}"
        exit 1
    fi
else
    echo "Cannot detect OS version"
    exit 1
fi

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

for dist_tag in "${DIST_TAGS[@]}"; do
    case "${dist_tag}" in
        gcc)
            DIST_SUFFIX="${GCC_TAG}"
            ;;
        clang)
            DIST_SUFFIX="${CLANG_TAG}"
            ;;
        gcc-clang)
            DIST_SUFFIX="${GCC_TAG}-${CLANG_TAG}"
            ;;
        *)
            echo "ERROR: Invalid dist tag: ${dist_tag}"
            usage
            exit 1
            ;;
    esac

    dist_name="dist-${DEVENV_TAG}-${OS_TAG}-${DIST_SUFFIX}-${ARCH_TAG}"
    dist_path="${SWBLOCKS_ROOT}/${dist_name}"
    tar_path="${TAR_DIR}/${dist_name}.tar.gz"

    if [ ! -d "${dist_path}" ]; then
        echo "Skipping missing directory: ${dist_path}"
        continue
    fi

    if [ -f "${tar_path}" ]; then
        echo "Skipping existing archive: ${tar_path}"
        continue
    fi

    echo "Creating archive: ${tar_path}"
    tar --owner=0 --group=0 --numeric-owner -czf "${tar_path}" -C "${SWBLOCKS_ROOT}" "${dist_name}"
done
