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
# utf_ppstream.py - prove that an edit to a header changed nothing any translation unit sees
#
# Some changes are refactors of a macro or a header which every translation unit in the project
# pulls in. Reviewing them is unreliable and running the tests only samples them: a test suite
# enumerates behaviours, while what such an edit puts at risk is everything the macro expands to -
# the order of its catch clauses, which of them classifies an exception, the scope of a lock
#
# The proof is mechanical instead. Both revisions are preprocessed, translation unit by
# translation unit, with the commands the makefiles themselves would use, and the two token
# streams must be equal. If they are, the compiler sees the same program on both sides, so the
# change is behaviour preserving by construction rather than by inspection
#
# The technique has precedent here - src/utests/AGENTS.md uses "the preprocessed translation unit
# is unchanged" as the gate for splitting a test header
#
# ONE difference is expected and it is not a difference in meaning. Moving the lines of a header
# moves the __LINE__ of every macro expanded BELOW the edit IN THAT HEADER - BL_EXCEPTION records
# it (core/ErrorHandling.h:50), and so do BL_ASSERT, BL_THROW and the logging macros. Those are
# integer literals and they all shift by the same amount. So --shift-file names the edited header
# and the comparison accepts a decimal integer literal which
#
#   - is attributed by the line markers to that file, on BOTH sides, and
#   - differs by one single amount, the same for every such literal in the whole run
#
# Everything else must be identical, including integer literals from any other file. The shift
# amount is not supplied, it is discovered from the first difference and then enforced
#
# Two properties make the result trustworthy:
#
#   - the translation unit set is taken from `make -n`, so it is what the build actually compiles
#     (apps and test modules alike) and not a list maintained by hand, and both sides must offer
#     the same set
#   - every failure mode is a FAIL. A raw string literal, a line structure the tokenizer cannot
#     align, a preprocessor error - none of them can produce a PASS
#
# Usage:
#
#   utf_ppstream.py --before HEAD~1 --after .
#   utf_ppstream.py --before HEAD~1 --after . --shift-file src/include/baselib/tasks/TaskBase.h
#   utf_ppstream.py --before abc1234 --after def5678 --only utf_baselib_tasks
#
# A revision is exported with `git archive`, so the working tree is never touched; `.` (or any
# directory) is used where it stands
#
# Stdlib only, by design - it must run on the devenv7 dist interpreter, which is an embeddable
# build with no venv and no pip
#

from __future__ import print_function

import argparse
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile

#
# A line marker of the form: # 42 "some/file.h" 2
#
# Everything else which begins with '#' in preprocessed output is a #pragma, which is part of the
# stream and is tokenized like any other line
#

LINE_MARKER_RE = re.compile( r'^#\s+\d+\s+"((?:[^"\\]|\\.)*)"' )

#
# The punctuators, longest first, so that '<<=' never tokenizes as '<' '<' '='
#

PUNCTUATORS = [
    '<<=', '>>=', '...', '->*', '<=>',
    '::', '->', '.*', '++', '--', '<<', '>>', '<=', '>=', '==', '!=', '&&', '||',
    '+=', '-=', '*=', '/=', '%=', '&=', '^=', '|=', '##',
    '{', '}', '[', ']', '(', ')', ';', ':', '?', '.', ',', '+', '-', '*', '/', '%',
    '^', '&', '|', '~', '!', '=', '<', '>', '#',
    ]

TOKEN_RE = re.compile(
    r'(?P<raw>(?:u8|u|U|L)?R")'
    r'|(?P<str>(?:u8|u|U|L)?"(?:[^"\\]|\\.)*")'
    r"|(?P<chr>(?:u8|u|U|L)?'(?:[^'\\]|\\.)*')"
    r'|(?P<num>\.?[0-9](?:[eEpP][-+]|\'[0-9A-Za-z_]|[0-9A-Za-z_.])*)'
    r'|(?P<name>[A-Za-z_$][A-Za-z0-9_$]*)'
    r'|(?P<punct>' + '|'.join( re.escape( p ) for p in PUNCTUATORS ) + r')'
    r'|(?P<other>\S)'
    )

DECIMAL_RE = re.compile( r'^[0-9]+$' )

