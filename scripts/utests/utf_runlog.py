#!/usr/bin/env python
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
# utf_runlog.py - compare what the test binaries actually did, before and after a module split
#
# utf_inventory.py proves the test text did not change. This proves the tests still run and still
# do the same work, which is the half a source comparison cannot reach: a case that lost a side
# effect it used to inherit from a sibling case in the same process still registers and still
# passes, but asserts fewer times
#
# Four signals are compared, all normalized so that a case legitimately moving to another file
# or line does not register as a difference:
#
#   registered   the case list from <binary> --list_content, which exits without running anything
#   executed     the Entering/Leaving pairs and the SKIPPED CASES block from the run log
#   assertions   the per-case counts from --report_level=detailed
#   verdict      the per-case pass/fail/abort lines, and the module's own exit code and totals
#
# Assertion counts are not deterministic for every case - retry loops and perf cases vary - so a
# baseline is captured twice and any case that disagrees with itself is recorded and thereafter
# compared on pass/fail only. Without that the gate produces false alarms and gets ignored
#
# Usage:
#
#   utf_runlog.py --run --bld <tree> --capture run1.json       run every binary and capture
#   utf_runlog.py --parse-logs <utflogs dir> --bld <tree> --capture x.json     parse make's logs
#   utf_runlog.py --nondeterministic run1.json run2.json --capture nondet.json
#   utf_runlog.py --compare before.json --against after.json [--nondet nondet.json]
#                                                            [--uncovered uncovered.json]
#
# Every comparison ends with a coverage statement naming the modules the baseline does not cover,
# because the four signals below are differential and can say nothing whatever about those. A PASS
# is a statement about the covered modules and nothing else
#
# A capture is also stamped with the platform it was taken on - the name of the build tree it read,
# win-x86-vc143-debug or ub24-a64-clang2010-debug - and a comparison across two platforms is refused
# rather than attempted. Refusing exits 3, a third outcome which check_split.sh renders as a SKIP
# naming the reason; see the note above PLATFORM_KEY for why none of the four signals survives the
# crossing
#
# The unstable list carries the same stamp, inherited from the two captures it is derived from, and
# a comparison will not consult one from another platform either. That list is the other input to a
# comparison, and the only one nothing used to check: it excuses the cases it names from the
# assertion comparison, so a list derived elsewhere excuses the wrong ones, silently and for as long
# as it stays committed
#
# Stdlib only, by design - it must run on the devenv7 dist interpreter, which is an embeddable
# build with no venv and no pip
#

from __future__ import print_function

import argparse
import json
import os
import re
import subprocess
import sys

ENTERING_RE = re.compile( r'Entering test case "([^"]+)"' )
LEAVING_RE = re.compile( r'Leaving test case "([^"]+)"' )
RUNNING_RE = re.compile( r'^Running (\d+) test cases?' )
MODULE_RE = re.compile( r'^(?:Entering|Leaving) test module "([^"]+)"' )

#
# A case which checked nothing reports 'has passed' with no trailing 'with:' and no assertion
# line, so the trailing group is what distinguishes the two shapes
#
# One formatter writes both of these lines and it chooses between five verdicts, three of which do
# not take 'has': 'has passed', 'has failed', 'was aborted', 'was skipped' and 'has timed out'. A
# case killed by a failing UTF_REQUIRE is 'was aborted', so matching only 'has' missed it outright
# and the fallthrough at the end of parse_run then recorded that case as passed
#

REPORT_VERDICT = r'(?:has|was) (passed|failed|aborted|skipped|timed out)'

REPORT_CASE_RE = re.compile( r'^\s*Test case "([^"]+)" ' + REPORT_VERDICT + r'(\s+with:)?\s*$' )
REPORT_ASSERT_RE = re.compile( r'^\s*(\d+) assertions? out of (\d+) passed' )
REPORT_MODULE_RE = re.compile( r'^\s*Test module "([^"]+)" ' + REPORT_VERDICT )

#
# Note this mid-run line carries the case name unquoted, unlike every line in the final report
#

REPORT_NO_ASSERT_RE = re.compile( r'^\s*Test case ([A-Za-z0-9_]+) did not check any assertions' )

SKIPPED_HEADER_RE = re.compile( r'^SKIPPED CASES \((\d+)\):' )

NO_ERRORS_RE = re.compile( r'^\*\*\* No errors detected' )
FAILURE_RE = re.compile( r'^\*\*\* (\d+) failure' )

#
# Boost colours the confirmation report, and only that report, so both lines above arrive behind an
# escape sequence whenever the run had a terminal - the report lines further up are never coloured,
# which is why anchoring worked for them and not for these two. Of the logs in this tree, 200 of
# the 207 saying 'No errors detected' and 21 of the 23 carrying a failure count were missed
#
# A capture taken by --run reads a pipe and Boost emits no colour into it, so this only ever adds
# signal to logs make or an operator produced. Stripped for every line rather than tolerated in
# these two patterns, since an escape can precede any of them
#

