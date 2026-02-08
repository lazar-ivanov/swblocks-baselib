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
