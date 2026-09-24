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
# placed on it, each of its ten invariants is shown to fire on a manifest corrupted in exactly
# the way that invariant exists to catch
#
# Several of them are also shown NOT to fire on the legitimate state they must stay silent about,
# because a rule which reds on ordinary work is worse than the blind spot it closes
#
# C9 is also shown NOT to fire on a module which never claimed a complete notes.txt index, because
# a rule that fired on the 481 cases in this tree with no recipe would be worse than the blind spot
# it closes - a gate nobody can keep green is a gate everybody learns to ignore
#
# This mutates in-memory copies of a captured manifest. It never touches the source tree
#
# Usage:  selftest_inventory.py <baseline manifest>
#

from __future__ import print_function

import ast
import copy
import json
import os
import re
import sys

sys.path.insert( 0, os.path.dirname( os.path.abspath( __file__ ) ) )

from utf_inventory import check_intrinsic, check_against


MARKER_RE = re.compile( r'^(C\d+)(.?)' )

#
# ast.parse( ) yields Constant from 3.8 and Str before it, and the devenv7 dist interpreter is
# whatever it is. Resolved here, behind the version test, because merely LOOKING UP ast.Str on
# 3.12 and later emits a DeprecationWarning on stderr of every run
#

LEGACY_STR = getattr( ast, 'Str', None ) if sys.version_info < ( 3, 8 ) else None


def spaced():
    """
    Every failure string in utf_inventory.py, and whether its marker is followed by a space

    Returns ( total, [ ( line, text ) ... ] ) for the ones that are not. Parsed from the source
    beside this file, which is the tool under test whether that is this branch's or an older one
    """

    path = os.path.join( os.path.dirname( os.path.abspath( __file__ ) ), 'utf_inventory.py' )

    with open( path, 'r', encoding = 'utf-8-sig', errors = 'replace' ) as stream:
        tree = ast.parse( stream.read() )

    total, bad = 0, []

    for node in ast.walk( tree ):

        if isinstance( node, ast.Constant ):
            value = node.value
        elif LEGACY_STR is not None and isinstance( node, LEGACY_STR ):
            value = node.s
        else:
            continue

        if not isinstance( value, str ):
            continue

        matched = MARKER_RE.match( value )

        if not matched:
            continue

        total += 1

        if matched.group( 2 ) != ' ':
            bad.append( ( node.lineno, value[ : 60 ] ) )

    return total, bad


def unreferenced( info ):
    """
    The data files one module carries which nothing in that module names

    Computed here rather than imported from utf_inventory, for two reasons. A control which
    inherits the definition it is checking cannot contradict it; and this file has to run
    against an older tool which has no such function, or the negative half of every proof
    below - the same corruption going unreported - cannot be produced at all
    """

    named = set( info.get( 'data_refs', [] ) ) | set( info.get( 'data_literals', [] ) )

    return { name for name in info.get( 'data_files', {} ) if name not in named }


