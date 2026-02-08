#!/usr/bin/env python3
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
Shared pytest fixtures for bl_tool.py and s3_manage.py tests.

Provides reusable test data generation and temporary file/directory fixtures.
Used by both unit and functional tests.
"""

import pytest
import tempfile
import os
import sys
from pathlib import Path
from datetime import datetime, timezone


# ========== Temporary Directory Fixtures ==========

@pytest.fixture
def temp_dir():
    """Create temporary directory, auto-cleanup."""
    with tempfile.TemporaryDirectory() as tmpdir:
        yield Path(tmpdir)


@pytest.fixture
def empty_dir(temp_dir):
    """Empty directory for testing."""
    return temp_dir


@pytest.fixture
def download_dir(temp_dir):
    """Separate directory for download testing (isolated from source files)."""
    dl_dir = temp_dir / "downloads"
    dl_dir.mkdir()
    return dl_dir


@pytest.fixture
def dir_with_files(temp_dir):
    """Directory with multiple files and subdirectories."""
    # Create files
    (temp_dir / "file1.txt").write_text("content1")
    (temp_dir / "file2.txt").write_text("content2")

    # Create subdirectory with file
    subdir = temp_dir / "subdir"
    subdir.mkdir()
    (subdir / "file3.txt").write_text("content3")

    return temp_dir


@pytest.fixture
def dir_with_hidden_files(temp_dir):
    """Directory with hidden files and directories."""
    # Regular files
    (temp_dir / "visible.txt").write_text("visible")

    # Hidden file
    (temp_dir / ".hidden.txt").write_text("hidden")

    # Hidden directory with file
    hidden_dir = temp_dir / ".hidden_dir"
    hidden_dir.mkdir()
    (hidden_dir / "file.txt").write_text("in hidden dir")

    return temp_dir


@pytest.fixture
def dir_with_symlink(temp_dir):
    """Directory with symlink (Unix only)."""
    if sys.platform == "win32":
        pytest.skip("Symlinks not reliably supported on Windows")

    # Create target file
    target = temp_dir / "target.txt"
    target.write_text("target content")

    # Create symlink
    link = temp_dir / "link.txt"
    link.symlink_to(target)

    return temp_dir


# ========== Temporary File Fixtures ==========

@pytest.fixture
def temp_file(temp_dir):
    """Create temporary file with content."""
    file_path = temp_dir / "test.txt"
    file_path.write_text("test content")
    return file_path


@pytest.fixture
def temp_file_small(temp_dir):
    """Create small temporary file (< 1 MB)."""
    file_path = temp_dir / "small.bin"

    # Write 100 KB of data
    file_path.write_bytes(b"x" * (100 * 1024))

    return file_path


@pytest.fixture
def temp_file_medium(temp_dir):
    """Create medium temporary file (~10 MB, tests chunked reading)."""
    file_path = temp_dir / "medium.bin"

    # Write 10 MB of data
    file_path.write_bytes(b"y" * (10 * 1024 * 1024))

    return file_path


@pytest.fixture
def temp_file_large(temp_dir):
    """Create large temporary file (~100 MB, tests memory efficiency)."""
    file_path = temp_dir / "large.bin"

    # Write 100 MB of data in chunks to avoid memory issues
    with open(file_path, "wb") as f:
        chunk = b"z" * (1 * 1024 * 1024)  # 1 MB chunk
        for _ in range(100):  # Write 100 chunks = 100 MB
            f.write(chunk)

    return file_path


@pytest.fixture
def temp_symlink(temp_dir):
    """Create symlink to temporary file (Unix only)."""
    if sys.platform == "win32":
        pytest.skip("Symlinks not reliably supported on Windows")

    # Create target
    target = temp_dir / "target.txt"
    target.write_text("target")

    # Create symlink
    link = temp_dir / "link.txt"
    link.symlink_to(target)

    return link


# ========== Utility Functions ==========

def create_test_directory_tree(base_path, num_files=10, num_dirs=3):
    """
    Create test directory tree with specified number of files and subdirectories.

    Useful for performance testing and stress testing.

    Args:
        base_path (Path): Base directory path
        num_files (int): Number of files per directory
        num_dirs (int): Number of subdirectories

    Returns:
        Path: Base directory path
    """
    base_path = Path(base_path)
    base_path.mkdir(parents=True, exist_ok=True)

    # Create files in base directory
    for i in range(num_files):
        (base_path / f"file_{i:04d}.txt").write_text(f"content {i}")

    # Create subdirectories with files
    for d in range(num_dirs):
        subdir = base_path / f"subdir_{d:02d}"
        subdir.mkdir()

        for i in range(num_files):
            (subdir / f"file_{i:04d}.txt").write_text(f"subdir {d} content {i}")

    return base_path


@pytest.fixture
def large_directory_tree(temp_dir):
    """Create large directory tree (100 files, 10 subdirs) for performance testing."""
    return create_test_directory_tree(temp_dir, num_files=10, num_dirs=10)


# ========== S3 Mock Fixtures (for s3_manage.py tests) ==========

@pytest.fixture
def sample_s3_objects():
    """Sample S3 object list for index generation testing."""
    return [
        {
            'key': 'file1.txt',
            'size': 1024,
            'last_modified': datetime(2024, 1, 1, 12, 0, 0, tzinfo=timezone.utc)
        },
        {
            'key': 'folder/file2.bin',
            'size': 1024 * 1024,
            'last_modified': datetime(2024, 1, 2, 12, 0, 0, tzinfo=timezone.utc)
        },
        {
            'key': 'large.dat',
            'size': 1024 * 1024 * 1024,  # 1 GB
            'last_modified': datetime(2024, 1, 3, 12, 0, 0, tzinfo=timezone.utc)
        }
    ]


@pytest.fixture
def known_etag_files(temp_dir):
    """Create files with known S3 ETags for validation."""
    files = {}

    # Empty file (known MD5)
    empty = temp_dir / "empty.txt"
    empty.touch()
    files['empty'] = (empty, 'd41d8cd98f00b204e9800998ecf8427e')

    # Known content (pre-calculated MD5)
    known = temp_dir / "known.txt"
    known.write_text("test content\n")
    files['known'] = (known, '9a0364b9e99bb480dd25e1f0284c8555')

    return files


@pytest.fixture
def mock_s3_bucket():
    """Create a mock S3 bucket for testing with moto."""
    from moto import mock_s3
    import boto3

    with mock_s3():
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        yield s3_client, 'test-bucket'


# ========== Compiler Wrapper Mock Fixtures (for cl.py/clang-cl.py tests) ==========

@pytest.fixture
def temp_source_file(temp_dir):
    """
    Create a temporary C++ source file for compiler wrapper testing.

    Used by cl.py and clang-cl.py tests to simulate compilation scenarios.

    Args:
        temp_dir: Temporary directory fixture

    Returns:
        Path: Path to test.cpp file
    """
    source = temp_dir / "test.cpp"
    source.write_text("""#include <iostream>
