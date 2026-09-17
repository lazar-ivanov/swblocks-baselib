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
# Run the x64 build/test matrix in an amd64 container on an aarch64 host.
#
# This is launch.sh wired for an unattended run: it adds --init, because PID 1
# here is not an interactive shell and something must reap orphans or process
# group teardown tests fail, and it sets HOME explicitly, because
# projects/make/ci-init-env.mk derives DIST_ROOT_DEPS* from $(HOME).
#
# The container home is a SEPARATE checkout and a SEPARATE dist, bind mounted
# from $HOME/x64_home. Prepare both before the first run - see the matrix
# section of scripts/devenv7/AGENTS.md.
#
# Usage: ./run-matrix-x64.sh [run-matrix.sh options]
#
#   Anything passed is forwarded to run-matrix.sh inside the container, so
#   --toolchains, --variants, --test-jobs, --keep-going all work here.
#
# Environment:
#   BL_X64_HOME     container home on the host; default $HOME/x64_home
#   BL_X64_REPO     repo path inside the container;
#                   default /home/lazar/dev/github/swblocks-baselib
#   BL_DOCKER_IMAGE default ubuntu-dev
###############################################################################

set -u

X64_HOME="${BL_X64_HOME:-$HOME/x64_home}"
IMAGE="${BL_DOCKER_IMAGE:-ubuntu-dev}"

#
# HOME inside the container is the mount point, which is the invoking user's
# home path because /etc/passwd is mounted through
#
CONTAINER_HOME="$HOME"
REPO="${BL_X64_REPO:-$CONTAINER_HOME/dev/github/swblocks-baselib}"

if [ ! -d "$X64_HOME" ]; then
    echo "run-matrix-x64.sh: '$X64_HOME' does not exist; see scripts/devenv7/AGENTS.md" >&2
    exit 2
fi

if [ ! -d "$X64_HOME/dev/github/swblocks-baselib" ]; then
    echo "run-matrix-x64.sh: no checkout under '$X64_HOME/dev/github'; clone it first" >&2
    exit 2
fi

if ! ls -d "$X64_HOME"/swblocks/dist-*-x64 >/dev/null 2>&1; then
    echo "run-matrix-x64.sh: no x64 dist under '$X64_HOME/swblocks'; unpack the tarball first" >&2
    echo "  cd $X64_HOME && tar -xf swblocks/tar/dist-devenv7-ub24-gcc1520-clang2010-x64.tar.gz" >&2
    exit 2
fi

#
# Refuse to start on QEMU. Both it and Rosetta answer for x86_64 ELF, so the fallback
# is silent - and it is not merely slower: qemu-user SIGSEGVs inside the JVM under
# gradle, which fails utf_baselib_jni hours into an otherwise healthy build.
#
# Rosetta is evicted by any package that ships a binfmt.d drop-in, because
# systemd-binfmt flushes the table and rebuilds it from .conf files only, and
# rosetta-binfmt.service registers imperatively. See scripts/devenv7/AGENTS.md.
#
if [ ! -e /proc/sys/fs/binfmt_misc/rosetta ] && [ "${BL_ALLOW_QEMU:-0}" != "1" ]; then
    echo "run-matrix-x64.sh: Rosetta is not registered; x86_64 would fall back to QEMU." >&2
    echo "  sudo /usr/local/bin/register-rosetta.sh            # restore it now" >&2
    echo "  see scripts/devenv7/AGENTS.md for the permanent binfmt.d drop-in" >&2
    echo "  BL_ALLOW_QEMU=1 to proceed anyway (expect the JNI build to abort)" >&2
    exit 2
fi

if [ -e /proc/sys/fs/binfmt_misc/qemu-x86_64 ]; then
    echo "run-matrix-x64.sh: warning - qemu-x86_64 is registered alongside rosetta;" >&2
    echo "  precedence is by registration order, so confirm which one actually ran." >&2
fi

exec docker run --rm --init \
    --platform linux/amd64 \
    --user "$( id -u ):$( id -g )" \
    -e HOME="$CONTAINER_HOME" \
    -v /etc/passwd:/etc/passwd:ro \
    -v /etc/group:/etc/group:ro \
    -v /media/psf/RosettaLinux:/media/psf/RosettaLinux \
    -v "$X64_HOME":"$CONTAINER_HOME" \
    -w "$REPO" \
    "$IMAGE" \
    bash -c "'$REPO/scripts/devenv7/linux/run-matrix.sh' --repo-root '$REPO' $*"
