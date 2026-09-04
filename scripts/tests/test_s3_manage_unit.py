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
- Phase 1: Pure Functions (TestFormatting, TestCandidateChunkSizes, TestETagSimple, TestMultipartEtagMatches, TestIndexGeneration)
- Phase 2: Worker Functions (TestFileExistsInBucket, TestUploadWorker, TestVerifyWorker, TestDownloadWorker)
"""

import pytest
import sys
import os
import shutil
import hashlib
import tracemalloc
from pathlib import Path

# Add scripts directory to path to import s3_manage
sys.path.insert(0, str(Path(__file__).parent.parent))

import s3_manage

MIB = 1024 * 1024


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


class TestCandidateChunkSizes:
    """Test candidate_chunk_sizes() - every standard chunk size consistent with a part count."""

    def test_exact_multiple_5mb(self):
        """50 MiB in 10 parts is reproducible only by 5 MiB chunks."""
        assert s3_manage.candidate_chunk_sizes(50 * MIB, 10) == [5 * MIB]

    def test_exact_multiple_8mb(self):
        """80 MiB in 10 parts is reproducible only by 8 MiB chunks."""
        assert s3_manage.candidate_chunk_sizes(80 * MIB, 10) == [8 * MIB]

    def test_exact_multiple_128mb(self):
        """5 GiB in 40 parts is reproducible only by 128 MiB chunks."""
        assert s3_manage.candidate_chunk_sizes(5 * 1024 * MIB, 40) == [128 * MIB]

    def test_no_standard_size_matches(self):
        """100 MiB in 11 parts matches no standard chunk size."""
        assert s3_manage.candidate_chunk_sizes(100 * MIB, 11) == []

    def test_non_positive_part_count(self):
        """A non-positive part count yields no candidates."""
        assert s3_manage.candidate_chunk_sizes(100 * MIB, 0) == []
        assert s3_manage.candidate_chunk_sizes(100 * MIB, -1) == []

    def test_ambiguous_part_count_returns_all_matches_8mb_first(self):
        """F-05: a 9.5 MiB / 2-part object is consistent with both 5 MiB and 8 MiB chunks.

        The old calculate_chunk_size() returned 5 MiB (first in its list) and computed the
        wrong ETag. The candidate list must contain both, with boto3's 8 MiB default first.
        """
        candidates = s3_manage.candidate_chunk_sizes(9_961_472, 2)
        assert set(candidates) == {5 * MIB, 8 * MIB}
        assert candidates[0] == 8 * MIB

    def test_single_part_empty_file(self):
        """A single-part upload has no interior boundary; an empty file maps to one chunk."""
        assert s3_manage.candidate_chunk_sizes(0, 1) == [1]

    def test_single_part_small_file(self):
        """A single-part upload returns the whole file as the only chunk size."""
        assert s3_manage.candidate_chunk_sizes(3 * MIB, 1) == [3 * MIB]

    def test_single_part_beyond_old_512mib_ceiling(self):
        """The old calculate_chunk_size() capped at 512 MiB and returned None (-> [ERROR])
        for a larger single-part object; the size-based rule now handles any size."""
        assert s3_manage.candidate_chunk_sizes(600 * MIB, 1) == [600 * MIB]
        assert s3_manage.candidate_chunk_sizes(5 * 1024 * MIB, 1) == [5 * 1024 * MIB]


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
        # write_bytes, not write_text: on Windows text mode translates "\n" to
        # "\r\n", which changes the bytes on disk and therefore the ETag
        test_file.write_bytes(b"test content\n")

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


class TestMultipartEtagMatches:
    """Test multipart_etag_matches() - exact multipart-ETag reproduction, never guessing."""

    @staticmethod
    def _reference_multipart_hash(data, chunk_size):
        """Independent oracle for S3's multipart-ETag hash over in-memory bytes."""
        digests = [
            hashlib.md5(data[offset:offset + chunk_size]).digest()
            for offset in range(0, max(len(data), 1), chunk_size)
        ]
        return hashlib.md5(b"".join(digests)).hexdigest()

    def test_matches_actual_8mb_chunking(self, temp_dir):
        """A 24 MiB / 3-part object chunked at 8 MiB verifies against its true ETag."""
        data = b"x" * (24 * MIB)
        test_file = temp_dir / "24mb.bin"
        test_file.write_bytes(data)

        expected = self._reference_multipart_hash(data, 8 * MIB)
        assert s3_manage.multipart_etag_matches(str(test_file), expected, 3) is True

    def test_ambiguous_size_still_matches_8mb_upload(self, temp_dir):
        """F-05: a 9.5 MiB object uploaded with 8 MiB chunks (2 parts) still verifies.

        5 MiB chunks also yield 2 parts for this size; the old code guessed 5 MiB and
        failed. The candidate loop must try 8 MiB and match exactly.
        """
        data = b"q" * 9_961_472
        test_file = temp_dir / "ambiguous.bin"
        test_file.write_bytes(data)

        etag_8mb = self._reference_multipart_hash(data, 8 * MIB)
        assert s3_manage.multipart_etag_matches(str(test_file), etag_8mb, 2) is True

    def test_reproduces_genuine_5mb_upload_of_ambiguous_size(self, temp_dir):
        """The same 9.5 MiB body genuinely uploaded with 5 MiB chunks also verifies."""
        data = b"q" * 9_961_472
        test_file = temp_dir / "ambiguous.bin"
        test_file.write_bytes(data)

        etag_5mb = self._reference_multipart_hash(data, 5 * MIB)
        assert s3_manage.multipart_etag_matches(str(test_file), etag_5mb, 2) is True

    def test_rejects_unrelated_hash(self, temp_dir):
        """A hash that no consistent chunk size reproduces is not a match."""
        data = b"q" * 9_961_472
        test_file = temp_dir / "ambiguous.bin"
        test_file.write_bytes(data)

        assert s3_manage.multipart_etag_matches(str(test_file), "0" * 32, 2) is False

    def test_single_part_uses_whole_file(self, temp_dir):
        """A 'hash-1' ETag is MD5(MD5(whole file)), independent of chunk size."""
        data = b"z" * 4096
        test_file = temp_dir / "small.bin"
        test_file.write_bytes(data)

        expected = hashlib.md5(hashlib.md5(data).digest()).hexdigest()
        assert s3_manage.multipart_etag_matches(str(test_file), expected, 1) is True

    def test_empty_file_single_part(self, temp_dir):
        """An empty single-part object hashes as one empty part."""
        test_file = temp_dir / "empty.bin"
        test_file.touch()

        expected = hashlib.md5(hashlib.md5(b"").digest()).hexdigest()
        assert s3_manage.multipart_etag_matches(str(test_file), expected, 1) is True

    def test_exactly_8mib_is_single_part_multipart(self, temp_dir):
        """boto3's multipart threshold is >=, so an exactly-8-MiB upload is 'hash-1'."""
        data = b"m" * (8 * MIB)
        test_file = temp_dir / "exact8.bin"
        test_file.write_bytes(data)

        expected = hashlib.md5(hashlib.md5(data).digest()).hexdigest()
        assert s3_manage.multipart_etag_matches(str(test_file), expected, 1) is True

    def test_wrong_part_count_has_no_candidates(self, temp_dir):
        """No standard chunk size splits this file into the claimed number of parts."""
        test_file = temp_dir / "tiny.bin"
        test_file.write_bytes(b"w" * 100)

        assert s3_manage.multipart_etag_matches(str(test_file), "a" * 32, 11) is False

    def test_deterministic(self, temp_dir):
        """Repeated calls return the same verdict."""
        data = b"d" * (15 * MIB)
        test_file = temp_dir / "15mb.bin"
        test_file.write_bytes(data)

        expected = self._reference_multipart_hash(data, 5 * MIB)
        assert s3_manage.multipart_etag_matches(str(test_file), expected, 3) is True
        assert s3_manage.multipart_etag_matches(str(test_file), expected, 3) is True

    def test_single_part_read_is_memory_bounded(self, temp_dir):
        """A 'hash-1' ETag hashes the whole file as one part; that must not pull the
        whole part into memory at once (regression: it used to allocate 2x file size)."""
        data = b"p" * (32 * MIB)
        test_file = temp_dir / "onepart.bin"
        test_file.write_bytes(data)
        expected = hashlib.md5(hashlib.md5(data).digest()).hexdigest()

        tracemalloc.start()
        try:
            matched = s3_manage.multipart_etag_matches(str(test_file), expected, 1)
            _, peak = tracemalloc.get_traced_memory()
        finally:
            tracemalloc.stop()

        assert matched is True
        assert peak < 8 * MIB, f"peak alloc {peak / MIB:.1f} MiB for a 32 MiB single-part file"

    def test_missing_file_raises(self, temp_dir):
        """A missing local file surfaces as OSError."""
        with pytest.raises(OSError):
            s3_manage.multipart_etag_matches(str(temp_dir / "nope.bin"), "a" * 32, 2)


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
        href, text = _parse_single_index_anchor(html)

        assert href == "https://example.com/file%3Ctest%3E.txt"
        assert text == "file<test>.txt"
        assert "file&lt;test&gt;.txt" in html
        assert "file<test>.txt" not in html

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

    def test_generate_html_invalid_prefix_empty_list(self):
        """Invalid prefix is rejected even with an empty object list."""
        with pytest.raises(s3_manage.InvalidIndexUrlPrefixError):
            s3_manage.generate_html_index([], 0, 0, "javascript:alert(1)")
        with pytest.raises(s3_manage.InvalidIndexUrlPrefixError):
            s3_manage.generate_html_index([], 0, 0, "https://example.com/)<img src=x>")

    def test_generate_markdown_invalid_prefix_empty_list(self):
        """Invalid prefix is rejected even with an empty object list."""
        with pytest.raises(s3_manage.InvalidIndexUrlPrefixError):
            s3_manage.generate_markdown_index([], 0, 0, "javascript:alert(1)")
        with pytest.raises(s3_manage.InvalidIndexUrlPrefixError):
            s3_manage.generate_markdown_index([], 0, 0, "https://example.com/)<img src=x>")


from html.parser import HTMLParser
from datetime import datetime, timezone
import urllib.parse


