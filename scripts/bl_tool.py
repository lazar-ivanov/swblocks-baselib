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

"""
bl_tool.py - Utility for baselib repository operations

Commands:
  hash    Calculate hash of file or directory contents
"""

import argparse
import hashlib
import os
import stat
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path


# Memory management constants
FILE_CHUNK_SIZE = 1024 * 1024  # 1 MB - size of chunks when reading large files
HASH_BLOCK_SIZE = 32000        # ~1 MB of hash data (32000 hashes × 32 bytes = 1 MB)

# Hash format v2 domain separation tags
#
# Every record is domain separated and length prefixed so the digest is an unambiguous
# commitment to the tree. Format v1 concatenated unframed fields, which let structurally
# different trees produce identical digests (content 'a' at path 'bc' collided with
# content 'ab' at path 'c').
HASH_FORMAT_NAME = 'blhash/v2'
LEAF_TAG_FILE = b'blhash/v2/file\x00'
LEAF_TAG_DIR = b'blhash/v2/dir\x00'
ROOT_TAG_TREE = b'blhash/v2/tree\x00'
ROOT_TAG_FILE = b'blhash/v2/single\x00'

# Width of every length and size field in the record format (unsigned 64-bit big-endian)
LENGTH_FIELD_SIZE = 8

# Name reported for the raw content mode selected by --exclude-paths
RAW_FORMAT_NAME = 'raw-content (no tree commitment)'


# --- THREAD SAFE PRINTING ---
# This lock stops threads from writing over each other
print_lock = threading.Lock()

def safe_print(message):
    """Prints a message ensuring no other thread interrupts."""
    with print_lock:
        print(message)


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


def encode_path(relative_path):
    """
    Encode a relative path to its canonical byte representation for hashing.

    Paths are '/' separated by the collection step, so an identical tree produces an
    identical digest on Windows and POSIX. 'surrogateescape' round-trips file names that
    are not valid UTF-8 (possible on POSIX) instead of failing part way through a run.

    Args:
        relative_path (str): Relative path from the hashed root

    Returns:
        bytes: Canonical path bytes
    """
    return relative_path.encode('utf-8', 'surrogateescape')


def encode_length(value):
    """Encode a length or size as an unsigned 64-bit big-endian record field."""
    return value.to_bytes(LENGTH_FIELD_SIZE, 'big')


def is_reparse_link(path):
    """
    Check whether a path is a symlink or a Windows junction.

    Path.is_symlink() reports False for junctions, so they are tested separately.
    os.path.isjunction() requires Python 3.12, hence the guarded lookup.

    Args:
        path (Path): Path to check

    Returns:
        bool: True if the path is a symlink or a junction
    """
    if path.is_symlink():
        return True

    isjunction = getattr(os.path, 'isjunction', None)

    return isjunction is not None and isjunction(path)


def fail_on_link(path):
    """
    Report a rejected symlink or junction and exit.

    Raises:
        SystemExit: Always
    """
    print(f"[ERROR] Symlink encountered: {path}")
    print("Symlinks are not supported by hash command")
    sys.exit(1)


def relative_path_for(path, folder_path_obj):
    """
    Compute the '/' separated relative path of an entry inside the hashed root.

    Args:
        path (Path): Absolute path of the entry
        folder_path_obj (Path): Resolved root directory

    Returns:
        str: Relative path using '/' separators

    Raises:
        SystemExit: If the entry lies outside the root
    """
    try:
        return path.relative_to(folder_path_obj).as_posix()
    except ValueError:
        print(f"[ERROR] Path escapes the hashed root: {path}")
        sys.exit(1)


