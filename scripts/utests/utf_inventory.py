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
# This script captures a manifest of every test case in src/utests and checks eleven invariants:
#
#   C1  the set of case names is identical
#   C2  every case body and doc comment hashes the same
#   C3  every case sits under the same #if guard stack
#   C4  every case sits under the same namespace stack
#   C5  no case name occurs twice anywhere in the tree
#   C6  no helper block or member occurs twice within one module (an ODR risk); none was lost,
#       and none was invented
#   C7  every data file a module references exists in that module's data/ directory, carries the
#       content it had, and does not become one nothing names
#   C8  every case a module's notes.txt names exists in that module
#   C9  no case loses a recipe it had, and a module which declares its notes.txt a complete
#       index really does name every one of its cases
#   C10 every file keeps the #include list it had
#   C11 no file-scope text - what sits outside every column-0 namespace block - is lost or
#       invented
#
# C2 together with C3 and C4 is the core claim about a case which stayed where it was: its text,
# the preprocessor guard stack and the namespace stack it sits under are all unchanged. C10 adds
# the file's #include list to that, and C11 the declarations the file makes at file scope: the
# fixtures of utf_baselib_loader, the column-0 statics, the BL_IID_DECLAREs, UTF_GLOBAL_FIXTURE
# and the file-scope using-directives. That is 39 spans over 423 lines in 16 files today, and for
# ten invariants' worth of history no hash read any of it. Measured before C11 existed: a member
# injected into ManifestFixture, which three cases are fixtured on, and a changed signature on a
# column-0 static helper BOTH passed tier 1
#
# What remains outside every hash is two named things at file scope, and no others:
#
#   - a PREPROCESSOR DIRECTIVE at file scope, with its line continuations. The #include lines
#     there are C10's and the conditionals are C3's; what is left is the include guard and the
#     per-module #define - UTF_TEST_MODULE above all - which a new module's entry point must
#     write fresh, so hashing them reds the very operation this tool exists to verify. A
#     multi-line #define at file scope is therefore unhashed in full, and tier 3 is what stands
#     behind UTF_TEST_MODULE: renaming it registers a different master suite
#
#   - a COMMENT BLOCK at file scope standing on its own, which is module-level prose rather than
#     evidence about a relocation. Writing one is part of creating a module: the real split
#     f992e2f wrote four, and C11 reported all four before this was measured. A comment which
#     documents a declaration sits against it with no blank line and is hashed with it
#
# Blank lines between spans, and trailing whitespace, are outside every hash too, by normalize( )
#
# A case RELOCATED into a different file is deliberately not judged on includes, because a split
# writes new headers with their own include blocks and a rule that fired on that would fire on
# every legitimate split. That limit is written down because the claim here used to be broader
# than the checks: it said "the compilation context that text sees is unchanged", which covers
# includes, and for three invariants' worth of history nothing read them at all
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
# C8 runs in one direction only - it resolves every recipe to a case, and says nothing about a case
# with no recipe. That asymmetry is not a detail: a recipe deleted from notes.txt, or a case landed
# without one, leaves tier 1 green, and one was found by a lane re-capturing by hand rather than by
# the gate. C9 is the other direction, and it cannot simply be "every case has a recipe" because
# 481 of the 1075 cases in this tree have none and always have - notes.txt is a curated list of the
# hard-to-reproduce ones in most modules, not an index
#
# So the requirement is opt-in, and the opt-in is already written in the tree: fifteen notes.txt
# files open with "each slice appends the recipes for the cases it lands here", and every one of
# those fifteen modules does have a recipe for each of its cases. The converse does not hold and
# the check does not need it: eighteen modules are complete, and utf_baselib_h2profiles (14 cases),
# utf_baselib_messaging4 (1) and utf_baselib_setprio (1) are complete without declaring it. The
# property C9 rests on is that the declared modules are a subset of the complete ones - a module
# which says that is making a claim C9 can hold it to, and a module which says nothing is simply
# not asked. "notes-index: complete" is accepted as well, so that a module outside that feature's
# vocabulary can make the same claim in its own words
#
# The declaration cannot be dropped to escape the check - withdrawing it is itself a C9 failure
# against a baseline which recorded it, which is also what keeps this prose matcher honest: any
# rewording that stops matching reports as a withdrawal rather than silently switching C9 off
#

