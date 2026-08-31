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
Functional tests for cl.py and clang-cl.py compiler wrappers.

These tests run the scripts end-to-end via subprocess, testing:
- Complete execution flow
- Dependency file generation
- Compiler output forwarding
- Error handling
- Script name detection (cl vs clang-cl)

Test Strategy:
- Uses pytest.mark.parametrize to run same tests for both cl.py and clang-cl.py
- Mocks compiler executable (no real MSVC/clang-cl needed)
- Tests via subprocess.run() for true end-to-end behavior
"""

import pytest
import subprocess
import sys
from pathlib import Path


# Test both scripts with the same test suite
@pytest.mark.parametrize("script_name", ["cl.py", "clang-cl.py"])
class TestCompilerWrapperFunctional:
    """Functional tests for both cl.py and clang-cl.py using shared test infrastructure."""

    def test_successful_compilation_with_dependencies(self, script_name, temp_dir, temp_source_file, mock_compiler_output, monkeypatch):
        """Test successful compilation with dependency file generation."""
        # Determine compiler name from script name
        compiler_name = "cl" if script_name == "cl.py" else "clang-cl"

        # Create mock compiler script with correct name
        if sys.platform == "win32":
            mock_compiler = temp_dir / f"{compiler_name}.bat"
            # Mock compiler that outputs include lines
            mock_compiler.write_text("@echo off\necho test.cpp\necho Note: including file: C:\\header1.h\necho Note: including file: C:\\header2.h\nexit /b 0")
        else:
            mock_compiler = temp_dir / compiler_name
            mock_compiler.write_text("#!/bin/sh\necho 'test.cpp'\necho 'Note: including file: /usr/include/header1.h'\necho 'Note: including file: /usr/include/header2.h'\nexit 0")
            mock_compiler.chmod(0o755)

        # Prepare environment to use mock compiler
        import os
        original_path = os.environ.get("PATH", "")
        monkeypatch.setenv("PATH", f"{temp_dir}{':' if sys.platform != 'win32' else ';'}{original_path}")

        # Run the wrapper script
        script_path = Path(__file__).parent.parent / script_name
        result = subprocess.run(
            [sys.executable, str(script_path), "-M", "-Fomain.obj", str(temp_source_file)],
            capture_output=True,
            text=True,
            cwd=temp_dir
        )

        # Verify success
        assert result.returncode == 0

        # Verify dependency file was created
        depfile = temp_dir / "main.d"
        assert depfile.exists()

        # Verify dependency file content
        content = depfile.read_text()
        assert "main.obj:" in content
        assert "header1.h" in content or "header2.h" in content

    def test_compiler_failure_propagates(self, script_name, temp_dir, temp_source_file, monkeypatch):
        """Test that compiler failures are propagated correctly."""
        # Determine compiler name from script name
        compiler_name = "cl" if script_name == "cl.py" else "clang-cl"

        # Create mock compiler that fails
        if sys.platform == "win32":
            mock_compiler = temp_dir / f"{compiler_name}.bat"
            mock_compiler.write_text("@echo off\necho Compilation failed\nexit /b 1")
        else:
            mock_compiler = temp_dir / compiler_name
            mock_compiler.write_text("#!/bin/sh\necho 'Compilation failed'\nexit 1")
            mock_compiler.chmod(0o755)

        # Prepare environment
        import os
        original_path = os.environ.get("PATH", "")
        monkeypatch.setenv("PATH", f"{temp_dir}{':' if sys.platform != 'win32' else ';'}{original_path}")

        # Run the wrapper script
        script_path = Path(__file__).parent.parent / script_name
        result = subprocess.run(
            [sys.executable, str(script_path), "-M", "-Fomain.obj", str(temp_source_file)],
            capture_output=True,
            text=True,
            cwd=temp_dir
        )

        # Verify failure is propagated
        assert result.returncode != 0

        # Verify no dependency file created on failure
        depfile = temp_dir / "main.d"
        assert not depfile.exists()

    def test_compiler_name_from_script_name(self, script_name, temp_dir, temp_source_file, monkeypatch, capsys):
        """Test that compiler name is derived from script name (cl vs clang-cl)."""
        # Create a mock compiler that echoes its arguments
        if sys.platform == "win32":
            # On Windows, create both cl.bat and clang-cl.bat
            for compiler_name in ["cl", "clang-cl"]:
                mock = temp_dir / f"{compiler_name}.bat"
                mock.write_text(f"@echo off\necho Running {compiler_name}\necho test.cpp\nexit /b 0")
        else:
            # On Unix, create both cl and clang-cl scripts
            for compiler_name in ["cl", "clang-cl"]:
                mock = temp_dir / compiler_name
                mock.write_text(f"#!/bin/sh\necho 'Running {compiler_name}'\necho 'test.cpp'\nexit 0")
                mock.chmod(0o755)

        # Prepare environment
        import os
        original_path = os.environ.get("PATH", "")
        monkeypatch.setenv("PATH", f"{temp_dir}{':' if sys.platform != 'win32' else ';'}{original_path}")

        # Run the wrapper script
        script_path = Path(__file__).parent.parent / script_name
        result = subprocess.run(
            [sys.executable, str(script_path), str(temp_source_file)],
            capture_output=True,
            text=True,
            cwd=temp_dir
        )

        # Verify the correct compiler was invoked based on script name
        expected_compiler = "cl" if script_name == "cl.py" else "clang-cl"
        assert f"Running {expected_compiler}" in result.stdout

    def test_stdout_forwarding(self, script_name, temp_dir, temp_source_file, monkeypatch):
        """Test that compiler stdout is forwarded correctly."""
        # Determine compiler name from script name
        compiler_name = "cl" if script_name == "cl.py" else "clang-cl"

        # Create mock compiler with distinctive output
        if sys.platform == "win32":
            mock_compiler = temp_dir / f"{compiler_name}.bat"
            mock_compiler.write_text("@echo off\necho Compiling test.cpp\necho Optimization enabled\nexit /b 0")
        else:
            mock_compiler = temp_dir / compiler_name
            mock_compiler.write_text("#!/bin/sh\necho 'Compiling test.cpp'\necho 'Optimization enabled'\nexit 0")
            mock_compiler.chmod(0o755)

        # Prepare environment
        import os
        original_path = os.environ.get("PATH", "")
        monkeypatch.setenv("PATH", f"{temp_dir}{':' if sys.platform != 'win32' else ';'}{original_path}")

        # Run the wrapper script
        script_path = Path(__file__).parent.parent / script_name
        result = subprocess.run(
            [sys.executable, str(script_path), str(temp_source_file)],
            capture_output=True,
            text=True,
            cwd=temp_dir
        )

        # Verify output is forwarded (excluding "Note: including file:" lines)
        assert "Compiling test.cpp" in result.stdout
        assert "Optimization enabled" in result.stdout

    def test_no_dependency_file_without_m_flag(self, script_name, temp_dir, temp_source_file, monkeypatch):
        """Test that no dependency file is created without -M flag."""
        # Determine compiler name from script name
        compiler_name = "cl" if script_name == "cl.py" else "clang-cl"

        # Create mock compiler
        if sys.platform == "win32":
            mock_compiler = temp_dir / f"{compiler_name}.bat"
            mock_compiler.write_text("@echo off\necho test.cpp\nexit /b 0")
        else:
            mock_compiler = temp_dir / compiler_name
            mock_compiler.write_text("#!/bin/sh\necho 'test.cpp'\nexit 0")
            mock_compiler.chmod(0o755)

        # Prepare environment
        import os
        original_path = os.environ.get("PATH", "")
        monkeypatch.setenv("PATH", f"{temp_dir}{':' if sys.platform != 'win32' else ';'}{original_path}")

        # Run WITHOUT -M flag
        script_path = Path(__file__).parent.parent / script_name
        result = subprocess.run(
            [sys.executable, str(script_path), str(temp_source_file)],
            capture_output=True,
            text=True,
            cwd=temp_dir
        )

        # Verify success but no .d file
        assert result.returncode == 0

        # Check that no .d file was created
        d_files = list(temp_dir.glob("*.d"))
        assert len(d_files) == 0

    def test_dependency_file_deduplication(self, script_name, temp_dir, temp_source_file, monkeypatch):
        """Test that duplicate includes are deduplicated in .d file."""
        # Determine compiler name from script name
        compiler_name = "cl" if script_name == "cl.py" else "clang-cl"

        # Create mock compiler with duplicate includes
        if sys.platform == "win32":
            mock_compiler = temp_dir / f"{compiler_name}.bat"
            mock_compiler.write_text(
                "@echo off\necho test.cpp\n"
                "echo Note: including file: C:\\header.h\n"
                "echo Note: including file: C:\\other.h\n"
                "echo Note: including file: C:\\header.h\n"  # Duplicate
                "exit /b 0"
            )
        else:
            mock_compiler = temp_dir / compiler_name
            mock_compiler.write_text(
                "#!/bin/sh\necho 'test.cpp'\n"
                "echo 'Note: including file: /usr/include/header.h'\n"
                "echo 'Note: including file: /usr/include/other.h'\n"
                "echo 'Note: including file: /usr/include/header.h'\n"  # Duplicate
                "exit 0"
            )
            mock_compiler.chmod(0o755)

        # Prepare environment
        import os
        original_path = os.environ.get("PATH", "")
        monkeypatch.setenv("PATH", f"{temp_dir}{':' if sys.platform != 'win32' else ';'}{original_path}")

        # Run the wrapper script
        script_path = Path(__file__).parent.parent / script_name
        result = subprocess.run(
            [sys.executable, str(script_path), "-M", "-Fomain.obj", str(temp_source_file)],
            capture_output=True,
            text=True,
            cwd=temp_dir
        )

        # Verify dependency file
        depfile = temp_dir / "main.d"
        assert depfile.exists()

        content = depfile.read_text()
        # Count occurrences of header.h
        header_count = content.count("header.h")

        # Should appear exactly twice: once in main section, once in empty rules
        # Format:
        #   main.obj: \
        #     header.h \      <- first occurrence
        #     other.h \
        #
        #   header.h:        <- second occurrence
        #   other.h:
        assert header_count == 2

    def test_target_inference_from_source(self, script_name, temp_dir, monkeypatch):
        """Test that target is inferred from source file when -Fo not specified."""
        # Determine compiler name from script name
        compiler_name = "cl" if script_name == "cl.py" else "clang-cl"

        # Create source file
        source = temp_dir / "module.cpp"
        source.write_text("int main() { return 0; }")

        # Create mock compiler
        if sys.platform == "win32":
            mock_compiler = temp_dir / f"{compiler_name}.bat"
            mock_compiler.write_text("@echo off\necho module.cpp\nexit /b 0")
        else:
            mock_compiler = temp_dir / compiler_name
            mock_compiler.write_text("#!/bin/sh\necho 'module.cpp'\nexit 0")
            mock_compiler.chmod(0o755)

        # Prepare environment
        import os
        original_path = os.environ.get("PATH", "")
        monkeypatch.setenv("PATH", f"{temp_dir}{':' if sys.platform != 'win32' else ';'}{original_path}")

        # Run WITHOUT -Fo flag (use relative path since cwd=temp_dir)
        script_path = Path(__file__).parent.parent / script_name
        result = subprocess.run(
            [sys.executable, str(script_path), "-M", "module.cpp"],
            capture_output=True,
            text=True,
            cwd=temp_dir
        )

        # Verify dependency file created with inferred name
        depfile = temp_dir / "module.d"
        assert depfile.exists()

        content = depfile.read_text()
        assert "module.obj:" in content


# Non-parametrized tests for encoding-specific behavior
class TestCompilerWrapperEncoding:
    """Functional tests for non-ASCII encoding handling in cl.py/clang-cl.py.

    These tests verify that the byte decoding fix (Option C) works correctly
    when the compiler outputs non-ASCII characters. The scripts read raw bytes and
    decode them as latin-1 instead of using universal_newlines=True, so the original
    bytes survive into the generated dependency file.

    See scripts/README.md for full documentation of the encoding issue.
    """

    @pytest.mark.parametrize("script_name", ["cl.py", "clang-cl.py"])
    @pytest.mark.skipif(sys.platform == "win32", reason="Uses Unix shell for byte-level output control")
    def test_non_ascii_compiler_output_no_crash(self, script_name, temp_dir, temp_source_file, monkeypatch):
        """Test that non-ASCII bytes in compiler output don't crash the script.

        Simulates a compiler that outputs include paths with non-UTF-8 bytes
        (e.g., CP1252 accented characters in Windows usernames). Previously this
        would cause UnicodeDecodeError with universal_newlines=True.
        """
        compiler_name = "cl" if script_name == "cl.py" else "clang-cl"

        # Create mock compiler that outputs non-UTF-8 bytes via printf
        # Octal 351 = 0xe9 = CP1252 e-acute, which is NOT valid standalone UTF-8
        mock_compiler = temp_dir / compiler_name
        mock_compiler.write_text(
            "#!/bin/sh\n"
            "echo 'test.cpp'\n"
            "/usr/bin/printf 'Note: including file: /usr/include/Jos\\351/header.h\\n'\n"
            "echo 'Note: including file: /usr/include/ascii_header.h'\n"
            "exit 0\n"
        )
        mock_compiler.chmod(0o755)

        import os
        original_path = os.environ.get("PATH", "")
        monkeypatch.setenv("PATH", f"{temp_dir}:{original_path}")

        script_path = Path(__file__).parent.parent / script_name
        result = subprocess.run(
            [sys.executable, str(script_path), "-M", "-Fomain.obj", str(temp_source_file)],
            capture_output=True,
            text=True,
            cwd=temp_dir
        )

        # Must not crash (previously would raise UnicodeDecodeError)
        assert result.returncode == 0

        # Dependency file should be created
        depfile = temp_dir / "main.d"
        assert depfile.exists()

        # The dependency file can now hold non-UTF-8 bytes verbatim, so read it as bytes
        content = depfile.read_bytes()
        assert b"main.obj:" in content
        assert b"ascii_header.h" in content

    @pytest.mark.parametrize("script_name", ["cl.py", "clang-cl.py"])
    @pytest.mark.skipif(sys.platform == "win32", reason="Uses Unix shell for byte-level output control")
    def test_valid_utf8_compiler_output(self, script_name, temp_dir, temp_source_file, monkeypatch):
        """Test that valid UTF-8 non-ASCII bytes in compiler output are handled correctly."""
        compiler_name = "cl" if script_name == "cl.py" else "clang-cl"

        # Create mock compiler that outputs valid UTF-8 e-acute (octal 303 251 = 0xc3 0xa9)
        mock_compiler = temp_dir / compiler_name
        mock_compiler.write_text(
            "#!/bin/sh\n"
            "echo 'test.cpp'\n"
            "/usr/bin/printf 'Note: including file: /usr/include/Jos\\303\\251/header.h\\n'\n"
            "exit 0\n"
        )
        mock_compiler.chmod(0o755)

        import os
        original_path = os.environ.get("PATH", "")
        monkeypatch.setenv("PATH", f"{temp_dir}:{original_path}")

        script_path = Path(__file__).parent.parent / script_name
        result = subprocess.run(
            [sys.executable, str(script_path), "-M", "-Fomain.obj", str(temp_source_file)],
            capture_output=True,
            text=True,
            cwd=temp_dir
        )

        # Must not crash
        assert result.returncode == 0

        # Dependency file should be created with the UTF-8 path preserved
        depfile = temp_dir / "main.d"
        assert depfile.exists()
        content = depfile.read_text()
        assert "main.obj:" in content
        # Valid UTF-8 should decode correctly, preserving the accented character
        assert "header.h" in content

    @pytest.mark.parametrize("script_name", ["cl.py", "clang-cl.py"])
    def test_ascii_only_output_unchanged(self, script_name, temp_dir, temp_source_file, monkeypatch):
        """Test that ASCII-only output works identically to before the encoding fix."""
        compiler_name = "cl" if script_name == "cl.py" else "clang-cl"

        if sys.platform == "win32":
            mock_compiler = temp_dir / f"{compiler_name}.bat"
            mock_compiler.write_text(
                "@echo off\necho test.cpp\n"
                "echo Note: including file: C:\\include\\header1.h\n"
                "echo Note: including file: C:\\include\\header2.h\n"
                "exit /b 0"
            )
        else:
            mock_compiler = temp_dir / compiler_name
            mock_compiler.write_text(
                "#!/bin/sh\n"
                "echo 'test.cpp'\n"
                "echo 'Note: including file: /usr/include/header1.h'\n"
                "echo 'Note: including file: /usr/include/header2.h'\n"
                "exit 0\n"
            )
            mock_compiler.chmod(0o755)

        import os
        original_path = os.environ.get("PATH", "")
        monkeypatch.setenv("PATH", f"{temp_dir}{':' if sys.platform != 'win32' else ';'}{original_path}")

        script_path = Path(__file__).parent.parent / script_name
        result = subprocess.run(
            [sys.executable, str(script_path), "-M", "-Fomain.obj", str(temp_source_file)],
            capture_output=True,
            text=True,
            cwd=temp_dir
        )

        assert result.returncode == 0

        depfile = temp_dir / "main.d"
        assert depfile.exists()

        content = depfile.read_text()
        assert "main.obj:" in content
        assert "header1.h" in content
        assert "header2.h" in content

    @pytest.mark.parametrize("script_name", ["cl.py", "clang-cl.py"])
    @pytest.mark.skipif(sys.platform == "win32", reason="Uses Unix shell for byte-level output control")
    def test_non_utf8_dependency_path_round_trips(self, script_name, temp_dir, temp_source_file, monkeypatch):
        """Test that non-UTF-8 dependency paths are written to the .d file byte-exactly.

        latin-1 maps bytes 0-255 bijectively, so a path the compiler emitted in a legacy
        code page must come back out of the dependency file as the identical byte sequence.
        Under the previous errors='replace' decoding these bytes became U+FFFD and make
        could no longer resolve the header.
        """
        compiler_name = "cl" if script_name == "cl.py" else "clang-cl"

        # Octal 351 = 0xe9 = CP1252 e-acute, which is NOT valid standalone UTF-8
        mock_compiler = temp_dir / compiler_name
        mock_compiler.write_text(
            "#!/bin/sh\n"
            "echo 'test.cpp'\n"
            "/usr/bin/printf 'Note: including file: /usr/include/Jos\\351/header.h\\n'\n"
            "exit 0\n"
        )
        mock_compiler.chmod(0o755)

        import os
        original_path = os.environ.get("PATH", "")
        monkeypatch.setenv("PATH", f"{temp_dir}:{original_path}")

        script_path = Path(__file__).parent.parent / script_name
        result = subprocess.run(
            [sys.executable, str(script_path), "-M", "-Fomain.obj", str(temp_source_file)],
            capture_output=True,
            cwd=temp_dir
        )

        assert result.returncode == 0

        depfile = temp_dir / "main.d"
        assert depfile.exists()

        # The original bytes must be preserved verbatim, with no U+FFFD replacement
        content = depfile.read_bytes()
        assert b"/usr/include/Jos\xe9/header.h" in content
        assert "�".encode("utf-8") not in content
