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

def main():
    # Parse command-line arguments
    parser = argparse.ArgumentParser(
        description='Upload files to S3 with duplicate detection and parallel uploads'
    )

    # Required arguments
    parser.add_argument('--account-id', required=True, metavar='ID',
                        help='S3 account ID')
    parser.add_argument('--access-key', required=True, metavar='KEY',
                        help='S3 access key ID')
    parser.add_argument('--secret-key', required=True, metavar='SECRET',
                        help='S3 secret access key')
    parser.add_argument('--bucket-name', required=True, metavar='NAME',
                        help='Target bucket name')
    parser.add_argument('--local-folder', required=True, metavar='PATH',
                        help='Local directory to upload')
    parser.add_argument('--endpoint-url', required=True, metavar='URL',
                        help='S3 endpoint URL')

    # Optional arguments
    parser.add_argument('--max-threads', type=int, default=3, metavar='N',
                        help='Number of parallel uploads (default: 3)')
    parser.add_argument('--dry-run', action='store_true',
                        help='Show what would be uploaded without actually uploading')
    parser.add_argument('--allow-hidden-files', action='store_true',
                        help='Include hidden files and directories (those starting with ".")')

    args = parser.parse_args()

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

if __name__ == "__main__":
    main()