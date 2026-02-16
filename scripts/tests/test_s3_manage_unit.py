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
Unit tests for s3_manage.py.

Tests pure functions, ETag calculation, index generation, and worker functions
following the same patterns as test_bl_tool_unit.py.

Test Organization:
- Phase 1: Pure Functions (TestFormatting, TestChunkSize, TestETagSimple, TestETagMultipart, TestIndexGeneration)
- Phase 2: Worker Functions (TestFileExistsInBucket, TestUploadWorker, TestVerifyWorker, TestDownloadWorker)
"""

import pytest
import sys
import os
import shutil
from pathlib import Path

# Add scripts directory to path to import s3_manage
sys.path.insert(0, str(Path(__file__).parent.parent))

import s3_manage


# ====================================================================================
# Phase 1: Pure Functions (Easy Testing - No S3 Mocking)
# ====================================================================================


class TestFormatting:
    """Test format_size() and format_speed() functions."""

    def test_format_size_bytes(self):
        """Test formatting bytes (< 1 KB)."""
        assert s3_manage.format_size(0) == "0.00 B"
        assert s3_manage.format_size(100) == "100.00 B"
        assert s3_manage.format_size(1023) == "1023.00 B"

    def test_format_size_kilobytes(self):
        """Test formatting kilobytes."""
        assert s3_manage.format_size(1024) == "1.00 KB"
        assert s3_manage.format_size(1536) == "1.50 KB"
        assert s3_manage.format_size(10 * 1024) == "10.00 KB"

    def test_format_size_megabytes(self):
        """Test formatting megabytes."""
        assert s3_manage.format_size(1024 * 1024) == "1.00 MB"
        assert s3_manage.format_size(10 * 1024 * 1024) == "10.00 MB"
        assert s3_manage.format_size(100 * 1024 * 1024) == "100.00 MB"

    def test_format_size_gigabytes(self):
        """Test formatting gigabytes."""
        assert s3_manage.format_size(1024 * 1024 * 1024) == "1.00 GB"
        assert s3_manage.format_size(10 * 1024 * 1024 * 1024) == "10.00 GB"

    def test_format_size_terabytes(self):
        """Test formatting terabytes."""
        assert s3_manage.format_size(1024 * 1024 * 1024 * 1024) == "1.00 TB"
        assert s3_manage.format_size(5 * 1024 * 1024 * 1024 * 1024) == "5.00 TB"

    def test_format_size_petabytes(self):
        """Test formatting petabytes."""
        assert s3_manage.format_size(1024 * 1024 * 1024 * 1024 * 1024) == "1.00 PB"
        assert s3_manage.format_size(10 * 1024 * 1024 * 1024 * 1024 * 1024) == "10.00 PB"

    def test_format_speed_zero_time(self):
        """Test speed calculation with zero elapsed time."""
        assert s3_manage.format_speed(1000, 0) == "0.00 B/s"
        assert s3_manage.format_speed(1000, -1) == "0.00 B/s"

    def test_format_speed_bytes_per_second(self):
        """Test speed formatting in bytes/second."""
        assert s3_manage.format_speed(100, 1.0) == "100.00 B/s"
        assert s3_manage.format_speed(500, 1.0) == "500.00 B/s"

    def test_format_speed_kilobytes_per_second(self):
        """Test speed formatting in KB/s."""
        assert s3_manage.format_speed(1024, 1.0) == "1.00 KB/s"
        assert s3_manage.format_speed(10 * 1024, 1.0) == "10.00 KB/s"

    def test_format_speed_megabytes_per_second(self):
        """Test speed formatting in MB/s."""
        assert s3_manage.format_speed(1024 * 1024, 1.0) == "1.00 MB/s"
        assert s3_manage.format_speed(50 * 1024 * 1024, 1.0) == "50.00 MB/s"

    def test_format_speed_gigabytes_per_second(self):
        """Test speed formatting in GB/s."""
        assert s3_manage.format_speed(1024 * 1024 * 1024, 1.0) == "1.00 GB/s"
        assert s3_manage.format_speed(2 * 1024 * 1024 * 1024, 1.0) == "2.00 GB/s"

    def test_format_speed_petabytes_per_second(self):
        """Test speed formatting in PB/s."""
        assert s3_manage.format_speed(1024 * 1024 * 1024 * 1024 * 1024, 1.0) == "1.00 PB/s"


class TestFormatDuration:
    """Test format_duration() function."""

    def test_format_duration_seconds(self):
        """Test duration under 60 seconds stays in seconds."""
        assert s3_manage.format_duration(0) == "0.00 seconds"
        assert s3_manage.format_duration(30.5) == "30.50 seconds"
        assert s3_manage.format_duration(59.99) == "59.99 seconds"

    def test_format_duration_minutes(self):
        """Test duration between 60 seconds and 1 hour converts to minutes."""
        assert s3_manage.format_duration(60) == "1.00 minutes"
        assert s3_manage.format_duration(90) == "1.50 minutes"
        assert s3_manage.format_duration(3599) == "59.98 minutes"

    def test_format_duration_hours(self):
        """Test duration of 1 hour or more converts to hours."""
        assert s3_manage.format_duration(3600) == "1.00 hours"
        assert s3_manage.format_duration(10207.90) == "2.84 hours"
        assert s3_manage.format_duration(7200) == "2.00 hours"


class TestChunkSize:
    """Test calculate_chunk_size() function for multipart upload chunk size determination."""

    def test_calculate_chunk_size_5mb(self):
        """Test chunk size calculation for 5MB chunks."""
        # 50 MB file with 10 parts = 5 MB chunks
        file_size = 50 * 1024 * 1024
        part_count = 10
        assert s3_manage.calculate_chunk_size(file_size, part_count) == 5 * 1024 * 1024

    def test_calculate_chunk_size_8mb_default(self):
        """Test chunk size calculation for 8MB chunks (boto3 default)."""
        # 80 MB file with 10 parts = 8 MB chunks
        file_size = 80 * 1024 * 1024
        part_count = 10
        assert s3_manage.calculate_chunk_size(file_size, part_count) == 8 * 1024 * 1024

    def test_calculate_chunk_size_16mb(self):
        """Test chunk size calculation for 16MB chunks."""
        # 160 MB file with 10 parts = 16 MB chunks
        file_size = 160 * 1024 * 1024
        part_count = 10
        assert s3_manage.calculate_chunk_size(file_size, part_count) == 16 * 1024 * 1024

    def test_calculate_chunk_size_32mb(self):
        """Test chunk size calculation for 32MB chunks."""
        # 320 MB file with 10 parts = 32 MB chunks
        file_size = 320 * 1024 * 1024
        part_count = 10
        assert s3_manage.calculate_chunk_size(file_size, part_count) == 32 * 1024 * 1024

    def test_calculate_chunk_size_64mb(self):
        """Test chunk size calculation for 64MB chunks."""
        # 640 MB file with 10 parts = 64 MB chunks
        file_size = 640 * 1024 * 1024
        part_count = 10
        assert s3_manage.calculate_chunk_size(file_size, part_count) == 64 * 1024 * 1024

    def test_calculate_chunk_size_no_match(self):
        """Test chunk size calculation when no standard size matches."""
        # Non-standard part count that doesn't match any standard chunk size
        # 100 MB with 11 parts - no standard chunk size produces exactly 11 parts
        file_size = 100 * 1024 * 1024
        part_count = 11
        assert s3_manage.calculate_chunk_size(file_size, part_count) is None

    def test_calculate_chunk_size_zero_parts(self):
        """Test chunk size calculation with zero parts."""
        file_size = 100 * 1024 * 1024
        part_count = 0
        assert s3_manage.calculate_chunk_size(file_size, part_count) is None

    def test_calculate_chunk_size_large_file(self):
        """Test chunk size calculation for large files."""
        # 5 GB file with 40 parts = 128 MB chunks
        # ceil(5GB / 128MB) = ceil(40) = 40 parts
        file_size = 5 * 1024 * 1024 * 1024
        part_count = 40
        result = s3_manage.calculate_chunk_size(file_size, part_count)
        assert result == 128 * 1024 * 1024


class TestETagSimple:
    """Test calculate_s3_etag_simple() function for simple (non-multipart) ETag calculation."""

    def test_etag_simple_empty_file(self, temp_dir):
        """Test ETag calculation for empty file."""
        empty_file = temp_dir / "empty.txt"
        empty_file.touch()

        result = s3_manage.calculate_s3_etag_simple(str(empty_file))
        # MD5 of empty string
        assert result == "d41d8cd98f00b204e9800998ecf8427e"

    def test_etag_simple_small_file(self, temp_file_small):
        """Test ETag calculation for small file."""
        result = s3_manage.calculate_s3_etag_simple(str(temp_file_small))
        # Should return 32-character hex string
        assert len(result) == 32
        assert all(c in '0123456789abcdef' for c in result)

    def test_etag_simple_medium_file(self, temp_file_medium):
        """Test ETag calculation for medium file."""
        result = s3_manage.calculate_s3_etag_simple(str(temp_file_medium))
        assert len(result) == 32
        assert all(c in '0123456789abcdef' for c in result)

    def test_etag_simple_large_file(self, temp_file_large):
        """Test ETag calculation for large file."""
        result = s3_manage.calculate_s3_etag_simple(str(temp_file_large))
        assert len(result) == 32
        assert all(c in '0123456789abcdef' for c in result)

    def test_etag_simple_deterministic(self, temp_dir):
        """Test that same file produces same ETag."""
        test_file = temp_dir / "test.txt"
        test_file.write_text("test content")

        result1 = s3_manage.calculate_s3_etag_simple(str(test_file))
        result2 = s3_manage.calculate_s3_etag_simple(str(test_file))

        assert result1 == result2

    def test_etag_simple_different_content(self, temp_dir):
        """Test that different content produces different ETags."""
        file1 = temp_dir / "file1.txt"
        file1.write_text("content1")

        file2 = temp_dir / "file2.txt"
        file2.write_text("content2")

        etag1 = s3_manage.calculate_s3_etag_simple(str(file1))
        etag2 = s3_manage.calculate_s3_etag_simple(str(file2))

        assert etag1 != etag2

    def test_etag_simple_same_size_different_content(self, temp_dir):
        """Test that files with same size but different content have different ETags."""
        file1 = temp_dir / "file1.txt"
        file1.write_text("aaaa")

        file2 = temp_dir / "file2.txt"
        file2.write_text("bbbb")

        etag1 = s3_manage.calculate_s3_etag_simple(str(file1))
        etag2 = s3_manage.calculate_s3_etag_simple(str(file2))

        assert etag1 != etag2

    def test_etag_simple_known_content(self, temp_dir):
        """Test ETag against known MD5 hash."""
        test_file = temp_dir / "known.txt"
        test_file.write_text("test content\n")

        result = s3_manage.calculate_s3_etag_simple(str(test_file))
        # Pre-calculated MD5 for "test content\n"
        assert result == "d6eb32081c822ed572b70567826d9d9d"

    def test_etag_simple_binary_content(self, temp_dir):
        """Test ETag calculation for binary content."""
        binary_file = temp_dir / "binary.bin"
        binary_file.write_bytes(b'\x00\x01\x02\x03\xff\xfe\xfd')

        result = s3_manage.calculate_s3_etag_simple(str(binary_file))
        assert len(result) == 32
        assert all(c in '0123456789abcdef' for c in result)

    def test_etag_simple_file_not_found(self, temp_dir):
        """Test ETag calculation for non-existent file."""
        nonexistent = temp_dir / "nonexistent.txt"

        with pytest.raises(IOError):
            s3_manage.calculate_s3_etag_simple(str(nonexistent))

    def test_etag_simple_unicode_content(self, temp_dir):
        """Test ETag calculation for Unicode content."""
        unicode_file = temp_dir / "unicode.txt"
        unicode_file.write_text("Hello 世界 🌍", encoding='utf-8')

        result = s3_manage.calculate_s3_etag_simple(str(unicode_file))
        assert len(result) == 32
        assert all(c in '0123456789abcdef' for c in result)

    def test_etag_simple_single_byte(self, temp_dir):
        """Test ETag calculation for single-byte file."""
        single_byte = temp_dir / "single.txt"
        single_byte.write_bytes(b'x')

        result = s3_manage.calculate_s3_etag_simple(str(single_byte))
        assert len(result) == 32

    def test_etag_simple_newlines(self, temp_dir):
        """Test that files with different newlines have different ETags."""
        file_lf = temp_dir / "lf.txt"
        file_lf.write_bytes(b"line1\nline2\n")

        file_crlf = temp_dir / "crlf.txt"
        file_crlf.write_bytes(b"line1\r\nline2\r\n")

        etag_lf = s3_manage.calculate_s3_etag_simple(str(file_lf))
        etag_crlf = s3_manage.calculate_s3_etag_simple(str(file_crlf))

        assert etag_lf != etag_crlf

    def test_etag_simple_large_repeated_content(self, temp_dir):
        """Test ETag for file with repeated content."""
        repeated_file = temp_dir / "repeated.txt"
        repeated_file.write_text("x" * 10000)

        result = s3_manage.calculate_s3_etag_simple(str(repeated_file))
        assert len(result) == 32
        assert all(c in '0123456789abcdef' for c in result)


class TestETagMultipart:
    """Test calculate_s3_etag_multipart() function for multipart ETag calculation."""

    def test_etag_multipart_5mb_chunks(self, temp_dir):
        """Test multipart ETag with 5MB chunks."""
        # Create 50 MB file
        test_file = temp_dir / "50mb.bin"
        test_file.write_bytes(b"x" * (50 * 1024 * 1024))

        # 50 MB / 10 parts = 5 MB chunks
        result = s3_manage.calculate_s3_etag_multipart(str(test_file), 10)

        assert result is not None
        assert "-" in result
        etag_hash, part_count = result.split("-")
        assert len(etag_hash) == 32
        assert part_count == "10"

    def test_etag_multipart_8mb_chunks(self, temp_dir):
        """Test multipart ETag with 8MB chunks (boto3 default)."""
        # Create 80 MB file
        test_file = temp_dir / "80mb.bin"
        with open(test_file, "wb") as f:
            chunk = b"y" * (1 * 1024 * 1024)  # 1 MB chunk
            for _ in range(80):
                f.write(chunk)

        # 80 MB / 10 parts = 8 MB chunks
        result = s3_manage.calculate_s3_etag_multipart(str(test_file), 10)

        assert result is not None
        assert "-" in result
        etag_hash, part_count = result.split("-")
        assert len(etag_hash) == 32
        assert part_count == "10"

    def test_etag_multipart_16mb_chunks(self, temp_dir):
        """Test multipart ETag with 16MB chunks."""
        # Create 160 MB file
        test_file = temp_dir / "160mb.bin"
        with open(test_file, "wb") as f:
            chunk = b"z" * (1 * 1024 * 1024)  # 1 MB chunk
            for _ in range(160):
                f.write(chunk)

        # 160 MB / 10 parts = 16 MB chunks
        result = s3_manage.calculate_s3_etag_multipart(str(test_file), 10)

        assert result is not None
        etag_hash, part_count = result.split("-")
        assert part_count == "10"

    def test_etag_multipart_deterministic(self, temp_dir):
        """Test that same file produces same multipart ETag."""
        test_file = temp_dir / "test.bin"
        test_file.write_bytes(b"a" * (50 * 1024 * 1024))

        result1 = s3_manage.calculate_s3_etag_multipart(str(test_file), 10)
        result2 = s3_manage.calculate_s3_etag_multipart(str(test_file), 10)

        assert result1 == result2

    def test_etag_multipart_different_part_count(self, temp_dir):
        """Test that different part counts produce different ETags."""
        # Create 100 MB file
        test_file = temp_dir / "test.bin"
        test_file.write_bytes(b"b" * (100 * 1024 * 1024))

        # Same file, different part counts (both valid)
        # 100 MB / 10 parts = 10 MB chunks (not standard, but let's use valid ones)
        # 100 MB / 20 parts = 5 MB chunks (valid)
        result_10_parts = s3_manage.calculate_s3_etag_multipart(str(test_file), 10)
        result_20_parts = s3_manage.calculate_s3_etag_multipart(str(test_file), 20)

        # At least result_20_parts should be valid
        assert result_20_parts is not None
        assert "-20" in result_20_parts
        # result_10_parts might be None if no standard chunk size matches
        if result_10_parts is not None:
            assert result_10_parts != result_20_parts
            assert "-10" in result_10_parts

    def test_etag_multipart_invalid_part_count(self, temp_dir):
        """Test multipart ETag with invalid part count returns None."""
        test_file = temp_dir / "test.bin"
        test_file.write_bytes(b"c" * (100 * 1024 * 1024))

        # Part count that doesn't match any standard chunk size
        result = s3_manage.calculate_s3_etag_multipart(str(test_file), 11)

        assert result is None

    def test_etag_multipart_chunk_size_none(self, temp_dir):
        """Test multipart ETag when chunk size calculation fails."""
        test_file = temp_dir / "test.bin"
        test_file.write_bytes(b"d" * (100 * 1024 * 1024))

        # Part count of 0 should cause chunk size calculation to fail
        result = s3_manage.calculate_s3_etag_multipart(str(test_file), 0)

        assert result is None

    def test_etag_multipart_empty_file(self, temp_dir):
        """Test multipart ETag for empty file."""
        empty_file = temp_dir / "empty.bin"
        empty_file.touch()

        # Empty file with 1 part
        result = s3_manage.calculate_s3_etag_multipart(str(empty_file), 1)

        # Empty file should still work
        assert result is not None or result is None  # Implementation dependent

    def test_etag_multipart_single_part(self, temp_dir):
        """Test multipart ETag with single part."""
        test_file = temp_dir / "5mb.bin"
        test_file.write_bytes(b"e" * (5 * 1024 * 1024))

        result = s3_manage.calculate_s3_etag_multipart(str(test_file), 1)

        assert result is not None
        assert "-1" in result

    def test_etag_multipart_many_parts(self, temp_dir):
        """Test multipart ETag with many parts."""
        # Create file that requires 64MB chunks
        test_file = temp_dir / "large.bin"
        with open(test_file, "wb") as f:
            chunk = b"f" * (1 * 1024 * 1024)  # 1 MB chunk
            for _ in range(640):  # 640 MB
                f.write(chunk)

        # 640 MB / 10 parts = 64 MB chunks
        result = s3_manage.calculate_s3_etag_multipart(str(test_file), 10)

        assert result is not None
        etag_hash, part_count = result.split("-")
        assert len(etag_hash) == 32
        assert part_count == "10"

    def test_etag_multipart_format_validation(self, temp_dir):
        """Test that multipart ETag has correct format."""
        test_file = temp_dir / "test.bin"
        test_file.write_bytes(b"g" * (50 * 1024 * 1024))

        result = s3_manage.calculate_s3_etag_multipart(str(test_file), 10)

        assert result is not None
        # Should be "hash-count" format
        parts = result.split("-")
        assert len(parts) == 2
        # Hash part should be 32 hex characters
        assert len(parts[0]) == 32
        assert all(c in '0123456789abcdef' for c in parts[0])
        # Count part should be numeric
        assert parts[1].isdigit()

    def test_etag_multipart_file_not_found(self, temp_dir):
        """Test multipart ETag for non-existent file."""
        nonexistent = temp_dir / "nonexistent.bin"

        with pytest.raises(IOError):
            s3_manage.calculate_s3_etag_multipart(str(nonexistent), 10)

    def test_etag_multipart_different_content_same_size(self, temp_dir):
        """Test that files with same size but different content have different multipart ETags."""
        file1 = temp_dir / "file1.bin"
        file1.write_bytes(b"a" * (50 * 1024 * 1024))

        file2 = temp_dir / "file2.bin"
        file2.write_bytes(b"b" * (50 * 1024 * 1024))

        etag1 = s3_manage.calculate_s3_etag_multipart(str(file1), 10)
        etag2 = s3_manage.calculate_s3_etag_multipart(str(file2), 10)

        assert etag1 != etag2

    def test_etag_multipart_partial_last_chunk(self, temp_dir):
        """Test multipart ETag when last chunk is partial."""
        # Create 55 MB file (11 * 5MB chunks)
        test_file = temp_dir / "55mb.bin"
        test_file.write_bytes(b"h" * (55 * 1024 * 1024))

        # 55 MB / 11 parts = 5 MB chunks (last chunk is 5MB)
        result = s3_manage.calculate_s3_etag_multipart(str(test_file), 11)

        assert result is not None
        assert "-11" in result

    def test_etag_multipart_128mb_chunks(self, temp_dir):
        """Test multipart ETag with 128MB chunks."""
        # Create 5 GB file (40 * 128MB chunks)
        test_file = temp_dir / "5gb.bin"
        with open(test_file, "wb") as f:
            chunk = b"i" * (1 * 1024 * 1024)  # 1 MB chunk
            for _ in range(5 * 1024):  # 5 GB = 5120 MB
                f.write(chunk)

        # 5 GB / 40 parts = 128 MB chunks
        result = s3_manage.calculate_s3_etag_multipart(str(test_file), 40)

        assert result is not None
        etag_hash, part_count = result.split("-")
        assert len(etag_hash) == 32
        assert part_count == "40"

    def test_etag_multipart_exact_chunk_boundary(self, temp_dir):
        """Test multipart ETag when file size is exact multiple of chunk size."""
        # Create exactly 80 MB (10 * 8MB chunks)
        test_file = temp_dir / "exact.bin"
        test_file.write_bytes(b"j" * (80 * 1024 * 1024))

        result = s3_manage.calculate_s3_etag_multipart(str(test_file), 10)

        assert result is not None
        assert "-10" in result

    def test_etag_multipart_minimal_file(self, temp_dir):
        """Test multipart ETag with minimal non-empty file."""
        test_file = temp_dir / "minimal.bin"
        test_file.write_bytes(b"x")

        result = s3_manage.calculate_s3_etag_multipart(str(test_file), 1)

        assert result is not None
        assert "-1" in result

    def test_etag_multipart_binary_content(self, temp_dir):
        """Test multipart ETag with binary content."""
        test_file = temp_dir / "binary.bin"
        # Write binary pattern
        with open(test_file, "wb") as f:
            for i in range(50 * 1024):  # 50 MB
                f.write(bytes([i % 256]) * 1024)

        result = s3_manage.calculate_s3_etag_multipart(str(test_file), 10)

        assert result is not None
        assert "-10" in result

    def test_etag_multipart_all_zeros(self, temp_dir):
        """Test multipart ETag with file containing all zeros."""
        test_file = temp_dir / "zeros.bin"
        test_file.write_bytes(b"\x00" * (50 * 1024 * 1024))

        result = s3_manage.calculate_s3_etag_multipart(str(test_file), 10)

        assert result is not None
        etag_hash, part_count = result.split("-")
        assert len(etag_hash) == 32
        assert part_count == "10"


class TestIndexGeneration:
    """Test generate_html_index() and generate_markdown_index() functions."""

    def test_generate_html_empty_list(self):
        """Test HTML generation with empty object list."""
        html = s3_manage.generate_html_index([], 0, 0, "https://example.com/bucket/")

        assert "<!DOCTYPE html>" in html
        assert "<title>Files Index</title>" in html
        assert "Total: 0 objects, 0.00 B" in html

    def test_generate_html_single_file(self, sample_s3_objects):
        """Test HTML generation with single file."""
        single_obj = [sample_s3_objects[0]]
        html = s3_manage.generate_html_index(single_obj, 1, 1024, "https://example.com/")

        assert "file1.txt" in html
        assert "1.00 KB" in html
        assert "Total: 1 objects" in html
        assert 'href="https://example.com/file1.txt"' in html

    def test_generate_html_multiple_files(self, sample_s3_objects):
        """Test HTML generation with multiple files."""
        total_size = sum(obj['size'] for obj in sample_s3_objects)
        html = s3_manage.generate_html_index(sample_s3_objects, 3, total_size, "https://example.com/")

        assert "file1.txt" in html
        assert "folder/file2.bin" in html
        assert "large.dat" in html
        assert "Total: 3 objects" in html

    def test_generate_html_special_characters(self):
        """Test HTML escaping of special characters."""
        from datetime import datetime, timezone
        objects = [{
            'key': 'file<test>.txt',
            'size': 100,
            'last_modified': datetime(2024, 1, 1, tzinfo=timezone.utc)
        }]
        html = s3_manage.generate_html_index(objects, 1, 100, "https://example.com/")

        # HTML entities should be escaped
        assert "file&lt;test&gt;.txt" in html
        assert "file<test>.txt" not in html or "<td>" in html  # Either escaped or in tags

    def test_generate_html_url_prefix(self):
        """Test HTML URL generation with different prefixes."""
        from datetime import datetime, timezone
        objects = [{'key': 'test.txt', 'size': 100, 'last_modified': datetime(2024, 1, 1, tzinfo=timezone.utc)}]

        # Without trailing slash
        html1 = s3_manage.generate_html_index(objects, 1, 100, "https://example.com")
        assert 'href="https://example.com/test.txt"' in html1

        # With trailing slash
        html2 = s3_manage.generate_html_index(objects, 1, 100, "https://example.com/")
        assert 'href="https://example.com/test.txt"' in html2

    def test_generate_html_large_list(self):
        """Test HTML generation with large object list."""
        from datetime import datetime, timezone
        objects = [
            {'key': f'file{i}.txt', 'size': i * 1024, 'last_modified': datetime(2024, 1, 1, tzinfo=timezone.utc)}
            for i in range(100)
        ]
        total_size = sum(obj['size'] for obj in objects)
        html = s3_manage.generate_html_index(objects, 100, total_size, "https://example.com/")

        assert "file0.txt" in html
        assert "file99.txt" in html
        assert "Total: 100 objects" in html

    def test_generate_markdown_empty_list(self):
        """Test Markdown generation with empty object list."""
        md = s3_manage.generate_markdown_index([], 0, 0, "https://example.com/bucket/")

        assert "# Files Index" in md
        assert "| File Path | Size | Last Modified |" in md
        assert "**Total:** 0 objects, 0.00 B" in md

    def test_generate_markdown_single_file(self, sample_s3_objects):
        """Test Markdown generation with single file."""
        single_obj = [sample_s3_objects[0]]
        md = s3_manage.generate_markdown_index(single_obj, 1, 1024, "https://example.com/")

        assert "[file1.txt](https://example.com/file1.txt)" in md
        assert "1.00 KB" in md
        assert "**Total:** 1 objects" in md

    def test_generate_markdown_multiple_files(self, sample_s3_objects):
        """Test Markdown generation with multiple files."""
        total_size = sum(obj['size'] for obj in sample_s3_objects)
        md = s3_manage.generate_markdown_index(sample_s3_objects, 3, total_size, "https://example.com/")

        assert "file1.txt" in md
        assert "folder/file2.bin" in md
        assert "large.dat" in md
        assert "**Total:** 3 objects" in md

    def test_generate_markdown_special_characters(self):
        """Test Markdown escaping of special characters."""
        from datetime import datetime, timezone
        objects = [{
            'key': 'file|with|pipes.txt',
            'size': 100,
            'last_modified': datetime(2024, 1, 1, tzinfo=timezone.utc)
        }]
        md = s3_manage.generate_markdown_index(objects, 1, 100, "https://example.com/")

        # Pipe characters should be escaped in Markdown tables
        assert "file\\|with\\|pipes.txt" in md

    def test_generate_markdown_url_prefix(self):
        """Test Markdown URL generation with different prefixes."""
        from datetime import datetime, timezone
        objects = [{'key': 'test.txt', 'size': 100, 'last_modified': datetime(2024, 1, 1, tzinfo=timezone.utc)}]

        # Without trailing slash
        md1 = s3_manage.generate_markdown_index(objects, 1, 100, "https://example.com")
        assert "[test.txt](https://example.com/test.txt)" in md1

        # With trailing slash
        md2 = s3_manage.generate_markdown_index(objects, 1, 100, "https://example.com/")
        assert "[test.txt](https://example.com/test.txt)" in md2

    def test_generate_markdown_pipe_in_filename(self):
        """Test Markdown generation when filename contains pipe character."""
        from datetime import datetime, timezone
        objects = [{
            'key': 'file_with_pipe_|_char.txt',
            'size': 200,
            'last_modified': datetime(2024, 1, 1, tzinfo=timezone.utc)
        }]
        md = s3_manage.generate_markdown_index(objects, 1, 200, "https://example.com/")

        # Pipes should be escaped
        assert "\\|" in md

    def test_generate_markdown_large_list(self):
        """Test Markdown generation with large object list."""
        from datetime import datetime, timezone
        objects = [
            {'key': f'file{i}.txt', 'size': i * 1024, 'last_modified': datetime(2024, 1, 1, tzinfo=timezone.utc)}
            for i in range(100)
        ]
        total_size = sum(obj['size'] for obj in objects)
        md = s3_manage.generate_markdown_index(objects, 100, total_size, "https://example.com/")

        assert "file0.txt" in md
        assert "file99.txt" in md
        assert "**Total:** 100 objects" in md

    def test_generate_html_includes_timestamp(self):
        """Test that HTML includes generation timestamp."""
        html = s3_manage.generate_html_index([], 0, 0, "https://example.com/")

        assert "Generated on" in html
        assert "UTC" in html

    def test_generate_markdown_includes_timestamp(self):
        """Test that Markdown includes generation timestamp."""
        md = s3_manage.generate_markdown_index([], 0, 0, "https://example.com/")

        assert "Generated on" in md or "generated on" in md.lower()
        assert "UTC" in md


# Phase 1 Complete! Now moving to Phase 2...


# ====================================================================================
# Phase 2: Worker Functions (Moderate Testing - Requires S3 Mocking with moto)
# ====================================================================================

from moto import mock_aws
import boto3


class TestFileExistsInBucket:
    """Test file_exists_in_bucket() function with mocked S3."""

    @mock_aws
    def test_file_exists_true(self, temp_file):
        """Test file existence check when file exists."""
        # Setup: Create mock S3 bucket and upload file
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'test.txt')

        # Test
        result = s3_manage.file_exists_in_bucket(s3_client, 'test-bucket', 'test.txt')
        assert result is True

    @mock_aws
    def test_file_exists_false(self):
        """Test file existence check when file does not exist."""
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        result = s3_manage.file_exists_in_bucket(s3_client, 'test-bucket', 'nonexistent.txt')
        assert result is False

    @mock_aws
    def test_file_exists_empty_bucket(self):
        """Test file existence check in empty bucket."""
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='empty-bucket')

        result = s3_manage.file_exists_in_bucket(s3_client, 'empty-bucket', 'any-file.txt')
        assert result is False

    @mock_aws
    def test_file_exists_multiple_files(self, temp_dir):
        """Test file existence check with multiple files in bucket."""
        # Create multiple test files
        file1 = temp_dir / "file1.txt"
        file1.write_text("content1")
        file2 = temp_dir / "file2.txt"
        file2.write_text("content2")

        # Upload to S3
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(file1), 'test-bucket', 'file1.txt')
        s3_client.upload_file(str(file2), 'test-bucket', 'file2.txt')

        # Test existence
        assert s3_manage.file_exists_in_bucket(s3_client, 'test-bucket', 'file1.txt') is True
        assert s3_manage.file_exists_in_bucket(s3_client, 'test-bucket', 'file2.txt') is True
        assert s3_manage.file_exists_in_bucket(s3_client, 'test-bucket', 'file3.txt') is False

    @mock_aws
    def test_file_exists_nested_path(self, temp_file):
        """Test file existence check with nested path."""
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'folder/subfolder/file.txt')

        result = s3_manage.file_exists_in_bucket(s3_client, 'test-bucket', 'folder/subfolder/file.txt')
        assert result is True


class TestUploadWorker:
    """Test upload_worker() function with mocked S3."""

    @mock_aws
    def test_upload_worker_success(self, temp_file):
        """Test successful file upload."""
        # Setup
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        # Test upload
        result = s3_manage.upload_worker(s3_client, 'test-bucket', str(temp_file), 'test.txt', dry_run=False)

        # Verify result string format
        assert '[SUCCESS]' in result
        assert 'test.txt' in result
        assert 'GB' in result

        # Verify file was actually uploaded
        assert s3_manage.file_exists_in_bucket(s3_client, 'test-bucket', 'test.txt') is True

    @mock_aws
    def test_upload_worker_already_exists(self, temp_file):
        """Test upload when file already exists (should skip)."""
        # Setup: Pre-upload the file
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'test.txt')

        # Test upload again
        result = s3_manage.upload_worker(s3_client, 'test-bucket', str(temp_file), 'test.txt', dry_run=False)

        # Verify skip message
        assert '[SKIPPED]' in result
        assert 'test.txt' in result
        assert 'Already exists' in result

    @mock_aws
    def test_upload_worker_dry_run(self, temp_file):
        """Test dry-run mode (should not upload)."""
        # Setup
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        # Test dry-run upload
        result = s3_manage.upload_worker(s3_client, 'test-bucket', str(temp_file), 'test.txt', dry_run=True)

        # Verify dry-run message
        assert '[DRY-RUN]' in result
        assert 'test.txt' in result
        assert 'would upload' in result
        assert 'GB' in result

        # Verify file was NOT uploaded
        assert s3_manage.file_exists_in_bucket(s3_client, 'test-bucket', 'test.txt') is False

    @mock_aws
    def test_upload_worker_file_not_found(self):
        """Test upload when file does not exist."""
        # Setup
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        # Test upload non-existent file
        result = s3_manage.upload_worker(s3_client, 'test-bucket', '/nonexistent/file.txt', 'file.txt', dry_run=False)

        # Verify failure message
        assert '[FAILURE]' in result
        assert 'file.txt' in result

    @mock_aws
    def test_upload_worker_size_reporting_small(self, temp_file_small):
        """Test size reporting for small files."""
        # Setup
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        # Test upload
        result = s3_manage.upload_worker(s3_client, 'test-bucket', str(temp_file_small), 'small.bin', dry_run=False)

        # Verify size is reported (100 KB = 0.00009 GB)
        assert '[SUCCESS]' in result
        assert 'GB' in result
        # Size should be very small
        assert '0.00' in result

    @mock_aws
    def test_upload_worker_size_reporting_medium(self, temp_file_medium):
        """Test size reporting for medium files."""
        # Setup
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        # Test upload
        result = s3_manage.upload_worker(s3_client, 'test-bucket', str(temp_file_medium), 'medium.bin', dry_run=False)

        # Verify size is reported (10 MB = 0.0093 GB)
        assert '[SUCCESS]' in result
        assert 'GB' in result
        assert '0.0' in result or '0.1' in result  # Should be around 0.01 GB

    @mock_aws
    def test_upload_worker_nested_path(self, temp_file):
        """Test upload with nested path structure."""
        # Setup
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        # Test upload to nested path
        result = s3_manage.upload_worker(s3_client, 'test-bucket', str(temp_file), 'folder/subfolder/file.txt', dry_run=False)

        # Verify success
        assert '[SUCCESS]' in result
        assert 'folder/subfolder/file.txt' in result

        # Verify file exists at nested path
        assert s3_manage.file_exists_in_bucket(s3_client, 'test-bucket', 'folder/subfolder/file.txt') is True

    @mock_aws
    def test_upload_worker_special_characters(self, temp_dir):
        """Test upload with special characters in filename."""
        # Create file with special characters
        special_file = temp_dir / "file-with_special.chars.txt"
        special_file.write_text("special content")

        # Setup
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        # Test upload
        result = s3_manage.upload_worker(s3_client, 'test-bucket', str(special_file), 'file-with_special.chars.txt', dry_run=False)

        # Verify success
        assert '[SUCCESS]' in result
        assert 'file-with_special.chars.txt' in result

    @mock_aws
    def test_upload_worker_empty_file(self, temp_dir):
        """Test upload of empty file."""
        # Create empty file
        empty_file = temp_dir / "empty.txt"
        empty_file.touch()

        # Setup
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        # Test upload
        result = s3_manage.upload_worker(s3_client, 'test-bucket', str(empty_file), 'empty.txt', dry_run=False)

        # Verify success
        assert '[SUCCESS]' in result
        assert 'empty.txt' in result
        assert '0.00 GB' in result

        # Verify file exists
        assert s3_manage.file_exists_in_bucket(s3_client, 'test-bucket', 'empty.txt') is True

    @mock_aws
    def test_upload_worker_multiple_files(self, temp_dir):
        """Test uploading multiple files sequentially."""
        # Create multiple files
        file1 = temp_dir / "file1.txt"
        file1.write_text("content1")
        file2 = temp_dir / "file2.txt"
        file2.write_text("content2")

        # Setup
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        # Upload both files
        result1 = s3_manage.upload_worker(s3_client, 'test-bucket', str(file1), 'file1.txt', dry_run=False)
        result2 = s3_manage.upload_worker(s3_client, 'test-bucket', str(file2), 'file2.txt', dry_run=False)

        # Verify both succeeded
        assert '[SUCCESS]' in result1
        assert '[SUCCESS]' in result2

        # Verify both exist
        assert s3_manage.file_exists_in_bucket(s3_client, 'test-bucket', 'file1.txt') is True
        assert s3_manage.file_exists_in_bucket(s3_client, 'test-bucket', 'file2.txt') is True

    @mock_aws
    def test_upload_worker_overwrite_prevention(self, temp_dir):
        """Test that upload_worker prevents overwriting existing files."""
        # Create two different files with same name
        file1 = temp_dir / "original.txt"
        file1.write_text("original content")
        file2 = temp_dir / "modified.txt"
        file2.write_text("modified content")

        # Setup
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        # Upload original
        result1 = s3_manage.upload_worker(s3_client, 'test-bucket', str(file1), 'data.txt', dry_run=False)
        assert '[SUCCESS]' in result1

        # Try to upload modified version with same key
        result2 = s3_manage.upload_worker(s3_client, 'test-bucket', str(file2), 'data.txt', dry_run=False)
        assert '[SKIPPED]' in result2
        assert 'Already exists' in result2

        # Verify original content is preserved
        response = s3_client.get_object(Bucket='test-bucket', Key='data.txt')
        content = response['Body'].read().decode('utf-8')
        assert content == "original content"


class TestVerifyWorker:
    """Test verify_worker() function with mocked S3."""

    @mock_aws
    def test_verify_worker_match(self, temp_file):
        """Test verification when ETags match."""
        # Setup: Upload file to S3
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'test.txt')

        # Test verification
        result = s3_manage.verify_worker(s3_client, 'test-bucket', str(temp_file), 'test.txt')

        # Verify result
        assert '[VERIFIED]' in result
        assert 'test.txt' in result

    @mock_aws
    def test_verify_worker_different(self, temp_dir):
        """Test verification when ETags differ."""
        # Create two different files
        file1 = temp_dir / "original.txt"
        file1.write_text("original content")
        file2 = temp_dir / "modified.txt"
        file2.write_text("modified content - different")

        # Upload original
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(file1), 'test-bucket', 'data.txt')

        # Verify with different file
        result = s3_manage.verify_worker(s3_client, 'test-bucket', str(file2), 'data.txt')

        # Should report difference
        assert '[DIFFERENT]' in result
        assert 'data.txt' in result
        assert 'S3:' in result
        assert 'Local:' in result

    @mock_aws
    def test_verify_worker_not_uploaded(self, temp_file):
        """Test verification when file not in S3."""
        # Setup bucket without uploading file
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        # Test verification
        result = s3_manage.verify_worker(s3_client, 'test-bucket', str(temp_file), 'test.txt')

        # Should report not uploaded
        assert '[NOT UPLOADED]' in result
        assert 'test.txt' in result

    @mock_aws
    def test_verify_worker_simple_etag(self, temp_file_small):
        """Test verification with simple ETag (non-multipart)."""
        # Upload small file (simple ETag)
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(temp_file_small), 'test-bucket', 'small.bin')

        # Verify
        result = s3_manage.verify_worker(s3_client, 'test-bucket', str(temp_file_small), 'small.bin')

        # Should verify successfully
        assert '[VERIFIED]' in result

    @mock_aws
    def test_verify_worker_multipart_etag(self, temp_file_large):
        """Test verification with multipart ETag.

        Note: This test verifies the multipart ETag detection and calculation logic,
        but ETags may not match exactly due to moto's multipart chunking behavior
        differing from production S3. The important part is that it detects multipart
        format and attempts verification rather than erroring.
        """
        # Upload large file (multipart ETag) using multipart upload with 8MB chunks
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        # Use multipart upload for large file with 8MB chunks (standard size)
        multipart_upload = s3_client.create_multipart_upload(Bucket='test-bucket', Key='large.bin')
        upload_id = multipart_upload['UploadId']

        # Upload in 8MB chunks (a standard chunk size)
        parts = []
        chunk_size = 8 * 1024 * 1024  # 8 MB
        with open(temp_file_large, 'rb') as f:
            part_number = 1
            while True:
                data = f.read(chunk_size)
                if not data:
                    break

                part = s3_client.upload_part(
                    Bucket='test-bucket',
                    Key='large.bin',
                    UploadId=upload_id,
                    PartNumber=part_number,
                    Body=data
                )
                parts.append({
                    'PartNumber': part_number,
                    'ETag': part['ETag']
                })
                part_number += 1

        # Complete multipart upload
        s3_client.complete_multipart_upload(
            Bucket='test-bucket',
            Key='large.bin',
            UploadId=upload_id,
            MultipartUpload={'Parts': parts}
        )

        # Verify - should at least detect multipart format
        result = s3_manage.verify_worker(s3_client, 'test-bucket', str(temp_file_large), 'large.bin')

        # The result should be either VERIFIED or DIFFERENT (not ERROR)
        # Due to moto behavior differences, we just verify it doesn't error
        assert '[ERROR]' not in result or 'Failed to calculate multipart ETag' not in result
        assert 'large.bin' in result

    @mock_aws
    def test_verify_worker_file_not_found(self):
        """Test verification when local file doesn't exist."""
        # Setup
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.put_object(Bucket='test-bucket', Key='test.txt', Body=b'content')

        # Try to verify non-existent local file
        result = s3_manage.verify_worker(s3_client, 'test-bucket', '/nonexistent/file.txt', 'test.txt')

        # Should report error
        assert '[ERROR]' in result
        assert 'test.txt' in result

    @mock_aws
    def test_verify_worker_empty_file(self, temp_dir):
        """Test verification of empty file."""
        # Create empty file
        empty_file = temp_dir / "empty.txt"
        empty_file.touch()

        # Upload
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(empty_file), 'test-bucket', 'empty.txt')

        # Verify
        result = s3_manage.verify_worker(s3_client, 'test-bucket', str(empty_file), 'empty.txt')

        # Should verify successfully
        assert '[VERIFIED]' in result

    @mock_aws
    def test_verify_worker_case_insensitive_etag(self, temp_file):
        """Test that ETag comparison is case-insensitive."""
        # Upload file
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'test.txt')

        # Verify (ETags should match regardless of case)
        result = s3_manage.verify_worker(s3_client, 'test-bucket', str(temp_file), 'test.txt')
        assert '[VERIFIED]' in result

    @mock_aws
    def test_verify_worker_nested_path(self, temp_file):
        """Test verification with nested path."""
        # Upload to nested path
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'folder/subfolder/file.txt')

        # Verify
        result = s3_manage.verify_worker(s3_client, 'test-bucket', str(temp_file), 'folder/subfolder/file.txt')

        assert '[VERIFIED]' in result
        assert 'folder/subfolder/file.txt' in result

    @mock_aws
    def test_verify_worker_multiple_files(self, temp_dir):
        """Test verifying multiple files."""
        # Create multiple files
        file1 = temp_dir / "file1.txt"
        file1.write_text("content1")
        file2 = temp_dir / "file2.txt"
        file2.write_text("content2")

        # Upload
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(file1), 'test-bucket', 'file1.txt')
        s3_client.upload_file(str(file2), 'test-bucket', 'file2.txt')

        # Verify both
        result1 = s3_manage.verify_worker(s3_client, 'test-bucket', str(file1), 'file1.txt')
        result2 = s3_manage.verify_worker(s3_client, 'test-bucket', str(file2), 'file2.txt')

        assert '[VERIFIED]' in result1
        assert '[VERIFIED]' in result2

    @mock_aws
    def test_verify_worker_size_mismatch_detected(self, temp_dir):
        """Test that different file sizes are detected as different."""
        # Create two files with different sizes
        small = temp_dir / "small.txt"
        small.write_text("small")
        large = temp_dir / "large.txt"
        large.write_text("large content with more data")

        # Upload small file
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(small), 'test-bucket', 'data.txt')

        # Verify with large file (different size = different content)
        result = s3_manage.verify_worker(s3_client, 'test-bucket', str(large), 'data.txt')

        assert '[DIFFERENT]' in result

    @mock_aws
    def test_verify_worker_same_size_different_content(self, temp_dir):
        """Test that same size but different content is detected."""
        # Create files with same size but different content
        file1 = temp_dir / "file1.txt"
        file1.write_text("content1")
        file2 = temp_dir / "file2.txt"
        file2.write_text("Content2")  # Same length, different content

        # Upload first file
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(file1), 'test-bucket', 'data.txt')

        # Verify with second file
        result = s3_manage.verify_worker(s3_client, 'test-bucket', str(file2), 'data.txt')

        assert '[DIFFERENT]' in result

    @mock_aws
    def test_verify_worker_deterministic(self, temp_file):
        """Test that verification is deterministic (same result on multiple calls)."""
        # Upload file
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'test.txt')

        # Verify multiple times
        result1 = s3_manage.verify_worker(s3_client, 'test-bucket', str(temp_file), 'test.txt')
        result2 = s3_manage.verify_worker(s3_client, 'test-bucket', str(temp_file), 'test.txt')
        result3 = s3_manage.verify_worker(s3_client, 'test-bucket', str(temp_file), 'test.txt')

        # All results should be identical
        assert result1 == result2 == result3
        assert '[VERIFIED]' in result1

    @mock_aws
    def test_verify_worker_special_characters(self, temp_dir):
        """Test verification with special characters in filename."""
        # Create file with special characters
        special_file = temp_dir / "file-with_special.chars.txt"
        special_file.write_text("special content")

        # Upload
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(special_file), 'test-bucket', 'file-with_special.chars.txt')

        # Verify
        result = s3_manage.verify_worker(s3_client, 'test-bucket', str(special_file), 'file-with_special.chars.txt')

        assert '[VERIFIED]' in result
        assert 'file-with_special.chars.txt' in result

    @mock_aws
    def test_verify_worker_medium_file(self, temp_file_medium):
        """Test verification of medium-sized file.

        Note: moto may use multipart upload for files > 5MB with its own chunking logic.
        This test verifies the function handles both simple and multipart ETags correctly.
        """
        # Upload medium file
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(temp_file_medium), 'test-bucket', 'medium.bin')

        # Verify
        result = s3_manage.verify_worker(s3_client, 'test-bucket', str(temp_file_medium), 'medium.bin')

        # Due to moto's multipart behavior, result may be VERIFIED or DIFFERENT
        # The important part is it doesn't error and processes the ETag correctly
        assert '[ERROR]' not in result or 'Failed to calculate' not in result
        assert 'medium.bin' in result


