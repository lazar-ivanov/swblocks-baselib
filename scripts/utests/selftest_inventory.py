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
# selftest_inventory.py - prove that utf_inventory.py actually fails when it should
#
# A verification gate nobody has seen fail is not a gate. The module split relies on
# utf_inventory.py to catch a silently dropped or edited test case, so before that reliance is
# placed on it, each of its seven invariants is shown to fire on a manifest corrupted in exactly
# the way that invariant exists to catch
#
# This mutates in-memory copies of a captured manifest. It never touches the source tree
#
# Usage:  selftest_inventory.py <baseline manifest>
#

from __future__ import print_function

import copy
import json
import os
import sys

sys.path.insert( 0, os.path.dirname( os.path.abspath( __file__ ) ) )

from utf_inventory import check_intrinsic, check_against


def expect( label, failures, marker ):
    """
    Assert that at least one reported failure carries the given invariant marker
    """

    hit = [ failure for failure in failures if failure.startswith( marker ) ]

    if hit:
        print( '    PASS  %-4s %-46s %s' % ( marker, label, hit[ 0 ][ : 76 ] ) )
        return True

    print( '    FAIL  %-4s %-46s (no %s violation reported)' % ( marker, label, marker ) )
    return False


def main():

    if len( sys.argv ) != 2:
        print( 'usage: selftest_inventory.py <baseline manifest>', file = sys.stderr )
        return 2

    with open( sys.argv[ 1 ] ) as stream:
        baseline = json.load( stream )

    print( 'selftest_inventory: baseline has %d cases, %d helper blocks, %d modules' % (
        len( baseline[ 'cases' ] ), len( baseline[ 'namespaces' ] ), len( baseline[ 'modules' ] ) ) )
    print( '' )

    ok = True

    #
    # The unmutated baseline must be clean both ways, or every negative result below is suspect
    #
    # C8 is excluded, and only C8. It was added in cc491ff, after the baseline was captured at
    # 36ec522, and that same commit fixed the eight stale --run_test recipes it found - recipes
    # naming cases deleted or migrated years earlier. So the baseline tree really does violate
    # C8; the current tree does not. Excluding it here keeps the precondition honest rather than
    # papering over it, and C8 is intrinsic to whichever tree is scanned, so the baseline's copy
    # of it is never consulted by a real run
    #

    clean = [ failure for failure in check_intrinsic( baseline ) if not failure.startswith( 'C8' ) ]
    clean += check_against( baseline, baseline )

    if clean:
        print( '    FAIL  ----  unmutated baseline is not clean' )
        for failure in clean:
            print( '                %s' % failure )
        return 1

    print( '    PASS  ----  unmutated baseline is clean both ways' )

    # C1 - a dropped case
    mutated = copy.deepcopy( baseline )
    dropped = mutated[ 'cases' ].pop( 17 )
    ok &= expect( 'case dropped (%s)' % dropped[ 'name' ], check_against( baseline, mutated ), 'C1' )

    # C1 - an added case
    mutated = copy.deepcopy( baseline )
    extra = copy.deepcopy( mutated[ 'cases' ][ 3 ] )
    extra[ 'name' ] += '_Invented'
    mutated[ 'cases' ].append( extra )
    ok &= expect( 'case invented', check_against( baseline, mutated ), 'C1' )

    # C2 - an edited body
    mutated = copy.deepcopy( baseline )
    mutated[ 'cases' ][ 5 ][ 'body_sha' ] = '0' * 32
    ok &= expect( 'case body edited (%s)' % mutated[ 'cases' ][ 5 ][ 'name' ],
                  check_against( baseline, mutated ), 'C2' )

    # C2 - a dropped doc comment
    mutated = copy.deepcopy( baseline )
    documented = next( case for case in mutated[ 'cases' ] if case[ 'doc_sha' ] )
    documented[ 'doc_sha' ] = None
    ok &= expect( 'doc comment dropped (%s)' % documented[ 'name' ],
                  check_against( baseline, mutated ), 'C2' )

    # C3 - a case moved under a platform guard
    mutated = copy.deepcopy( baseline )
    mutated[ 'cases' ][ 7 ][ 'guards' ] = [ 'if defined( _WIN32 )' ]
    ok &= expect( 'case moved under a guard (%s)' % mutated[ 'cases' ][ 7 ][ 'name' ],
                  check_against( baseline, mutated ), 'C3' )

    # C4 - a case moved into an anonymous namespace
    mutated = copy.deepcopy( baseline )
    mutated[ 'cases' ][ 9 ][ 'namespaces' ] = [ '<anonymous>' ]
    ok &= expect( 'case moved into a namespace (%s)' % mutated[ 'cases' ][ 9 ][ 'name' ],
                  check_against( baseline, mutated ), 'C4' )

    # C5 - the same case name in two modules
    mutated = copy.deepcopy( baseline )
    clone = copy.deepcopy( mutated[ 'cases' ][ 11 ] )
    clone[ 'file' ] = 'utf_baselib_elsewhere/TestClone.h'
    mutated[ 'cases' ].append( clone )
    ok &= expect( 'case name duplicated (%s)' % clone[ 'name' ], check_intrinsic( mutated ), 'C5' )

    # C6 - a helper block copied into a second header of the same module
    mutated = copy.deepcopy( baseline )
    block = copy.deepcopy( mutated[ 'namespaces' ][ 0 ] )
    block[ 'file' ] = block[ 'file' ] + '.copy'
    mutated[ 'namespaces' ].append( block )
    ok &= expect( 'helper duplicated within a module', check_intrinsic( mutated ), 'C6' )

    # C6 - a helper member lost entirely
    mutated = copy.deepcopy( baseline )
    lost = mutated[ 'members' ].pop( 0 )
    ok &= expect( 'helper member lost (%s:%d)' % ( lost[ 'file' ], lost[ 'line' ] ),
                  check_against( baseline, mutated ), 'C6' )

    #
    # C6 - a helper block partitioned, which must NOT be reported as a loss
    #
    # This is the case the whole-block check could not express, and the reason the no-loss half
    # of C6 moved down to members. Splitting a block in two keeps every member, so nothing is
    # lost; deleting one of the halves is the previous test and still fires
    #
    mutated = copy.deepcopy( baseline )
    victim = mutated[ 'namespaces' ][ 0 ]
    moved = [ m for m in mutated[ 'members' ]
              if m[ 'file' ] == victim[ 'file' ] and m[ 'module' ] == victim[ 'module' ] ][ : 2 ]

    if len( moved ) == 2:
        mutated[ 'namespaces' ].pop( 0 )
        mutated[ 'namespaces' ].append( dict( victim, sha = 'a' * 32, line = 9001 ) )
        mutated[ 'namespaces' ].append( dict( victim, sha = 'b' * 32, line = 9100 ) )
        for member in moved:
            member[ 'file' ] = victim[ 'file' ].replace( '.h', 'Split.h' )
            member[ 'line' ] += 9000

        residue = check_against( baseline, mutated )
        if any( failure.startswith( 'C6' ) for failure in residue ):
            print( '    FAIL  C6   block partitioned                             '
                   '(reported as a loss - the per-member check is not working)' )
            ok = False
        else:
            print( '    PASS  C6   block partitioned                             '
                   'correctly read as a move, not a loss' )

    # C6 - the same helper member copied into a second header of the same module
    mutated = copy.deepcopy( baseline )
    clone = copy.deepcopy( mutated[ 'members' ][ 0 ] )
    clone[ 'file' ] = clone[ 'file' ].replace( '.h', 'Copy.h' )
    mutated[ 'members' ].append( clone )
    ok &= expect( 'helper member duplicated within a module',
                  check_intrinsic( mutated ), 'C6' )

    # C7 - a data file a module still references was left behind
    mutated = copy.deepcopy( baseline )
    module = next( name for name, info in mutated[ 'modules' ].items() if info[ 'data_refs' ] )
    mutated[ 'modules' ][ module ][ 'data_files' ] = {}
    ok &= expect( 'data file missing (%s)' % module, check_intrinsic( mutated ), 'C7' )

    # C8 - a notes.txt recipe naming a case which moved to another module
    mutated = copy.deepcopy( baseline )
    source = next( name for name, info in mutated[ 'modules' ].items() if info.get( 'notes_cases' ) )
    target = next( name for name in mutated[ 'modules' ] if name != source )
    migrated = mutated[ 'modules' ][ source ][ 'notes_cases' ][ 0 ]
    mutated[ 'modules' ][ target ].setdefault( 'notes_cases', [] ).append( migrated )
    ok &= expect( 'notes.txt names a case in another module (%s)' % migrated,
                  check_intrinsic( mutated ), 'C8' )

    # C8 - a notes.txt recipe naming a case which exists nowhere
    mutated = copy.deepcopy( baseline )
    source = next( name for name, info in mutated[ 'modules' ].items() if info.get( 'notes_cases' ) )
    mutated[ 'modules' ][ source ][ 'notes_cases' ].append( 'ACaseWhichWasDeletedYearsAgo' )
    ok &= expect( 'notes.txt names a deleted case', check_intrinsic( mutated ), 'C8' )

    # C7 - the same data file name diverging between two modules
    mutated = copy.deepcopy( baseline )
    source = next( name for name, info in mutated[ 'modules' ].items() if info[ 'data_files' ] )
    target = next( name for name in mutated[ 'modules' ] if name != source )
    name, _ = sorted( mutated[ 'modules' ][ source ][ 'data_files' ].items() )[ 0 ]
    mutated[ 'modules' ][ target ][ 'data_files' ][ name ] = 'f' * 32
    ok &= expect( 'data file diverged (%s)' % name, check_intrinsic( mutated ), 'C7' )

    print( '' )

    if ok:
        print( 'selftest_inventory: PASS - every invariant fires on the corruption it exists to catch' )
        return 0

    print( 'selftest_inventory: FAIL - at least one invariant did not fire' )
    return 1


if __name__ == '__main__':
    sys.exit( main() )
