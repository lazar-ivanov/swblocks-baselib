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

import os
import re
import sys
import argparse
import boto3
from boto3.s3.transfer import TransferConfig
import threading
import hashlib
import stat
import tempfile
import time
import unicodedata
import html
import urllib.parse
from botocore.exceptions import ClientError
from concurrent.futures import ThreadPoolExecutor, as_completed

# Configuration will be provided via command-line arguments

# --- EXIT CODES ---
EXIT_SUCCESS = 0
EXIT_FAILURE = 1

# --- CONTENT INTEGRITY ---
# SHA-256 of the local file, recorded on upload as S3 user metadata (surfaces as
# x-amz-meta-bl-content-sha256) and preferred over the ETag when verifying.
CONTENT_SHA256_METADATA_KEY = 'bl-content-sha256'

# --- GENERATED INDEX SERVING ---
# Content types for the generated index objects. Without an explicit type S3 stores them as
# binary/octet-stream, so a browser downloads the index instead of rendering it and the feature
# does not work at all.
#
# Note that setting this re-enables rendering of content which is derived from S3 keys, so it is
# deliberately paired with the meta CSP below and with the output encoding in generate_html_index()
# and _escape_markdown_index_label(). Do not set the content type without them.
INDEX_CONTENT_TYPES = {
    'index.html': 'text/html; charset=utf-8',
    'index.md': 'text/markdown; charset=utf-8',
}

# The index is regenerated on every publish, so a stale cached copy is worse than a re-fetch.
INDEX_CACHE_CONTROL = 'no-cache'

# Defence in depth for the generated HTML index, delivered as a meta element because S3 cannot set
# arbitrary response headers on an object - only Content-Type, Cache-Control and the other
# well-known ones plus x-amz-meta-* user metadata.
#
# The page is entirely self-contained: one inline <style> block, no scripts, no images, no fonts and
# no forms. 'unsafe-inline' is therefore needed for the style element and nothing else is permitted.
# Ordinary link navigation is unaffected by these directives.
#
# NOTE that X-Content-Type-Options: nosniff CANNOT be delivered this way - it is header-only and has
# no meta equivalent. Serving the index through a layer which can set response headers, such as
# CloudFront with a response headers policy, should add it there. With an explicit and correct
# Content-Type set above, sniffing is a much narrower concern than it would be with none.
INDEX_CONTENT_SECURITY_POLICY = (
    "default-src 'none'; style-src 'unsafe-inline'; base-uri 'none'; form-action 'none'"
)

# Pin the multipart layout so an uploaded object has a reproducible ETag rather
# than one that depends on the s3transfer defaults of the installed boto3. These
# values match the current defaults, so objects already in a bucket still verify.
MULTIPART_THRESHOLD_BYTES = 8 * 1024 * 1024
MULTIPART_CHUNKSIZE_BYTES = 8 * 1024 * 1024
TRANSFER_CONFIG = TransferConfig(
    multipart_threshold=MULTIPART_THRESHOLD_BYTES,
    multipart_chunksize=MULTIPART_CHUNKSIZE_BYTES,
)

# --- THREAD SAFE PRINTING ---
# This lock stops threads from writing over each other
print_lock = threading.Lock()

def safe_print(message):
    """Prints a message ensuring no other thread interrupts."""
    with print_lock:
        print(message)

def make_walk_error_handler():
    """Return (onerror, errors) for os.walk(): reports unreadable directories and collects them."""
    errors = []

    def onerror(error):
        errors.append(error)
        print(f"[ERROR] Cannot scan directory: {error.filename} ({error.strerror})")

    return onerror, errors

def classify_tree_entry(path):
    """
    Return the reason a local tree entry must be rejected by upload/verify, or None to accept it.

    Symbolic links (to files or to directories, dangling ones included), junctions, other
    reparse points and non-regular files are rejected, which is the policy bl_tool.py applies
    to the same tree: os.walk() does not descend into a linked directory, so its contents
    would otherwise be silently omitted, and a linked file would be published by content,
    including content which lives outside the local folder.
    """
    try:
        st = os.lstat(path)
    except OSError as exc:
        return f"cannot be examined ({exc.strerror})"
    if is_hostile_reparse(path, st):
        return "symbolic link, junction or reparse point"
    if not stat.S_ISDIR(st.st_mode) and not stat.S_ISREG(st.st_mode):
        return "not a regular file"
    return None

def reject_tree_entry(path, rejected_entries):
    """Report and record an entry which classify_tree_entry() rejects; returns True when rejected."""
    reason = classify_tree_entry(path)
    if reason is None:
        return False
    rejected_entries.append(path)
    print(f"[ERROR] Rejected entry: {path} ({reason})")
    return True

def has_control_characters(text):
    """Return True when the text contains a C0 control character or DEL."""
    return any(ord(ch) < 0x20 or ord(ch) == 0x7F for ch in text)

def format_size(size_bytes):
    """Format size in bytes to human-readable format."""
    for unit in ['B', 'KB', 'MB', 'GB', 'TB']:
        if size_bytes < 1024.0:
            return f"{size_bytes:.2f} {unit}"
        size_bytes /= 1024.0
    return f"{size_bytes:.2f} PB"

def format_duration(seconds):
    """Format duration in seconds to human-readable format (seconds, minutes, or hours)."""
    if seconds < 60:
        return f"{seconds:.2f} seconds"
    elif seconds < 3600:
        return f"{seconds / 60:.2f} minutes"
    else:
        return f"{seconds / 3600:.2f} hours"

def format_speed(size_bytes, elapsed_seconds):
    """
    Format processing speed with auto-adapting units.

    Args:
        size_bytes (int): Total bytes processed
        elapsed_seconds (float): Elapsed time in seconds

    Returns:
        str: Formatted speed (e.g., "45.23 MB/s", "2.34 GB/s")
    """
    if elapsed_seconds <= 0:
        return "0.00 B/s"

    bytes_per_second = size_bytes / elapsed_seconds

    # Auto-select unit based on speed
    for unit in ['B/s', 'KB/s', 'MB/s', 'GB/s', 'TB/s']:
        if bytes_per_second < 1024.0:
            return f"{bytes_per_second:.2f} {unit}"
        bytes_per_second /= 1024.0

    return f"{bytes_per_second:.2f} PB/s"

def paginate_s3_objects(s3_client, bucket_name, prefix=None, max_keys=None):
    """
    Paginate through S3 objects and yield them one at a time.

    This is a reusable helper for list, indexupload, and download commands that need
    to iterate through S3 bucket contents with pagination.

    Args:
        s3_client: boto3 S3 client
        bucket_name (str): S3 bucket name
        prefix (str, optional): Prefix filter for object keys
        max_keys (int, optional): Maximum keys per page (if specified, stops after first page)

    Yields:
        dict: S3 object dict with keys: 'Key', 'Size', 'LastModified', 'ETag', etc.

    Example:
        # Collect all objects
        objects = list(paginate_s3_objects(s3_client, 'my-bucket', prefix='data/'))

        # Process objects without collecting
        for obj in paginate_s3_objects(s3_client, 'my-bucket'):
            print(f"{obj['Key']}: {obj['Size']} bytes")
    """
    list_params = {'Bucket': bucket_name}

    if prefix:
        list_params['Prefix'] = prefix

    if max_keys:
        list_params['MaxKeys'] = max_keys

    continuation_token = None

    while True:
        if continuation_token:
            list_params['ContinuationToken'] = continuation_token

        response = s3_client.list_objects_v2(**list_params)

        # Stop if bucket is empty or no objects match prefix
        if 'Contents' not in response:
            return

        # Yield each object
        for obj in response['Contents']:
            yield obj

        # Check if there are more results
        if not response.get('IsTruncated', False):
            break

        continuation_token = response.get('NextContinuationToken')

        # Stop after first page if max_keys specified
        if max_keys:
            break

# Standard S3 multipart chunk sizes. boto3's 8 MiB default is first so it wins
# any tie when several sizes are consistent with the same part count.
_MIB = 1024 * 1024
_STANDARD_MULTIPART_CHUNK_SIZES = [
    8 * _MIB,
    5 * _MIB,
    16 * _MIB,
    32 * _MIB,
    64 * _MIB,
    128 * _MIB,
    256 * _MIB,
    512 * _MIB,
]

