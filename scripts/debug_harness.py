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
# run programs and capture diagnostics on failure
#
# on linux, core dump must be enabled
# see /etc/security/limits.conf and maybe /etc/profile
#
# on windows, crash dumps must be enabled per
# http://msdn.microsoft.com/en-us/library/windows/desktop/bb787181%28v=vs.85%29.aspx
# also, see settings/windows/enable-user-mode-dumps.reg
#

# Enable Python 3's print function in Python 2.7+ for compatibility
# This allows print(..., file=stderr) syntax to work in both Python 2 and 3
# Required for devenv4-6 (Python 2.x) and devenv7+ (Python 3.x)
from __future__ import print_function

from sys import argv, exit, platform, stderr, stdout
from re import search
from subprocess import Popen, call, PIPE, STDOUT
from os import rename, getenv
from os.path import exists, basename, dirname, join

# Reconfigure stdout to use UTF-8 encoding on Windows to handle Unicode characters
# This prevents UnicodeEncodeError when test output contains characters not in cp1252
if platform == 'win32':
    import sys
    import io
    import codecs
    # Reconfigure for both Python 2 and 3, using different methods
    if sys.version_info[0] >= 3:
        # Python 3: Use io.TextIOWrapper with stdout.buffer
        stdout = io.TextIOWrapper(stdout.buffer, encoding='utf-8', errors='replace', line_buffering=True)
        stderr = io.TextIOWrapper(stderr.buffer, encoding='utf-8', errors='replace', line_buffering=True)
    else:
        # Python 2: Use codecs.getwriter() to wrap stdout/stderr
        # This allows Unicode strings to be encoded to UTF-8 when printed
        stdout = codecs.getwriter('utf-8')(stdout, errors='replace')
        stderr = codecs.getwriter('utf-8')(stderr, errors='replace')

# per-os configuration
cfg = {
  'linux2': {
    'debugger': ['gdb', '--batch', '-ex', 'thread apply all bt',
                 '-ex', 'quit', '%(testfile)s', '%(dumpfile)s'],
    'dumpfile': 'core.%(pid)d'
  },
  'darwin': {
    'debugger': ['gdb', '--batch', '-ex', 'thread apply all bt',
                 '-ex', 'quit', '%(testfile)s', '%(dumpfile)s'],
    'dumpfile': 'core.%(pid)d'
  },
  'win32': {
    'debugger': ['cdb', '-c', '!analyze -v; q',
                 '-z', '%(dumpfile)s'],
    'dumpfile': join(getenv('LOCALAPPDATA', ''),
                     'CrashDumps', '%(testname)s.%(pid)d.dmp')
  }
}

# make this work on Python 3.3 or higher
# Prior to Python 3.3, the value for any Linux version is always linux2; after, it is linux.
# https://stackoverflow.com/questions/446209/possible-values-from-sys-platform
cfg['linux'] = cfg['linux2']

def handle_failure(proc):
  # setup parameters
  testdir = dirname(argv[1])
  params = { 'testdir': testdir, 'testname': basename(argv[1]),
             'testfile': argv[1], 'pid': proc.pid }
  dumpfile = cfg[platform]['dumpfile'] % params
  params['dumpfile'] = dumpfile

  # locate dumpfile
  if exists(dumpfile):
    print('!' * 5, 'crash dump file', dumpfile, 'found')

    # replace formatting strings in debugger command line
    debugger = [a % params for a in cfg[platform]['debugger']]

    # run debugger to dump stack trace to stdout
    call(debugger)

    newfile = join(testdir, basename(dumpfile))
    print('!' * 5, 'crash dump file upload path is', newfile)

    # move dump file to build directory for upload
    rename(dumpfile, newfile)

def process_output(proc):
  for raw_line in proc.stdout:
    if isinstance(raw_line, bytes):
      line = raw_line.decode('utf-8', errors='replace')
    else:
      line = raw_line
    line = line.rstrip()
    if search(': (fatal|error|warn)', line) and not line.startswith('DEBUG:'):
      print('\n######### Failure in %s #########\n %s \n' %(basename(argv[1]), line), file=stderr)
      print('Please see full error details in the log file (search for the test name)\n', file=stderr)
    print(line, file=stdout)
  proc.wait()

if __name__ == '__main__':
  # run command and deal with failures
  p = Popen(argv[1:], stdout=PIPE, stderr=STDOUT)
  process_output(p)
  if p.returncode:
    handle_failure(p)
  exit(p.returncode)