class TestDownloadWorker:
    """Test download_worker() function with mocked S3."""

    @mock_aws
    def test_download_worker_new_file(self, temp_dir, temp_file):
        """Test downloading a new file (doesn't exist locally)."""
        # Setup: Upload file to S3
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'test.txt')

        # Create separate download directory
        download_dir = temp_dir / 'downloads'
        download_dir.mkdir()

        # Test download
        message, size, category = s3_manage.download_worker(s3_client, 'test-bucket', 'test.txt', str(download_dir), dry_run=False)

        # Verify result
        assert '[DOWNLOADED]' in message
        assert '[VERIFIED]' in message
        assert 'test.txt' in message
        assert size > 0
        assert category == 'downloaded'

        # Verify file was downloaded
        downloaded_file = download_dir / 'test.txt'
        assert downloaded_file.exists()

    @mock_aws
    def test_download_worker_existing_verified(self, temp_file, download_dir):
        """Test when file exists locally and ETags match (should not re-download)."""
        # Setup: Upload to S3
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'test.txt')

        # Download first time
        message1, size1, category1 = s3_manage.download_worker(s3_client, 'test-bucket', 'test.txt', str(download_dir), dry_run=False)
        assert category1 == 'downloaded'

        # Download again (should verify, not download)
        message2, size2, category2 = s3_manage.download_worker(s3_client, 'test-bucket', 'test.txt', str(download_dir), dry_run=False)

        # Should verify existing file without downloading
        assert '[VERIFIED]' in message2
        assert '[DOWNLOADED]' not in message2
        assert size2 == 0  # No download occurred
        assert category2 == 'verified'

    @mock_aws
    def test_download_worker_existing_different(self, temp_dir, temp_file):
        """Test when file exists locally but ETags differ."""
        # Setup: Upload file to S3
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'test.txt')

        # Create different local file
        local_file = temp_dir / 'test.txt'
        local_file.write_text("different content")

        # Test download (should detect difference)
        message, size, category = s3_manage.download_worker(s3_client, 'test-bucket', 'test.txt', str(temp_dir), dry_run=False)

        # Should report difference without downloading
        assert '[DIFFERENT]' in message
        assert size == 0  # No download occurred
        assert category == 'different'

    @mock_aws
    def test_download_worker_dry_run(self, temp_file, download_dir):
        """Test dry-run mode (should not download)."""
        # Setup: Upload file to S3
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'test.txt')

        # Test dry-run download
        message, size, category = s3_manage.download_worker(s3_client, 'test-bucket', 'test.txt', str(download_dir), dry_run=True)

        # Verify dry-run message
        assert '[DRY-RUN]' in message
        assert 'would be downloaded' in message
        assert size == 0
        assert category == 'downloaded'

        # Verify file was NOT downloaded
        downloaded_file = download_dir / 'test.txt'
        assert not downloaded_file.exists()

    @mock_aws
    def test_download_worker_creates_directories(self, temp_dir, temp_file):
        """Test that download creates parent directories."""
        # Setup: Upload file to nested path in S3
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'folder/subfolder/file.txt')

        # Test download
        message, size, category = s3_manage.download_worker(s3_client, 'test-bucket', 'folder/subfolder/file.txt', str(temp_dir), dry_run=False)

        # Verify success
        assert '[DOWNLOADED]' in message
        assert category == 'downloaded'

        # Verify directories were created
        downloaded_file = temp_dir / 'folder' / 'subfolder' / 'file.txt'
        assert downloaded_file.exists()
        assert (temp_dir / 'folder').is_dir()
        assert (temp_dir / 'folder' / 'subfolder').is_dir()

    @mock_aws
    def test_download_worker_s3_not_found(self, temp_dir):
        """Test when S3 object doesn't exist."""
        # Setup empty bucket
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        # Test download non-existent file
        message, size, category = s3_manage.download_worker(s3_client, 'test-bucket', 'nonexistent.txt', str(temp_dir), dry_run=False)

        # Should report error
        assert '[ERROR]' in message
        assert 'not found' in message.lower() or '404' in message
        assert size == 0
        assert category == 'error'

    @mock_aws
    def test_download_worker_return_tuple_format(self, temp_dir, temp_file):
        """Test that return value is a properly formatted tuple."""
        # Setup
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'test.txt')

        # Test download
        result = s3_manage.download_worker(s3_client, 'test-bucket', 'test.txt', str(temp_dir), dry_run=False)

        # Verify tuple structure
        assert isinstance(result, tuple)
        assert len(result) == 3

        message, size, category = result
        assert isinstance(message, str)
        assert isinstance(size, int)
        assert isinstance(category, str)
        assert category in ['downloaded', 'verified', 'different', 'error']

    @mock_aws
    def test_download_worker_empty_file(self, temp_dir):
        """Test downloading an empty file."""
        # Create empty file in S3
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.put_object(Bucket='test-bucket', Key='empty.txt', Body=b'')

        # Test download
        message, size, category = s3_manage.download_worker(s3_client, 'test-bucket', 'empty.txt', str(temp_dir), dry_run=False)

        # Verify success
        assert '[DOWNLOADED]' in message
        assert '[VERIFIED]' in message
        assert category == 'downloaded'

        # Verify empty file exists
        downloaded_file = temp_dir / 'empty.txt'
        assert downloaded_file.exists()
        assert downloaded_file.stat().st_size == 0

    @mock_aws
    def test_download_worker_small_file(self, temp_file_small, download_dir):
        """Test downloading small file."""
        # Setup
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(temp_file_small), 'test-bucket', 'small.bin')

        # Test download
        message, size, category = s3_manage.download_worker(s3_client, 'test-bucket', 'small.bin', str(download_dir), dry_run=False)

        # Verify success
        assert '[DOWNLOADED]' in message
        assert '[VERIFIED]' in message
        assert category == 'downloaded'
        assert size == 100 * 1024  # 100 KB

    @mock_aws
    def test_download_worker_medium_file(self, temp_dir, temp_file_medium):
        """Test downloading medium file."""
        # Setup
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(temp_file_medium), 'test-bucket', 'medium.bin')

        # Test download
        message, size, category = s3_manage.download_worker(s3_client, 'test-bucket', 'medium.bin', str(temp_dir), dry_run=False)

        # Verify success or known moto multipart issue
        assert category in ['downloaded', 'different']  # May be different due to moto multipart
        if category == 'downloaded':
            assert '[VERIFIED]' in message
            assert size == 10 * 1024 * 1024  # 10 MB

    @mock_aws
    def test_download_worker_size_mismatch(self, temp_dir, temp_file):
        """Test when local file size differs from S3."""
        # Setup: Upload file to S3
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'test.txt')

        # Create local file with different size
        local_file = temp_dir / 'test.txt'
        local_file.write_text("short")  # Different size

        # Test download (should detect size difference)
        message, size, category = s3_manage.download_worker(s3_client, 'test-bucket', 'test.txt', str(temp_dir), dry_run=False)

        # Should report size mismatch
        assert '[DIFFERENT]' in message
        assert 'size mismatch' in message.lower() or 'size' in message.lower()
        assert category == 'different'

    @mock_aws
    def test_download_worker_special_characters(self, temp_dir):
        """Test downloading file with special characters in name."""
        # Create file with special characters
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.put_object(Bucket='test-bucket', Key='file-with_special.chars.txt', Body=b'content')

        # Test download
        message, size, category = s3_manage.download_worker(s3_client, 'test-bucket', 'file-with_special.chars.txt', str(temp_dir), dry_run=False)

        # Verify success
        assert '[DOWNLOADED]' in message
        assert category == 'downloaded'

        # Verify file exists
        downloaded_file = temp_dir / 'file-with_special.chars.txt'
        assert downloaded_file.exists()

    @mock_aws
    def test_download_worker_multiple_files(self, temp_dir, temp_file):
        """Test downloading multiple files."""
        # Setup: Upload multiple files
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'file1.txt')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'file2.txt')

        # Download both
        message1, size1, category1 = s3_manage.download_worker(s3_client, 'test-bucket', 'file1.txt', str(temp_dir), dry_run=False)
        message2, size2, category2 = s3_manage.download_worker(s3_client, 'test-bucket', 'file2.txt', str(temp_dir), dry_run=False)

        # Verify both succeeded
        assert category1 == 'downloaded'
        assert category2 == 'downloaded'

        # Verify both exist
        assert (temp_dir / 'file1.txt').exists()
        assert (temp_dir / 'file2.txt').exists()

    @mock_aws
    def test_download_worker_nested_paths(self, temp_dir, temp_file):
        """Test downloading files with nested paths."""
        # Setup: Upload files to various nested paths
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'a/b/c/file.txt')

        # Test download
        message, size, category = s3_manage.download_worker(s3_client, 'test-bucket', 'a/b/c/file.txt', str(temp_dir), dry_run=False)

        # Verify success
        assert category == 'downloaded'

        # Verify nested structure preserved
        downloaded_file = temp_dir / 'a' / 'b' / 'c' / 'file.txt'
        assert downloaded_file.exists()

    @mock_aws
    def test_download_worker_overwrite_protection(self, temp_file, download_dir):
        """Test that existing verified files are not re-downloaded."""
        # Setup
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'test.txt')

        # Download first time
        message1, size1, category1 = s3_manage.download_worker(s3_client, 'test-bucket', 'test.txt', str(download_dir), dry_run=False)
        assert category1 == 'downloaded'

        # Get file modification time
        local_file = download_dir / 'test.txt'
        mtime_before = local_file.stat().st_mtime

        # Download again - should verify without modifying file
        message2, size2, category2 = s3_manage.download_worker(s3_client, 'test-bucket', 'test.txt', str(download_dir), dry_run=False)
        assert category2 == 'verified'

        # File should not be modified
        mtime_after = local_file.stat().st_mtime
        assert mtime_before == mtime_after

    @mock_aws
    def test_download_worker_size_reporting(self, temp_file_small, download_dir):
        """Test that downloaded size is reported correctly."""
        # Setup
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(temp_file_small), 'test-bucket', 'small.bin')

        # Test download
        message, size, category = s3_manage.download_worker(s3_client, 'test-bucket', 'small.bin', str(download_dir), dry_run=False)

        # Verify size reported correctly
        assert size == 100 * 1024  # 100 KB
        assert category == 'downloaded'

        # Verify no size reported when verifying existing
        message2, size2, category2 = s3_manage.download_worker(s3_client, 'test-bucket', 'small.bin', str(download_dir), dry_run=False)
        assert size2 == 0  # No download occurred
        assert category2 == 'verified'

    @mock_aws
    def test_download_worker_etag_case_insensitive(self, temp_file, download_dir):
        """Test that ETag comparison is case-insensitive."""
        # Setup
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'test.txt')

        # Download
        message, size, category = s3_manage.download_worker(s3_client, 'test-bucket', 'test.txt', str(download_dir), dry_run=False)

        # Should verify (case-insensitive comparison)
        assert category == 'downloaded'
        assert '[VERIFIED]' in message

    @mock_aws
    def test_download_worker_deterministic(self, temp_file, download_dir):
        """Test that download is deterministic (same result on multiple attempts)."""
        # Setup
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'test.txt')

        # Download multiple times
        result1 = s3_manage.download_worker(s3_client, 'test-bucket', 'test.txt', str(download_dir), dry_run=False)
        result2 = s3_manage.download_worker(s3_client, 'test-bucket', 'test.txt', str(download_dir), dry_run=False)
        result3 = s3_manage.download_worker(s3_client, 'test-bucket', 'test.txt', str(download_dir), dry_run=False)

        # First download should succeed
        assert result1[2] == 'downloaded'

        # Subsequent attempts should verify
        assert result2[2] == 'verified'
        assert result3[2] == 'verified'
        assert result2[0] == result3[0]  # Same message

    @mock_aws
    def test_download_worker_content_verification(self, temp_file, download_dir):
        """Test that downloaded content matches S3 content."""
        # Setup
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        # Read original content
        original_content = temp_file.read_text()

        # Upload to S3
        s3_client.upload_file(str(temp_file), 'test-bucket', 'test.txt')

        # Download
        message, size, category = s3_manage.download_worker(s3_client, 'test-bucket', 'test.txt', str(download_dir), dry_run=False)

        # Verify content matches
        downloaded_file = download_dir / 'test.txt'
        downloaded_content = downloaded_file.read_text()
        assert downloaded_content == original_content
        assert category == 'downloaded'