# Upper bound on a single read() while hashing a multipart part, so a large
# chunk size (a single-part ETag uses the whole file as one "chunk") does not
# pull the entire part into memory at once.
_ETAG_READ_BLOCK_BYTES = 1024 * 1024

def candidate_chunk_sizes(file_size, part_count):
    """
    Return every standard multipart chunk size consistent with a file size and
    a multipart ETag's part count.

    A uniform chunk size ``C`` splits ``file_size`` bytes into exactly
    ``part_count`` parts iff ``(part_count - 1) * C < file_size <= part_count * C``
    (equivalently ``part_count == ceil(file_size / C)``). Part count alone does
    not identify a single chunk size, so all matches are returned and the caller
    disambiguates by recomputing the full multipart ETag for each.

    Args:
        file_size (int): Size of the local file in bytes
        part_count (int): Part count parsed from the S3 multipart ETag

    Returns:
        list[int]: Consistent chunk sizes in bytes, most likely first. Empty when
        ``part_count`` is not positive. A single-part upload has no interior
        boundary, so the whole file is returned as the only chunk size.
    """
    if part_count <= 0:
        return []

    if part_count == 1:
        return [max(file_size, 1)]

    return [
        chunk_size
        for chunk_size in _STANDARD_MULTIPART_CHUNK_SIZES
        if (part_count - 1) * chunk_size < file_size <= part_count * chunk_size
    ]

def calculate_s3_etag_simple(file_path):
    """
    Calculate S3 ETag for a file as if uploaded without multipart.

    Args:
        file_path (str): Absolute path to local file

    Returns:
        str: MD5 hash in hexadecimal format (32 characters, no hyphen)

    Raises:
        IOError: If file cannot be read
    """
    md5_hash = hashlib.md5()

    with open(file_path, 'rb') as f:
        # Read in 8KB chunks to avoid loading entire file into memory
        for chunk in iter(lambda: f.read(8192), b''):
            md5_hash.update(chunk)

    return md5_hash.hexdigest()

def calculate_file_sha256(file_path):
    """
    Calculate the SHA-256 of a local file.

    Args:
        file_path (str): Absolute path to local file

    Returns:
        str: SHA-256 digest as lowercase hex (64 characters)

    Raises:
        IOError: If file cannot be read
    """
    sha256_hash = hashlib.sha256()

    with open(file_path, 'rb') as f:
        # Read in 8KB chunks to avoid loading entire file into memory
        for chunk in iter(lambda: f.read(8192), b''):
            sha256_hash.update(chunk)

    return sha256_hash.hexdigest()

def _valid_sha256(value):
    """Return the lowercase hex digest if value is a well-formed SHA-256, else None."""
    if value and re.fullmatch(r'[0-9a-fA-F]{64}', value):
        return value.lower()
    return None

def _multipart_etag_for_chunk_size(file_path, chunk_size):
    """
    Compute S3's multipart ETag for a file assuming a given chunk size.

    S3's algorithm: MD5 each part, concatenate the binary digests, MD5 the
    concatenation, and append "-<part count>".

    Args:
        file_path (str): Absolute path to the local file
        chunk_size (int): Bytes per part (must be positive)

    Returns:
        tuple: (hex_md5_of_concatenated_digests, part_count). An empty file is
        treated as a single empty part.

    Raises:
        IOError: If the file cannot be read
    """
    md5_digests = []

    with open(file_path, 'rb') as f:
        while True:
            part_md5 = hashlib.md5()
            remaining = chunk_size
            while remaining > 0:
                block = f.read(min(_ETAG_READ_BLOCK_BYTES, remaining))
                if not block:
                    break
                part_md5.update(block)
                remaining -= len(block)
            if remaining == chunk_size:
                # This part consumed no bytes: end of file.
                break
            md5_digests.append(part_md5.digest())

    if not md5_digests:
        md5_digests.append(hashlib.md5(b'').digest())

    return hashlib.md5(b''.join(md5_digests)).hexdigest(), len(md5_digests)

def multipart_etag_matches(file_path, etag_hash, part_count):
    """
    Return True if the local file reproduces a multipart ETag exactly.

    The chunk size used for a multipart upload cannot be recovered from the part
    count alone, so every standard chunk size consistent with the file size and
    part count is tried and the full multipart ETag is recomputed for each. The
    match must be exact; a chunk size that merely reproduces the part count is
    not accepted.

    Args:
        file_path (str): Absolute path to the local file
        etag_hash (str): Hash portion of the S3 ETag (before the "-")
        part_count (int): Part count parsed from the S3 ETag

    Returns:
        bool: True on an exact match for some consistent chunk size, else False.

    Raises:
        IOError: If the file cannot be read
    """
    file_size = os.path.getsize(file_path)

    for chunk_size in candidate_chunk_sizes(file_size, part_count):
        local_hash, local_parts = _multipart_etag_for_chunk_size(file_path, chunk_size)
        if local_parts == part_count and local_hash.lower() == etag_hash.lower():
            return True

    return False

def verify_worker(s3_client, bucket_name, file_path, relative_path):
    """
    Verify a single file against its S3 object by comparing ETags.

    This function:
    1. Checks if file exists in S3 using head_object
    2. Retrieves S3 ETag from response
    3. Calculates local ETag matching S3's method (simple or multipart)
    4. Compares ETags and returns status message

    Args:
        s3_client: boto3 S3 client instance
        bucket_name (str): Target S3 bucket name
        file_path (str): Absolute path to local file
        relative_path (str): Relative path used as S3 key

    Returns:
        tuple: (status_message, status_category)
        - status_message (str): Status message in format "[STATUS] relative_path"
          Where STATUS is: VERIFIED, DIFFERENT, NOT UPLOADED, or ERROR
        - status_category (str): One of: "verified", "different", "not_uploaded", "error"
    """
    try:
        # STEP 1: Check if file exists in S3 and get metadata
        try:
            response = s3_client.head_object(Bucket=bucket_name, Key=relative_path)
        except ClientError as e:
            if e.response['Error']['Code'] == "404":
                return (f"[NOT UPLOADED] {relative_path}", "not_uploaded")
            else:
                error_msg = e.response['Error'].get('Message', str(e))
                return (f"[ERROR] {relative_path} - S3 error: {error_msg}", "error")

        # STEP 2: Extract S3 ETag (remove surrounding quotes if present)
        s3_etag = response['ETag'].strip('"')

        # STEP 2b: Prefer a recorded SHA-256 of the content over the ETag
        remote_sha256 = _valid_sha256(response.get('Metadata', {}).get(CONTENT_SHA256_METADATA_KEY))
        if remote_sha256 is not None:
            if calculate_file_sha256(file_path) == remote_sha256:
                return (f"[VERIFIED] {relative_path}", "verified")
            return (f"[DIFFERENT] {relative_path} (SHA-256 mismatch)", "different")

        # STEP 3: Detect if multipart upload (contains hyphen)
        if '-' in s3_etag:
            # Multipart ETag format: "xxxxxxxx-N" where N is part count
            etag_parts = s3_etag.split('-')
            if len(etag_parts) != 2:
                return (f"[ERROR] {relative_path} - Invalid S3 ETag format: {s3_etag}", "error")

            etag_hash = etag_parts[0]
            try:
                part_count = int(etag_parts[1])
            except ValueError:
                return (f"[ERROR] {relative_path} - Invalid part count in ETag: {s3_etag}", "error")

            # STEP 4: Reproduce the multipart ETag exactly (no chunk-size guessing)
            if multipart_etag_matches(file_path, etag_hash, part_count):
                return (f"[VERIFIED] {relative_path}", "verified")
            return (f"[DIFFERENT] {relative_path} (S3: {s3_etag}, Local: multipart mismatch)", "different")

        # Simple upload (no multipart)
        # STEP 5: Calculate local simple ETag
        local_etag = calculate_s3_etag_simple(file_path)

        # STEP 6: Compare ETags (case-insensitive)
        if local_etag.lower() == s3_etag.lower():
            return (f"[VERIFIED] {relative_path}", "verified")
        else:
            return (f"[DIFFERENT] {relative_path} (S3: {s3_etag}, Local: {local_etag})", "different")

    except IOError as e:
        return (f"[ERROR] {relative_path} - File read error: {str(e)}", "error")
    except OSError as e:
        return (f"[ERROR] {relative_path} - File not found: {str(e)}", "error")
    except Exception as e:
        return (f"[ERROR] {relative_path} - Unexpected error: {str(e)}", "error")

