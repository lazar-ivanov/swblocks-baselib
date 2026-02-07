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
import argparse
import boto3
import threading
import hashlib
import math
import time
from botocore.exceptions import ClientError, NoCredentialsError
from concurrent.futures import ThreadPoolExecutor, as_completed

# Configuration will be provided via command-line arguments

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

def calculate_chunk_size(file_size, part_count):
    """
    Deterministically find the chunk size used for S3 multipart upload.

    boto3 uses standard chunk sizes (5MB, 8MB, 16MB, 32MB, 64MB, etc.).
    This function tests each standard size to find which one produces
    the expected part count for the given file size.

    Args:
        file_size (int): Size of file in bytes
        part_count (int): Number of parts from S3 ETag

    Returns:
        int: Chunk size in bytes, or None if no standard size matches
    """
    if part_count <= 0:
        return None

    # Standard boto3 chunk sizes (in order of preference)
    # boto3 default is 8MB, but can use others based on file size
    standard_chunk_sizes = [
        5 * 1024 * 1024,    # 5MB
        8 * 1024 * 1024,    # 8MB (boto3 default)
        16 * 1024 * 1024,   # 16MB
        32 * 1024 * 1024,   # 32MB
        64 * 1024 * 1024,   # 64MB
        128 * 1024 * 1024,  # 128MB
        256 * 1024 * 1024,  # 256MB
        512 * 1024 * 1024,  # 512MB
    ]

    # Test each standard chunk size to find which produces the expected part count
    for chunk_size in standard_chunk_sizes:
        expected_parts = math.ceil(file_size / chunk_size)
        if expected_parts == part_count:
            return chunk_size

    # No standard chunk size produces the expected part count
    # This should never happen with boto3 uploads, but handle gracefully
    return None

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