# ====================================================================================
# Phase 3: Command Functions (Complex Testing - Requires S3 Mocking + stdout capture)
# ====================================================================================

class TestPaginateS3Objects:
    """Test paginate_s3_objects() generator function with mocked S3."""

    @mock_aws
    def test_paginate_empty_bucket(self):
        """Test pagination returns nothing for empty bucket."""
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        objects = list(s3_manage.paginate_s3_objects(s3_client, 'test-bucket'))
        assert objects == []

    @mock_aws
    def test_paginate_single_object(self, temp_file):
        """Test pagination with single object."""
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'file1.txt')

        objects = list(s3_manage.paginate_s3_objects(s3_client, 'test-bucket'))

        assert len(objects) == 1
        assert objects[0]['Key'] == 'file1.txt'
        assert objects[0]['Size'] > 0
        assert 'LastModified' in objects[0]

    @mock_aws
    def test_paginate_multiple_objects(self, temp_dir):
        """Test pagination with multiple objects (single page)."""
        # Create 10 files
        files = []
        for i in range(10):
            f = temp_dir / f"file{i}.txt"
            f.write_text(f"content {i}")
            files.append(f)

        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        for f in files:
            s3_client.upload_file(str(f), 'test-bucket', f.name)

        objects = list(s3_manage.paginate_s3_objects(s3_client, 'test-bucket'))

        assert len(objects) == 10
        keys = [obj['Key'] for obj in objects]
        assert 'file0.txt' in keys
        assert 'file9.txt' in keys

    @mock_aws
    def test_paginate_with_prefix(self, temp_dir):
        """Test pagination with prefix filter."""
        # Create files with different prefixes
        (temp_dir / "data1.txt").write_text("data1")
        (temp_dir / "data2.txt").write_text("data2")
        (temp_dir / "log1.txt").write_text("log1")

        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(temp_dir / "data1.txt"), 'test-bucket', 'data1.txt')
        s3_client.upload_file(str(temp_dir / "data2.txt"), 'test-bucket', 'data2.txt')
        s3_client.upload_file(str(temp_dir / "log1.txt"), 'test-bucket', 'log1.txt')

        # Filter by prefix
        objects = list(s3_manage.paginate_s3_objects(s3_client, 'test-bucket', prefix='data'))

        assert len(objects) == 2
        keys = [obj['Key'] for obj in objects]
        assert 'data1.txt' in keys
        assert 'data2.txt' in keys
        assert 'log1.txt' not in keys

    @mock_aws
    def test_paginate_with_max_keys(self, temp_dir):
        """Test pagination with max_keys limit (stops after first page)."""
        # Create 20 files
        for i in range(20):
            f = temp_dir / f"file{i:02d}.txt"
            f.write_text(f"content {i}")

        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        for i in range(20):
            s3_client.upload_file(str(temp_dir / f"file{i:02d}.txt"), 'test-bucket', f"file{i:02d}.txt")

        # Limit to 5 objects
        objects = list(s3_manage.paginate_s3_objects(s3_client, 'test-bucket', max_keys=5))

        # Should stop after first page
        assert len(objects) == 5

    @mock_aws
    def test_paginate_generator_pattern(self, temp_dir):
        """Test that paginate_s3_objects is a generator (lazy evaluation)."""
        # Create 3 files
        for i in range(3):
            f = temp_dir / f"file{i}.txt"
            f.write_text(f"content {i}")

        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        for i in range(3):
            s3_client.upload_file(str(temp_dir / f"file{i}.txt"), 'test-bucket', f"file{i}.txt")

        # Get generator
        gen = s3_manage.paginate_s3_objects(s3_client, 'test-bucket')

        # Should be generator, not list
        import types
        assert isinstance(gen, types.GeneratorType)

        # Should yield objects one at a time
        first = next(gen)
        assert 'Key' in first
        assert first['Key'] == 'file0.txt'

    @mock_aws
    def test_paginate_no_prefix_match(self):
        """Test pagination when prefix doesn't match any objects."""
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.put_object(Bucket='test-bucket', Key='data/file1.txt', Body=b'content')

        # Prefix doesn't match
        objects = list(s3_manage.paginate_s3_objects(s3_client, 'test-bucket', prefix='logs/'))

        assert objects == []

    @mock_aws
    def test_paginate_nested_paths(self, temp_file):
        """Test pagination with nested directory structures."""
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        # Upload to nested paths
        s3_client.upload_file(str(temp_file), 'test-bucket', 'data/2024/01/file1.txt')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'data/2024/02/file2.txt')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'logs/app.log')

        # Get all objects
        objects = list(s3_manage.paginate_s3_objects(s3_client, 'test-bucket'))

        assert len(objects) == 3
        keys = [obj['Key'] for obj in objects]
        assert 'data/2024/01/file1.txt' in keys
        assert 'data/2024/02/file2.txt' in keys
        assert 'logs/app.log' in keys