class UnsafeDownloadPathError(ValueError):
    """Raised when an S3 key cannot be mapped safely below the download root."""


_WINDOWS_RESERVED_NAMES = {
    'CON', 'PRN', 'AUX', 'NUL', 'CONIN$', 'CONOUT$',
    *(f'COM{suffix}' for suffix in '123456789¹²³'),
    *(f'LPT{suffix}' for suffix in '123456789¹²³'),
}
_WINDOWS_INVALID_CHARS = set('<>"|?*')


def validate_s3_key_components(s3_key):
    """Validate an S3 POSIX key and return (components, is_folder_marker)."""
    if not isinstance(s3_key, str) or not s3_key:
        raise UnsafeDownloadPathError("S3 key is empty")
    if '\x00' in s3_key:
        raise UnsafeDownloadPathError("S3 key contains NUL")
    if '\\' in s3_key:
        raise UnsafeDownloadPathError("S3 key contains a backslash")

    parts = s3_key.split('/')
    is_folder_marker = parts[-1] == ''
    components = parts[:-1] if is_folder_marker else parts

    if not components:
        raise UnsafeDownloadPathError("S3 key has no file-name components")

    for component in components:
        if component == '':
            raise UnsafeDownloadPathError("S3 key is absolute or contains an empty component")
        if component in ('.', '..'):
            raise UnsafeDownloadPathError(f"S3 key contains {component!r} component")
        if ':' in component:
            raise UnsafeDownloadPathError("S3 key contains ':'")
        if component[-1] in ('.', ' '):
            raise UnsafeDownloadPathError("S3 key component ends with a dot or space")
        if any(ord(char) < 0x20 or char in _WINDOWS_INVALID_CHARS for char in component):
            raise UnsafeDownloadPathError("S3 key contains a control or Windows-invalid character")

        device_name = component.split('.', 1)[0].rstrip(' ').upper()
        if device_name in _WINDOWS_RESERVED_NAMES:
            raise UnsafeDownloadPathError("S3 key contains a Windows reserved device name")

    return tuple(components), is_folder_marker


def resolve_download_root(local_folder, create=False):
    """Resolve the user-controlled download root, optionally creating it."""
    local_folder = os.path.abspath(local_folder)

    if create:
        os.makedirs(local_folder, exist_ok=True)

    root = os.path.realpath(local_folder)
    if os.path.lexists(local_folder) and not os.path.exists(root):
        raise UnsafeDownloadPathError("download root is a broken symbolic link")
    if os.path.exists(root) and not os.path.isdir(root):
        raise UnsafeDownloadPathError("download root is not a directory")

    return root


def ensure_download_root(local_folder):
    """Create and resolve the download root."""
    return resolve_download_root(local_folder, create=True)


def resolve_download_path(root, components):
    """Map validated key components below a resolved download root."""
    root = os.path.normpath(os.path.abspath(root))
    dest = os.path.normpath(os.path.join(root, *components))
    try:
        contained = os.path.commonpath([root, dest]) == root
    except ValueError as exc:
        raise UnsafeDownloadPathError("resolved path is outside the download root") from exc
    if not contained or dest == root:
        raise UnsafeDownloadPathError("resolved path is outside the download root")
    return dest


def is_hostile_reparse(path, st=None):
    """Return True for symlinks, junctions, and Windows reparse points."""
    if st is None:
        st = os.lstat(path)
    if stat.S_ISLNK(st.st_mode) or os.path.islink(path):
        return True

    isjunction = getattr(os.path, 'isjunction', None)
    if isjunction is not None and isjunction(path):
        return True

    file_attributes = getattr(st, 'st_file_attributes', 0)
    reparse_flag = getattr(stat, 'FILE_ATTRIBUTE_REPARSE_POINT', 0x400)
    return bool(file_attributes & reparse_flag)


def lstat_walk_parents(root, dest):
    """Validate existing destination components without following links."""
    relative_path = os.path.relpath(dest, root)
    components = relative_path.split(os.sep)
    current = root

    for index, component in enumerate(components):
        current = os.path.join(current, component)
        try:
            current_stat = os.lstat(current)
        except FileNotFoundError:
            return None

        if is_hostile_reparse(current, current_stat):
            raise UnsafeDownloadPathError(f"destination contains a reparse point: {current}")

        is_final = index == len(components) - 1
        if is_final:
            if not stat.S_ISREG(current_stat.st_mode):
                raise UnsafeDownloadPathError(f"destination is not a regular file: {current}")
            return current_stat
        if not stat.S_ISDIR(current_stat.st_mode):
            raise UnsafeDownloadPathError(f"destination parent is not a directory: {current}")

    return None


def ensure_parent_dirs_nofollow(root, dest):
    """Create missing destination parents one component at a time, without links."""
    relative_parent = os.path.relpath(os.path.dirname(dest), root)
    if relative_parent == '.':
        return

    current = root
    for component in relative_parent.split(os.sep):
        current = os.path.join(current, component)
        try:
            current_stat = os.lstat(current)
        except FileNotFoundError:
            try:
                os.mkdir(current)
            except FileExistsError:
                pass
            current_stat = os.lstat(current)

        if is_hostile_reparse(current, current_stat) or not stat.S_ISDIR(current_stat.st_mode):
            raise UnsafeDownloadPathError(f"destination parent is not a real directory: {current}")


def _verify_download_file(file_path, s3_etag, s3_size, remote_sha256=None):
    """Return (category, details) after verifying a local file against S3 metadata."""
    local_size = os.path.getsize(file_path)
    if local_size != s3_size:
        size_diff = f"S3: {format_size(s3_size)}, Local: {format_size(local_size)}"
        return "different", f"size mismatch: {size_diff}"

    if remote_sha256 is not None:
        if calculate_file_sha256(file_path) == remote_sha256:
            return "verified", ""
        return "different", "SHA-256 mismatch"

    if '-' in s3_etag:
        etag_parts = s3_etag.split('-')
        if len(etag_parts) != 2:
            return "error", f"Invalid S3 ETag format: {s3_etag}"
        try:
            part_count = int(etag_parts[1])
        except ValueError:
            return "error", f"Invalid part count in ETag: {s3_etag}"
        if multipart_etag_matches(file_path, etag_parts[0], part_count):
            return "verified", ""
        return "different", f"S3: {s3_etag}, Local: multipart mismatch"

    local_etag = calculate_s3_etag_simple(file_path)
    if local_etag.lower() == s3_etag.lower():
        return "verified", ""
    return "different", f"S3: {s3_etag}, Local: {local_etag}"