def calculate_s3_etag_multipart(file_path, part_count):
    """
    Calculate S3 ETag for a file as if uploaded with multipart.

    This uses the deterministic chunk size from calculate_chunk_size(),
    then calculates the multipart ETag using S3's algorithm:
    MD5(concatenated MD5 digests) + "-" + part_count

    Args:
        file_path (str): Absolute path to local file
        part_count (int): Number of parts used in S3 multipart upload

    Returns:
        str: Multipart ETag in format "xxxxxxxx-N" or None if calculation fails

    Raises:
        IOError: If file cannot be read
    """
    # Get file size
    file_size = os.path.getsize(file_path)

    # Deterministically find the chunk size used by boto3
    chunk_size = calculate_chunk_size(file_size, part_count)

    if chunk_size is None:
        # No standard chunk size produces the expected part count
        # This should never happen with boto3 uploads
        return None

    md5_digests = []

    with open(file_path, 'rb') as f:
        part_num = 0
        while True:
            # Read one chunk
            chunk = f.read(chunk_size)
            if not chunk:
                break

            # Calculate MD5 for this chunk
            chunk_md5 = hashlib.md5(chunk)
            md5_digests.append(chunk_md5.digest())  # Store binary digest
            part_num += 1

    # Verify part count matches expectation
    # This should always match since calculate_chunk_size() is deterministic
    if part_num != part_count:
        # This indicates an unexpected error - chunk size calculation was wrong
        return None

    # Concatenate all MD5 digests
    concatenated_md5 = b''.join(md5_digests)

    # Calculate MD5 of concatenated digests
    final_md5 = hashlib.md5(concatenated_md5)

    # Return in S3 multipart format: hash-partcount
    return f"{final_md5.hexdigest()}-{part_count}"

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
        str: Status message in format "[STATUS] relative_path"
             Where STATUS is: VERIFIED, DIFFERENT, NOT UPLOADED, or ERROR
    """
    try:
        # STEP 1: Check if file exists in S3 and get metadata
        try:
            response = s3_client.head_object(Bucket=bucket_name, Key=relative_path)
        except ClientError as e:
            if e.response['Error']['Code'] == "404":
                return f"[NOT UPLOADED] {relative_path}"
            else:
                error_msg = e.response['Error'].get('Message', str(e))
                return f"[ERROR] {relative_path} - S3 error: {error_msg}"

        # STEP 2: Extract S3 ETag (remove surrounding quotes if present)
        s3_etag = response['ETag'].strip('"')

        # STEP 3: Detect if multipart upload (contains hyphen)
        if '-' in s3_etag:
            # Multipart ETag format: "xxxxxxxx-N" where N is part count
            etag_parts = s3_etag.split('-')
            if len(etag_parts) != 2:
                return f"[ERROR] {relative_path} - Invalid S3 ETag format: {s3_etag}"

            etag_hash = etag_parts[0]
            try:
                part_count = int(etag_parts[1])
            except ValueError:
                return f"[ERROR] {relative_path} - Invalid part count in ETag: {s3_etag}"

            # STEP 4: Calculate local multipart ETag
            local_etag = calculate_s3_etag_multipart(file_path, part_count)

            if local_etag is None:
                return f"[ERROR] {relative_path} - Failed to calculate multipart ETag"
        else:
            # Simple upload (no multipart)
            # STEP 5: Calculate local simple ETag
            local_etag = calculate_s3_etag_simple(file_path)
            etag_hash = s3_etag

        # STEP 6: Compare ETags (case-insensitive)
        local_etag_hash = local_etag.split('-')[0] if '-' in local_etag else local_etag

        if local_etag_hash.lower() == etag_hash.lower():
            return f"[VERIFIED] {relative_path}"
        else:
            return f"[DIFFERENT] {relative_path} (S3: {s3_etag}, Local: {local_etag})"

    except IOError as e:
        return f"[ERROR] {relative_path} - File read error: {str(e)}"
    except OSError as e:
        return f"[ERROR] {relative_path} - File not found: {str(e)}"
    except Exception as e:
        return f"[ERROR] {relative_path} - Unexpected error: {str(e)}"

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
        - status_category (str): One of: "downloaded", "verified", "different", "error"
    """
    try:
        # STEP 1: Get S3 object metadata (ETag, Size) using head_object
        try:
            response = s3_client.head_object(Bucket=bucket_name, Key=s3_key)
        except ClientError as e:
            if e.response['Error']['Code'] == "404":
                return (f"[ERROR] {s3_key} - Object not found in S3", 0, "error")
            else:
                error_msg = e.response['Error'].get('Message', str(e))
                return (f"[ERROR] {s3_key} - S3 error: {error_msg}", 0, "error")

        s3_etag = response['ETag'].strip('"')
        s3_size = response['ContentLength']

        # STEP 2: Construct local file path (preserve full S3 key as path)
        # CRITICAL: Do NOT strip prefix - local paths mirror bucket structure
        local_path = os.path.join(local_folder, s3_key)

        # STEP 3: Check if local file exists
        file_exists_locally = os.path.exists(local_path)

        if file_exists_locally:
            # STEP 4A: File exists - VERIFY ONLY (do not re-download)
            try:
                local_size = os.path.getsize(local_path)

                # Quick size check first (optimization)
                if local_size != s3_size:
                    size_diff = f"S3: {format_size(s3_size)}, Local: {format_size(local_size)}"
                    return (f"[DIFFERENT] {s3_key} (size mismatch: {size_diff})", 0, "different")

                # Calculate local ETag matching S3's method (reuse verify logic)
                if '-' in s3_etag:
                    # Multipart ETag
                    etag_parts = s3_etag.split('-')
                    if len(etag_parts) != 2:
                        return (f"[ERROR] {s3_key} - Invalid S3 ETag format: {s3_etag}", 0, "error")

                    try:
                        part_count = int(etag_parts[1])
                    except ValueError:
                        return (f"[ERROR] {s3_key} - Invalid part count in ETag: {s3_etag}", 0, "error")

                    local_etag = calculate_s3_etag_multipart(local_path, part_count)
                    if local_etag is None:
                        return (f"[ERROR] {s3_key} - Failed to calculate multipart ETag", 0, "error")
                else:
                    # Simple ETag
                    local_etag = calculate_s3_etag_simple(local_path)

                # Compare ETags (case-insensitive)
                local_etag_hash = local_etag.split('-')[0] if '-' in local_etag else local_etag
                s3_etag_hash = s3_etag.split('-')[0] if '-' in s3_etag else s3_etag

                if local_etag_hash.lower() == s3_etag_hash.lower():
                    return (f"[VERIFIED] {s3_key} ({format_size(s3_size)})", 0, "verified")
                else:
                    return (f"[DIFFERENT] {s3_key} (S3: {s3_etag}, Local: {local_etag})", 0, "different")

            except IOError as e:
                return (f"[ERROR] {s3_key} - File read error: {str(e)}", 0, "error")
            except OSError as e:
                return (f"[ERROR] {s3_key} - File access error: {str(e)}", 0, "error")

        else:
            # STEP 4B: File does not exist - DOWNLOAD then VERIFY

            # Handle dry-run mode
            if dry_run:
                return (f"[DRY-RUN] {s3_key} ({format_size(s3_size)} would be downloaded)", 0, "downloaded")

            # Create parent directories if needed
            local_dir = os.path.dirname(local_path)
            if local_dir:
                os.makedirs(local_dir, exist_ok=True)

            # Announce download start (for user feedback)
            safe_print(f"[DOWNLOADING] {s3_key} ({format_size(s3_size)})...")

            # Download file using boto3
            try:
                s3_client.download_file(bucket_name, s3_key, local_path)
            except Exception as e:
                return (f"[ERROR] {s3_key} - Download failed: {str(e)}", 0, "error")

            # Verify downloaded file (same logic as above)
            try:
                local_size = os.path.getsize(local_path)

                if local_size != s3_size:
                    size_diff = f"S3: {format_size(s3_size)}, Local: {format_size(local_size)}"
                    return (f"[DOWNLOADED] {s3_key} → [DIFFERENT] (size mismatch: {size_diff})", s3_size, "different")

                # Calculate ETag
                if '-' in s3_etag:
                    etag_parts = s3_etag.split('-')
                    part_count = int(etag_parts[1])
                    local_etag = calculate_s3_etag_multipart(local_path, part_count)
                    if local_etag is None:
                        return (f"[DOWNLOADED] {s3_key} → [ERROR] (ETag calculation failed)", s3_size, "error")
                else:
                    local_etag = calculate_s3_etag_simple(local_path)

                local_etag_hash = local_etag.split('-')[0] if '-' in local_etag else local_etag
                s3_etag_hash = s3_etag.split('-')[0] if '-' in s3_etag else s3_etag

                if local_etag_hash.lower() == s3_etag_hash.lower():
                    return (f"[DOWNLOADED] {s3_key} ({format_size(s3_size)}) → [VERIFIED]", s3_size, "downloaded")
                else:
                    return (f"[DOWNLOADED] {s3_key} → [DIFFERENT] (S3: {s3_etag}, Local: {local_etag})", s3_size, "different")

            except (IOError, OSError) as e:
                return (f"[DOWNLOADED] {s3_key} → [ERROR] (verification failed: {str(e)})", s3_size, "error")

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

