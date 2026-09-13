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
# utf_objsize.py - measure and gate the size of unit-test object files
#
# Every test module is a single translation unit, so its object file grows without bound as
# test cases are added. A 32-bit compiler host runs out of address space somewhere above
# 100MB; see notes/plans/issues/x86-clang-cl-host-and-test-module-size-deferral.md
#
# This script is the acceptance gate for the module split: it walks the built object files
# under a bld tree and fails if any of them exceeds the ceiling
#
# Note there is a large fixed floor per translation unit - about 21MB on x86 debug - which is
# the cost of UtfMain.h plus baselib plus the header-only Boost.Test runner. A module of 223
# lines still produces a 21.4MB object, so the ceiling can never be set near that floor
#
# Usage:
#
#   utf_objsize.py --capture                    write a manifest of every object found
#   utf_objsize.py --ceiling 40                 fail if any object exceeds 40MB
#   utf_objsize.py --compare before.json        report the delta against an earlier manifest
#
# Stdlib only, by design - it must run on the devenv7 dist interpreter, which is an
# embeddable build with no venv and no pip
#

from __future__ import print_function

import argparse
import json
import os
import re
import sys

#
# The ceiling, in megabytes, for one unit-test object on x86 debug
#
# The deferral record originally proposed 40MB. The utf_baselib_security pilot then measured that
# instantiating the AuthorizationCache stack costs about 26MB on its own, against the roughly 19MB
# of marginal content a 40MB ceiling allows - so 40MB was unreachable for any module holding even
# one such test case, by any arrangement of files
#
# 55MB was adopted instead on 2026-09-12. It is twice the headroom against the roughly 110MB which
# actually exhausted the 32-bit clang-cl host, and it is reachable by moving files. Reducing the
# instantiation weight itself is tracked separately in
# notes/plans/issues/test-instantiation-weight-deferral.md
#

DEFAULT_CEILING_MB = 55.0

#
# The measured fixed cost of a test translation unit on x86 debug, in megabytes; reported
# alongside each object so the marginal (splittable) content is visible rather than implied
#

TU_FLOOR_MB = 21.0

MB = 1024.0 * 1024.0

OBJECT_SUFFIXES = ( '.obj', '.o' )


def repo_root():
    return os.path.normpath( os.path.join( os.path.dirname( os.path.abspath( __file__ ) ), '..', '..' ) )


def collect( bld_dir ):
    """
    Walk every configured build tree under bld/ and return the size of each unit-test object

    The returned shape is { '<platform tag>': { '<module>': { '<tu>': bytes } } } where the
    platform tag is the bld subdirectory name, e.g. 'win-x86-vc143-debug'
    """

    trees = {}

    if not os.path.isdir( bld_dir ):
        return trees

    for platform_tag in sorted( os.listdir( bld_dir ) ):

        utests_dir = os.path.join( bld_dir, platform_tag, 'utests' )

        if not os.path.isdir( utests_dir ):
            continue

        modules = {}

        for module in sorted( os.listdir( utests_dir ) ):

            module_dir = os.path.join( utests_dir, module )

            if not os.path.isdir( module_dir ):
                continue

            objects = {}

            for entry in sorted( os.listdir( module_dir ) ):
                if entry.endswith( OBJECT_SUFFIXES ):
                    objects[ entry ] = os.path.getsize( os.path.join( module_dir, entry ) )

            if objects:
                modules[ module ] = objects

        if modules:
            trees[ platform_tag ] = modules

    return trees


def flatten( trees ):
    """
    Yield ( platform_tag, module, tu, bytes ) for every object, largest first
    """

    rows = []

    for platform_tag, modules in trees.items():
        for module, objects in modules.items():
            for tu, size in objects.items():
                rows.append( ( platform_tag, module, tu, size ) )

    rows.sort( key = lambda row: row[ 3 ], reverse = True )

    return rows


def is_gated( platform_tag, gate_pattern ):
    return re.search( gate_pattern, platform_tag ) is not None