def download_worker(s3_client, bucket_name, s3_key, local_folder, dry_run=False):
    """
    Download a single S3 object to local folder and verify.

    Args:
        s3_client: boto3 S3 client instance
        bucket_name (str): Target S3 bucket name
        s3_key (str): S3 object key (full path in bucket)
        local_folder (str): Local root directory for downloads
        dry_run (bool): If True, preview only (no actual download)

    Returns:
        tuple: (status_message, downloaded_size, status_category)
        - status_message (str): Human-readable status line for printing
        - downloaded_size (int): Size in bytes (0 if not downloaded)
        - status_category (str): One of: "downloaded", "verified", "different", "error", "skipped"
    """
    try:
        components, is_folder_marker = validate_s3_key_components(s3_key)

        try:
            response = s3_client.head_object(Bucket=bucket_name, Key=s3_key)
        except ClientError as e:
            if e.response['Error']['Code'] == "404":
                return (f"[ERROR] {s3_key} - Object not found in S3", 0, "error")
            error_msg = e.response['Error'].get('Message', str(e))
            return (f"[ERROR] {s3_key} - S3 error: {error_msg}", 0, "error")

        s3_etag = response['ETag'].strip('"')
        s3_size = response['ContentLength']
        remote_sha256 = _valid_sha256(response.get('Metadata', {}).get(CONTENT_SHA256_METADATA_KEY))

        if is_folder_marker:
            if s3_size == 0:
                return (f"[SKIPPED] {s3_key!r} (folder marker)", 0, "skipped")
            raise UnsafeDownloadPathError("non-empty trailing-slash object is not a folder marker")

        root = resolve_download_root(local_folder, create=False)
        local_path = resolve_download_path(root, components)
        local_stat = lstat_walk_parents(root, local_path)

        if local_stat is not None:
            category, details = _verify_download_file(local_path, s3_etag, s3_size, remote_sha256)
            if category == "verified":
                return (f"[VERIFIED] {s3_key} ({format_size(s3_size)})", 0, "verified")
            if category == "different":
                return (f"[DIFFERENT] {s3_key} ({details})", 0, "different")
            return (f"[ERROR] {s3_key} - {details}", 0, "error")

        if dry_run:
            return (f"[DRY-RUN] {s3_key} ({format_size(s3_size)} would be downloaded)", 0, "downloaded")

        if not os.path.exists(root):
            root = ensure_download_root(local_folder)
            local_path = resolve_download_path(root, components)
            if lstat_walk_parents(root, local_path) is not None:
                raise UnsafeDownloadPathError("destination appeared before download")

        ensure_parent_dirs_nofollow(root, local_path)
        parent = os.path.dirname(local_path)
        fd = None
        tmp_path = None
        try:
            fd, tmp_path = tempfile.mkstemp(prefix='.s3dl-', dir=parent)
            safe_print(f"[DOWNLOADING] {s3_key} ({format_size(s3_size)})...")
            try:
                with os.fdopen(fd, 'wb') as fileobj:
                    fd = None
                    s3_client.download_fileobj(bucket_name, s3_key, fileobj)
            except Exception as e:
                return (f"[ERROR] {s3_key} - Download failed: {str(e)}", 0, "error")

            try:
                category, details = _verify_download_file(tmp_path, s3_etag, s3_size, remote_sha256)
            except (IOError, OSError) as e:
                return (f"[DOWNLOADED] {s3_key} -> [ERROR] (verification failed: {str(e)})", 0, "error")

            if category == "different":
                return (f"[DOWNLOADED] {s3_key} -> [DIFFERENT] ({details})", 0, "different")
            if category == "error":
                return (f"[DOWNLOADED] {s3_key} -> [ERROR] ({details})", 0, "error")

            try:
                os.lstat(local_path)
            except FileNotFoundError:
                pass
            else:
                return (f"[ERROR] {s3_key} - Destination appeared before publish", 0, "error")

            os.replace(tmp_path, local_path)
            tmp_path = None
            return (f"[DOWNLOADED] {s3_key} ({format_size(s3_size)}) -> [VERIFIED]", s3_size, "downloaded")
        finally:
            if fd is not None:
                os.close(fd)
            if tmp_path is not None:
                try:
                    os.unlink(tmp_path)
                except FileNotFoundError:
                    pass
    except UnsafeDownloadPathError as e:
        return (f"[ERROR] {s3_key!r} - {str(e)}", 0, "error")
    except Exception as e:
        return (f"[ERROR] {s3_key} - Unexpected error: {str(e)}", 0, "error")
# S3 client will be created in main() using command-line arguments

def file_exists_in_bucket(s3_client, bucket, key):
    """Checks if a file exists in S3 without downloading it."""
    try:
        s3_client.head_object(Bucket=bucket, Key=key)
        return True
    except ClientError as e:
        # If the error code is 404 (Not Found), the file is missing
        if e.response['Error']['Code'] == "404":
            return False
        # If it's another error (e.g., 403 Forbidden), re-raise it
        raise e

def upload_worker(s3_client, bucket_name, file_path, relative_path, dry_run=False, force=False):
    """Handles logic for skipping or uploading a single file.

    Args:
        force (bool): Re-upload even if the key already exists (repairs a bad
            recorded checksum, backfills checksums onto older objects).

    Returns:
        tuple: (status_message, size_bytes, status_category)
        - status_message (str): Human-readable status line for printing
        - size_bytes (int): Size in bytes (0 if not uploaded)
        - status_category (str): One of: "skipped", "uploaded", "failure"
    """
    try:
        # 1. CHECK IF EXISTS (unless --force re-uploads regardless)
        if not force and file_exists_in_bucket(s3_client, bucket_name, relative_path):
            return (f"[SKIPPED]  {relative_path} (Already exists)", 0, "skipped")

        # 2. ANNOUNCE START
        # We print this immediately so you know it's working
        safe_print(f"[STARTING] {relative_path}...")

        # 3. CALCULATE SIZE (for report)
        st_before = os.stat(file_path)
        size_bytes = st_before.st_size
        size_gb = size_bytes / (1024 * 1024 * 1024)

        # 4. UPLOAD or DRY-RUN
        if dry_run:
            # Dry-run: skip actual upload, just report what would happen
            return (f"[DRY-RUN]  {relative_path}  --  {size_gb:.2f} GB (would upload)", size_bytes, "uploaded")
        else:
            # Actual upload; record the content SHA-256 and pin the multipart layout
            extra_args = {'Metadata': {CONTENT_SHA256_METADATA_KEY: calculate_file_sha256(file_path)}}
            s3_client.upload_file(
                file_path, bucket_name, relative_path,
                ExtraArgs=extra_args, Config=TRANSFER_CONFIG,
            )

            # The SHA-256 was read separately from the body s3transfer uploaded. If the
            # file changed in between, the recorded checksum is a lie and the object is
            # torn - remove it (best effort) and fail rather than leave it stranded.
            st_after = os.stat(file_path)
            if (st_after.st_size, st_after.st_mtime_ns) != (st_before.st_size, st_before.st_mtime_ns):
                try:
                    s3_client.delete_object(Bucket=bucket_name, Key=relative_path)
                except Exception:
                    pass
                return (f"[FAILURE]  {relative_path} - file changed during upload", 0, "failure")

            return (f"[SUCCESS]  {relative_path}  --  {size_gb:.2f} GB", size_bytes, "uploaded")

    except Exception as e:
        return (f"[FAILURE]  {relative_path} - {str(e)}", 0, "failure")

def create_parent_parser():
    """Create parser for arguments common to all commands."""
    parent_parser = argparse.ArgumentParser(add_help=False)

    # Common required arguments
    parent_parser.add_argument('--account-id', required=True, metavar='ID',
                               help='S3 account ID')
    parent_parser.add_argument('--access-key', required=True, metavar='KEY',
                               help='S3 access key ID')
    parent_parser.add_argument('--secret-key', required=True, metavar='SECRET',
                               help='S3 secret access key')
    parent_parser.add_argument('--bucket-name', required=True, metavar='NAME',
                               help='Target bucket name')
    parent_parser.add_argument('--endpoint-url', required=True, metavar='URL',
                               help='S3 endpoint URL')

    return parent_parser

