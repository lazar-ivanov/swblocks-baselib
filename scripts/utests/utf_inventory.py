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
# This script captures a manifest of every test case in src/utests and checks thirteen invariants:
#
#   C1  the set of case names is identical
#   C2  every case body and doc comment hashes the same
#   C3  every case sits under the same #if guard stack
#   C4  every case sits under the same namespace stack
#   C5  no case name occurs twice anywhere in the tree
#   C6  no helper block or member occurs twice within one module (an ODR risk); none was lost,
#       none was invented, and none changed the #if stack it sits under
#   C7  every data file a module references exists in that module's data/ directory, carries the
#       content it had, and does not become one nothing names
#   C8  every case a module's notes.txt names exists in that module
#   C9  no case loses a recipe it had, and a module which declares its notes.txt a complete
#       index really does name every one of its cases
#   C10 every file keeps the #include list it had
#   C11 no file-scope text - what sits outside every column-0 namespace block - is lost, invented,
#       or moved to a different #if stack
#   C12 a helper member which stayed in its file kept the namespace it sat in
#   C13 src/utests/include, the shared tree every module compiles against, is scanned too
#
# C2 together with C3 and C4 is the core claim about a case which stayed where it was: its text,
# the preprocessor guard stack and the namespace stack it sits under are all unchanged. C10 adds
# the file's #include list to that, and C11 the declarations the file makes at file scope: the
# fixtures of utf_baselib_loader, the column-0 statics, the BL_IID_DECLAREs, UTF_GLOBAL_FIXTURE,
# the file-scope using-directives and the behavioural #defines. That is 79 spans over 1128 lines
# in 25 files today, and for ten invariants' worth of history no hash read any of it.
# Measured before C11 existed: a member injected into ManifestFixture, which three cases are
# fixtured on, and a changed signature on a column-0 static helper BOTH passed tier 1
#
# The hash is text alone, compared tree wide, which is what makes a relocation silent - and the
# price is that a span whose exact text appears in more than one file is not protected against one
# of its copies being deleted. Two do: "using namespace bl;" in two entry points, and one
# THREAD_POOLS define in three. Counting the copies would close that and would red the new-module
# control, whose entry point legitimately writes the third "using namespace bl;" - measured
#
# C4 is the part of that core claim with nothing to say about this tree, and C12 is why that had
# to be said out loud: EVERY case in this tree sits at file scope, so C4's subject is empty and it
# protects nothing that exists. The namespace a HELPER sits in is the thing that really moves, and
# it was read by the duplication check alone - a column-0 namespace renamed passed tier 1, measured
# in a helper-only header and in one holding live cases
#
# The scan covers src/utests/utf*/ AND src/utests/include/ - C13 - so "for its text" is the only
# qualifier still needed on what follows. Within a scanned file, and for its text, what remains
# outside every hash is two named things at file scope and no others:
#
#   - a PREPROCESSOR DIRECTIVE at file scope which another invariant already reads, or which a
#     new module must write fresh, and no other. The #include lines are C10's and the conditionals
#     are C3's. Of the #define lines only two kinds go: the include guard, by its shape AND by its
#     #endif being the file's last directive, and UTF_TEST_MODULE, by its name - hashing those reds
#     the very operation this tool exists to verify, and tier 3 is what stands behind
#     UTF_TEST_MODULE, since renaming it registers a different master suite. Every other #define is
#     hashed with its continuations, which is what a blanket exclusion gave up: editing
#     UTF_TEST_NORMALIZE's body and deleting BL_PLUGINS_CLASS_IMPLEMENTATION both passed tier 1
#     before this was narrowed - and the shape half alone still gave away SSL_R_SHORT_READ and the
#     default UTF_TEST_APP_INIT_UTF_ARGS_PARSER, because define-if-not-defined has a guard's shape
#
#   - a COMMENT BLOCK at file scope every line of which opens with a comment token, which is
#     module-level prose rather than evidence about a relocation. Writing one is part of creating
#     a module: the real split f992e2f wrote four, and C11 reported all four before this was
#     measured. A comment which documents a declaration sits against it with no blank line and is
#     hashed with it
#
#     "Standing on its own" is what the rule means and "every line opens with a comment token" is
#     what it tests, which is a house-style test rather than the thing itself. Every block in this
#     tree that breaks the style is commented-out code inside a case body - 31 strictly interior
#     lines in five files, all of them C2's and none of them at file scope - so a file-scope block
#     written that way would be hashed rather than dropped
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
# --capture REFUSES to overwrite a baseline whose keys are a STRICT SUPERSET of the ones this run
# would write, and exits 4 without touching the file. That is an older tool about to disarm a check
# a newer one armed, and all it would leave behind is a printed note nobody has to read. The
# comparison is key sets alone - no schema and no contents - so a key added later needs no edit
# here, and it runs at two levels because a check's key is not always a top-level one: the guard
# stack C6 and C11 fold in is a field on a MEMBER
#
# This is PREVENTIVE, and the distinction is worth stating exactly because the obvious story is
# wrong. No baseline anywhere in this repository carried file_members or shared before adc00c8
# armed them: every one of the 30 baseline commits across every ref carries four top-level keys,
# and the four lazari2 refreshes made while C11, C12 and C13 were being built - 21b0c37, 3c57955,
# 6e97b86, fd6d80a - dropped NOTHING, because nothing was armed yet. No downgrade exists in this
# history. What does exist is a branch positioned to make one: s6r3-1 carries its own baseline
# commit, b814ed9, written on 2026-09-23 with an older tool and never merged, so a re-capture from
# it after the arming would write four keys over six
#
# It protects only against tools built from this commit onward, which is why the refresh rule is
# also written down in src/utests/AGENTS.md: an inventory.json conflict is resolved by re-capturing
# with the integrated tool, never by taking one side. Disagreement in BOTH directions is not
# refused either - that is schema evolution rather than a downgrade, and a gate which cannot tell
# them apart would block the next key this tool learns to write
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

#
# The case walk needs a stricter terminator than the namespace walk, and the difference is
# measured rather than stylistic
#
# A case body can hold a raw string literal, and one of them closes on a line reading })"; at
# column 0 - JsonPrettyPrintNestedLayout, utf_baselib_data/TestJsonAbstraction.h:2507. With ^\}
# the walk ended there, and the 32 lines to the real brace at :2539 - the #else branch,
# UTF_REQUIRE_EQUAL( pretty, expected ), verifyDeepEqual( ) and two more UTF_REQUIREs - sat
# outside C2 entirely: editing that assertion passed tier 1. It is the only such case of 1082,
# every other one ends on a bare }, so requiring one costs nothing here
#
# The namespace closers cannot take the same rule: 144 of the 165 in this tree read } // __unnamed
#