def report( trees, ceiling_mb, gate_pattern ):
    """
    Print the object table and return the list of ( platform_tag, module, tu, mb ) over ceiling

    Only trees matching the gate pattern are gated; the others are reported for information
    because the ceiling was calibrated on x86 debug and does not transfer directly - a64 debug
    objects measure about 1.4x their x86 counterparts
    """

    over = []

    for platform_tag, module, tu, size in flatten( trees ):

        mb = size / MB
        gated = is_gated( platform_tag, gate_pattern )
        breach = gated and mb > ceiling_mb

        if breach:
            over.append( ( platform_tag, module, tu, mb ) )

        marker = 'OVER' if breach else ( '    ' if gated else ' -  ' )

        print(
            '%s %8.1f MB  (marginal %6.1f)  %s  %s/%s' % (
                marker,
                mb,
                mb - TU_FLOOR_MB,
                platform_tag,
                module,
                tu,
                )
            )

    return over


def compare( before, after ):
    """
    Print the per-object delta between two manifests and return the number of objects that grew
    """

    def index( trees ):
        return { ( p, m, t ): s for p, m, t, s in flatten( trees ) }

    old = index( before )
    new = index( after )

    grew = 0

    for key in sorted( set( old ) | set( new ) ):

        platform_tag, module, tu = key

        old_mb = old.get( key, 0 ) / MB
        new_mb = new.get( key, 0 ) / MB
        delta = new_mb - old_mb

        if key not in old:
            state = 'ADDED  '
        elif key not in new:
            state = 'REMOVED'
        elif abs( delta ) < 0.05:
            continue
        else:
            state = 'GREW   ' if delta > 0 else 'SHRANK '

        if delta > 0.05:
            grew += 1

        print(
            '%s %8.1f -> %8.1f MB  (%+7.1f)  %s/%s/%s' % (
                state, old_mb, new_mb, delta, platform_tag, module, tu
                )
            )

    return grew


def main():

    parser = argparse.ArgumentParser( description = 'measure and gate unit-test object file sizes' )

    parser.add_argument(
        '--bld-dir',
        default = None,
        help = 'the build root to walk (default: <repo>/bld)',
        )

    parser.add_argument(
        '--capture',
        metavar = 'PATH',
        default = None,
        help = 'write the manifest as JSON to PATH',
        )

    parser.add_argument(
        '--ceiling',
        type = float,
        default = None,
        help = 'fail with a non-zero exit code if any gated object exceeds this many MB',
        )

    parser.add_argument(
        '--gate-pattern',
        default = r'^win-x86-.*-debug$',
        help = 'only platform tags matching this regex are gated (default: x86 debug, where the ceiling was calibrated)',
        )

    parser.add_argument(
        '--compare',
        metavar = 'PATH',
        default = None,
        help = 'compare the current tree against an earlier manifest',
        )

    parser.add_argument(
        '--quiet',
        action = 'store_true',
        help = 'suppress the per-object table',
        )

    args = parser.parse_args()

    bld_dir = args.bld_dir or os.path.join( repo_root(), 'bld' )

    trees = collect( bld_dir )

    if not trees:
        print( 'utf_objsize: no unit-test objects found under %s' % bld_dir, file = sys.stderr )
        print( 'utf_objsize: build at least one test module before capturing a baseline', file = sys.stderr )
        return 2

    if args.capture:
        with open( args.capture, 'w' ) as stream:
            json.dump( trees, stream, indent = 2, sort_keys = True )
            stream.write( '\n' )
        print( 'utf_objsize: wrote %s' % args.capture )

    if args.compare:
        with open( args.compare ) as stream:
            before = json.load( stream )
        grew = compare( before, trees )
        print( '' )
        print( 'utf_objsize: %d object(s) grew' % grew )

    over = []

    if not args.quiet or args.ceiling is not None:
        ceiling = args.ceiling if args.ceiling is not None else DEFAULT_CEILING_MB
        over = report( trees, ceiling, args.gate_pattern )

    total = len( flatten( trees ) )

    print( '' )
    print( 'utf_objsize: %d object(s) across %d build tree(s)' % ( total, len( trees ) ) )

    if args.ceiling is not None:

        if over:
            print( '' )
            print( 'utf_objsize: FAIL - %d object(s) exceed the %.0f MB ceiling:' % ( len( over ), args.ceiling ) )
            for platform_tag, module, tu, mb in over:
                print( '    %8.1f MB  %s/%s/%s' % ( mb, platform_tag, module, tu ) )
            return 1

        print( 'utf_objsize: PASS - every gated object is within the %.0f MB ceiling' % args.ceiling )

    return 0


if __name__ == '__main__':
    sys.exit( main() )