def command_upload(args, s3_client=None):
    """Execute the upload command.

    Args:
        args: Command-line arguments
        s3_client: Optional boto3 S3 client (for testing)

    Returns:
        int: EXIT_SUCCESS if every file uploaded/skipped/dry-ran cleanly,
             EXIT_FAILURE if the local folder is unusable, any file failed,
             or a directory could not be scanned
    """
    # Create S3 client using command-line arguments
    # Note: boto3 uses 'aws_access_key_id' and 'aws_secret_access_key' parameter names
    # for all S3-compatible services (not just AWS)
    if s3_client is None:
        s3_client = boto3.client(
            's3',
            endpoint_url=args.endpoint_url,
            aws_access_key_id=args.access_key,
            aws_secret_access_key=args.secret_key
        )

    if not os.path.isdir(args.local_folder):
        print(f"[ERROR] Local folder not found or not a directory: {args.local_folder}")
        return EXIT_FAILURE

    force = getattr(args, 'force', False)

    files_to_upload = []
    total_size_bytes = 0

    print(f"Scanning files in {args.local_folder}...")

    # 1. Walk through the folder structure
    on_walk_error, scan_errors = make_walk_error_handler()
    rejected_entries = []
    for root, dirs, files in os.walk(args.local_folder, onerror=on_walk_error):
        # Skip hidden directories unless --allow-hidden-files is set
        if not args.allow_hidden_files:
            # Modify dirs in-place to prevent os.walk from descending into hidden directories
            dirs[:] = [d for d in dirs if not d.startswith('.')]

        # Reject linked directories, which os.walk() would otherwise skip silently
        dirs[:] = [d for d in dirs if not reject_tree_entry(os.path.join(root, d), rejected_entries)]

        for filename in files:
            # Skip hidden files unless --allow-hidden-files is set
            if not args.allow_hidden_files and filename.startswith('.'):
                continue

            local_path = os.path.join(root, filename)

            # Reject linked and non-regular files (a dangling link is caught here as well,
            # before os.path.getsize() below could fail on it)
            if reject_tree_entry(local_path, rejected_entries):
                continue

            # Create the "Key" (path inside the bucket)
            relative_path = os.path.relpath(local_path, args.local_folder)

            # Windows path fix
            if os.sep == '\\':
                relative_path = relative_path.replace('\\', '/')

            files_to_upload.append((local_path, relative_path))
            total_size_bytes += os.path.getsize(local_path)

    total_files = len(files_to_upload)
    print(f"Found {total_files} files. Checking for existing files & starting upload...\n")

    # Initialize statistics tracking
    upload_count = 0
    skip_count = 0
    failure_count = 0
    total_upload_size_bytes = 0

    # Start timer for speed calculation
    start_time = time.time()

    # 2. Execute
    with ThreadPoolExecutor(max_workers=args.max_threads) as executor:
        future_to_file = {
            executor.submit(upload_worker, s3_client, args.bucket_name, f[0], f[1], args.dry_run, force): f[1]
            for f in files_to_upload
        }

        for future in as_completed(future_to_file):
            try:
                result_message, size_bytes, status_category = future.result()
                safe_print(result_message)

                # Track statistics
                if status_category == "skipped":
                    skip_count += 1
                elif status_category == "uploaded":
                    upload_count += 1
                    total_upload_size_bytes += size_bytes
                elif status_category == "failure":
                    failure_count += 1
            except Exception as exc:
                safe_print(f"[CRITICAL ERROR] Thread crashed: {exc}")
                failure_count += 1

    # Stop timer
    elapsed_time = time.time() - start_time

    print("\nAll operations complete!")

    # Print summary
    if args.dry_run:
        print("\n--- DRY-RUN SUMMARY ---")
        print(f"Total files scanned: {total_files}")
        print(f"Files that would be uploaded: {upload_count}")
        print(f"Files that would be skipped: {skip_count}")
        print(f"Failed: {failure_count}")
        print(f"Directories not scanned: {len(scan_errors)}")
        print(f"Entries rejected: {len(rejected_entries)}")
        print(f"Total upload size: {format_size(total_upload_size_bytes)}")
        print(f"Upload speed: {format_speed(total_upload_size_bytes, elapsed_time)} ({format_size(total_upload_size_bytes)} in {format_duration(elapsed_time)})")
        print("\nNo files were actually uploaded (dry-run mode)")
    else:
        print("\n--- UPLOAD SUMMARY ---")
        print(f"Total files scanned: {total_files}")
        print(f"Files uploaded: {upload_count}")
        print(f"Files skipped (already exist): {skip_count}")
        print(f"Failed: {failure_count}")
        print(f"Directories not scanned: {len(scan_errors)}")
        print(f"Entries rejected: {len(rejected_entries)}")
        print(f"Total uploaded size: {format_size(total_upload_size_bytes)}")
        print(f"Upload speed: {format_speed(total_upload_size_bytes, elapsed_time)} ({format_size(total_upload_size_bytes)} in {format_duration(elapsed_time)})")

    return EXIT_FAILURE if failure_count or scan_errors or rejected_entries else EXIT_SUCCESS

def command_list(args, s3_client=None):
    """
    Execute the list command.

    Args:
        args: Command-line arguments with bucket_name, prefix, max_keys, paths_only, etc.
        s3_client: Optional boto3 S3 client (for testing). If None, creates client from args.

    Returns:
        int: EXIT_SUCCESS if listing succeeded, EXIT_FAILURE if it failed
    """
    # Create S3 client if not provided (for testing)
    # Note: boto3 uses 'aws_access_key_id' and 'aws_secret_access_key' parameter names
    # for all S3-compatible services (not just AWS)
    if s3_client is None:
        s3_client = boto3.client(
            's3',
            endpoint_url=args.endpoint_url,
            aws_access_key_id=args.access_key,
            aws_secret_access_key=args.secret_key
        )

    # In --paths-only mode stdout is a machine readable stream (one key per line) for a shell
    # consumer, so diagnostics go to stderr there and a key which would corrupt the stream
    # is reported instead of printed
    paths_only = getattr(args, 'paths_only', False)
    diagnostics = sys.stderr if paths_only else sys.stdout
    hostile_keys = 0

    # List objects
    try:
        if not paths_only:
            print(f"Listing objects in bucket: {args.bucket_name}")
            if args.prefix:
                print(f"Prefix filter: {args.prefix}")
            print()

            # Print header
            print(f"{'SIZE':<12} {'LAST MODIFIED':<25} FILE PATH")
            print("-" * 107)

        # Paginate through results using reusable helper
        total_objects = 0
        total_size = 0

        for obj in paginate_s3_objects(s3_client, args.bucket_name, args.prefix, args.max_keys):
            key = obj['Key']
            size_bytes = obj['Size']

            if paths_only:
                # A key carrying a newline or another control character cannot be emitted as
                # one line; repr() keeps the terminal intact and the command fails at the end
                if has_control_characters(key):
                    print(f"[ERROR] {key!r} - key contains control characters", file=diagnostics)
                    hostile_keys += 1
                    continue
                print(key)
            else:
                last_modified = obj['LastModified'].strftime('%Y-%m-%d %H:%M:%S %Z')
                size_str = format_size(size_bytes)
                print(f"{size_str:<12} {last_modified:<25} {key}")

            total_objects += 1
            total_size += size_bytes

        if not paths_only:
            # Check if bucket is empty or no objects match prefix
            if total_objects == 0:
                if args.prefix:
                    print(f"No objects found with prefix: {args.prefix}")
                else:
                    print("Bucket is empty")

            # Print max_keys warning if results may be truncated
            if args.max_keys and total_objects == args.max_keys:
                print(f"\n(Results limited to {args.max_keys} objects. Use --max-keys to adjust or remove to see all.)")

            # Print summary
            print("-" * 107)
            print(f"Total: {total_objects} objects, {format_size(total_size)}")

        return EXIT_FAILURE if hostile_keys else EXIT_SUCCESS

    except ClientError as e:
        error_code = e.response['Error']['Code']
        error_msg = e.response['Error']['Message']
        print(f"Error listing bucket: {error_code} - {error_msg}", file=diagnostics)
        return EXIT_FAILURE
    except Exception as e:
        print(f"Error: {str(e)}", file=diagnostics)
        return EXIT_FAILURE

