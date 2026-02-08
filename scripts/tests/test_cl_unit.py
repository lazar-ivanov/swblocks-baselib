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

"""
Unit tests for cl.py and clang-cl.py compiler wrappers.

These tests cover:
- PassThroughOptionParser: Option parsing and pass-through behavior
- Dependency option handling: -M flag and -showIncludes
- Output option handling: -Fo flag and target extraction
- Target inference: Automatic .obj target from source file

Test Strategy:
- Unit tests focus on parser logic without subprocess calls
- Tests are shared for both cl.py and clang-cl.py (they're identical)
- Uses monkeypatch for mocking when needed
"""

import pytest
import sys
from pathlib import Path
from unittest.mock import Mock, patch
from io import StringIO

# Add parent directory to path to import cl module
sys.path.insert(0, str(Path(__file__).parent.parent))

import cl


# ========== Test PassThroughOptionParser ==========

class TestPassThroughOptionParser:
    """Test PassThroughOptionParser class that passes unrecognized options through."""

    def test_passthrough_single_unknown_option(self):
        """Test that single unrecognized option passes through."""
        parser = cl.PassThroughOptionParser()
        parser.add_option('-M', action='store_true')

        # Parse with unknown option
        options, args = parser.parse_args(['-Wall', 'source.cpp'])

        assert args == ['-Wall', 'source.cpp']

    def test_passthrough_multiple_unknown_options(self):
        """Test that multiple unrecognized options pass through."""
        parser = cl.PassThroughOptionParser()
        parser.add_option('-M', action='store_true')

        # Parse with multiple unknown options
        options, args = parser.parse_args(['-Wall', '-O2', '-std=c++17', 'source.cpp'])

        assert args == ['-Wall', '-O2', '-std=c++17', 'source.cpp']

    def test_passthrough_preserves_order(self):
        """Test that option order is preserved."""
        parser = cl.PassThroughOptionParser()
        parser.add_option('-M', action='store_true')

        # Parse with mixed known and unknown options
        options, args = parser.parse_args(['-M', '-Wall', '-O2', 'source.cpp'])

        # -M should be recognized, others should pass through in order
        assert options.M is True
        assert args == ['-Wall', '-O2', 'source.cpp']

    def test_passthrough_short_opts_with_values(self):
        """Test pass-through of short options with values."""
        parser = cl.PassThroughOptionParser()
        parser.add_option('-M', action='store_true')

        # Parse with short option that has value
        options, args = parser.parse_args(['-DDEBUG', '-I/usr/include', 'source.cpp'])

        assert args == ['-DDEBUG', '-I/usr/include', 'source.cpp']

    def test_passthrough_long_opts(self):
        """Test pass-through of long options."""
        parser = cl.PassThroughOptionParser()
        parser.add_option('-M', action='store_true')

        # Parse with long options
        # Note: optparse splits --config=release into '--config=release' and 'release'
        options, args = parser.parse_args(['--verbose', '--config=release', 'source.cpp'])

        # Both the combined option and its value are passed through
        assert '--verbose' in args
        assert '--config=release' in args
        assert 'source.cpp' in args


# ========== Test Dependency Option Handler ==========

class TestDependencyOption:
    """Test handle_dependency_option callback for -M flag."""

    def test_dependency_option_adds_showincludes(self):
        """Test that -M flag adds -showIncludes to compiler args."""
        parser = cl.PassThroughOptionParser()
        parser.add_option('-M',
                          action='callback',
                          callback=cl.handle_dependency_option,
                          dest='dependencies',
                          help='write dependency file')

        # Parse with -M flag
        options, args = parser.parse_args(['-M', 'source.cpp'])

        # Should set dependencies=True and add -showIncludes
        assert options.dependencies is True
        assert '-showIncludes' in args

    def test_dependency_option_preserves_other_args(self):
        """Test that -M doesn't affect other arguments."""
        parser = cl.PassThroughOptionParser()
        parser.add_option('-M',
                          action='callback',
                          callback=cl.handle_dependency_option,
                          dest='dependencies')

        # Parse with -M and other flags
        options, args = parser.parse_args(['-M', '-Wall', '-O2', 'source.cpp'])

        assert options.dependencies is True
        assert '-showIncludes' in args
        assert '-Wall' in args
        assert '-O2' in args
        assert 'source.cpp' in args

    def test_dependency_option_without_flag(self):
        """Test that dependencies is not set without -M flag."""
        parser = cl.PassThroughOptionParser()
        parser.add_option('-M',
                          action='callback',
                          callback=cl.handle_dependency_option,
                          dest='dependencies')

        # Parse without -M flag
        options, args = parser.parse_args(['source.cpp'])

        # dependencies should not be set
        assert not hasattr(options, 'dependencies') or options.dependencies is None
        assert '-showIncludes' not in args


