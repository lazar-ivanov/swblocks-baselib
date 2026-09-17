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
# utf_inventory.py - prove that a test module split moved tests without changing them
#
# Splitting the oversized test modules is a pure relocation: test case text is moved between
# headers and between modules, but never edited. That makes it provable rather than reviewable,
# which matters because the failure mode - a case that silently stops being registered - looks
# exactly like success in a green test run
#
# This script captures a manifest of every test case in src/utests and checks seven invariants:
#
#   C1  the set of case names is identical
#   C2  every case body and doc comment hashes the same
#   C3  every case sits under the same #if guard stack
#   C4  every case sits under the same namespace stack
#   C5  no case name occurs twice anywhere in the tree
#   C6  no helper block or member occurs twice within one module (an ODR risk); none was lost
#   C7  every data file a module references exists in that module's data/ directory
#   C8  every case a module's notes.txt names exists in that module
#
# C2 together with C3 and C4 is the core claim: the text of every test, and the compilation
# context that text sees, is unchanged
#
# The parser relies on a layout precondition which holds throughout src/utests and is asserted
# on every run: each test case macro sits at column 0 and its braces sit at column 0, so a case
# extracts unambiguously without a C++ parser
#
# Usage:
#
#   utf_inventory.py --capture baseline.json
#   utf_inventory.py --compare baseline.json
#   utf_inventory.py --compare before.json --against after.json
#
# Stdlib only, by design - it must run on the devenv7 dist interpreter, which is an embeddable
# build with no venv and no pip
#

from __future__ import print_function

import argparse
import hashlib
import json
import os
import re
import sys

CASE_RE = re.compile(
    r'^(UTF_AUTO_TEST_CASE|UTF_FIXTURE_TEST_CASE)\('
    r'\s*([A-Za-z_][A-Za-z0-9_]*)\s*'
    r'(?:,\s*([A-Za-z_][A-Za-z0-9_:<>]*)\s*)?\)'
    )

NAMESPACE_RE = re.compile( r'^namespace(?:\s+([A-Za-z_][A-Za-z0-9_]*))?\s*(\{)?\s*$' )

CLOSE_RE = re.compile( r'^\}' )

COND_OPEN_RE = re.compile( r'^\s*#\s*(if|ifdef|ifndef)\b\s*(.*)$' )
COND_MID_RE = re.compile( r'^\s*#\s*(elif|else)\b\s*(.*)$' )
COND_CLOSE_RE = re.compile( r'^\s*#\s*endif\b' )

INCLUDE_RE = re.compile( r'^\s*#\s*include\s+(.+?)\s*$' )

DATA_REF_RE = re.compile( r'(?:resolveDataFilePath|loadDataFile)\(\s*"([^"]+)"' )

#
# Matching only the call sites above is not enough. Several test helpers take the file name as a
# parameter - testAuthorizationServiceForResponse( service, "response_success.txt" ) - so the
# literal is nowhere near a resolveDataFilePath call. A module could then be split away from a
# data file it genuinely needs and C7 would still pass, with the failure surfacing only as a test
# error later
#
# So every filename-shaped literal is collected too, and checked against the set of data file
# names which actually exist anywhere in the tree
#

DATA_LITERAL_RE = re.compile( r'"([A-Za-z0-9_][A-Za-z0-9_.\-]*\.[A-Za-z0-9]{1,8})"' )

#
# Each module's notes.txt is a list of ready-made command lines for running single cases. They are
# the first thing anyone reaches for when investigating a failure, and nothing else checks them: a
# recipe naming a case which has moved or been deleted still satisfies every other invariant. Two
# such were found in consecutive modules during the split, one of them naming a case which was very
# much alive but had migrated to another module years earlier
#
# Boost.Test accepts a comma separated list in one --run_test, which is the only compound form used
# in this tree; anything carrying a wildcard is skipped rather than guessed at
#

NOTES_RUN_TEST_RE = re.compile( r'--run_test=([^\s]+)' )

#
# C6 hashes each helper block whole, which cannot tell a block that was partitioned from one that
# was deleted. Both look like "the block whose sha was X is gone". That is not a hypothetical: the
# plan splits headers precisely by moving some helpers out and leaving the rest, and the tasks
# fixture hoist does it on a much larger scale
#
# So helper blocks are also split into members, and the no-loss half of C6 is checked per member.
# A member is a maximal run of lines ending at the first blank line at which every bracket it
# opened is closed. That is the house style throughout src/utests - members are separated by
# blank lines, and none leaves a bracket open across one - and it is asserted on every run by
# requiring that the members of a block cover every non-blank line of it
#
# A nested namespace inside a block is one member rather than being descended into. That is a
# deliberate limit: it keeps the rule total, and the outer block still moves or dies as a unit
#