def command_verify(args, s3_client=None):
    """Execute the verify command.

    Args:
        args: Command-line arguments
        s3_client: Optional boto3 S3 client (for testing)

    Returns:
        int: EXIT_SUCCESS if every file verified as matching,
             EXIT_FAILURE if the local folder is unusable, any file
             differed, was not uploaded, or errored, or a directory
             could not be scanned
    """

    # Create S3 client using command-line arguments
    if s3_client is None:
        s3_client = boto3.client(
            's3',
            endpoint_url=args.endpoint_url,
            aws_access_key_id=args.access_key,
            aws_secret_access_key=args.secret_key
        )

    if not os.path.isdir(args.local_folder):
        print(f"[ERROR] Local folder not found or not a directory: {args.local_folder}")
        return EXIT_FAILURE

    files_to_verify = []

    print(f"Scanning files in {args.local_folder}...")

    # Walk through the folder structure
    on_walk_error, scan_errors = make_walk_error_handler()
    rejected_entries = []
    for root, dirs, files in os.walk(args.local_folder, onerror=on_walk_error):
        # Skip hidden directories unless --allow-hidden-files is set
        if not args.allow_hidden_files:
            # Modify dirs in-place to prevent os.walk from descending into hidden directories
            dirs[:] = [d for d in dirs if not d.startswith('.')]

        # Reject linked directories, which os.walk() would otherwise skip silently
        dirs[:] = [d for d in dirs if not reject_tree_entry(os.path.join(root, d), rejected_entries)]

        for filename in files:
            # Skip hidden files unless --allow-hidden-files is set
            if not args.allow_hidden_files and filename.startswith('.'):
                continue

            local_path = os.path.join(root, filename)

            # Reject linked and non-regular files, the same policy as upload
            if reject_tree_entry(local_path, rejected_entries):
                continue

            # Create the "Key" (path inside the bucket)
            relative_path = os.path.relpath(local_path, args.local_folder)

            # Windows path fix
            if os.sep == '\\':
                relative_path = relative_path.replace('\\', '/')

            files_to_verify.append((local_path, relative_path))

    total_files = len(files_to_verify)
    print(f"Found {total_files} files. Starting verification...\n")

    # Initialize statistics tracking
    verified_count = 0
    different_count = 0
    not_uploaded_count = 0
    error_count = 0
    total_verified_size_bytes = 0

    # Start timer for speed calculation
    start_time = time.time()

    # Execute verification with ThreadPoolExecutor
    with ThreadPoolExecutor(max_workers=args.max_threads) as executor:
        future_to_file = {
            executor.submit(verify_worker, s3_client, args.bucket_name, f[0], f[1]): f
            for f in files_to_verify
        }

        for future in as_completed(future_to_file):
            file_path, relative_path = future_to_file[future]
            try:
                result_message, status_category = future.result()
                safe_print(result_message)

                # Get file size for ALL processed files
                try:
                    file_size = os.path.getsize(file_path)
                    total_verified_size_bytes += file_size
                except (IOError, OSError):
                    pass  # If can't get size, don't count it

                # Track statistics based on status category
                if status_category == "verified":
                    verified_count += 1
                elif status_category == "different":
                    different_count += 1
                elif status_category == "not_uploaded":
                    not_uploaded_count += 1
                elif status_category == "error":
                    error_count += 1
            except Exception as exc:
                safe_print(f"[CRITICAL ERROR] Thread crashed: {exc}")
                error_count += 1

    # Stop timer
    elapsed_time = time.time() - start_time

    # Print summary
    print("\nAll verifications complete!")
    print("\n--- VERIFICATION SUMMARY ---")
    print(f"Total files scanned: {total_files}")
    print(f"Verified (match): {verified_count}")
    print(f"Different (mismatch): {different_count}")
    print(f"Not uploaded to S3: {not_uploaded_count}")
    print(f"Errors: {error_count}")
    print(f"Directories not scanned: {len(scan_errors)}")
    print(f"Entries rejected: {len(rejected_entries)}")
    print(f"Verify speed: {format_speed(total_verified_size_bytes, elapsed_time)} ({format_size(total_verified_size_bytes)} in {format_duration(elapsed_time)})")

    # Fail if any files are missing, mismatched, or errored, a directory was not scanned, or
    # an entry was rejected
    if different_count > 0 or not_uploaded_count > 0 or error_count > 0 or scan_errors or rejected_entries:
        return EXIT_FAILURE
    return EXIT_SUCCESS


class InvalidIndexUrlPrefixError(ValueError):
    """Raised when --url-prefix cannot be used as an index download base."""


_INDEX_PREFIX_FORBIDDEN_CHARS = set(chr(code) for code in range(0x00, 0x21)) | {
    '\x7f', '\\', '(', ')', '<', '>', '"', "'",
}
_INDEX_PERCENT_HEX = set('0123456789ABCDEFabcdef')


def validate_index_url_prefix(url_prefix):
    """Return a normalized http(s) prefix ending in '/' for index download URLs."""
    if not isinstance(url_prefix, str) or not url_prefix:
        raise InvalidIndexUrlPrefixError("url prefix must be a non-empty string")

    for character in url_prefix:
        if character in _INDEX_PREFIX_FORBIDDEN_CHARS:
            raise InvalidIndexUrlPrefixError("url prefix contains a forbidden character")

    index = 0
    length = len(url_prefix)
    while index < length:
        if url_prefix[index] == '%':
            if (
                index + 2 >= length
                or url_prefix[index + 1] not in _INDEX_PERCENT_HEX
                or url_prefix[index + 2] not in _INDEX_PERCENT_HEX
            ):
                raise InvalidIndexUrlPrefixError("url prefix contains a malformed percent-escape")
            index += 3
        else:
            index += 1

    try:
        parts = urllib.parse.urlsplit(url_prefix)
        hostname = parts.hostname
        port = parts.port
    except ValueError as exc:
        raise InvalidIndexUrlPrefixError(str(exc)) from exc

    if parts.scheme not in ('http', 'https'):
        raise InvalidIndexUrlPrefixError("url prefix scheme must be http or https")

    if not hostname:
        raise InvalidIndexUrlPrefixError("url prefix must include a host")

    if parts.username is not None or parts.password is not None or '@' in parts.netloc:
        raise InvalidIndexUrlPrefixError("url prefix must not include userinfo")

    if '?' in url_prefix:
        raise InvalidIndexUrlPrefixError("url prefix must not include a query")

    if '#' in url_prefix:
        raise InvalidIndexUrlPrefixError("url prefix must not include a fragment")

    if port is None and parts.netloc.endswith(':'):
        raise InvalidIndexUrlPrefixError("url prefix port is empty")

    path = parts.path if parts.path else '/'
    if not path.endswith('/'):
        path += '/'

    return f"{parts.scheme}://{parts.netloc}{path}"


def encode_s3_key_for_url(key):
    """Percent-encode an S3 key as URL path data, keeping '/' as a separator."""
    if not isinstance(key, str):
        raise TypeError("S3 key must be a str")
    return urllib.parse.quote(key, safe='/', encoding='utf-8', errors='strict')


def build_index_download_url(url_prefix, key):
    """Build an index download URL from a validated prefix and encoded key."""
    prefix = validate_index_url_prefix(url_prefix)
    return prefix + encode_s3_key_for_url(key)


def _escape_markdown_index_label(key):
    """Escape an S3 key for use as a Markdown table link label."""
    label = key.replace('\r', ' ').replace('\n', ' ').replace('\t', ' ')
    label = html.escape(label, quote=True)
    label = label.replace('\\', '\\\\')
    label = label.replace('[', '\\[')
    label = label.replace(']', '\\]')
    label = label.replace('|', '\\|')
    return label


def generate_html_index(objects, total_objects, total_size, url_prefix):
    """Generate HTML index file content."""
    from datetime import datetime, timezone

    url_prefix = validate_index_url_prefix(url_prefix)

    # Start HTML document
    html_lines = []
    html_lines.append('<!DOCTYPE html>')
    html_lines.append('<html>')
    html_lines.append('<head>')
    html_lines.append('  <meta charset="UTF-8">')
    html_lines.append(
        f'  <meta http-equiv="Content-Security-Policy" content="{INDEX_CONTENT_SECURITY_POLICY}">'
    )
    html_lines.append('  <meta name="viewport" content="width=device-width, initial-scale=1.0">')
    html_lines.append('  <title>Files Index</title>')
    html_lines.append('  <style>')
    html_lines.append('    body { font-family: Arial, sans-serif; margin: 20px; }')
    html_lines.append('    h1 { color: #333; }')
    html_lines.append('    table { border-collapse: collapse; width: 100%; }')
    html_lines.append('    th, td { border: 1px solid #ddd; padding: 8px; text-align: left; }')
    html_lines.append('    th { background-color: #f2f2f2; }')
    html_lines.append('    tr:hover { background-color: #f5f5f5; }')
    html_lines.append('    a { color: #0066cc; text-decoration: none; }')
    html_lines.append('    a:hover { text-decoration: underline; }')
    html_lines.append('    .summary { margin-top: 20px; font-weight: bold; }')
    html_lines.append('  </style>')
    html_lines.append('</head>')
    html_lines.append('<body>')
    html_lines.append('  <h1>Files Index</h1>')
    html_lines.append('  <table>')
    html_lines.append('    <thead>')
    html_lines.append('      <tr>')
    html_lines.append('        <th>File Path</th>')
    html_lines.append('        <th>Size</th>')
    html_lines.append('        <th>Last Modified</th>')
    html_lines.append('      </tr>')
    html_lines.append('    </thead>')
    html_lines.append('    <tbody>')

    # Add table rows
    for obj in objects:
        key = obj['key']
        size_str = format_size(obj['size'])
        last_modified_str = obj['last_modified'].strftime('%Y-%m-%d %H:%M:%S %Z')
        download_url = build_index_download_url(url_prefix, key)
        href = html.escape(download_url, quote=True)
        key_escaped = html.escape(key, quote=True)

        html_lines.append('      <tr>')
        html_lines.append(f'        <td><a href="{href}">{key_escaped}</a></td>')
        html_lines.append(f'        <td>{size_str}</td>')
        html_lines.append(f'        <td>{last_modified_str}</td>')
        html_lines.append('      </tr>')

    html_lines.append('    </tbody>')
    html_lines.append('  </table>')
    html_lines.append(f'  <div class="summary">Total: {total_objects} objects, {format_size(total_size)}</div>')
    html_lines.append(f'  <p><em>Generated on {datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")}</em></p>')
    html_lines.append('</body>')
    html_lines.append('</html>')

    return '\n'.join(html_lines)

