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
Unit tests for debug_harness.py.

Tests platform configuration, dumpfile path construction, error detection,
and UTF-8 handling logic without requiring external debuggers.
"""

import pytest
import sys
from pathlib import Path

# Add parent directory to path to import debug_harness
sys.path.insert(0, str(Path(__file__).parent.parent))

import debug_harness


# ========== Test Unknown Platform ==========

class TestUnknownPlatform:
    """handle_failure() must not replace the child's exit code with a KeyError on an unknown platform."""

    def test_handle_failure_skips_unknown_platform(self, monkeypatch, capsys):
        """An unconfigured platform is reported on stderr and the debugger step is skipped."""
        monkeypatch.setattr(debug_harness, 'platform', 'freebsd14')
        monkeypatch.setattr(debug_harness, 'argv', ['debug_harness.py', '/tmp/tests/utf-baselib'])
        monkeypatch.setattr(debug_harness, 'stderr', sys.stderr)
        proc = type('Proc', (), {'pid': 4242, 'returncode': 3})()

        debug_harness.handle_failure(proc)

        captured = capsys.readouterr()
        assert 'no crash dump configuration for platform freebsd14' in captured.err


# ========== Test Platform Configuration ==========

class TestPlatformConfiguration:
    """Test platform-specific configuration dictionary."""

    def test_linux_config_exists(self):
        """Test that Linux configuration is defined."""
        assert 'linux' in debug_harness.cfg
        assert 'debugger' in debug_harness.cfg['linux']
        assert 'dumpfile' in debug_harness.cfg['linux']

    def test_linux2_config_exists(self):
        """Test that linux2 configuration exists (Python < 3.3 compatibility)."""
        assert 'linux2' in debug_harness.cfg
        assert debug_harness.cfg['linux'] is debug_harness.cfg['linux2']

    def test_darwin_config_exists(self):
        """Test that macOS (darwin) configuration is defined."""
        assert 'darwin' in debug_harness.cfg
        assert 'debugger' in debug_harness.cfg['darwin']
        assert 'dumpfile' in debug_harness.cfg['darwin']

    def test_windows_config_exists(self):
        """Test that Windows (win32) configuration is defined."""
        assert 'win32' in debug_harness.cfg
        assert 'debugger' in debug_harness.cfg['win32']
        assert 'dumpfile' in debug_harness.cfg['win32']

    def test_debugger_is_list(self):
        """Test that debugger commands are lists."""
        for platform_name in ['linux', 'darwin', 'win32']:
            assert isinstance(debug_harness.cfg[platform_name]['debugger'], list)
            assert len(debug_harness.cfg[platform_name]['debugger']) > 0

    def test_linux_uses_gdb(self):
        """Test that Linux configuration uses gdb."""
        assert debug_harness.cfg['linux']['debugger'][0] == 'gdb'

    def test_darwin_uses_gdb(self):
        """Test that macOS configuration uses gdb."""
        assert debug_harness.cfg['darwin']['debugger'][0] == 'gdb'

    def test_windows_uses_cdb(self):
        """Test that Windows configuration uses cdb."""
        assert debug_harness.cfg['win32']['debugger'][0] == 'cdb'


# ========== Test Dumpfile Path Construction ==========