ANSI_RE = re.compile( r'\x1b\[[0-9;]*[A-Za-z]' )

UTF_FLAGS = [
    '--log_level=test_suite',
    '--catch_system_errors=no',
    '--report_level=detailed',
    ]


def parse_run( text ):
    """
    Parse one module's run output into a per-case record

    Absolute paths, source line numbers, timestamps and testing times are all discarded; they
    change legitimately whenever code moves between files, which is the whole point of the split
    """

    entered = []
    left = set()
    cases = {}
    skipped = []
    expected = None
    failures = None
    clean = False
    reported = False

    in_skipped = False
    pending = None

    for line in text.split( '\n' ):

        stripped = ANSI_RE.sub( '', line ).rstrip()

        if in_skipped:
            if stripped.startswith( '    ' ) and stripped.strip():
                skipped.append( stripped.strip() )
                continue
            in_skipped = False

        matched = SKIPPED_HEADER_RE.match( stripped )
        if matched:
            in_skipped = True
            continue

        matched = RUNNING_RE.match( stripped )
        if matched:
            expected = int( matched.group( 1 ) )
            continue

        matched = ENTERING_RE.search( stripped )
        if matched and not stripped.lstrip().startswith( 'Test case' ):
            entered.append( matched.group( 1 ) )
            continue

        matched = LEAVING_RE.search( stripped )
        if matched:
            left.add( matched.group( 1 ) )
            continue

        matched = REPORT_NO_ASSERT_RE.match( stripped )
        if matched:
            cases.setdefault( matched.group( 1 ), {} ).update(
                { 'assertions': 0, 'assertions_total': 0 }
                )
            continue

        matched = REPORT_MODULE_RE.match( stripped )
        if matched:
            clean = matched.group( 2 ) == 'passed'
            pending = None
            continue

        matched = REPORT_CASE_RE.match( stripped )
        if matched:
            name = matched.group( 1 )
            cases.setdefault( name, {} )[ 'outcome' ] = matched.group( 2 )
            reported = True
            if matched.group( 3 ):
                # an assertion line follows and belongs to this case
                pending = name
            else:
                # the case checked no assertions, so no assertion line will follow
                cases[ name ].setdefault( 'assertions', 0 )
                cases[ name ].setdefault( 'assertions_total', 0 )
                pending = None
            continue

        matched = REPORT_ASSERT_RE.match( stripped )
        if matched:
            if pending:
                cases[ pending ][ 'assertions' ] = int( matched.group( 1 ) )
                cases[ pending ][ 'assertions_total' ] = int( matched.group( 2 ) )
                pending = None
            continue

        if NO_ERRORS_RE.match( stripped ):
            clean = True
            continue

        matched = FAILURE_RE.match( stripped )
        if matched:
            failures = int( matched.group( 1 ) )

    #
    # Entering and leaving is scored passed only when the run printed no case verdicts at all,
    # which is what the default report level does; clean, failures and exit then carry the whole
    # of the module's verdict. When the run did print verdicts and this case has none, a line went
    # unrecognised, and scoring that passed is how a critically-failed case used to read as green
    #

    for name in entered:
        cases.setdefault( name, {} )
        cases[ name ].setdefault( 'outcome',
            ( 'unknown' if reported else 'passed' ) if name in left else 'incomplete' )
        cases[ name ][ 'entered' ] = True
        cases[ name ][ 'left' ] = name in left

    return {
        'expected': expected,
        'entered': sorted( set( entered ) ),
        'incomplete': sorted( set( entered ) - left ),
        'skipped': sorted( skipped ),
        'cases': cases,
        'clean': clean,
        'failures': failures,
        }


def find_binaries( bld_tree ):
    """
    Return { module: path } for every built unit-test executable under a bld tree
    """

    found = {}

    utests_dir = os.path.join( bld_tree, 'utests' )

    if not os.path.isdir( utests_dir ):
        return found

    for module in sorted( os.listdir( utests_dir ) ):

        module_dir = os.path.join( utests_dir, module )

        if not os.path.isdir( module_dir ):
            continue

        stem = module.replace( '_', '-' )

        for candidate in ( stem + '.exe', stem ):
            path = os.path.join( module_dir, candidate )
            if os.path.isfile( path ) and os.access( path, os.X_OK ):
                found[ module ] = path
                break

    return found


