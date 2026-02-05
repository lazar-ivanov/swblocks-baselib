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
  hash    Calculate SHA256 hash of file or directory contents
"""

import argparse
import hashlib
import os
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path


# Memory management constants
FILE_CHUNK_SIZE = 1024 * 1024  # 1 MB - size of chunks when reading large files
HASH_BLOCK_SIZE = 32000        # ~1 MB of hash data (32000 hashes × 32 bytes = 1 MB)


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


def collect_files(folder_path, allow_hidden_files):
    """
    Collect all files in directory, sorted by full relative path.

    Args:
        folder_path (str): Root directory path
        allow_hidden_files (bool): Include files starting with '.'

    Returns:
        list: Tuples of (absolute_path, relative_path) sorted by relative path

    Raises:
        SystemExit: If symlink encountered
    """
    files = []
    folder_path_obj = Path(folder_path).resolve()

    for root, dirs, filenames in os.walk(folder_path):
        # Filter hidden directories if needed
        if not allow_hidden_files:
            dirs[:] = [d for d in dirs if not d.startswith('.')]

        for filename in filenames:
            # Filter hidden files if needed
            if not allow_hidden_files and filename.startswith('.'):
                continue

            file_path = Path(root) / filename

            # Check for symlinks - FAIL if found
            if file_path.is_symlink():
                print(f"[ERROR] Symlink encountered: {file_path}")
                print("Symlinks are not supported by hash command")
                sys.exit(1)

            # Resolve to canonical path to handle symlinks like /var -> /private/var on macOS
            file_path = file_path.resolve()
            relative_path = file_path.relative_to(folder_path_obj)
            files.append((str(file_path), str(relative_path)))

    # Sort by full relative path (alphabetically)
    files.sort(key=lambda x: x[1])

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
        SystemExit: If file is a symlink
    """
    # Check for symlink - FAIL if found (consistent with directory behavior)
    if file_path_obj.is_symlink():
        print(f"[ERROR] Symlink encountered: {file_path}")
        print("Symlinks are not supported by hash command")
        sys.exit(1)

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
        list: List of (absolute_path, relative_path) tuples
    """
    print(f"Scanning directory: {folder_path}")
    files = collect_files(folder_path, allow_hidden_files)
    print(f"Found {len(files)} files to process")
    return files


def hash_worker(file_path, relative_path, verbose):
    """
    Hash a single file using chunked reading: SHA256(file_contents + relative_path_bytes)

    Reads file in 1 MB chunks to handle large files without loading into memory.

    Args:
        file_path (str): Absolute path to file
        relative_path (str): Relative path from root (for hash input)
        verbose (bool): If True, print status for each file

    Returns:
        tuple: (relative_path, hash_bytes, file_size)

    Raises:
        Exception: On file read errors (propagated to main thread)
    """
    try:
        # Hash file in chunks to handle large files
        hasher = hashlib.sha256()
        file_size = 0

        with open(file_path, 'rb') as f:
            while True:
                chunk = f.read(FILE_CHUNK_SIZE)
                if not chunk:
                    break
                hasher.update(chunk)
                file_size += len(chunk)

        # After all file chunks, append relative path
        hasher.update(relative_path.encode('utf-8'))
        hash_bytes = hasher.digest()  # Raw bytes (32 bytes)
        hash_hex = hash_bytes.hex()    # For display only

        # Thread-safe print (only in verbose mode)
        if verbose:
            safe_print(f"[HASHED] {relative_path} -- {hash_hex}")

        return (relative_path, hash_bytes, file_size)

    except Exception as e:
        # Thread-safe error print (always shown)
        safe_print(f"[ERROR] Failed to hash {relative_path}: {e}")
        raise  # Re-raise to fail entire operation


def combine_hashes(hash_list):
    """
    Combine hashes in blocks to avoid large memory allocations.

    Processes hashes in blocks of HASH_BLOCK_SIZE, feeding each block
    to a single streaming hasher. This produces the same result as
    concatenating all hashes at once, but without allocating a large buffer.

    Args:
        hash_list (list): List of raw hash bytes (32 bytes each) in order

    Returns:
        str: Final combined hash as hex string (64 characters)
    """
    total_hashes = len(hash_list)

    # Initialize the final hasher
    final_hasher = hashlib.sha256()

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
    Execute hash command: calculate SHA256 hash of file or directory.

    Workflow:
    1. Validate path exists
    2. Detect if path is file or directory
    3. If file: Hash single file (fail on symlinks)
    4. If directory: Hash all files in directory (existing logic)
    5. Combine hashes and print summary
    6. Verify hash if --verify-sha256 provided
    """

    # Step 1: Validate path exists
    path = os.path.abspath(args.path)
    if not os.path.exists(path):
        print(f"[ERROR] Path not found: {path}")
        sys.exit(1)

    # Step 2: Detect path type
    path_obj = Path(path)

    if path_obj.is_file():
        # Single file path
        files_to_process = handle_single_file(path, path_obj)
        root_path = path  # For display in summary
    elif path_obj.is_dir():
        # Directory path
        files_to_process = handle_directory(path, args.allow_hidden_files)
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
                executor.submit(hash_worker, file_path, relative_path, args.verbose): (file_path, relative_path)
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

    # Step 5: Sort results by relative path to maintain canonical order
    results.sort(key=lambda x: x[0])

    # Step 6: Combine hashes
    hash_list = [hash_bytes for _, hash_bytes, _ in results]

    if len(hash_list) == 0:
        # Empty directory
        combined_hash = hashlib.sha256(b'').hexdigest()
    else:
        combined_hash = combine_hashes(hash_list)

    # Calculate total size
    total_size = sum(file_size for _, _, file_size in results)

    # Step 7: Print summary
    print("\nAll operations complete!")
    print("\n--- HASH SUMMARY ---")
    print(f"Path: {root_path}")
    print(f"Total files processed: {total_files}")
    print(f"Total size: {format_size(total_size)}")
    print(f"Combined SHA256: {combined_hash}")
    print(f"Hashing speed: {format_speed(total_size, elapsed_time)}")

    # Step 8: Verify hash if requested
    if args.verify_sha256:
        expected_hash = args.verify_sha256.lower()
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
  %(prog)s hash --path /path/to/dir --verify-sha256 abc123...

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
        help='Calculate SHA256 hash of file or directory contents',
        description='Calculate SHA256 hash of a single file or combined hash of all files in a directory'
    )

    parser_hash.add_argument(
        '--path',
        required=True,
        help='Path to file or directory to hash'
    )

    parser_hash.add_argument(
        '--verify-sha256',
        help='Expected SHA256 hash for verification (hex string)'
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