def generate_markdown_index(objects, total_objects, total_size, url_prefix):
    """Generate Markdown index file content."""
    from datetime import datetime, timezone

    url_prefix = validate_index_url_prefix(url_prefix)

    # Start Markdown document
    md = []
    md.append('# Files Index')
    md.append('')
    md.append('| File Path | Size | Last Modified |')
    md.append('|-----------|------|---------------|')

    # Add table rows
    for obj in objects:
        key = obj['key']
        size_str = format_size(obj['size'])
        last_modified_str = obj['last_modified'].strftime('%Y-%m-%d %H:%M:%S %Z')
        download_url = build_index_download_url(url_prefix, key)
        key_escaped = _escape_markdown_index_label(key)

        # Create Markdown link
        md.append(f'| [{key_escaped}]({download_url}) | {size_str} | {last_modified_str} |')

    # Add summary
    md.append('')
    md.append(f'**Total:** {total_objects} objects, {format_size(total_size)}')
    md.append('')
    md.append(f'*Generated on {datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")}*')

    return '\n'.join(md)

def command_indexupload(args, s3_client=None):
    """Execute the indexupload command.

    Args:
        args: Command-line arguments
        s3_client: Optional boto3 S3 client (for testing)

    Returns:
        int: EXIT_SUCCESS if the index was generated and uploaded (or there
             was nothing to index), EXIT_FAILURE if the URL prefix was
             invalid, listing failed, or the index upload failed
    """
    import tempfile

    try:
        validate_index_url_prefix(args.url_prefix)
    except InvalidIndexUrlPrefixError as exc:
        print(f"[ERROR] {exc}")
        return EXIT_FAILURE

    # Create S3 client if not provided (for testing)
    if s3_client is None:
        s3_client = boto3.client(
            's3',
            endpoint_url=args.endpoint_url,
            aws_access_key_id=args.access_key,
            aws_secret_access_key=args.secret_key
        )

    print(f"Listing objects in bucket: {args.bucket_name}")
    if args.prefix:
        print(f"Prefix filter: {args.prefix}")
    print()

    # Collect all objects using paginate_s3_objects() generator
    try:
        all_s3_objects = list(paginate_s3_objects(s3_client, args.bucket_name, args.prefix))
    except ClientError as e:
        error_code = e.response['Error']['Code']
        error_message = e.response['Error']['Message']
        print(f"Error listing bucket: [{error_code}] {error_message}")
        return EXIT_FAILURE
    except Exception as e:
        print(f"Error listing bucket: {str(e)}")
        return EXIT_FAILURE

    # Filter out index files and build object list
    objects = []
    total_size = 0

    for obj in all_s3_objects:
        key = obj['Key']

        # Exclude index.html and index.md from list
        if key in ('index.html', 'index.md'):
            continue

        size_bytes = obj['Size']
        last_modified = obj['LastModified']

        objects.append({
            'key': key,
            'size': size_bytes,
            'last_modified': last_modified
        })
        total_size += size_bytes

    # Check if any objects were found
    if len(objects) == 0:
        if args.prefix:
            print(f"No objects found with prefix: {args.prefix}")
        else:
            print("Bucket is empty")
        print("No index files will be generated.")
        return EXIT_SUCCESS

    total_objects = len(objects)
    print(f"Found {total_objects} objects (excluded index.html, index.md)")
    print()

    # Generate index.html content
    html_content = generate_html_index(objects, total_objects, total_size, args.url_prefix)

    # Generate index.md content
    md_content = generate_markdown_index(objects, total_objects, total_size, args.url_prefix)

    # Write files to temporary location
    with tempfile.TemporaryDirectory() as tmpdir:
        html_path = os.path.join(tmpdir, 'index.html')
        md_path = os.path.join(tmpdir, 'index.md')

        with open(html_path, 'w', encoding='utf-8') as f:
            f.write(html_content)

        with open(md_path, 'w', encoding='utf-8') as f:
            f.write(md_content)

        print("Generated index files:")
        print(f"  - index.html ({os.path.getsize(html_path)} bytes)")
        print(f"  - index.md ({os.path.getsize(md_path)} bytes)")
        print()

        # Upload both files to bucket root
        print("Uploading index files to bucket root...")

        try:
            for local_path, key in ((html_path, 'index.html'), (md_path, 'index.md')):
                s3_client.upload_file(
                    local_path, args.bucket_name, key,
                    ExtraArgs={
                        'ContentType': INDEX_CONTENT_TYPES[key],
                        'CacheControl': INDEX_CACHE_CONTROL,
                        'Metadata': {CONTENT_SHA256_METADATA_KEY: calculate_file_sha256(local_path)},
                    },
                    Config=TRANSFER_CONFIG,
                )
                print(f"[SUCCESS] {key} uploaded")

            print("\nIndex files uploaded successfully!")
        except Exception as e:
            print(f"[ERROR] Failed to upload index files: {str(e)}")
            return EXIT_FAILURE

    return EXIT_SUCCESS

def _download_collision_identity(component):
    """Return the fail-closed cross-platform collision identity for a component."""
    decomposed = unicodedata.normalize('NFD', component)
    return unicodedata.normalize('NFD', decomposed.casefold())


def preflight_download_objects(s3_objects):
    """Classify listed objects and find unsafe or colliding destination keys."""
    download_queue = []
    folder_markers = []
    errors = {}
    trie = {'children': {}, 'terminals': [], 'keys': []}

    for obj in s3_objects:
        s3_key = obj['Key']
        s3_size = obj['Size']
        try:
            components, is_folder_marker = validate_s3_key_components(s3_key)
        except UnsafeDownloadPathError as exc:
            errors[s3_key] = str(exc)
            continue

        if is_folder_marker:
            if s3_size == 0:
                folder_markers.append(s3_key)
            else:
                errors[s3_key] = "non-empty trailing-slash object is not a folder marker"
            continue

        node = trie
        path_nodes = [node]
        conflicts = set()
        for component in components:
            conflicts.update(node['terminals'])
            identity = _download_collision_identity(component)
            node = node['children'].setdefault(
                identity,
                {'children': {}, 'terminals': [], 'keys': []}
            )
            path_nodes.append(node)

        conflicts.update(node['keys'])
        if conflicts:
            for conflict_key in conflicts:
                errors[conflict_key] = "destination namespace conflict"
            errors[s3_key] = "destination namespace conflict"

        node['terminals'].append(s3_key)
        for path_node in path_nodes:
            path_node['keys'].append(s3_key)
        download_queue.append((s3_key, s3_size))

    return download_queue, folder_markers, errors


def _print_download_summary(total_files, downloaded_count, verified_count,
                            different_count, error_count, skipped_count,
                            total_downloaded_size, total_verified_size,
                            elapsed_time):
    """Print download statistics in one consistent format."""
    print("\n--- DOWNLOAD SUMMARY ---")
    print(f"Total files found: {total_files}")
    print(f"Downloaded (new files): {downloaded_count} ({format_size(total_downloaded_size)})")
    print(f"Verified (existing files, match): {verified_count} ({format_size(total_verified_size)})")
    print(f"Different (existing files, mismatch): {different_count}")
    print(f"Skipped (folder markers): {skipped_count}")
    print(f"Errors: {error_count}")
    print(f"Download speed: {format_speed(total_downloaded_size, elapsed_time)} "
          f"({format_size(total_downloaded_size)} in {format_duration(elapsed_time)})")