class _IndexAnchorParser(HTMLParser):
    """Collect <a> tags and extra start tags from a generated index."""

    def __init__(self):
        super().__init__()
        self.anchors = []
        self.start_tags = []
        self._current = None
        self._data = []

    def handle_starttag(self, tag, attrs):
        self.start_tags.append(tag)
        if tag == 'a':
            self._current = {'attrs': list(attrs), 'text': ''}
            self._data = []

    def handle_data(self, data):
        if self._current is not None:
            self._data.append(data)

    def handle_endtag(self, tag):
        if tag == 'a' and self._current is not None:
            self._current['text'] = ''.join(self._data)
            self.anchors.append(self._current)
            self._current = None
            self._data = []


def _parse_single_index_anchor(html):
    parser = _IndexAnchorParser()
    parser.feed(html)
    assert parser.start_tags.count('a') == 1
    assert 'script' not in parser.start_tags
    assert len(parser.anchors) == 1
    attrs = parser.anchors[0]['attrs']
    assert [name for name, _ in attrs] == ['href']
    return attrs[0][1], parser.anchors[0]['text']


def _expected_index_url(key, prefix="https://example.com/"):
    return prefix + urllib.parse.quote(key, safe='/', encoding='utf-8', errors='strict')


def _index_object(key, size=100):
    return {
        'key': key,
        'size': size,
        'last_modified': datetime(2024, 1, 1, tzinfo=timezone.utc),
    }


class TestIndexUrlPrefix:
    """Test validate_index_url_prefix() and build_index_download_url()."""

    def test_accept_http_and_https_trailing_slash(self):
        assert s3_manage.validate_index_url_prefix("https://example.com/") == "https://example.com/"
        assert s3_manage.validate_index_url_prefix("http://example.com/") == "http://example.com/"

    def test_accept_missing_trailing_slash(self):
        assert s3_manage.validate_index_url_prefix("https://example.com") == "https://example.com/"

    def test_accept_port_and_path(self):
        assert s3_manage.validate_index_url_prefix("https://example.com:8443/bucket") == "https://example.com:8443/bucket/"

    def test_accept_scheme_case(self):
        assert s3_manage.validate_index_url_prefix("HTTP://example.com") == "http://example.com/"

    def test_accept_ipv6_with_port(self):
        assert s3_manage.validate_index_url_prefix("http://[::1]:9000/bucket") == "http://[::1]:9000/bucket/"

    def test_accept_valid_percent_escape(self):
        assert s3_manage.validate_index_url_prefix("https://example.com/%2F") == "https://example.com/%2F/"
        assert s3_manage.validate_index_url_prefix("https://example.com/%3F/%23") == (
            "https://example.com/%3F/%23/"
        )

    @pytest.mark.parametrize("prefix", [
        "javascript:alert(1)",
        "data:text/html,hi",
        "file:///tmp",
        "",
        None,
        "   ",
        " https://example.com/",
        "https://example.com/with space/",
        "/bucket/",
        "//cdn.example/",
        "https://u:p@host/",
        "https://example.com/?x=1",
        "https://example.com/path?",
        "https://example.com/#x",
        "https://example.com/path#",
        "https://example.com/path?#",
        "https://example.com/\\path/",
        "https://example.com/\x00/",
        "https://example.com/)/",
        "https://example.com/(/",
        "https://example.com/</",
        "https://example.com/>/",
        'https://example.com/"/',
        "https://example.com/'/",
        "https://example.com/%zz/",
        "https://example.com/%2",
        "https://example.com:/path",
        "http://[::1]:/path",
        "https://example.com:abc/",
        "https://example.com:70000/",
    ])
    def test_reject_invalid_prefixes(self, prefix):
        with pytest.raises(s3_manage.InvalidIndexUrlPrefixError):
            s3_manage.validate_index_url_prefix(prefix)

    def test_build_index_download_url_concatenates_normalized_prefix(self):
        url = s3_manage.build_index_download_url("https://example.com", "folder/file.txt")
        assert url == "https://example.com/folder/file.txt"

    def test_build_index_download_url_encodes_key_only(self):
        url = s3_manage.build_index_download_url("https://example.com/", 'a b.txt')
        assert url == "https://example.com/a%20b.txt"


class TestIndexHostileKeys:
    """Hostile S3 keys are encoded and included, never omitted."""

    def test_html_quote_breakout(self):
        key = 'foo"><script>alert(1)</script>'
        html = s3_manage.generate_html_index([_index_object(key)], 1, 100, "https://example.com/")
        href, text = _parse_single_index_anchor(html)
        assert href == _expected_index_url(key)
        assert '%22' in href
        assert text == key

    def test_html_ampersand_and_angles(self):
        key = 'a&b<c>.txt'
        html = s3_manage.generate_html_index([_index_object(key)], 1, 100, "https://example.com/")
        href, text = _parse_single_index_anchor(html)
        assert href == "https://example.com/a%26b%3Cc%3E.txt"
        assert "a&amp;b&lt;c&gt;.txt" in html
        assert text == key

    def test_html_fragment_and_query_and_scheme_like(self):
        cases = [
            ('file.txt#xss', 'https://example.com/file.txt%23xss'),
            ('file.txt?x=1', 'https://example.com/file.txt%3Fx%3D1'),
            ('javascript:alert(1)', 'https://example.com/javascript%3Aalert%281%29'),
        ]
        for key, expected in cases:
            html = s3_manage.generate_html_index([_index_object(key)], 1, 100, "https://example.com/")
            href, text = _parse_single_index_anchor(html)
            assert href == expected
            assert text == key

    def test_html_control_characters(self):
        key = "a\x00b\tc\rd\ne\x0cf\x7fg.txt"
        html = s3_manage.generate_html_index([_index_object(key)], 1, 100, "https://example.com/")
        href, text = _parse_single_index_anchor(html)
        assert href == "https://example.com/a%00b%09c%0Dd%0Ae%0Cf%7Fg.txt"
        assert text == key

    def test_html_unicode_and_nested_and_percent(self):
        cases = [
            ('café.txt', 'https://example.com/caf%C3%A9.txt', 'café.txt'),
            ('folder/file.txt', 'https://example.com/folder/file.txt', 'folder/file.txt'),
            ('100%.txt', 'https://example.com/100%25.txt', '100%.txt'),
        ]
        for key, expected, label in cases:
            html = s3_manage.generate_html_index([_index_object(key)], 1, 100, "https://example.com/")
            href, text = _parse_single_index_anchor(html)
            assert href == expected
            assert text == label

    def test_html_dot_segments_and_slashes_source_only(self):
        cases = [
            ('../file', 'https://example.com/../file'),
            ('a/../b', 'https://example.com/a/../b'),
            ('./file', 'https://example.com/./file'),
            ('/file', 'https://example.com//file'),
            ('//file', 'https://example.com///file'),
            ('foo//bar', 'https://example.com/foo//bar'),
        ]
        for key, expected in cases:
            html = s3_manage.generate_html_index([_index_object(key)], 1, 100, "https://example.com/")
            href, text = _parse_single_index_anchor(html)
            assert href == expected
            assert '%2E' not in href
            assert '%2F' not in href
            assert text == key

    def test_markdown_matches_html_url_and_escapes_label(self):
        key = 'file\\path|with|[brackets] and ).txt'
        objects = [_index_object(key)]
        html = s3_manage.generate_html_index(objects, 1, 100, "https://example.com/")
        md = s3_manage.generate_markdown_index(objects, 1, 100, "https://example.com/")
        href, _text = _parse_single_index_anchor(html)
        assert f']({href})' in md
        assert 'file\\\\path' in md
        assert '\\|' in md
        assert '\\[' in md
        assert '\\]' in md
        assert '%29' in href

    @pytest.mark.parametrize("key", [
        'foo"><script>alert(1)</script>',
        'a&b<c>.txt',
        'file.txt#xss',
        'file.txt?x=1',
        'javascript:alert(1)',
        "a\x00b\tc\rd\ne\x0cf\x7fg.txt",
        'café.txt',
        'folder/file.txt',
        '100%.txt',
        '../file',
        'a/../b',
        './file',
        '/file',
        '//file',
        'foo//bar',
    ])
    def test_markdown_url_matches_html_for_hostile_keys(self, key):
        """HTML and Markdown use the same encoded URL for every hostile-key class."""
        objects = [_index_object(key)]
        html = s3_manage.generate_html_index(objects, 1, 100, "https://example.com/")
        md = s3_manage.generate_markdown_index(objects, 1, 100, "https://example.com/")
        href, _text = _parse_single_index_anchor(html)
        assert href == _expected_index_url(key)
        assert f']({href})' in md

    def test_markdown_html_escapes_script_like_label(self):
        key = 'foo"><script>alert(1)</script>'
        md = s3_manage.generate_markdown_index(
            [_index_object(key)], 1, 100, "https://example.com/"
        )
        assert 'foo&quot;&gt;&lt;script&gt;alert(1)&lt;/script&gt;' in md
        assert '<script>' not in md

    def test_markdown_javascript_key_is_not_javascript_url(self):
        key = 'javascript:alert(1)'
        md = s3_manage.generate_markdown_index([_index_object(key)], 1, 100, "https://example.com/")
        expected = _expected_index_url(key)
        assert f']({expected})' in md
        assert '](javascript:' not in md

    def test_markdown_newline_stays_one_table_row(self):
        key = "file\r\nwith\ttab.txt"
        md = s3_manage.generate_markdown_index([_index_object(key)], 1, 100, "https://example.com/")
        data_rows = [line for line in md.split('\n') if line.startswith('| [')]
        assert len(data_rows) == 1
        assert '\n' not in data_rows[0]
        assert '\r' not in data_rows[0]
        assert '\t' not in data_rows[0]
        assert 'file with tab.txt' in data_rows[0] or 'file  with tab.txt' in data_rows[0]


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
        result, _, _ = s3_manage.upload_worker(s3_client, 'test-bucket', str(temp_file), 'test.txt', dry_run=False)

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
        result, _, _ = s3_manage.upload_worker(s3_client, 'test-bucket', str(temp_file), 'test.txt', dry_run=False)

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
        result, _, _ = s3_manage.upload_worker(s3_client, 'test-bucket', str(temp_file), 'test.txt', dry_run=True)

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
        result, _, _ = s3_manage.upload_worker(s3_client, 'test-bucket', '/nonexistent/file.txt', 'file.txt', dry_run=False)

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
        result, _, _ = s3_manage.upload_worker(s3_client, 'test-bucket', str(temp_file_small), 'small.bin', dry_run=False)

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
        result, _, _ = s3_manage.upload_worker(s3_client, 'test-bucket', str(temp_file_medium), 'medium.bin', dry_run=False)

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
        result, _, _ = s3_manage.upload_worker(s3_client, 'test-bucket', str(temp_file), 'folder/subfolder/file.txt', dry_run=False)

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
        result, _, _ = s3_manage.upload_worker(s3_client, 'test-bucket', str(special_file), 'file-with_special.chars.txt', dry_run=False)

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
        result, _, _ = s3_manage.upload_worker(s3_client, 'test-bucket', str(empty_file), 'empty.txt', dry_run=False)

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
        result1, _, _ = s3_manage.upload_worker(s3_client, 'test-bucket', str(file1), 'file1.txt', dry_run=False)
        result2, _, _ = s3_manage.upload_worker(s3_client, 'test-bucket', str(file2), 'file2.txt', dry_run=False)

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
        result1, _, _ = s3_manage.upload_worker(s3_client, 'test-bucket', str(file1), 'data.txt', dry_run=False)
        assert '[SUCCESS]' in result1

        # Try to upload modified version with same key
        result2, _, _ = s3_manage.upload_worker(s3_client, 'test-bucket', str(file2), 'data.txt', dry_run=False)
        assert '[SKIPPED]' in result2
        assert 'Already exists' in result2

        # Verify original content is preserved
        response = s3_client.get_object(Bucket='test-bucket', Key='data.txt')
        content = response['Body'].read().decode('utf-8')
        assert content == "original content"