def upload_worker(s3_client, bucket_name, file_path, relative_path, dry_run=False):
    """Handles logic for skipping or uploading a single file."""
    try:
        # 1. CHECK IF EXISTS
        if file_exists_in_bucket(s3_client, bucket_name, relative_path):
            return f"[SKIPPED]  {relative_path} (Already exists)"

        # 2. ANNOUNCE START
        # We print this immediately so you know it's working
        safe_print(f"[STARTING] {relative_path}...")

        # 3. CALCULATE SIZE (for report)
        size_bytes = os.path.getsize(file_path)
        size_gb = size_bytes / (1024 * 1024 * 1024)

        # 4. UPLOAD or DRY-RUN
        if dry_run:
            # Dry-run: skip actual upload, just report what would happen
            return f"[DRY-RUN]  {relative_path}  --  {size_gb:.2f} GB (would upload)"
        else:
            # Actual upload
            s3_client.upload_file(file_path, bucket_name, relative_path)
            return f"[SUCCESS]  {relative_path}  --  {size_gb:.2f} GB"

    except Exception as e:
        return f"[FAILURE]  {relative_path} - {str(e)}"

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

    files_to_upload = []
    total_size_bytes = 0

    print(f"Scanning files in {args.local_folder}...")

    # 1. Walk through the folder structure
    for root, dirs, files in os.walk(args.local_folder):
        # Skip hidden directories unless --allow-hidden-files is set
        if not args.allow_hidden_files:
            # Modify dirs in-place to prevent os.walk from descending into hidden directories
            dirs[:] = [d for d in dirs if not d.startswith('.')]

        for filename in files:
            # Skip hidden files unless --allow-hidden-files is set
            if not args.allow_hidden_files and filename.startswith('.'):
                continue

            local_path = os.path.join(root, filename)

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
    total_upload_size_bytes = 0

    # Start timer for speed calculation
    start_time = time.time()

    # 2. Execute
    with ThreadPoolExecutor(max_workers=args.max_threads) as executor:
        future_to_file = {
            executor.submit(upload_worker, s3_client, args.bucket_name, f[0], f[1], args.dry_run): f[1]
            for f in files_to_upload
        }

        for future in as_completed(future_to_file):
            try:
                result_message = future.result()
                safe_print(result_message)

                # Track statistics
                if "[SKIPPED]" in result_message:
                    skip_count += 1
                elif "[DRY-RUN]" in result_message or "[SUCCESS]" in result_message:
                    upload_count += 1
                    # Extract size from message (format: "-- X.XX GB")
                    if "--" in result_message and "GB" in result_message:
                        parts = result_message.split("--")[1].strip().split()
                        if len(parts) >= 2:
                            size_gb = float(parts[0])
                            total_upload_size_bytes += int(size_gb * 1024 * 1024 * 1024)
            except Exception as exc:
                safe_print(f"[CRITICAL ERROR] Thread crashed: {exc}")

    # Stop timer
    elapsed_time = time.time() - start_time

    print("\nAll operations complete!")

    # Print summary
    if args.dry_run:
        print("\n--- DRY-RUN SUMMARY ---")
        print(f"Total files scanned: {total_files}")
        print(f"Files that would be uploaded: {upload_count}")
        print(f"Files that would be skipped: {skip_count}")
        print(f"Total upload size: {format_size(total_upload_size_bytes)}")
        print(f"Upload speed: {format_speed(total_upload_size_bytes, elapsed_time)} ({format_size(total_upload_size_bytes)} in {elapsed_time:.2f} seconds)")
        print("\nNo files were actually uploaded (dry-run mode)")
    else:
        print("\n--- UPLOAD SUMMARY ---")
        print(f"Total files scanned: {total_files}")
        print(f"Files uploaded: {upload_count}")
        print(f"Files skipped (already exist): {skip_count}")
        print(f"Total uploaded size: {format_size(total_upload_size_bytes)}")
        print(f"Upload speed: {format_speed(total_upload_size_bytes, elapsed_time)} ({format_size(total_upload_size_bytes)} in {elapsed_time:.2f} seconds)")