def collect_tree(folder_path, allow_hidden_files):
    """
    Collect all files and directories in a tree, sorted by canonical relative path.

    Directories are collected as well as files so that the digest commits to the tree
    shape, including directories that are empty or hold only filtered out entries.

    Args:
        folder_path (str): Root directory path
        allow_hidden_files (bool): Include entries starting with '.'

    Returns:
        tuple: (files, directories) where files is a list of
               (absolute_path, relative_path) tuples and directories is a list of
               relative paths, both sorted by canonical path bytes

    Raises:
        SystemExit: If a symlink, junction or non-regular file is encountered
    """
    files = []
    directories = []
    folder_path_obj = Path(folder_path).resolve()

    # Walk the resolved root so relative paths can be derived lexically. The root is
    # link free (checked by the caller) and links inside the tree are rejected below,
    # so no per-file resolve() is needed.
    for root, dirs, filenames in os.walk(folder_path_obj):
        root_path = Path(root)

        # Filter hidden directories if needed
        if not allow_hidden_files:
            dirs[:] = [d for d in dirs if not d.startswith('.')]

        for dirname in dirs:
            dir_path = root_path / dirname

            # Reject symlinked directories before descending. os.walk() does not follow
            # them, so without this check their contents would be silently omitted.
            if is_reparse_link(dir_path):
                fail_on_link(dir_path)

            directories.append(relative_path_for(dir_path, folder_path_obj))

        for filename in filenames:
            # Filter hidden files if needed
            if not allow_hidden_files and filename.startswith('.'):
                continue

            file_path = root_path / filename

            # A single lstat() classifies the entry without following it
            entry_stat = os.lstat(file_path)

            # Check for symlinks - FAIL if found
            if stat.S_ISLNK(entry_stat.st_mode):
                fail_on_link(file_path)

            # Reject FIFOs, sockets and devices - opening them can block forever
            if not stat.S_ISREG(entry_stat.st_mode):
                print(f"[ERROR] Not a regular file: {file_path}")
                print("Only regular files are supported by hash command")
                sys.exit(1)

            files.append((str(file_path), relative_path_for(file_path, folder_path_obj)))

    # Sort by canonical path bytes (platform and locale independent)
    files.sort(key=lambda x: encode_path(x[1]))
    directories.sort(key=encode_path)

    return files, directories


def collect_files(folder_path, allow_hidden_files):
    """
    Collect all files in directory, sorted by full relative path.

    Args:
        folder_path (str): Root directory path
        allow_hidden_files (bool): Include files starting with '.'

    Returns:
        list: Tuples of (absolute_path, relative_path) sorted by relative path

    Raises:
        SystemExit: If a symlink, junction or non-regular file is encountered
    """
    files, _ = collect_tree(folder_path, allow_hidden_files)

    return files


def handle_single_file(file_path, file_path_obj):
    """
    Prepare a single file for hashing.

    Args:
        file_path (str): Absolute path to file
        file_path_obj (Path): Path object for file

    Returns:
        list: Single-element list of (absolute_path, relative_path) tuple

    Raises:
        SystemExit: If file is a symlink or junction
    """
    # Check for symlink - FAIL if found (consistent with directory behavior)
    if is_reparse_link(file_path_obj):
        fail_on_link(file_path)

    # Use filename as relative path
    relative_path = file_path_obj.name

    # Return single-element list for consistent processing
    return [(file_path, relative_path)]


def handle_directory(folder_path, allow_hidden_files):
    """
    Prepare directory files for hashing.

    Args:
        folder_path (str): Absolute path to directory
        allow_hidden_files (bool): Include hidden files/directories

    Returns:
        tuple: (files, directories) as returned by collect_tree()
    """
    print(f"Scanning directory: {folder_path}")
    files, directories = collect_tree(folder_path, allow_hidden_files)
    print(f"Found {len(files)} files to process")
    return files, directories