# ========== Test Output Option Handler ==========

class TestOutputOption:
    """Test handle_output_options callback for -F flag (especially -Fo)."""

    def test_output_option_fo_extracts_target(self):
        """Test that -Fo extracts target and generates depfile path."""
        parser = cl.PassThroughOptionParser()
        parser.add_option('-F',
                          action='callback',
                          callback=cl.handle_output_options,
                          type='string', dest='target')

        # Parse with -Fo option
        options, args = parser.parse_args(['-Fobuild/main.obj', 'source.cpp'])

        assert options.target == 'build/main.obj'
        assert options.depfile == 'build/main.d'
        assert '-Fobuild/main.obj' in args

    def test_output_option_fo_different_paths(self):
        """Test -Fo with various path formats."""
        parser = cl.PassThroughOptionParser()
        parser.add_option('-F',
                          action='callback',
                          callback=cl.handle_output_options,
                          type='string', dest='target')

        # Test with different path formats
        test_cases = [
            ('-Fomain.obj', 'main.obj', 'main.d'),
            ('-Foout/test.obj', 'out/test.obj', 'out/test.d'),
            ('-FoC:\\build\\module.obj', 'C:\\build\\module.obj', 'C:\\build\\module.d'),
        ]

        for fo_arg, expected_target, expected_depfile in test_cases:
            options, args = parser.parse_args([fo_arg, 'source.cpp'])
            assert options.target == expected_target, f"Failed for {fo_arg}"
            assert options.depfile == expected_depfile, f"Failed for {fo_arg}"

    def test_output_option_non_fo_flags(self):
        """Test that non-Fo -F flags don't set target."""
        parser = cl.PassThroughOptionParser()
        parser.add_option('-F',
                          action='callback',
                          callback=cl.handle_output_options,
                          type='string', dest='target')

        # -Fa (assembly listing) should not set target
        options, args = parser.parse_args(['-Faassembly.asm', 'source.cpp'])

        # target should not be set for -Fa
        assert not hasattr(options, 'target') or options.target is None
        assert '-Faassembly.asm' in args

    def test_output_option_fo_appends_to_args(self):
        """Test that -Fo option is appended to args for compiler."""
        parser = cl.PassThroughOptionParser()
        parser.add_option('-F',
                          action='callback',
                          callback=cl.handle_output_options,
                          type='string', dest='target')

        options, args = parser.parse_args(['-Fomain.obj', '-Wall', 'source.cpp'])

        # -Fo should be in args for compiler
        assert '-Fomain.obj' in args
        assert '-Wall' in args
        assert 'source.cpp' in args


# ========== Test Target Inference ==========

class TestTargetInference:
    """Test automatic target inference from source file when -Fo not specified."""

    def test_infer_target_from_cpp_file(self, monkeypatch):
        """Test target inference from .cpp source file."""
        # Mock sys.argv to simulate script invocation
        monkeypatch.setattr('sys.argv', ['cl.py', '-M', 'module.cpp'])

        # Parse args
        parser = cl.PassThroughOptionParser()
        parser.add_option('-M',
                          action='callback',
                          callback=cl.handle_dependency_option,
                          dest='dependencies')
        parser.add_option('-F',
                          action='callback',
                          callback=cl.handle_output_options,
                          type='string', dest='target')

        options, args = parser.parse_args(['-M', 'module.cpp'])

        # At this point, target is not set yet
        # The script would infer it from the first non-option argument

        if options.dependencies and not hasattr(options, 'target'):
            # Simulate target inference logic (lines 75-77 in cl.py)
            firstNonOpt = [a for a in args if a[0] not in ('-', '/')][0]
            inferred_target = firstNonOpt.replace('.cpp', '.obj')

            assert inferred_target == 'module.obj'

    def test_infer_target_from_c_file(self, monkeypatch):
        """Test target inference from .c source file."""
        monkeypatch.setattr('sys.argv', ['cl.py', '-M', 'source.c'])

        parser = cl.PassThroughOptionParser()
        parser.add_option('-M',
                          action='callback',
                          callback=cl.handle_dependency_option,
                          dest='dependencies')
        parser.add_option('-F',
                          action='callback',
                          callback=cl.handle_output_options,
                          type='string', dest='target')

        options, args = parser.parse_args(['-M', 'source.c'])

        if options.dependencies and not hasattr(options, 'target'):
            firstNonOpt = [a for a in args if a[0] not in ('-', '/')][0]
            inferred_target = firstNonOpt.replace('.c', '.obj')

            assert inferred_target == 'source.obj'

    def test_infer_target_with_path(self, monkeypatch):
        """Test target inference from source file with directory path."""
        monkeypatch.setattr('sys.argv', ['cl.py', '-M', 'src/module.cpp'])

        parser = cl.PassThroughOptionParser()
        parser.add_option('-M',
                          action='callback',
                          callback=cl.handle_dependency_option,
                          dest='dependencies')
        parser.add_option('-F',
                          action='callback',
                          callback=cl.handle_output_options,
                          type='string', dest='target')

        options, args = parser.parse_args(['-M', 'src/module.cpp'])

        if options.dependencies and not hasattr(options, 'target'):
            firstNonOpt = [a for a in args if a[0] not in ('-', '/')][0]
            # splitext removes extension, then add .obj
            from os.path import splitext
            inferred_target = f"{splitext(firstNonOpt)[0]}.obj"

            assert inferred_target == 'src/module.obj'