#include "header1.h"
#include "header2.h"

int main() {
    std::cout << "Hello, World!" << std::endl;
    return 0;
}
""")
    return source


@pytest.fixture
def mock_compiler_output():
    """
    Generate realistic MSVC compiler output with include directives.

    Simulates output from cl.exe or clang-cl.exe when run with -showIncludes flag.
    Includes:
    - Copyright banner
    - Source file name
    - "Note: including file:" lines (with varying indentation levels)
    - Duplicate includes (to test deduplication)
    - Compiler messages

    Returns:
        list: List of output lines as they would appear from compiler stdout
    """
    return [
        "Microsoft (R) C/C++ Optimizing Compiler Version 19.00.00000",
        "Copyright (C) Microsoft Corporation.  All rights reserved.",
        "",
        "test.cpp",
        "Note: including file: C:\\Program Files\\Microsoft Visual Studio\\include\\iostream",
        "Note: including file:  C:\\Users\\test\\project\\header1.h",
        "Note: including file:  C:\\Users\\test\\project\\header2.h",
        "Note: including file:   C:\\Program Files\\Microsoft Visual Studio\\include\\string",
        "Note: including file: C:\\Users\\test\\project\\header1.h",  # Duplicate (should be filtered)
        "Generating Code...",
        "Finished generating code.",
    ]


@pytest.fixture
def dependency_file_parser():
    """
    Helper function to parse and validate .d (dependency) file format.

    Dependency files (.d) are generated by cl.py/clang-cl.py and used by make
    to track header dependencies. Format:

        target.obj: \\
          header1.h \\
          header2.h \\

        header1.h:
        header2.h:

    Returns:
        function: Parser function that takes a file path and returns (target, dependencies)
    """
    def parse(path):
        """
        Parse dependency file and extract target and dependencies.

        Args:
            path (Path or str): Path to .d file

        Returns:
            tuple: (target, dependencies) where:
                - target (str): The build target (e.g., "main.obj")
                - dependencies (list): List of dependency file paths

        Raises:
            ValueError: If file format is invalid
        """
        content = Path(path).read_text()

        # Split into sections (before and after empty line)
        sections = content.split('\n\n')
        if not sections:
            raise ValueError("Empty dependency file")

        # Parse first section: target and dependencies
        first_section = sections[0].strip()
        if not first_section:
            raise ValueError("No target/dependencies section found")

        lines = [line.strip().rstrip('\\').strip() for line in first_section.split('\n')]

        # First line should be "target: \"
        if ':' not in lines[0]:
            raise ValueError(f"Invalid target line: {lines[0]}")

        target = lines[0].split(':')[0].strip()
        dependencies = [dep for dep in lines[1:] if dep]  # Skip empty lines

        return target, dependencies

    return parse