def hash_worker(file_path, relative_path, verbose, exclude_paths=False, hash_algo='sha256'):
    """
    Hash a single file using chunked reading.

    Reads file in 1 MB chunks to handle large files without loading into memory.
    By default the content digest is framed into a length prefixed file record that
    commits the relative path and the file size, so no field can be shifted across a
    record boundary. Use exclude_paths=True to return the raw content digest instead.

    Args:
        file_path (str): Absolute path to file
        relative_path (str): Relative path from root (committed by the file record)
        verbose (bool): If True, print status for each file
        exclude_paths (bool): If True, return the unframed content digest
        hash_algo (str): Hash algorithm name ('sha256' or 'sha1')

    Returns:
        tuple: (relative_path, hash_bytes, file_size)

    Raises:
        Exception: On file read errors (propagated to main thread)
    """
    try:
        # Hash file in chunks to handle large files
        hasher = hashlib.new(hash_algo)
        file_size = 0

        with open(file_path, 'rb') as f:
            while True:
                chunk = f.read(FILE_CHUNK_SIZE)
                if not chunk:
                    break
                hasher.update(chunk)
                file_size += len(chunk)

        if exclude_paths:
            # Raw content digest - no path, no framing, no tree commitment
            hash_bytes = hasher.digest()
        else:
            # Frame the content digest into a length prefixed file record
            path_bytes = encode_path(relative_path)

            leaf_hasher = hashlib.new(hash_algo)
            leaf_hasher.update(LEAF_TAG_FILE)
            leaf_hasher.update(encode_length(len(path_bytes)))
            leaf_hasher.update(path_bytes)
            leaf_hasher.update(encode_length(file_size))
            leaf_hasher.update(hasher.digest())
            hash_bytes = leaf_hasher.digest()

        hash_hex = hash_bytes.hex()    # For display only

        # Thread-safe print (only in verbose mode)
        if verbose:
            safe_print(f"[HASHED] {relative_path} -- {hash_hex}")

        return (relative_path, hash_bytes, file_size)

    except Exception as e:
        # Thread-safe error print (always shown)
        safe_print(f"[ERROR] Failed to hash {relative_path}: {e}")
        raise  # Re-raise to fail entire operation


def hash_dir_record(relative_path, hash_algo='sha256'):
    """
    Compute the record digest for a directory entry.

    Directory records commit the tree shape, so two trees that differ only by an empty
    directory receive different digests.

    Args:
        relative_path (str): Relative path of the directory from the hashed root
        hash_algo (str): Hash algorithm name ('sha256' or 'sha1')

    Returns:
        bytes: Raw record digest
    """
    path_bytes = encode_path(relative_path)

    hasher = hashlib.new(hash_algo)
    hasher.update(LEAF_TAG_DIR)
    hasher.update(encode_length(len(path_bytes)))
    hasher.update(path_bytes)

    return hasher.digest()


def compute_root_digest(record_hashes, root_tag, hash_algo='sha256'):
    """
    Combine record digests into the final root digest.

    The root is domain separated by root_tag (single file vs directory tree), bound to
    the hash algorithm and committed to the record count, so a record digest can never
    be mistaken for a root digest. Records are fed in blocks of HASH_BLOCK_SIZE to keep
    memory usage constant for very large trees.

    Args:
        record_hashes (list): Raw record digests in canonical order
        root_tag (bytes): Domain separation tag for the hashed scope
        hash_algo (str): Hash algorithm name ('sha256' or 'sha1')

    Returns:
        str: Root digest as hex string
    """
    total_records = len(record_hashes)

    final_hasher = hashlib.new(hash_algo)
    final_hasher.update(root_tag)
    final_hasher.update(hash_algo.encode('ascii'))
    final_hasher.update(b'\x00')
    final_hasher.update(encode_length(total_records))

    # Process records in blocks to avoid large memory allocation
    for block_start in range(0, total_records, HASH_BLOCK_SIZE):
        block_end = min(block_start + HASH_BLOCK_SIZE, total_records)
        final_hasher.update(b''.join(record_hashes[block_start:block_end]))

    return final_hasher.hexdigest()


def combine_hashes(hash_list, hash_algo='sha256'):
    """
    Combine hashes in blocks to avoid large memory allocations.

    Used only by the raw content mode selected by --exclude-paths. The tree digest is
    built by compute_root_digest() instead.

    If only one hash is provided, returns it directly without re-hashing.
    Otherwise, processes hashes in blocks of HASH_BLOCK_SIZE, feeding each
    block to a single streaming hasher.

    Args:
        hash_list (list): List of raw hash bytes in order
        hash_algo (str): Hash algorithm name ('sha256' or 'sha1')

    Returns:
        str: Final combined hash as hex string
    """
    total_hashes = len(hash_list)

    # Single hash: return directly without re-hashing
    if total_hashes == 1:
        return hash_list[0].hex()

    # Initialize the final hasher with selected algorithm
    final_hasher = hashlib.new(hash_algo)

    # Process hashes in blocks to avoid large memory allocation
    for block_start in range(0, total_hashes, HASH_BLOCK_SIZE):
        block_end = min(block_start + HASH_BLOCK_SIZE, total_hashes)
        block = hash_list[block_start:block_end]

        # Concatenate this block and feed to the streaming hasher
        block_bytes = b''.join(block)
        final_hasher.update(block_bytes)

    return final_hasher.hexdigest()