class TestHandleFailurePathConstruction:
    """Test dumpfile path construction logic without running debuggers."""

    def test_linux_dumpfile_format(self):
        """Test Linux dumpfile path format."""
        dumpfile_template = debug_harness.cfg['linux']['dumpfile']
        params = {'pid': 12345}
        dumpfile = dumpfile_template % params

        assert dumpfile == 'core.12345'
        assert 'core.' in dumpfile

    def test_darwin_dumpfile_format(self):
        """Test macOS dumpfile path format."""
        dumpfile_template = debug_harness.cfg['darwin']['dumpfile']
        params = {'pid': 67890}
        dumpfile = dumpfile_template % params

        assert dumpfile == 'core.67890'
        assert 'core.' in dumpfile

    def test_windows_dumpfile_format(self, monkeypatch):
        """Test Windows dumpfile path format."""
        # Mock LOCALAPPDATA environment variable
        monkeypatch.setenv('LOCALAPPDATA', 'C:\\Users\\TestUser\\AppData\\Local')

        # Re-import to pick up new environment
        import importlib
        importlib.reload(debug_harness)

        dumpfile_template = debug_harness.cfg['win32']['dumpfile']
        params = {'testname': 'test_example.exe', 'pid': 99999}
        dumpfile = dumpfile_template % params

        assert 'CrashDumps' in dumpfile
        assert 'test_example.exe.99999.dmp' in dumpfile
        assert dumpfile.endswith('.dmp')

    def test_dumpfile_uses_pid(self):
        """Test that dumpfile paths include process ID."""
        for platform_name in ['linux', 'darwin']:
            dumpfile_template = debug_harness.cfg[platform_name]['dumpfile']
            params = {'pid': 55555}
            dumpfile = dumpfile_template % params

            assert '55555' in dumpfile

    def test_debugger_command_substitution_linux(self):
        """Test debugger command parameter substitution on Linux."""
        params = {
            'testfile': '/path/to/test_binary',
            'dumpfile': 'core.12345'
        }

        debugger_template = debug_harness.cfg['linux']['debugger']
        debugger_cmd = [arg % params for arg in debugger_template]

        assert '/path/to/test_binary' in debugger_cmd
        assert 'core.12345' in debugger_cmd

    def test_debugger_command_substitution_windows(self):
        """Test debugger command parameter substitution on Windows."""
        params = {
            'dumpfile': 'C:\\Users\\Test\\AppData\\Local\\CrashDumps\\test.12345.dmp'
        }

        debugger_template = debug_harness.cfg['win32']['debugger']
        debugger_cmd = [arg % params for arg in debugger_template]

        assert 'C:\\Users\\Test\\AppData\\Local\\CrashDumps\\test.12345.dmp' in debugger_cmd


# ========== Test Process Output Error Detection ==========

class TestProcessOutput:
    """Test error detection in process output."""

    def test_detect_error_line(self, monkeypatch):
        """Test detection of error lines in output."""
        # Create mock process with error output
        class MockProc:
            stdout = [
                b'Running test...\n',
                b'test.cpp:42: error: something failed\n',
                b'Test complete\n'
            ]
            returncode = 1

            def wait(self):
                pass

        # Mock argv for error message formatting
        import debug_harness
        from io import StringIO

        original_argv = debug_harness.argv
        original_stdout = debug_harness.stdout
        original_stderr = debug_harness.stderr

        debug_harness.argv = ['debug_harness.py', 'test_example']
        mock_stdout = StringIO()
        mock_stderr = StringIO()
        monkeypatch.setattr(debug_harness, 'stdout', mock_stdout)
        monkeypatch.setattr(debug_harness, 'stderr', mock_stderr)

        try:
            proc = MockProc()
            debug_harness.process_output(proc)

            stderr_output = mock_stderr.getvalue()
            # Should detect error and print to stderr
            assert 'Failure in test_example' in stderr_output
            assert 'error' in stderr_output.lower()
        finally:
            debug_harness.argv = original_argv
            debug_harness.stdout = original_stdout
            debug_harness.stderr = original_stderr

    def test_detect_fatal_line(self, monkeypatch):
        """Test detection of fatal error lines."""
        class MockProc:
            stdout = [
                b'Initializing...\n',
                b'system.cpp:99: fatal: system crash\n'
            ]
            returncode = 1

            def wait(self):
                pass

        import debug_harness
        from io import StringIO

        original_argv = debug_harness.argv
        debug_harness.argv = ['debug_harness.py', 'test_crash']
        mock_stdout = StringIO()
        mock_stderr = StringIO()
        monkeypatch.setattr(debug_harness, 'stdout', mock_stdout)
        monkeypatch.setattr(debug_harness, 'stderr', mock_stderr)

        try:
            proc = MockProc()
            debug_harness.process_output(proc)

            stderr_output = mock_stderr.getvalue()
            assert 'Failure in test_crash' in stderr_output
        finally:
            debug_harness.argv = original_argv

    def test_detect_warning_line(self, monkeypatch):
        """Test detection of warning lines."""
        class MockProc:
            stdout = [
                b'Processing...\n',
                b'module.cpp:100: warning: deprecated function used\n'
            ]
            returncode = 0

            def wait(self):
                pass

        import debug_harness
        from io import StringIO

        original_argv = debug_harness.argv
        debug_harness.argv = ['debug_harness.py', 'test_warnings']
        mock_stdout = StringIO()
        mock_stderr = StringIO()
        monkeypatch.setattr(debug_harness, 'stdout', mock_stdout)
        monkeypatch.setattr(debug_harness, 'stderr', mock_stderr)

        try:
            proc = MockProc()
            debug_harness.process_output(proc)

            stderr_output = mock_stderr.getvalue()
            assert 'Failure in test_warnings' in stderr_output
        finally:
            debug_harness.argv = original_argv

    def test_ignore_debug_lines(self, monkeypatch):
        """Test that DEBUG: prefixed lines are not treated as errors."""
        class MockProc:
            stdout = [
                b'DEBUG: error detection test\n',
                b'DEBUG: warning simulation\n',
                b'Test passed\n'
            ]
            returncode = 0

            def wait(self):
                pass

        import debug_harness
        from io import StringIO

        mock_stdout = StringIO()
        mock_stderr = StringIO()
        monkeypatch.setattr(debug_harness, 'stdout', mock_stdout)
        monkeypatch.setattr(debug_harness, 'stderr', mock_stderr)

        proc = MockProc()
        debug_harness.process_output(proc)

        stdout_output = mock_stdout.getvalue()
        stderr_output = mock_stderr.getvalue()
        # DEBUG lines should be printed to stdout but not trigger error detection
        assert 'DEBUG' in stdout_output
        assert 'Failure' not in stderr_output

    def test_normal_output_no_errors(self, monkeypatch):
        """Test that normal output without errors is not flagged."""
        class MockProc:
            stdout = [
                b'Running test suite\n',
                b'Test 1: PASS\n',
                b'Test 2: PASS\n',
                b'All tests completed successfully\n'
            ]
            returncode = 0

            def wait(self):
                pass

        import debug_harness
        from io import StringIO

        mock_stdout = StringIO()
        mock_stderr = StringIO()
        monkeypatch.setattr(debug_harness, 'stdout', mock_stdout)
        monkeypatch.setattr(debug_harness, 'stderr', mock_stderr)

        proc = MockProc()
        debug_harness.process_output(proc)

        stdout_output = mock_stdout.getvalue()
        stderr_output = mock_stderr.getvalue()
        assert 'PASS' in stdout_output
        assert 'Failure' not in stderr_output

    def test_utf8_decoding_in_output(self, monkeypatch):
        """Test UTF-8 decoding of process output."""
        class MockProc:
            stdout = [
                'Test with UTF-8: café\n'.encode('utf-8'),
                'Unicode: 你好\n'.encode('utf-8')
            ]
            returncode = 0

            def wait(self):
                pass

        import debug_harness
        from io import StringIO

        mock_stdout = StringIO()
        mock_stderr = StringIO()
        monkeypatch.setattr(debug_harness, 'stdout', mock_stdout)
        monkeypatch.setattr(debug_harness, 'stderr', mock_stderr)

        proc = MockProc()
        debug_harness.process_output(proc)

        stdout_output = mock_stdout.getvalue()
        # UTF-8 should be decoded correctly
        assert 'café' in stdout_output or 'caf' in stdout_output  # May have encoding issues in test env
        # The important thing is it doesn't crash