CASE_CLOSE_RE = re.compile( r'^\}\s*$' )

COND_OPEN_RE = re.compile( r'^\s*#\s*(if|ifdef|ifndef)\b\s*(.*)$' )
COND_MID_RE = re.compile( r'^\s*#\s*(elif|else)\b\s*(.*)$' )
COND_CLOSE_RE = re.compile( r'^\s*#\s*endif\b' )

#
# RESIDUE, recorded where it would bite: group( 2 ) of both patterns runs to end of line, so a
# trailing comment goes into the condition text - #else // defined( X ) would be recorded as
# 'else // defined( X )' and would not match a bare #else. Every reader of these two patterns is
# affected: C3's case guards and the member and file-scope stacks below. Measured on this tree:
# 0 of the 70 guard strings the manifest records carries a comment token, so nothing is wrong
# today; what makes it bite is someone writing a comment on the SAME line as a live conditional
#

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

    "Every line opens with a comment token" is a house-style test rather than the thing itself.
    31 strictly interior lines in five files break that style, every one commented-out code
    inside a case body, so a file-scope block written the same way would be hashed, not dropped
    """

    return all( COMMENT_LINE_RE.match( line )
                for line in shadow[ first : last + 1 ] if line.strip() )


def conditional_only( lines, first, last ):
    """
    True when every non-blank line of a span is a preprocessor conditional and nothing else

    C6's duplication half asks one question - is this an ODR risk? - and a span that is nothing but
    #if, #else and #endif declares nothing, so it never is. It repeats across sibling headers of one
    module on any ordinary split, which is how the duplication half came to red a legal relocation:
    move a guarded helper into a sibling header and repeat its guard, and the #if and the #endif are
    reported as duplicated within the module. Measured, on a filesystem copy

    The predicate has to be the WHOLE span rather than its first line, and that is measured too. Six
    members tree wide OPEN on a directive and only three of them are a directive and nothing else -
    UtfPluginFixture.h:105, TestBaselibDefault5.h:396 and :427. The other three carry code: two
    6-line g_libExt definitions under #if defined( _WIN32 ) / #else, and a 25-line #if 0 block. A
    first-line test would exempt those three as well, and the #if 0 block IS a real ODR risk if it
    is ever copied

    Conditionals only, not every directive: a member which is a bare #define declares something, and
    two of those in one module is exactly what the duplication half is for
    """

    span = [ line for line in lines[ first : last + 1 ] if line.strip() ]

    return bool( span ) and all(
        COND_OPEN_RE.match( line ) or COND_MID_RE.match( line ) or COND_CLOSE_RE.match( line )
        for line in span
        )


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
    """
    Read a source file as lines, with a byte order mark stripped if one is there

    utf-8-sig rather than utf-8, and the difference is one measured false positive. C11 is the
    only invariant that reads line 1 of a file, and in this tree line 1 is always the licence
    comment. A BOM survives a utf-8 read as a character which is not whitespace, so COMMENT_LINE_RE
    stops matching, the licence block stops being prose, and a new module's entry point saved by a
    Windows editor is reported as file-scope text ADDED - a red on a legitimate relocation, on a
    file whose content is correct. No file in this tree carries a BOM, the eol tier does not look
    for one, and agents write files here from Windows
    """

    with open( path, 'r', encoding = 'utf-8-sig', errors = 'replace' ) as stream:
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

UTF_TEST_MODULE_RE = re.compile( r'^\s*#\s*define\s+UTF_TEST_MODULE\b' )


def closes_the_file( lines, index ):
    """
    True when the #endif matching the conditional opened at this line is the file's last directive
    """

    depth = 0
    probe = index

    while probe < len( lines ):

        if COND_OPEN_RE.match( lines[ probe ] ):
            depth += 1
        elif COND_CLOSE_RE.match( lines[ probe ] ):
            depth -= 1
            if depth == 0:
                break

        probe += 1

    if probe >= len( lines ):
        return False

    return not any( DIRECTIVE_RE.match( line ) for line in lines[ probe + 1 : ] )


def include_guard_define( lines, index, matched ):
    """
    The index of the #define which makes the #ifndef at this line a plain include guard, or None

    The shape is #ifndef FOO followed, ignoring blank and comment lines, by #define FOO. The index
    is returned rather than a bare yes, because C11 needs to know which #define lines are the
    guard's: those are excluded from its hash and every other one is not

    Shape ALONE is not enough, and the cost of believing it was measured: define-if-not-defined has
    the guard's shape exactly, so the shape test handed back two real defines - SSL_R_SHORT_READ in
    utf_baselib_cmdline/TestCmdLineEhUtils.h, and UTF_TEST_APP_INIT_UTF_ARGS_PARSER in the shared
    UtfMain.h, which is the default args parser every module in the tree gets. Editing either, and
    deleting the SSL block outright, all three passed tier 1. So a second clause: the matching
    #endif must be the file's LAST directive, which a real include guard's always is and neither of
    those two is. Census of the 212 scanned files: 74 guard-shaped, 72 real, those two the only
    failures, and all 72 open on line 17 on a __<NAME>_H_ symbol

    It has no false negative here, and both halves of that were measured rather than assumed: no
    file guards itself with #if ! defined( X ), and none uses #pragma once. What WOULD make one is
    a directive written after a guard's #endif - the guard would stop being recognised, C11 would
    start hashing it, and the gate would red on every header rename. Nothing does that today, and
    the near miss is why the clause reads "last DIRECTIVE" and not "nothing follows": 190 lines of
    live case text sit after the guard closes in TestHttpClientRequestTask.h, with no directive
    among them. What no shape test can tell apart is the same idiom written at the very end of a
    file, past every other directive - that residue stays open
    """

    if matched.group( 1 ) != 'ifndef':
        return None

    symbol = matched.group( 2 ).strip()

    if not symbol:
        return None

    probe = index + 1

    while probe < len( lines ):

        candidate = lines[ probe ].strip()

        if candidate == '' or candidate.startswith( ( '//', '/*', '*' ) ):
            probe += 1
            continue

        defined = DEFINE_RE.match( lines[ probe ] )

        if not defined or defined.group( 1 ) != symbol:
            return None

        return probe if closes_the_file( lines, index ) else None

    return None


def is_include_guard( lines, index, matched ):
    """
    True when the #ifndef at this line is a plain include guard
    """

    return include_guard_define( lines, index, matched ) is not None


def condition_stack( lines ):
    """
    The preprocessor conditions in force AT each line, in C3's own spelling

    The entry for a line is the stack as it stands BEFORE that line is read, which is exactly how a
    case gets its guards from the walk: a #if line carries the OUTER stack, because its own
    condition starts on the line after, and an #endif line still carries the inner one, because the
    pop is what reading it does. An #else or #elif appends to the top entry rather than replacing
    it - '%s | else' - so that the two branches of one conditional are told apart in the spelling
    C3 has always used

    An include guard contributes nothing, exactly as it contributes nothing to a case's stack.
    Counting it would put __TEST_UTF_H_ under every member of every guarded header and make a
    header rename red the whole tree

    Identity is the TEXT of the condition, not a normalised form of it, and that is a decision
    rather than an oversight. C3 has recorded the text since it existed, so a normalised member
    stack would mean two spellings of "the guard stack" in one manifest. A re-spelling is an edit
    and a relocation gate should say so - #if ! defined( X ) rewritten as #ifndef X during a split
    is not text moving. And a normaliser sound enough to be trusted would have to be an expression
    parser: #if ! defined( X ) && ! defined( Y ) has no #ifndef spelling at all, so a half
    normaliser would bless exactly the careless rewrites and no others. The price is that a
    deliberate re-spelling reports, which is the ordinary companion-refresh answer this gate
    already gives for every other deliberate edit

    selftest_inventory.py asserts this walk directly, on synthetic line lists whose every line
    carries the stack it expects: the boundary rule above, the #else and #elif spelling, the
    include guard exclusion and the define-if-not-defined that shares its shape, nesting, and the
    direction an unbalanced or unterminated conditional takes. That is the only probe there whose
    subject is not a manifest, and it had to be, because both sides of a manifest comparison are
    built by this walk - a stack computed wrongly is computed wrongly twice and reads as equal
    """

    stack = []
    at = []

    for index, line in enumerate( lines ):

        at.append( tuple( entry for entry in stack if entry is not None ) )

        matched = COND_OPEN_RE.match( line )
        if matched:
            if is_include_guard( lines, index, matched ):
                stack.append( None )
            else:
                stack.append( '%s %s' % ( matched.group( 1 ), matched.group( 2 ).strip() ) )
            continue

        matched = COND_MID_RE.match( line )
        if matched and stack:
            if stack[ -1 ] is not None:
                stack[ -1 ] += ' | %s %s' % ( matched.group( 1 ), matched.group( 2 ).strip() )
            continue

        if COND_CLOSE_RE.match( line ) and stack:
            stack.pop()

    return at


def span_conditions( cond_at, first, last ):
    """
    Every condition governing any line of a file-scope span, the stack at its first line first

    C11's identity is the sha of the span's SHADOW text together with the conditions it compiles
    under, and the shadow is where the hole was: a conditional which opens and closes entirely
    INSIDE a bracketed span is blanked out of the shadow, so it is not in the sha - and the stack
    recorded at the span's first line is the one OUTSIDE it, so it was not in the guards either.
    A blank line inside an open bracket does not split a member, so the span swallowed the whole
    conditional and neither half of the identity could see it. Measured on a filesystem copy:
    inverting #if BOOST_VERSION < 105900 at include/utests/baselib/UtfMain.h:96, inside the
    319-line span at :52, PASSED tier 1 green - a file-scope declaration silently changing what
    compiles on which Boost, and the gate said nothing at all

    The union rather than the first line's stack, and the union rather than the span's TEXT, and
    both halves of that are the decision:

      - into the GUARDS, because the report has to say the condition changed. Hashing lines
        rather than shadow over the extent would close the same hole, and it was the shape first
        recorded for it, but it puts the directive into the sha - so the report is LOST plus
        ADDED, indistinguishable from text moving, which is the very distinction the guard half
        was added to draw. It would also hash the #include lines inside such a span, which are
        C10's, and report every one of them twice

      - the UNION over the extent, because a span is not under one stack: 319 lines of UtfMain.h
        compile unconditionally and a handful of them under a BOOST_VERSION test. Flattening that
        to "the conditions governing this span" is the honest summary a flat list can carry, and
        it degenerates to exactly the old value - the stack at the first line - for every span
        with no conditional inside it, which today is 78 of the 79

    Order is first appearance, which puts the outer stack first and in its own order, because the
    entry for the first line IS the outer stack and every interior entry extends it

    MEMBERS are deliberately left alone. A helper member's sha is over lines rather than shadow,
    so a conditional inside one is already in its identity as text; folding it in here as well
    would report it twice and change nothing about what is judged. 18 members carry one today
    """

    conditions = []

    for stack in cond_at[ first : last + 1 ]:
        for entry in stack:
            if entry not in conditions:
                conditions.append( entry )

    return conditions


def scan_file( path, module, rel_path, problems ):
    """
    Extract every test case, column-0 namespace block and include from one file

    Namespace tracking is deliberately limited to column 0. Every test case in this tree sits at
    column 0, so a column-0 stack is enough to detect a case moving into or out of a namespace,
    which is what C4 exists to catch
    """

    lines = read_lines( path )

    #
    # The preprocessor condition enclosing anything but a test case was read by no invariant at
    # all, and the half nobody had looked at is the severe one. C11 drops conditionals "because
    # conditionals are C3's", and C3 speaks for CASES only - so 11 file-scope spans and 18 helper
    # members sat under a #if that nothing judged
    #
    # Measured before this existed, and it is the reason the shape below is identity rather than a
    # per-file anchor: namedMutexSemaphoreKey( ) cut out from under #if ! defined( _WIN32 ) at
    # utf_baselib/TestBaselibDefault5.h:396 into a sibling header WITH NO GUARD, roster edited,
    # using split_members( )'s own extent - which is how every split in this project is cut -
    # PASSED tier 1 green. A relocation accident that silently changes what compiles on which
    # platform, and the gate called it a move
    #
    # The member half looked protected and was not. Six members tree wide OPEN on a directive, and
    # exactly three of those are one line and nothing else - UtfPluginFixture.h:105,
    # TestBaselibDefault5.h:396 and :427. The other three are real members whose first line happens
    # to be a directive and which carry code: two 6-line g_libExt definitions under
    # #if defined( _WIN32 ) / #else, and a 25-line #if 0 block. So inverting the guard at :396
    # reported two C6 lines - which is one of those three one-line members moving, TEXT and not the
    # guard, and a helper carried out from under a guard moves no directive at all. A C12-style
    # per-file anchor would have been silent on exactly the relocation above, because the file
    # changes
    #
    # So the condition stack joins C6's and C11's IDENTITY, sha plus guards, the way C3 already
    # does it for cases, and a text that survives under a different stack is reported as a guard
    # change rather than as a loss
    #
    # A conditional opening INSIDE a bracketed file-scope span was invisible to both halves of that,
    # and span_conditions( ) below is what closes it. The stack used to be taken at a span's first
    # line alone, while the directive itself is blanked out of the shadow and so is not in the sha
    # either - and a blank line inside an open bracket does not split the member, so the two halves
    # met in the middle and judged nothing. One live instance, and it is 319 lines long:
    # include/utests/baselib/UtfMain.h:52-370, with #if BOOST_VERSION < 105900 at :96. Inverting :96
    # PASSED, measured, before span_conditions( ) existed
    #

    cond_at = condition_stack( lines )

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
    # The preprocessor directives another invariant reads are blanked here, ahead of the walk, so
    # that nothing about the walk itself changes - the alternative was a new branch inside it,
    # which would have had to get the data literal collection right as well. The walk blanks the
    # two spans it alone knows: a case with its doc comment, and a namespace block
    #
    # Which directives those are is narrower than it looks, and the narrowing is measured. The
    # #include lines are C10's and the conditionals are C3's, so both go. Of the #define lines,
    # only two kinds go: the include guard, recognised by its shape AND by its #endif being the
    # file's last directive, and UTF_TEST_MODULE, recognised by its name - exactly what the
    # measurement showed a new module's entry point must write fresh, and hashing those two reds
    # the very operation this tool exists to verify. The second half of the guard test is not
    # decoration: shape alone let through the two defines named in include_guard_define( )
    #
    # Every OTHER #define is hashed, with its continuations. 38 spans at file scope carry one
    # today, 32 of them in the shared tree C13 brought into the scan. Measured on tree copies,
    # before this narrowing and after: editing the body of UTF_TEST_NORMALIZE, and deleting
    # #define BL_PLUGINS_CLASS_IMPLEMENTATION from utf_baselib_plugin/Calculator.cpp, both went
    # from PASS to a C11 line. A new module writing a define of its own is ADDED, exactly as a new
    # helper is, and the baseline refresh blesses it
    #
    # What this does NOT close, and the distinction is C11's rather than this exclusion's: three
    # entry points carry #define UTF_TEST_APP_INIT_DEACTIVATE_THREAD_POOLS ( true ) with identical
    # text, and C11's identity is text alone compared tree wide, so dropping one of the three is
    # silent both before and after the narrowing - as dropping one of the two "using namespace bl;"
    # lines is. Counting the copies instead would close it and would red the new-module control,
    # whose entry point legitimately writes the third "using namespace bl;". The exclusion is what
    # decides whether a define is hashed at all; the identity is what decides what that buys
    #

    guard_defines = set()

    for probe in range( total ):

        matched = COND_OPEN_RE.match( lines[ probe ] )

        if matched:
            defined = include_guard_define( lines, probe, matched )
            if defined is not None:
                guard_defines.add( defined )

    shadow = list( lines )

    probe = 0

    while probe < total:

        if DIRECTIVE_RE.match( lines[ probe ] ):

            hashed = ( DEFINE_RE.match( lines[ probe ] )
                       and probe not in guard_defines
                       and not UTF_TEST_MODULE_RE.match( lines[ probe ] ) )

            while probe < total and lines[ probe ].rstrip().endswith( '\\' ):
                if not hashed:
                    shadow[ probe ] = ''
                probe += 1

            if not hashed:
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
            # It is recognised by its shape, #ifndef FOO immediately followed by #define FOO, AND
            # by its #endif being the file's last directive - which is what tells a guard apart
            # from define-if-not-defined, an idiom with exactly the same shape
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
            while end < total and not CASE_CLOSE_RE.match( lines[ end ] ):
                end += 1

            if end >= total:
                problems.append(
                    '%s:%d: case %s has no bare closing brace at column 0' % ( rel_path, index + 1, name )
                    )
                index += 1
                continue

            if lines[ index + 1 ].strip() != '{':
                problems.append(
                    '%s:%d: case %s is not followed by an opening brace at column 0 - '
                    'the extraction precondition does not hold' % ( rel_path, index + 2, name )
                    )

            #
            # The other half of the same precondition, and the one the stricter terminator makes
            # necessary: a case which does not end on a bare brace now runs ON rather than
            # stopping early, and would swallow whatever follows it. A case macro inside the
            # extent is proof that it did
            #

            if any( CASE_RE.match( lines[ inner ] ) for inner in range( index + 1, end ) ):
                problems.append(
                    '%s:%d: case %s reaches a second case macro before its closing brace - '
                    'the extraction precondition does not hold' % ( rel_path, index + 1, name )
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
                    'guards': list( cond_at[ first ] ),
                    'conditional_only': conditional_only( lines, first, last ),
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
        'guards': span_conditions( cond_at, first, last ),
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


SHARED_DIR = 'include'

SHARED_MODULE = '<shared>'

#
# C13 - src/utests/include is scanned too, and its text joins the tree-wide lists
#
# The scan used to start at a directory named utf*, and src/utests/include is not one: 27 files and
# 15,373 lines, included by 181 of the 185 module files, read by no invariant at all. Measured
# before this: editing a shared fixture's declaration in TestMessagingUtils.h, and redefining
# UTF_AUTO_TEST_CASE itself in Utf.h - the macro every case in the tree is declared with - BOTH passed
# tier 1
#
# It is SCOPE rather than a new rule. The same scan_file( ) runs over those files with the same
# exclusions, and what it finds is judged by the invariants that already exist: C6 for a helper
# inside a namespace block, C10 for the include list, C11 for file scope, C12 for the namespace a
# member sits in. C13 itself reports only whether the scan is in force, because a baseline captured
# before it carries no 'shared' key and has nothing to be compared against
#
# The shared tree is deliberately NOT a module. It has no data/ directory and cannot have one -
# TestUtils::resolveDataFilePath resolves relative to the executable - so attributing the one real
# data file name it mentions, async_rpc_request.json in TestMessagingUtilsImpl.cpp, to a pseudo
# module would make C7 demand a directory that cannot exist. Keeping it out of manifest[ 'modules' ]
# also leaves C7's orphan grandfathering and C10's roster exemption untouched, both measured
# identical: the shared tree carries no data file, and all 358 of its includes and every include OF
# it are <angle> spellings, which the roster exemption never covers
#
# Its members and file-scope spans DO join the tree-wide lists, under the module name <shared>, and
# that is the choice worth stating. C6 says a helper is lost only if its text survives nowhere in
# the tree, and with the shared tree outside the scan that was false as written - a helper hoisted
# into src/utests/include read as LOST. One list makes it a move again. The same label keeps C6's
# duplication check, which is per module, asking whether the shared tree redefines something inside
# itself; measured: 0 duplicate blocks and 0 duplicate members there, and 0 shas shared with any
# module's members
#


def capture( src_utests ):
    """
    Walk every utf* module directory, and the shared include tree, and build the manifest
    """

    manifest = { 'cases': [], 'namespaces': [], 'members': [], 'file_members': [],
                 'shared': { 'files': [] }, 'modules': {} }
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

    #
    # The shared include tree, scanned exactly as a module's files are. Its data references are
    # collected and then dropped on the floor: the tree carries no data/ directory and cannot, so
    # the only thing to do with them would be to demand one
    #

    for root, dirs, files in os.walk( os.path.join( src_utests, SHARED_DIR ) ):

        dirs.sort()

        for entry in sorted( files ):

            if not entry.endswith( ( '.h', '.cpp' ) ):
                continue

            path = os.path.join( root, entry )
            rel_path = os.path.relpath( path, src_utests ).replace( os.sep, '/' )

            ( cases, namespaces, members, file_members, includes,
              refs, literals ) = scan_file( path, SHARED_MODULE, rel_path, problems )

            manifest[ 'cases' ].extend( cases )
            manifest[ 'namespaces' ].extend( namespaces )
            manifest[ 'members' ].extend( members )
            manifest[ 'file_members' ].extend( file_members )

            manifest[ 'shared' ][ 'files' ].append( { 'path': rel_path, 'includes': includes } )

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
    File path -> its #include list, across every module and the shared include tree

    Paths carry the module directory and so are unique tree wide, which is what lets a file be
    matched between two manifests without also matching on the module. The shared tree's paths
    begin with include/ and are unique for the same reason, so C10 judges them with no further
    machinery - and a baseline which predates the shared scan simply does not carry them, so the
    intersection leaves them unjudged until it is refreshed
    """

    includes = {}

    for info in manifest[ 'modules' ].values():
        for entry in info[ 'files' ]:
            includes[ entry[ 'path' ] ] = entry.get( 'includes', [] )

    for entry in manifest.get( 'shared', {} ).get( 'files', [] ):
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