def command_hash(args):
    """
    Execute hash command: calculate hash of file or directory.

    Workflow:
    1. Validate path exists
    2. Detect if path is file or directory
    3. If file: Hash single file (fail on symlinks)
    4. If directory: Hash all files in directory (existing logic)
    5. Combine hashes and print summary
    6. Verify hash if --expected-hash provided
    """

    # Step 1: Validate path exists
    path = os.path.abspath(args.path)
    if not os.path.exists(path):
        print(f"[ERROR] Path not found: {path}")
        sys.exit(1)

    # Step 2: Detect path type
    path_obj = Path(path)

    # Reject a linked root before classifying it - is_file()/is_dir() follow links, so a
    # symlinked directory would otherwise be accepted while a symlinked file is rejected
    if is_reparse_link(path_obj):
        fail_on_link(path)

    if path_obj.is_file():
        # Single file path
        files_to_process = handle_single_file(path, path_obj)
        directories = []
        root_tag = ROOT_TAG_FILE
        root_path = path  # For display in summary
    elif path_obj.is_dir():
        # Directory path
        files_to_process, directories = handle_directory(path, args.allow_hidden_files)
        root_tag = ROOT_TAG_TREE
        root_path = path
    else:
        print(f"[ERROR] Path is neither a file nor a directory: {path}")
        sys.exit(1)

    total_files = len(files_to_process)

    # Step 3: Dry-run mode (works for both files and directories)
    if args.dry_run:
        total_size = 0
        for file_path, relative_path in files_to_process:
            try:
                file_size = os.path.getsize(file_path)
                total_size += file_size
                if args.verbose:
                    print(f"[DRY-RUN] Would hash {relative_path}")
            except Exception as e:
                print(f"[ERROR] Failed to stat {relative_path}: {e}")
                sys.exit(1)

        print("\nAll operations complete!")
        print("\n--- DRY-RUN SUMMARY ---")
        print(f"Path: {root_path}")
        print(f"Total files found: {total_files}")
        print(f"Total size: {format_size(total_size)}")
        print("\nNo files were actually hashed (dry-run mode)")
        sys.exit(0)

    # Determine hash algorithm and display name
    hash_algo = 'sha1' if args.use_sha1 else 'sha256'
    hash_name = 'SHA1' if args.use_sha1 else 'SHA256'

    # Step 4: Hash files (single or multiple)
    if total_files == 1:
        print(f"Hashing single file...")
    else:
        print(f"Hashing files with {args.max_threads} threads...")

    results = []  # List of (relative_path, hash_bytes, file_size)

    # Start timer for speed calculation
    start_time = time.time()

    try:
        # Use ThreadPoolExecutor even for single file (simplicity, no performance impact)
        with ThreadPoolExecutor(max_workers=args.max_threads) as executor:
            # Submit all files
            future_to_file = {
                executor.submit(hash_worker, file_path, relative_path, args.verbose,
                                args.exclude_paths, hash_algo): (file_path, relative_path)
                for file_path, relative_path in files_to_process
            }

            # Collect results (maintain order)
            for future in as_completed(future_to_file):
                result = future.result()  # Raises exception if worker failed
                results.append(result)

    except Exception as e:
        # Worker failed - error already printed in worker
        print("\nOperation failed due to errors")
        sys.exit(1)

    # Stop timer
    elapsed_time = time.time() - start_time

    # Step 5: Sort results by canonical path bytes to maintain canonical order
    results.sort(key=lambda x: encode_path(x[0]))

    # Step 6: Combine hashes
    if args.exclude_paths:
        # Raw content mode - contents only, no records and no tree commitment
        hash_list = [hash_bytes for _, hash_bytes, _ in results]

        if len(hash_list) == 0:
            # Empty directory
            combined_hash = hashlib.new(hash_algo, b'').hexdigest()
        else:
            combined_hash = combine_hashes(hash_list, hash_algo)

        hash_format = RAW_FORMAT_NAME
    else:
        # Merge file and directory records into a single canonically ordered list
        records = [(relative_path, hash_bytes) for relative_path, hash_bytes, _ in results]
        records.extend(
            (relative_path, hash_dir_record(relative_path, hash_algo))
            for relative_path in directories
        )
        records.sort(key=lambda x: encode_path(x[0]))

        combined_hash = compute_root_digest(
            [record_hash for _, record_hash in records], root_tag, hash_algo)
        hash_format = HASH_FORMAT_NAME

    # Calculate total size
    total_size = sum(file_size for _, _, file_size in results)

    # Step 7: Print summary
    print("\nAll operations complete!")
    print("\n--- HASH SUMMARY ---")
    print(f"Path: {root_path}")
    print(f"Total files processed: {total_files}")
    print(f"Total size: {format_size(total_size)}")
    print(f"Combined {hash_name}: {combined_hash}")
    print(f"Hash format: {hash_format}")
    print(f"Hashing speed: {format_speed(total_size, elapsed_time)} ({format_size(total_size)} in {format_duration(elapsed_time)})")

    # Step 8: Verify hash if requested
    if args.expected_hash:
        expected_hash = args.expected_hash.lower()
        actual_hash = combined_hash.lower()

        if expected_hash == actual_hash:
            print("Verification: MATCH")
            sys.exit(0)
        else:
            print("Verification: MISMATCH")
            print(f"Expected: {expected_hash}")
            print(f"Actual:   {actual_hash}")
            sys.exit(1)

    sys.exit(0)