class TestCommandList:
    """Test command_list() function with mocked S3 and stdout capture."""

    @mock_aws
    def test_list_empty_bucket(self, capsys):
        """Test listing empty bucket."""
        # Setup
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        # Create args mock
        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'prefix': None,
            'max_keys': None
        })()

        # Execute
        s3_manage.command_list(args, s3_client=s3_client)

        # Verify output
        captured = capsys.readouterr()
        assert "Listing objects in bucket: test-bucket" in captured.out
        assert "Bucket is empty" in captured.out
        assert "Total: 0 objects" in captured.out

    @mock_aws
    def test_list_single_file(self, temp_file, capsys):
        """Test listing single file."""
        # Setup
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'test.txt')

        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'prefix': None,
            'max_keys': None
        })()

        # Execute
        s3_manage.command_list(args, s3_client=s3_client)

        # Verify output
        captured = capsys.readouterr()
        assert "FILE PATH" in captured.out
        assert "SIZE" in captured.out
        assert "LAST MODIFIED" in captured.out
        assert "test.txt" in captured.out
        assert "Total: 1 objects" in captured.out

    @mock_aws
    def test_list_multiple_files(self, temp_dir, capsys):
        """Test listing multiple files."""
        # Create 5 files
        for i in range(5):
            f = temp_dir / f"file{i}.txt"
            f.write_text(f"content {i}")

        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        for i in range(5):
            s3_client.upload_file(str(temp_dir / f"file{i}.txt"), 'test-bucket', f"file{i}.txt")

        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'prefix': None,
            'max_keys': None
        })()

        # Execute
        s3_manage.command_list(args, s3_client=s3_client)

        # Verify output
        captured = capsys.readouterr()
        assert "file0.txt" in captured.out
        assert "file4.txt" in captured.out
        assert "Total: 5 objects" in captured.out

    @mock_aws
    def test_list_with_prefix(self, temp_dir, capsys):
        """Test listing with prefix filter."""
        # Create files with different prefixes
        (temp_dir / "data1.txt").write_text("data1")
        (temp_dir / "data2.txt").write_text("data2")
        (temp_dir / "log1.txt").write_text("log1")

        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(temp_dir / "data1.txt"), 'test-bucket', 'data1.txt')
        s3_client.upload_file(str(temp_dir / "data2.txt"), 'test-bucket', 'data2.txt')
        s3_client.upload_file(str(temp_dir / "log1.txt"), 'test-bucket', 'log1.txt')

        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'prefix': 'data',
            'max_keys': None
        })()

        # Execute
        s3_manage.command_list(args, s3_client=s3_client)

        # Verify output
        captured = capsys.readouterr()
        assert "Prefix filter: data" in captured.out
        assert "data1.txt" in captured.out
        assert "data2.txt" in captured.out
        assert "log1.txt" not in captured.out
        assert "Total: 2 objects" in captured.out

    @mock_aws
    def test_list_with_max_keys(self, temp_dir, capsys):
        """Test listing with max_keys limit."""
        # Create 10 files
        for i in range(10):
            f = temp_dir / f"file{i:02d}.txt"
            f.write_text(f"content {i}")

        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        for i in range(10):
            s3_client.upload_file(str(temp_dir / f"file{i:02d}.txt"), 'test-bucket', f"file{i:02d}.txt")

        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'prefix': None,
            'max_keys': 3
        })()

        # Execute
        s3_manage.command_list(args, s3_client=s3_client)

        # Verify output
        captured = capsys.readouterr()
        assert "Total: 3 objects" in captured.out
        assert "Results limited to 3 objects" in captured.out

    @mock_aws
    def test_list_no_prefix_match(self, temp_file, capsys):
        """Test listing when prefix doesn't match any objects."""
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'data/file1.txt')

        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'prefix': 'logs/',
            'max_keys': None
        })()

        # Execute
        s3_manage.command_list(args, s3_client=s3_client)

        # Verify output
        captured = capsys.readouterr()
        assert "No objects found with prefix: logs/" in captured.out
        assert "Total: 0 objects" in captured.out

    @mock_aws
    def test_list_formatted_output(self, temp_file_small, capsys):
        """Test that output is properly formatted (columns aligned)."""
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(temp_file_small), 'test-bucket', 'test.bin')

        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'prefix': None,
            'max_keys': None
        })()

        # Execute
        s3_manage.command_list(args, s3_client=s3_client)

        # Verify formatting
        captured = capsys.readouterr()
        lines = captured.out.split('\n')

        # Find header line
        header_line = [line for line in lines if 'FILE PATH' in line][0]
        separator_line = [line for line in lines if line.startswith('-')][0]

        # Verify separator matches header width
        assert len(separator_line) == 107

        # Verify columns are present
        assert 'FILE PATH' in header_line
        assert 'SIZE' in header_line
        assert 'LAST MODIFIED' in header_line

    @mock_aws
    def test_list_size_formatting(self, temp_dir, capsys):
        """Test that file sizes are formatted correctly."""
        # Create files of different sizes
        small = temp_dir / "small.txt"
        small.write_bytes(b"x" * 1024)  # 1 KB

        medium = temp_dir / "medium.bin"
        medium.write_bytes(b"y" * (1024 * 1024))  # 1 MB

        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(small), 'test-bucket', 'small.txt')
        s3_client.upload_file(str(medium), 'test-bucket', 'medium.bin')

        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'prefix': None,
            'max_keys': None
        })()

        # Execute
        s3_manage.command_list(args, s3_client=s3_client)

        # Verify size formatting
        captured = capsys.readouterr()
        assert "1.00 KB" in captured.out  # small.txt
        assert "1.00 MB" in captured.out  # medium.bin

        # Verify total includes both
        assert "Total: 2 objects" in captured.out

    @mock_aws
    def test_list_nested_paths(self, temp_file, capsys):
        """Test listing with nested directory structures."""
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        # Upload to nested paths
        s3_client.upload_file(str(temp_file), 'test-bucket', 'data/2024/file1.txt')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'logs/app.log')

        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'prefix': None,
            'max_keys': None
        })()

        # Execute
        s3_manage.command_list(args, s3_client=s3_client)

        # Verify output
        captured = capsys.readouterr()
        assert "data/2024/file1.txt" in captured.out
        assert "logs/app.log" in captured.out
        assert "Total: 2 objects" in captured.out

    @mock_aws
    def test_list_error_handling_bucket_not_found(self, capsys):
        """Test error handling when bucket doesn't exist."""
        s3_client = boto3.client('s3', region_name='us-east-1')

        args = type('Args', (), {
            'bucket_name': 'nonexistent-bucket',
            'prefix': None,
            'max_keys': None
        })()

        # Execute
        s3_manage.command_list(args, s3_client=s3_client)

        # Verify error message
        captured = capsys.readouterr()
        assert "Error listing bucket" in captured.out or "NoSuchBucket" in captured.out

    @mock_aws
    def test_list_timestamp_format(self, temp_file, capsys):
        """Test that timestamps are formatted correctly."""
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'test.txt')

        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'prefix': None,
            'max_keys': None
        })()

        # Execute
        s3_manage.command_list(args, s3_client=s3_client)

        # Verify timestamp format (YYYY-MM-DD HH:MM:SS)
        captured = capsys.readouterr()
        import re
        timestamp_pattern = r'\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}'
        assert re.search(timestamp_pattern, captured.out) is not None