STRIP_RE = re.compile( r'"[^"]*"|\'[^\']*\'|//.*$' )


def bracket_delta( line ):
    """
    Net bracket depth contributed by one line, ignoring string literals and line comments
    """

    text = STRIP_RE.sub( '', line )

    return ( text.count( '{' ) + text.count( '(' ) + text.count( '[' )
           - text.count( '}' ) - text.count( ')' ) - text.count( ']' ) )


def split_members( lines, start, stop ):
    """
    Split a helper block body, lines[ start : stop ], into ( first, last ) line index pairs
    """

    members = []
    index = start

    while index < stop:

        if lines[ index ].strip() == '':
            index += 1
            continue

        first = index
        depth = 0

        while index < stop:

            depth += bracket_delta( lines[ index ] )
            index += 1

            if depth <= 0 and ( index >= stop or lines[ index ].strip() == '' ):
                break

        members.append( ( first, index - 1 ) )

    return members


def sha( text ):
    return hashlib.sha256( text.encode( 'utf-8' ) ).hexdigest()[ :32 ]


def normalize( lines ):
    """
    Join lines with a single newline after stripping trailing whitespace

    This makes every hash immune to line-ending changes, which matters because core.autocrlf is
    true with no .gitattributes in this repository, and git diff --check does not catch a
    whole-file line-ending rewrite
    """

    return '\n'.join( line.rstrip() for line in lines )


def read_lines( path ):
    with open( path, 'r', encoding = 'utf-8', errors = 'replace' ) as stream:
        return stream.read().split( '\n' )


def doc_comment_span( lines, case_start ):
    """
    Return ( start, end ) of the block comment immediately above a case, or None

    Every case in the large headers is preceded by a substantive block comment explaining why
    the case exists and what else does not cover it. Those comments are part of the case for
    relocation purposes and must be hashed, or the checks would silently permit losing them
    """

    index = case_start - 1

    while index >= 0 and lines[ index ].strip() == '':
        index -= 1

    if index < 0 or not lines[ index ].rstrip().endswith( '*/' ):
        return None

    end = index

    while index >= 0:
        if lines[ index ].lstrip().startswith( '/*' ):
            return ( index, end )
        index -= 1

    return None


DEFINE_RE = re.compile( r'^\s*#\s*define\s+([A-Za-z_][A-Za-z0-9_]*)' )


def is_include_guard( lines, index, matched ):
    """
    True when the #ifndef at this line is a plain include guard

    The shape is #ifndef FOO followed, ignoring blank and comment lines, by #define FOO
    """

    if matched.group( 1 ) != 'ifndef':
        return False

    symbol = matched.group( 2 ).strip()

    if not symbol:
        return False

    probe = index + 1

    while probe < len( lines ):

        candidate = lines[ probe ].strip()

        if candidate == '' or candidate.startswith( ( '//', '/*', '*' ) ):
            probe += 1
            continue

        defined = DEFINE_RE.match( lines[ probe ] )
        return bool( defined and defined.group( 1 ) == symbol )

    return False


