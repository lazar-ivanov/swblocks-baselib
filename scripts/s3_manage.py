import os
import argparse
import boto3
import threading
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

def command_upload(args):
    """Execute the upload command."""
    # Create S3 client using command-line arguments
    # Note: boto3 uses 'aws_access_key_id' and 'aws_secret_access_key' parameter names
    # for all S3-compatible services (not just AWS)
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
    total_upload_size_gb = 0.0

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
                            total_upload_size_gb += float(parts[0])
            except Exception as exc:
                safe_print(f"[CRITICAL ERROR] Thread crashed: {exc}")

    print("\nAll operations complete!")

    # Print dry-run summary if in dry-run mode
    if args.dry_run:
        print("\n--- DRY-RUN SUMMARY ---")
        print(f"Total files scanned: {total_files}")
        print(f"Files that would be uploaded: {upload_count}")
        print(f"Files that would be skipped: {skip_count}")
        print(f"Total upload size: {total_upload_size_gb:.2f} GB")
        print("\nNo files were actually uploaded (dry-run mode)")

def command_list(args):
    """Execute the list command."""
    # Create S3 client using command-line arguments
    # Note: boto3 uses 'aws_access_key_id' and 'aws_secret_access_key' parameter names
    # for all S3-compatible services (not just AWS)
    s3_client = boto3.client(
        's3',
        endpoint_url=args.endpoint_url,
        aws_access_key_id=args.access_key,
        aws_secret_access_key=args.secret_key
    )

    # Prepare list_objects_v2 parameters
    list_params = {
        'Bucket': args.bucket_name
    }

    if args.prefix:
        list_params['Prefix'] = args.prefix

    if args.max_keys:
        list_params['MaxKeys'] = args.max_keys

    # List objects
    try:
        print(f"Listing objects in bucket: {args.bucket_name}")
        if args.prefix:
            print(f"Prefix filter: {args.prefix}")
        print()

        # Print header
        print(f"{'FILE PATH':<70} {'SIZE':<12} {'LAST MODIFIED':<25}")
        print("-" * 107)

        # Paginate through results
        total_objects = 0
        total_size = 0  # Accumulate total size in bytes for aggregate statistics
        continuation_token = None

        while True:
            if continuation_token:
                list_params['ContinuationToken'] = continuation_token

            response = s3_client.list_objects_v2(**list_params)

            # Check if bucket is empty or no objects match prefix
            if 'Contents' not in response:
                if total_objects == 0:
                    if args.prefix:
                        print(f"No objects found with prefix: {args.prefix}")
                    else:
                        print("Bucket is empty")
                break

            # Process objects
            for obj in response['Contents']:
                key = obj['Key']
                size_bytes = obj['Size']
                last_modified = obj['LastModified'].strftime('%Y-%m-%d %H:%M:%S %Z')

                # Format size
                size_str = format_size(size_bytes)

                # Print object info
                print(f"{key:<70} {size_str:<12} {last_modified:<25}")

                total_objects += 1
                total_size += size_bytes

            # Check if there are more results
            if not response.get('IsTruncated', False):
                break

            continuation_token = response.get('NextContinuationToken')

            # If max_keys is set, stop after first page
            if args.max_keys:
                if response.get('IsTruncated', False):
                    print(f"\n(Results limited to {args.max_keys} objects. Use --max-keys to adjust or remove to see all.)")
                break

        # Print summary
        print("-" * 107)
        print(f"Total: {total_objects} objects, {format_size(total_size)}")

    except ClientError as e:
        error_code = e.response['Error']['Code']
        error_msg = e.response['Error']['Message']
        print(f"Error listing bucket: {error_code} - {error_msg}")
    except Exception as e:
        print(f"Error: {str(e)}")

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

    # Parse arguments
    args = parser.parse_args()

    # Dispatch to appropriate command
    if args.command == 'upload':
        command_upload(args)
    elif args.command == 'list':
        command_list(args)
    else:
        parser.error(f"Unknown command: {args.command}")

if __name__ == "__main__":
    main()