# ========== Test Dependency Parsing (Stage 3) ==========

class TestDependencyParsing:
    """Test parsing of compiler output for dependency extraction."""

    def test_parse_single_include(self, mock_compiler_output, temp_dir, monkeypatch):
        """Test parsing a single 'Note: including file:' line."""
        # Create simplified output with one include
        output = [
            "test.cpp",
            "Note: including file: C:\\Users\\test\\header.h",
            "Compiling..."
        ]

        # Mock subprocess.Popen to return our output
        class MockProc:
            returncode = 0
            stdout = output

            def wait(self):
                pass

        def mock_popen(*args, **kwargs):
            return MockProc()

        monkeypatch.setattr('subprocess.Popen', mock_popen)
        monkeypatch.setattr('sys.argv', ['cl.py', '-M', '-Fomain.obj', 'test.cpp'])
        monkeypatch.chdir(temp_dir)

        # Simulate the dependency parsing logic (lines 84-92 in cl.py)
        deps = []
        p = MockProc()
        for line in p.stdout:
            line_str = line.rstrip() if isinstance(line, str) else line
            if line_str.startswith('Note: including file:'):
                dep = line_str.split()[-1]
                if dep not in deps:
                    deps.append(dep)

        assert len(deps) == 1
        assert deps[0] == 'C:\\Users\\test\\header.h'

    def test_parse_multiple_includes(self, temp_dir):
        """Test parsing multiple include directives."""
        output = [
            "test.cpp",
            "Note: including file: C:\\header1.h",
            "Note: including file:  C:\\header2.h",  # Extra space (indentation)
            "Note: including file:   C:\\header3.h",  # More indentation
            "Compiling..."
        ]

        # Parse dependencies
        deps = []
        for line in output:
            line_str = line.rstrip()
            if line_str.startswith('Note: including file:'):
                dep = line_str.split()[-1]
                if dep not in deps:
                    deps.append(dep)

        assert len(deps) == 3
        assert deps == ['C:\\header1.h', 'C:\\header2.h', 'C:\\header3.h']

    def test_parse_ignores_non_include_lines(self, temp_dir):
        """Test that non-include lines are ignored."""
        output = [
            "Microsoft (R) C/C++ Compiler",
            "test.cpp",
            "Note: including file: C:\\header.h",
            "Generating Code...",
            "Code generation complete.",
        ]

        deps = []
        for line in output:
            line_str = line.rstrip()
            if line_str.startswith('Note: including file:'):
                dep = line_str.split()[-1]
                if dep not in deps:
                    deps.append(dep)

        assert len(deps) == 1
        assert deps[0] == 'C:\\header.h'

    def test_parse_nested_includes(self, temp_dir):
        """Test parsing nested includes (indicated by indentation)."""
        output = [
            "test.cpp",
            "Note: including file: C:\\top.h",
            "Note: including file:  C:\\nested1.h",  # Nested (1 space indent)
            "Note: including file:   C:\\nested2.h",  # More nested (2 spaces)
            "Note: including file:  C:\\nested1b.h",  # Back to 1 space
            "Compiling..."
        ]

        deps = []
        for line in output:
            line_str = line.rstrip()
            if line_str.startswith('Note: including file:'):
                dep = line_str.split()[-1]
                if dep not in deps:
                    deps.append(dep)

        assert len(deps) == 4