#
# The compile line the pattern rule of projects/make/toolchain/gcc-default.mk emits
#

COMPILE_RE = re.compile( r'^(?P<argv>.*\s-c\s+(?P<source>\S+\.cpp)\s+-o\s+(?P<object>\S+\.o))\s*$' )


class RawStringFound( Exception ):
    """
    Raised where a raw string literal is met - see the header comment
    """


def repo_root():
    return os.path.normpath( os.path.join( os.path.dirname( os.path.abspath( __file__ ) ), '..', '..' ) )


def normalize_path( path ):
    """
    The path as the line markers spell it, reduced to something comparable with a repo relative
    path - the markers carry './src/...' for project headers because that is how -I spells them
    """

    path = path.replace( '\\\\', '\\' ).replace( '\\"', '"' )
    path = path.replace( os.sep, '/' )

    while path.startswith( './' ):
        path = path[ 2 : ]

    return path


def tokenize( text ):
    """
    The tokens of one line of preprocessed output, in order

    Whitespace is dropped - it never distinguishes two programs at this stage, and the two sides
    are free to lay the same tokens out differently
    """

    tokens = []

    for match in TOKEN_RE.finditer( text ):

        if match.lastgroup == 'raw':
            raise RawStringFound()

        tokens.append( match.group() )

    return tokens


class Stream( object ):
    """
    One side of the comparison: the preprocessed output of one translation unit, as a token
    stream which remembers which file each token came from
    """

    def __init__( self, handle ):

        self._handle = handle

        self.current_file = '<unknown>'
        self.pending = []
        self.lines = 0

    def next_line( self ):
        """
        The next line which is not a line marker, or None at the end of the stream
        """

        for line in self._handle:

            if line.startswith( '#' ):

                match = LINE_MARKER_RE.match( line )

                if match:
                    self.current_file = normalize_path( match.group( 1 ) )
                    continue

            self.lines += 1

            return line

        return None

    def push_line( self, line ):

        for token in tokenize( line ):
            self.pending.append( ( token, self.current_file ) )

    def refill( self ):
        """
        Appends the tokens of the next content line; False at the end of the stream

        Note that a line can be blank or all whitespace, in which case it contributes nothing and
        the caller has to keep pulling - which is what makes the comparison a token stream
        comparison rather than a line by line one
        """

        line = self.next_line()

        if line is None:
            return False

        self.push_line( line )

        return True


def compare_streams( before, after, shift_file, shift ):
    """
    Walks the two token streams in lockstep

    Returns ( failures, shift, compared, shifted ) where shift is the amount carried in and out,
    so that one single amount is enforced across every translation unit of the run
    """

    failures = []
    compared = 0
    shifted = 0

    while True:

        if not before.pending and not after.pending:

            #
            # Both sides sit on a line boundary, so two identical lines tokenize identically and
            # can be skipped without tokenizing them at all. This is what makes the run finish in
            # minutes rather than hours - almost every line of almost every unit is untouched
            #

            line_before = before.next_line()
            line_after = after.next_line()

            if line_before is None and line_after is None:
                break

            if line_before is None or line_after is None:
                failures.append(
                    'one side ended first - before at line %d, after at line %d'
                    % ( before.lines, after.lines )
                    )
                break

            if line_before == line_after:
                continue

            before.push_line( line_before )
            after.push_line( line_after )

            continue

        if not before.pending:
            if not before.refill():
                failures.append( 'the before side ended with %d token(s) left over' % len( after.pending ) )
                break
            continue

        if not after.pending:
            if not after.refill():
                failures.append( 'the after side ended with %d token(s) left over' % len( before.pending ) )
                break
            continue

        token_before, file_before = before.pending.pop( 0 )
        token_after, file_after = after.pending.pop( 0 )

        compared += 1

        if token_before == token_after:
            continue

        allowed = (
            shift_file is not None and
            file_before == shift_file and
            file_after == shift_file and
            DECIMAL_RE.match( token_before ) and
            DECIMAL_RE.match( token_after )
            )

        if allowed:

            delta = int( token_after ) - int( token_before )

            if shift is None:
                shift = delta

            if delta == shift:
                shifted += 1
                continue

            failures.append(
                'integer literal in %s shifted by %d where every other one shifted by %d (%s -> %s)'
                % ( shift_file, delta, shift, token_before, token_after )
                )

            break

        failures.append(
            'token %d differs: %s (%s) -> %s (%s)'
            % ( compared, token_before, file_before, token_after, file_after )
            )

        break

    return failures, shift, compared, shifted


