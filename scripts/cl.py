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

from optparse import OptionParser, BadOptionError
from os.path import basename, splitext
from subprocess import Popen, PIPE, STDOUT
from sys import argv, exit

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
      line = raw_line.decode('utf-8', errors='replace')
    else:
      line = raw_line
    line = line.rstrip()
    if line.startswith('Note: including file:'):
      dep = line.split()[-1]
      if dep not in deps:
        deps.append(dep)
    else:
      print(line)
  p.wait()

  # if successful, write the dependency file
  if p.returncode == 0 and options.dependencies:
    f = open('%s.d' % splitext(options.target)[0], 'wt')
    f.writelines([options.target, ': \\\n' ] +
                 [' %s \\\n' % (dep,) for dep in deps])
    f.writelines(['\n\n' ] +
                 ['%s:\n' % (dep,) for dep in deps])
    f.close()

  exit(p.returncode)