def without_shared( manifest ):
    """
    The same manifest with every trace of the shared include tree taken out

    Used on the current side when the baseline predates the shared scan, so that the comparison is
    exactly the one that ran before it existed rather than 200-odd spurious ADDED lines. The four
    lists are rebuilt and everything else is shared by reference, because nothing here writes
    """

    trimmed = dict( manifest )

    for key in ( 'cases', 'namespaces', 'members', 'file_members' ):
        trimmed[ key ] = [ entry for entry in manifest.get( key, [] )
                           if entry.get( 'module' ) != SHARED_MODULE ]

    trimmed.pop( 'shared', None )

    return trimmed


def carries_guards( *lists ):
    """
    True when any of these manifest span lists carries the guard stack C6 and C11 fold in

    capture( ) writes the key on every member and every file-scope span, so presence on one is
    presence on all. A baseline captured before this carries it nowhere, and the guard half is then
    simply not in force - the C11 and C13 precedent, and for their reason: a hard red would stop
    every lane until the refresh lands rather than stopping the change that earned it

    What bounds that silence is that the next refresh for any reason arms it. What does NOT bound
    it is --capture's refusal, and the gap is worth naming rather than assuming: that guard compares
    TOP-LEVEL keys, and guards is a field inside a span. A pre-guard tool re-capturing over this
    baseline writes the same six top-level keys, is not refused, and disarms this half with only the
    printed note to say so - which is the residue the C11 section already names one level up
    """

    return any( 'guards' in entry for entries in lists for entry in entries )


