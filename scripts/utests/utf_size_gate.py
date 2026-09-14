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
# utf_size_gate.py - keep test module objects from growing without a bound
#
# One oversized test translation unit (112.68MB) once made two x86 build combinations impossible
# to compile at all, with no warning beforehand: a module grows quietly for a year and then a
# toolchain dies with no diagnostic. This runs on every build so that cannot happen silently again
#
# Limits live in src/utests/object-size-limits.json, keyed by glob patterns matched against the
# build directory name (OS-ARCH-TOOLCHAIN-VARIANT). The most specific pattern wins. A pattern can
# be 'enforce' (over ceiling fails), 'report' (print, never fail) or 'off' (silent), which is what
# makes the rollout per platform - see notes/plans/test-module-size-gate-plan.md
#
# Nothing here consults a recorded baseline. The ceiling is a static number, so this costs no data
# churn and there is never a file for a developer to update. Drift against recorded sizes is a
# separate, milestone-scoped concern handled by utf_objsize.py
#
# Usage:
#
#   utf_size_gate.py --platform-dir bld/win-x86-vc143-debug --module utf_baselib_io
#   utf_size_gate.py --platform-dir bld/win-x86-vc143-debug --summary
#   utf_size_gate.py --bld bld --summary
#
# Stdlib only, by design - it must run on the devenv7 dist interpreter, which is an embeddable
# build with no venv and no pip
#

from __future__ import print_function

import argparse
import fnmatch
import json
import os
import sys

MB = 1024.0 * 1024.0

OBJECT_SUFFIXES = ( '.obj', '.o' )

BAR_WIDTH = 10

#
# Set BL_SKIP_SIZE_GATE=1 to silence this locally while experimenting. It is deliberately not
# honoured when recording a matrix, and the name is verbose so it does not end up in an alias
#

SKIP_ENV = 'BL_SKIP_SIZE_GATE'


def repo_root():
    return os.path.normpath( os.path.join( os.path.dirname( os.path.abspath( __file__ ) ), '..', '..' ) )


def limits_path():
    return os.path.join( repo_root(), 'src', 'utests', 'object-size-limits.json' )


def load_limits( path = None ):
    """
    Return the limits table, or None when the file is missing or unreadable

    A missing or broken limits file must never break a build. The gate is a guard rail, and a
    guard rail which stops the car is worse than none
    """

    path = path or limits_path()

    try:
        with open( path ) as stream:
            return json.load( stream ).get( 'limits', {} )
    except Exception:
        return None


def specificity( pattern ):
    """
    How specific a glob is, as the count of characters which are not wildcards

    'win-x86-*-debug' beats 'win-x86-*' beats '*', which is what lets a platform be adopted by
    adding one narrower line rather than by reordering the file
    """

    return len( [ char for char in pattern if char not in '*?' ] )


def resolve( platform_tag, limits ):
    """
    Return the most specific limits entry matching this platform tag, or None
    """

    matches = [ ( specificity( pattern ), pattern, entry )
                for pattern, entry in limits.items()
                if fnmatch.fnmatchcase( platform_tag, pattern ) ]

    if not matches:
        return None

    matches.sort( key = lambda row: row[ 0 ], reverse = True )

    return matches[ 0 ][ 2 ]


def objects_for( module_dir ):
    """
    Return { object name: bytes } for one module's build directory
    """

    objects = {}

    if not os.path.isdir( module_dir ):
        return objects

    for entry in sorted( os.listdir( module_dir ) ):
        if entry.endswith( OBJECT_SUFFIXES ):
            full = os.path.join( module_dir, entry )
            if os.path.isfile( full ):
                objects[ entry ] = os.path.getsize( full )

    return objects


def modules_for( platform_dir ):
    """
    Return { module: { object: bytes } } for one build tree
    """

    utests_dir = os.path.join( platform_dir, 'utests' )
    modules = {}

    if not os.path.isdir( utests_dir ):
        return modules

    for module in sorted( os.listdir( utests_dir ) ):
        objects = objects_for( os.path.join( utests_dir, module ) )
        if objects:
            modules[ module ] = objects

    return modules


def bar( fraction ):
    filled = int( round( min( fraction, 1.0 ) * BAR_WIDTH ) )
    return '[' + ( '#' * filled ) + ( '.' * ( BAR_WIDTH - filled ) ) + ']'


def describe( module, objects, entry ):
    """
    Return ( line, breach ) for one module - the worst object decides both
    """

    ceiling = entry.get( 'ceiling_mb' )
    target = entry.get( 'target_mb' )

    name, size = max( objects.items(), key = lambda item: item[ 1 ] )
    mb = size / MB

    if not ceiling:
        return ( '  %-28s %7.1f MB' % ( module, mb ), False )

    fraction = mb / ceiling
    note = ''

    if mb > ceiling:
        note = '  OVER THE %g MB CEILING' % ceiling
    elif target and mb > target:
        note = '  over %g MB target' % target

    line = '  %-28s %7.1f / %g MB  %s %3d%%%s' % (
        module, mb, ceiling, bar( fraction ), int( round( fraction * 100 ) ), note )

    return ( line, mb > ceiling )


