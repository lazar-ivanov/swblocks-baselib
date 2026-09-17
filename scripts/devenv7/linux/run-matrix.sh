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
# Build and test the toolchain x variant matrix for the current host.
#
# ARCH is not a parameter. platform.mk assigns it from uname -m on Linux, so
# this script builds a64 on an aarch64 host and x64 inside an amd64 container.
# See scripts/devenv7/AGENTS.md for how to get an x64 container.
#
# Builds at -j1: AGENTS.md forbids parallelizing a whole-repo build, and the
# largest test translation units peak near 3.7GB resident under gcc -O2, so two
# concurrent compiles will OOM an 8GB machine. Tests default to -j5, the
# AGENTS.md cap.
#
# Usage: ./run-matrix.sh [options]
#
#   --toolchains "a b"   default "gcc1520 clang2010"
#   --variants "a b"     default "debug release"
#   --test-jobs N        default 5
#   --log-dir DIR        default <repo>/bld/matrix-logs
#   --repo-root DIR      default the repo this script lives in
#   --min-free-mb N      abort before a combo below this; default 1024
#   --keep-going         carry on to the next combo after a failure
#   --help
#
# Each combo logs to <log-dir>/{build,test}-<toolchain>-<variant>.log, and the
# per-module test logs are copied to <log-dir>/utflogs-<toolchain>-<variant>/
# so they survive the next combo and any cleanup of the build tree.
###############################################################################

set -u

TOOLCHAINS="gcc1520 clang2010"
VARIANTS="debug release"
TEST_JOBS=5
LOG_DIR=""
REPO_ROOT=""
MIN_FREE_MB=1024
KEEP_GOING=0

while [ $# -gt 0 ]; do
    case "$1" in
        --toolchains)  TOOLCHAINS="$2";  shift 2 ;;
        --variants)    VARIANTS="$2";    shift 2 ;;
        --test-jobs)   TEST_JOBS="$2";   shift 2 ;;
        --log-dir)     LOG_DIR="$2";     shift 2 ;;
        --repo-root)   REPO_ROOT="$2";   shift 2 ;;
        --min-free-mb) MIN_FREE_MB="$2"; shift 2 ;;
        --keep-going)  KEEP_GOING=1;     shift ;;
        --help)
            sed -n '20,44p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *)
            echo "run-matrix.sh: unknown option '$1'; try --help" >&2
            exit 2
            ;;
    esac
done

#
# The repo root is three levels up from scripts/devenv7/linux, unless told otherwise -
# an override lets a copy of this script drive a checkout it does not live in
#
[ -n "$REPO_ROOT" ] || REPO_ROOT="$( cd "$( dirname "${BASH_SOURCE[0]}" )/../../.." && pwd )"

if [ ! -f "$REPO_ROOT/projects/make/common.mk" ]; then
    echo "run-matrix.sh: '$REPO_ROOT' is not a baselib checkout; pass --repo-root" >&2
    exit 2
fi

cd "$REPO_ROOT" || exit 1

[ -n "$LOG_DIR" ] || LOG_DIR="$REPO_ROOT/bld/matrix-logs"
mkdir -p "$LOG_DIR" || exit 1

SUMMARY="$LOG_DIR/summary.txt"
: > "$SUMMARY"

note() { echo "$( date '+%Y-%m-%d %H:%M:%S' ) | $*" | tee -a "$SUMMARY"; }

#
# Mirrors the uname -m mapping platform.mk applies; reported for the log only,
# since the makefiles remain the authority on what actually gets built
#
case "$( uname -m )" in
    aarch64|arm64) PLAT_ARCH="a64" ;;
    x86_64)        PLAT_ARCH="x64" ;;
    i386|i686)     PLAT_ARCH="x86" ;;
    *)             PLAT_ARCH="$( uname -m )" ;;
esac

note "HOST   $( uname -m ) / arch=$PLAT_ARCH / $( nproc ) cores"
note "MATRIX toolchains='$TOOLCHAINS' variants='$VARIANTS' build=-j1 test=-j$TEST_JOBS"

FAILURES=0

for TC in $TOOLCHAINS; do
    for V in $VARIANTS; do
        COMBO="$TC-$V"

        #
        # A full tree is several GB. Stopping here beats dying mid-link, where a
        # truncated object and .DELETE_ON_ERROR make the cause hard to read back
        #
        FREE_MB="$( df -m . | tail -1 | awk '{ print $4 }' )"

        if [ "$FREE_MB" -lt "$MIN_FREE_MB" ]; then
            note "ABORT  only ${FREE_MB}MB free before $COMBO, need $MIN_FREE_MB"
            exit 1
        fi

        note "BUILD  START  $COMBO  (${FREE_MB}MB free)"
        t0=$( date +%s )
        make -k -j1 all TOOLCHAIN="$TC" VARIANT="$V" > "$LOG_DIR/build-$COMBO.log" 2>&1
        rc=$?
        t1=$(( $( date +%s ) - t0 ))
        note "BUILD  END    $COMBO  rc=$rc  $(( t1 / 60 ))m"

        if [ $rc -ne 0 ]; then
            FAILURES=$(( FAILURES + 1 ))
            [ $KEEP_GOING -eq 1 ] || { note "ABORT  build failed; see $LOG_DIR/build-$COMBO.log"; exit 1; }

            #
            # Test anyway. The build ran under -k, so one broken target does not mean
            # the other modules are unbuilt, and their results are the whole point of
            # the run - a single failing target once cost a matrix its entire test signal
            #
            note "NOTE   build failed; testing what did build"
        fi

        note "TEST   START  $COMBO"
        t0=$( date +%s )
        make -k -j"$TEST_JOBS" test TOOLCHAIN="$TC" VARIANT="$V" > "$LOG_DIR/test-$COMBO.log" 2>&1
        rc=$?
        t1=$(( $( date +%s ) - t0 ))
        note "TEST   END    $COMBO  rc=$rc  $(( t1 / 60 ))m"

        #
        # Preserve the per-module logs; they carry the failure detail, which the
        # make output above does not
        #
        for tree in bld/*"-$COMBO"; do
            [ -d "$tree/utflogs" ] && cp -r "$tree/utflogs" "$LOG_DIR/utflogs-$COMBO"
        done

        if [ $rc -ne 0 ]; then
            FAILURES=$(( FAILURES + 1 ))
            [ $KEEP_GOING -eq 1 ] || { note "ABORT  tests failed; see $LOG_DIR/utflogs-$COMBO/"; exit 1; }
        fi

        note "DISK   $( df -h . | tail -1 | awk '{ print $4 }' ) free"
    done
done

note "MATRIX COMPLETE  failures=$FAILURES"
exit $( [ $FAILURES -eq 0 ] && echo 0 || echo 1 )
