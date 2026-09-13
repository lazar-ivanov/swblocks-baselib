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
# Three signals are compared, all normalized so that a case legitimately moving to another file
# or line does not register as a difference:
#
#   registered   the case list from <binary> --list_content, which exits without running anything
#   executed     the Entering/Leaving pairs and the SKIPPED CASES block from the run log
#   assertions   the per-case counts from --report_level=detailed
#
# Assertion counts are not deterministic for every case - retry loops and perf cases vary - so a
# baseline is captured twice and any case that disagrees with itself is recorded and thereafter
# compared on pass/fail only. Without that the gate produces false alarms and gets ignored
#
# Usage:
#
#   utf_runlog.py --run --bld <tree> --capture run1.json       run every binary and capture
#   utf_runlog.py --parse-logs <utflogs dir> --capture x.json  parse logs make already produced
#   utf_runlog.py --nondeterministic run1.json run2.json --capture nondet.json
#   utf_runlog.py --compare before.json --against after.json [--nondet nondet.json]
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

REPORT_CASE_RE = re.compile( r'^\s*Test case "([^"]+)" has (passed|failed|aborted)(\s+with:)?\s*$' )
REPORT_ASSERT_RE = re.compile( r'^\s*(\d+) assertions? out of (\d+) passed' )
REPORT_MODULE_RE = re.compile( r'^\s*Test module "([^"]+)" has (passed|failed|aborted)' )

#
# Note this mid-run line carries the case name unquoted, unlike every line in the final report
#

REPORT_NO_ASSERT_RE = re.compile( r'^\s*Test case ([A-Za-z0-9_]+) did not check any assertions' )

SKIPPED_HEADER_RE = re.compile( r'^SKIPPED CASES \((\d+)\):' )

NO_ERRORS_RE = re.compile( r'^\*\*\* No errors detected' )
FAILURE_RE = re.compile( r'^\*\*\* (\d+) failure' )

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

    in_skipped = False
    pending = None

    for line in text.split( '\n' ):

        stripped = line.rstrip()

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

    for name in entered:
        cases.setdefault( name, {} )
        cases[ name ].setdefault( 'outcome', 'passed' if name in left else 'incomplete' )
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

        print( '    %-32s %4d registered  %4d ran  %4d skipped  exit=%s' % (
            module, len( registered ), len( parsed[ 'entered' ] ),
            len( parsed[ 'skipped' ] ), code ) )

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
            'clean' if parsed[ 'clean' ] else 'NOT CLEAN' ) )

    return result


def restrict( snapshot, prefix ):
    """
    Keep only the modules whose name starts with prefix

    A split renames modules - utf_baselib_security becomes utf_baselib_security, _security2 and
    _security3 - so a lane validating one family needs the baseline restricted to the family's
    prefix on one side and to its several successors on the other. Comparing the union of each is
    then exactly the right question, and it avoids running all seventeen modules to check three
    """

    return { name: record for name, record in snapshot.items() if name.startswith( prefix ) }


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

    for module, record in sorted( after.items() ):
        if record.get( 'incomplete' ):
            failures.append( 'MODULE %s left %d case(s) incomplete: %s' % (
                module, len( record[ 'incomplete' ] ), ', '.join( record[ 'incomplete' ][ :5 ] ) ) )

    return failures


def repo_root():
    return os.path.normpath( os.path.join( os.path.dirname( os.path.abspath( __file__ ) ), '..', '..' ) )


def main():

    parser = argparse.ArgumentParser( description = 'capture and compare unit-test run behaviour' )

    parser.add_argument( '--run', action = 'store_true', help = 'run every built test binary directly' )
    parser.add_argument( '--bld', help = 'the build tree to run binaries from, e.g. bld/win-x86-vc143-debug' )
    parser.add_argument( '--parse-logs', metavar = 'DIR', help = 'parse the utflogs directory make produced' )
    parser.add_argument( '--only', nargs = '*', help = 'restrict to these modules' )
    parser.add_argument( '--timeout', type = int, default = 1800, help = 'per-module timeout in seconds' )
    parser.add_argument( '--capture', metavar = 'PATH', help = 'write the snapshot as JSON to PATH' )
    parser.add_argument( '--compare', metavar = 'PATH', help = 'the before snapshot' )
    parser.add_argument( '--against', metavar = 'PATH', help = 'the after snapshot (default: the tree)' )
    parser.add_argument( '--nondet', metavar = 'PATH', help = 'names to compare on outcome only' )
    parser.add_argument( '--family', metavar = 'PREFIX',
                         help = 'restrict both sides of the comparison to modules with this name prefix, '
                                'so one split family can be validated without running the whole tree' )
    parser.add_argument( '--nondeterministic', nargs = 2, metavar = ( 'RUN1', 'RUN2' ),
                         help = 'derive the unstable-assertion list from two baseline runs' )

    args = parser.parse_args()

    if args.nondeterministic:

        with open( args.nondeterministic[ 0 ] ) as stream:
            first = json.load( stream )
        with open( args.nondeterministic[ 1 ] ) as stream:
            second = json.load( stream )

        unstable = nondeterministic( first, second )

        print( 'utf_runlog: %d case(s) have unstable assertion counts' % len( unstable ) )
        for name in unstable:
            print( '    %s' % name )

        if args.capture:
            with open( args.capture, 'w' ) as stream:
                json.dump( unstable, stream, indent = 1 )
                stream.write( '\n' )
            print( 'utf_runlog: wrote %s' % args.capture )

        return 0

    snapshot = None

    if args.run:
        if not args.bld:
            print( 'utf_runlog: --run needs --bld', file = sys.stderr )
            return 2
        print( 'utf_runlog: running binaries under %s' % args.bld )
        snapshot = collect_by_running( args.bld, set( args.only or [] ), args.timeout )

    elif args.parse_logs:
        print( 'utf_runlog: parsing %s' % args.parse_logs )
        snapshot = collect_by_parsing( args.parse_logs, set( args.only or [] ) )

    elif args.against:
        with open( args.against ) as stream:
            snapshot = json.load( stream )

    if snapshot is None:
        print( 'utf_runlog: nothing to do - pass --run, --parse-logs or --against', file = sys.stderr )
        return 2

    if args.capture and not args.nondeterministic:
        with open( args.capture, 'w' ) as stream:
            json.dump( snapshot, stream, indent = 1, sort_keys = True )
            stream.write( '\n' )
        print( 'utf_runlog: wrote %s' % args.capture )

    print( '' )
    print( 'utf_runlog: %d module(s), %d case(s) ran, %d registered' % (
        len( snapshot ), len( union( snapshot, 'entered' ) ), len( union( snapshot, 'registered' ) ) ) )

    if args.compare:

        with open( args.compare ) as stream:
            before = json.load( stream )

        if args.family:
            before = restrict( before, args.family )
            snapshot = restrict( snapshot, args.family )
            print( '' )
            print( 'utf_runlog: restricted to family %s* - %d baseline module(s), %d now' % (
                args.family, len( before ), len( snapshot ) ) )

        unstable = []

        if args.nondet and os.path.isfile( args.nondet ):
            with open( args.nondet ) as stream:
                unstable = json.load( stream )

        unmeasured = []

        failures = compare( before, snapshot, unstable, unmeasured )

        if unmeasured:
            print( '' )
            print( 'utf_runlog: NOTE - %d case(s) compared without assertion counts' % len( unmeasured ) )
            print( 'utf_runlog: one side lacked --report_level=detailed, so only the outcome was checked' )

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