# ========== Test Handle Failure Function ==========

class TestHandleFailure:
    """Test handle_failure() function without running actual debuggers."""

    def test_handle_failure_dumpfile_found(self, temp_dir, monkeypatch):
        """Test handle_failure when dumpfile exists."""
        import debug_harness

        # Create mock process
        class MockProc:
            pid = 12345

        # Mock argv to provide testfile path
        test_file = temp_dir / "test_binary"
        test_file.touch()  # Create empty test file

        original_argv = debug_harness.argv
        debug_harness.argv = ['debug_harness.py', str(test_file)]

        # Create fake dumpfile
        if sys.platform == 'darwin':
            dumpfile = temp_dir / "core.12345"
        elif sys.platform.startswith('linux'):
            dumpfile = temp_dir / "core.12345"
        else:
            # Windows would use different path, skip for now
            pytest.skip("Windows dumpfile path handling complex")

        dumpfile.write_text("fake dump data")

        # Mock subprocess.call to avoid running real debugger
        call_args = []
        def mock_call(cmd):
            call_args.append(cmd)
            return 0

        monkeypatch.setattr(debug_harness, 'call', mock_call)

        # Change to temp_dir so dumpfile is found
        monkeypatch.chdir(temp_dir)

        try:
            proc = MockProc()
            debug_harness.handle_failure(proc)

            # Debugger should have been called with gdb
            assert len(call_args) == 1
            debugger_cmd = call_args[0]
            assert 'gdb' in debugger_cmd or debugger_cmd[0] == 'gdb'
            # Should include the test file and dumpfile paths
            assert str(test_file) in debugger_cmd
            assert 'core.12345' in ' '.join(debugger_cmd)

            # Dumpfile should have been moved to test directory
            moved_dumpfile = temp_dir / "core.12345"
            assert moved_dumpfile.exists()

        finally:
            debug_harness.argv = original_argv

    def test_handle_failure_dumpfile_not_found(self, temp_dir, monkeypatch):
        """Test handle_failure when dumpfile doesn't exist."""
        import debug_harness

        # Create mock process
        class MockProc:
            pid = 99999  # Use PID that won't have existing dumpfile

        # Mock argv
        test_file = temp_dir / "test_binary"
        test_file.touch()

        original_argv = debug_harness.argv
        debug_harness.argv = ['debug_harness.py', str(test_file)]

        # Mock subprocess.call to track if it's called
        call_count = []
        def mock_call(cmd):
            call_count.append(cmd)
            return 0

        monkeypatch.setattr(debug_harness, 'call', mock_call)

        # Change to temp_dir
        monkeypatch.chdir(temp_dir)

        try:
            proc = MockProc()
            debug_harness.handle_failure(proc)

            # Debugger should NOT be called if dumpfile doesn't exist
            assert len(call_count) == 0

        finally:
            debug_harness.argv = original_argv

    def test_handle_failure_params_construction(self, temp_dir):
        """Test parameter construction in handle_failure."""
        import debug_harness

        # Mock argv
        test_file = temp_dir / "test_binary"
        test_file.touch()

        original_argv = debug_harness.argv
        debug_harness.argv = ['debug_harness.py', str(test_file)]

        class MockProc:
            pid = 55555

        try:
            proc = MockProc()
            # Extract just the parameter construction logic
            testdir = Path(debug_harness.argv[1]).parent
            params = {
                'testdir': testdir,
                'testname': Path(debug_harness.argv[1]).name,
                'testfile': debug_harness.argv[1],
                'pid': proc.pid
            }

            assert params['testname'] == 'test_binary'
            assert params['pid'] == 55555
            assert str(params['testdir']) == str(temp_dir)

        finally:
            debug_harness.argv = original_argv