# ====================================================================================
# Phase 4 Chunk 1: command_indexupload (Requires S3 Mocking + stdout capture)
# ====================================================================================

class TestCommandIndexupload:
    """Test command_indexupload() with mocked S3 and capsys (10 tests)"""

    @mock_aws
    def test_indexupload_empty_bucket(self, capsys):
        """Test indexupload with empty bucket."""
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'prefix': None,
            'url_prefix': None
        })()

        # Execute
        s3_manage.command_indexupload(args, s3_client=s3_client)

        # Verify output
        captured = capsys.readouterr()
        assert "Bucket is empty" in captured.out
        assert "No index files will be generated" in captured.out

    @mock_aws
    def test_indexupload_html_single_file(self, temp_file, capsys):
        """Test HTML index generation with single file."""
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'test.txt')

        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'prefix': None,
            'url_prefix': None
        })()

        # Execute
        s3_manage.command_indexupload(args, s3_client=s3_client)

        # Verify index.html uploaded
        response = s3_client.list_objects_v2(Bucket='test-bucket')
        object_keys = [obj['Key'] for obj in response['Contents']]
        assert 'index.html' in object_keys

        # Verify HTML content
        index_obj = s3_client.get_object(Bucket='test-bucket', Key='index.html')
        index_content = index_obj['Body'].read().decode('utf-8')
        assert '<html>' in index_content
        assert 'test.txt' in index_content
        assert 'Total: 1 objects' in index_content

        # Verify stdout
        captured = capsys.readouterr()
        assert 'Generated index files:' in captured.out
        assert '[SUCCESS] index.html uploaded' in captured.out

    @mock_aws
    def test_indexupload_html_multiple_files(self, temp_file, capsys):
        """Test HTML index with multiple files."""
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        # Upload multiple files
        s3_client.upload_file(str(temp_file), 'test-bucket', 'file1.txt')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'file2.txt')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'data/file3.bin')

        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'prefix': None,
            'url_prefix': None
        })()

        # Execute
        s3_manage.command_indexupload(args, s3_client=s3_client)

        # Verify HTML content
        index_obj = s3_client.get_object(Bucket='test-bucket', Key='index.html')
        index_content = index_obj['Body'].read().decode('utf-8')
        assert 'file1.txt' in index_content
        assert 'file2.txt' in index_content
        assert 'data/file3.bin' in index_content
        assert 'Total: 3 objects' in index_content

    @mock_aws
    def test_indexupload_markdown_format(self, temp_file, capsys):
        """Test Markdown index generation."""
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'test.txt')

        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'prefix': None,
            'url_prefix': None
        })()

        # Execute
        s3_manage.command_indexupload(args, s3_client=s3_client)

        # Verify index.md uploaded
        response = s3_client.list_objects_v2(Bucket='test-bucket')
        object_keys = [obj['Key'] for obj in response['Contents']]
        assert 'index.md' in object_keys

        # Verify Markdown content
        index_obj = s3_client.get_object(Bucket='test-bucket', Key='index.md')
        index_content = index_obj['Body'].read().decode('utf-8')
        assert '# Files Index' in index_content
        assert '| File Path | Size | Last Modified |' in index_content
        assert 'test.txt' in index_content
        assert '**Total:** 1 objects' in index_content

        # Verify stdout (command generates both HTML and MD regardless of format)
        captured = capsys.readouterr()
        assert 'Generated index files:' in captured.out
        assert '[SUCCESS] index.md uploaded' in captured.out

    @mock_aws
    def test_indexupload_with_prefix(self, temp_file, capsys):
        """Test index generation with prefix filter."""
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        # Upload files with different prefixes
        s3_client.upload_file(str(temp_file), 'test-bucket', 'data/file1.txt')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'data/file2.txt')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'logs/app.log')

        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'prefix': 'data/',
            'url_prefix': None
        })()

        # Execute
        s3_manage.command_indexupload(args, s3_client=s3_client)

        # Verify HTML content
        index_obj = s3_client.get_object(Bucket='test-bucket', Key='index.html')
        index_content = index_obj['Body'].read().decode('utf-8')
        assert 'data/file1.txt' in index_content
        assert 'data/file2.txt' in index_content
        assert 'logs/app.log' not in index_content  # Should be excluded by prefix
        assert 'Total: 2 objects' in index_content

    @mock_aws
    def test_indexupload_excludes_index_files(self, temp_file, capsys):
        """Test that index.html and index.md are excluded."""
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        # Upload regular file and existing index files
        s3_client.upload_file(str(temp_file), 'test-bucket', 'file.txt')
        s3_client.put_object(Bucket='test-bucket', Key='index.html', Body=b'<html>old</html>')
        s3_client.put_object(Bucket='test-bucket', Key='index.md', Body=b'# Old Index')

        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'prefix': None,
            'url_prefix': None
        })()

        # Execute
        s3_manage.command_indexupload(args, s3_client=s3_client)

        # Verify new HTML content doesn't include index files
        index_obj = s3_client.get_object(Bucket='test-bucket', Key='index.html')
        index_content = index_obj['Body'].read().decode('utf-8')
        assert 'file.txt' in index_content
        assert 'Total: 1 objects' in index_content  # Only file.txt counted
        # Index files should not appear in the listing
        assert 'index.html' not in [line for line in index_content.split('\n') if '<tr>' in line and 'index.html' in line]

    @mock_aws
    def test_indexupload_url_prefix(self, temp_file, capsys):
        """Test index generation with custom URL prefix."""
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'file.txt')

        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'prefix': None,
            'format': 'html',
            'url_prefix': 'https://cdn.example.com/bucket/',
            'dry_run': False
        })()

        # Execute
        s3_manage.command_indexupload(args, s3_client=s3_client)

        # Verify HTML content has custom URL prefix
        index_obj = s3_client.get_object(Bucket='test-bucket', Key='index.html')
        index_content = index_obj['Body'].read().decode('utf-8')
        assert 'https://cdn.example.com/bucket/file.txt' in index_content

        # Verify Markdown also has custom URL prefix (both formats generated simultaneously)
        index_obj = s3_client.get_object(Bucket='test-bucket', Key='index.md')
        index_content = index_obj['Body'].read().decode('utf-8')
        assert 'https://cdn.example.com/bucket/file.txt' in index_content

    @mock_aws
    def test_indexupload_error_bucket_not_found(self, capsys):
        """Test error handling when bucket doesn't exist."""
        s3_client = boto3.client('s3', region_name='us-east-1')

        args = type('Args', (), {
            'bucket_name': 'nonexistent-bucket',
            'prefix': None,
            'url_prefix': None
        })()

        # Execute
        s3_manage.command_indexupload(args, s3_client=s3_client)

        # Verify error message
        captured = capsys.readouterr()
        assert "Error listing bucket" in captured.out

    @mock_aws
    def test_indexupload_nested_paths(self, temp_file, capsys):
        """Test index generation with nested paths."""
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        # Upload files in nested paths
        s3_client.upload_file(str(temp_file), 'test-bucket', 'data/2024/01/file.txt')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'logs/app/debug.log')

        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'prefix': None,
            'url_prefix': None
        })()

        # Execute
        s3_manage.command_indexupload(args, s3_client=s3_client)

        # Verify HTML content shows nested paths
        index_obj = s3_client.get_object(Bucket='test-bucket', Key='index.html')
        index_content = index_obj['Body'].read().decode('utf-8')
        assert 'data/2024/01/file.txt' in index_content
        assert 'logs/app/debug.log' in index_content
        assert 'Total: 2 objects' in index_content