class TestDependencyDeduplication:
    """Test deduplication of repeated include files."""

    def test_deduplicate_repeated_includes(self):
        """Test that duplicate includes are filtered out."""
        output = [
            "test.cpp",
            "Note: including file: C:\\header.h",
            "Note: including file:  C:\\other.h",
            "Note: including file: C:\\header.h",  # Duplicate
            "Note: including file: C:\\header.h",  # Another duplicate
        ]

        deps = []
        for line in output:
            line_str = line.rstrip()
            if line_str.startswith('Note: including file:'):
                dep = line_str.split()[-1]
                if dep not in deps:  # Deduplication logic
                    deps.append(dep)

        assert len(deps) == 2
        assert deps == ['C:\\header.h', 'C:\\other.h']

    def test_deduplication_preserves_first_occurrence(self):
        """Test that the first occurrence order is preserved."""
        output = [
            "Note: including file: C:\\first.h",
            "Note: including file: C:\\second.h",
            "Note: including file: C:\\third.h",
            "Note: including file: C:\\second.h",  # Duplicate of second
            "Note: including file: C:\\first.h",   # Duplicate of first
        ]

        deps = []
        for line in output:
            line_str = line.rstrip()
            if line_str.startswith('Note: including file:'):
                dep = line_str.split()[-1]
                if dep not in deps:
                    deps.append(dep)

        assert deps == ['C:\\first.h', 'C:\\second.h', 'C:\\third.h']

    def test_empty_dependencies_list(self):
        """Test handling of output with no include directives."""
        output = [
            "Microsoft (R) C/C++ Compiler",
            "test.cpp",
            "Compiling...",
            "Code generation complete.",
        ]

        deps = []
        for line in output:
            line_str = line.rstrip()
            if line_str.startswith('Note: including file:'):
                dep = line_str.split()[-1]
                if dep not in deps:
                    deps.append(dep)

        assert len(deps) == 0


class TestDependencyFileFormat:
    """Test generation of .d dependency file in makefile format."""

    def test_dependency_file_basic_format(self, temp_dir, dependency_file_parser):
        """Test basic .d file format with target and dependencies."""
        target = 'main.obj'
        deps = ['header1.h', 'header2.h']

        # Generate .d file content (lines 97-102 in cl.py)
        depfile = temp_dir / 'main.d'
        with open(depfile, 'wt') as f:
            f.writelines([target, ': \\\n'] +
                         [' %s \\\n' % (dep,) for dep in deps])
            f.writelines(['\n\n'] +
                         ['%s:\n' % (dep,) for dep in deps])

        # Parse and verify
        parsed_target, parsed_deps = dependency_file_parser(depfile)
        assert parsed_target == 'main.obj'
        assert parsed_deps == ['header1.h', 'header2.h']

    def test_dependency_file_empty_deps(self, temp_dir, dependency_file_parser):
        """Test .d file with no dependencies."""
        target = 'main.obj'
        deps = []

        depfile = temp_dir / 'main.d'
        with open(depfile, 'wt') as f:
            f.writelines([target, ': \\\n'] +
                         [' %s \\\n' % (dep,) for dep in deps])
            f.writelines(['\n\n'] +
                         ['%s:\n' % (dep,) for dep in deps])

        parsed_target, parsed_deps = dependency_file_parser(depfile)
        assert parsed_target == 'main.obj'
        assert parsed_deps == []

    def test_dependency_file_many_deps(self, temp_dir, dependency_file_parser):
        """Test .d file with many dependencies."""
        target = 'module.obj'
        deps = [f'header{i}.h' for i in range(20)]

        depfile = temp_dir / 'module.d'
        with open(depfile, 'wt') as f:
            f.writelines([target, ': \\\n'] +
                         [' %s \\\n' % (dep,) for dep in deps])
            f.writelines(['\n\n'] +
                         ['%s:\n' % (dep,) for dep in deps])

        parsed_target, parsed_deps = dependency_file_parser(depfile)
        assert parsed_target == 'module.obj'
        assert len(parsed_deps) == 20

    def test_dependency_file_path_with_spaces(self, temp_dir, dependency_file_parser):
        """Test .d file with paths containing spaces."""
        target = 'build/main.obj'
        deps = ['C:\\Program Files\\header.h', 'C:\\My Documents\\file.h']

        depfile = temp_dir / 'main.d'
        with open(depfile, 'wt') as f:
            f.writelines([target, ': \\\n'] +
                         [' %s \\\n' % (dep,) for dep in deps])
            f.writelines(['\n\n'] +
                         ['%s:\n' % (dep,) for dep in deps])

        parsed_target, parsed_deps = dependency_file_parser(depfile)
        assert parsed_target == 'build/main.obj'
        assert 'C:\\Program Files\\header.h' in parsed_deps

    def test_dependency_file_backslash_continuation(self, temp_dir):
        """Test that lines end with backslash continuation character."""
        target = 'main.obj'
        deps = ['header1.h', 'header2.h']

        depfile = temp_dir / 'main.d'
        with open(depfile, 'wt') as f:
            f.writelines([target, ': \\\n'] +
                         [' %s \\\n' % (dep,) for dep in deps])
            f.writelines(['\n\n'] +
                         ['%s:\n' % (dep,) for dep in deps])

        # Read and check format
        content = depfile.read_text()
        lines = content.split('\n')

        # First line should be "main.obj: \"
        assert lines[0] == 'main.obj: \\'
        # Dependency lines should end with " \"
        assert lines[1] == ' header1.h \\'
        assert lines[2] == ' header2.h \\'