def expect( label, failures, marker ):
    """
    Assert that at least one reported failure carries the given invariant marker

    The marker is matched with its trailing space, because a bare prefix does not separate C1
    from C10 and C11: an expectation written for C1 would be satisfied by either of them, and a
    control that can be satisfied by the wrong invariant proves nothing about the right one

    The control for that is a discrimination, not a run of the harness. The bare-prefix matcher
    answers True for the marker C1 on 'C10 file INCLUDES CHANGED: ...' and on 'C11 file-scope
    text LOST: ...'; this one answers False on both and True on 'C1 case LOST: ...'. Disabling C1
    and watching both C1 expectations go red measures something else - it says the bug was masking
    nothing today, because no mutation here happens to disturb includes or file scope as well, and
    that is a claim about latency rather than about the fix

    The stricter form is only safe if every failure string the tool emits really does carry the
    marker followed by a space, and spaced( ) below asserts that by parsing the source rather than
    by anyone reading it - because the way this defect comes back is one new failure string
    written without the space, which would then match nothing and go silently unproven
    """

    hit = [ failure for failure in failures if failure.startswith( marker + ' ' ) ]

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
    # C9 needs no such exclusion and must not be given one. A baseline predating notes_index
    # declares nothing, so C9's completeness half has nothing to say about it, and a refreshed one
    # satisfies that half already - the fifteen declared modules are complete today. If this line
    # ever reds on C9, a declared index really has lost a recipe and the right answer is to put it
    # back, not to widen the exclusion
    #

    clean = [ failure for failure in check_intrinsic( baseline ) if not failure.startswith( 'C8' ) ]
    clean += check_against( baseline, baseline )

    if clean:
        print( '    FAIL  ----  unmutated baseline is not clean' )
        for failure in clean:
            print( '                %s' % failure )
        return 1

    print( '    PASS  ----  unmutated baseline is clean both ways' )

    #
    # The precondition every expect( ) below rests on, asserted rather than assumed
    #
    # expect( ) matches the marker with a trailing space so that a C1 expectation cannot be
    # satisfied by a C10 or C11 line. That is only safe while every failure string the tool emits
    # carries the space, and the way this defect returns is one new string written without it -
    # which would then match nothing and leave its own expectation silently unproven
    #

    total, unspaced = spaced()

    if unspaced:
        print( '    FAIL  ----  %d of %d failure string(s) do not follow the marker with a space'
               % ( len( unspaced ), total ) )
        for line, text in unspaced:
            print( '                utf_inventory.py:%d  %s' % ( line, text ) )
        ok = False
    else:
        print( '    PASS  ----  all %d failure strings follow the marker with a space  '
               'expect( ) can discriminate C1 from C10 and C11' % total )

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
    # It carries the silence control for the ADDED direction as well, since it asserts that NO
    # C6 failure of any kind is reported: members whose file and line changed but whose text did
    # not are a move, and a move must read as neither a loss nor an invention
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

    #
    # C6 - a helper member invented
    #
    # The direction the no-loss half never looked in, exactly as C8 never looked for a lost
    # recipe. C1 has always reported an invented case; a helper is no different
    #

    mutated = copy.deepcopy( baseline )
    invented = copy.deepcopy( mutated[ 'members' ][ 0 ] )
    invented[ 'sha' ] = 'c' * 32
    invented[ 'label' ] = 'void anInventedHelper( )'
    mutated[ 'members' ].append( invented )
    ok &= expect( 'helper member invented', check_against( baseline, mutated ), 'C6' )

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

    #
    # C9 - the direction C8 never looked in
    #
    # The declaration is synthesized rather than looked for, so that these fire against a baseline
    # captured before notes_index existed as well as against a fresh one
    #

    uncovered = {}

    for case in baseline[ 'cases' ]:
        info = baseline[ 'modules' ].get( case[ 'module' ] )
        if info is not None and case[ 'name' ] not in set( info.get( 'notes_cases', [] ) ):
            uncovered.setdefault( case[ 'module' ], [] ).append( case[ 'name' ] )

    incomplete = sorted( uncovered )[ 0 ]
    orphan = sorted( uncovered[ incomplete ] )[ 0 ]

    # C9 - a declared complete index which does not name one of its own cases
    mutated = copy.deepcopy( baseline )
    mutated[ 'modules' ][ incomplete ][ 'notes_index' ] = True
    ok &= expect( 'declared index missing a recipe (%s)' % orphan, check_intrinsic( mutated ), 'C9' )

    #
    # C9 - and the same module with no such declaration must stay silent
    #
    # This is the discrimination the rule rests on. 481 of this tree's 1075 cases have no recipe
    # and are meant to have none, so a C9 which cannot tell a claim of completeness from the
    # absence of one would report every single one of them
    #

    mutated = copy.deepcopy( baseline )
    mutated[ 'modules' ][ incomplete ][ 'notes_index' ] = False
    residue = [ f for f in check_intrinsic( mutated ) if f.startswith( 'C9' ) ]

    if residue:
        print( '    FAIL  C9   undeclared module with no recipes              '
               '(reported - the rule is not opt-in and would fire on 481 cases)' )
        ok = False
    else:
        print( '    PASS  C9   undeclared module with no recipes              '
               'correctly silent - the requirement is opt-in' )

    # C9 - a surviving case which lost the recipe it had
    owners = {}
    for module, info in baseline[ 'modules' ].items():
        for name in info.get( 'notes_cases', [] ):
            owners.setdefault( name, [] ).append( module )

    live = { case[ 'name' ] for case in baseline[ 'cases' ] }
    dropped = sorted( name for name in owners if name in live )[ 0 ]

    mutated = copy.deepcopy( baseline )
    for info in mutated[ 'modules' ].values():
        info[ 'notes_cases' ] = [ n for n in info.get( 'notes_cases', [] ) if n != dropped ]
    ok &= expect( 'recipe deleted for a live case (%s)' % dropped,
                  check_against( baseline, mutated ), 'C9' )

    #
    # C9 - the declaration withdrawn, which would switch the completeness half off silently
    #
    declared = copy.deepcopy( baseline )
    declared[ 'modules' ][ incomplete ][ 'notes_index' ] = True
    withdrawn = copy.deepcopy( baseline )
    withdrawn[ 'modules' ][ incomplete ][ 'notes_index' ] = False
    ok &= expect( 'index declaration withdrawn (%s)' % incomplete,
                  check_against( declared, withdrawn ), 'C9' )

    #
    # C7 - a data file whose content changed under tests that still read it
    #

    mutated = copy.deepcopy( baseline )
    owner = next( name for name, info in sorted( mutated[ 'modules' ].items() ) if info[ 'data_files' ] )
    edited = sorted( mutated[ 'modules' ][ owner ][ 'data_files' ] )[ 0 ]
    mutated[ 'modules' ][ owner ][ 'data_files' ][ edited ] = 'e' * 32
    ok &= expect( 'data file content changed (%s)' % edited,
                  check_against( baseline, mutated ), 'C7' )

    #
    # C7 - EVERY copy of a shared data file changed identically
    #
    # The stronger form, and the one that rules out the cross-module divergence check as
    # accidental cover: change one copy and that intrinsic check fires, change all of them and it
    # has nothing to compare. The assertion below is that the intrinsic half really is silent
    # here, so that the differential half is shown to be what catches it
    #

    carried = {}

    for name, info in baseline[ 'modules' ].items():
        for data in info[ 'data_files' ]:
            carried.setdefault( data, [] ).append( name )

    shared = sorted( ( len( owners ), name ) for name, owners in carried.items() )[ -1 ][ 1 ]

    mutated = copy.deepcopy( baseline )

    for info in mutated[ 'modules' ].values():
        if shared in info[ 'data_files' ]:
            info[ 'data_files' ][ shared ] = 'e' * 32

    residue = [ f for f in check_intrinsic( mutated ) if f.startswith( 'C7' ) ]

    if residue:
        print( '    ----  C7   all %d copies of %-30s the intrinsic half fired too'
               % ( len( carried[ shared ] ), shared ) )
    else:
        print( '    ----  C7   all %d copies of %-30s intrinsic half silent, as expected'
               % ( len( carried[ shared ] ), shared ) )

    ok &= expect( 'every copy of %s changed alike' % shared,
                  check_against( baseline, mutated ), 'C7' )

    #
    # C7 - a data file which lost its last reference
    #

    mutated = copy.deepcopy( baseline )
    stranded = None

    for name, info in sorted( mutated[ 'modules' ].items() ):
        for data in sorted( info[ 'data_files' ] ):
            if data in info[ 'data_refs' ]:
                stranded = ( name, data )
                break
        if stranded:
            break

    module, data = stranded
    info = mutated[ 'modules' ][ module ]
    info[ 'data_refs' ] = [ ref for ref in info[ 'data_refs' ] if ref != data ]
    info[ 'data_literals' ] = [ ref for ref in info.get( 'data_literals', [] ) if ref != data ]

    ok &= expect( 'data file lost its last reference (%s)' % data,
                  check_against( baseline, mutated ), 'C7' )

    #
    # C7 - and the data files this tree already carries unreferenced must stay silent
    #
    # Four of them today. An intrinsic orphan rule would report all four on a tree nobody has
    # changed, which is the same mistake a universal C9 would have been
    #
    # The count is ASSERTED, not merely printed. Silence over a baseline carrying no orphan at
    # all proves nothing about a differential rule, so the control has to establish that there
    # is something there to stay silent about before the silence means anything
    #

    stale = sum( len( unreferenced( info ) ) for info in baseline[ 'modules' ].values() )
    residue = [ f for f in check_against( baseline, baseline ) if f.startswith( 'C7' ) ]

    if not stale:
        print( '    FAIL  C7   %d data file(s) unreferenced in the baseline      '
               '(the baseline carries no orphan - this control has nothing to prove)' % stale )
        ok = False
    elif residue:
        print( '    FAIL  C7   %d data file(s) unreferenced in the baseline      '
               '(reported - the orphan rule is not differential)' % stale )
        ok = False
    else:
        print( '    PASS  C7   %d data file(s) unreferenced in the baseline      '
               'correctly silent - only a NEW orphan is reported' % stale )

    #
    # C7 - but a module the baseline does not carry is judged intrinsically
    #
    # A new module has no earlier state to be grandfathered against, and the f992e2f split gave
    # utf_baselib_messaging3 a data file nothing in it names. Skipping new modules is why that
    # went unreported while the same split's two leftovers were caught
    #

    mutated = copy.deepcopy( baseline )
    donor = next( info for info in baseline[ 'modules' ].values() if info.get( 'data_files' ) )

    mutated[ 'modules' ][ 'utf_baselib_freshly_split' ] = {
        'files': [],
        'data_refs': [],
        'data_literals': [],
        'data_files': dict( donor[ 'data_files' ] ),
        'notes_cases': [],
        'notes_index': False,
        }

    ok &= expect( 'new module carries a data file it never names',
                  check_against( baseline, mutated ), 'C7' )

    #
    # C7 - a baseline with no data file hashes must say so rather than pass everything
    #

    stripped = copy.deepcopy( baseline )

    for info in stripped[ 'modules' ].values():
        info[ 'data_files' ] = {}

    ok &= expect( 'baseline predating the data file capture',
                  check_against( stripped, baseline ), 'C7' )

    #
    # C10 - an #include added to a file which holds live test cases
    #
    # This is the mutation that showed the docstring claimed more than the checks held: the
    # compilation context of every case in that header changes and C1 to C4 stay green
    #

    mutated = copy.deepcopy( baseline )
    carrier = sorted( case[ 'file' ] for case in baseline[ 'cases' ] )[ 0 ]

    for info in mutated[ 'modules' ].values():
        for entry in info[ 'files' ]:
            if entry[ 'path' ] == carrier:
                entry[ 'includes' ] = entry[ 'includes' ] + [ '<an/invented/header.h>' ]

    ok &= expect( 'include added to a file with cases (%s)' % carrier,
                  check_against( baseline, mutated ), 'C10' )

    # C10 - the same file's includes merely reordered
    mutated = copy.deepcopy( baseline )

    for info in mutated[ 'modules' ].values():
        for entry in info[ 'files' ]:
            if entry[ 'path' ] == carrier and len( entry[ 'includes' ] ) > 1:
                entry[ 'includes' ] = list( reversed( entry[ 'includes' ] ) )

    ok &= expect( 'includes reordered (%s)' % carrier, check_against( baseline, mutated ), 'C10' )

    #
    # C10 - a file removed by a relocation must NOT be reported
    #
    # A split deletes headers by design, so a rule which judged a vanished file would be red on
    # every legitimate use of this tool. The cases and members that file carried are C1's and
    # C6's to speak for
    #

    mutated = copy.deepcopy( baseline )

    for info in mutated[ 'modules' ].values():
        info[ 'files' ] = [ entry for entry in info[ 'files' ] if entry[ 'path' ] != carrier ]

    residue = [ f for f in check_against( baseline, mutated ) if f.startswith( 'C10' ) ]

    if residue:
        print( '    FAIL  C10  file removed by a relocation                  '
               '(reported - the rule is not scoped to files present on both sides)' )
        ok = False
    else:
        print( '    PASS  C10  file removed by a relocation                  '
               'correctly silent - only files present on both sides are judged' )

    #
    # C10 - the roster edit a relocation cannot avoid
    #
    # Every module's Utf<Name>Main.cpp holds one quoted include per header the module carries, so
    # moving a header between modules MUST edit it. Judging that edit reds the very operation
    # this tool exists to verify - the real split f992e2f was reported as a violation before the
    # exemption existed
    #
    # The five below are written as one group because the exemption is only defensible together
    # with the three cases it must NOT swallow
    #

    roster_module, roster, siblings = None, None, set()

    for name, info in sorted( baseline[ 'modules' ].items() ):

        held = { entry[ 'path' ][ len( name ) + 1 : ] for entry in info[ 'files' ] }

        for entry in info[ 'files' ]:
            quoted = [ i for i in entry[ 'includes' ]
                       if i.startswith( '"' ) and i[ 1 : -1 ] in held ]
            if len( quoted ) >= 3:
                roster_module, roster, siblings = name, entry, held
                break

        if roster:
            break

    def roster_of( manifest ):
        info = manifest[ 'modules' ][ roster_module ]
        return next( e for e in info[ 'files' ] if e[ 'path' ] == roster[ 'path' ] )

    def without_header( manifest, header ):
        info = manifest[ 'modules' ][ roster_module ]
        info[ 'files' ] = [ e for e in info[ 'files' ]
                            if e[ 'path' ] != roster_module + '/' + header ]

    header = next( i[ 1 : -1 ] for i in roster[ 'includes' ]
                   if i.startswith( '"' ) and i[ 1 : -1 ] in siblings )

    # C10 - a header moved out of the module, struck from the roster with it: SILENT
    mutated = copy.deepcopy( baseline )
    without_header( mutated, header )
    roster_of( mutated )[ 'includes' ] = [ i for i in roster[ 'includes' ]
                                           if i != '"%s"' % header ]

    residue = [ f for f in check_against( baseline, mutated ) if f.startswith( 'C10' ) ]

    if residue:
        print( '    FAIL  C10  header moved out, roster edited to match      '
               '(reported - C10 would fire on every real relocation)' )
        ok = False
    else:
        print( '    PASS  C10  header moved out, roster edited to match      '
               'correctly silent - that roster edit IS the relocation' )

    # C10 - a header cut into the module, added to the roster with it: SILENT
    mutated = copy.deepcopy( baseline )
    mutated[ 'modules' ][ roster_module ][ 'files' ].append(
        { 'path': roster_module + '/TestCut.h', 'includes': [] } )
    roster_of( mutated )[ 'includes' ] = roster[ 'includes' ] + [ '"TestCut.h"' ]

    residue = [ f for f in check_against( baseline, mutated ) if f.startswith( 'C10' ) ]

    if residue:
        print( '    FAIL  C10  header cut in, roster edited to match         '
               '(reported - a split writes sibling headers and lists them)' )
        ok = False
    else:
        print( '    PASS  C10  header cut in, roster edited to match         '
               'correctly silent - the same clause, the other direction' )

    #
    # C10 - a header STRUCK FROM THE ROSTER while it stays in the module
    #
    # The case that makes the exemption safe to hold. Every test case in that header stops being
    # registered, the manifest still finds all of them, and C1 to C4 stay green - which is the
    # failure mode this whole tool was built for
    #

    mutated = copy.deepcopy( baseline )
    roster_of( mutated )[ 'includes' ] = [ i for i in roster[ 'includes' ]
                                           if i != '"%s"' % header ]
    ok &= expect( 'roster drops a header that stays (%s)' % header,
                  check_against( baseline, mutated ), 'C10' )

    # C10 - an <angle> include added to a roster is never exempt
    mutated = copy.deepcopy( baseline )
    roster_of( mutated )[ 'includes' ] = roster[ 'includes' ] + [ '<an/invented/header.h>' ]
    ok &= expect( 'angle include added to a roster', check_against( baseline, mutated ), 'C10' )

    #
    # C10 - whatever survives the exemption is still compared in ORDER
    #
    # In a roster, include order is registration order is run order, and
    # MessagingUtils_TokenTypeConcurrencyTests needs a cold process-global cache: any case that
    # runs before it and warms that cache neuters it while it still passes
    #

    mutated = copy.deepcopy( baseline )
    without_header( mutated, header )
    roster_of( mutated )[ 'includes' ] = list( reversed(
        [ i for i in roster[ 'includes' ] if i != '"%s"' % header ] ) )
    ok &= expect( 'surviving roster reordered', check_against( baseline, mutated ), 'C10' )

    #
    # C10 - a baseline with no include lists must say so rather than pass everything
    #
    # The failure mode this guards has bitten this tool twice already: a check added after a
    # baseline was captured reads an absent field, finds nothing to compare, and reports clean
    #

    stripped = copy.deepcopy( baseline )

    for info in stripped[ 'modules' ].values():
        for entry in info[ 'files' ]:
            entry[ 'includes' ] = []

    ok &= expect( 'baseline predating the include capture',
                  check_against( stripped, baseline ), 'C10' )

    #
    # C11 - file-scope text, which for ten invariants nothing hashed
    #
    # A refreshed baseline carries its own file_members and they are used as they stand. One
    # captured before C11 existed carries none, and rather than skip the proof the list is stood
    # up from the baseline's own helper members - real spans with real shas, re-filed as
    # file-scope ones. Every branch of the check is exercised either way
    #
    # This is deliberately NOT the capture path, and the difference matters: what C11 extracts
    # from a real tree, and what it must stay silent about, is proved on filesystem copies of
    # src/utests. Those probes are what found the two exclusions; a synthesized list could not
    # have, because it has no preprocessor lines and no comment blocks in it
    #

    armed = copy.deepcopy( baseline )

    if not armed.get( 'file_members' ):
        armed[ 'file_members' ] = [
            { 'module': member[ 'module' ], 'file': member[ 'file' ], 'line': member[ 'line' ],
              'sha': member[ 'sha' ], 'label': member[ 'label' ] }
            for member in baseline[ 'members' ][ : 40 ]
            ]

    if check_against( armed, armed ):
        print( '    FAIL  C11  armed baseline is not clean against itself' )
        ok = False
    else:
        print( '    PASS  C11  armed baseline is clean against itself       '
               '%d file-scope span(s) in force' % len( armed[ 'file_members' ] ) )

    # C11 - a file-scope span edited, which is what a member injected into a fixture looks like
    mutated = copy.deepcopy( armed )
    mutated[ 'file_members' ][ 0 ][ 'sha' ] = 'a' * 32
    ok &= expect( 'file-scope span edited (%s)' % mutated[ 'file_members' ][ 0 ][ 'label' ][ : 24 ],
                  check_against( armed, mutated ), 'C11' )

    # C11 - a file-scope span deleted outright
    mutated = copy.deepcopy( armed )
    gone = mutated[ 'file_members' ].pop( 1 )
    ok &= expect( 'file-scope span deleted (%s)' % gone[ 'label' ][ : 24 ],
                  check_against( armed, mutated ), 'C11' )

    #
    # C11 - a file-scope span invented
    #
    # The direction C6 was one-way about until 1c7003e, and C8 before it. A relocation invents no
    # fixture any more than it invents a case
    #

    mutated = copy.deepcopy( armed )
    mutated[ 'file_members' ].append(
        { 'module': 'utf_baselib', 'file': 'utf_baselib/TestObjModel.h', 'line': 1,
          'sha': 'b' * 32, 'label': 'struct AnInventedFixture' } )
    ok &= expect( 'file-scope span invented', check_against( armed, mutated ), 'C11' )

    #
    # C11 - the same span in another file: a relocation, and it must be SILENT
    #
    # This is the whole reason the identity is text alone and the comparison is tree wide. A
    # split moving a fixture into a sibling header, or into a new module with the cases it
    # fixtures, changes its file and its line and nothing else
    #

    mutated = copy.deepcopy( armed )
    mutated[ 'file_members' ][ 2 ][ 'file' ] = 'utf_baselib_tasks9/TestTasks8.h'
    mutated[ 'file_members' ][ 2 ][ 'module' ] = 'utf_baselib_tasks9'
    mutated[ 'file_members' ][ 2 ][ 'line' ] = 4242

    residue = [ f for f in check_against( armed, mutated ) if f.startswith( 'C11 ' ) ]

    if residue:
        print( '    FAIL  C11  span relocated to another module             '
               '(reported - C11 would fire on every legitimate split)' )
        ok = False
    else:
        print( '    PASS  C11  span relocated to another module             '
               'correctly silent - identity is text alone, compared tree wide' )

    #
    # C11 - a baseline predating the file-scope capture leaves the check not in force, silently
    #
    # It is the one arming guard in this tool that must NOT fail, because no baseline captured
    # before C11 carries the field and a hard red here would stop every lane until the refresh
    # lands. main( ) prints the state on every run instead, which is the C9 no-withdrawal
    # precedent. The two guards below are what keep that from becoming a permanent silence
    #

    older = copy.deepcopy( armed )
    del older[ 'file_members' ]

    mutated = copy.deepcopy( armed )
    mutated[ 'file_members' ][ 0 ][ 'sha' ] = 'c' * 32

    residue = [ f for f in check_against( older, mutated ) if f.startswith( 'C11 ' ) ]

    if residue:
        print( '    FAIL  C11  baseline predating the file-scope capture    '
               '(reported - it must be a printed note, not a red gate)' )
        ok = False
    else:
        print( '    PASS  C11  baseline predating the file-scope capture    '
               'correctly silent - not in force until the baseline is refreshed' )

    # C11 - but a baseline which carries the field EMPTY is broken, not merely old
    broken = copy.deepcopy( armed )
    broken[ 'file_members' ] = []
    ok &= expect( 'baseline carrying an empty file-scope list',
                  check_against( broken, armed ), 'C11' )

    # C11 - and an extraction which produced nothing is a failure of the run, not of the baseline
    empty = copy.deepcopy( armed )
    empty[ 'file_members' ] = []
    ok &= expect( 'capture which extracted no file-scope text',
                  check_against( armed, empty ), 'C11' )

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