class TestUploadWorkerIntegrity:
    """Test upload_worker() checksum recording, TransferConfig pinning, and TOCTOU guard."""

    @mock_aws
    def test_transfer_config_and_metadata_passed_to_upload_file(self, temp_file, monkeypatch):
        """The pinned TRANSFER_CONFIG and the content SHA-256 reach s3transfer verbatim."""
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        seen = {}
        real = s3_client.upload_file

        def spy(Filename, Bucket, Key, ExtraArgs=None, Config=None, **kw):
            seen['ExtraArgs'] = ExtraArgs
            seen['Config'] = Config
            return real(Filename, Bucket, Key, ExtraArgs=ExtraArgs, Config=Config, **kw)

        monkeypatch.setattr(s3_client, 'upload_file', spy)

        s3_manage.upload_worker(s3_client, 'test-bucket', str(temp_file), 'test.txt', dry_run=False)

        assert seen['Config'] is s3_manage.TRANSFER_CONFIG
        assert seen['ExtraArgs']['Metadata'][s3_manage.CONTENT_SHA256_METADATA_KEY] == \
            s3_manage.calculate_file_sha256(str(temp_file))

    @mock_aws
    def test_sha256_read_failure_reports_failure(self, temp_file, monkeypatch):
        """calculate_file_sha256 now runs before upload_file; a read error must fail the file."""
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        def boom(path):
            raise OSError('unreadable')

        monkeypatch.setattr(s3_manage, 'calculate_file_sha256', boom)

        msg, size, cat = s3_manage.upload_worker(
            s3_client, 'test-bucket', str(temp_file), 'test.txt', dry_run=False)
        assert cat == 'failure'
        assert '[FAILURE]' in msg
        assert not s3_manage.file_exists_in_bucket(s3_client, 'test-bucket', 'test.txt')

    @mock_aws
    def test_command_upload_exits_nonzero_on_sha256_read_failure(self, temp_dir, monkeypatch):
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        folder = temp_dir / 'up'
        folder.mkdir()
        (folder / 'a.txt').write_text('hi')

        def boom(path):
            raise OSError('unreadable')

        monkeypatch.setattr(s3_manage, 'calculate_file_sha256', boom)
        args = type('Args', (), {
            'bucket_name': 'test-bucket', 'local_folder': str(folder),
            'max_threads': 2, 'allow_hidden_files': False, 'dry_run': False,
        })()
        assert s3_manage.command_upload(args, s3_client=s3_client) == 1

    @mock_aws
    def test_file_changed_during_upload_fails_and_removes_object(self, temp_dir, monkeypatch):
        """A concurrent writer touching the file after the SHA-256 read strands the object;
        the guard must fail the file and remove the torn object."""
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        p = temp_dir / 'a.bin'
        p.write_bytes(b'original content')

        real = s3_client.upload_file

        def upload_then_grow(Filename, Bucket, Key, **kw):
            real(Filename, Bucket, Key, **kw)      # object really lands in the bucket
            with open(p, 'ab') as f:               # ...then the file changes underneath us
                f.write(b'MORE BYTES')

        monkeypatch.setattr(s3_client, 'upload_file', upload_then_grow)

        msg, size, cat = s3_manage.upload_worker(s3_client, 'test-bucket', str(p), 'a.bin', dry_run=False)
        assert cat == 'failure'
        assert 'changed during upload' in msg
        assert not s3_manage.file_exists_in_bucket(s3_client, 'test-bucket', 'a.bin')

    @mock_aws
    def test_unchanged_file_uploads_and_object_remains(self, temp_dir):
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        p = temp_dir / 'a.bin'
        p.write_bytes(b'stable content')

        msg, size, cat = s3_manage.upload_worker(s3_client, 'test-bucket', str(p), 'a.bin', dry_run=False)
        assert cat == 'uploaded'
        assert '[SUCCESS]' in msg
        assert s3_manage.file_exists_in_bucket(s3_client, 'test-bucket', 'a.bin')

    @mock_aws
    def test_delete_failure_does_not_mask_failure_result(self, temp_dir, monkeypatch):
        """A missing s3:DeleteObject permission must not hide the 'changed during upload' failure."""
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        p = temp_dir / 'a.bin'
        p.write_bytes(b'original content')

        real = s3_client.upload_file

        def upload_then_grow(Filename, Bucket, Key, **kw):
            real(Filename, Bucket, Key, **kw)
            with open(p, 'ab') as f:
                f.write(b'X')

        monkeypatch.setattr(s3_client, 'upload_file', upload_then_grow)

        def boom(**kw):
            raise RuntimeError('no s3:DeleteObject permission')

        monkeypatch.setattr(s3_client, 'delete_object', boom)

        msg, size, cat = s3_manage.upload_worker(s3_client, 'test-bucket', str(p), 'a.bin', dry_run=False)
        assert cat == 'failure'
        assert 'changed during upload' in msg


class TestUploadForce:
    """Test `upload --force` re-uploads existing objects (repair / checksum backfill)."""

    @mock_aws
    def test_existing_object_skipped_without_force(self, temp_file):
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_manage.upload_worker(s3_client, 'test-bucket', str(temp_file), 't.txt', dry_run=False)

        msg, _, cat = s3_manage.upload_worker(s3_client, 'test-bucket', str(temp_file), 't.txt', dry_run=False)
        assert cat == 'skipped'
        assert '[SKIPPED]' in msg

    @mock_aws
    def test_force_reuploads_existing_object(self, temp_file):
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_manage.upload_worker(s3_client, 'test-bucket', str(temp_file), 't.txt', dry_run=False)

        msg, _, cat = s3_manage.upload_worker(
            s3_client, 'test-bucket', str(temp_file), 't.txt', dry_run=False, force=True)
        assert cat == 'uploaded'
        assert '[SUCCESS]' in msg

    @mock_aws
    def test_force_backfills_checksum_on_legacy_object(self, temp_dir):
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        p = temp_dir / 'legacy.bin'
        p.write_bytes(b'legacy payload')
        s3_client.put_object(Bucket='test-bucket', Key='legacy.bin', Body=b'legacy payload')
        assert 'bl-content-sha256' not in s3_client.head_object(
            Bucket='test-bucket', Key='legacy.bin').get('Metadata', {})

        s3_manage.upload_worker(s3_client, 'test-bucket', str(p), 'legacy.bin', dry_run=False, force=True)

        head = s3_client.head_object(Bucket='test-bucket', Key='legacy.bin')
        assert head['Metadata']['bl-content-sha256'] == s3_manage.calculate_file_sha256(str(p))
        result, cat = s3_manage.verify_worker(s3_client, 'test-bucket', str(p), 'legacy.bin')
        assert '[VERIFIED]' in result
        assert cat == 'verified'

    @mock_aws
    def test_force_repairs_bad_recorded_checksum(self, temp_dir):
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        p = temp_dir / 'bad.bin'
        p.write_bytes(b'good bytes')
        s3_client.put_object(
            Bucket='test-bucket', Key='bad.bin', Body=b'good bytes',
            Metadata={'bl-content-sha256': hashlib.sha256(b'WRONG').hexdigest()})

        pre, pre_cat = s3_manage.verify_worker(s3_client, 'test-bucket', str(p), 'bad.bin')
        assert '[DIFFERENT]' in pre
        assert pre_cat == 'different'

        s3_manage.upload_worker(s3_client, 'test-bucket', str(p), 'bad.bin', dry_run=False, force=True)

        post, post_cat = s3_manage.verify_worker(s3_client, 'test-bucket', str(p), 'bad.bin')
        assert '[VERIFIED]' in post
        assert post_cat == 'verified'

    @mock_aws
    def test_command_upload_force_reuploads(self, temp_dir, capsys):
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        folder = temp_dir / 'artifacts'
        folder.mkdir()
        (folder / 't.txt').write_bytes(b'hello')

        def make_args(force):
            return type('Args', (), {
                'bucket_name': 'test-bucket', 'local_folder': str(folder),
                'max_threads': 2, 'allow_hidden_files': False, 'dry_run': False, 'force': force,
            })()

        assert s3_manage.command_upload(make_args(False), s3_client=s3_client) == 0
        capsys.readouterr()
        s3_manage.command_upload(make_args(False), s3_client=s3_client)
        assert 'SKIPPED' in capsys.readouterr().out
        assert s3_manage.command_upload(make_args(True), s3_client=s3_client) == 0
        assert 'SUCCESS' in capsys.readouterr().out


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
        result, _ = s3_manage.verify_worker(s3_client, 'test-bucket', str(temp_file), 'test.txt')

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
        result, _ = s3_manage.verify_worker(s3_client, 'test-bucket', str(file2), 'data.txt')

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
        result, _ = s3_manage.verify_worker(s3_client, 'test-bucket', str(temp_file), 'test.txt')

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
        result, _ = s3_manage.verify_worker(s3_client, 'test-bucket', str(temp_file_small), 'small.bin')

        # Should verify successfully
        assert '[VERIFIED]' in result

    @mock_aws
    def test_verify_worker_multipart_etag(self, temp_file_large):
        """Test verification with multipart ETag.

        The 100 MiB fixture uploaded in 8 MiB parts has 13 parts, and 8 MiB is the only
        standard chunk size consistent with that (size, part count), so the fallback
        reproduces moto's ETag exactly and reports VERIFIED.
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

        # Verify - the fallback reproduces the multipart ETag exactly
        result, category = s3_manage.verify_worker(s3_client, 'test-bucket', str(temp_file_large), 'large.bin')

        assert '[VERIFIED]' in result
        assert category == 'verified'
        assert 'large.bin' in result

    @mock_aws
    def test_verify_worker_file_not_found(self):
        """Test verification when local file doesn't exist."""
        # Setup
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.put_object(Bucket='test-bucket', Key='test.txt', Body=b'content')

        # Try to verify non-existent local file
        result, _ = s3_manage.verify_worker(s3_client, 'test-bucket', '/nonexistent/file.txt', 'test.txt')

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
        result, _ = s3_manage.verify_worker(s3_client, 'test-bucket', str(empty_file), 'empty.txt')

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
        result, _ = s3_manage.verify_worker(s3_client, 'test-bucket', str(temp_file), 'test.txt')
        assert '[VERIFIED]' in result

    @mock_aws
    def test_verify_worker_nested_path(self, temp_file):
        """Test verification with nested path."""
        # Upload to nested path
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'folder/subfolder/file.txt')

        # Verify
        result, _ = s3_manage.verify_worker(s3_client, 'test-bucket', str(temp_file), 'folder/subfolder/file.txt')

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
        result1, _ = s3_manage.verify_worker(s3_client, 'test-bucket', str(file1), 'file1.txt')
        result2, _ = s3_manage.verify_worker(s3_client, 'test-bucket', str(file2), 'file2.txt')

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
        result, _ = s3_manage.verify_worker(s3_client, 'test-bucket', str(large), 'data.txt')

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
        result, _ = s3_manage.verify_worker(s3_client, 'test-bucket', str(file2), 'data.txt')

        assert '[DIFFERENT]' in result

    @mock_aws
    def test_verify_worker_deterministic(self, temp_file):
        """Test that verification is deterministic (same result on multiple calls)."""
        # Upload file
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'test.txt')

        # Verify multiple times
        result1, _ = s3_manage.verify_worker(s3_client, 'test-bucket', str(temp_file), 'test.txt')
        result2, _ = s3_manage.verify_worker(s3_client, 'test-bucket', str(temp_file), 'test.txt')
        result3, _ = s3_manage.verify_worker(s3_client, 'test-bucket', str(temp_file), 'test.txt')

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
        result, _ = s3_manage.verify_worker(s3_client, 'test-bucket', str(special_file), 'file-with_special.chars.txt')

        assert '[VERIFIED]' in result
        assert 'file-with_special.chars.txt' in result

    @mock_aws
    def test_verify_worker_medium_file(self, temp_file_medium):
        """Test verification of a medium-sized file.

        The 10 MiB fixture is uploaded multipart (8 MiB default chunk => 2 parts). 5 MiB
        chunks would also give 2 parts, but the fallback tries every consistent size and
        matches the full ETag, so it deterministically reports VERIFIED.
        """
        # Upload medium file
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(temp_file_medium), 'test-bucket', 'medium.bin')

        # Verify
        result, category = s3_manage.verify_worker(s3_client, 'test-bucket', str(temp_file_medium), 'medium.bin')

        assert '[VERIFIED]' in result
        assert category == 'verified'