def list_content( path ):
    """
    Return the registered case names from a binary, which --list_content prints without running
    """

    try:
        output = subprocess.run(
            [ path, '--list_content' ],
            stdout = subprocess.PIPE,
            stderr = subprocess.STDOUT,
            timeout = 120,
            ).stdout.decode( 'utf-8', 'replace' )
    except Exception as error:
        return None, str( error )

    names = []

    for line in output.split( '\n' ):
        stripped = line.strip()
        # Boost prints one name per line, suffixed with * when the case is enabled
        if stripped and not stripped.startswith( ( ' ', '-' ) ):
            names.append( stripped.rstrip( '*' ) )

    return sorted( set( names ) ), None


def run_module( path, timeout ):
    try:
        completed = subprocess.run(
            [ path ] + UTF_FLAGS,
            stdout = subprocess.PIPE,
            stderr = subprocess.STDOUT,
            timeout = timeout,
            )
        return completed.stdout.decode( 'utf-8', 'replace' ), completed.returncode
    except subprocess.TimeoutExpired:
        return '', 'timeout'


def collect_by_running( bld_tree, only, timeout ):

    result = {}

    for module, path in sorted( find_binaries( bld_tree ).items() ):

        if only and module not in only:
            continue

        registered, error = list_content( path )

        if error:
            print( '    %-32s LISTING FAILED: %s' % ( module, error ), file = sys.stderr )
            continue

        text, code = run_module( path, timeout )
        parsed = parse_run( text )
        parsed[ 'registered' ] = registered
        parsed[ 'exit' ] = code

        result[ module ] = parsed

        #
        # Flushed because this is the only progress signal a long tier 3 has, and check_split.sh
        # pipes this output through tee. CPython block-buffers a pipe, so without the flush all six
        # lines of a twelve-second capture arrive together at exit - measured - while on a terminal
        # they arrive as each module finishes. A pipe and a captured variable are equally silent:
        # the flush is what makes the difference, not the way the output is collected
        #

        print( '    %-32s %4d registered  %4d ran  %4d skipped  exit=%s' % (
            module, len( registered ), len( parsed[ 'entered' ] ),
            len( parsed[ 'skipped' ] ), code ), flush = True )

    return result


def collect_by_parsing( logs_dir, only ):

    result = {}

    if not os.path.isdir( logs_dir ):
        print( 'utf_runlog: no such directory: %s' % logs_dir, file = sys.stderr )
        return result

    for entry in sorted( os.listdir( logs_dir ) ):

        if not entry.endswith( '.log' ):
            continue

        module = entry[ : -4 ]

        if only and module not in only:
            continue

        with open( os.path.join( logs_dir, entry ), 'r', encoding = 'utf-8', errors = 'replace' ) as stream:
            parsed = parse_run( stream.read() )

        result[ module ] = parsed

        print( '    %-32s %4d ran  %4d skipped  %s' % (
            module, len( parsed[ 'entered' ] ), len( parsed[ 'skipped' ] ),
            'clean' if parsed[ 'clean' ] else 'NOT CLEAN' ), flush = True )

    return result


def restrict( snapshot, prefix ):
    """
    Keep the modules of one split family: the prefix itself, plus the prefix with a numeric suffix

    A split renames modules - utf_baselib_security becomes utf_baselib_security, _security2 and
    _security3 - so a lane validating one family needs the baseline restricted to the family's
    original name on one side and to its several successors on the other. Comparing the union of
    each is then exactly the right question, and it avoids running every module to check three

    Note this deliberately does NOT match on a plain string prefix. Every module in the tree begins
    with 'utf_baselib', so a prefix match for the utf_baselib family would sweep in utf_baselib_http,
    utf_baselib_security and the rest, and then report every module which was not run as having lost
    its cases. Matching '<prefix>' or '<prefix><digits>' follows the naming the split actually uses
    """

    pattern = re.compile( r'^%s[0-9]*$' % re.escape( prefix ) )

    return { name: record for name, record in snapshot.items() if pattern.match( name ) }


def union( snapshot, key ):
    """
    Collapse a per-module snapshot into one tree-wide set, since the split moves cases between
    modules and only the union is invariant
    """

    merged = set()

    for module, record in snapshot.items():
        merged.update( record.get( key ) or [] )

    return merged


def union_cases( snapshot ):

    merged = {}

    for module, record in snapshot.items():
        for name, case in record.get( 'cases', {} ).items():
            merged[ name ] = case

    return merged


def nondeterministic( first, second ):
    """
    Names whose assertion count disagrees between two runs of the same tree
    """

    a = union_cases( first )
    b = union_cases( second )

    unstable = []

    for name in sorted( set( a ) & set( b ) ):
        if a[ name ].get( 'assertions' ) != b[ name ].get( 'assertions' ):
            unstable.append( name )

    return unstable