def scan_file( path, module, rel_path, problems ):
    """
    Extract every test case, column-0 namespace block and include from one file

    Namespace tracking is deliberately limited to column 0. Every test case in this tree sits at
    column 0, so a column-0 stack is enough to detect a case moving into or out of a namespace,
    which is what C4 exists to catch
    """

    lines = read_lines( path )

    cases = []
    namespaces = []
    members = []
    includes = []
    data_refs = set()
    data_literals = set()

    cond_stack = []
    ns_stack = []

    index = 0
    total = len( lines )

    while index < total:

        line = lines[ index ]

        matched = COND_OPEN_RE.match( line )
        if matched:
            #
            # An include guard is not part of a case's compilation context in any meaningful
            # sense, and counting it would make C3 fire whenever a case moved into a new header
            # written without one - a false alarm that would quickly erode trust in the gate.
            # It is recognised by its shape: #ifndef FOO immediately followed by #define FOO
            #
            if is_include_guard( lines, index, matched ):
                cond_stack.append( None )
            else:
                cond_stack.append( '%s %s' % ( matched.group( 1 ), matched.group( 2 ).strip() ) )
            index += 1
            continue

        matched = COND_MID_RE.match( line )
        if matched and cond_stack:
            if cond_stack[ -1 ] is not None:
                cond_stack[ -1 ] += ' | %s %s' % ( matched.group( 1 ), matched.group( 2 ).strip() )
            index += 1
            continue

        if COND_CLOSE_RE.match( line ):
            if cond_stack:
                cond_stack.pop()
            index += 1
            continue

        matched = INCLUDE_RE.match( line )
        if matched:
            includes.append( matched.group( 1 ) )
            index += 1
            continue

        for ref in DATA_REF_RE.findall( line ):
            data_refs.add( ref )

        for literal in DATA_LITERAL_RE.findall( line ):
            data_literals.add( literal )

        matched = CASE_RE.match( line )
        if matched:

            kind, name, fixture = matched.group( 1 ), matched.group( 2 ), matched.group( 3 )

            end = index + 1
            while end < total and not CLOSE_RE.match( lines[ end ] ):
                end += 1

            if end >= total:
                problems.append(
                    '%s:%d: case %s has no closing brace at column 0' % ( rel_path, index + 1, name )
                    )
                index += 1
                continue

            if lines[ index + 1 ].strip() != '{':
                problems.append(
                    '%s:%d: case %s is not followed by an opening brace at column 0 - '
                    'the extraction precondition does not hold' % ( rel_path, index + 2, name )
                    )

            doc = doc_comment_span( lines, index )

            # most data file references sit inside case bodies, which the jump below skips
            for body_line in lines[ index : end + 1 ]:
                for ref in DATA_REF_RE.findall( body_line ):
                    data_refs.add( ref )

                for literal in DATA_LITERAL_RE.findall( body_line ):
                    data_literals.add( literal )

            cases.append( {
                'name': name,
                'kind': kind,
                'fixture': fixture,
                'module': module,
                'file': rel_path,
                'line': index + 1,
                'body_sha': sha( normalize( lines[ index : end + 1 ] ) ),
                'doc_sha': sha( normalize( lines[ doc[ 0 ] : doc[ 1 ] + 1 ] ) ) if doc else None,
                'guards': [ guard for guard in cond_stack if guard is not None ],
                'namespaces': list( ns_stack ),
                } )

            index = end + 1
            continue

        matched = NAMESPACE_RE.match( line )
        if matched:

            name = matched.group( 1 ) or '<anonymous>'

            open_index = index if matched.group( 2 ) else index + 1

            if open_index >= total or lines[ open_index ].strip() != '{':
                if not matched.group( 2 ):
                    index += 1
                    continue

            end = open_index + 1
            while end < total and not CLOSE_RE.match( lines[ end ] ):
                end += 1

            namespaces.append( {
                'name': name,
                'module': module,
                'file': rel_path,
                'line': index + 1,
                'sha': sha( normalize( lines[ index : min( end, total - 1 ) + 1 ] ) ),
                } )

            #
            # The enclosing namespace path is recorded beside the member rather than folded into
            # its hash, because the two checks want different identities. The no-loss check
            # compares text alone, so that hoisting a helper out of an anonymous namespace into
            # a shared one reads as a move and not as a deletion. The duplication check compares
            # text and path together, because the same helper text in two different namespaces
            # is no ODR risk at all - utf_baselib_async carries five such pairs deliberately,
            # one set in namespace asynccb and the parallel set in namespace asyncv2
            #

            member_ns = '::'.join( ns_stack + [ name ] )

            covered = set()

            for first, last in split_members( lines, open_index + 1, min( end, total ) ):

                covered.update( range( first, last + 1 ) )

                members.append( {
                    'module': module,
                    'file': rel_path,
                    'line': first + 1,
                    'ns': member_ns,
                    'sha': sha( normalize( lines[ first : last + 1 ] ) ),
                    'label': lines[ first ].strip()[ : 60 ],
                    } )

            for probe in range( open_index + 1, min( end, total ) ):
                if lines[ probe ].strip() != '' and probe not in covered:
                    problems.append(
                        '%s:%d: helper block member extraction left this line uncovered - '
                        'the member precondition does not hold' % ( rel_path, probe + 1 )
                        )

            ns_stack.append( name )
            index = open_index + 1
            continue

        if CLOSE_RE.match( line ) and ns_stack:
            ns_stack.pop()
            index += 1
            continue

        index += 1

    return cases, namespaces, members, includes, sorted( data_refs ), sorted( data_literals )