NOTES_INDEX_RE = re.compile(
    r'^\s*#.*(?:appends the recipes for the cases it lands here|notes-index:\s*complete)',
    re.IGNORECASE
    )

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
# C11 runs the very same split over what is left of a file once every span another invariant reads
# has been blanked out of it - the case bodies with their doc comments, the namespace blocks, and
# the preprocessor directives. Blanking rather than cutting is what keeps a file-scope helper with
# a #if in its body one member instead of three, and keeps every residue line number the file's own
#

DIRECTIVE_RE = re.compile( r'^\s*#' )

COMMENT_LINE_RE = re.compile( r'^\s*(?://|/\*|\*)' )

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


def blank( shadow, first, last ):
    """
    Blank a span of the file-scope residue copy, so C11 does not hash what another invariant reads
    """

    for index in range( max( first, 0 ), min( last, len( shadow ) - 1 ) + 1 ):
        shadow[ index ] = ''


def is_prose( shadow, first, last ):
    """
    True when every line of a file-scope span is comment text - C11 hashes declarations, not prose

    A standalone comment block at file scope is module-level prose, and writing one is part of
    creating a module rather than evidence about one: the real split f992e2f wrote four - the
    explanatory block at the head of each new Utf<Name>Main.cpp and one in a new forwarding
    translation unit - and C11 reported every one of them before this was measured. A comment
    which documents a declaration sits against it with no blank line between, so it is part of
    that declaration's span and stays hashed; only a block standing on its own is dropped
    """

    return all( COMMENT_LINE_RE.match( line )
                for line in shadow[ first : last + 1 ] if line.strip() )


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

    #
    # The preprocessor directives are blanked here, ahead of the walk, so that nothing about the
    # walk itself changes - the alternative was a new branch inside it, which would have had to
    # get the data literal collection right as well. The walk blanks the two spans it alone knows:
    # a case with its doc comment, and a namespace block
    #

    shadow = list( lines )

    probe = 0

    while probe < total:

        if DIRECTIVE_RE.match( lines[ probe ] ):
            while probe < total and lines[ probe ].rstrip().endswith( '\\' ):
                shadow[ probe ] = ''
                probe += 1
            blank( shadow, probe, probe )

        probe += 1

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

            if doc:
                blank( shadow, doc[ 0 ], doc[ 1 ] )

            blank( shadow, index, end )

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

            blank( shadow, index, end )

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

    file_members = [ {
        'module': module,
        'file': rel_path,
        'line': first + 1,
        'sha': sha( normalize( shadow[ first : last + 1 ] ) ),
        'label': shadow[ first ].strip()[ : 60 ],
        } for first, last in split_members( shadow, 0, total )
        if not is_prose( shadow, first, last ) ]

    return ( cases, namespaces, members, file_members, includes,
             sorted( data_refs ), sorted( data_literals ) )


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

    manifest = { 'cases': [], 'namespaces': [], 'members': [], 'file_members': [], 'modules': {} }
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

                ( cases, namespaces, members, file_members, includes,
                  refs, literals ) = scan_file( path, module, rel_path, problems )

                manifest[ 'cases' ].extend( cases )
                manifest[ 'namespaces' ].extend( namespaces )
                manifest[ 'members' ].extend( members )
                manifest[ 'file_members' ].extend( file_members )
                data_refs.update( refs )
                data_literals.update( literals )

                module_info[ 'files' ].append( { 'path': rel_path, 'includes': includes } )

        notes_path = os.path.join( module_dir, 'notes.txt' )
        notes_cases = []
        notes_index = False

        if os.path.isfile( notes_path ):
            with open( notes_path, 'r', encoding = 'utf-8', errors = 'replace' ) as stream:
                notes_text = stream.read()

            for spec in NOTES_RUN_TEST_RE.findall( notes_text ):
                for name in spec.split( ',' ):
                    name = name.strip()
                    if name and '*' not in name and '?' not in name:
                        notes_cases.append( name )

            notes_index = any( NOTES_INDEX_RE.match( line ) for line in notes_text.split( '\n' ) )

        module_info[ 'notes_cases' ] = sorted( set( notes_cases ) )
        module_info[ 'notes_index' ] = notes_index

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
    manifest[ 'file_members' ].sort( key = lambda m: ( m[ 'module' ], m[ 'file' ], m[ 'line' ] ) )

    return manifest, problems