def materialize( spec, scratch, label ):
    """
    A directory holding the tree to preprocess - the spec as it stands if it names one, otherwise
    a `git archive` export of that revision
    """

    if os.path.isdir( spec ):
        return os.path.abspath( spec )

    target = os.path.join( scratch, label )

    os.mkdir( target )

    export = subprocess.Popen(
        [ 'git', 'archive', '--format=tar', spec ],
        cwd = repo_root(),
        stdout = subprocess.PIPE,
        )

    extract = subprocess.Popen( [ 'tar', '-x', '-C', target ], stdin = export.stdout )

    export.stdout.close()
    extract.communicate()

    if export.wait() != 0 or extract.returncode != 0:
        raise RuntimeError( 'could not export revision %s' % spec )

    #
    # The CI environment descriptor is deliberately not in git (.gitignore), and common.mk refuses
    # to run without it. It describes the machine, not the revision, so the working copy of it is
    # carried into both exports - which is what makes the two sides differ in source only
    #

    for local in ( os.path.join( 'projects', 'make', 'ci-init-env.mk' ), ):

        source = os.path.join( repo_root(), local )

        if os.path.isfile( source ):
            shutil.copyfile( source, os.path.join( target, local ) )

    return target


def compile_commands( tree, toolchain, variant ):
    """
    The compile command of every translation unit the makefiles build, keyed by source path

    -n asks make what it would run and -B stops it from omitting a unit whose object happens to be
    up to date, so the answer does not depend on the state of the build tree
    """

    argv = [
        'make', '-n', '-B', '-k', 'all',
        'TOOLCHAIN=%s' % toolchain,
        'VARIANT=%s' % variant,
        ]

    process = subprocess.Popen(
        argv,
        cwd = tree,
        stdout = subprocess.PIPE,
        stderr = subprocess.PIPE,
        universal_newlines = True,
        )

    out, err = process.communicate()

    if process.returncode != 0:
        raise RuntimeError( 'make -n failed in %s:\n%s' % ( tree, err ) )

    commands = {}

    for line in out.splitlines():

        match = COMPILE_RE.match( line.strip() )

        if not match:
            continue

        commands[ match.group( 'source' ) ] = shlex.split( match.group( 'argv' ) )

    return commands


def preprocess_argv( argv ):
    """
    The same command, preprocessing to stdout instead of compiling to an object

    The dependency generation flags go too - with no -o they would scatter .d files around the
    tree and they contribute nothing to the token stream
    """

    result = []
    skip = False

    for index, item in enumerate( argv ):

        if skip:
            skip = False
            continue

        if item in ( '-MMD', '-MP', '-MD' ):
            continue

        if item == '-o':
            skip = True
            continue

        if item == '-c':
            result.append( '-E' )
            continue

        result.append( item )

    return result


def compare_unit( source, argv_before, tree_before, argv_after, tree_after, shift_file, shift, errors_dir ):

    handles = []
    processes = []

    try:

        for label, argv, tree in (
            ( 'before', argv_before, tree_before ),
            ( 'after', argv_after, tree_after ),
            ):

            stderr = open( os.path.join( errors_dir, '%s.err' % label ), 'w+' )
            handles.append( stderr )

            processes.append(
                subprocess.Popen(
                    preprocess_argv( argv ),
                    cwd = tree,
                    stdout = subprocess.PIPE,
                    stderr = stderr,
                    universal_newlines = True,
                    )
                )

        before = Stream( processes[ 0 ].stdout )
        after = Stream( processes[ 1 ].stdout )

        try:
            failures, shift, compared, shifted = compare_streams( before, after, shift_file, shift )
        except RawStringFound:
            return (
                [ 'a raw string literal was met - this tool does not tokenize them, extend it' ],
                shift,
                0,
                0,
                )

        stopped_early = bool( failures )

        for process in processes:

            #
            # The comparison stops at the first difference, so the preprocessor can still be
            # writing; close its pipe and let it go rather than waiting for output nobody reads
            #

            process.stdout.close()

            if process.poll() is None:
                process.terminate()

            process.wait()

        #
        # An exit code only means something where the whole stream was read. Where the comparison
        # stopped early the child was killed, or was cut off by the closed pipe, and the
        # difference which was already found is the finding
        #

        if not stopped_early:

            for index, process in enumerate( processes ):

                if process.returncode != 0:

                    handles[ index ].seek( 0 )

                    failures.append(
                        'the preprocessor failed (exit %d):\n%s'
                        % ( process.returncode, handles[ index ].read()[ -2000 : ] )
                        )

        return failures, shift, compared, shifted

    finally:

        for handle in handles:
            handle.close()