def compare( before, after, unstable, unmeasured = None ):

    failures = []

    unstable = set( unstable or [] )

    if unmeasured is None:
        unmeasured = []

    old_reg, new_reg = union( before, 'registered' ), union( after, 'registered' )

    #
    # Only --run mode populates the registered set, from --list_content. A snapshot parsed from
    # logs has none, and treating an absent set as an empty one would report every case in the
    # tree as lost. Compare only when both sides actually measured it
    #

    if old_reg and new_reg:
        for name in sorted( old_reg - new_reg ):
            failures.append( 'REGISTRATION LOST: %s' % name )
        for name in sorted( new_reg - old_reg ):
            failures.append( 'REGISTRATION ADDED: %s' % name )

    old_run, new_run = union( before, 'entered' ), union( after, 'entered' )

    for name in sorted( old_run - new_run ):
        failures.append( 'NO LONGER RUNS: %s' % name )
    for name in sorted( new_run - old_run ):
        failures.append( 'NEWLY RUNS: %s' % name )

    old_skip, new_skip = union( before, 'skipped' ), union( after, 'skipped' )

    for name in sorted( old_skip ^ new_skip ):
        failures.append( 'SKIPPED SET CHANGED: %s' % name )

    old_cases, new_cases = union_cases( before ), union_cases( after )

    for name in sorted( set( old_cases ) & set( new_cases ) ):

        a, b = old_cases[ name ], new_cases[ name ]

        if a.get( 'outcome' ) != b.get( 'outcome' ):
            failures.append( 'OUTCOME CHANGED: %s (%s -> %s)' % (
                name, a.get( 'outcome' ), b.get( 'outcome' ) ) )

        if name in unstable:
            continue

        #
        # Assertion counts only exist when the run carried --report_level=detailed. Logs which
        # make produced with the default UTF_FLAGS have none, and comparing a real count against
        # a missing one would report every case as changed - the sort of false alarm that gets a
        # gate switched off. Skip the pair and account for it instead
        #

        if a.get( 'assertions' ) is None or b.get( 'assertions' ) is None:
            unmeasured.append( name )
            continue

        if a.get( 'assertions' ) != b.get( 'assertions' ):
            failures.append( 'ASSERTION COUNT CHANGED: %s (%s -> %s)' % (
                name, a.get( 'assertions' ), b.get( 'assertions' ) ) )

    #
    # Everything above this point is differential, and a module the baseline never saw has nothing
    # to be differed against - its cases are absent from the intersection and so is the module. A
    # round which adds a module can therefore fail inside it in complete silence, and every layer
    # of this feature has added one. That is how a gate read clean with utf_baselib_h2client2
    # exiting 201
    #
    # So the two below - the case outcomes, and the module's own verdict - are absolute, read from
    # the after side alone. What makes that safe is that each rests on something the run actually
    # printed rather than on the absence of it; exit is the one signal which cannot, and it is
    # left differential further down
    #

    for module, record in sorted( after.items() ):

        if record.get( 'incomplete' ):
            failures.append( 'MODULE %s left %d case(s) incomplete: %s' % (
                module, len( record[ 'incomplete' ] ), ', '.join( record[ 'incomplete' ][ :5 ] ) ) )

        #
        # skipped is a verdict in its own right and a change to it is caught differentially above.
        # incomplete already has the message just above and is not reported twice here
        #

        for name in sorted( record.get( 'cases', {} ) ):
            outcome = record[ 'cases' ][ name ].get( 'outcome' )
            if outcome not in ( 'passed', 'skipped', 'incomplete' ):
                failures.append( 'CASE DID NOT PASS: %s (%s, in %s)' % ( name, outcome, module ) )

        #
        # The module's own verdict, which is the only one a confirmation-level log carries at all.
        # Once the escapes are stripped clean stops being an absence - across the 312 run logs in
        # this tree only 29 are unclean and every one of them has a real failure behind it - so it
        # can be read from the after side alone. A module which reported nothing is covered too,
        # which is what a timeout or a truncated capture looks like
        #
        # One message per module: a failure count always implies the module was not clean
        #

        if record.get( 'failures' ):
            failures.append( 'MODULE REPORTED FAILURES: %s (%s)' % ( module, record[ 'failures' ] ) )
        elif not record.get( 'clean' ):
            failures.append( 'MODULE DID NOT REPORT CLEAN: %s (and printed no failure count)'
                             % module )

    #
    # exit is the one module-wide signal which still cannot be read from a single side, because it
    # does not distinguish a bad value from a benign one. A --parse-logs snapshot has none at all,
    # and utf_baselib_jni exits 200 on some hosts having reported no errors and run no cases, so
    # only a change between the two sides means anything. The module's own verdict above is what
    # covers a new module exiting non-zero, and it does so without this ambiguity
    #

    for module in sorted( set( before ) & set( after ) ):

        old, new = before[ module ], after[ module ]

        if old.get( 'exit' ) is not None and new.get( 'exit' ) is not None:
            if old[ 'exit' ] != new[ 'exit' ]:
                failures.append( 'MODULE EXIT CHANGED: %s (%s -> %s)' % (
                    module, old[ 'exit' ], new[ 'exit' ] ) )

    return failures