class TestMultipartEtagAmbiguity:
    """F-05 regression: a size where several standard chunk sizes yield the same part count."""

    KEY = 'ambiguous.bin'
    SIZE = 9_961_472  # 9.5 MiB: 2 parts under both 8 MiB and 5 MiB chunks

    def _put_8mb_multipart(self, s3_client, body):
        """Store *body* as a 2-part multipart object chunked at 8 MiB (boto3's default)."""
        s3_client.create_bucket(Bucket='test-bucket')
        upload_id = s3_client.create_multipart_upload(Bucket='test-bucket', Key=self.KEY)['UploadId']
        parts = []
        for number, offset in enumerate(range(0, len(body), 8 * MIB), start=1):
            resp = s3_client.upload_part(
                Bucket='test-bucket', Key=self.KEY, UploadId=upload_id,
                PartNumber=number, Body=body[offset:offset + 8 * MIB],
            )
            parts.append({'PartNumber': number, 'ETag': resp['ETag']})
        s3_client.complete_multipart_upload(
            Bucket='test-bucket', Key=self.KEY, UploadId=upload_id,
            MultipartUpload={'Parts': parts},
        )

    @mock_aws
    def test_identical_file_verifies_against_8mb_multipart_etag(self, temp_dir):
        """The old code guessed 5 MiB here and reported DIFFERENT for a byte-identical file."""
        body = b'q' * self.SIZE
        s3_client = boto3.client('s3', region_name='us-east-1')
        self._put_8mb_multipart(s3_client, body)

        etag = s3_client.head_object(Bucket='test-bucket', Key=self.KEY)['ETag'].strip('"')
        assert etag.endswith('-2')

        local = temp_dir / self.KEY
        local.write_bytes(body)

        result, category = s3_manage.verify_worker(s3_client, 'test-bucket', str(local), self.KEY)
        assert '[VERIFIED]' in result
        assert category == 'verified'

    @mock_aws
    def test_modified_file_reports_different(self, temp_dir):
        """A same-length change to the object is still detected once guessing is removed."""
        body = b'q' * self.SIZE
        s3_client = boto3.client('s3', region_name='us-east-1')
        self._put_8mb_multipart(s3_client, body)

        local = temp_dir / self.KEY
        local.write_bytes(body[:-1] + b'Z')

        result, category = s3_manage.verify_worker(s3_client, 'test-bucket', str(local), self.KEY)
        assert '[DIFFERENT]' in result
        assert category == 'different'

    @mock_aws
    def test_verify_download_file_helper_matches(self, temp_dir):
        """The download-side verifier reproduces the same ETag."""
        body = b'q' * self.SIZE
        s3_client = boto3.client('s3', region_name='us-east-1')
        self._put_8mb_multipart(s3_client, body)

        head = s3_client.head_object(Bucket='test-bucket', Key=self.KEY)
        local = temp_dir / self.KEY
        local.write_bytes(body)

        category, details = s3_manage._verify_download_file(
            str(local), head['ETag'].strip('"'), head['ContentLength'],
        )
        assert (category, details) == ('verified', '')


class TestNonStandardChunkSizeVerification:
    """A correct object uploaded elsewhere with a non-standard multipart chunk size cannot
    be verified by ETag alone, but a recorded SHA-256 rescues it."""

    KEY = 'weird.bin'
    SIZE = 15 * MIB          # 3 parts at a non-standard 6 MiB chunk size
    PART = 6 * MIB

    def _put_multipart(self, s3_client, body, part_size, metadata=None):
        s3_client.create_bucket(Bucket='test-bucket')
        kwargs = {'Bucket': 'test-bucket', 'Key': self.KEY}
        if metadata:
            kwargs['Metadata'] = metadata
        upload_id = s3_client.create_multipart_upload(**kwargs)['UploadId']
        parts = []
        for number, offset in enumerate(range(0, len(body), part_size), start=1):
            resp = s3_client.upload_part(
                Bucket='test-bucket', Key=self.KEY, UploadId=upload_id,
                PartNumber=number, Body=body[offset:offset + part_size],
            )
            parts.append({'PartNumber': number, 'ETag': resp['ETag']})
        s3_client.complete_multipart_upload(
            Bucket='test-bucket', Key=self.KEY, UploadId=upload_id,
            MultipartUpload={'Parts': parts},
        )

    @mock_aws
    def test_no_recorded_checksum_reports_different(self, temp_dir):
        body = b'w' * self.SIZE
        s3_client = boto3.client('s3', region_name='us-east-1')
        self._put_multipart(s3_client, body, self.PART)

        etag = s3_client.head_object(Bucket='test-bucket', Key=self.KEY)['ETag'].strip('"')
        assert etag.endswith('-3')

        local = temp_dir / self.KEY
        local.write_bytes(body)          # byte-identical, yet unverifiable by ETag alone

        result, category = s3_manage.verify_worker(s3_client, 'test-bucket', str(local), self.KEY)
        assert '[DIFFERENT]' in result
        assert 'multipart mismatch' in result
        assert category == 'different'

    @mock_aws
    def test_recorded_checksum_verifies_the_same_object(self, temp_dir):
        body = b'w' * self.SIZE
        s3_client = boto3.client('s3', region_name='us-east-1')
        self._put_multipart(s3_client, body, self.PART,
                            metadata={'bl-content-sha256': hashlib.sha256(body).hexdigest()})

        local = temp_dir / self.KEY
        local.write_bytes(body)

        result, category = s3_manage.verify_worker(s3_client, 'test-bucket', str(local), self.KEY)
        assert '[VERIFIED]' in result
        assert category == 'verified'