def index_cases( manifest ):
    return { case[ 'name' ]: case for case in manifest[ 'cases' ] }


def recipe_owners( manifest ):
    """
    Case name -> the set of modules whose notes.txt carries a --run_test recipe for it

    Ownership is tree wide on purpose. A case that moves to another module with its recipe
    following it has lost nothing, and C8 already reports a recipe left behind in the module the
    case departed - so C9 asks only whether some notes.txt still names it
    """

    owners = {}

    for module, info in manifest[ 'modules' ].items():
        for name in info.get( 'notes_cases', [] ):
            owners.setdefault( name, set() ).add( module )

    return owners


def file_includes( manifest ):
    """
    File path -> its #include list, across every module

    Paths carry the module directory and so are unique tree wide, which is what lets a file be
    matched between two manifests without also matching on the module
    """

    includes = {}

    for info in manifest[ 'modules' ].values():
        for entry in info[ 'files' ]:
            includes[ entry[ 'path' ] ] = entry.get( 'includes', [] )

    return includes


def module_file_churn( before, after ):
    """
    Module -> the module-relative paths of the files this change added to or removed from it

    Every module's entry point is a roster: Utf<Name>Main.cpp carries one quoted include per
    header the module holds, so moving a header between modules MUST edit it. That edit is the
    relocation itself rather than evidence of one, which is what C10 exempts
    """

    churn = {}

    for module in set( before[ 'modules' ] ) | set( after[ 'modules' ] ):

        was = { entry[ 'path' ]
                for entry in before[ 'modules' ].get( module, {} ).get( 'files', [] ) }

        now = { entry[ 'path' ]
                for entry in after[ 'modules' ].get( module, {} ).get( 'files', [] ) }

        prefix = module + '/'

        churn[ module ] = { path[ len( prefix ) : ]
                            for path in was ^ now if path.startswith( prefix ) }

    return churn


def names_a_moved_file( include, moved ):
    """
    True when a quoted include names one of the files this change moved into or out of the module

    Only a quoted include can name a sibling header, so an <angle> include added to a roster is
    judged exactly as it would be anywhere else. The spelling must match the module-relative
    path: an unexpected one stays judged rather than exempted by guesswork, because the safe
    direction for a gate is to fire
    """

    if len( include ) < 2 or not include.startswith( '"' ) or not include.endswith( '"' ):
        return False

    return include[ 1 : -1 ].replace( '\\', '/' ) in moved


def unreferenced_data_files( info ):
    """
    The data files one module carries which nothing in that module names

    Both the resolved call sites and the bare filename literals count as a reference, exactly as
    C7's intrinsic half treats them, so a file reached through a helper parameter is not called
    an orphan
    """

    named = set( info.get( 'data_refs', [] ) ) | set( info.get( 'data_literals', [] ) )

    return { name for name in info.get( 'data_files', {} ) if name not in named }