def main():
    """
    Main entry point - argument parsing with subparsers.
    """
    # Create parent parser (not used directly, just for help text)
    parser = argparse.ArgumentParser(
        description='bl_tool.py - Utility for baselib repository operations',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Hash a single file
  %(prog)s hash --path /path/to/file.txt

  # Hash a directory
  %(prog)s hash --path /path/to/dir

  # Hash with verification
  %(prog)s hash --path /path/to/dir --expected-hash abc123...

  # Hash without paths in hash input (matches sha256sum output for single files)
  %(prog)s hash --path /path/to/file.txt --exclude-paths

  # Use SHA-1 instead of SHA-256
  %(prog)s hash --path /path/to/dir --use-sha1

  # Include hidden files (directory only)
  %(prog)s hash --path /path/to/dir --allow-hidden-files --max-threads 8

  # Dry-run (scan only)
  %(prog)s hash --path /path/to/dir --dry-run

  # Verbose mode (show each file hash)
  %(prog)s hash --path /path/to/dir --verbose
"""
    )

    # Create subparsers for commands
    subparsers = parser.add_subparsers(dest='command', help='Available commands')
    subparsers.required = True

    # ========== hash command ==========
    parser_hash = subparsers.add_parser(
        'hash',
        help='Calculate hash of file or directory contents',
        description='Calculate hash of a single file or combined hash of all files in a directory'
    )

    parser_hash.add_argument(
        '--path',
        required=True,
        help='Path to file or directory to hash'
    )

    parser_hash.add_argument(
        '--expected-hash',
        help='Expected hash for verification (hex string)'
    )

    parser_hash.add_argument(
        '--exclude-paths',
        action='store_true',
        help='Exclude relative file paths from hash input (hash file contents only)'
    )

    parser_hash.add_argument(
        '--use-sha1',
        action='store_true',
        help='Use SHA-1 instead of SHA-256 (default)'
    )

    parser_hash.add_argument(
        '--allow-hidden-files',
        action='store_true',
        help='Include files and directories starting with . (applies to directories only)'
    )

    parser_hash.add_argument(
        '--max-threads',
        type=int,
        default=4,
        help='Maximum number of parallel threads (default: 4)'
    )

    parser_hash.add_argument(
        '--dry-run',
        action='store_true',
        help='Scan and report statistics without hashing'
    )

    parser_hash.add_argument(
        '--verbose',
        action='store_true',
        help='Print status for each file being hashed'
    )

    parser_hash.set_defaults(func=command_hash)

    # Parse arguments
    args = parser.parse_args()

    # Execute command
    args.func(args)


if __name__ == '__main__':
    main()