#
# The platform a capture was taken on, kept beside the modules under a key which cannot collide
# with one - every module name is a directory under <tree>/utests and none of them begins with an
# underscore. Keeping it in the same object is what makes stamping an existing baseline a one-key
# edit rather than a recapture
#
# None of the four signals above is portable. The committed baseline is a win-x86-vc143-debug
# capture and against a Linux tree it reports differences inside its own modules which are nothing
# but Windows versus POSIX - BaseLib_OSJunctionsTests 18 assertions to 0, BaseLib_OSRegistryValueTest
# 8 to 0, Windows argv quoting 81 to 0. That wall of red says nothing whatever about the test tree,
# and the first reader to see it reasonably concludes the gate is broken
#

PLATFORM_KEY = '__platform__'


def platform_of_tree( bld_tree ):
    """
    The platform a build tree speaks for, which is the tree's own directory name: bld/win-x86-vc143-debug
    is win-x86-vc143-debug. That string is the platform identity this project already uses everywhere
    """

    if not bld_tree:
        return None

    return os.path.basename( os.path.normpath( bld_tree ) ) or None


def load_snapshot( path ):
    """
    Read a capture as ( modules, platform ), so every other function here sees modules alone
    """

    with open( path ) as stream:
        raw = json.load( stream )

    modules = { name: record for name, record in raw.items() if name != PLATFORM_KEY }

    return modules, raw.get( PLATFORM_KEY )


def stamp( snapshot, platform ):
    """
    The capture as it is written out: the modules, plus the platform when one is known
    """

    if not platform:
        return snapshot

    stamped = dict( snapshot )
    stamped[ PLATFORM_KEY ] = platform

    return stamped


#
# An unstable list is an array of case names, so unlike a capture it has nowhere to put a key, and a
# stamped one is an object holding both: { "__platform__": ..., "unstable": [ ... ] }. The same key
# and the same string as a capture's, so there is one convention to learn rather than two
#
# An array is still read, and an array is exactly what an unstamped list looks like - every list
# derived before this existed is one. The shape says which it is, so nothing has to be inferred from
# a missing key that might equally be a typo
#
# A third key, "observed", holds the names that were never DERIVED at all, each mapped to where it
# was seen. Ten of the eleven names this repo carries re-derive from the two committed baseline
# passes; one does not, because it never was derived from them.
# IO_SimpleConnectAndTransmitDataMessageDispatcherOutgoingTests was added by hand at 77ef537, whose
# message records that re-running the unchanged binary twice gave 8199 and then 8194 - so the case
# simply varies and the two baseline passes were unlucky in agreeing. Written back into "unstable",
# a name like that survives exactly until the next --nondeterministic recomputes that array and
# drops it, silently, after which the case is compared against a count it does not hold
#
# So the two halves are kept apart by provenance and joined only on the way out. "unstable" is what
# a derivation writes and may freely overwrite; "observed" is hand-maintained and a derivation
# carries it through untouched; load_unstable( ) returns the union, deduplicated, so a name written
# into both is one name. A list with no "observed" key behaves exactly as it did before this
# existed, and its value is read for its names alone, exactly as "unstable" is
#

UNSTABLE_KEY = 'unstable'
OBSERVED_KEY = 'observed'


def load_unstable( path ):
    """
    Read an unstable list as ( names, platform ), the bare array it used to be reading as unstamped

    The names are the union of the derived half and the observed half. A refresh that recomputes
    "unstable" alone therefore cannot drop an observed name, which is the whole point of the split
    """

    with open( path ) as stream:
        raw = json.load( stream )

    if isinstance( raw, list ):
        return raw, None

    derived = raw.get( UNSTABLE_KEY ) or []
    observed = raw.get( OBSERVED_KEY ) or {}

    return sorted( set( derived ) | set( observed ) ), raw.get( PLATFORM_KEY )


def load_observed( path ):
    """
    The observed half alone, name -> where it was seen, so that a derivation can carry it through

    A bare legacy array has none, and a file which is not there has none - which is also the right
    answer for a derivation writing to a path that does not hold the existing list yet
    """

    if not path or not os.path.isfile( path ):
        return {}

    with open( path ) as stream:
        raw = json.load( stream )

    if isinstance( raw, list ):
        return {}

    return raw.get( OBSERVED_KEY ) or {}