def main():

    parser = argparse.ArgumentParser(
        description = 'prove that two revisions preprocess to the same token stream'
        )

    parser.add_argument( '--before', required = True, help = 'the baseline revision, or a directory' )
    parser.add_argument( '--after', required = True, help = 'the revision under test, or a directory' )
    parser.add_argument( '--toolchain', default = 'clang2010', help = 'the TOOLCHAIN to ask make about' )
    parser.add_argument( '--variant', default = 'debug', help = 'the VARIANT to ask make about' )
    parser.add_argument(
        '--shift-file',
        metavar = 'PATH',
        help = 'the edited header whose __LINE__ literals may shift by one constant amount',
        )
    parser.add_argument( '--only', metavar = 'SUBSTR', help = 'limit the run to units whose path contains SUBSTR' )

    args = parser.parse_args()

    shift_file = normalize_path( args.shift_file ) if args.shift_file else None

    scratch = tempfile.mkdtemp( prefix = 'utf_ppstream_' )

    try:

        tree_before = materialize( args.before, scratch, 'before' )
        tree_after = materialize( args.after, scratch, 'after' )

        commands_before = compile_commands( tree_before, args.toolchain, args.variant )
        commands_after = compile_commands( tree_after, args.toolchain, args.variant )

        print( 'utf_ppstream: before %s -> %d translation unit(s)' % ( args.before, len( commands_before ) ) )
        print( 'utf_ppstream: after  %s -> %d translation unit(s)' % ( args.after, len( commands_after ) ) )

        if set( commands_before ) != set( commands_after ):

            print( '' )
            print( 'utf_ppstream: FAIL - the two revisions do not build the same translation units:' )

            for source in sorted( set( commands_before ) - set( commands_after ) ):
                print( '    only before: %s' % source )

            for source in sorted( set( commands_after ) - set( commands_before ) ):
                print( '    only after:  %s' % source )

            return 3

        sources = sorted( commands_before )

        if args.only:
            sources = [ source for source in sources if args.only in source ]

        if not sources:
            print( 'utf_ppstream: FAIL - no translation unit selected' )
            return 3

        shift = None
        failures = []
        compared = 0
        shifted = 0

        for index, source in enumerate( sources ):

            print( '    [%2d/%2d] %s' % ( index + 1, len( sources ), source ) )
            sys.stdout.flush()

            unit_failures, shift, unit_compared, unit_shifted = compare_unit(
                source,
                commands_before[ source ],
                tree_before,
                commands_after[ source ],
                tree_after,
                shift_file,
                shift,
                scratch,
                )

            compared += unit_compared
            shifted += unit_shifted

            for failure in unit_failures:
                failures.append( '%s: %s' % ( source, failure ) )

        print( '' )
        print( 'utf_ppstream: %d unit(s), %d token(s) compared beyond the identical lines' % (
            len( sources ), compared ) )

        if shift_file is not None:
            print( 'utf_ppstream: %d integer literal(s) from %s shifted by %s' % (
                shifted, shift_file, 'nothing' if shift is None else '%+d' % shift ) )

        if failures:
            print( '' )
            print( 'utf_ppstream: FAIL - %d difference(s):' % len( failures ) )
            for failure in failures:
                print( '    %s' % failure )
            return 1

        print( 'utf_ppstream: PASS - the token streams are equal' )
        return 0

    finally:

        shutil.rmtree( scratch, ignore_errors = True )


if __name__ == '__main__':
    sys.exit( main() )