class TestContentSha256:
    """Test the SHA-256 content checksum: recorded on upload, preferred on verify/download."""

    def test_calculate_file_sha256_matches_hashlib(self, temp_dir):
        data = b"the quick brown fox\n" * 1000
        f = temp_dir / "f.bin"
        f.write_bytes(data)
        assert s3_manage.calculate_file_sha256(str(f)) == hashlib.sha256(data).hexdigest()

    def test_calculate_file_sha256_empty_and_chunked(self, temp_dir):
        empty = temp_dir / "empty.bin"
        empty.touch()
        assert s3_manage.calculate_file_sha256(str(empty)) == hashlib.sha256(b"").hexdigest()

        spanning = temp_dir / "spanning.bin"
        data = b"y" * (5 * MIB + 123)  # spans many 8 KiB read blocks
        spanning.write_bytes(data)
        assert s3_manage.calculate_file_sha256(str(spanning)) == hashlib.sha256(data).hexdigest()

    def test_calculate_file_sha256_deterministic(self, temp_dir):
        f = temp_dir / "f.bin"
        f.write_bytes(b"abc")
        assert s3_manage.calculate_file_sha256(str(f)) == s3_manage.calculate_file_sha256(str(f))

    @pytest.mark.parametrize("value", [
        None, "", "xyz", "g" * 64, "a" * 63, "a" * 65, "  " + "a" * 62,
    ])
    def test_valid_sha256_rejects_malformed(self, value):
        assert s3_manage._valid_sha256(value) is None

    def test_valid_sha256_accepts_and_lowercases(self):
        digest = hashlib.sha256(b"x").hexdigest()
        assert s3_manage._valid_sha256(digest.upper()) == digest

    @mock_aws
    def test_upload_worker_records_sha256_metadata(self, temp_file):
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        s3_manage.upload_worker(s3_client, 'test-bucket', str(temp_file), 'test.txt', dry_run=False)

        head = s3_client.head_object(Bucket='test-bucket', Key='test.txt')
        assert head['Metadata']['bl-content-sha256'] == s3_manage.calculate_file_sha256(str(temp_file))

    @mock_aws
    def test_verify_worker_uses_recorded_sha256(self, temp_file):
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_manage.upload_worker(s3_client, 'test-bucket', str(temp_file), 'test.txt', dry_run=False)

        result, category = s3_manage.verify_worker(s3_client, 'test-bucket', str(temp_file), 'test.txt')
        assert '[VERIFIED]' in result
        assert category == 'verified'

    @mock_aws
    def test_recorded_sha256_beats_ambiguous_multipart_etag(self, temp_dir):
        """A ~9.5 MiB upload (ambiguous 5/8 MiB) verifies via SHA-256, not ETag guessing."""
        src = temp_dir / "src" / "big.bin"
        src.parent.mkdir()
        src.write_bytes(b"q" * 9_961_472)

        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_manage.upload_worker(s3_client, 'test-bucket', str(src), 'big.bin', dry_run=False)

        etag = s3_client.head_object(Bucket='test-bucket', Key='big.bin')['ETag'].strip('"')
        assert etag.endswith('-2')  # pinned 8 MiB layout => 2 parts

        result, category = s3_manage.verify_worker(s3_client, 'test-bucket', str(src), 'big.bin')
        assert '[VERIFIED]' in result
        assert category == 'verified'

    @mock_aws
    def test_verify_worker_detects_tampered_content(self, temp_dir):
        """The recorded SHA-256 does not match the local bytes => DIFFERENT."""
        local = temp_dir / "data.bin"
        local.write_bytes(b"real content")

        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.put_object(
            Bucket='test-bucket', Key='data.bin', Body=b"real content",
            Metadata={'bl-content-sha256': hashlib.sha256(b"different bytes entirely").hexdigest()},
        )

        result, category = s3_manage.verify_worker(s3_client, 'test-bucket', str(local), 'data.bin')
        assert '[DIFFERENT]' in result
        assert 'SHA-256 mismatch' in result
        assert category == 'different'

    @mock_aws
    def test_malformed_metadata_falls_back_to_etag(self, temp_dir):
        local = temp_dir / "data.bin"
        local.write_bytes(b"x")

        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.put_object(
            Bucket='test-bucket', Key='data.bin', Body=b"x",
            Metadata={'bl-content-sha256': 'not-a-valid-digest'},
        )

        result, category = s3_manage.verify_worker(s3_client, 'test-bucket', str(local), 'data.bin')
        assert '[VERIFIED]' in result
        assert category == 'verified'

    @mock_aws
    def test_verify_worker_without_metadata_uses_etag(self, temp_file):
        """Legacy objects (no recorded SHA-256) still verify via the ETag path."""
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'test.txt')  # no ExtraArgs

        head = s3_client.head_object(Bucket='test-bucket', Key='test.txt')
        assert 'bl-content-sha256' not in head.get('Metadata', {})

        result, category = s3_manage.verify_worker(s3_client, 'test-bucket', str(temp_file), 'test.txt')
        assert '[VERIFIED]' in result
        assert category == 'verified'

    @mock_aws
    def test_download_worker_verifies_via_sha256(self, temp_dir):
        src = temp_dir / "src" / "f.bin"
        src.parent.mkdir()
        src.write_bytes(b"payload bytes")

        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_manage.upload_worker(s3_client, 'test-bucket', str(src), 'f.bin', dry_run=False)

        dest = temp_dir / "dest"
        dest.mkdir()
        message, size, category = s3_manage.download_worker(
            s3_client, 'test-bucket', 'f.bin', str(dest), dry_run=False,
        )
        assert category == 'downloaded'
        assert '[VERIFIED]' in message
        assert (dest / 'f.bin').read_bytes() == b"payload bytes"

    @mock_aws
    def test_download_worker_rejects_sha256_mismatch(self, temp_dir):
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.put_object(
            Bucket='test-bucket', Key='f.bin', Body=b"payload bytes",
            Metadata={'bl-content-sha256': hashlib.sha256(b"other").hexdigest()},
        )

        dest = temp_dir / "dest"
        dest.mkdir()
        message, size, category = s3_manage.download_worker(
            s3_client, 'test-bucket', 'f.bin', str(dest), dry_run=False,
        )
        assert category == 'different'
        assert 'SHA-256 mismatch' in message
        assert not (dest / 'f.bin').exists()