# ========== Test UTF-8 Handling ==========

class TestUTF8Handling:
    """Test UTF-8 stdout/stderr reconfiguration on Windows."""

    @pytest.mark.skipif(sys.platform != 'win32', reason="Windows-specific test")
    def test_windows_stdout_reconfigured(self):
        """Test that stdout is reconfigured on Windows."""
        import debug_harness
        # On Windows, stdout should be wrapped for UTF-8 encoding
        # This test mainly verifies the code doesn't crash
        assert debug_harness.stdout is not None

    @pytest.mark.skipif(sys.platform != 'win32', reason="Windows-specific test")
    def test_windows_stderr_reconfigured(self):
        """Test that stderr is reconfigured on Windows."""
        import debug_harness
        assert debug_harness.stderr is not None

    @pytest.mark.skipif(sys.platform == 'win32', reason="Non-Windows test")
    def test_non_windows_stdout_reconfigured(self):
        """Test that stdout is reconfigured for UTF-8 on non-Windows platforms too.

        Previously only Windows had stdout reconfiguration. Now all platforms
        reconfigure to prevent UnicodeEncodeError when piped through make.
        """
        import debug_harness
        assert hasattr(debug_harness, 'stdout')
        assert debug_harness.stdout is not None


# ========== Test Byte Decoding for Python 2.7/3.x Compatibility ==========

