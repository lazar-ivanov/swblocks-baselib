"""
Shared pytest fixtures for bl_tool.py tests.

Provides reusable test data generation and temporary file/directory fixtures.
Used by both unit and functional tests.
"""

import pytest
import tempfile
import os
import sys
from pathlib import Path


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