def command_download(args, s3_client=None):
    """Execute the download command.

    Args:
        args: Command-line arguments
        s3_client: Optional boto3 S3 client (for testing)

    Returns:
        int: EXIT_SUCCESS if every object downloaded/verified cleanly (or
             there was nothing to download), EXIT_FAILURE if listing,
             preflight, or the download root failed, or any object failed
    """

    if s3_client is None:
        s3_client = boto3.client(
            's3',
            endpoint_url=args.endpoint_url,
            aws_access_key_id=args.access_key,
            aws_secret_access_key=args.secret_key
        )

    print(f"Listing objects in bucket: {args.bucket_name}")
    if args.prefix:
        print(f"Prefix filter: {args.prefix}")
    print()

    try:
        all_s3_objects = list(paginate_s3_objects(s3_client, args.bucket_name, args.prefix))
    except ClientError as e:
        error_code = e.response['Error']['Code']
        error_msg = e.response['Error']['Message']
        print(f"Error listing bucket: {error_code} - {error_msg}")
        return EXIT_FAILURE
    except Exception as e:
        print(f"Error: {str(e)}")
        return EXIT_FAILURE

    if not all_s3_objects:
        if args.prefix:
            print(f"No objects found with prefix: {args.prefix}")
        else:
            print("Bucket is empty")
        print("Nothing to download.")
        return EXIT_SUCCESS

    total_files = len(all_s3_objects)
    total_s3_size = sum(obj['Size'] for obj in all_s3_objects)
    print(f"Found {total_files} objects ({format_size(total_s3_size)} total)")
    print(f"Local folder: {args.local_folder}")
    print()

    if args.dry_run:
        print("Running in DRY-RUN mode (no files will be downloaded)")
        print()

    download_queue, folder_markers, preflight_errors = preflight_download_objects(all_s3_objects)

    for marker_key in folder_markers:
        safe_print(f"[SKIPPED] {marker_key!r} (folder marker)")

    if preflight_errors:
        for s3_key, reason in preflight_errors.items():
            safe_print(f"[ERROR] {s3_key!r} - {reason}")
        print("\nDownload preflight failed; no files were changed.")
        _print_download_summary(
            total_files, 0, 0, 0, len(preflight_errors), len(folder_markers),
            0, 0, 0
        )
        if args.dry_run:
            print("\nNo files were actually downloaded (dry-run mode)")
        return EXIT_FAILURE

    try:
        resolved_root = resolve_download_root(args.local_folder, create=False)
        if download_queue and not args.dry_run and not os.path.exists(resolved_root):
            ensure_download_root(args.local_folder)
    except (OSError, UnsafeDownloadPathError) as exc:
        safe_print(f"[ERROR] Download root {args.local_folder!r} - {str(exc)}")
        _print_download_summary(
            total_files, 0, 0, 0, 1, len(folder_markers), 0, 0, 0
        )
        return EXIT_FAILURE

    downloaded_count = 0
    verified_count = 0
    different_count = 0
    error_count = 0
    skipped_count = len(folder_markers)
    total_downloaded_size = 0
    total_verified_size = 0
    start_time = time.time()

    if download_queue:
        with ThreadPoolExecutor(max_workers=args.max_threads) as executor:
            future_to_file = {
                executor.submit(
                    download_worker, s3_client, args.bucket_name, s3_key,
                    args.local_folder, args.dry_run
                ): (s3_key, s3_size)
                for s3_key, s3_size in download_queue
            }

            for future in as_completed(future_to_file):
                s3_key, s3_size = future_to_file[future]
                try:
                    status_message, downloaded_size, status_category = future.result()
                    safe_print(status_message)

                    if status_category == "downloaded":
                        downloaded_count += 1
                        total_downloaded_size += downloaded_size
                    elif status_category == "verified":
                        verified_count += 1
                        total_verified_size += s3_size
                    elif status_category == "different":
                        different_count += 1
                    elif status_category == "error":
                        error_count += 1
                    elif status_category == "skipped":
                        skipped_count += 1

                except Exception as exc:
                    safe_print(f"[CRITICAL ERROR] Thread crashed processing {s3_key}: {exc}")
                    error_count += 1

    elapsed_time = time.time() - start_time

    print("\nAll operations complete!")
    _print_download_summary(
        total_files, downloaded_count, verified_count, different_count,
        error_count, skipped_count, total_downloaded_size,
        total_verified_size, elapsed_time
    )

    if args.dry_run:
        print("\nNo files were actually downloaded (dry-run mode)")

    if different_count > 0 or error_count > 0:
        return EXIT_FAILURE
    return EXIT_SUCCESS

def main():
    # Create parent parser for common arguments
    parent_parser = create_parent_parser()

    # Create main parser
    parser = argparse.ArgumentParser(
        description='S3 management tool for upload, download, and bucket operations'
    )

    # Create subparsers for commands
    subparsers = parser.add_subparsers(
        dest='command',
        required=True,
        help='Command to execute'
    )

    # Add 'upload' subcommand
    upload_parser = subparsers.add_parser(
        'upload',
        parents=[parent_parser],
        help='Upload files to S3 bucket'
    )

    # Add upload-specific arguments
    upload_parser.add_argument('--local-folder', required=True, metavar='PATH',
                               help='Local directory to upload')
    upload_parser.add_argument('--max-threads', type=int, default=3, metavar='N',
                               help='Number of parallel uploads (default: 3)')
    upload_parser.add_argument('--dry-run', action='store_true',
                               help='Show what would be uploaded without uploading')
    upload_parser.add_argument('--allow-hidden-files', action='store_true',
                               help='Include hidden files and directories (those starting with ".")')
    upload_parser.add_argument('--force', action='store_true',
                               help='Re-upload objects that already exist (repairs a bad recorded '
                                    'checksum and backfills checksums onto older objects)')

    # Add 'list' subcommand
    list_parser = subparsers.add_parser(
        'list',
        parents=[parent_parser],
        help='List objects in S3 bucket'
    )

    # Add list-specific arguments
    list_parser.add_argument('--prefix', metavar='PREFIX',
                             help='Filter objects by prefix (e.g., "folder/subfolder/")')
    list_parser.add_argument('--max-keys', type=int, metavar='N',
                             help='Maximum number of objects to list')
    list_parser.add_argument('--paths-only', action='store_true',
                             help='Output only file paths, one per line (no header, summary, or other columns)')

    # Add 'verify' subcommand
    verify_parser = subparsers.add_parser(
        'verify',
        parents=[parent_parser],
        help='Verify local files against S3 objects by comparing ETags'
    )

    # Add verify-specific arguments
    verify_parser.add_argument('--local-folder', required=True, metavar='PATH',
                              help='Local directory to verify against S3')
    verify_parser.add_argument('--max-threads', type=int, default=3, metavar='N',
                              help='Number of parallel verification threads (default: 3)')
    verify_parser.add_argument('--allow-hidden-files', action='store_true',
                              help='Include hidden files and directories (those starting with ".")')

    # Add 'indexupload' subcommand
    indexupload_parser = subparsers.add_parser(
        'indexupload',
        parents=[parent_parser],
        help='Generate index.html and index.md files listing bucket contents, then upload to bucket root'
    )

    # Add indexupload-specific arguments
    indexupload_parser.add_argument('--url-prefix', required=True, metavar='URL',
                                    help='Base URL for generating file download links (e.g., https://storage.example.com/mybucket/)')
    indexupload_parser.add_argument('--prefix', metavar='PREFIX',
                                    help='Filter objects by prefix (e.g., "folder/subfolder/")')

    # Add 'download' subcommand
    download_parser = subparsers.add_parser(
        'download',
        parents=[parent_parser],
        help='Download S3 objects to local folder with verification'
    )

    # Add download-specific arguments
    download_parser.add_argument('--local-folder', required=True, metavar='PATH',
                                 help='Local directory to download files to')
    download_parser.add_argument('--max-threads', type=int, default=3, metavar='N',
                                 help='Number of parallel download threads (default: 3)')
    download_parser.add_argument('--dry-run', action='store_true',
                                 help='Preview what would be downloaded without downloading')
    download_parser.add_argument('--prefix', metavar='PREFIX',
                                 help='Filter S3 objects by prefix (e.g., "folder/subfolder/")')

    # Parse arguments
    args = parser.parse_args()

    # Dispatch to appropriate command
    if args.command == 'upload':
        exit_code = command_upload(args)
    elif args.command == 'list':
        exit_code = command_list(args)
    elif args.command == 'verify':
        exit_code = command_verify(args)
    elif args.command == 'indexupload':
        exit_code = command_indexupload(args)
    elif args.command == 'download':
        exit_code = command_download(args)
    else:
        parser.error(f"Unknown command: {args.command}")

    sys.exit(exit_code)

if __name__ == "__main__":
    main()