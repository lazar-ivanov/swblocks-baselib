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
# DevEnv7 Distribution Archiver for macOS
# This script creates tar.gz archives for dist directories under ~/swblocks
#
# Usage:
#   ./archive-dists-macos.sh [--devenv TAG]
#
# Defaults:
#   DEVENV_TAG: devenv7
#
# Examples:
#   ./archive-dists-macos.sh
#   ./archive-dists-macos.sh --devenv devenv7
#
# Environment:
#   SWBLOCKS_ROOT: Root path for distributions (default: ~/swblocks)
###############################################################################

set -e  # Exit on error
set -u  # Exit on undefined variable

usage() {
    cat <<'EOF_USAGE'
Usage:
  ./archive-dists-macos.sh [--devenv TAG]

Defaults:
  DEVENV_TAG: devenv7

Examples:
  ./archive-dists-macos.sh
  ./archive-dists-macos.sh --devenv devenv7

Environment:
  SWBLOCKS_ROOT: Root path for distributions (default: ~/swblocks)
EOF_USAGE
}

DEVENV_TAG="devenv7"

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
        -h|--help)
            usage
            exit 0
            ;;
        --)
            shift
            break
            ;;
        -* )
            echo "ERROR: Unknown option: $1"
            usage
            exit 1
            ;;
        *)
            echo "ERROR: Unexpected argument: $1"
            usage
            exit 1
            ;;
    esac
done

SWBLOCKS_ROOT="${SWBLOCKS_ROOT:-${HOME}/swblocks}"
TAR_DIR="${SWBLOCKS_ROOT}/tar"

if [ ! -d "${SWBLOCKS_ROOT}" ]; then
    echo "ERROR: SWBLOCKS_ROOT not found: ${SWBLOCKS_ROOT}"
    exit 1
fi

mkdir -p "${TAR_DIR}"

# Detect macOS version
MACOS_VERSION=$(sw_vers -productVersion | cut -d. -f1)
if [ "${MACOS_VERSION}" -ge 15 ]; then
    OS_TAG="25"  # macOS 15 (Sequoia) and above
elif [ "${MACOS_VERSION}" -ge 14 ]; then
    OS_TAG="24"  # macOS 14 (Sonoma)
elif [ "${MACOS_VERSION}" -ge 13 ]; then
    OS_TAG="23"  # macOS 13 (Ventura)
else
    OS_TAG="22"  # macOS 12 (Monterey) and below
fi

# Detect architecture
ARCH=$(uname -m)
if [ "${ARCH}" = "arm64" ]; then
    ARCH_TAG="a64"
elif [ "${ARCH}" = "x86_64" ]; then
    ARCH_TAG="x64"
else
    echo "Unsupported architecture: ${ARCH}"
    exit 1
fi

dist_name="dist-${DEVENV_TAG}-darwin-${OS_TAG}-${ARCH_TAG}"
dist_path="${SWBLOCKS_ROOT}/${dist_name}"
tar_path="${TAR_DIR}/${dist_name}.tar.gz"

if [ ! -d "${dist_path}" ]; then
    echo "Skipping missing directory: ${dist_path}"
    exit 1
fi

if [ -f "${tar_path}" ]; then
    echo "ERROR: Archive already exists: ${tar_path}"
    exit 1
fi

echo "Creating archive: ${tar_path}"
COPYFILE_DISABLE=1 tar --uid 0 --gid 0 -czf "${tar_path}" -C "${SWBLOCKS_ROOT}" "${dist_name}"