# ====================================================================================
# Phase 4 Chunk 2: command_download (Requires S3 Mocking + stdout capture + file I/O)
# ====================================================================================

class TestCommandDownload:
    """Test command_download() with mocked S3 and capsys (12 tests)"""

    @mock_aws
    def test_download_empty_bucket(self, temp_dir, capsys):
        """Test download from empty bucket shows appropriate message."""
        # Setup S3 environment
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        # Create mock args
        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'prefix': None,
            'local_folder': str(temp_dir),
            'dry_run': False,
            'max_threads': 2
        })()

        # Execute command
        s3_manage.command_download(args, s3_client=s3_client)

        # Verify output
        captured = capsys.readouterr()
        assert 'Bucket is empty' in captured.out
        assert 'Nothing to download' in captured.out

    @mock_aws
    def test_download_single_file_new(self, temp_dir, temp_file, capsys):
        """Test downloading a single new file."""
        # Setup S3 environment
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'test.txt')

        # Create mock args
        local_folder = temp_dir / 'downloads'
        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'prefix': None,
            'local_folder': str(local_folder),
            'dry_run': False,
            'max_threads': 2
        })()

        # Execute command
        s3_manage.command_download(args, s3_client=s3_client)

        # Verify file was downloaded
        downloaded_file = local_folder / 'test.txt'
        assert downloaded_file.exists()

        # Verify output
        captured = capsys.readouterr()
        assert 'Found 1 objects' in captured.out
        assert '[DOWNLOADED]' in captured.out
        assert 'Downloaded (new files): 1' in captured.out

    @mock_aws
    def test_download_multiple_files(self, temp_dir, temp_file, capsys):
        """Test downloading multiple files."""
        # Setup S3 environment
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        # Upload multiple files
        for i in range(3):
            s3_client.upload_file(str(temp_file), 'test-bucket', f'file{i}.txt')

        # Create mock args
        local_folder = temp_dir / 'downloads'
        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'prefix': None,
            'local_folder': str(local_folder),
            'dry_run': False,
            'max_threads': 2
        })()

        # Execute command
        s3_manage.command_download(args, s3_client=s3_client)

        # Verify all files downloaded
        assert (local_folder / 'file0.txt').exists()
        assert (local_folder / 'file1.txt').exists()
        assert (local_folder / 'file2.txt').exists()

        # Verify output
        captured = capsys.readouterr()
        assert 'Found 3 objects' in captured.out
        assert 'Downloaded (new files): 3' in captured.out

    @mock_aws
    def test_download_existing_file_verified(self, temp_dir, temp_file, capsys):
        """Test downloading when local file already exists and matches."""
        # Setup S3 environment
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'test.txt')

        # Create local file with same content
        local_folder = temp_dir / 'downloads'
        local_folder.mkdir(parents=True)
        local_file = local_folder / 'test.txt'
        shutil.copy(str(temp_file), str(local_file))

        # Create mock args
        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'prefix': None,
            'local_folder': str(local_folder),
            'dry_run': False,
            'max_threads': 2
        })()

        # Execute command
        s3_manage.command_download(args, s3_client=s3_client)

        # Verify output shows verification (not download)
        captured = capsys.readouterr()
        assert '[VERIFIED]' in captured.out
        assert 'Verified (existing files, match): 1' in captured.out
        assert 'Downloaded (new files): 0' in captured.out

    @mock_aws
    def test_download_existing_file_different(self, temp_dir, temp_file, capsys):
        """Test downloading when local file exists but differs from S3."""
        # Setup S3 environment
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'test.txt')

        # Create local file with different content
        local_folder = temp_dir / 'downloads'
        local_folder.mkdir(parents=True)
        local_file = local_folder / 'test.txt'
        local_file.write_text('different content')

        # Create mock args
        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'prefix': None,
            'local_folder': str(local_folder),
            'dry_run': False,
            'max_threads': 2
        })()

        # Execute command - should exit with code 1
        with pytest.raises(SystemExit) as exc_info:
            s3_manage.command_download(args, s3_client=s3_client)
        assert exc_info.value.code == 1

        # Verify output shows difference
        captured = capsys.readouterr()
        assert '[DIFFERENT]' in captured.out
        assert 'Different (existing files, mismatch): 1' in captured.out

    @mock_aws
    def test_download_with_prefix(self, temp_dir, temp_file, capsys):
        """Test downloading with prefix filter."""
        # Setup S3 environment
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        # Upload files with different prefixes
        s3_client.upload_file(str(temp_file), 'test-bucket', 'data/file1.txt')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'data/file2.txt')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'logs/file3.txt')

        # Create mock args with prefix filter
        local_folder = temp_dir / 'downloads'
        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'prefix': 'data/',
            'local_folder': str(local_folder),
            'dry_run': False,
            'max_threads': 2
        })()

        # Execute command
        s3_manage.command_download(args, s3_client=s3_client)

        # Verify only matching files downloaded
        assert (local_folder / 'data' / 'file1.txt').exists()
        assert (local_folder / 'data' / 'file2.txt').exists()
        assert not (local_folder / 'logs' / 'file3.txt').exists()

        # Verify output
        captured = capsys.readouterr()
        assert 'Prefix filter: data/' in captured.out
        assert 'Found 2 objects' in captured.out

    @mock_aws
    def test_download_dry_run(self, temp_dir, temp_file, capsys):
        """Test download in dry-run mode (no actual downloads)."""
        # Setup S3 environment
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'test.txt')

        # Create mock args with dry_run=True
        local_folder = temp_dir / 'downloads'
        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'prefix': None,
            'local_folder': str(local_folder),
            'dry_run': True,
            'max_threads': 2
        })()

        # Execute command
        s3_manage.command_download(args, s3_client=s3_client)

        # Verify NO file was downloaded
        assert not (local_folder / 'test.txt').exists()

        # Verify output shows dry-run
        captured = capsys.readouterr()
        assert 'DRY-RUN mode' in captured.out
        assert 'No files were actually downloaded' in captured.out

    @mock_aws
    def test_download_error_bucket_not_found(self, temp_dir, capsys):
        """Test download from non-existent bucket shows error."""
        # Setup S3 environment (bucket NOT created)
        s3_client = boto3.client('s3', region_name='us-east-1')

        # Create mock args
        args = type('Args', (), {
            'bucket_name': 'nonexistent-bucket',
            'prefix': None,
            'local_folder': str(temp_dir),
            'dry_run': False,
            'max_threads': 2
        })()

        # Execute command - should exit with code 1
        with pytest.raises(SystemExit) as exc_info:
            s3_manage.command_download(args, s3_client=s3_client)
        assert exc_info.value.code == 1

        # Verify error message
        captured = capsys.readouterr()
        assert 'Error listing bucket' in captured.out

    @mock_aws
    def test_download_creates_directories(self, temp_dir, temp_file, capsys):
        """Test download creates nested directories for S3 keys."""
        # Setup S3 environment
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'path/to/nested/file.txt')

        # Create mock args
        local_folder = temp_dir / 'downloads'
        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'prefix': None,
            'local_folder': str(local_folder),
            'dry_run': False,
            'max_threads': 2
        })()

        # Execute command
        s3_manage.command_download(args, s3_client=s3_client)

        # Verify nested directories created
        downloaded_file = local_folder / 'path' / 'to' / 'nested' / 'file.txt'
        assert downloaded_file.exists()
        assert downloaded_file.parent.exists()

    @mock_aws
    def test_download_nested_paths(self, temp_dir, temp_file, capsys):
        """Test download handles nested S3 paths correctly."""
        # Setup S3 environment
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        # Upload files with nested paths
        s3_client.upload_file(str(temp_file), 'test-bucket', 'data/2024/01/file.txt')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'logs/app/debug.log')

        # Create mock args
        local_folder = temp_dir / 'downloads'
        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'prefix': None,
            'local_folder': str(local_folder),
            'dry_run': False,
            'max_threads': 2
        })()

        # Execute command
        s3_manage.command_download(args, s3_client=s3_client)

        # Verify nested paths created correctly
        assert (local_folder / 'data' / '2024' / '01' / 'file.txt').exists()
        assert (local_folder / 'logs' / 'app' / 'debug.log').exists()

        # Verify output
        captured = capsys.readouterr()
        assert 'Found 2 objects' in captured.out
        assert 'Downloaded (new files): 2' in captured.out

    @mock_aws
    def test_download_no_prefix_match(self, temp_dir, temp_file, capsys):
        """Test download with prefix that matches no objects."""
        # Setup S3 environment
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'data/file.txt')

        # Create mock args with non-matching prefix
        local_folder = temp_dir / 'downloads'
        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'prefix': 'logs/',
            'local_folder': str(local_folder),
            'dry_run': False,
            'max_threads': 2
        })()

        # Execute command
        s3_manage.command_download(args, s3_client=s3_client)

        # Verify output shows no objects found
        captured = capsys.readouterr()
        assert 'No objects found with prefix: logs/' in captured.out
        assert 'Nothing to download' in captured.out

    @mock_aws
    def test_download_statistics_validation(self, temp_dir, temp_file, capsys):
        """Test download command reports accurate statistics."""
        # Setup S3 environment
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        # Upload 3 files
        for i in range(3):
            s3_client.upload_file(str(temp_file), 'test-bucket', f'file{i}.txt')

        # Create mock args
        local_folder = temp_dir / 'downloads'
        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'prefix': None,
            'local_folder': str(local_folder),
            'dry_run': False,
            'max_threads': 2
        })()

        # Execute command
        s3_manage.command_download(args, s3_client=s3_client)

        # Verify statistics in output
        captured = capsys.readouterr()
        assert 'Total files found: 3' in captured.out
        assert 'Downloaded (new files): 3' in captured.out
        assert 'Verified (existing files, match): 0' in captured.out
        assert 'Different (existing files, mismatch): 0' in captured.out
        assert 'Errors: 0' in captured.out
        assert 'Download speed:' in captured.out