def failure_message( platform_tag, module, name, mb, ceiling ):
    return """
  %s / %s
  %.1f MB exceeds the %g MB ceiling for %s

  This translation unit is too large for the 32-bit x86 toolchain, which has roughly
  2GB of address space. It is not a limit to raise - a 112.68MB object once made two
  x86 build combinations impossible to compile at all.

  Move some test headers into a numbered sibling module:

      src/utests/%s<N>/

  See src/utests/AGENTS.md for the checklist.
  Set %s=1 to silence this locally while experimenting.
""" % ( module, name, mb, ceiling, platform_tag, module.rstrip( '0123456789' ), SKIP_ENV )


def run( platform_dir, module_filter, summary, limits ):
    """
    Report and gate one build tree; return the number of ceiling breaches
    """

    platform_tag = os.path.basename( os.path.normpath( platform_dir ) )
    entry = resolve( platform_tag, limits ) if limits is not None else None

    if entry is None or entry.get( 'gate', 'off' ) == 'off':
        return 0

    enforcing = entry.get( 'gate' ) == 'enforce'
    ceiling = entry.get( 'ceiling_mb' )

    modules = modules_for( platform_dir )

    if module_filter:
        modules = { name: objects for name, objects in modules.items() if name == module_filter }

    if not modules:
        return 0

    breaches = []

    if summary:
        print( '' )
        print( 'Test module object sizes - %s%s' % (
            platform_tag, '' if enforcing else '   (reporting only)' ) )

        if ceiling:
            print( '%s   target %g MB, ceiling %g MB' % (
                ' ' * 2, entry.get( 'target_mb', 0 ), ceiling ) )

        print( '' )

        ordered = sorted( modules.items(),
                          key = lambda item: max( item[ 1 ].values() ),
                          reverse = True )
    else:
        ordered = sorted( modules.items() )

    for module, objects in ordered:
        line, breach = describe( module, objects, entry )
        print( line )
        if breach:
            name, size = max( objects.items(), key = lambda item: item[ 1 ] )
            breaches.append( ( module, name, size / MB ) )

    if summary:
        over_target = 0
        target = entry.get( 'target_mb' )

        if target:
            over_target = len( [ 1 for objects in modules.values()
                                 if max( objects.values() ) / MB > target ] )

        print( '' )
        print( '  %d module%s, %d objects, %d over ceiling, %d over target' % (
            len( modules ),
            '' if len( modules ) == 1 else 's',
            sum( len( objects ) for objects in modules.values() ),
            len( breaches ),
            over_target ) )
        print( '' )

    if breaches and enforcing:
        #
        # Flush first: the per-module lines go to stdout and the explanation to stderr, and an
        # unflushed stdout puts the error above the line it is explaining in a build log
        #
        sys.stdout.flush()

        for module, name, mb in breaches:
            print( failure_message( platform_tag, module, name, mb, ceiling ), file = sys.stderr )

        sys.stderr.flush()

        return len( breaches )

    return 0


def main():

    parser = argparse.ArgumentParser( description = 'gate unit test module object sizes' )

    parser.add_argument( '--platform-dir', help = 'one build tree, e.g. bld/win-x86-vc143-debug' )
    parser.add_argument( '--bld', help = 'the bld root; every tree under it is checked' )
    parser.add_argument( '--module', help = 'restrict to one module (the per-build hook uses this)' )
    parser.add_argument( '--summary', action = 'store_true', help = 'print the sorted table and totals' )
    parser.add_argument( '--limits', help = 'override the limits file path' )

    args = parser.parse_args()

    if os.environ.get( SKIP_ENV ) == '1':
        return 0

    limits = load_limits( args.limits )

    if limits is None:
        #
        # Never break a build over the gate's own configuration
        #
        print( 'utf_size_gate: limits file missing or unreadable, skipping', file = sys.stderr )
        return 0

    if args.platform_dir:
        trees = [ args.platform_dir ]
    elif args.bld:
        trees = [ os.path.join( args.bld, name ) for name in sorted( os.listdir( args.bld ) )
                  if os.path.isdir( os.path.join( args.bld, name, 'utests' ) ) ] \
                if os.path.isdir( args.bld ) else []
    else:
        parser.error( 'one of --platform-dir or --bld is required' )

    breaches = 0

    for tree in trees:
        breaches += run( tree, args.module, args.summary, limits )

    return 1 if breaches else 0


if __name__ == '__main__':
    sys.exit( main() )