def file_sha( path ):
    """
    Hash a data file with line endings normalized to LF

    Hashing the raw bytes makes the manifest depend on how the tree happened to be checked out.
    core.autocrlf is true here with no .gitattributes, so a fresh worktree of the very same
    commit gets CRLF where a long-lived one has LF, and the hashes disagree for no reason that
    matters. Normalizing keeps the manifest a property of the commit rather than of the checkout,
    which is what makes a baseline reproducible
    """

    digest = hashlib.sha256()

    with open( path, 'rb' ) as stream:
        digest.update( stream.read().replace( b'\r\n', b'\n' ) )

    return digest.hexdigest()[ :32 ]


def capture( src_utests ):
    """
    Walk every utf* module directory and build the manifest
    """

    manifest = { 'cases': [], 'namespaces': [], 'members': [], 'modules': {} }
    problems = []

    for module in sorted( os.listdir( src_utests ) ):

        module_dir = os.path.join( src_utests, module )

        if not module.startswith( 'utf' ) or not os.path.isdir( module_dir ):
            continue

        module_info = { 'files': [], 'data_refs': [], 'data_files': {} }
        data_refs = set()
        data_literals = set()

        for root, dirs, files in os.walk( module_dir ):

            dirs.sort()

            for entry in sorted( files ):

                if not entry.endswith( ( '.h', '.cpp' ) ):
                    continue

                path = os.path.join( root, entry )
                rel_path = os.path.relpath( path, src_utests ).replace( os.sep, '/' )

                cases, namespaces, members, includes, refs, literals = scan_file( path, module, rel_path, problems )

                manifest[ 'cases' ].extend( cases )
                manifest[ 'namespaces' ].extend( namespaces )
                manifest[ 'members' ].extend( members )
                data_refs.update( refs )
                data_literals.update( literals )

                module_info[ 'files' ].append( { 'path': rel_path, 'includes': includes } )

        notes_path = os.path.join( module_dir, 'notes.txt' )
        notes_cases = []

        if os.path.isfile( notes_path ):
            with open( notes_path, 'r', encoding = 'utf-8', errors = 'replace' ) as stream:
                for spec in NOTES_RUN_TEST_RE.findall( stream.read() ):
                    for name in spec.split( ',' ):
                        name = name.strip()
                        if name and '*' not in name and '?' not in name:
                            notes_cases.append( name )

        module_info[ 'notes_cases' ] = sorted( set( notes_cases ) )

        data_dir = os.path.join( module_dir, 'data' )

        if os.path.isdir( data_dir ):
            for entry in sorted( os.listdir( data_dir ) ):
                full = os.path.join( data_dir, entry )
                if os.path.isfile( full ):
                    module_info[ 'data_files' ][ entry ] = file_sha( full )

        module_info[ 'data_refs' ] = sorted( data_refs )
        module_info[ 'data_literals' ] = sorted( data_literals )
        manifest[ 'modules' ][ module ] = module_info

    manifest[ 'cases' ].sort( key = lambda case: case[ 'name' ] )
    manifest[ 'namespaces' ].sort( key = lambda ns: ( ns[ 'module' ], ns[ 'file' ], ns[ 'line' ] ) )
    manifest[ 'members' ].sort( key = lambda m: ( m[ 'module' ], m[ 'file' ], m[ 'line' ] ) )

    return manifest, problems


def index_cases( manifest ):
    return { case[ 'name' ]: case for case in manifest[ 'cases' ] }


