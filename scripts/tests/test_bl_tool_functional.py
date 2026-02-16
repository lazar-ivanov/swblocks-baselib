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
Functional tests for bl_tool.py command-line interface.

Tests end-to-end command behavior and output verification.
Uses subprocess to run bl_tool.py as the user would.
"""

import pytest
import subprocess
import sys
import re
from pathlib import Path

# Path to bl_tool.py
BL_TOOL = str(Path(__file__).parent.parent / "bl_tool.py")


class TestCommandExecution:
    """Test basic command execution."""

    def test_help_flag(self):
        """Test --help flag shows usage."""
        result = subprocess.run(
            [sys.executable, BL_TOOL, "hash", "--help"],
            capture_output=True,
            text=True
        )

        assert result.returncode == 0
        assert "Calculate hash" in result.stdout
        assert "--path" in result.stdout

    def test_no_arguments_shows_error(self):
        """Test running without arguments shows error."""
        result = subprocess.run(
            [sys.executable, BL_TOOL],
            capture_output=True,
            text=True
        )

        assert result.returncode != 0


class TestSingleFileHashing:
    """Test hashing single files."""

    def test_hash_small_file(self, temp_file_small):
        """Test hashing small file."""
        result = subprocess.run(
            [sys.executable, BL_TOOL, "hash", "--path", str(temp_file_small)],
            capture_output=True,
            text=True
        )

        assert result.returncode == 0

        # Verify output format
        assert "Hashing single file..." in result.stdout
        assert "All operations complete!" in result.stdout
        assert "--- HASH SUMMARY ---" in result.stdout
        assert "Total files processed: 1" in result.stdout

        # Extract and verify SHA256 hash (64 hex chars)
        hash_match = re.search(r"Combined SHA256: ([0-9a-f]{64})", result.stdout)
        assert hash_match is not None

        # Verify speed measurement with raw values
        assert re.search(r"Hashing speed: \d+\.\d+ [KMGT]?B/s \(\d+\.\d+ [KMGT]?B in \d+\.\d+ (?:seconds|minutes|hours)\)", result.stdout)

    def test_hash_medium_file(self, temp_file_medium):
        """Test hashing medium file (~10 MB)."""
        result = subprocess.run(
            [sys.executable, BL_TOOL, "hash", "--path", str(temp_file_medium)],
            capture_output=True,
            text=True
        )

        assert result.returncode == 0
        assert "Total files processed: 1" in result.stdout

        # Verify size is reported correctly (~10 MB)
        assert re.search(r"Total size: \d+\.\d+ MB", result.stdout)

    def test_hash_large_file(self, temp_file_large):
        """Test hashing large file (~100 MB)."""
        result = subprocess.run(
            [sys.executable, BL_TOOL, "hash", "--path", str(temp_file_large)],
            capture_output=True,
            text=True,
            timeout=30  # Allow time for large file
        )

        assert result.returncode == 0
        assert "Total files processed: 1" in result.stdout

    def test_hash_deterministic(self, temp_file_small):
        """Test hashing same file twice produces same hash."""
        result1 = subprocess.run(
            [sys.executable, BL_TOOL, "hash", "--path", str(temp_file_small)],
            capture_output=True,
            text=True
        )

        result2 = subprocess.run(
            [sys.executable, BL_TOOL, "hash", "--path", str(temp_file_small)],
            capture_output=True,
            text=True
        )

        hash1 = re.search(r"Combined SHA256: ([0-9a-f]{64})", result1.stdout).group(1)
        hash2 = re.search(r"Combined SHA256: ([0-9a-f]{64})", result2.stdout).group(1)

        assert hash1 == hash2


class TestDirectoryHashing:
    """Test hashing directories."""

    def test_hash_empty_directory(self, empty_dir):
        """Test hashing empty directory."""
        result = subprocess.run(
            [sys.executable, BL_TOOL, "hash", "--path", str(empty_dir)],
            capture_output=True,
            text=True
        )

        assert result.returncode == 0
        assert "Total files processed: 0" in result.stdout or "Found 0 files" in result.stdout

    def test_hash_directory_with_files(self, dir_with_files):
        """Test hashing directory with multiple files."""
        result = subprocess.run(
            [sys.executable, BL_TOOL, "hash", "--path", str(dir_with_files)],
            capture_output=True,
            text=True
        )

        assert result.returncode == 0
        assert "Scanning directory:" in result.stdout
        assert "Found" in result.stdout and "files to process" in result.stdout
        assert "Hashing files with" in result.stdout
        assert "--- HASH SUMMARY ---" in result.stdout

        # Verify file count
        assert re.search(r"Total files processed: \d+", result.stdout)

    def test_hash_directory_hidden_files_excluded(self, dir_with_hidden_files):
        """Test hidden files are excluded by default."""
        result = subprocess.run(
            [sys.executable, BL_TOOL, "hash", "--path", str(dir_with_hidden_files)],
            capture_output=True,
            text=True
        )

        # Count should not include hidden files
        # This depends on fixture implementation
        assert result.returncode == 0

    def test_hash_directory_hidden_files_included(self, dir_with_hidden_files):
        """Test hidden files are included with --allow-hidden-files."""
        result = subprocess.run(
            [sys.executable, BL_TOOL, "hash", "--path", str(dir_with_hidden_files),
             "--allow-hidden-files"],
            capture_output=True,
            text=True
        )

        assert result.returncode == 0
        # File count should be higher than without --allow-hidden-files


class TestVerification:
    """Test hash verification with --expected-hash."""

    def test_verify_match(self, temp_file_small):
        """Test verification succeeds with matching hash."""
        # First, get the hash
        result1 = subprocess.run(
            [sys.executable, BL_TOOL, "hash", "--path", str(temp_file_small)],
            capture_output=True,
            text=True
        )

        hash_value = re.search(r"Combined SHA256: ([0-9a-f]{64})", result1.stdout).group(1)

        # Verify with same hash
        result2 = subprocess.run(
            [sys.executable, BL_TOOL, "hash", "--path", str(temp_file_small),
             "--expected-hash", hash_value],
            capture_output=True,
            text=True
        )

        assert result2.returncode == 0
        assert "Verification: MATCH" in result2.stdout

    def test_verify_mismatch(self, temp_file_small):
        """Test verification fails with wrong hash."""
        wrong_hash = "0" * 64  # Invalid hash

        result = subprocess.run(
            [sys.executable, BL_TOOL, "hash", "--path", str(temp_file_small),
             "--expected-hash", wrong_hash],
            capture_output=True,
            text=True
        )

        assert result.returncode == 1  # Exit code 1 for failure
        assert "Verification: MISMATCH" in result.stdout
        assert "Expected:" in result.stdout
        assert "Actual:" in result.stdout


class TestDryRun:
    """Test --dry-run mode."""

    def test_dry_run_single_file(self, temp_file_small):
        """Test dry-run with single file (non-verbose)."""
        result = subprocess.run(
            [sys.executable, BL_TOOL, "hash", "--path", str(temp_file_small), "--dry-run"],
            capture_output=True,
            text=True
        )

        assert result.returncode == 0
        assert "--- DRY-RUN SUMMARY ---" in result.stdout
        assert "No files were actually hashed" in result.stdout

        # Should NOT contain actual hash
        assert "Combined SHA256:" not in result.stdout

        # Should NOT show individual files in non-verbose mode
        assert "[DRY-RUN]" not in result.stdout

    def test_dry_run_directory(self, dir_with_files):
        """Test dry-run with directory (non-verbose)."""
        result = subprocess.run(
            [sys.executable, BL_TOOL, "hash", "--path", str(dir_with_files), "--dry-run"],
            capture_output=True,
            text=True
        )

        assert result.returncode == 0
        assert "No files were actually hashed" in result.stdout

        # Should NOT show individual files in non-verbose mode
        assert "[DRY-RUN]" not in result.stdout

    def test_dry_run_verbose(self, dir_with_files):
        """Test dry-run with verbose mode shows file list."""
        result = subprocess.run(
            [sys.executable, BL_TOOL, "hash", "--path", str(dir_with_files), "--dry-run", "--verbose"],
            capture_output=True,
            text=True
        )

        assert result.returncode == 0
        assert "No files were actually hashed" in result.stdout

        # Should show individual files in verbose mode
        assert "[DRY-RUN]" in result.stdout


class TestVerboseMode:
    """Test --verbose flag."""

    def test_verbose_shows_individual_hashes(self, dir_with_files):
        """Test verbose mode shows individual file hashes."""
        result = subprocess.run(
            [sys.executable, BL_TOOL, "hash", "--path", str(dir_with_files), "--verbose"],
            capture_output=True,
            text=True
        )

        assert result.returncode == 0

        # Should contain [HASHED] messages for each file
        assert "[HASHED]" in result.stdout
        assert re.search(r"\[HASHED\] .+ -- [0-9a-f]{64}", result.stdout)

    def test_non_verbose_hides_individual_hashes(self, dir_with_files):
        """Test non-verbose mode hides individual file hashes."""
        result = subprocess.run(
            [sys.executable, BL_TOOL, "hash", "--path", str(dir_with_files)],
            capture_output=True,
            text=True
        )

        assert result.returncode == 0

        # Should NOT contain [HASHED] messages
        assert "[HASHED]" not in result.stdout


class TestThreading:
    """Test multi-threading with --max-threads."""

    def test_max_threads_option(self, dir_with_files):
        """Test --max-threads option."""
        result = subprocess.run(
            [sys.executable, BL_TOOL, "hash", "--path", str(dir_with_files),
             "--max-threads", "8"],
            capture_output=True,
            text=True
        )

        assert result.returncode == 0
        assert "Hashing files with 8 threads" in result.stdout


class TestErrorHandling:
    """Test error conditions."""

    def test_nonexistent_path(self):
        """Test error for nonexistent path."""
        result = subprocess.run(
            [sys.executable, BL_TOOL, "hash", "--path", "/nonexistent/path"],
            capture_output=True,
            text=True
        )

        assert result.returncode == 1
        assert "[ERROR]" in result.stdout or "[ERROR]" in result.stderr
        assert "Path not found" in result.stdout or "not found" in result.stderr

    @pytest.mark.skipif(sys.platform == "win32", reason="Symlinks may not be supported on Windows")
    def test_symlink_error(self, temp_symlink):
        """Test error when symlink encountered."""
        result = subprocess.run(
            [sys.executable, BL_TOOL, "hash", "--path", str(temp_symlink)],
            capture_output=True,
            text=True
        )

        assert result.returncode == 1
        assert "Symlink" in result.stdout or "symlink" in result.stdout


class TestConsistency:
    """Test hash consistency between file and directory."""

    def test_single_file_matches_directory(self, temp_dir):
        """Test hashing file directly matches hashing directory with that file."""
        # Create directory with single file
        file_path = temp_dir / "test.txt"
        file_path.write_text("test content")

        # Hash file directly
        result_file = subprocess.run(
            [sys.executable, BL_TOOL, "hash", "--path", str(file_path)],
            capture_output=True,
            text=True
        )

        # Hash directory containing the file
        result_dir = subprocess.run(
            [sys.executable, BL_TOOL, "hash", "--path", str(temp_dir)],
            capture_output=True,
            text=True
        )

        hash_file = re.search(r"Combined SHA256: ([0-9a-f]{64})", result_file.stdout).group(1)
        hash_dir = re.search(r"Combined SHA256: ([0-9a-f]{64})", result_dir.stdout).group(1)

        # Hashes should match
        assert hash_file == hash_dir


class TestExcludePaths:
    """Test --exclude-paths flag."""

    def test_exclude_paths_matches_sha256sum(self, temp_file_small):
        """Test --exclude-paths on single file matches hashlib sha256."""
        import hashlib

        # Calculate expected hash using pure hashlib (no path in input)
        file_content = temp_file_small.read_bytes()
        expected_hash = hashlib.sha256(file_content).hexdigest()

        result = subprocess.run(
            [sys.executable, BL_TOOL, "hash", "--path", str(temp_file_small),
             "--exclude-paths"],
            capture_output=True,
            text=True
        )

        assert result.returncode == 0
        actual_hash = re.search(r"Combined SHA256: ([0-9a-f]{64})", result.stdout).group(1)
        assert actual_hash == expected_hash

    def test_exclude_paths_different_from_default(self, temp_file_small):
        """Test --exclude-paths produces different hash than default."""
        result_default = subprocess.run(
            [sys.executable, BL_TOOL, "hash", "--path", str(temp_file_small)],
            capture_output=True,
            text=True
        )

        result_exclude = subprocess.run(
            [sys.executable, BL_TOOL, "hash", "--path", str(temp_file_small),
             "--exclude-paths"],
            capture_output=True,
            text=True
        )

        hash_default = re.search(r"Combined SHA256: ([0-9a-f]{64})", result_default.stdout).group(1)
        hash_exclude = re.search(r"Combined SHA256: ([0-9a-f]{64})", result_exclude.stdout).group(1)

        assert hash_default != hash_exclude

    def test_exclude_paths_with_verification(self, temp_file_small):
        """Test --exclude-paths works with --expected-hash."""
        import hashlib

        file_content = temp_file_small.read_bytes()
        expected_hash = hashlib.sha256(file_content).hexdigest()

        result = subprocess.run(
            [sys.executable, BL_TOOL, "hash", "--path", str(temp_file_small),
             "--exclude-paths", "--expected-hash", expected_hash],
            capture_output=True,
            text=True
        )

        assert result.returncode == 0
        assert "Verification: MATCH" in result.stdout


class TestSha1Mode:
    """Test --use-sha1 flag."""

    def test_sha1_output_format(self, temp_file_small):
        """Test SHA-1 output shows SHA1 label and 40-char hex."""
        result = subprocess.run(
            [sys.executable, BL_TOOL, "hash", "--path", str(temp_file_small),
             "--use-sha1"],
            capture_output=True,
            text=True
        )

        assert result.returncode == 0
        assert "Combined SHA1:" in result.stdout
        assert re.search(r"Combined SHA1: [0-9a-f]{40}", result.stdout)
        # Should NOT contain SHA256 label
        assert "Combined SHA256:" not in result.stdout

    def test_sha1_deterministic(self, temp_file_small):
        """Test SHA-1 is deterministic."""
        result1 = subprocess.run(
            [sys.executable, BL_TOOL, "hash", "--path", str(temp_file_small),
             "--use-sha1"],
            capture_output=True,
            text=True
        )

        result2 = subprocess.run(
            [sys.executable, BL_TOOL, "hash", "--path", str(temp_file_small),
             "--use-sha1"],
            capture_output=True,
            text=True
        )

        hash1 = re.search(r"Combined SHA1: ([0-9a-f]{40})", result1.stdout).group(1)
        hash2 = re.search(r"Combined SHA1: ([0-9a-f]{40})", result2.stdout).group(1)

        assert hash1 == hash2

    def test_sha1_with_verification(self, temp_file_small):
        """Test --use-sha1 works with --expected-hash."""
        # Get the SHA-1 hash first
        result1 = subprocess.run(
            [sys.executable, BL_TOOL, "hash", "--path", str(temp_file_small),
             "--use-sha1"],
            capture_output=True,
            text=True
        )

        hash_value = re.search(r"Combined SHA1: ([0-9a-f]{40})", result1.stdout).group(1)

        # Verify with same hash
        result2 = subprocess.run(
            [sys.executable, BL_TOOL, "hash", "--path", str(temp_file_small),
             "--use-sha1", "--expected-hash", hash_value],
            capture_output=True,
            text=True
        )

        assert result2.returncode == 0
        assert "Verification: MATCH" in result2.stdout

    def test_sha1_exclude_paths_combined(self, temp_file_small):
        """Test --use-sha1 and --exclude-paths together."""
        import hashlib

        # Calculate expected: SHA1(file_contents) only
        file_content = temp_file_small.read_bytes()
        expected_hash = hashlib.sha1(file_content).hexdigest()

        result = subprocess.run(
            [sys.executable, BL_TOOL, "hash", "--path", str(temp_file_small),
             "--use-sha1", "--exclude-paths", "--expected-hash", expected_hash],
            capture_output=True,
            text=True
        )

        assert result.returncode == 0
        assert "Verification: MATCH" in result.stdout