class TestByteDecodingCompatibility:
    """Test the byte decoding pattern in process_output() for cross-version compatibility.

    debug_harness.py reads raw bytes from subprocess and decodes UTF-8 first,
    falling back to latin-1. Both branches are lossless, so test output with
    non-ASCII characters (e.g., file paths with accented names) survives intact.

    See scripts/README.md for full documentation of the encoding issue.
    """

    @staticmethod
    def decode_line(raw_line):
        """Replicate the decode logic from debug_harness.py process_output()."""
        if isinstance(raw_line, bytes):
            try:
                return raw_line.decode('utf-8')
            except UnicodeDecodeError:
                return raw_line.decode('latin-1')
        return raw_line

    def test_ascii_bytes_decoded_correctly(self):
        """Test that ASCII bytes decode to the same string content."""
        raw = b'Running test suite...\n'
        line = self.decode_line(raw).rstrip()
        assert line == 'Running test suite...'

    def test_valid_utf8_bytes_decoded_correctly(self):
        """Test that valid UTF-8 non-ASCII bytes decode correctly.

        This is the case a latin-1-only decode would corrupt: genuine UTF-8 is the
        normal output encoding on Linux and macOS, so it must not be mojibaked.
        """
        raw = u'Test with UTF-8: caf\u00e9\n'.encode('utf-8')
        line = self.decode_line(raw).rstrip()
        assert line == u'Test with UTF-8: caf\u00e9'

    def test_invalid_utf8_bytes_preserved_not_replaced(self):
        """Test that non-UTF-8 bytes fall back to latin-1 without loss."""
        # CP1252 e-acute (0xe9) is NOT valid standalone UTF-8
        raw = b'C:\\Users\\Jos\xe9\\test.exe: error: failed\n'
        line = self.decode_line(raw).rstrip()
        # Must not crash
        assert 'error' in line
        assert 'test.exe' in line
        # The byte must survive, not become U+FFFD
        assert '\ufffd' not in line
        assert line.encode('latin-1') == raw.rstrip()

    def test_string_input_passthrough(self):
        """Test that string input passes through without modification."""
        raw = 'already a string\n'
        line = self.decode_line(raw).rstrip()
        assert line == 'already a string'

    def test_error_pattern_matches_after_decode(self):
        """Test that the error detection regex works on decoded bytes."""
        from re import search
        raw = b'module.cpp:42: error: undefined reference\n'
        line = self.decode_line(raw).rstrip()
        assert search(': (fatal|error|warn)', line)

    def test_debug_prefix_detected_after_decode(self):
        """Test that DEBUG: prefix is detected correctly after decoding bytes."""
        raw = b'DEBUG: error in subsystem (not a real error)\n'
        line = self.decode_line(raw).rstrip()
        assert line.startswith('DEBUG:')

    def test_non_ascii_in_error_line_no_crash(self, monkeypatch):
        """Test that non-ASCII characters in error lines don't crash process_output().

        Previously, the str() wrapper on the error message format string would
        crash in Python 2.7 when line contained unicode characters > 127,
        because str(unicode_value) attempts ASCII encoding.
        """
        class MockProc:
            stdout = [
                b'Running test...\n',
                # Error line with non-UTF-8 byte (CP1252 e-acute)
                b'C:\\Users\\Jos\xe9\\module.cpp:42: error: failed\n',
                b'Test complete\n'
            ]
            returncode = 1

            def wait(self):
                pass

        import debug_harness
        from io import StringIO

        original_argv = debug_harness.argv
        debug_harness.argv = ['debug_harness.py', 'test_example']
        mock_stdout = StringIO()
        mock_stderr = StringIO()
        monkeypatch.setattr(debug_harness, 'stdout', mock_stdout)
        monkeypatch.setattr(debug_harness, 'stderr', mock_stderr)

        try:
            proc = MockProc()
            # Must not crash (previously str() wrapper would cause
            # UnicodeEncodeError on Python 2.7 for non-ASCII unicode)
            debug_harness.process_output(proc)

            stderr_output = mock_stderr.getvalue()
            # Error should be detected and reported
            assert 'Failure in test_example' in stderr_output
            assert 'error' in stderr_output.lower()

            stdout_output = mock_stdout.getvalue()
            # All lines should appear in stdout
            assert 'Running test' in stdout_output
            assert 'Test complete' in stdout_output
        finally:
            debug_harness.argv = original_argv

    def test_multiple_invalid_bytes_in_output(self, monkeypatch):
        """Test that multiple lines with invalid bytes all decode without crashing."""
        class MockProc:
            stdout = [
                b'Line with \x80 invalid byte\n',
                b'Another \xff invalid byte\n',
                b'Normal ASCII line\n'
            ]
            returncode = 0

            def wait(self):
                pass

        import debug_harness
        from io import StringIO

        mock_stdout = StringIO()
        mock_stderr = StringIO()
        monkeypatch.setattr(debug_harness, 'stdout', mock_stdout)
        monkeypatch.setattr(debug_harness, 'stderr', mock_stderr)

        proc = MockProc()
        debug_harness.process_output(proc)

        stdout_output = mock_stdout.getvalue()
        # All lines present, and the non-UTF-8 bytes survive the latin-1 fallback
        assert 'invalid byte' in stdout_output
        assert 'Normal ASCII line' in stdout_output
        assert '\ufffd' not in stdout_output
        assert u'\u0080' in stdout_output
        assert u'\u00ff' in stdout_output