def span_comparison( before_list, after_list, armed ):
    """
    Sort two lists of spans into ( lost, added, reguarded ) - C6's and C11's shared comparison

    The identity is the text sha together with the preprocessor conditions recorded beside it - the
    stack enclosing a helper member, and for a file-scope span the conditions governing any line of
    it, which span_conditions( ) explains - as C3 does for a case. A span whose text survives on the
    other side under a DIFFERENT stack is neither lost nor invented - it is the same helper now
    compiling on a different platform, and saying so in its own words is what makes the control
    meaningful. Reporting it as LOST plus ADDED would be indistinguishable from one of the three
    one-line directive members moving, which is text and not the guard

    With armed False the guard half of every key is empty on both sides, so the keys differ only by
    sha, reguarded is necessarily empty, and lost and added are exactly what this comparison
    reported before the guard stack existed. That equivalence is what lets a baseline predating the
    capture go on being judged rather than red
    """

    def keyed( entries ):

        table = {}

        for entry in entries:
            key = ( entry[ 'sha' ], tuple( entry.get( 'guards', [] ) ) if armed else () )
            table.setdefault( key, entry )

        return table

    old, new = keyed( before_list ), keyed( after_list )

    old_text = set( key[ 0 ] for key in old )
    new_text = set( key[ 0 ] for key in new )

    lost = [ old[ key ] for key in sorted( set( old ) - set( new ) ) if key[ 0 ] not in new_text ]
    added = [ new[ key ] for key in sorted( set( new ) - set( old ) ) if key[ 0 ] not in old_text ]

    def stacks( table, digest ):
        return sorted( ' & '.join( key[ 1 ] ) or '<none>' for key in table if key[ 0 ] == digest )

    reguarded = []

    for digest in sorted( set( key[ 0 ] for key in set( old ) - set( new ) ) & new_text ):
        where = next( new[ key ] for key in sorted( new ) if key[ 0 ] == digest )
        reguarded.append( ( where, stacks( old, digest ), stacks( new, digest ) ) )

    return lost, added, reguarded


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

    #
    # A member which is nothing but preprocessor conditionals is not asked, because it can never be
    # the thing this check is looking for. It declares nothing, so two copies are no ODR risk; and
    # it repeats across sibling headers on any ordinary split, so asking reds a legal relocation -
    # move a guarded helper into a sibling header and repeat its guard, and the #if and the #endif
    # were reported as duplicated within the module. Three members tree wide have that shape, and
    # conditional_only( ) reads every line of the span rather than the first, because three MORE
    # open on a directive and carry code
    #
    # The no-loss half above still judges them, and so does the guard half: their text moving is
    # still a report, which is what control 2 of the guard change-set rests on
    #
    # A baseline captured before the field simply does not carry it, and .get( ) then answers False
    # - every member is asked, exactly as before. That is the conservative direction, and this check
    # is intrinsic so main( ) hands it the current scan in any case
    #

    by_module_member = {}

    for member in manifest.get( 'members', [] ):

        if member.get( 'conditional_only' ):
            continue

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

    C5 is asked here too, of the baseline, because check_intrinsic( ) is only ever given the
    current tree and a duplicate on the other side hides a deletion from C1
    """

    failures = []

    #
    # C13 - is the shared include tree in force?
    #
    # A baseline captured before it was scanned carries no 'shared' key and none of its text, so
    # every span, member and namespace the current scan found there would report as ADDED - 164
    # helper members and 38 file-scope spans of pure noise on a tree nobody has changed. The
    # departure is C11's, for C11's reason: a hard red would stop every lane until the refresh
    # lands rather than stopping the change that earned it. main( ) prints which state a run is in
    #
    # So when the baseline predates it the shared tree is taken out of the CURRENT side too, which
    # makes the comparison bit-identical to the one before this existed. When the baseline carries
    # it, both sides are judged whole
    #
    # The note is bounded the way C11's is - capture( ) always writes the key, so the next refresh
    # for any reason arms it - and the two states it cannot be in are hard failures below: a
    # baseline which carries the key EMPTY is broken rather than old, and a scan which found no
    # shared file when the baseline has them is an extraction failure
    #

    armed = 'shared' in before

    if armed:

        if not before[ 'shared' ].get( 'files' ):
            failures.append(
                'C13 the baseline carries an EMPTY shared-tree file list - src/utests/%s holds '
                'files in every real tree, so this baseline is broken rather than merely old'
                % SHARED_DIR
                )

        elif not after.get( 'shared', {} ).get( 'files' ):
            failures.append(
                'C13 the current manifest carries no shared-tree files - extraction failed'
                )

    else:
        after = without_shared( after )

    #
    # The guard half of C6 and C11 - is the enclosing preprocessor condition part of the identity?
    #
    # No invariant read a condition enclosing anything but a test case: C11 drops conditionals
    # "because conditionals are C3's", and C3 speaks for cases only. 18 helper members sit under
    # one today and 12 file-scope spans sit under or contain one, among them Utf.h's
    # #if defined( UTF_TEST_MODULE ), which is the condition gating main( )
    #
    # A baseline captured before this carries no guards anywhere, and the same departure C11 and
    # C13 take is taken here rather than reding every lane: span_comparison( ) then keys on the sha
    # alone, which is bit-identical to the comparison that ran before this existed. main( ) prints
    # which state a run is in, and capture( ) always writes the key, so the next refresh arms it
    #
    # It is folded into C6's and C11's identity rather than gated on a per-file anchor, and the
    # difference is measured: the relocation this exists to catch - a guarded helper cut into a
    # sibling header WITHOUT its guard - changes the file, so a per-file anchor is silent on
    # exactly it. C6's DUPLICATION half is deliberately left alone here: two identical helpers
    # under mutually exclusive conditions are no ODR risk, so adding the stack to that key could
    # only make it report less, and the safe direction for a gate is to fire
    #

    guarded = carries_guards( before.get( 'members', [] ), before.get( 'file_members', [] ) )

    #
    # C5, asked of the BASELINE as well as of the tree being scanned
    #
    # check_intrinsic( ) runs on one manifest and main( ) only ever hands it the current one, so
    # the baseline's own duplicate names were read by nothing. That is not academic: index_cases( )
    # keys on the name and collapses a duplicate pair to one entry, and C1 to C4 all key on the
    # name too - so with a baseline carrying two cases called N, deleting either one of them is
    # reported by NOTHING. The path that can produce such a baseline is --capture, which writes the
    # manifest before check_intrinsic( ) has run and cannot refuse after the fact
    #

    doubled = {}

    for case in before[ 'cases' ]:

        name = case[ 'name' ]

        if name in doubled:
            failures.append(
                'C5 the BASELINE carries case name %s twice (%s and %s) - C1 to C4 key on the '
                'name, so one of the pair is invisible to this comparison'
                % ( name, doubled[ name ][ 'file' ], case[ 'file' ] )
                )

        doubled[ name ] = case

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

            lost, added, reguarded = span_comparison(
                before[ 'file_members' ], after[ 'file_members' ], guarded
                )

            for where in lost:
                failures.append(
                    'C11 file-scope text LOST: %s (%s:%d)'
                    % ( where[ 'label' ], where[ 'file' ], where[ 'line' ] )
                    )

            for where in added:
                failures.append(
                    'C11 file-scope text ADDED: %s (%s:%d)'
                    % ( where[ 'label' ], where[ 'file' ], where[ 'line' ] )
                    )

            for where, was, now in reguarded:
                failures.append(
                    'C11 file-scope text GUARD STACK CHANGED: %s (%s:%d) - was under %s, now '
                    'under %s' % ( where[ 'label' ], where[ 'file' ], where[ 'line' ],
                                   ', '.join( was ), ', '.join( now ) )
                    )

    #
    # C12 - a helper member which stayed in its file kept the namespace it sat in
    #
    # manifest[ 'namespaces' ] - the column-0 blocks, each with its name and its whole-block sha -
    # was read by check_intrinsic( )'s duplication check and by nothing else, so renaming a
    # column-0 namespace passed tier 1. Measured, in a helper-only header and in one holding live
    # cases. C4 cannot stand in for this: every case in this tree sits at file scope, so C4's
    # subject is empty and it protects nothing that exists - asserted by the selftest on whatever
    # baseline it is given, not left as a count in a comment that a new case would make stale
    #
    # The identity is deliberately NOT the block. A block's sha covers its opening line, so a
    # rename changes it and the block cannot be matched across the two manifests at all; and the
    # block's name is not distinguishing either, since the whole tree uses four names and 95 of
    # the 165 blocks are anonymous. Worse, the per-file list of block names fires on a header
    # partition - the one operation C6 was moved down to members precisely in order to stay
    # silent about
    #
    # So the anchor is the member: text and file together. A member whose text and file are both
    # unchanged has not been relocated, and its namespace path is then part of what a relocation
    # gate must hold fixed - moving it from an anonymous namespace into a named one, or renaming
    # the block around it, changes the linkage of the helper every case in that module compiles
    # against. A member which moved to another file is not judged, for the reason C6 does not
    # judge it either: a split writes new headers and a hoist into a different namespace IS the
    # operation. The set of paths is compared rather than one, because the same text may legally
    # appear twice in one file under two different namespaces
    #
    # It sits before the C6 section for the reason C9's no-loss half and C11 do: that section
    # returns early when a baseline carries no members
    #

    old_paths, new_paths = {}, {}

    for member in before.get( 'members', [] ):
        old_paths.setdefault( ( member[ 'sha' ], member[ 'file' ] ), set() ).add( member[ 'ns' ] )

    for member in after.get( 'members', [] ):
        new_paths.setdefault( ( member[ 'sha' ], member[ 'file' ] ), set() ).add( member[ 'ns' ] )

    labels = { ( member[ 'sha' ], member[ 'file' ] ): member
               for member in after.get( 'members', [] ) }

    for key in sorted( set( old_paths ) & set( new_paths ) ):

        if old_paths[ key ] == new_paths[ key ]:
            continue

        where = labels[ key ]

        failures.append(
            'C12 helper member CHANGED NAMESPACE: %s (%s:%d) - was in %s, now in %s'
            % ( where[ 'label' ], where[ 'file' ], where[ 'line' ],
                ', '.join( sorted( old_paths[ key ] ) ),
                ', '.join( sorted( new_paths[ key ] ) ) )
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

    lost, added, reguarded = span_comparison( before[ 'members' ], after[ 'members' ], guarded )

    for where in lost:
        failures.append(
            'C6 helper member LOST: %s (%s:%d)' % ( where[ 'label' ], where[ 'file' ], where[ 'line' ] )
            )

    for where in added:
        failures.append(
            'C6 helper member ADDED: %s (%s:%d)' % ( where[ 'label' ], where[ 'file' ], where[ 'line' ] )
            )

    for where, was, now in reguarded:
        failures.append(
            'C6 helper member GUARD STACK CHANGED: %s (%s:%d) - was under %s, now under %s'
            % ( where[ 'label' ], where[ 'file' ], where[ 'line' ],
                ', '.join( was ), ', '.join( now ) )
            )

    return failures


def key_sets( manifest ):
    """
    The key sets --capture's refusal compares: the top level, each list's ENTRY shape, and a module

    Two levels, and the second one is where the stop actually has to be, because a check's key is
    not always a top-level one: the guard stack C6 and C11 fold in is a field on a member, so a
    pre-guard tool re-capturing writes the same top-level keys and would be waved through by a
    top-level comparison alone

    Two levels is also as deep as a SCHEMA-FREE rule can go here, and that is measured rather than
    chosen for tidiness. Below this the manifest is DATA-keyed - modules by module name, and
    modules[ ... ].data_files by filename - so a rule which walked further would read the ordinary
    refresh that deletes a module, or a data file, as keys being dropped and refuse it

    The shortcut is the FIRST entry of a list standing for all of them, which holds because
    scan_file( ) builds every entry of a list from one literal. selftest_inventory.py asserts it
    over every entry of every list rather than leaving it to this comment

    RESIDUE: a field added BELOW entry level is not covered - a new key inside an entry of an entry.
    Nothing in this manifest has that shape today, and the day one does is the day this rule has to
    be rethought rather than deepened, because deepening runs into the data-keyed levels above. A
    dict value which is neither modules nor a list - shared today - is compared by its presence
    alone for the same reason
    """

    sets = { '': set( manifest ) }

    for name, value in manifest.items():
        if isinstance( value, list ) and value and isinstance( value[ 0 ], dict ):
            sets[ name + '[]' ] = set( value[ 0 ] )

    modules = manifest.get( 'modules' )

    if isinstance( modules, dict ) and modules:
        first = modules[ sorted( modules )[ 0 ] ]
        if isinstance( first, dict ):
            sets[ 'modules{}' ] = set( first )

    return sets


def keys_a_capture_would_drop( path, manifest ):
    """
    The keys a baseline already at this path carries and this manifest does not, level by level

    A level is reported only when the existing key set there is a STRICT SUPERSET of the one about
    to be written, which is the one state that says unambiguously "an older tool is overwriting a
    newer baseline". Any other disagreement at that level is left alone: a manifest carrying a key
    the baseline lacks is the ordinary arming refresh, and disagreement in both directions is
    schema evolution rather than a downgrade. Levels are judged independently, so an arming refresh
    at the top level does not excuse a field disappearing from a member

    Key sets alone, deliberately - no schema and no contents. Every check that came with a key was
    written to notice its own absence, so the only thing this has to stop is the key vanishing; and
    a rule which validated contents would need editing for every key added after it

    A path which does not exist, does not parse as JSON, or does not hold an object is not a
    baseline this could be downgrading, so nothing is reported and the write goes ahead
    """

    if not os.path.isfile( path ):
        return []

    try:
        with open( path ) as stream:
            existing = json.load( stream )
    except ( ValueError, OSError ):
        return []

    if not isinstance( existing, dict ):
        return []

    was, now = key_sets( existing ), key_sets( manifest )

    dropped = []

    for level in sorted( set( was ) & set( now ) ):

        if now[ level ] - was[ level ]:
            continue

        for key in sorted( was[ level ] - now[ level ] ):
            dropped.append( '%s%s' % ( level + '.' if level else '', key ) )

    return dropped


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

        #
        # Refuse before the write rather than warn after it. A printed note is exactly what the
        # unarmed state already has, and the state this tool has been caught in three times is one
        # nobody read
        #

        dropped = keys_a_capture_would_drop( args.capture, manifest )

        if dropped:
            print( 'utf_inventory: REFUSING to overwrite %s - it carries key(s) this run does not '
                   'write: %s' % ( args.capture, ', '.join( dropped ) ),
                   file = sys.stderr )
            print( 'utf_inventory: every key this run writes is already there, so this tool is '
                   'OLDER than the baseline and the write would drop whatever reads those keys, '
                   'leaving those invariants judging nothing', file = sys.stderr )
            print( 'utf_inventory: re-capture with the integrated tool - merge or rebase onto the '
                   'branch that wrote this baseline first. A conflict in a baseline is never '
                   'resolved by taking one side', file = sys.stderr )
            return 4

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

        #
        # Every count below is of what is actually JUDGED, which is not the whole manifest when
        # the baseline predates the shared include tree - C13 takes that tree out of both sides,
        # and a scope line printed over a population the comparison never looked at is the exact
        # mistake these lines exist to prevent
        #

        judged = manifest if 'shared' in before else without_shared( manifest )

        print( 'utf_inventory: C6 reports a helper member ADDED as well as one LOST, over %d '
               'member(s) - a relocation invents neither, so a slice which adds one on purpose '
               'refreshes the baseline, exactly as C1 already requires for a new case'
               % len( judged.get( 'members', [] ) ) )

        #
        # C12's scope is its anchor, and a reader of a green run has to be able to size it: a
        # member which moved to another file is deliberately not judged, so the number that IS
        # judged is the number worth printing
        #

        anchored = ( { ( member[ 'sha' ], member[ 'file' ] ) for member in before.get( 'members', [] ) }
                     & { ( member[ 'sha' ], member[ 'file' ] ) for member in judged.get( 'members', [] ) } )

        print( 'utf_inventory: C12 judges the namespace path of the %d member(s) whose text and '
               'file are both unchanged, of %d - one that moved to another file is a relocation '
               'and is C6\'s alone; the block list itself is read only by the duplication check, '
               'because a block sha covers its own opening line and a partition changes it'
               % ( len( anchored ), len( judged.get( 'members', [] ) ) ) )

        #
        # The same reasoning once more: a check whose scope is not printed is a check a reader of
        # a green run cannot size. C11's scope has two halves worth stating - what it reads, and
        # the one thing at file scope it deliberately does not
        #

        #
        # C13's state, on every run, for the reason C11 prints its own: an unarmed check a reader
        # cannot see is the state this tool has been caught in three times
        #

        shared_files = manifest.get( 'shared', {} ).get( 'files', [] )
        shared_paths = { entry[ 'path' ] for entry in shared_files }

        consumers = sum(
            1
            for info in manifest[ 'modules' ].values()
            for entry in info[ 'files' ]
            if any( SHARED_DIR + '/' + include[ 1 : -1 ] in shared_paths
                    for include in entry[ 'includes' ] if len( include ) > 2 )
            )

        if 'shared' not in before:
            print( 'utf_inventory: C13 - this baseline predates the shared include tree, so the '
                   '%d file(s) under src/utests/%s, included by %d of the %d module file(s), are '
                   'judged by nothing until it is refreshed'
                   % ( len( shared_files ), SHARED_DIR, consumers,
                       sum( len( info[ 'files' ] ) for info in manifest[ 'modules' ].values() ) ) )
        else:
            print( 'utf_inventory: C13 - the shared include tree is in force: %d file(s) under '
                   'src/utests/%s, %d helper member(s) and %d file-scope span(s), judged by C6, '
                   'C10, C11 and C12 exactly as a module\'s files are; it is not a module, so C7 '
                   'and C9 never ask it anything'
                   % ( len( shared_files ), SHARED_DIR,
                       sum( 1 for m in manifest.get( 'members', [] ) if m[ 'module' ] == SHARED_MODULE ),
                       sum( 1 for m in manifest.get( 'file_members', [] ) if m[ 'module' ] == SHARED_MODULE ) ) )

        if 'file_members' not in before:
            print( 'utf_inventory: C11 - this baseline predates the file-scope capture, so text '
                   'outside every namespace block is judged by nothing until it is refreshed' )
        else:
            print( 'utf_inventory: C11 compares the %d span(s) of file-scope text in %d file(s) - '
                   'what is left once the cases, the namespace blocks and the preprocessor lines '
                   'are taken out - by text alone, tree wide and in both directions, exactly as '
                   'C6 does inside a namespace'
                   % ( len( judged.get( 'file_members', [] ) ),
                       len( { member[ 'file' ] for member in judged.get( 'file_members', [] ) } ) ) )

            print( 'utf_inventory: C11 exempts the file-scope directives another invariant already '
                   'reads - includes are C10\'s and conditionals C3\'s - and the two a new module '
                   'writes fresh: the include guard by its shape and UTF_TEST_MODULE by its name. '
                   'Every OTHER #define is hashed with its continuations. A comment block every '
                   'line of which opens with a comment token is dropped as module-level prose; a '
                   'comment against a declaration is part of it and stays judged' )

        #
        # The guard half's state, on every run, for the reason C11 and C13 print theirs. A check
        # that is not in force and cannot be seen not to be is the state this tool has been caught
        # in three times, and what it covers is worth sizing as well: the population is small, and
        # a reader who thinks it is large will trust it for more than it says
        #

        under_a_guard = sum( 1 for member in judged.get( 'members', [] ) if member.get( 'guards' ) )

        spans_under_a_guard = sum(
            1 for member in judged.get( 'file_members', [] ) if member.get( 'guards' )
            )

        if not carries_guards( before.get( 'members', [] ), before.get( 'file_members', [] ) ):
            print( 'utf_inventory: C6/C11 - this baseline predates the guard capture, so the '
                   'preprocessor condition enclosing a helper member or a file-scope span is '
                   'judged by nothing until it is refreshed; %d member(s) and %d span(s) sit '
                   'under one' % ( under_a_guard, spans_under_a_guard ) )
        else:
            print( 'utf_inventory: C6 and C11 fold the enclosing #if stack into their identity, as '
                   'C3 does for a case - %d member(s) sit under one, and %d file-scope span(s) sit '
                   'under or CONTAIN one, since a conditional opening inside a bracketed span is '
                   'read by neither the stack outside it nor the hash within it. A text that '
                   'survives under a DIFFERENT stack reports as a guard change rather than as a '
                   'loss. The condition is compared as written, so a re-spelling of the same '
                   'condition reports' % ( under_a_guard, spans_under_a_guard ) )

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