def stamp_unstable( names, platform, observed = None ):
    """
    The list as it is written out. There is no unstamped case to write here, unlike a capture: the
    two runs a list is derived from must already agree on a platform or the derivation is refused,
    and that platform - never --bld - is what the list speaks for

    The observed half is written back exactly as it was read, because a derivation can say nothing
    whatever about it: a name is in it precisely because two passes do not produce it. An empty one
    writes no key at all, so a list that never had one does not grow one
    """

    written = { PLATFORM_KEY: platform, UNSTABLE_KEY: names }

    if observed:
        written[ OBSERVED_KEY ] = observed

    return written


def platform_refusal( baseline_platform, platform, subject = 'baseline' ):
    """
    Why a comparison must not be attempted, or None when the two sides are known to agree

    Three states, not two. A stamp which disagrees is a mismatch; a stamp absent on either side is
    an unknown, and an unknown is not a match. Every capture taken before this existed is unstamped,
    and the one this repo carries was taken on win-x86-vc143-debug, so treating unstamped as matching
    would wave through exactly the comparison this exists to refuse

    subject names the side the first stamp came from, because the unstable list is asked the same
    question against the same tree. One predicate, so the unknown case cannot be decided one way for
    the baseline and another for the list
    """

    if baseline_platform and platform and baseline_platform == platform:
        return None

    if baseline_platform is None:
        return ( 'the %s carries no platform stamp, so there is nothing to match this tree '
                 'against - not in force until the %s is refreshed' % ( subject, subject ) )

    if platform is None:
        return ( 'the %s speaks for %s and this side carries no platform stamp'
                 % ( subject, baseline_platform ) )

    return 'the %s speaks for %s and this tree is %s' % ( subject, baseline_platform, platform )


def unstable_refusal( path, platform ):
    """
    Why a comparison must not consult this unstable list, or None when it may

    No list at all is not a refusal: excusing nothing weakens nothing, and a --nondet naming a file
    which does not exist is what --compare has always treated as no list. The risk is a list which
    IS applied and was derived somewhere else
    """

    if not path or not os.path.isfile( path ):
        return None

    return platform_refusal( load_unstable( path )[ 1 ], platform, 'unstable list' )


#
# Why the comparison did not happen and how to get one, which differs by what was refused even
# though the line above it does not
#

BASELINE_RECOVERY = (
    'nothing was compared. None of the four signals is portable, so a',
    'comparison across platforms reports platform difference as regression',
    '- capture a baseline on this platform, or run on the one the baseline',
    'speaks for',
    )

UNSTABLE_RECOVERY = (
    'nothing was compared. The unstable list excuses the cases it names from',
    'the assertion comparison, so a list derived on another platform excuses',
    'the wrong ones - silently, and for as long as it stays committed. Derive',
    'one here with --nondeterministic over two captures of this tree',
    )


def print_refusal( refusal, recovery ):
    """
    One wording for the refusal, wherever it is reached from, so the two points cannot drift apart

    check_split.sh reads the REFUSED line back to build its summary note, so its shape is load
    bearing: the reason is everything after the dash, on one line. It cannot tell a refused baseline
    from a refused list and should not - both mean the comparison did not happen - so only the lines
    below the REFUSED line vary
    """

    print( '' )
    print( 'utf_runlog: REFUSED - %s' % refusal )

    for line in recovery:
        print( 'utf_runlog: %s' % line )


def repo_root():
    return os.path.normpath( os.path.join( os.path.dirname( os.path.abspath( __file__ ) ), '..', '..' ) )