def check_intrinsic( manifest ):
    """
    Invariants that hold of a single manifest on its own: C5, C6 and C7
    """

    failures = []

    seen = {}

    for case in manifest[ 'cases' ]:
        name = case[ 'name' ]
        if name in seen:
            failures.append(
                'C5 duplicate case name %s in %s and %s' % ( name, seen[ name ][ 'file' ], case[ 'file' ] )
                )
        seen[ name ] = case

    by_module = {}

    for ns in manifest[ 'namespaces' ]:
        by_module.setdefault( ns[ 'module' ], {} ).setdefault( ns[ 'sha' ], [] ).append( ns )

    for module, blocks in sorted( by_module.items() ):
        for digest, entries in sorted( blocks.items() ):
            if len( entries ) > 1:
                where = ', '.join( '%s:%d' % ( e[ 'file' ], e[ 'line' ] ) for e in entries )
                failures.append(
                    'C6 helper block duplicated within module %s (ODR risk): %s' % ( module, where )
                    )

    #
    # The same check one level finer, so that copying a single helper into a second header of the
    # same module is caught as well as copying a whole block. Identity here is the text together
    # with the enclosing namespace path: the same text under two different namespaces is not a
    # redefinition, and utf_baselib_async relies on that
    #

    by_module_member = {}

    for member in manifest.get( 'members', [] ):
        key = ( member[ 'sha' ], member[ 'ns' ] )
        by_module_member.setdefault( member[ 'module' ], {} ).setdefault( key, [] ).append( member )

    for module, entries in sorted( by_module_member.items() ):
        for key, found in sorted( entries.items(), key = lambda item: item[ 0 ] ):
            if len( found ) > 1:
                where = ', '.join( '%s:%d' % ( f[ 'file' ], f[ 'line' ] ) for f in found )
                failures.append(
                    'C6 helper member duplicated within module %s (ODR risk): %s in namespace %s at %s'
                    % ( module, found[ 0 ][ 'label' ], found[ 0 ][ 'ns' ] or '<global>', where )
                    )

    for module, info in sorted( manifest[ 'modules' ].items() ):
        for ref in info[ 'data_refs' ]:
            if ref not in info[ 'data_files' ]:
                failures.append(
                    'C7 module %s references data file %s which is not in its data/ directory' % ( module, ref )
                    )

    #
    # The set of names which are data files somewhere in the tree. A module naming one of these as
    # a literal needs its own copy, because TestUtils::resolveDataFilePath resolves relative to the
    # binary and a data/ directory cannot be shared between modules
    #

    known_data_names = set()

    for info in manifest[ 'modules' ].values():
        known_data_names.update( info[ 'data_files' ] )

    for module, info in sorted( manifest[ 'modules' ].items() ):
        for literal in info.get( 'data_literals', [] ):
            if literal in known_data_names and literal not in info[ 'data_files' ]:
                failures.append(
                    'C7 module %s names data file %s but does not carry it in its data/ directory'
                    % ( module, literal )
                    )

    #
    # C8 - every case a module's notes.txt names must exist in that module
    #
    # Split into the two failure modes, because they call for different fixes: a case which still
    # exists somewhere has a recipe that should follow it to its new module, while one which exists
    # nowhere is a stale recipe to delete
    #

    everywhere = {}

    for case in manifest[ 'cases' ]:
        everywhere[ case[ 'name' ] ] = case[ 'module' ]

    for module, info in sorted( manifest[ 'modules' ].items() ):

        own = { case[ 'name' ] for case in manifest[ 'cases' ] if case[ 'module' ] == module }

        for name in info.get( 'notes_cases', [] ):

            if name in own:
                continue

            if name in everywhere:
                failures.append(
                    'C8 module %s notes.txt names case %s which now lives in %s'
                    % ( module, name, everywhere[ name ] )
                    )
            else:
                failures.append(
                    'C8 module %s notes.txt names case %s which does not exist anywhere'
                    % ( module, name )
                    )

    by_name = {}

    for module, info in sorted( manifest[ 'modules' ].items() ):
        for name, digest in sorted( info[ 'data_files' ].items() ):
            by_name.setdefault( name, {} )[ module ] = digest

    for name, owners in sorted( by_name.items() ):
        if len( set( owners.values() ) ) > 1:
            failures.append(
                'C7 data file %s differs between modules %s' % ( name, ', '.join( sorted( owners ) ) )
                )

    return failures