class TestDownloadPathSafety:
    """Test platform-independent S3 key and destination validation."""

    def test_safe_nested_key_and_folder_marker(self):
        components, is_marker = s3_manage.validate_s3_key_components('safe/nested/file.txt')
        assert components == ('safe', 'nested', 'file.txt')
        assert is_marker is False

        components, is_marker = s3_manage.validate_s3_key_components('safe/folder/')
        assert components == ('safe', 'folder')
        assert is_marker is True

    @pytest.mark.parametrize('s3_key', [
        '', '/', '///', '.', '..', '../', '../escape.txt', '../../escape/',
        '/absolute/path', '/etc/passwd', '//server/share', 'foo//bar', 'foo//',
        'foo/../bar', 'foo/./bar', '\\server\\share', 'C:\\escape',
        'C:/escape', '\\', 'foo:bar', 'control/\x1bname', 'nul/\x00name',
        'bad<name', 'bad>name', 'bad"name', 'bad|name', 'bad?name',
        'bad*name', 'CON', 'CON/', 'con.txt', 'NUL .txt', 'CONIN$', 'CONOUT$',
        'COM1', 'COM¹', 'LPT9', 'LPT³', 'foo.', 'foo ',
    ])
    def test_rejects_hostile_keys(self, s3_key):
        with pytest.raises(s3_manage.UnsafeDownloadPathError):
            s3_manage.validate_s3_key_components(s3_key)

    def test_resolve_download_path_stays_below_root(self, temp_dir):
        components, _ = s3_manage.validate_s3_key_components('a/b/file.txt')
        root = s3_manage.resolve_download_root(str(temp_dir), create=False)
        assert s3_manage.resolve_download_path(root, components) == os.path.join(
            root, 'a', 'b', 'file.txt'
        )

    def test_windows_reparse_attribute_is_rejected(self, monkeypatch):
        fake_stat = type('Stat', (), {
            'st_mode': s3_manage.stat.S_IFREG,
            'st_file_attributes': 0x400,
        })()
        monkeypatch.setattr(s3_manage.os.path, 'islink', lambda path: False)
        assert s3_manage.is_hostile_reparse('/unused', fake_stat) is True

    @pytest.mark.parametrize('keys', [
        ['README', 'readme'],
        ['café.txt', 'cafe\u0301.txt'],
        ['a', 'a/b'],
    ])
    def test_preflight_rejects_destination_namespace_collisions(self, keys):
        objects = [{'Key': key, 'Size': 1} for key in keys]
        queue, markers, errors = s3_manage.preflight_download_objects(objects)
        assert queue == [(key, 1) for key in keys]
        assert markers == []
        assert set(errors) == set(keys)


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
        assert category in ['downloaded', 'verified', 'different', 'error', 'skipped']

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
        """A pre-existing medium file at the destination verifies against its multipart ETag."""
        # Setup: the fixture already wrote temp_dir/medium.bin, which is the download target
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(temp_file_medium), 'test-bucket', 'medium.bin')

        # Existing local file -> verified in place, nothing downloaded
        message, size, category = s3_manage.download_worker(s3_client, 'test-bucket', 'medium.bin', str(temp_dir), dry_run=False)

        # The multipart ETag is reproduced exactly (8 MiB tried before 5 MiB), so it verifies
        assert category == 'verified'
        assert '[VERIFIED]' in message
        assert size == 0

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

    def test_hostile_key_does_not_escape_root(self, temp_dir):
        download_root = temp_dir / 'downloads'
        sentinel = temp_dir / 'sentinel.txt'
        sentinel.write_text('unchanged')

        message, size, category = s3_manage.download_worker(
            object(), 'test-bucket', '../sentinel.txt', str(download_root)
        )

        assert category == 'error'
        assert size == 0
        assert repr('../sentinel.txt') in message
        assert sentinel.read_text() == 'unchanged'
        assert not download_root.exists()

    @mock_aws
    def test_folder_marker_is_skipped_by_direct_worker(self, temp_dir):
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.put_object(Bucket='test-bucket', Key='folder/', Body=b'')

        message, size, category = s3_manage.download_worker(
            s3_client, 'test-bucket', 'folder/', str(temp_dir)
        )

        assert '[SKIPPED]' in message
        assert size == 0
        assert category == 'skipped'
        assert not (temp_dir / 'folder').exists()

    @pytest.mark.skipif(sys.platform == 'win32', reason='Symlink creation is not reliable on Windows')
    @mock_aws
    def test_existing_symlink_file_is_rejected(self, temp_dir):
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.put_object(Bucket='test-bucket', Key='file.txt', Body=b's3')

        download_root = temp_dir / 'downloads'
        download_root.mkdir()
        sentinel = temp_dir / 'sentinel.txt'
        sentinel.write_text('unchanged')
        (download_root / 'file.txt').symlink_to(sentinel)

        message, _, category = s3_manage.download_worker(
            s3_client, 'test-bucket', 'file.txt', str(download_root)
        )

        assert category == 'error'
        assert 'reparse point' in message
        assert sentinel.read_text() == 'unchanged'

    @pytest.mark.parametrize('outside_file_exists', [False, True])
    @pytest.mark.skipif(sys.platform == 'win32', reason='Symlink creation is not reliable on Windows')
    @mock_aws
    def test_symlink_parent_is_rejected(self, temp_dir, outside_file_exists):
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.put_object(Bucket='test-bucket', Key='link/file.txt', Body=b's3')

        download_root = temp_dir / 'downloads'
        outside = temp_dir / 'outside'
        download_root.mkdir()
        outside.mkdir()
        if outside_file_exists:
            (outside / 'file.txt').write_text('unchanged')
        (download_root / 'link').symlink_to(outside, target_is_directory=True)

        message, _, category = s3_manage.download_worker(
            s3_client, 'test-bucket', 'link/file.txt', str(download_root)
        )

        assert category == 'error'
        assert 'reparse point' in message
        if outside_file_exists:
            assert (outside / 'file.txt').read_text() == 'unchanged'
        else:
            assert not (outside / 'file.txt').exists()

    @pytest.mark.skipif(sys.platform == 'win32', reason='FIFO creation is not supported on Windows')
    @mock_aws
    def test_special_file_destination_is_rejected(self, temp_dir):
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.put_object(Bucket='test-bucket', Key='pipe', Body=b's3')
        os.mkfifo(temp_dir / 'pipe')

        message, _, category = s3_manage.download_worker(
            s3_client, 'test-bucket', 'pipe', str(temp_dir)
        )

        assert category == 'error'
        assert 'not a regular file' in message

    @pytest.mark.parametrize('failure_kind', ['download', 'verify', 'replace'])
    @mock_aws
    def test_failed_atomic_download_leaves_no_output(
            self, temp_dir, monkeypatch, failure_kind):
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.put_object(Bucket='test-bucket', Key='file.txt', Body=b'content')
        download_root = temp_dir / 'downloads'
        download_root.mkdir()

        if failure_kind == 'download':
            def fail_download(*args, **kwargs):
                raise OSError('download failed')
            monkeypatch.setattr(s3_client, 'download_fileobj', fail_download)
        elif failure_kind == 'verify':
            monkeypatch.setattr(
                s3_manage, '_verify_download_file',
                lambda *args, **kwargs: ('different', 'forced mismatch')
            )
        else:
            def fail_replace(*args, **kwargs):
                raise OSError('replace failed')
            monkeypatch.setattr(s3_manage.os, 'replace', fail_replace)

        message, _, category = s3_manage.download_worker(
            s3_client, 'test-bucket', 'file.txt', str(download_root)
        )

        assert category in ('error', 'different')
        assert '[ERROR]' in message or '[DIFFERENT]' in message
        assert not (download_root / 'file.txt').exists()
        assert list(download_root.glob('.s3dl-*')) == []

    @pytest.mark.skipif(os.name == 'nt', reason='POSIX permissions are not portable to Windows')
    @mock_aws
    def test_published_file_retains_private_temp_mode(self, temp_dir):
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.put_object(Bucket='test-bucket', Key='nested/file.txt', Body=b'content')

        message, _, category = s3_manage.download_worker(
            s3_client, 'test-bucket', 'nested/file.txt', str(temp_dir)
        )

        downloaded_file = temp_dir / 'nested' / 'file.txt'
        assert category == 'downloaded'
        assert '[VERIFIED]' in message
        assert s3_manage.stat.S_IMODE(downloaded_file.stat().st_mode) == 0o600

    @pytest.mark.skipif(sys.platform == 'win32', reason='Symlink creation is not reliable on Windows')
    @mock_aws
    def test_download_root_symlink_is_allowed(self, temp_dir):
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.put_object(Bucket='test-bucket', Key='file.txt', Body=b'content')

        real_root = temp_dir / 'real-root'
        link_root = temp_dir / 'link-root'
        real_root.mkdir()
        link_root.symlink_to(real_root, target_is_directory=True)

        _, _, category = s3_manage.download_worker(
            s3_client, 'test-bucket', 'file.txt', str(link_root)
        )

        assert category == 'downloaded'
        assert (real_root / 'file.txt').read_bytes() == b'content'


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
            'max_keys': None,
            'paths_only': False
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
            'max_keys': None,
            'paths_only': False
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
            'max_keys': None,
            'paths_only': False
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
            'max_keys': None,
            'paths_only': False
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
            'max_keys': 3,
            'paths_only': False
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
            'max_keys': None,
            'paths_only': False
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
            'max_keys': None,
            'paths_only': False
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

        # Verify columns are present in correct order: SIZE, LAST MODIFIED, FILE PATH
        assert 'SIZE' in header_line
        assert 'LAST MODIFIED' in header_line
        assert 'FILE PATH' in header_line
        size_pos = header_line.index('SIZE')
        modified_pos = header_line.index('LAST MODIFIED')
        path_pos = header_line.index('FILE PATH')
        assert size_pos < modified_pos < path_pos

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
            'max_keys': None,
            'paths_only': False
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
            'max_keys': None,
            'paths_only': False
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
            'max_keys': None,
            'paths_only': False
        })()

        # Execute
        exit_code = s3_manage.command_list(args, s3_client=s3_client)

        # Verify error message and nonzero exit code
        assert exit_code == 1
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
            'max_keys': None,
            'paths_only': False
        })()

        # Execute
        s3_manage.command_list(args, s3_client=s3_client)

        # Verify timestamp format (YYYY-MM-DD HH:MM:SS)
        captured = capsys.readouterr()
        import re
        timestamp_pattern = r'\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}'
        assert re.search(timestamp_pattern, captured.out) is not None

    @mock_aws
    def test_list_paths_only_single_file(self, temp_file, capsys):
        """Test --paths-only outputs only file paths, one per line."""
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'test.txt')

        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'prefix': None,
            'max_keys': None,
            'paths_only': True
        })()

        s3_manage.command_list(args, s3_client=s3_client)

        captured = capsys.readouterr()
        assert captured.out.strip() == "test.txt"

    @mock_aws
    def test_list_paths_only_multiple_files(self, temp_dir, capsys):
        """Test --paths-only with multiple files outputs one path per line."""
        for i in range(3):
            (temp_dir / f"file{i}.txt").write_text(f"content {i}")

        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        for i in range(3):
            s3_client.upload_file(str(temp_dir / f"file{i}.txt"), 'test-bucket', f"file{i}.txt")

        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'prefix': None,
            'max_keys': None,
            'paths_only': True
        })()

        s3_manage.command_list(args, s3_client=s3_client)

        captured = capsys.readouterr()
        lines = captured.out.strip().split('\n')
        assert len(lines) == 3
        assert "file0.txt" in lines
        assert "file1.txt" in lines
        assert "file2.txt" in lines

    @mock_aws
    def test_list_paths_only_no_header_or_summary(self, temp_file, capsys):
        """Test --paths-only suppresses header, separator, summary, and intro messages."""
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'test.txt')

        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'prefix': None,
            'max_keys': None,
            'paths_only': True
        })()

        s3_manage.command_list(args, s3_client=s3_client)

        captured = capsys.readouterr()
        assert "Listing objects" not in captured.out
        assert "FILE PATH" not in captured.out
        assert "SIZE" not in captured.out
        assert "LAST MODIFIED" not in captured.out
        assert "Total:" not in captured.out
        assert "---" not in captured.out

    @mock_aws
    def test_list_paths_only_empty_bucket(self, capsys):
        """Test --paths-only with empty bucket produces no output."""
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'prefix': None,
            'max_keys': None,
            'paths_only': True
        })()

        s3_manage.command_list(args, s3_client=s3_client)

        captured = capsys.readouterr()
        assert captured.out.strip() == ""

    @mock_aws
    def test_list_paths_only_with_prefix(self, temp_dir, capsys):
        """Test --paths-only respects prefix filter."""
        (temp_dir / "data1.txt").write_text("data1")
        (temp_dir / "log1.txt").write_text("log1")

        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(temp_dir / "data1.txt"), 'test-bucket', 'data1.txt')
        s3_client.upload_file(str(temp_dir / "log1.txt"), 'test-bucket', 'log1.txt')

        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'prefix': 'data',
            'max_keys': None,
            'paths_only': True
        })()

        s3_manage.command_list(args, s3_client=s3_client)

        captured = capsys.readouterr()
        assert captured.out.strip() == "data1.txt"
        assert "log1.txt" not in captured.out


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
            'url_prefix': 'https://example.com/'
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
            'url_prefix': 'https://example.com/'
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
    def test_indexupload_records_checksum_metadata(self, temp_file):
        """Every object this tool writes carries a bl-content-sha256, index files included."""
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'test.txt')

        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'prefix': None,
            'url_prefix': 'https://example.com/'
        })()
        s3_manage.command_indexupload(args, s3_client=s3_client)

        for key in ('index.html', 'index.md'):
            meta = s3_client.head_object(Bucket='test-bucket', Key=key)['Metadata']
            digest = meta.get('bl-content-sha256', '')
            assert len(digest) == 64 and all(c in '0123456789abcdef' for c in digest)

    @mock_aws
    def test_indexupload_sets_content_type_so_the_index_renders(self, temp_file):
        """Without an explicit ContentType S3 serves binary/octet-stream and browsers download."""
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'test.txt')

        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'prefix': None,
            'url_prefix': 'https://example.com/'
        })()
        assert s3_manage.command_indexupload(args, s3_client=s3_client) == 0

        expected = {
            'index.html': 'text/html; charset=utf-8',
            'index.md': 'text/markdown; charset=utf-8',
        }

        for key, content_type in expected.items():
            head = s3_client.head_object(Bucket='test-bucket', Key=key)
            assert head['ContentType'] == content_type
            assert head['CacheControl'] == 'no-cache'

    @mock_aws
    def test_indexupload_html_carries_a_restrictive_csp(self, temp_file):
        """Setting ContentType re-enables rendering, so the CSP must ship with it.

        S3 cannot set arbitrary response headers on an object, so the policy is delivered as a
        meta element inside the document itself.
        """
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.upload_file(str(temp_file), 'test-bucket', 'test.txt')

        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'prefix': None,
            'url_prefix': 'https://example.com/'
        })()
        assert s3_manage.command_indexupload(args, s3_client=s3_client) == 0

        body = s3_client.get_object(Bucket='test-bucket', Key='index.html')['Body'].read().decode('utf-8')

        assert '<meta http-equiv="Content-Security-Policy"' in body
        assert "default-src 'none'" in body
        assert "form-action 'none'" in body
        assert "base-uri 'none'" in body

        # the page must remain self-contained for that policy to be satisfiable
        assert '<script' not in body.lower()
        assert '<img' not in body.lower()

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
            'url_prefix': 'https://example.com/'
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
            'url_prefix': 'https://example.com/'
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
            'url_prefix': 'https://example.com/'
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
            'url_prefix': 'https://example.com/'
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
            'url_prefix': 'https://example.com/'
        })()

        # Execute
        exit_code = s3_manage.command_indexupload(args, s3_client=s3_client)

        # Verify error message and nonzero exit code
        assert exit_code == 1
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
            'url_prefix': 'https://example.com/'
        })()

        # Execute
        s3_manage.command_indexupload(args, s3_client=s3_client)

        # Verify HTML content shows nested paths
        index_obj = s3_client.get_object(Bucket='test-bucket', Key='index.html')
        index_content = index_obj['Body'].read().decode('utf-8')
        assert 'data/2024/01/file.txt' in index_content
        assert 'logs/app/debug.log' in index_content
        assert 'Total: 2 objects' in index_content

    def test_indexupload_invalid_prefix_injected_client(self, capsys):
        """Invalid prefix exits before listing, generating, or uploading."""
        s3_client = object()
        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'prefix': None,
            'url_prefix': 'javascript:alert(1)'
        })()

        exit_code = s3_manage.command_indexupload(args, s3_client=s3_client)
        assert exit_code == 1
        captured = capsys.readouterr()
        assert "[ERROR]" in captured.out

    def test_indexupload_invalid_prefix_does_not_construct_client(self, capsys, monkeypatch):
        """Invalid prefix exits before boto3.client is constructed."""
        def fail_client(*_args, **_kwargs):
            raise AssertionError("boto3.client must not be called")

        monkeypatch.setattr(s3_manage.boto3, "client", fail_client)
        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'prefix': None,
            'url_prefix': 'javascript:alert(1)',
            'endpoint_url': 'https://s3.example.com',
            'access_key': 'key',
            'secret_key': 'secret'
        })()

        exit_code = s3_manage.command_indexupload(args, s3_client=None)
        assert exit_code == 1
        captured = capsys.readouterr()
        assert "[ERROR]" in captured.out

    def test_indexupload_invalid_prefix_does_not_generate_or_upload(self, capsys, monkeypatch):
        """Invalid prefix does not call generators or upload_file."""
        class FakeClient:
            def list_objects_v2(self, **_kwargs):
                raise AssertionError("listing must not occur")

            def upload_file(self, *_args, **_kwargs):
                raise AssertionError("upload must not occur")

        monkeypatch.setattr(s3_manage, "generate_html_index", lambda *_a, **_k: (_ for _ in ()).throw(AssertionError("html")))
        monkeypatch.setattr(s3_manage, "generate_markdown_index", lambda *_a, **_k: (_ for _ in ()).throw(AssertionError("md")))
        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'prefix': None,
            'url_prefix': 'https://example.com/)<img src=x>'
        })()

        exit_code = s3_manage.command_indexupload(args, s3_client=FakeClient())
        assert exit_code == 1
        captured = capsys.readouterr()
        assert "[ERROR]" in captured.out

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
        exit_code = s3_manage.command_download(args, s3_client=s3_client)
        assert exit_code == 0

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
        exit_code = s3_manage.command_download(args, s3_client=s3_client)
        assert exit_code == 1

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
        exit_code = s3_manage.command_download(args, s3_client=s3_client)
        assert exit_code == 1

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

    @staticmethod
    def _security_test_args(local_folder, dry_run=False):
        return type('Args', (), {
            'bucket_name': 'test-bucket',
            'prefix': None,
            'local_folder': str(local_folder),
            'dry_run': dry_run,
            'max_threads': 2,
        })()

    @pytest.mark.parametrize('s3_key,dry_run', [
        ('../outside.txt', False),
        ('../', False),
        ('../outside.txt', True),
    ])
    def test_path_unsafe_preflight_aborts_without_mutation(
            self, temp_dir, capsys, monkeypatch, s3_key, dry_run):
        download_root = temp_dir / 'downloads'
        objects = [
            {'Key': 'safe.txt', 'Size': 1},
            {'Key': s3_key, 'Size': 0},
        ]
        worker_called = False

        monkeypatch.setattr(
            s3_manage, 'paginate_s3_objects',
            lambda *args, **kwargs: iter(objects)
        )

        def unexpected_worker(*args, **kwargs):
            nonlocal worker_called
            worker_called = True
            raise AssertionError('worker must not run after failed preflight')

        monkeypatch.setattr(s3_manage, 'download_worker', unexpected_worker)
        args = self._security_test_args(download_root, dry_run=dry_run)

        exit_code = s3_manage.command_download(args, s3_client=object())
        assert exit_code == 1

        assert worker_called is False
        assert not download_root.exists()
        assert repr(s3_key) in capsys.readouterr().out

    @pytest.mark.parametrize('keys', [
        ['README', 'readme'],
        ['café.txt', 'cafe\u0301.txt'],
        ['a', 'a/b'],
    ])
    def test_collision_preflight_aborts_without_mutation(
            self, temp_dir, capsys, monkeypatch, keys):
        download_root = temp_dir / 'downloads'
        objects = [{'Key': key, 'Size': 1} for key in keys]
        monkeypatch.setattr(
            s3_manage, 'paginate_s3_objects',
            lambda *args, **kwargs: iter(objects)
        )
        monkeypatch.setattr(
            s3_manage, 'download_worker',
            lambda *args, **kwargs: pytest.fail('worker must not run')
        )
        args = self._security_test_args(download_root)

        exit_code = s3_manage.command_download(args, s3_client=object())
        assert exit_code == 1

        assert not download_root.exists()
        output = capsys.readouterr().out
        for key in keys:
            assert repr(key) in output

    def test_zero_byte_folder_marker_is_skipped(
            self, temp_dir, capsys, monkeypatch):
        download_root = temp_dir / 'downloads'
        monkeypatch.setattr(
            s3_manage, 'paginate_s3_objects',
            lambda *args, **kwargs: iter([{'Key': 'folder/', 'Size': 0}])
        )
        args = self._security_test_args(download_root)

        s3_manage.command_download(args, s3_client=object())

        output = capsys.readouterr().out
        assert '[SKIPPED]' in output
        assert 'Skipped (folder markers): 1' in output
        assert not download_root.exists()

    def test_nonzero_trailing_slash_object_aborts(
            self, temp_dir, capsys, monkeypatch):
        download_root = temp_dir / 'downloads'
        monkeypatch.setattr(
            s3_manage, 'paginate_s3_objects',
            lambda *args, **kwargs: iter([{'Key': 'folder/', 'Size': 1}])
        )
        args = self._security_test_args(download_root)

        exit_code = s3_manage.command_download(args, s3_client=object())
        assert exit_code == 1

        assert repr('folder/') in capsys.readouterr().out
        assert not download_root.exists()

    @mock_aws
    def test_shared_parent_downloads_succeed(self, temp_dir, capsys):
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')
        s3_client.put_object(Bucket='test-bucket', Key='shared/a.txt', Body=b'a')
        s3_client.put_object(Bucket='test-bucket', Key='shared/b.txt', Body=b'b')
        download_root = temp_dir / 'downloads'
        args = self._security_test_args(download_root)

        s3_manage.command_download(args, s3_client=s3_client)

        assert (download_root / 'shared' / 'a.txt').read_bytes() == b'a'
        assert (download_root / 'shared' / 'b.txt').read_bytes() == b'b'
        assert 'Errors: 0' in capsys.readouterr().out

    def test_post_preflight_404_may_leave_created_root(
            self, temp_dir, monkeypatch):
        download_root = temp_dir / 'downloads'
        monkeypatch.setattr(
            s3_manage, 'paginate_s3_objects',
            lambda *args, **kwargs: iter([{'Key': 'gone.txt', 'Size': 1}])
        )

        class MissingObjectClient:
            def head_object(self, **kwargs):
                raise s3_manage.ClientError(
                    {
                        'Error': {'Code': '404', 'Message': 'Not Found'},
                        'ResponseMetadata': {'HTTPStatusCode': 404},
                    },
                    'HeadObject'
                )

        args = self._security_test_args(download_root)
        exit_code = s3_manage.command_download(args, s3_client=MissingObjectClient())
        assert exit_code == 1

        assert download_root.is_dir()
        assert list(download_root.iterdir()) == []


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
        exit_code = s3_manage.command_upload(args, s3_client=s3_client)
        assert exit_code == 0

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
        exit_code = s3_manage.command_upload(args, s3_client=s3_client)
        assert exit_code == 0

        # Verify statistics in output
        captured = capsys.readouterr()
        assert 'Total files scanned: 3' in captured.out
        assert 'Files uploaded: 2' in captured.out
        assert 'Files skipped (already exist): 1' in captured.out
        assert 'Failed: 0' in captured.out
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

        # Execute command - should report failure and return EXIT_FAILURE
        exit_code = s3_manage.command_upload(args, s3_client=s3_client)
        assert exit_code == 1

        # Verify error message in output
        captured = capsys.readouterr()
        assert '[FAILURE]' in captured.out or 'ERROR' in captured.out
        assert 'Failed: 1' in captured.out

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
        exit_code = s3_manage.command_verify(args, s3_client=s3_client)
        assert exit_code == 0

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
        exit_code = s3_manage.command_verify(args, s3_client=s3_client)
        assert exit_code == 1

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

        # Execute command - a missing remote object is a failure
        exit_code = s3_manage.command_verify(args, s3_client=s3_client)
        assert exit_code == 1

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
        exit_code = s3_manage.command_verify(args, s3_client=s3_client)
        assert exit_code == 1

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
        exit_code = s3_manage.command_verify(args, s3_client=s3_client)
        assert exit_code == 1

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
        exit_code = s3_manage.command_verify(args, s3_client=s3_client)
        assert exit_code == 1

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

        # Execute command - should return EXIT_SUCCESS
        exit_code = s3_manage.command_verify(args, s3_client=s3_client)
        assert exit_code == 0

        # Verify output
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

        # Execute command - should return EXIT_FAILURE
        exit_code = s3_manage.command_verify(args, s3_client=s3_client)
        assert exit_code == 1

    @mock_aws
    def test_verify_exit_code_failure_on_sha256_mismatch(self, temp_dir, capsys):
        """A recorded SHA-256 that does not match the local content exits with code 1."""
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        local_folder = temp_dir / 'verify'
        local_folder.mkdir()
        (local_folder / 'test.txt').write_bytes(b'real content')
        s3_client.put_object(
            Bucket='test-bucket', Key='test.txt', Body=b'real content',
            Metadata={'bl-content-sha256': hashlib.sha256(b'tampered').hexdigest()},
        )

        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'local_folder': str(local_folder),
            'max_threads': 2,
            'allow_hidden_files': False
        })()

        exit_code = s3_manage.command_verify(args, s3_client=s3_client)
        assert exit_code == 1
        assert 'Different (mismatch): 1' in capsys.readouterr().out

    @mock_aws
    def test_verify_exit_code_success_with_recorded_sha256(self, temp_dir, temp_file, capsys):
        """A file round-tripped through command_upload carries its SHA-256 and verifies."""
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        local_folder = temp_dir / 'artifacts'
        local_folder.mkdir()
        shutil.copy(str(temp_file), str(local_folder / 'test.txt'))

        upload_args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'local_folder': str(local_folder),
            'max_threads': 2,
            'allow_hidden_files': False,
            'dry_run': False,
        })()
        assert s3_manage.command_upload(upload_args, s3_client=s3_client) == 0

        verify_args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'local_folder': str(local_folder),
            'max_threads': 2,
            'allow_hidden_files': False
        })()
        exit_code = s3_manage.command_verify(verify_args, s3_client=s3_client)
        assert exit_code == 0
        assert 'Verified (match): 1' in capsys.readouterr().out

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


