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

#
# check_split.sh - the single gate for the test module split
#
# Run this before every handback and after every integrated commit. A red gate blocks the merge
#
# Tiers, cheapest first; each is skipped with a clear note when its inputs are absent, so the
# script is useful from a bare checkout and gets stricter as build and run artifacts appear:
#
#   1   source equivalence   no build needed, about a second
#   2   object ceiling       needs a built x86 debug tree
#   3   runtime equivalence  needs test logs or built binaries
#   eol line endings         guards the CRLF hazard git diff --check cannot see
#
# Usage:
#
#   check_split.sh                          run every tier whose inputs exist
#   check_split.sh --tier1                  source equivalence only
#   check_split.sh --ceiling 40             override the object size ceiling
#   check_split.sh --bld bld/win-x86-vc143-debug --run
#

set -u

HERE="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
ROOT="$( cd "${HERE}/../.." && pwd )"
BASELINE="${ROOT}/notes/reviews/major/update_2026/baseline"

CEILING=75
TIER1_ONLY=0
DO_RUN=0
BLD=""

#
# The devenv7 dist interpreter is an embeddable build with no venv and no pip, and the repo
# .venv points at a scratch directory which does not survive cleanup, so resolve a usable
# interpreter rather than assuming either one works
#

resolve_python() {

    if [[ -n "${UTF_PYTHON:-}" ]] && "${UTF_PYTHON}" -c 'import json' >/dev/null 2>&1; then
        echo "${UTF_PYTHON}"
        return 0
    fi

    local candidate
    for candidate in \
        "${ROOT}/.venv/Scripts/python.exe" \
        "${ROOT}/.venv/bin/python" \
        "$( command -v python3 2>/dev/null )" \
        "$( command -v python 2>/dev/null )"
    do
        if [[ -n "${candidate}" ]] && "${candidate}" -c 'import json' >/dev/null 2>&1; then
            echo "${candidate}"
            return 0
        fi
    done

    for candidate in "${HOME}"/swblocks/dist-devenv7-*/python/*/default/python.exe; do
        if [[ -x "${candidate}" ]] && "${candidate}" -c 'import json' >/dev/null 2>&1; then
            echo "${candidate}"
            return 0
        fi
    done

    return 1
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --tier1)   TIER1_ONLY=1; shift ;;
        --ceiling) CEILING="$2"; shift 2 ;;
        --bld)     BLD="$2"; shift 2 ;;
        --run)     DO_RUN=1; shift ;;
        -h|--help) sed -n '18,40p' "$0"; exit 0 ;;
        *) echo "check_split: unknown argument: $1" >&2; exit 2 ;;
    esac
done

PYTHON="$( resolve_python )" || {
    echo "check_split: FATAL - no usable python interpreter found" >&2
    echo "check_split: set UTF_PYTHON, or see scripts/devenv7/AGENTS.md for provisioning" >&2
    exit 2
}

echo "check_split: using ${PYTHON}"
echo ""

RC=0
SUMMARY=()

note() { SUMMARY+=( "$1" ); }

# ---------------------------------------------------------------- tier 1

echo "=== tier 1: source equivalence ==============================================="

if [[ -f "${BASELINE}/inventory.json" ]]; then
    if "${PYTHON}" "${HERE}/utf_inventory.py" --compare "${BASELINE}/inventory.json"; then
        note "tier1  PASS  source equivalence (C1-C10)"
    else
        note "tier1  FAIL  source equivalence (C1-C10)"
        RC=1
    fi
else
    echo "check_split: no baseline inventory at ${BASELINE}/inventory.json"
    echo "check_split: capture one first - utf_inventory.py --capture <path>"
    note "tier1  SKIP  no baseline inventory"
fi

echo ""

# ---------------------------------------------------------------- line endings

echo "=== line endings ============================================================="

#
# Two checks, because neither alone is sufficient. git ls-files --eol covers tracked files but
# is blind to new ones, and a naive grep for a carriage return is unreliable in this shell - so
# untracked files are checked with file(1), which reports CRLF explicitly
#
# This matters because core.autocrlf is true with no .gitattributes, and git diff --check does
# not catch a whole-file line-ending rewrite in either direction
#

BAD_EOL="$( cd "${ROOT}" && git ls-files --eol src/utests scripts/utests 2>/dev/null | grep -v 'i/lf' || true )"

BAD_NEW=""
while IFS= read -r candidate; do
    [[ -z "${candidate}" ]] && continue
    if file "${ROOT}/${candidate}" 2>/dev/null | grep -q 'CRLF'; then
        BAD_NEW+="${candidate}"$'\n'
    fi
done < <( cd "${ROOT}" && git ls-files --others --exclude-standard src/utests scripts/utests 2>/dev/null )

if [[ -z "${BAD_EOL}" && -z "${BAD_NEW}" ]]; then
    echo "tracked files are LF in the index; no untracked file has CRLF terminators"
    note "eol    PASS  line endings"
else
    [[ -n "${BAD_EOL}" ]] && { echo "tracked files whose index line endings are not LF:"; echo "${BAD_EOL}"; }
    [[ -n "${BAD_NEW}" ]] && { echo "untracked files with CRLF terminators:"; echo "${BAD_NEW}"; }
    note "eol    FAIL  line endings"
    RC=1
fi

echo ""

if [[ "${TIER1_ONLY}" == "1" ]]; then
    printf '%s\n' "${SUMMARY[@]}"
    exit "${RC}"
fi

# ---------------------------------------------------------------- tier 2

echo "=== tier 2: object ceiling (${CEILING} MB) ==================================="

if compgen -G "${ROOT}/bld/win-x86-*-debug/utests" > /dev/null; then
    if "${PYTHON}" "${HERE}/utf_objsize.py" --ceiling "${CEILING}" --quiet; then
        note "tier2  PASS  object ceiling ${CEILING} MB"
    else
        note "tier2  FAIL  object ceiling ${CEILING} MB"
        RC=1
    fi
else
    echo "no x86 debug build tree - build one to gate the ceiling"
    note "tier2  SKIP  no x86 debug build tree"
fi

echo ""

# ---------------------------------------------------------------- tier 3

echo "=== tier 3: runtime equivalence =============================================="

NONDET_ARG=()
[[ -f "${BASELINE}/nondeterministic.json" ]] && NONDET_ARG=( --nondet "${BASELINE}/nondeterministic.json" )

#
# The baseline covers 17 of the tree's modules and was captured on one platform, so a tier 3 PASS
# is a statement about those modules only. utf_runlog prints which modules it could not speak about
# and why; this file supplies the why
#

UNCOVERED_ARG=()
[[ -f "${BASELINE}/uncovered.json" ]] && UNCOVERED_ARG=( --uncovered "${BASELINE}/uncovered.json" )

if [[ ! -f "${BASELINE}/runlog.json" ]]; then
    echo "no runtime baseline at ${BASELINE}/runlog.json"
    note "tier3  SKIP  no runtime baseline"
elif [[ "${DO_RUN}" == "1" && -n "${BLD}" ]]; then
    if "${PYTHON}" "${HERE}/utf_runlog.py" --run --bld "${BLD}" \
        --compare "${BASELINE}/runlog.json" "${NONDET_ARG[@]}" "${UNCOVERED_ARG[@]}"; then
        note "tier3  PASS  runtime equivalence, baseline modules only (ran binaries)"
    else
        note "tier3  FAIL  runtime equivalence, baseline modules only (ran binaries)"
        RC=1
    fi
elif [[ -n "${BLD}" && -d "${BLD}/utflogs" ]]; then
    if "${PYTHON}" "${HERE}/utf_runlog.py" --parse-logs "${BLD}/utflogs" \
        --compare "${BASELINE}/runlog.json" "${NONDET_ARG[@]}" "${UNCOVERED_ARG[@]}"; then
        note "tier3  PASS  runtime equivalence, baseline modules only (parsed logs)"
    else
        note "tier3  FAIL  runtime equivalence, baseline modules only (parsed logs)"
        RC=1
    fi
else
    echo "pass --bld <tree> to compare logs, and add --run to execute the binaries"
    note "tier3  SKIP  no build tree given"
fi

echo ""
echo "=== summary =================================================================="

printf '%s\n' "${SUMMARY[@]}"

echo ""

if [[ "${RC}" == "0" ]]; then
    echo "check_split: GREEN"
else
    echo "check_split: RED - the split is not safe to hand back"
fi

exit "${RC}"