def command_list(args, s3_client=None):
    """
    Execute the list command.

    Args:
        args: Command-line arguments with bucket_name, prefix, max_keys, etc.
        s3_client: Optional boto3 S3 client (for testing). If None, creates client from args.
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

    # List objects
    try:
        print(f"Listing objects in bucket: {args.bucket_name}")
        if args.prefix:
            print(f"Prefix filter: {args.prefix}")
        print()

        # Print header
        print(f"{'FILE PATH':<70} {'SIZE':<12} {'LAST MODIFIED':<25}")
        print("-" * 107)

        # Paginate through results using reusable helper
        total_objects = 0
        total_size = 0

        for obj in paginate_s3_objects(s3_client, args.bucket_name, args.prefix, args.max_keys):
            key = obj['Key']
            size_bytes = obj['Size']
            last_modified = obj['LastModified'].strftime('%Y-%m-%d %H:%M:%S %Z')

            # Format size
            size_str = format_size(size_bytes)

            # Print object info
            print(f"{key:<70} {size_str:<12} {last_modified:<25}")

            total_objects += 1
            total_size += size_bytes

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

    except ClientError as e:
        error_code = e.response['Error']['Code']
        error_msg = e.response['Error']['Message']
        print(f"Error listing bucket: {error_code} - {error_msg}")
    except Exception as e:
        print(f"Error: {str(e)}")

def command_verify(args, s3_client=None):
    """Execute the verify command.

    Args:
        args: Command-line arguments
        s3_client: Optional boto3 S3 client (for testing)
    """

    # Create S3 client using command-line arguments
    if s3_client is None:
        s3_client = boto3.client(
            's3',
            endpoint_url=args.endpoint_url,
            aws_access_key_id=args.access_key,
            aws_secret_access_key=args.secret_key
        )

    files_to_verify = []

    print(f"Scanning files in {args.local_folder}...")

    # Walk through the folder structure
    for root, dirs, files in os.walk(args.local_folder):
        # Skip hidden directories unless --allow-hidden-files is set
        if not args.allow_hidden_files:
            # Modify dirs in-place to prevent os.walk from descending into hidden directories
            dirs[:] = [d for d in dirs if not d.startswith('.')]

        for filename in files:
            # Skip hidden files unless --allow-hidden-files is set
            if not args.allow_hidden_files and filename.startswith('.'):
                continue

            local_path = os.path.join(root, filename)

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
                result_message = future.result()
                safe_print(result_message)

                # Get file size for ALL processed files
                try:
                    file_size = os.path.getsize(file_path)
                    total_verified_size_bytes += file_size
                except (IOError, OSError):
                    pass  # If can't get size, don't count it

                # Track statistics based on status in message
                if "[VERIFIED]" in result_message:
                    verified_count += 1
                elif "[DIFFERENT]" in result_message:
                    different_count += 1
                elif "[NOT UPLOADED]" in result_message:
                    not_uploaded_count += 1
                elif "[ERROR]" in result_message:
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
    print(f"Verify speed: {format_speed(total_verified_size_bytes, elapsed_time)} ({format_size(total_verified_size_bytes)} in {elapsed_time:.2f} seconds)")

    # Exit with non-zero code if any mismatches or errors
    if different_count > 0 or error_count > 0:
        import sys
        sys.exit(1)

def generate_html_index(objects, total_objects, total_size, url_prefix):
    """Generate HTML index file content."""
    from datetime import datetime

    # Ensure url_prefix ends with slash
    if url_prefix and not url_prefix.endswith('/'):
        url_prefix += '/'

    # Start HTML document
    html = []
    html.append('<!DOCTYPE html>')
    html.append('<html>')
    html.append('<head>')
    html.append('  <meta charset="UTF-8">')
    html.append('  <meta name="viewport" content="width=device-width, initial-scale=1.0">')
    html.append('  <title>Files Index</title>')
    html.append('  <style>')
    html.append('    body { font-family: Arial, sans-serif; margin: 20px; }')
    html.append('    h1 { color: #333; }')
    html.append('    table { border-collapse: collapse; width: 100%; }')
    html.append('    th, td { border: 1px solid #ddd; padding: 8px; text-align: left; }')
    html.append('    th { background-color: #f2f2f2; }')
    html.append('    tr:hover { background-color: #f5f5f5; }')
    html.append('    a { color: #0066cc; text-decoration: none; }')
    html.append('    a:hover { text-decoration: underline; }')
    html.append('    .summary { margin-top: 20px; font-weight: bold; }')
    html.append('  </style>')
    html.append('</head>')
    html.append('<body>')
    html.append('  <h1>Files Index</h1>')
    html.append('  <table>')
    html.append('    <thead>')
    html.append('      <tr>')
    html.append('        <th>File Path</th>')
    html.append('        <th>Size</th>')
    html.append('        <th>Last Modified</th>')
    html.append('      </tr>')
    html.append('    </thead>')
    html.append('    <tbody>')

    # Add table rows
    for obj in objects:
        key = obj['key']
        size_str = format_size(obj['size'])
        last_modified_str = obj['last_modified'].strftime('%Y-%m-%d %H:%M:%S %Z')
        download_url = (url_prefix + key) if url_prefix else key

        # Escape HTML special characters in key
        key_escaped = key.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')

        html.append('      <tr>')
        html.append(f'        <td><a href="{download_url}">{key_escaped}</a></td>')
        html.append(f'        <td>{size_str}</td>')
        html.append(f'        <td>{last_modified_str}</td>')
        html.append('      </tr>')

    html.append('    </tbody>')
    html.append('  </table>')
    html.append(f'  <div class="summary">Total: {total_objects} objects, {format_size(total_size)}</div>')
    html.append(f'  <p><em>Generated on {datetime.utcnow().strftime("%Y-%m-%d %H:%M:%S UTC")}</em></p>')
    html.append('</body>')
    html.append('</html>')

    return '\n'.join(html)

def generate_markdown_index(objects, total_objects, total_size, url_prefix):
    """Generate Markdown index file content."""
    from datetime import datetime

    # Ensure url_prefix ends with slash
    if url_prefix and not url_prefix.endswith('/'):
        url_prefix += '/'

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
        download_url = (url_prefix + key) if url_prefix else key

        # Escape Markdown special characters in key (pipe character)
        key_escaped = key.replace('|', '\\|')

        # Create Markdown link
        md.append(f'| [{key_escaped}]({download_url}) | {size_str} | {last_modified_str} |')

    # Add summary
    md.append('')
    md.append(f'**Total:** {total_objects} objects, {format_size(total_size)}')
    md.append('')
    md.append(f'*Generated on {datetime.utcnow().strftime("%Y-%m-%d %H:%M:%S UTC")}*')

    return '\n'.join(md)

def command_indexupload(args, s3_client=None):
    """Execute the indexupload command.

    Args:
        args: Command-line arguments
        s3_client: Optional boto3 S3 client (for testing)
    """
    import tempfile
    from datetime import datetime

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
        return

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
        return

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
            s3_client.upload_file(html_path, args.bucket_name, 'index.html')
            print("[SUCCESS] index.html uploaded")

            s3_client.upload_file(md_path, args.bucket_name, 'index.md')
            print("[SUCCESS] index.md uploaded")

            print("\nIndex files uploaded successfully!")
        except Exception as e:
            print(f"[ERROR] Failed to upload index files: {str(e)}")
            import sys
            sys.exit(1)

def command_download(args, s3_client=None):
    """Execute the download command.

    Args:
        args: Command-line arguments
        s3_client: Optional boto3 S3 client (for testing)
    """

    # STEP 1: Create S3 client (same pattern as other commands)
    if s3_client is None:
        s3_client = boto3.client(
            's3',
            endpoint_url=args.endpoint_url,
            aws_access_key_id=args.access_key,
            aws_secret_access_key=args.secret_key
        )

    # STEP 2: List S3 objects using pagination generator
    print(f"Listing objects in bucket: {args.bucket_name}")
    if args.prefix:
        print(f"Prefix filter: {args.prefix}")
    print()

    # STEP 3: Paginate through S3 objects and build download queue
    try:
        all_s3_objects = list(paginate_s3_objects(s3_client, args.bucket_name, args.prefix))
    except ClientError as e:
        error_code = e.response['Error']['Code']
        error_msg = e.response['Error']['Message']
        print(f"Error listing bucket: {error_code} - {error_msg}")
        import sys
        sys.exit(1)
    except Exception as e:
        print(f"Error: {str(e)}")
        import sys
        sys.exit(1)

    # Check if bucket is empty or no objects match prefix
    if not all_s3_objects:
        if args.prefix:
            print(f"No objects found with prefix: {args.prefix}")
        else:
            print("Bucket is empty")
        print("Nothing to download.")
        return

    # Build download queue from S3 objects
    download_queue = [(obj['Key'], obj['Size']) for obj in all_s3_objects]
    total_s3_size = sum(obj['Size'] for obj in all_s3_objects)

    total_files = len(download_queue)
    print(f"Found {total_files} objects ({format_size(total_s3_size)} total)")
    print(f"Local folder: {args.local_folder}")
    print()

    if args.dry_run:
        print("Running in DRY-RUN mode (no files will be downloaded)")
        print()

    # STEP 4: Initialize statistics tracking
    downloaded_count = 0
    verified_count = 0
    different_count = 0
    error_count = 0
    total_downloaded_size = 0
    total_verified_size = 0

    # Start timer for speed calculation
    start_time = time.time()

    # STEP 5: Execute downloads with ThreadPoolExecutor
    with ThreadPoolExecutor(max_workers=args.max_threads) as executor:
        future_to_file = {
            executor.submit(download_worker, s3_client, args.bucket_name, s3_key, args.local_folder, args.dry_run): (s3_key, s3_size)
            for s3_key, s3_size in download_queue
        }

        for future in as_completed(future_to_file):
            s3_key, s3_size = future_to_file[future]
            try:
                status_message, downloaded_size, status_category = future.result()
                safe_print(status_message)

                # Track statistics
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

            except Exception as exc:
                safe_print(f"[CRITICAL ERROR] Thread crashed processing {s3_key}: {exc}")
                error_count += 1

    # Stop timer
    elapsed_time = time.time() - start_time

    # STEP 6: Print summary
    print("\nAll operations complete!")
    print("\n--- DOWNLOAD SUMMARY ---")
    print(f"Total files found: {total_files}")
    print(f"Downloaded (new files): {downloaded_count} ({format_size(total_downloaded_size)})")
    print(f"Verified (existing files, match): {verified_count} ({format_size(total_verified_size)})")
    print(f"Different (existing files, mismatch): {different_count}")
    print(f"Errors: {error_count}")
    print(f"Download speed: {format_speed(total_downloaded_size, elapsed_time)} ({format_size(total_downloaded_size)} in {elapsed_time:.2f} seconds)")

    if args.dry_run:
        print("\nNo files were actually downloaded (dry-run mode)")

    # STEP 7: Exit with appropriate code (like verify command)
    if different_count > 0 or error_count > 0:
        import sys
        sys.exit(1)

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
        command_upload(args)
    elif args.command == 'list':
        command_list(args)
    elif args.command == 'verify':
        command_verify(args)
    elif args.command == 'indexupload':
        command_indexupload(args)
    elif args.command == 'download':
        command_download(args)
    else:
        parser.error(f"Unknown command: {args.command}")

if __name__ == "__main__":
    main()