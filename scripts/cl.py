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
# run msvs compiler with /showincludes and generate make dependencies
#

from __future__ import print_function

from optparse import OptionParser, BadOptionError
from os.path import basename, splitext
from subprocess import Popen, PIPE, STDOUT
from sys import argv, exit, getfilesystemencoding, stdout

# an options parser that will pass-through unrecognized options
class PassThroughOptionParser(OptionParser):
  def _process_long_opt(self, rargs, values):
    arg = rargs[0]
    try:
      OptionParser._process_long_opt(self, rargs, values)
    except BadOptionError as err:
      self.largs.append(arg)

  def _process_short_opts(self, rargs, values):
    arg = rargs[0]
    try:
      OptionParser._process_short_opts(self, rargs, values)
    except BadOptionError as err:
      self.largs.append(arg)

# callback for dependency option, adds -showIncludes to the command
def handle_dependency_option(option, opt, value, parser):
  setattr(parser.values, option.dest, True)
  parser.largs.append('-showIncludes')

# callback for extracting the target from the output option (-Fo)
def handle_output_options(option, opt, value, parser):
  # option parse watches for -F
  # everything else is the option value
  # we only need to take special action on -Fo
  if value[0]=='o':
    setattr(parser.values, option.dest, value[1:])
    setattr(parser.values, 'depfile', '%s.d' % (splitext(value[1:])[0],))
  parser.largs.append(opt + value)

# Reconfigure stdout to use UTF-8 encoding with error replacement
# This prevents UnicodeEncodeError when compiler output contains non-ASCII characters
# and stdout is piped through make (ASCII encoding)
# Deferred to __main__ to avoid detaching sys.stdout.buffer when imported by tests
def _reconfigure_stdio():
  global stdout
  import sys
  if sys.version_info[0] >= 3:
    import io
    stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace', line_buffering=True)
  else:
    import codecs
    stdout = codecs.getwriter('utf-8')(sys.stdout, errors='replace')

# Encode a dependency file entry back to bytes
# Paths captured from the compiler output are decoded as latin-1, which maps bytes 0-255
# bijectively, so encoding them back to latin-1 reproduces the original bytes exactly and
# make can still resolve them; the target comes from the command line as real text, so it
# falls back to the filesystem encoding when it does not fit in latin-1
def _encode_dependency_text(text):
  if isinstance(text, bytes):
    # Python 2 native strings are already byte strings and need no encoding
    return text
  try:
    return text.encode('latin-1')
  except UnicodeError:
    return text.encode(getfilesystemencoding(), 'replace')

parser = PassThroughOptionParser()
parser.add_option('-M',
          action='callback',
          callback=handle_dependency_option,
          dest='dependencies',
          help='write dependency file')
parser.add_option('-F',
          action='callback',
          callback=handle_output_options,
          type='string', dest='target',
          help='standard MSVC cl.exe output options')

if __name__ == '__main__':
  _reconfigure_stdio()

  # parse options
  (options, args) = parser.parse_args()

  # if not output option is specified, attempt to infer
  # the target from the first non-option argument
  if options.dependencies and not options.target:
    firstNonOpt = list(filter(lambda a: a[0] not in ('-', '/'), args))[0]
    options.target = '%s.obj' % (splitext(firstNonOpt)[0],)

  # insert the compiler command (basename of this script)
  args.insert(0, splitext(basename(argv[0]))[0])

  # run the command tracking dependencies
  deps = []
  p = Popen(args, stdout=PIPE, stderr=STDOUT)
  for raw_line in p.stdout:
    if isinstance(raw_line, bytes):
      # latin-1 is lossless for arbitrary bytes, so dependency paths stay byte-exact
      # regardless of the code page the compiler used for its output
      line = raw_line.decode('latin-1')
    else:
      line = raw_line
    line = line.rstrip()
    if line.startswith('Note: including file:'):
      dep = line.split()[-1]
      if dep not in deps:
        deps.append(dep)
    else:
      print(line, file=stdout)
  p.wait()

  # if successful, write the dependency file
  # the file is written in binary mode so the dependency paths are emitted as the exact
  # bytes the compiler produced
  if p.returncode == 0 and options.dependencies:
    f = open('%s.d' % splitext(options.target)[0], 'wb')
    f.write(_encode_dependency_text('%s: \\\n' % (options.target,)))
    for dep in deps:
      f.write(_encode_dependency_text(' %s \\\n' % (dep,)))
    f.write(b'\n\n')
    for dep in deps:
      f.write(_encode_dependency_text('%s:\n' % (dep,)))
    f.close()

  exit(p.returncode)
