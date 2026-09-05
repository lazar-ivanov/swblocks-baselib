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
  # a platform without a debugger configuration cannot produce a crash dump analysis; say
  # so and let the child's exit code through rather than replacing it with a KeyError
  config = cfg.get(platform)
  if config is None:
    print('!' * 5, 'no crash dump configuration for platform', platform,
          '- skipping the debugger', file=stderr)
    return

  # setup parameters
  testdir = dirname(argv[1])
  params = { 'testdir': testdir, 'testname': basename(argv[1]),
             'testfile': argv[1], 'pid': proc.pid }
  dumpfile = config['dumpfile'] % params
  params['dumpfile'] = dumpfile

  # locate dumpfile
  if exists(dumpfile):
    print('!' * 5, 'crash dump file', dumpfile, 'found')

    # replace formatting strings in debugger command line
    debugger = [a % params for a in config['debugger']]

    # run debugger to dump stack trace to stdout
    call(debugger)

    newfile = join(testdir, basename(dumpfile))
    print('!' * 5, 'crash dump file upload path is', newfile)

    # move dump file to build directory for upload
    rename(dumpfile, newfile)

def process_output(proc):
  for raw_line in proc.stdout:
    if isinstance(raw_line, bytes):
      # decode losslessly: UTF-8 first, which is the normal case on Linux and macOS, then
      # latin-1, which maps bytes 0-255 bijectively and therefore cannot fail. Unlike
      # errors='replace' this keeps non-UTF-8 diagnostics readable rather than collapsing
      # every offending byte to U+FFFD
      try:
        line = raw_line.decode('utf-8')
      except UnicodeDecodeError:
        line = raw_line.decode('latin-1')
    else:
      line = raw_line
    line = line.rstrip()
    if search(': (fatal|error|warn)', line) and not line.startswith('DEBUG:'):
      print('\n######### Failure in %s #########\n %s \n' %(basename(argv[1]), line), file=stderr)
      print('Please see full error details in the log file (search for the test name)\n', file=stderr)
    print(line, file=stdout)
  proc.wait()

# Reconfigure stdout/stderr to use UTF-8 encoding with error replacement
# This prevents UnicodeEncodeError when test output contains non-ASCII characters
# (e.g., file paths with accented names) and stdout is piped through make (ASCII encoding)
# Required on all platforms: Windows (cp1252 default) and Linux/macOS (ASCII when piped)
# Deferred to __main__ to avoid detaching sys.stdout.buffer when imported by tests
def _reconfigure_stdio():
  global stdout, stderr
  import sys
  if sys.version_info[0] >= 3:
    import io
    stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace', line_buffering=True)
    stderr = io.TextIOWrapper(sys.stderr.buffer, encoding='utf-8', errors='replace', line_buffering=True)
  else:
    import codecs
    stdout = codecs.getwriter('utf-8')(sys.stdout, errors='replace')
    stderr = codecs.getwriter('utf-8')(sys.stderr, errors='replace')

if __name__ == '__main__':
  # reconfigure stdio before processing output
  _reconfigure_stdio()
  # run command and deal with failures
  p = Popen(argv[1:], stdout=PIPE, stderr=STDOUT)
  process_output(p)
  if p.returncode:
    handle_failure(p)
  exit(p.returncode)