# ====================================================================================
# Phase 5: Exit code contract (F-04 regression coverage)
#
# Every command_* function must return EXIT_SUCCESS (0) only when every
# requested operation actually succeeded, and EXIT_FAILURE (1) if anything
# failed, was missing, differed, or the input was unusable.
# ====================================================================================

class TestExitCodeContract:
    """Test that every command_* returns the documented exit code, not just a
    printed message, for every failure mode."""

    # --- Invalid/nonexistent bucket: every command must return 1 ---

    @mock_aws
    def test_upload_invalid_bucket_returns_failure(self, temp_dir, temp_file, capsys):
        s3_client = boto3.client('s3', region_name='us-east-1')
        local_folder = temp_dir / 'uploads'
        local_folder.mkdir()
        shutil.copy(str(temp_file), str(local_folder / 'test.txt'))

        args = type('Args', (), {
            'bucket_name': 'nonexistent-bucket',
            'local_folder': str(local_folder),
            'dry_run': False,
            'max_threads': 2,
            'allow_hidden_files': False
        })()

        assert s3_manage.command_upload(args, s3_client=s3_client) == 1

    @mock_aws
    def test_list_invalid_bucket_returns_failure(self, capsys):
        s3_client = boto3.client('s3', region_name='us-east-1')
        args = type('Args', (), {
            'bucket_name': 'nonexistent-bucket',
            'prefix': None,
            'max_keys': None,
            'paths_only': False
        })()

        assert s3_manage.command_list(args, s3_client=s3_client) == 1

    @mock_aws
    def test_verify_invalid_bucket_returns_failure(self, temp_dir, temp_file, capsys):
        s3_client = boto3.client('s3', region_name='us-east-1')
        local_folder = temp_dir / 'verify'
        local_folder.mkdir()
        shutil.copy(str(temp_file), str(local_folder / 'test.txt'))

        args = type('Args', (), {
            'bucket_name': 'nonexistent-bucket',
            'local_folder': str(local_folder),
            'max_threads': 2,
            'allow_hidden_files': False
        })()

        assert s3_manage.command_verify(args, s3_client=s3_client) == 1

    @mock_aws
    def test_indexupload_invalid_bucket_returns_failure(self, capsys):
        s3_client = boto3.client('s3', region_name='us-east-1')
        args = type('Args', (), {
            'bucket_name': 'nonexistent-bucket',
            'prefix': None,
            'url_prefix': 'https://example.com/'
        })()

        assert s3_manage.command_indexupload(args, s3_client=s3_client) == 1

    @mock_aws
    def test_download_invalid_bucket_returns_failure(self, temp_dir, capsys):
        s3_client = boto3.client('s3', region_name='us-east-1')
        args = type('Args', (), {
            'bucket_name': 'nonexistent-bucket',
            'prefix': None,
            'local_folder': str(temp_dir),
            'dry_run': False,
            'max_threads': 2
        })()

        assert s3_manage.command_download(args, s3_client=s3_client) == 1

    # --- Upload worker failures ---

    @mock_aws
    def test_upload_file_failure_returns_failure(self, temp_dir, temp_file, capsys):
        """A single file whose upload_file() raises must fail the whole command."""
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        local_folder = temp_dir / 'uploads'
        local_folder.mkdir()
        good_file = local_folder / 'good.txt'
        shutil.copy(str(temp_file), str(good_file))
        bad_file = local_folder / 'bad.txt'
        shutil.copy(str(temp_file), str(bad_file))

        real_upload_file = s3_client.upload_file

        def flaky_upload_file(filename, bucket, key, *args, **kwargs):
            if key == 'bad.txt':
                raise RuntimeError('simulated upload failure')
            return real_upload_file(filename, bucket, key, *args, **kwargs)

        s3_client.upload_file = flaky_upload_file

        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'local_folder': str(local_folder),
            'dry_run': False,
            'max_threads': 1,
            'allow_hidden_files': False
        })()

        exit_code = s3_manage.command_upload(args, s3_client=s3_client)
        assert exit_code == 1

        captured = capsys.readouterr()
        assert '[FAILURE]' in captured.out
        assert 'Files uploaded: 1' in captured.out
        assert 'Failed: 1' in captured.out

    def test_upload_worker_thread_crash_returns_failure(self, temp_dir, temp_file, capsys, monkeypatch):
        """A worker that raises instead of returning must still fail the command."""
        def crashing_worker(*_args, **_kwargs):
            raise RuntimeError('simulated thread crash')

        monkeypatch.setattr(s3_manage, 'upload_worker', crashing_worker)

        local_folder = temp_dir / 'uploads'
        local_folder.mkdir()
        shutil.copy(str(temp_file), str(local_folder / 'test.txt'))

        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'local_folder': str(local_folder),
            'dry_run': False,
            'max_threads': 1,
            'allow_hidden_files': False
        })()

        exit_code = s3_manage.command_upload(args, s3_client=object())
        assert exit_code == 1

        captured = capsys.readouterr()
        assert '[CRITICAL ERROR]' in captured.out
        assert 'Failed: 1' in captured.out

    # --- Missing remote object ---

    @mock_aws
    def test_verify_missing_remote_object_returns_failure(self, temp_dir, temp_file, capsys):
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        local_folder = temp_dir / 'verify'
        local_folder.mkdir()
        shutil.copy(str(temp_file), str(local_folder / 'test.txt'))

        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'local_folder': str(local_folder),
            'max_threads': 2,
            'allow_hidden_files': False
        })()

        exit_code = s3_manage.command_verify(args, s3_client=s3_client)
        assert exit_code == 1

        captured = capsys.readouterr()
        assert 'Not uploaded to S3: 1' in captured.out

    # --- Failed index/bucket listing (list and indexupload) ---

    def test_indexupload_listing_client_error_returns_failure(self, capsys, monkeypatch):
        def raise_client_error(*_args, **_kwargs):
            raise s3_manage.ClientError(
                {'Error': {'Code': 'AccessDenied', 'Message': 'Access Denied'}},
                'ListObjectsV2'
            )

        monkeypatch.setattr(s3_manage, 'paginate_s3_objects', raise_client_error)
        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'prefix': None,
            'url_prefix': 'https://example.com/'
        })()

        assert s3_manage.command_indexupload(args, s3_client=object()) == 1

    def test_indexupload_listing_unexpected_error_returns_failure(self, capsys, monkeypatch):
        def raise_error(*_args, **_kwargs):
            raise RuntimeError('simulated listing failure')

        monkeypatch.setattr(s3_manage, 'paginate_s3_objects', raise_error)
        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'prefix': None,
            'url_prefix': 'https://example.com/'
        })()

        assert s3_manage.command_indexupload(args, s3_client=object()) == 1

    def test_list_listing_unexpected_error_returns_failure(self, capsys, monkeypatch):
        def raise_error(*_args, **_kwargs):
            raise RuntimeError('simulated listing failure')

        monkeypatch.setattr(s3_manage, 'paginate_s3_objects', raise_error)
        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'prefix': None,
            'max_keys': None,
            'paths_only': False
        })()

        assert s3_manage.command_list(args, s3_client=object()) == 1

    # --- Missing local folder ---

    @mock_aws
    def test_upload_missing_local_folder_returns_failure(self, temp_dir, capsys):
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'local_folder': str(temp_dir / 'does-not-exist'),
            'dry_run': False,
            'max_threads': 2,
            'allow_hidden_files': False
        })()

        exit_code = s3_manage.command_upload(args, s3_client=s3_client)
        assert exit_code == 1
        captured = capsys.readouterr()
        assert '[ERROR]' in captured.out

    @mock_aws
    def test_verify_missing_local_folder_returns_failure(self, temp_dir, capsys):
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'local_folder': str(temp_dir / 'does-not-exist'),
            'max_threads': 2,
            'allow_hidden_files': False
        })()

        exit_code = s3_manage.command_verify(args, s3_client=s3_client)
        assert exit_code == 1
        captured = capsys.readouterr()
        assert '[ERROR]' in captured.out

    @mock_aws
    def test_upload_existing_empty_folder_still_succeeds(self, temp_dir, capsys):
        """An existing-but-empty directory is a legitimate no-op, not a failure."""
        s3_client = boto3.client('s3', region_name='us-east-1')
        s3_client.create_bucket(Bucket='test-bucket')

        local_folder = temp_dir / 'uploads'
        local_folder.mkdir()

        args = type('Args', (), {
            'bucket_name': 'test-bucket',
            'local_folder': str(local_folder),
            'dry_run': False,
            'max_threads': 2,
            'allow_hidden_files': False
        })()

        assert s3_manage.command_upload(args, s3_client=s3_client) == 0


class TestMainDispatch:
    """Test that main() propagates each command's return value to sys.exit()."""

    def _upload_argv(self, local_folder):
        return [
            's3_manage.py', 'upload',
            '--account-id', 'acct',
            '--access-key', 'key',
            '--secret-key', 'secret',
            '--bucket-name', 'test-bucket',
            '--endpoint-url', 'http://127.0.0.1:1',
            '--local-folder', str(local_folder),
        ]

    def test_main_exits_zero_on_command_success(self, temp_dir, monkeypatch):
        monkeypatch.setattr(sys, 'argv', self._upload_argv(temp_dir))
        monkeypatch.setattr(s3_manage, 'command_upload', lambda args: s3_manage.EXIT_SUCCESS)

        with pytest.raises(SystemExit) as exc_info:
            s3_manage.main()
        assert exc_info.value.code == 0

    def test_main_exits_one_on_command_failure(self, temp_dir, monkeypatch):
        monkeypatch.setattr(sys, 'argv', self._upload_argv(temp_dir))
        monkeypatch.setattr(s3_manage, 'command_upload', lambda args: s3_manage.EXIT_FAILURE)

        with pytest.raises(SystemExit) as exc_info:
            s3_manage.main()
        assert exc_info.value.code == 1