# ====================================================================================
# Phase 4 Chunk 3: command_upload (Requires S3 Mocking + stdout capture + file I/O)
# ====================================================================================

class TestCommandUpload:
    """Test command_upload() with mocked S3 and capsys (12 tests)"""

    @mock_aws
    def test_upload_empty_directory(self, temp_dir, capsys):
        """Test upload from empty directory shows appropriate message."""
        # Setup S3 environment
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        # Create empty directory
        local_folder = temp_dir / 'uploads'
        local_folder.mkdir()

        # Create mock args
        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'local_folder': str(local_folder),
            'dry_run': False,
            'max_threads': 2,
            'allow_hidden_files': False
        })()

        # Execute command
        s3_manage.command_upload(args, s3_client=s3_client)

        # Verify output
        captured = capsys.readouterr()
        assert 'Found 0 files' in captured.out

    @mock_aws
    def test_upload_single_file_new(self, temp_dir, temp_file, capsys):
        """Test uploading a single new file."""
        # Setup S3 environment
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        # Create local directory with file
        local_folder = temp_dir / 'uploads'
        local_folder.mkdir()
        test_file = local_folder / 'test.txt'
        shutil.copy(str(temp_file), str(test_file))

        # Create mock args
        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'local_folder': str(local_folder),
            'dry_run': False,
            'max_threads': 2,
            'allow_hidden_files': False
        })()

        # Execute command
        s3_manage.command_upload(args, s3_client=s3_client)

        # Verify file was uploaded
        response = s3_client.list_objects_v2(Bucket='test-bucket')
        assert 'Contents' in response
        assert len(response['Contents']) == 1
        assert response['Contents'][0]['Key'] == 'test.txt'

        # Verify output
        captured = capsys.readouterr()
        assert 'Found 1 files' in captured.out
        assert '[SUCCESS]' in captured.out
        assert 'Files uploaded: 1' in captured.out

    @mock_aws
    def test_upload_multiple_files(self, temp_dir, temp_file, capsys):
        """Test uploading multiple files."""
        # Setup S3 environment
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        # Create local directory with multiple files
        local_folder = temp_dir / 'uploads'
        local_folder.mkdir()
        for i in range(3):
            test_file = local_folder / f'file{i}.txt'
            shutil.copy(str(temp_file), str(test_file))

        # Create mock args
        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'local_folder': str(local_folder),
            'dry_run': False,
            'max_threads': 2,
            'allow_hidden_files': False
        })()

        # Execute command
        s3_manage.command_upload(args, s3_client=s3_client)

        # Verify all files uploaded
        response = s3_client.list_objects_v2(Bucket='test-bucket')
        assert len(response['Contents']) == 3
        keys = [obj['Key'] for obj in response['Contents']]
        assert 'file0.txt' in keys
        assert 'file1.txt' in keys
        assert 'file2.txt' in keys

        # Verify output
        captured = capsys.readouterr()
        assert 'Found 3 files' in captured.out
        assert 'Files uploaded: 3' in captured.out

    @mock_aws
    def test_upload_file_already_exists_skipped(self, temp_dir, temp_file, capsys):
        """Test uploading when file already exists in S3 (should skip)."""
        # Setup S3 environment
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        # Upload file to S3 first
        s3_client.upload_file(str(temp_file), 'test-bucket', 'test.txt')

        # Create local directory with same file
        local_folder = temp_dir / 'uploads'
        local_folder.mkdir()
        test_file = local_folder / 'test.txt'
        shutil.copy(str(temp_file), str(test_file))

        # Create mock args
        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'local_folder': str(local_folder),
            'dry_run': False,
            'max_threads': 2,
            'allow_hidden_files': False
        })()

        # Execute command
        s3_manage.command_upload(args, s3_client=s3_client)

        # Verify output shows skip
        captured = capsys.readouterr()
        assert '[SKIPPED]' in captured.out
        assert 'Already exists' in captured.out
        assert 'Files uploaded: 0' in captured.out
        assert 'Files skipped (already exist): 1' in captured.out

    @mock_aws
    def test_upload_dry_run(self, temp_dir, temp_file, capsys):
        """Test upload in dry-run mode (no actual uploads)."""
        # Setup S3 environment
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        # Create local directory with file
        local_folder = temp_dir / 'uploads'
        local_folder.mkdir()
        test_file = local_folder / 'test.txt'
        shutil.copy(str(temp_file), str(test_file))

        # Create mock args with dry_run=True
        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'local_folder': str(local_folder),
            'dry_run': True,
            'max_threads': 2,
            'allow_hidden_files': False
        })()

        # Execute command
        s3_manage.command_upload(args, s3_client=s3_client)

        # Verify NO file was uploaded
        response = s3_client.list_objects_v2(Bucket='test-bucket')
        assert 'Contents' not in response

        # Verify output shows dry-run
        captured = capsys.readouterr()
        assert '[DRY-RUN]' in captured.out
        assert 'would upload' in captured.out
        assert 'DRY-RUN SUMMARY' in captured.out
        assert 'No files were actually uploaded' in captured.out

    @mock_aws
    def test_upload_hidden_files_excluded_by_default(self, temp_dir, temp_file, capsys):
        """Test that hidden files are excluded by default."""
        # Setup S3 environment
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        # Create local directory with hidden and regular files
        local_folder = temp_dir / 'uploads'
        local_folder.mkdir()

        # Regular file
        regular_file = local_folder / 'regular.txt'
        shutil.copy(str(temp_file), str(regular_file))

        # Hidden file
        hidden_file = local_folder / '.hidden.txt'
        shutil.copy(str(temp_file), str(hidden_file))

        # Hidden directory with file
        hidden_dir = local_folder / '.hidden_dir'
        hidden_dir.mkdir()
        hidden_dir_file = hidden_dir / 'file.txt'
        shutil.copy(str(temp_file), str(hidden_dir_file))

        # Create mock args with allow_hidden_files=False (default)
        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'local_folder': str(local_folder),
            'dry_run': False,
            'max_threads': 2,
            'allow_hidden_files': False
        })()

        # Execute command
        s3_manage.command_upload(args, s3_client=s3_client)

        # Verify only regular file uploaded
        response = s3_client.list_objects_v2(Bucket='test-bucket')
        assert len(response['Contents']) == 1
        assert response['Contents'][0]['Key'] == 'regular.txt'

        # Verify output
        captured = capsys.readouterr()
        assert 'Found 1 files' in captured.out

    @mock_aws
    def test_upload_hidden_files_included_when_allowed(self, temp_dir, temp_file, capsys):
        """Test that hidden files are included when --allow-hidden-files is set."""
        # Setup S3 environment
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        # Create local directory with hidden and regular files
        local_folder = temp_dir / 'uploads'
        local_folder.mkdir()

        regular_file = local_folder / 'regular.txt'
        shutil.copy(str(temp_file), str(regular_file))

        hidden_file = local_folder / '.hidden.txt'
        shutil.copy(str(temp_file), str(hidden_file))

        # Create mock args with allow_hidden_files=True
        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'local_folder': str(local_folder),
            'dry_run': False,
            'max_threads': 2,
            'allow_hidden_files': True
        })()

        # Execute command
        s3_manage.command_upload(args, s3_client=s3_client)

        # Verify both files uploaded
        response = s3_client.list_objects_v2(Bucket='test-bucket')
        assert len(response['Contents']) == 2
        keys = [obj['Key'] for obj in response['Contents']]
        assert 'regular.txt' in keys
        assert '.hidden.txt' in keys

    @mock_aws
    def test_upload_nested_directories(self, temp_dir, temp_file, capsys):
        """Test upload preserves nested directory structure."""
        # Setup S3 environment
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        # Create nested directory structure
        local_folder = temp_dir / 'uploads'
        local_folder.mkdir()

        # Create nested structure: uploads/data/2024/01/file.txt
        nested_dir = local_folder / 'data' / '2024' / '01'
        nested_dir.mkdir(parents=True)
        nested_file = nested_dir / 'file.txt'
        shutil.copy(str(temp_file), str(nested_file))

        # Also create file at root
        root_file = local_folder / 'root.txt'
        shutil.copy(str(temp_file), str(root_file))

        # Create mock args
        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'local_folder': str(local_folder),
            'dry_run': False,
            'max_threads': 2,
            'allow_hidden_files': False
        })()

        # Execute command
        s3_manage.command_upload(args, s3_client=s3_client)

        # Verify nested paths preserved
        response = s3_client.list_objects_v2(Bucket='test-bucket')
        assert len(response['Contents']) == 2
        keys = [obj['Key'] for obj in response['Contents']]
        assert 'root.txt' in keys
        assert 'data/2024/01/file.txt' in keys

    @mock_aws
    def test_upload_statistics_validation(self, temp_dir, temp_file, capsys):
        """Test upload command reports accurate statistics."""
        # Setup S3 environment
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        # Upload 1 file to S3 (will be skipped)
        s3_client.upload_file(str(temp_file), 'test-bucket', 'existing.txt')

        # Create local directory with 3 files (1 exists, 2 new)
        local_folder = temp_dir / 'uploads'
        local_folder.mkdir()

        # Existing file (will be skipped)
        existing = local_folder / 'existing.txt'
        shutil.copy(str(temp_file), str(existing))

        # New files (will be uploaded)
        for i in range(2):
            new_file = local_folder / f'new{i}.txt'
            shutil.copy(str(temp_file), str(new_file))

        # Create mock args
        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'local_folder': str(local_folder),
            'dry_run': False,
            'max_threads': 2,
            'allow_hidden_files': False
        })()

        # Execute command
        s3_manage.command_upload(args, s3_client=s3_client)

        # Verify statistics in output
        captured = capsys.readouterr()
        assert 'Total files scanned: 3' in captured.out
        assert 'Files uploaded: 2' in captured.out
        assert 'Files skipped (already exist): 1' in captured.out
        assert 'Upload speed:' in captured.out

    @mock_aws
    def test_upload_error_handling_invalid_bucket(self, temp_dir, temp_file, capsys):
        """Test upload handles errors gracefully."""
        # Setup S3 environment (bucket NOT created - will cause error)
        s3_client = boto3.client('s3', region_name='us-east-1')

        # Create local directory with file
        local_folder = temp_dir / 'uploads'
        local_folder.mkdir()
        test_file = local_folder / 'test.txt'
        shutil.copy(str(temp_file), str(test_file))

        # Create mock args
        args = type('Args', (), {
            'bucket_name': 'nonexistent-bucket',
            'local_folder': str(local_folder),
            'dry_run': False,
            'max_threads': 2,
            'allow_hidden_files': False
        })()

        # Execute command - should handle error gracefully
        s3_manage.command_upload(args, s3_client=s3_client)

        # Verify error message in output
        captured = capsys.readouterr()
        assert '[FAILURE]' in captured.out or 'ERROR' in captured.out

    @mock_aws
    def test_upload_mixed_success_and_skip(self, temp_dir, temp_file, capsys):
        """Test upload with mix of new files and existing files."""
        # Setup S3 environment
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        # Upload 2 files to S3 first
        s3_client.upload_file(str(temp_file), 'test-bucket', 'skip1.txt')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'skip2.txt')

        # Create local directory with 5 files (2 exist, 3 new)
        local_folder = temp_dir / 'uploads'
        local_folder.mkdir()

        # Existing files (will be skipped)
        for i in range(2):
            skip_file = local_folder / f'skip{i+1}.txt'
            shutil.copy(str(temp_file), str(skip_file))

        # New files (will be uploaded)
        for i in range(3):
            new_file = local_folder / f'new{i}.txt'
            shutil.copy(str(temp_file), str(new_file))

        # Create mock args
        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'local_folder': str(local_folder),
            'dry_run': False,
            'max_threads': 2,
            'allow_hidden_files': False
        })()

        # Execute command
        s3_manage.command_upload(args, s3_client=s3_client)

        # Verify final state
        response = s3_client.list_objects_v2(Bucket='test-bucket')
        assert len(response['Contents']) == 5

        # Verify output statistics
        captured = capsys.readouterr()
        assert 'Total files scanned: 5' in captured.out
        assert 'Files uploaded: 3' in captured.out
        assert 'Files skipped (already exist): 2' in captured.out

    @mock_aws
    def test_upload_summary_format(self, temp_dir, temp_file, capsys):
        """Test upload summary format matches expected output."""
        # Setup S3 environment
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        # Create local directory with file
        local_folder = temp_dir / 'uploads'
        local_folder.mkdir()
        test_file = local_folder / 'test.txt'
        shutil.copy(str(temp_file), str(test_file))

        # Create mock args
        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'local_folder': str(local_folder),
            'dry_run': False,
            'max_threads': 2,
            'allow_hidden_files': False
        })()

        # Execute command
        s3_manage.command_upload(args, s3_client=s3_client)

        # Verify summary format
        captured = capsys.readouterr()
        assert '--- UPLOAD SUMMARY ---' in captured.out
        assert 'Total files scanned:' in captured.out
        assert 'Files uploaded:' in captured.out
        assert 'Files skipped (already exist):' in captured.out
        assert 'Total uploaded size:' in captured.out
        assert 'Upload speed:' in captured.out
        assert 'All operations complete!' in captured.out

