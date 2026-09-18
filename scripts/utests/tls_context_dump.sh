#!/usr/bin/env bash
#
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
#
# Build tls_context_dump.cpp - the probe which dumps the observable configuration of the TLS
# contexts the library builds, so that a refactor of the context initialization can be shown to
# change nothing. Build it against two worktrees, run both, and diff the output
#
#   tls_context_dump.sh <worktree-root> <output-binary>
#
# There is deliberately NO makefile target for this. It is a proof probe, run by hand when a
# change touches crypto/CryptoBase.h, not something the tree builds
#
# THE FLAGS BELOW ARE A CAPTURE, NOT A DERIVATION. They were taken from
# `make -n utf_baselib_http2 TOOLCHAIN=clang2010 VARIANT=debug` on 2026-09-17, dropping only the
# two unit-test-only defines (-DBL_IS_UNIT_TEST_BINARY -DBL_ENABLE_EXCEPTION_HOOKS). They pin a
# toolchain, a Boost, an OpenSSL and an architecture, so they go stale when any of those move.
# The dist root, the tool versions and the architecture tag are variables below and can be
# overridden from the environment. The target triplet (aarch64-unknown-linux-gnu, aarch64-linux-gnu)
# and the clang resource directory (lib/clang/20) are NOT - they are still literal in the command -
# so overriding BL_ARCH or BL_CLANG_VERSION alone does not make a different platform build. If the
# build fails after a devenv change, recapture from `make -n` rather than patching one flag at a time
#
# The OpenSSL version matters more than the rest: this probe exists to compare two revisions on
# the SAME OpenSSL, and the evidence it produces is per flavor. See
# notes/plans/issues/openssl-1x-evidence-not-producible-record.md for the 1.1.1w half, which is
# owed and which needs BL_OPENSSL_VERSION and the dist to change together

set -eu

ROOT=${1:?usage: tls_context_dump.sh <worktree-root> <output-binary>}
OUT=${2:?usage: tls_context_dump.sh <worktree-root> <output-binary>}

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

#
# Machine and devenv specific - override any of these from the environment
#

DIST=${BL_DIST_ROOT:-/home/lazar/swblocks/dist-devenv7-ub24-gcc1520-clang2010-a64}
CLANG_VERSION=${BL_CLANG_VERSION:-20.1.0}
BOOST_VERSION=${BL_BOOST_VERSION:-1.90.0}
OPENSSL_VERSION=${BL_OPENSSL_VERSION:-3.5.4}
JDK_VERSION=${BL_JDK_VERSION:-25}
ARCH=${BL_ARCH:-a64}
PLAT=${BL_PLAT:-ub24-$ARCH-clang2010-debug}
TOOLCHAIN_PLAT=${BL_TOOLCHAIN_PLAT:-ub24-$ARCH-clang2010-release}

TOOLCHAIN=$DIST/toolchain-clang/$CLANG_VERSION/$TOOLCHAIN_PLAT
CLANG=$TOOLCHAIN/bin/clang++
BOOST=$DIST/boost/$BOOST_VERSION/$PLAT
SSL=$DIST/openssl/$OPENSSL_VERSION/$PLAT

"$CLANG" \
    -stdlib=libc++ -Wno-unused-command-line-argument -nostdinc -std=c++11 -fPIC \
    -Wall -Wpedantic -Wextra -fno-strict-aliasing -fmessage-length=0 -fvisibility=hidden \
    -ggdb -Werror -O0 \
    -DOPENSSL_API_COMPAT=0x10100000L -DBL_DEVENV_VERSION=7 -DBL_BUILD_PLATFORM=linux-ub24 \
    -DBL_BUILD_VARIANT=debug -DBL_BUILD_ARCH=$ARCH -DBL_BUILD_OS=ub24 -DBL_BUILD_TOOLCHAIN=clang2010 \
    -D_FILE_OFFSET_BITS=64 -DBOOST_ALL_NO_LIB -DBOOST_BIND_GLOBAL_PLACEHOLDERS \
    -I"$ROOT/src/versioning" -I"$ROOT/src/include" -I"$ROOT/src/utests/include" -I"$ROOT/src/local" \
    -isystem "$DIST/openjdk/$JDK_VERSION/$ARCH/include" \
    -isystem "$DIST/openjdk/$JDK_VERSION/$ARCH/include/linux" \
    -isystem "$BOOST/include" \
    -isystem "$DIST/openssl/$OPENSSL_VERSION/source" \
    -isystem "$DIST/openssl/$OPENSSL_VERSION/source/include" \
    -isystem "$SSL/include" \
    -isystem "$TOOLCHAIN/include/aarch64-unknown-linux-gnu/c++/v1" \
    -isystem "$TOOLCHAIN/include/c++/v1" \
    -isystem "$TOOLCHAIN/lib/clang/20/include" \
    -isystem /usr/local/include -isystem /usr/include/aarch64-linux-gnu -isystem /usr/include \
    "$HERE/tls_context_dump.cpp" \
    -o "$OUT" \
    -pthread -fuse-ld=lld -rtlib=compiler-rt --unwindlib=libunwind -static-libstdc++ \
    -Wl,-Bstatic \
    -L"$TOOLCHAIN/lib/aarch64-unknown-linux-gnu" \
    -L"$TOOLCHAIN/lib" \
    -L"$BOOST/lib" -L"$SSL/lib" \
    -lboost_date_time-mt-sd-$ARCH -lboost_thread-mt-sd-$ARCH -lboost_filesystem-mt-sd-$ARCH \
    -lboost_program_options-mt-sd-$ARCH -lboost_regex-mt-sd-$ARCH -lboost_random-mt-sd-$ARCH \
    -lboost_locale-mt-sd-$ARCH -lboost_json-mt-sd-$ARCH \
    -lssl -lcrypto -lc++ -lc++abi -lunwind \
    -Wl,-Bdynamic -lrt -ldl