def main():

    parser = argparse.ArgumentParser( description = 'capture and compare unit-test run behaviour' )

    parser.add_argument( '--run', action = 'store_true', help = 'run every built test binary directly' )
    parser.add_argument( '--bld', help = 'the build tree to run binaries from, e.g. bld/win-x86-vc143-debug; '
                                         'its name is the platform the capture is stamped with, and is '
                                         'worth passing alongside --parse-logs for that reason alone' )
    parser.add_argument( '--parse-logs', metavar = 'DIR', help = 'parse the utflogs directory make produced' )
    parser.add_argument( '--only', nargs = '*', help = 'restrict to these modules' )
    parser.add_argument( '--timeout', type = int, default = 1800, help = 'per-module timeout in seconds' )
    parser.add_argument( '--capture', metavar = 'PATH', help = 'write the snapshot as JSON to PATH' )
    parser.add_argument( '--compare', metavar = 'PATH', help = 'the before snapshot' )
    parser.add_argument( '--against', metavar = 'PATH', help = 'the after snapshot (default: the tree)' )
    parser.add_argument( '--nondet', metavar = 'PATH', help = 'names to compare on outcome only; it '
                                                              'carries the platform it was derived on '
                                                              'and one from elsewhere is refused' )
    parser.add_argument( '--uncovered', metavar = 'PATH',
                         help = 'module -> reason for the modules the baseline deliberately does not '
                                'cover, printed with the coverage statement so a PASS cannot be read '
                                'as covering them' )
    parser.add_argument( '--family', metavar = 'PREFIX',
                         help = 'restrict both sides of the comparison to modules with this name prefix, '
                                'so one split family can be validated without running the whole tree' )
    parser.add_argument( '--nondeterministic', nargs = 2, metavar = ( 'RUN1', 'RUN2' ),
                         help = 'derive the unstable-assertion list from two baseline runs' )

    args = parser.parse_args()

    if args.nondeterministic:

        first, first_platform = load_snapshot( args.nondeterministic[ 0 ] )
        second, second_platform = load_snapshot( args.nondeterministic[ 1 ] )

        #
        # Two runs of two platforms is the same comparison refused below, and worse in its effect:
        # the assertion counts differ for the platform's reasons, so what falls out is not a list of
        # unstable cases but the platform difference itself - and every name on it is thereafter
        # excused from the assertion comparison in every run that passes --nondet. The same predicate
        # decides it, so the two cannot drift apart
        #

        if platform_refusal( first_platform, second_platform ):
            print( '' )
            print( 'utf_runlog: REFUSED - run 1 is %s and run 2 is %s; an unstable list is only '
                   'meaningful within one platform' % (
                       first_platform or 'unstamped', second_platform or 'unstamped' ) )
            return 3

        unstable = nondeterministic( first, second )

        print( 'utf_runlog: %d case(s) have unstable assertion counts' % len( unstable ) )
        for name in unstable:
            print( '    %s' % name )

        #
        # The observed half is read back from the file about to be overwritten and carried through,
        # because a derivation writing only what it derived is exactly how one of those names is
        # lost. Named line by line rather than counted, and printed even when it is empty, so that a
        # derivation into a path which does NOT hold the existing list says so here rather than
        # after the result has been copied over one that does
        #

        observed = load_observed( args.capture )

        print( 'utf_runlog: %d observed case(s) carried through from %s' % (
            len( observed ), args.capture or '<no --capture>' ) )
        for name in sorted( observed ):
            print( '    %s  -  %s' % ( name, observed[ name ] ) )

        #
        # Stamped with the platform the two runs agree on, which the refusal just above is what
        # makes safe to assume. That string is the tree name a capture is stamped with, so the list
        # and the baseline it applies to speak the same language and can be compared directly
        #

        if args.capture:
            with open( args.capture, 'w' ) as stream:
                json.dump( stamp_unstable( unstable, first_platform, observed ), stream,
                           indent = 1, sort_keys = True )
                stream.write( '\n' )
            print( 'utf_runlog: wrote %s, stamped %s' % ( args.capture, first_platform ) )

        return 0

    snapshot = None

    #
    # A tree names its own platform, and a capture read back from a file carries whatever platform
    # it was stamped with. The file's own stamp wins over --bld deliberately: an unstamped capture
    # cannot be laundered into a stamped one by naming a tree on the command line
    #

    platform = platform_of_tree( args.bld )

    #
    # Refused before a single binary is executed, whenever going on would produce nothing worth
    # having. Both inputs are known here: the tree names its platform and the baseline carries its
    # own. Refusing after the run is right only when --capture was asked for, because then the run
    # still yields the capture this platform needs in order to get a baseline of its own - and
    # check_split.sh's --run branch passes no --capture, so there every module would run to its
    # 1800s timeout, nothing would be kept, and the operator would be told to run it all again
    #

    if args.compare and not args.capture and ( args.run or args.parse_logs ):

        refusal = platform_refusal( load_snapshot( args.compare )[ 1 ], platform )

        if refusal:
            print_refusal( refusal, BASELINE_RECOVERY )
            return 3

        refusal = unstable_refusal( args.nondet, platform )

        if refusal:
            print_refusal( refusal, UNSTABLE_RECOVERY )
            return 3

    if args.run:
        if not args.bld:
            print( 'utf_runlog: --run needs --bld', file = sys.stderr )
            return 2
        #
        # Flushed for the same reason the per-module line is, and for the case that one cannot
        # reach: nothing below prints until the first module finishes, so a module which hangs on
        # a 600 or 1800 second timeout leaves a blank screen for the whole of it. That is the one
        # moment somebody is watching, deciding whether to kill the run
        #

        print( 'utf_runlog: running binaries under %s' % args.bld, flush = True )
        snapshot = collect_by_running( args.bld, set( args.only or [] ), args.timeout )

    elif args.parse_logs:
        print( 'utf_runlog: parsing %s' % args.parse_logs )
        snapshot = collect_by_parsing( args.parse_logs, set( args.only or [] ) )

    elif args.against:
        snapshot, platform = load_snapshot( args.against )

    if snapshot is None:
        print( 'utf_runlog: nothing to do - pass --run, --parse-logs or --against', file = sys.stderr )
        return 2

    if args.capture and not args.nondeterministic:
        with open( args.capture, 'w' ) as stream:
            json.dump( stamp( snapshot, platform ), stream, indent = 1, sort_keys = True )
            stream.write( '\n' )
        print( 'utf_runlog: wrote %s%s' % (
            args.capture, '' if platform else ' (no --bld given, so it carries no platform stamp)' ) )

    print( '' )
    print( 'utf_runlog: %d module(s), %d case(s) ran, %d registered' % (
        len( snapshot ), len( union( snapshot, 'entered' ) ), len( union( snapshot, 'registered' ) ) ) )

    if args.compare:

        before, baseline_platform = load_snapshot( args.compare )

        #
        # Refused before anything is compared, and reported as neither a PASS nor a FAIL: exit 3,
        # which check_split.sh renders as a SKIP naming this reason. A FAIL here would red the gate
        # for everyone who runs it on a platform the baseline was not captured on, and a PASS would
        # claim a check that never ran
        #

        refusal = platform_refusal( baseline_platform, platform )

        if refusal:
            print_refusal( refusal, BASELINE_RECOVERY )
            return 3

        #
        # And the same question of the list, which the check above is what makes answerable: past it
        # the baseline and this side agree, so a list which matches this side matches the comparison
        # it is about to inform. It is the last unchecked input to that comparison
        #

        refusal = unstable_refusal( args.nondet, platform )

        if refusal:
            print_refusal( refusal, UNSTABLE_RECOVERY )
            return 3

        if args.family:
            before = restrict( before, args.family )
            snapshot = restrict( snapshot, args.family )
            print( '' )
            print( 'utf_runlog: restricted to family %s* - %d baseline module(s), %d now' % (
                args.family, len( before ), len( snapshot ) ) )

        unstable = []

        if args.nondet and os.path.isfile( args.nondet ):
            unstable = load_unstable( args.nondet )[ 0 ]

        unmeasured = []

        failures = compare( before, snapshot, unstable, unmeasured )

        if unmeasured:
            print( '' )
            print( 'utf_runlog: NOTE - %d case(s) compared without assertion counts' % len( unmeasured ) )
            print( 'utf_runlog: one side lacked --report_level=detailed, so only the outcome was checked' )

        #
        # A green report says "nothing changed in what the baseline covers", which is a narrower
        # claim than it reads as. Every check but the two absolute ones is differential, so a module
        # the baseline never captured contributes no registered set, no executed set, no skip set and
        # no assertion count - the signal tier 3 exists for. Only its own verdict is read, and a case
        # in it which still registers and still passes while asserting less is invisible
        #
        # Printed on PASS as well as on FAIL, because it is the PASS which misleads. Measured: with
        # one assertion removed from a case in an uncovered module the report is byte-identical, and
        # from a covered one it grows the expected ASSERTION COUNT CHANGED line
        #

        uncovered = sorted( set( snapshot ) - set( before ) )

        reasons = {}

        if args.uncovered and os.path.isfile( args.uncovered ):
            with open( args.uncovered ) as stream:
                reasons = json.load( stream )

        if uncovered:
            print( '' )
            print( 'utf_runlog: COVERAGE - the baseline does not cover %d of the %d module(s) here:'
                   % ( len( uncovered ), len( snapshot ) ) )
            for module in uncovered:
                print( '    %-30s %4d case(s)  %s' % (
                    module,
                    len( snapshot[ module ].get( 'entered' ) or [] ),
                    reasons.get( module, 'not in the baseline' ) ) )
            print( 'utf_runlog: for those the registered set, the executed set, the skips and the' )
            print( 'utf_runlog: per-case assertion counts were NOT compared - only each case outcome' )
            print( 'utf_runlog: and the module verdict were read' )

        #
        # A reason which no longer describes anything is worse than none, because it is read as a
        # live statement about the tree. Say so rather than letting it rot silently
        #

        stale = sorted( set( reasons ) - set( uncovered ) )

        if stale:
            print( '' )
            print( 'utf_runlog: NOTE - %d exclusion note(s) are stale; these modules ARE covered now:'
                   % len( stale ) )
            for module in stale:
                print( '    %s' % module )

        if failures:
            print( '' )
            print( 'utf_runlog: FAIL - %d difference(s):' % len( failures ) )
            for failure in failures:
                print( '    %s' % failure )
            return 1

        print( 'utf_runlog: PASS - runtime behaviour is unchanged' )

    return 0


if __name__ == '__main__':
    sys.exit( main() )
