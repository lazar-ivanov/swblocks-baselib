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
Unit tests for bl_tool.py functions.

Tests individual functions with realistic calling patterns (integration-style).
Uses shared fixtures from conftest.py for test data generation.
"""

import pytest
import sys
import os
from pathlib import Path

# Import bl_tool module
sys.path.insert(0, str(Path(__file__).parent.parent))
import bl_tool


class TestFormatting:
    """Test format_size() and format_speed() functions."""

    def test_format_size_bytes(self):
        """Test byte formatting."""
        assert bl_tool.format_size(0) == "0.00 B"
        assert bl_tool.format_size(512) == "512.00 B"
        assert bl_tool.format_size(1023) == "1023.00 B"

    def test_format_size_kilobytes(self):
        """Test KB formatting."""
        assert bl_tool.format_size(1024) == "1.00 KB"
        assert bl_tool.format_size(1536) == "1.50 KB"

    def test_format_size_megabytes(self):
        """Test MB formatting."""
        assert bl_tool.format_size(1024 * 1024) == "1.00 MB"
        assert bl_tool.format_size(5 * 1024 * 1024) == "5.00 MB"

    def test_format_size_gigabytes(self):
        """Test GB formatting."""
        assert bl_tool.format_size(1024 * 1024 * 1024) == "1.00 GB"

    def test_format_size_terabytes(self):
        """Test TB formatting."""
        assert bl_tool.format_size(1024 * 1024 * 1024 * 1024) == "1.00 TB"

    def test_format_speed_zero_time(self):
        """Test speed with zero elapsed time."""
        assert bl_tool.format_speed(1000, 0) == "0.00 B/s"

    def test_format_speed_bytes_per_second(self):
        """Test B/s formatting."""
        assert bl_tool.format_speed(500, 1.0) == "500.00 B/s"

    def test_format_speed_megabytes_per_second(self):
        """Test MB/s formatting."""
        assert bl_tool.format_speed(10 * 1024 * 1024, 1.0) == "10.00 MB/s"

    def test_format_speed_gigabytes_per_second(self):
        """Test GB/s formatting."""
        assert bl_tool.format_speed(2 * 1024 * 1024 * 1024, 1.0) == "2.00 GB/s"

    def test_format_size_petabytes(self):
        """Test PB formatting (edge case for very large files)."""
        # 1 PB = 1024^5 bytes
        assert bl_tool.format_size(1024 * 1024 * 1024 * 1024 * 1024) == "1.00 PB"
        assert bl_tool.format_size(5 * 1024 * 1024 * 1024 * 1024 * 1024) == "5.00 PB"

    def test_format_speed_petabytes_per_second(self):
        """Test PB/s formatting (edge case for very high speeds)."""
        # 1 PB/s = 1024^5 bytes/second
        assert bl_tool.format_speed(1024 * 1024 * 1024 * 1024 * 1024, 1.0) == "1.00 PB/s"
        assert bl_tool.format_speed(3 * 1024 * 1024 * 1024 * 1024 * 1024, 1.0) == "3.00 PB/s"


class TestFormatDuration:
    """Test format_duration() function."""

    def test_format_duration_seconds(self):
        """Test duration under 60 seconds stays in seconds."""
        assert bl_tool.format_duration(0) == "0.00 seconds"
        assert bl_tool.format_duration(30.5) == "30.50 seconds"
        assert bl_tool.format_duration(59.99) == "59.99 seconds"

    def test_format_duration_minutes(self):
        """Test duration between 60 seconds and 1 hour converts to minutes."""
        assert bl_tool.format_duration(60) == "1.00 minutes"
        assert bl_tool.format_duration(90) == "1.50 minutes"
        assert bl_tool.format_duration(3599) == "59.98 minutes"

    def test_format_duration_hours(self):
        """Test duration of 1 hour or more converts to hours."""
        assert bl_tool.format_duration(3600) == "1.00 hours"
        assert bl_tool.format_duration(10207.90) == "2.84 hours"
        assert bl_tool.format_duration(7200) == "2.00 hours"


class TestFileCollection:
    """Test collect_files() function."""

    def test_collect_files_empty_directory(self, empty_dir):
        """Test collecting files from empty directory."""
        files = bl_tool.collect_files(str(empty_dir), allow_hidden_files=False)
        assert files == []

    def test_collect_files_single_file(self, dir_with_files):
        """Test collecting files."""
        files = bl_tool.collect_files(str(dir_with_files), allow_hidden_files=False)
        assert len(files) == 3  # file1.txt, file2.txt, subdir/file3.txt

        # Verify format: (absolute_path, relative_path)
        for abs_path, rel_path in files:
            assert os.path.isabs(abs_path)
            assert not os.path.isabs(rel_path)

    def test_collect_files_sorted_order(self, dir_with_files):
        """Test files are sorted by relative path."""
        files = bl_tool.collect_files(str(dir_with_files), allow_hidden_files=False)
        rel_paths = [rel_path for _, rel_path in files]

        # Should be alphabetically sorted
        assert rel_paths == sorted(rel_paths)

    def test_collect_files_hidden_files_excluded(self, dir_with_hidden_files):
        """Test hidden files are excluded by default."""
        files = bl_tool.collect_files(str(dir_with_hidden_files), allow_hidden_files=False)
        rel_paths = [rel_path for _, rel_path in files]

        # Should not include .hidden.txt or files in .hidden_dir
        assert not any(p.startswith('.') for p in rel_paths)

    def test_collect_files_hidden_files_included(self, dir_with_hidden_files):
        """Test hidden files are included when flag is set."""
        files = bl_tool.collect_files(str(dir_with_hidden_files), allow_hidden_files=True)
        rel_paths = [rel_path for _, rel_path in files]

        # Should include .hidden.txt
        assert any('.hidden.txt' in p for p in rel_paths)

    @pytest.mark.skipif(sys.platform == "win32", reason="Symlinks may not be supported on Windows")
    def test_collect_files_symlink_fails(self, dir_with_symlink):
        """Test that symlinks cause failure."""
        with pytest.raises(SystemExit) as exc_info:
            bl_tool.collect_files(str(dir_with_symlink), allow_hidden_files=False)

        assert exc_info.value.code == 1


class TestSingleFileHandling:
    """Test handle_single_file() function."""

    def test_handle_single_file_regular(self, temp_file):
        """Test handling regular file."""
        file_path_obj = Path(temp_file)
        result = bl_tool.handle_single_file(str(temp_file), file_path_obj)

        # Should return single-element list
        assert len(result) == 1
        assert result[0][0] == str(temp_file)  # Absolute path
        assert result[0][1] == file_path_obj.name  # Filename only

    @pytest.mark.skipif(sys.platform == "win32", reason="Symlinks may not be supported on Windows")
    def test_handle_single_file_symlink_fails(self, temp_symlink):
        """Test that symlink file causes failure."""
        file_path_obj = Path(temp_symlink)

        with pytest.raises(SystemExit) as exc_info:
            bl_tool.handle_single_file(str(temp_symlink), file_path_obj)

        assert exc_info.value.code == 1


class TestDirectoryHandling:
    """Test handle_directory() function."""

    def test_handle_directory_returns_file_list(self, dir_with_files):
        """Test directory handling returns file list."""
        files = bl_tool.handle_directory(str(dir_with_files), allow_hidden_files=False)

        assert isinstance(files, list)
        assert len(files) > 0

        # Verify each item is (absolute_path, relative_path) tuple
        for item in files:
            assert isinstance(item, tuple)
            assert len(item) == 2


class TestHashWorker:
    """Test hash_worker() function."""

    def test_hash_worker_small_file(self, temp_file_small):
        """Test hashing small file (< 1 MB)."""
        file_path = str(temp_file_small)
        relative_path = Path(file_path).name

        result = bl_tool.hash_worker(file_path, relative_path, verbose=False)

        # Verify result format: (relative_path, hash_bytes, file_size)
        assert result[0] == relative_path
        assert isinstance(result[1], bytes)
        assert len(result[1]) == 32  # SHA256 = 32 bytes
        assert result[2] > 0  # File size

    def test_hash_worker_medium_file(self, temp_file_medium):
        """Test hashing medium file (~10 MB, tests chunked reading)."""
        file_path = str(temp_file_medium)
        relative_path = Path(file_path).name

        result = bl_tool.hash_worker(file_path, relative_path, verbose=False)

        assert result[0] == relative_path
        assert isinstance(result[1], bytes)
        assert len(result[1]) == 32
        assert result[2] >= 10 * 1024 * 1024  # ~10 MB

    def test_hash_worker_deterministic(self, temp_file_small):
        """Test hashing is deterministic (same file -> same hash)."""
        file_path = str(temp_file_small)
        relative_path = Path(file_path).name

        result1 = bl_tool.hash_worker(file_path, relative_path, verbose=False)
        result2 = bl_tool.hash_worker(file_path, relative_path, verbose=False)

        assert result1[1] == result2[1]  # Same hash bytes

    def test_hash_worker_different_relative_paths(self, temp_file_small):
        """Test same file with different relative paths produces different hashes."""
        file_path = str(temp_file_small)

        result1 = bl_tool.hash_worker(file_path, "path1.txt", verbose=False)
        result2 = bl_tool.hash_worker(file_path, "path2.txt", verbose=False)

        assert result1[1] != result2[1]  # Different hashes

    def test_hash_worker_nonexistent_file(self):
        """Test hashing nonexistent file raises exception."""
        with pytest.raises(Exception):
            bl_tool.hash_worker("/nonexistent/file.txt", "file.txt", verbose=False)

    def test_hash_worker_verbose_mode(self, temp_file_small, capsys):
        """Test hash_worker with verbose=True prints file hash."""
        file_path = str(temp_file_small)
        relative_path = Path(file_path).name

        result = bl_tool.hash_worker(file_path, relative_path, verbose=True)

        # Verify verbose output
        captured = capsys.readouterr()
        assert "[HASHED]" in captured.out
        assert relative_path in captured.out
        # Verify hash is in output (64 hex chars)
        assert len([c for c in captured.out if c in "0123456789abcdef"]) >= 64


class TestCombineHashes:
    """Test combine_hashes() function."""

    def test_combine_hashes_empty_list(self):
        """Test combining empty hash list."""
        # Empty list should be handled by command_hash(), but test robustness
        result = bl_tool.combine_hashes([])

        # Should return hash of empty bytes
        import hashlib
        expected = hashlib.sha256(b'').hexdigest()
        assert result == expected

    def test_combine_hashes_single_hash(self):
        """Test combining single hash returns it directly (no re-hashing)."""
        import hashlib

        hash1 = hashlib.sha256(b"test").digest()
        result = bl_tool.combine_hashes([hash1])

        # Result should be hash1 directly (not SHA256(hash1))
        expected = hash1.hex()
        assert result == expected

    def test_combine_hashes_multiple_hashes(self):
        """Test combining multiple hashes."""
        import hashlib

        hash1 = hashlib.sha256(b"file1").digest()
        hash2 = hashlib.sha256(b"file2").digest()
        hash3 = hashlib.sha256(b"file3").digest()

        result = bl_tool.combine_hashes([hash1, hash2, hash3])

        # Result should be SHA256(hash1 + hash2 + hash3)
        expected = hashlib.sha256(hash1 + hash2 + hash3).hexdigest()
        assert result == expected

    def test_combine_hashes_order_matters(self):
        """Test that hash order affects result."""
        import hashlib

        hash1 = hashlib.sha256(b"file1").digest()
        hash2 = hashlib.sha256(b"file2").digest()

        result1 = bl_tool.combine_hashes([hash1, hash2])
        result2 = bl_tool.combine_hashes([hash2, hash1])

        assert result1 != result2  # Order matters

    def test_combine_hashes_large_list(self):
        """Test combining many hashes (tests block-based combining)."""
        import hashlib

        # Create 100,000 hashes to test block processing
        hash_list = [hashlib.sha256(f"file{i}".encode()).digest() for i in range(100000)]

        result = bl_tool.combine_hashes(hash_list)

        # Should complete without memory error and return valid hash
        assert isinstance(result, str)
        assert len(result) == 64  # SHA256 hex = 64 chars


class TestCommandHash:
    """Test command_hash() function with mock arguments."""

    def test_command_hash_single_file(self, temp_file_small, capsys):
        """Test command_hash with single file."""
        import argparse

        args = argparse.Namespace(
            path=str(temp_file_small),
            expected_hash=None,
            exclude_paths=False,
            use_sha1=False,
            allow_hidden_files=False,
            max_threads=4,
            dry_run=False,
            verbose=False
        )

        with pytest.raises(SystemExit) as exc_info:
            bl_tool.command_hash(args)

        # Should exit with code 0 (success)
        assert exc_info.value.code == 0

        # Verify output
        captured = capsys.readouterr()
        assert "Hashing single file..." in captured.out
        assert "All operations complete!" in captured.out
        assert "Combined SHA256:" in captured.out

    def test_command_hash_directory(self, dir_with_files, capsys):
        """Test command_hash with directory."""
        import argparse

        args = argparse.Namespace(
            path=str(dir_with_files),
            expected_hash=None,
            exclude_paths=False,
            use_sha1=False,
            allow_hidden_files=False,
            max_threads=4,
            dry_run=False,
            verbose=False
        )

        with pytest.raises(SystemExit) as exc_info:
            bl_tool.command_hash(args)

        assert exc_info.value.code == 0

        captured = capsys.readouterr()
        assert "Scanning directory:" in captured.out
        assert "Hashing files with" in captured.out
        assert "Combined SHA256:" in captured.out

    def test_command_hash_dry_run(self, dir_with_files, capsys):
        """Test command_hash with dry-run mode (non-verbose)."""
        import argparse

        args = argparse.Namespace(
            path=str(dir_with_files),
            expected_hash=None,
            exclude_paths=False,
            use_sha1=False,
            allow_hidden_files=False,
            max_threads=4,
            dry_run=True,
            verbose=False
        )

        with pytest.raises(SystemExit) as exc_info:
            bl_tool.command_hash(args)

        assert exc_info.value.code == 0

        captured = capsys.readouterr()
        assert "No files were actually hashed" in captured.out
        assert "Combined SHA256:" not in captured.out
        # Should NOT show individual files in non-verbose mode
        assert "[DRY-RUN]" not in captured.out

    def test_command_hash_dry_run_verbose(self, dir_with_files, capsys):
        """Test command_hash with dry-run mode and verbose."""
        import argparse

        args = argparse.Namespace(
            path=str(dir_with_files),
            expected_hash=None,
            exclude_paths=False,
            use_sha1=False,
            allow_hidden_files=False,
            max_threads=4,
            dry_run=True,
            verbose=True
        )

        with pytest.raises(SystemExit) as exc_info:
            bl_tool.command_hash(args)

        assert exc_info.value.code == 0

        captured = capsys.readouterr()
        assert "No files were actually hashed" in captured.out
        assert "Combined SHA256:" not in captured.out
        # Should show individual files in verbose mode
        assert "[DRY-RUN]" in captured.out

    def test_command_hash_verification_match(self, temp_file_small, capsys):
        """Test command_hash with matching verification hash."""
        import argparse
        import hashlib

        # Calculate the expected hash (single file returns hash directly, no re-hashing)
        file_content = temp_file_small.read_bytes()
        relative_path = temp_file_small.name
        hasher = hashlib.sha256()
        hasher.update(file_content)
        hasher.update(relative_path.encode('utf-8'))
        expected_hash = hasher.hexdigest()

        args = argparse.Namespace(
            path=str(temp_file_small),
            expected_hash=expected_hash,
            exclude_paths=False,
            use_sha1=False,
            allow_hidden_files=False,
            max_threads=4,
            dry_run=False,
            verbose=False
        )

        with pytest.raises(SystemExit) as exc_info:
            bl_tool.command_hash(args)

        assert exc_info.value.code == 0

        captured = capsys.readouterr()
        assert "Verification: MATCH" in captured.out

    def test_command_hash_verification_mismatch(self, temp_file_small, capsys):
        """Test command_hash with mismatched verification hash."""
        import argparse

        wrong_hash = "0" * 64

        args = argparse.Namespace(
            path=str(temp_file_small),
            expected_hash=wrong_hash,
            exclude_paths=False,
            use_sha1=False,
            allow_hidden_files=False,
            max_threads=4,
            dry_run=False,
            verbose=False
        )

        with pytest.raises(SystemExit) as exc_info:
            bl_tool.command_hash(args)

        assert exc_info.value.code == 1

        captured = capsys.readouterr()
        assert "Verification: MISMATCH" in captured.out

    def test_command_hash_nonexistent_path(self, capsys):
        """Test command_hash with nonexistent path."""
        import argparse

        args = argparse.Namespace(
            path="/nonexistent/path",
            expected_hash=None,
            exclude_paths=False,
            use_sha1=False,
            allow_hidden_files=False,
            max_threads=4,
            dry_run=False,
            verbose=False
        )

        with pytest.raises(SystemExit) as exc_info:
            bl_tool.command_hash(args)

        assert exc_info.value.code == 1

        captured = capsys.readouterr()
        assert "[ERROR]" in captured.out
        assert "Path not found" in captured.out

    def test_command_hash_empty_directory(self, empty_dir, capsys):
        """Test command_hash with empty directory."""
        import argparse

        args = argparse.Namespace(
            path=str(empty_dir),
            expected_hash=None,
            exclude_paths=False,
            use_sha1=False,
            allow_hidden_files=False,
            max_threads=4,
            dry_run=False,
            verbose=False
        )

        with pytest.raises(SystemExit) as exc_info:
            bl_tool.command_hash(args)

        assert exc_info.value.code == 0

        captured = capsys.readouterr()
        assert "Total files processed: 0" in captured.out or "Found 0 files" in captured.out

    @pytest.mark.skipif(sys.platform == "win32", reason="FIFO creation not supported on Windows")
    def test_command_hash_special_file(self, temp_dir, capsys):
        """Test command_hash with special file (FIFO/named pipe)."""
        import argparse
        import os

        # Create a FIFO (named pipe) - neither a regular file nor a directory
        fifo_path = temp_dir / "test_fifo"
        os.mkfifo(str(fifo_path))

        args = argparse.Namespace(
            path=str(fifo_path),
            expected_hash=None,
            exclude_paths=False,
            use_sha1=False,
            allow_hidden_files=False,
            max_threads=4,
            dry_run=False,
            verbose=False
        )

        with pytest.raises(SystemExit) as exc_info:
            bl_tool.command_hash(args)

        # Should exit with error
        assert exc_info.value.code == 1

        captured = capsys.readouterr()
        assert "[ERROR]" in captured.out
        assert "neither a file nor a directory" in captured.out

    def test_command_hash_dry_run_stat_failure(self, temp_dir, capsys, monkeypatch):
        """Test command_hash dry-run with file stat failure."""
        import argparse
        import os

        # Create a file
        test_file = temp_dir / "test.txt"
        test_file.write_text("content")

        # Mock os.path.getsize to raise an exception
        original_getsize = os.path.getsize
        call_count = [0]

        def mock_getsize(path):
            call_count[0] += 1
            # Fail on the first file in dry-run
            if call_count[0] == 1:
                raise OSError("Permission denied")
            return original_getsize(path)

        monkeypatch.setattr(os.path, "getsize", mock_getsize)

        args = argparse.Namespace(
            path=str(temp_dir),
            expected_hash=None,
            exclude_paths=False,
            use_sha1=False,
            allow_hidden_files=False,
            max_threads=4,
            dry_run=True,
            verbose=False
        )

        with pytest.raises(SystemExit) as exc_info:
            bl_tool.command_hash(args)

        # Should exit with error when stat fails
        assert exc_info.value.code == 1

        captured = capsys.readouterr()
        assert "[ERROR]" in captured.out
        assert "Failed to stat" in captured.out


class TestExcludePaths:
    """Test --exclude-paths functionality."""

    def test_hash_worker_exclude_paths_same_hash(self, temp_file_small):
        """Test that exclude_paths=True ignores relative path in hash."""
        file_path = str(temp_file_small)

        result1 = bl_tool.hash_worker(file_path, "path1.txt", verbose=False, exclude_paths=True)
        result2 = bl_tool.hash_worker(file_path, "path2.txt", verbose=False, exclude_paths=True)

        # With exclude_paths, different relative paths should produce same hash
        assert result1[1] == result2[1]

    def test_hash_worker_exclude_paths_vs_include(self, temp_file_small):
        """Test that exclude_paths produces different hash than including path."""
        file_path = str(temp_file_small)
        relative_path = Path(file_path).name

        result_with = bl_tool.hash_worker(file_path, relative_path, verbose=False, exclude_paths=False)
        result_without = bl_tool.hash_worker(file_path, relative_path, verbose=False, exclude_paths=True)

        # Including vs excluding path should produce different hashes
        assert result_with[1] != result_without[1]

    def test_command_hash_exclude_paths(self, temp_file_small, capsys):
        """Test command_hash with --exclude-paths flag."""
        import argparse
        import hashlib

        # Calculate expected hash: SHA256(file_contents) only, no path
        file_content = temp_file_small.read_bytes()
        expected_hash = hashlib.sha256(file_content).hexdigest()

        args = argparse.Namespace(
            path=str(temp_file_small),
            expected_hash=expected_hash,
            exclude_paths=True,
            use_sha1=False,
            allow_hidden_files=False,
            max_threads=4,
            dry_run=False,
            verbose=False
        )

        with pytest.raises(SystemExit) as exc_info:
            bl_tool.command_hash(args)

        assert exc_info.value.code == 0

        captured = capsys.readouterr()
        assert "Verification: MATCH" in captured.out


class TestSha1Mode:
    """Test --use-sha1 functionality."""

    def test_hash_worker_sha1_digest_size(self, temp_file_small):
        """Test SHA-1 produces 20-byte digest."""
        file_path = str(temp_file_small)
        relative_path = Path(file_path).name

        result = bl_tool.hash_worker(file_path, relative_path, verbose=False, hash_algo='sha1')

        assert isinstance(result[1], bytes)
        assert len(result[1]) == 20  # SHA-1 = 20 bytes

    def test_hash_worker_sha1_vs_sha256_different(self, temp_file_small):
        """Test SHA-1 and SHA-256 produce different hashes."""
        file_path = str(temp_file_small)
        relative_path = Path(file_path).name

        result_sha256 = bl_tool.hash_worker(file_path, relative_path, verbose=False, hash_algo='sha256')
        result_sha1 = bl_tool.hash_worker(file_path, relative_path, verbose=False, hash_algo='sha1')

        # Different algorithms produce different hash bytes
        assert result_sha256[1] != result_sha1[1]

    def test_combine_hashes_sha1(self):
        """Test combining multiple hashes with SHA-1."""
        import hashlib

        hash1 = hashlib.sha1(b"file1").digest()
        hash2 = hashlib.sha1(b"file2").digest()

        result = bl_tool.combine_hashes([hash1, hash2], hash_algo='sha1')

        # Result should be SHA1(hash1 + hash2)
        expected = hashlib.sha1(hash1 + hash2).hexdigest()
        assert result == expected
        assert len(result) == 40  # SHA-1 hex = 40 chars

    def test_combine_hashes_single_sha1(self):
        """Test single SHA-1 hash returns directly."""
        import hashlib

        hash1 = hashlib.sha1(b"test").digest()
        result = bl_tool.combine_hashes([hash1], hash_algo='sha1')

        # Single hash returned directly
        assert result == hash1.hex()
        assert len(result) == 40

    def test_command_hash_sha1(self, temp_file_small, capsys):
        """Test command_hash with --use-sha1 flag."""
        import argparse

        args = argparse.Namespace(
            path=str(temp_file_small),
            expected_hash=None,
            exclude_paths=False,
            use_sha1=True,
            allow_hidden_files=False,
            max_threads=4,
            dry_run=False,
            verbose=False
        )

        with pytest.raises(SystemExit) as exc_info:
            bl_tool.command_hash(args)

        assert exc_info.value.code == 0

        captured = capsys.readouterr()
        assert "Combined SHA1:" in captured.out
        # SHA-1 hex is 40 chars
        import re
        assert re.search(r"Combined SHA1: [0-9a-f]{40}", captured.out)
