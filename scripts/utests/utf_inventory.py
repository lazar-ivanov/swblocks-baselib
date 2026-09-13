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
#   C6  no helper block occurs twice within one module (an ODR risk); none was lost
#   C7  every data file a module references exists in that module's data/ directory
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
    includes = []
    data_refs = set()

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

            ns_stack.append( name )
            index = open_index + 1
            continue

        if CLOSE_RE.match( line ) and ns_stack:
            ns_stack.pop()
            index += 1
            continue

        index += 1

    return cases, namespaces, includes, sorted( data_refs )


def file_sha( path ):
    digest = hashlib.sha256()
    with open( path, 'rb' ) as stream:
        digest.update( stream.read() )
    return digest.hexdigest()[ :32 ]


def capture( src_utests ):
    """
    Walk every utf* module directory and build the manifest
    """

    manifest = { 'cases': [], 'namespaces': [], 'modules': {} }
    problems = []

    for module in sorted( os.listdir( src_utests ) ):

        module_dir = os.path.join( src_utests, module )

        if not module.startswith( 'utf' ) or not os.path.isdir( module_dir ):
            continue

        module_info = { 'files': [], 'data_refs': [], 'data_files': {} }
        data_refs = set()

        for root, dirs, files in os.walk( module_dir ):

            dirs.sort()

            for entry in sorted( files ):

                if not entry.endswith( ( '.h', '.cpp' ) ):
                    continue

                path = os.path.join( root, entry )
                rel_path = os.path.relpath( path, src_utests ).replace( os.sep, '/' )

                cases, namespaces, includes, refs = scan_file( path, module, rel_path, problems )

                manifest[ 'cases' ].extend( cases )
                manifest[ 'namespaces' ].extend( namespaces )
                data_refs.update( refs )

                module_info[ 'files' ].append( { 'path': rel_path, 'includes': includes } )

        data_dir = os.path.join( module_dir, 'data' )

        if os.path.isdir( data_dir ):
            for entry in sorted( os.listdir( data_dir ) ):
                full = os.path.join( data_dir, entry )
                if os.path.isfile( full ):
                    module_info[ 'data_files' ][ entry ] = file_sha( full )

        module_info[ 'data_refs' ] = sorted( data_refs )
        manifest[ 'modules' ][ module ] = module_info

    manifest[ 'cases' ].sort( key = lambda case: case[ 'name' ] )
    manifest[ 'namespaces' ].sort( key = lambda ns: ( ns[ 'module' ], ns[ 'file' ], ns[ 'line' ] ) )

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

    for module, info in sorted( manifest[ 'modules' ].items() ):
        for ref in info[ 'data_refs' ]:
            if ref not in info[ 'data_files' ]:
                failures.append(
                    'C7 module %s references data file %s which is not in its data/ directory' % ( module, ref )
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

    old_blocks = {}

    for ns in before[ 'namespaces' ]:
        old_blocks.setdefault( ns[ 'sha' ], ns )

    new_blocks = { ns[ 'sha' ] for ns in after[ 'namespaces' ] }

    for digest in sorted( set( old_blocks ) - new_blocks ):
        where = old_blocks[ digest ]
        failures.append( 'C6 helper block LOST: %s:%d' % ( where[ 'file' ], where[ 'line' ] ) )

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