def check_against( before, after ):
    """
    Invariants that relate two manifests: C1 to C4, plus the C6 no-loss half
    """

    failures = []

    old = index_cases( before )
    new = index_cases( after )

    for name in sorted( set( old ) - set( new ) ):
        failures.append( 'C1 case LOST: %s (was in %s)' % ( name, old[ name ][ 'file' ] ) )

    for name in sorted( set( new ) - set( old ) ):
        failures.append( 'C1 case ADDED: %s (now in %s)' % ( name, new[ name ][ 'file' ] ) )

    for name in sorted( set( old ) & set( new ) ):

        a, b = old[ name ], new[ name ]

        if a[ 'body_sha' ] != b[ 'body_sha' ]:
            failures.append( 'C2 case BODY CHANGED: %s (%s -> %s)' % ( name, a[ 'file' ], b[ 'file' ] ) )

        if a[ 'doc_sha' ] != b[ 'doc_sha' ]:
            failures.append( 'C2 case DOC COMMENT CHANGED: %s (%s -> %s)' % ( name, a[ 'file' ], b[ 'file' ] ) )

        if a[ 'guards' ] != b[ 'guards' ]:
            failures.append(
                'C3 case GUARD STACK CHANGED: %s (%s -> %s)' % ( name, a[ 'guards' ], b[ 'guards' ] )
                )

        if a[ 'namespaces' ] != b[ 'namespaces' ]:
            failures.append(
                'C4 case NAMESPACE STACK CHANGED: %s (%s -> %s)' % ( name, a[ 'namespaces' ], b[ 'namespaces' ] )
                )

    #
    # The no-loss half of C6, checked per member rather than per block. A helper is lost only if
    # its text survives nowhere in the tree; a block that was partitioned, or a helper hoisted
    # into a different namespace, is a move and reads as one
    #
    # Comparing text alone is what makes a hoist a move. The namespace path is deliberately not
    # part of this identity - it is used only by the duplication check above
    #
    # Both manifests must actually carry members, or a baseline captured before this check
    # existed would silently pass everything. That failure mode has bitten this tool twice
    #

    if 'members' not in before or not before[ 'members' ]:
        failures.append(
            'C6 the baseline carries no helper members - it predates the per-member check and '
            'must be regenerated from its own commit before this invariant can be trusted'
            )
        return failures

    if 'members' not in after or not after[ 'members' ]:
        failures.append( 'C6 the current manifest carries no helper members - extraction failed' )
        return failures

    old_members = {}

    for member in before[ 'members' ]:
        old_members.setdefault( member[ 'sha' ], member )

    new_members = { member[ 'sha' ] for member in after[ 'members' ] }

    for digest in sorted( set( old_members ) - new_members ):
        where = old_members[ digest ]
        failures.append(
            'C6 helper member LOST: %s (%s:%d)' % ( where[ 'label' ], where[ 'file' ], where[ 'line' ] )
            )

    return failures


def repo_root():
    return os.path.normpath( os.path.join( os.path.dirname( os.path.abspath( __file__ ) ), '..', '..' ) )


def main():

    parser = argparse.ArgumentParser( description = 'capture and compare the unit-test case inventory' )

    parser.add_argument( '--src', default = None, help = 'the utests source root (default: <repo>/src/utests)' )
    parser.add_argument( '--capture', metavar = 'PATH', help = 'write the manifest as JSON to PATH' )
    parser.add_argument( '--compare', metavar = 'PATH', help = 'compare against the manifest at PATH' )
    parser.add_argument( '--against', metavar = 'PATH', help = 'use this manifest instead of scanning the tree' )
    parser.add_argument( '--summary', action = 'store_true', help = 'print a per-module summary' )

    args = parser.parse_args()

    src_utests = args.src or os.path.join( repo_root(), 'src', 'utests' )

    if args.against:
        with open( args.against ) as stream:
            manifest = json.load( stream )
        problems = []
    else:
        manifest, problems = capture( src_utests )

    if problems:
        print( 'utf_inventory: PARSE PROBLEMS - the layout precondition does not hold:', file = sys.stderr )
        for problem in problems:
            print( '    %s' % problem, file = sys.stderr )
        return 3

    print( 'utf_inventory: %d cases, %d helper blocks, %d modules' % (
        len( manifest[ 'cases' ] ), len( manifest[ 'namespaces' ] ), len( manifest[ 'modules' ] ) ) )

    if args.summary:
        counts = {}
        for case in manifest[ 'cases' ]:
            counts[ case[ 'module' ] ] = counts.get( case[ 'module' ], 0 ) + 1
        print( '' )
        for module in sorted( manifest[ 'modules' ] ):
            info = manifest[ 'modules' ][ module ]
            print( '    %-30s %4d cases  %2d files  %2d data refs  %2d data files' % (
                module, counts.get( module, 0 ), len( info[ 'files' ] ),
                len( info[ 'data_refs' ] ), len( info[ 'data_files' ] ) ) )
        print( '' )

    if args.capture:
        with open( args.capture, 'w' ) as stream:
            json.dump( manifest, stream, indent = 1, sort_keys = True )
            stream.write( '\n' )
        print( 'utf_inventory: wrote %s' % args.capture )

    failures = check_intrinsic( manifest )

    if args.compare:
        with open( args.compare ) as stream:
            before = json.load( stream )
        failures.extend( check_against( before, manifest ) )

    if failures:
        print( '' )
        print( 'utf_inventory: FAIL - %d invariant violation(s):' % len( failures ) )
        for failure in failures:
            print( '    %s' % failure )
        return 1

    print( 'utf_inventory: PASS - all invariants hold' )
    return 0


if __name__ == '__main__':
    sys.exit( main() )