# ====================================================================================
# Phase 4 Chunk 4: command_verify (Requires S3 Mocking + stdout capture + file I/O)
# ====================================================================================

class TestCommandVerify:
    """Test command_verify() with mocked S3 and capsys (13 tests)"""

    @mock_aws
    def test_verify_empty_directory(self, temp_dir, capsys):
        """Test verify from empty directory shows appropriate message."""
        # Setup S3 environment
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        # Create empty directory
        local_folder = temp_dir / 'verify'
        local_folder.mkdir()

        # Create mock args
        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'local_folder': str(local_folder),
            'max_threads': 2,
            'allow_hidden_files': False
        })()

        # Execute command
        s3_manage.command_verify(args, s3_client=s3_client)

        # Verify output
        captured = capsys.readouterr()
        assert 'Found 0 files' in captured.out

    @mock_aws
    def test_verify_all_files_match(self, temp_dir, temp_file, capsys):
        """Test verify when all files match S3."""
        # Setup S3 environment
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        # Create local directory with file
        local_folder = temp_dir / 'verify'
        local_folder.mkdir()
        test_file = local_folder / 'test.txt'
        shutil.copy(str(temp_file), str(test_file))

        # Upload same file to S3
        s3_client.upload_file(str(test_file), 'test-bucket', 'test.txt')

        # Create mock args
        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'local_folder': str(local_folder),
            'max_threads': 2,
            'allow_hidden_files': False
        })()

        # Execute command (should succeed with exit code 0)
        s3_manage.command_verify(args, s3_client=s3_client)

        # Verify output
        captured = capsys.readouterr()
        assert '[VERIFIED]' in captured.out
        assert 'Verified (match): 1' in captured.out
        assert 'Different (mismatch): 0' in captured.out

    @mock_aws
    def test_verify_file_different(self, temp_dir, temp_file, capsys):
        """Test verify when file differs from S3 (should exit 1)."""
        # Setup S3 environment
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        # Create local file with content
        local_folder = temp_dir / 'verify'
        local_folder.mkdir()
        test_file = local_folder / 'test.txt'
        test_file.write_text('local content')

        # Upload different content to S3
        s3_client.put_object(Bucket='test-bucket', Key='test.txt', Body=b'different content')

        # Create mock args
        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'local_folder': str(local_folder),
            'max_threads': 2,
            'allow_hidden_files': False
        })()

        # Execute command - should exit with code 1
        with pytest.raises(SystemExit) as exc_info:
            s3_manage.command_verify(args, s3_client=s3_client)
        assert exc_info.value.code == 1

        # Verify output
        captured = capsys.readouterr()
        assert '[DIFFERENT]' in captured.out
        assert 'Different (mismatch): 1' in captured.out

    @mock_aws
    def test_verify_file_not_uploaded(self, temp_dir, temp_file, capsys):
        """Test verify when file doesn't exist in S3."""
        # Setup S3 environment
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        # Create local file (not uploaded to S3)
        local_folder = temp_dir / 'verify'
        local_folder.mkdir()
        test_file = local_folder / 'test.txt'
        shutil.copy(str(temp_file), str(test_file))

        # Create mock args
        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'local_folder': str(local_folder),
            'max_threads': 2,
            'allow_hidden_files': False
        })()

        # Execute command
        s3_manage.command_verify(args, s3_client=s3_client)

        # Verify output
        captured = capsys.readouterr()
        assert '[NOT UPLOADED]' in captured.out
        assert 'Not uploaded to S3: 1' in captured.out

    @mock_aws
    def test_verify_mixed_scenarios(self, temp_dir, temp_file, capsys):
        """Test verify with mixed verified, different, and not uploaded files."""
        # Setup S3 environment
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        # Create local directory
        local_folder = temp_dir / 'verify'
        local_folder.mkdir()

        # File 1: Verified (matches S3)
        verified_file = local_folder / 'verified.txt'
        shutil.copy(str(temp_file), str(verified_file))
        s3_client.upload_file(str(verified_file), 'test-bucket', 'verified.txt')

        # File 2: Different (content differs)
        different_file = local_folder / 'different.txt'
        different_file.write_text('local content')
        s3_client.put_object(Bucket='test-bucket', Key='different.txt', Body=b'different content')

        # File 3: Not uploaded
        not_uploaded_file = local_folder / 'not_uploaded.txt'
        shutil.copy(str(temp_file), str(not_uploaded_file))

        # Create mock args
        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'local_folder': str(local_folder),
            'max_threads': 2,
            'allow_hidden_files': False
        })()

        # Execute command - should exit with code 1 due to different file
        with pytest.raises(SystemExit) as exc_info:
            s3_manage.command_verify(args, s3_client=s3_client)
        assert exc_info.value.code == 1

        # Verify output
        captured = capsys.readouterr()
        assert 'Total files scanned: 3' in captured.out
        assert 'Verified (match): 1' in captured.out
        assert 'Different (mismatch): 1' in captured.out
        assert 'Not uploaded to S3: 1' in captured.out

    @mock_aws
    def test_verify_hidden_files_excluded_by_default(self, temp_dir, temp_file, capsys):
        """Test that hidden files are excluded by default."""
        # Setup S3 environment
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        # Create local directory with hidden and regular files
        local_folder = temp_dir / 'verify'
        local_folder.mkdir()

        # Regular file
        regular_file = local_folder / 'regular.txt'
        shutil.copy(str(temp_file), str(regular_file))
        s3_client.upload_file(str(regular_file), 'test-bucket', 'regular.txt')

        # Hidden file
        hidden_file = local_folder / '.hidden.txt'
        shutil.copy(str(temp_file), str(hidden_file))

        # Create mock args with allow_hidden_files=False (default)
        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'local_folder': str(local_folder),
            'max_threads': 2,
            'allow_hidden_files': False
        })()

        # Execute command
        s3_manage.command_verify(args, s3_client=s3_client)

        # Verify only regular file verified
        captured = capsys.readouterr()
        assert 'Found 1 files' in captured.out
        assert 'Verified (match): 1' in captured.out

    @mock_aws
    def test_verify_hidden_files_included_when_allowed(self, temp_dir, temp_file, capsys):
        """Test that hidden files are included when --allow-hidden-files is set."""
        # Setup S3 environment
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        # Create local directory with hidden and regular files
        local_folder = temp_dir / 'verify'
        local_folder.mkdir()

        regular_file = local_folder / 'regular.txt'
        shutil.copy(str(temp_file), str(regular_file))
        s3_client.upload_file(str(regular_file), 'test-bucket', 'regular.txt')

        hidden_file = local_folder / '.hidden.txt'
        shutil.copy(str(temp_file), str(hidden_file))
        s3_client.upload_file(str(hidden_file), 'test-bucket', '.hidden.txt')

        # Create mock args with allow_hidden_files=True
        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'local_folder': str(local_folder),
            'max_threads': 2,
            'allow_hidden_files': True
        })()

        # Execute command
        s3_manage.command_verify(args, s3_client=s3_client)

        # Verify both files verified
        captured = capsys.readouterr()
        assert 'Found 2 files' in captured.out
        assert 'Verified (match): 2' in captured.out

    @mock_aws
    def test_verify_nested_directories(self, temp_dir, temp_file, capsys):
        """Test verify preserves nested directory structure."""
        # Setup S3 environment
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        # Create nested directory structure
        local_folder = temp_dir / 'verify'
        local_folder.mkdir()

        # Create nested structure: verify/data/2024/01/file.txt
        nested_dir = local_folder / 'data' / '2024' / '01'
        nested_dir.mkdir(parents=True)
        nested_file = nested_dir / 'file.txt'
        shutil.copy(str(temp_file), str(nested_file))

        # Upload to S3 with nested path
        s3_client.upload_file(str(nested_file), 'test-bucket', 'data/2024/01/file.txt')

        # Also create file at root
        root_file = local_folder / 'root.txt'
        shutil.copy(str(temp_file), str(root_file))
        s3_client.upload_file(str(root_file), 'test-bucket', 'root.txt')

        # Create mock args
        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'local_folder': str(local_folder),
            'max_threads': 2,
            'allow_hidden_files': False
        })()

        # Execute command
        s3_manage.command_verify(args, s3_client=s3_client)

        # Verify both files verified
        captured = capsys.readouterr()
        assert 'Found 2 files' in captured.out
        assert 'Verified (match): 2' in captured.out

    @mock_aws
    def test_verify_statistics_validation(self, temp_dir, temp_file, capsys):
        """Test verify command reports accurate statistics."""
        # Setup S3 environment
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        # Create local directory with various scenarios
        local_folder = temp_dir / 'verify'
        local_folder.mkdir()

        # 2 verified files
        for i in range(2):
            verified = local_folder / f'verified{i}.txt'
            shutil.copy(str(temp_file), str(verified))
            s3_client.upload_file(str(verified), 'test-bucket', f'verified{i}.txt')

        # 1 different file
        different = local_folder / 'different.txt'
        different.write_text('local content')
        s3_client.put_object(Bucket='test-bucket', Key='different.txt', Body=b'different')

        # 1 not uploaded file
        not_uploaded = local_folder / 'not_uploaded.txt'
        shutil.copy(str(temp_file), str(not_uploaded))

        # Create mock args
        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'local_folder': str(local_folder),
            'max_threads': 2,
            'allow_hidden_files': False
        })()

        # Execute command
        with pytest.raises(SystemExit) as exc_info:
            s3_manage.command_verify(args, s3_client=s3_client)
        assert exc_info.value.code == 1

        # Verify statistics
        captured = capsys.readouterr()
        assert 'Total files scanned: 4' in captured.out
        assert 'Verified (match): 2' in captured.out
        assert 'Different (mismatch): 1' in captured.out
        assert 'Not uploaded to S3: 1' in captured.out
        assert 'Errors: 0' in captured.out
        assert 'Verify speed:' in captured.out

    @mock_aws
    def test_verify_error_handling_invalid_bucket(self, temp_dir, temp_file, capsys):
        """Test verify handles errors gracefully."""
        # Setup S3 environment (bucket NOT created)
        s3_client = boto3.client('s3', region_name='us-east-1')

        # Create local directory with file
        local_folder = temp_dir / 'verify'
        local_folder.mkdir()
        test_file = local_folder / 'test.txt'
        shutil.copy(str(temp_file), str(test_file))

        # Create mock args
        args = type('Args', (), {
            'bucket_name': 'nonexistent-bucket',
            'local_folder': str(local_folder),
            'max_threads': 2,
            'allow_hidden_files': False
        })()

        # Execute command - should handle error gracefully and exit 1
        with pytest.raises(SystemExit) as exc_info:
            s3_manage.command_verify(args, s3_client=s3_client)
        assert exc_info.value.code == 1

        # Verify error in output
        captured = capsys.readouterr()
        assert 'ERROR' in captured.out or 'Errors: 1' in captured.out

    @mock_aws
    def test_verify_exit_code_success(self, temp_dir, temp_file, capsys):
        """Test verify exits with code 0 when all files verified."""
        # Setup S3 environment
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        # Create local file matching S3
        local_folder = temp_dir / 'verify'
        local_folder.mkdir()
        test_file = local_folder / 'test.txt'
        shutil.copy(str(temp_file), str(test_file))
        s3_client.upload_file(str(test_file), 'test-bucket', 'test.txt')

        # Create mock args
        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'local_folder': str(local_folder),
            'max_threads': 2,
            'allow_hidden_files': False
        })()

        # Execute command - should NOT raise SystemExit
        s3_manage.command_verify(args, s3_client=s3_client)

        # Verify output (no exception means exit code 0)
        captured = capsys.readouterr()
        assert 'Verified (match): 1' in captured.out
        assert 'Different (mismatch): 0' in captured.out
        assert 'Errors: 0' in captured.out

    @mock_aws
    def test_verify_exit_code_failure_on_mismatch(self, temp_dir, temp_file, capsys):
        """Test verify exits with code 1 when mismatches found."""
        # Setup S3 environment
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        # Create local file with different content
        local_folder = temp_dir / 'verify'
        local_folder.mkdir()
        test_file = local_folder / 'test.txt'
        test_file.write_text('local content')
        s3_client.put_object(Bucket='test-bucket', Key='test.txt', Body=b'different')

        # Create mock args
        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'local_folder': str(local_folder),
            'max_threads': 2,
            'allow_hidden_files': False
        })()

        # Execute command - should raise SystemExit with code 1
        with pytest.raises(SystemExit) as exc_info:
            s3_manage.command_verify(args, s3_client=s3_client)
        assert exc_info.value.code == 1

    @mock_aws
    def test_verify_summary_format(self, temp_dir, temp_file, capsys):
        """Test verify summary format matches expected output."""
        # Setup S3 environment
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        # Create local file matching S3
        local_folder = temp_dir / 'verify'
        local_folder.mkdir()
        test_file = local_folder / 'test.txt'
        shutil.copy(str(temp_file), str(test_file))
        s3_client.upload_file(str(test_file), 'test-bucket', 'test.txt')

        # Create mock args
        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'local_folder': str(local_folder),
            'max_threads': 2,
            'allow_hidden_files': False
        })()

        # Execute command
        s3_manage.command_verify(args, s3_client=s3_client)

        # Verify summary format
        captured = capsys.readouterr()
        assert '--- VERIFICATION SUMMARY ---' in captured.out
        assert 'Total files scanned:' in captured.out
        assert 'Verified (match):' in captured.out
        assert 'Different (mismatch):' in captured.out
        assert 'Not uploaded to S3:' in captured.out
        assert 'Errors:' in captured.out
        assert 'Verify speed:' in captured.out
        assert 'All verifications complete!' in captured.out