def check_intrinsic( manifest ):
    """
    Invariants that hold of a single manifest on its own: C5 to C9
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

    #
    # C9 intrinsic half - a module which declares its notes.txt a complete index must name every
    # one of its own cases there
    #
    # This is the half that survives a baseline refresh, and it is the one that catches the live
    # risk: a case landing in one of the declared modules without the recipe its neighbours all
    # have. A differential check cannot do that, because a newly added case has no recipe in the
    # baseline to lose and a refresh would bless the gap
    #

    own_by_module = {}

    for case in manifest[ 'cases' ]:
        own_by_module.setdefault( case[ 'module' ], [] ).append( case[ 'name' ] )

    for module, info in sorted( manifest[ 'modules' ].items() ):

        if not info.get( 'notes_index' ):
            continue

        indexed = set( info.get( 'notes_cases', [] ) )

        for name in sorted( own_by_module.get( module, [] ) ):
            if name not in indexed:
                failures.append(
                    'C9 module %s declares its notes.txt a complete index but has no --run_test '
                    'recipe for case %s' % ( module, name )
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
    Invariants that relate two manifests: C1 to C4 and C10, plus the C6, C7 and C9 no-loss halves

    C7 and C10 read manifest[ 'modules' ], which is worth saying because for a long time nothing
    here did: the whole per-module half was captured on every run and compared by nothing, so
    every differential claim this gate made was about cases and members alone
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
    # C10 - the #include list of every file present on both sides
    #
    # Includes are the third part of a case's compilation context, beside its guard stack and its
    # namespace stack, and they were the part no check read. An include added to a header full of
    # live cases changes what every one of them compiles against while leaving C1 to C4 green,
    # because nothing about the case text itself moved
    #
    # The comparison is per file rather than per case, and that is the whole of its scope. A case
    # which moved to another file is judged on text, guards and namespaces only - a split writes
    # new headers with their own include blocks, so judging a moved case on the include list of
    # its new home would fire on every legitimate relocation, which is the one thing this gate
    # cannot afford. A file added or removed is likewise not judged
    #
    # And a quoted include naming a file this change added to or removed from the same module is
    # not judged either, for exactly the reason that file itself is not: every module's
    # Utf<Name>Main.cpp is a roster of one quoted include per header it holds, so a relocation
    # MUST edit it. Judging that edit fires on the very operation this tool exists to verify -
    # measured on f992e2f, the real four-way messaging split, which C10 reported as a violation
    # before this clause existed
    #
    # It is a narrow exemption and everything around it stays live. An <angle> include added to a
    # roster still fires. A quoted include DROPPED while the header it names stays in the module
    # still fires, and that is the case worth having: a header cut from the roster unregisters
    # every case in it while the manifest still finds them, so C1 stays green - the exact failure
    # mode this tool was built for
    #
    # Order is part of the comparison, on whatever survives the exemption. An include can depend
    # on one before it, and in these rosters include order is registration order is run order:
    # MessagingUtils_TokenTypeConcurrencyTests needs a cold process-global cache and is neutered,
    # while still passing, by any case that runs first and warms it
    #

    old_includes = file_includes( before )
    new_includes = file_includes( after )
    churn = module_file_churn( before, after )

    if not any( old_includes.values() ):
        failures.append(
            'C10 the baseline carries no #include lists - it predates the include capture and '
            'must be regenerated from its own commit before this invariant can be trusted'
            )
    else:
        for path in sorted( set( old_includes ) & set( new_includes ) ):

            moved = churn.get( path.split( '/' )[ 0 ], set() )

            was = [ entry for entry in old_includes[ path ]
                    if not names_a_moved_file( entry, moved ) ]

            now = [ entry for entry in new_includes[ path ]
                    if not names_a_moved_file( entry, moved ) ]

            if was == now:
                continue

            added = [ entry for entry in now if entry not in was ]
            removed = [ entry for entry in was if entry not in now ]

            if added or removed:
                failures.append(
                    'C10 file INCLUDES CHANGED: %s (added %s, removed %s)'
                    % ( path, ', '.join( added ) or 'nothing', ', '.join( removed ) or 'nothing' )
                    )
            else:
                failures.append( 'C10 file INCLUDES REORDERED: %s' % path )

    #
    # C7's differential half - the content of every data file, and the data file nothing names
    #
    # C7 was intrinsic only: it asked whether a referenced file exists and whether two modules'
    # copies of one name agree, and both questions are answered inside a single manifest. Neither
    # notices a data file whose CONTENT changed - the tests then read different input and pass or
    # fail for a reason no invariant reports. Changing every copy of a shared file identically
    # defeats the divergence check too, which is why that is not accidental cover
    #
    # The other direction of the existence check is the orphan: a module keeps a data file whose
    # last reference has moved away. That cannot be intrinsic tree-wide, because this tree carries
    # four such files today and always has, and a rule red on legitimate state is worse than the
    # blind spot it closes. So for a module the baseline already knew it is differential and
    # narrow - a data file unreferenced now which was not in that state in the baseline, whether
    # it lost its last reference or arrived without one
    #
    # A module the baseline does NOT carry is judged INTRINSICALLY instead, because there is no
    # earlier state to grandfather against: a new module carrying a data file it never names is
    # carrying it by accident. That is not hypothetical - the f992e2f split gave the new
    # utf_baselib_messaging3 a copy of async_rpc_response_with_exception.json which nothing in it
    # names, and skipping new modules is exactly why that went unreported while the same split's
    # two leftovers in utf_baselib_messaging were caught
    #
    # A data file added or removed outright is not judged for content. A split moves a data file
    # with the cases that read it, and C7's intrinsic half already reports the module left
    # referencing one it no longer carries
    #

    if ( any( info.get( 'data_files' ) for info in after[ 'modules' ].values() )
         and not any( info.get( 'data_files' ) for info in before[ 'modules' ].values() ) ):
        failures.append(
            'C7 the baseline carries no data file hashes - it predates the content capture and '
            'must be regenerated from its own commit before this invariant can be trusted'
            )

    for module, info in sorted( after[ 'modules' ].items() ):

        #
        # an empty dict rather than a skip: it carries no data file hashes, so nothing is judged
        # on content, and it grandfathers no orphan, so a new module is judged intrinsically
        #
        was = before[ 'modules' ].get( module, {} )

        for name, digest in sorted( info.get( 'data_files', {} ).items() ):

            previous = was.get( 'data_files', {} ).get( name )

            if previous is not None and previous != digest:
                failures.append(
                    'C7 data file CONTENT CHANGED: %s/data/%s (%s -> %s)'
                    % ( module, name, previous, digest )
                    )

        for name in sorted( unreferenced_data_files( info ) - unreferenced_data_files( was ) ):
            failures.append(
                'C7 data file NEWLY UNREFERENCED: %s/data/%s - %s' % (
                    module, name,
                    'that module is new and nothing in it names this file' if not was
                    else 'nothing in that module names it any more'
                    )
                )

    #
    # The no-loss half of C9 - a case which exists on both sides and had a recipe must still have
    # one. This is the direction C8 never looked in, and it is the only C9 coverage the thirty-one
    # modules which declare nothing have, since the intrinsic half says nothing about them
    #
    # It is placed before the C6 section deliberately, because that section returns early when a
    # baseline carries no members and C9 must not be skipped along with it
    #

    old_recipes = recipe_owners( before )
    new_recipes = recipe_owners( after )

    for name in sorted( set( old ) & set( new ) ):
        if name in old_recipes and name not in new_recipes:
            failures.append(
                'C9 case RECIPE LOST: %s - notes.txt of %s named it, none does now'
                % ( name, ', '.join( sorted( old_recipes[ name ] ) ) )
                )

    #
    # A declaration withdrawn is a check switched off, so it has to be reported rather than
    # obeyed. A baseline captured before notes_index existed records nothing here, and then this
    # half is simply not in force - which main( ) says out loud rather than leaving implied
    #

    for module, info in sorted( before[ 'modules' ].items() ):

        if not info.get( 'notes_index' ):
            continue

        current = after[ 'modules' ].get( module )

        if current is not None and not current.get( 'notes_index' ):
            failures.append(
                'C9 module %s WITHDREW its notes.txt complete-index declaration - the C9 '
                'completeness check no longer applies to it' % module
                )

    #
    # C11 - the file-scope residue, which is everything outside every column-0 namespace block
    #
    # C6 extracts helper members from inside such a block only, so text at file scope was hashed
    # by nothing at all - and utf_baselib_loader's three fixtures live exactly there. Measured
    # before this check existed: a member injected into ManifestFixture, which three
    # UTF_FIXTURE_TEST_CASEs are fixtured on, and a changed signature on a column-0 static helper
    # BOTH passed tier 1
    #
    # The identity and the direction are C6's, for C6's reasons. Text alone, so a fixture that
    # moves to another header with the cases it fixtures is a move rather than a loss; tree wide,
    # so a move between modules is one too; and both ways, so an invented file-scope helper is
    # reported exactly as C1 reports an invented case
    #
    # What C11 does NOT ask is whether the same file-scope text occurs twice within one module,
    # and that is a decision rather than an omission. There is nothing there to catch: every
    # header of a module is included into one translation unit, so a real redefinition at file
    # scope does not compile, and C6's duplication half exists because a helper inside a namespace
    # CAN be copied without a diagnostic. What does legitimately repeat is "using namespace bl;",
    # which opens two module entry points today and would open a third
    #
    # It sits before the C6 section for the reason C9's no-loss half does: that section returns
    # early when a baseline carries no members, and C11 must not be skipped along with it
    #

    if 'file_members' in before:

        if not before[ 'file_members' ]:
            failures.append(
                'C11 the baseline carries an EMPTY file-scope member list - every real tree has '
                'at least one per file, so this baseline is broken rather than merely old'
                )

        elif not after.get( 'file_members' ):
            failures.append(
                'C11 the current manifest carries no file-scope text - extraction failed'
                )

        else:

            old_scope = {}

            for member in before[ 'file_members' ]:
                old_scope.setdefault( member[ 'sha' ], member )

            new_scope = {}

            for member in after[ 'file_members' ]:
                new_scope.setdefault( member[ 'sha' ], member )

            for digest in sorted( set( old_scope ) - set( new_scope ) ):
                where = old_scope[ digest ]
                failures.append(
                    'C11 file-scope text LOST: %s (%s:%d)'
                    % ( where[ 'label' ], where[ 'file' ], where[ 'line' ] )
                    )

            for digest in sorted( set( new_scope ) - set( old_scope ) ):
                where = new_scope[ digest ]
                failures.append(
                    'C11 file-scope text ADDED: %s (%s:%d)'
                    % ( where[ 'label' ], where[ 'file' ], where[ 'line' ] )
                    )

    #
    # The no-loss half of C6, checked per member rather than per block. A helper is lost only if
    # its text survives nowhere in the tree; a block that was partitioned, or a helper hoisted
    # into a different namespace, is a move and reads as one
    #
    # Comparing text alone is what makes a hoist a move. The namespace path is deliberately not
    # part of this identity - it is used only by the duplication check above
    #
    # It runs in BOTH directions, because looking one way is how C8 hid a lost recipe for as long
    # as it did. A relocation invents no helper any more than it invents a case, so a member that
    # exists only on the new side is reported exactly as C1 reports a case that does - and a slice
    # which legitimately adds one refreshes the baseline, which is already the workflow here
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

    new_members = {}

    for member in after[ 'members' ]:
        new_members.setdefault( member[ 'sha' ], member )

    for digest in sorted( set( old_members ) - set( new_members ) ):
        where = old_members[ digest ]
        failures.append(
            'C6 helper member LOST: %s (%s:%d)' % ( where[ 'label' ], where[ 'file' ], where[ 'line' ] )
            )

    for digest in sorted( set( new_members ) - set( old_members ) ):
        where = new_members[ digest ]
        failures.append(
            'C6 helper member ADDED: %s (%s:%d)' % ( where[ 'label' ], where[ 'file' ], where[ 'line' ] )
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

    #
    # State C9's rule where a reader of a green run will see it, because what it does NOT require
    # is the whole reason it is not noisy - and an unstated scope is how C8's one-wayness went
    # unnoticed in the first place
    #

    declared = { module for module, info in manifest[ 'modules' ].items() if info.get( 'notes_index' ) }
    gated = sum( 1 for case in manifest[ 'cases' ] if case[ 'module' ] in declared )

    print( 'utf_inventory: C9 requires a recipe for every case of the %d module(s) whose notes.txt '
           'declares itself a complete index (%d of %d cases); elsewhere it requires only that no '
           'case loses a recipe it had' % ( len( declared ), gated, len( manifest[ 'cases' ] ) ) )

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

        if not any( 'notes_index' in info for info in before[ 'modules' ].values() ):
            print( 'utf_inventory: C9 - this baseline predates the index declaration, so the '
                   'no-withdrawal half is not in force until it is refreshed' )

        #
        # The same reasoning as C9's roster line, for the differential half: what a check does not
        # look at is exactly what a reader of a green run needs told, and leaving it implied is
        # how the per-module half went uncompared for as long as it did
        #

        old_paths, new_paths = set( file_includes( before ) ), set( file_includes( manifest ) )

        moved_files = sum( len( names ) for names in module_file_churn( before, manifest ).values() )

        print( 'utf_inventory: C10 compares the #include list of the %d file(s) present in both '
               'manifests, order included (%d added and %d removed by this change are not judged); '
               'a case that moved between files is judged on text, guards and namespaces only'
               % ( len( old_paths & new_paths ), len( new_paths - old_paths ),
                   len( old_paths - new_paths ) ) )

        print( 'utf_inventory: C10 exempts a quoted include naming one of the %d file(s) this '
               'change moved into or out of its own module - a roster edit IS the relocation; a '
               'dropped include whose header stayed, and any <angle> include, still fire'
               % moved_files )

        shared_data = sum(
            len( set( info.get( 'data_files', {} ) )
                 & set( before[ 'modules' ].get( module, {} ).get( 'data_files', {} ) ) )
            for module, info in manifest[ 'modules' ].items()
            )

        accepted = sum(
            len( unreferenced_data_files( info ) ) for info in before[ 'modules' ].values()
            )

        fresh_modules = sorted( set( manifest[ 'modules' ] ) - set( before[ 'modules' ] ) )

        print( 'utf_inventory: C7 compares the content of the %d data file(s) present in both '
               'manifests and reports one nothing names any more; the %d already unreferenced in '
               'the baseline stay accepted' % ( shared_data, accepted ) )

        print( 'utf_inventory: C7 grandfathers an orphan only in a module the baseline carries - '
               'the %d module(s) new in this change are judged intrinsically, since a new module '
               'has no earlier state to be accepted against%s'
               % ( len( fresh_modules ),
                   ' (%s)' % ', '.join( fresh_modules ) if fresh_modules else '' ) )

        print( 'utf_inventory: C6 reports a helper member ADDED as well as one LOST, over %d '
               'member(s) - a relocation invents neither, so a slice which adds one on purpose '
               'refreshes the baseline, exactly as C1 already requires for a new case'
               % len( manifest.get( 'members', [] ) ) )

        #
        # The same reasoning once more: a check whose scope is not printed is a check a reader of
        # a green run cannot size. C11's scope has two halves worth stating - what it reads, and
        # the one thing at file scope it deliberately does not
        #

        if 'file_members' not in before:
            print( 'utf_inventory: C11 - this baseline predates the file-scope capture, so text '
                   'outside every namespace block is judged by nothing until it is refreshed' )
        else:
            print( 'utf_inventory: C11 compares the %d span(s) of file-scope text in %d file(s) - '
                   'what is left once the cases, the namespace blocks and the preprocessor lines '
                   'are taken out - by text alone, tree wide and in both directions, exactly as '
                   'C6 does inside a namespace'
                   % ( len( manifest.get( 'file_members', [] ) ),
                       len( { member[ 'file' ] for member in manifest.get( 'file_members', [] ) } ) ) )

            print( 'utf_inventory: C11 exempts a preprocessor directive at file scope with its '
                   'continuations - includes are C10\'s, conditionals C3\'s, and the include guard '
                   'and per-module #define are what a new module writes fresh - and a comment '
                   'block standing on its own, which is module-level prose a split must write; a '
                   'comment against a declaration is part of it and stays judged' )

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
