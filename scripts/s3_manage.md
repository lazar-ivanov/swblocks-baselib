# s3_manage.py - S3 Management Tool

A command-line tool for managing S3-compatible object storage with support for upload, list, and verify operations.

## Overview

`s3_manage.py` is a Python script that provides four main commands for working with S3-compatible storage:
- **upload**: Upload local files to an S3 bucket with parallel execution and smart skip logic
- **list**: List objects in an S3 bucket with filtering and pagination
- **verify**: Verify local files against S3 objects by comparing checksums (ETags)
- **indexupload**: Generate HTML and Markdown index files listing bucket contents with download links

## Requirements

- Python 3.x
- boto3 library: `pip install boto3`

## Common Arguments

All commands share the following required arguments:

| Argument | Description |
|----------|-------------|
| `--account-id ID` | S3 account ID |
| `--access-key KEY` | S3 access key ID |
| `--secret-key SECRET` | S3 secret access key |
| `--bucket-name NAME` | Target S3 bucket name |
| `--endpoint-url URL` | S3 endpoint URL |

## Commands

### 1. upload - Upload Files to S3

Upload files from a local directory to an S3 bucket with parallel execution and intelligent skip logic.

#### Usage

```bash
python scripts/s3_manage.py upload \
  --account-id <id> \
  --access-key <key> \
  --secret-key <secret> \
  --bucket-name <bucket> \
  --endpoint-url <url> \
  --local-folder <path> \
  [--max-threads <n>] \
  [--dry-run] \
  [--allow-hidden-files]
```

#### Arguments

| Argument | Required | Default | Description |
|----------|----------|---------|-------------|
| `--local-folder PATH` | Yes | - | Local directory to upload |
| `--max-threads N` | No | 3 | Number of parallel upload threads |
| `--dry-run` | No | False | Show what would be uploaded without actually uploading |
| `--allow-hidden-files` | No | False | Include hidden files and directories (starting with `.`) |

#### Features

- **Smart Skip Logic**: Automatically skips files that already exist in S3
- **Parallel Uploads**: Uses ThreadPoolExecutor for concurrent uploads (configurable)
- **Hidden File Filtering**: By default, skips hidden files and directories (starting with `.`)
- **Dry-Run Mode**: Preview what would be uploaded without making changes
- **Progress Tracking**: Real-time status messages for each file
- **Summary Statistics**: Shows upload counts, skip counts, and total upload size

#### Output Format

```
[STARTING] path/to/file.txt...
[SUCCESS]  path/to/file.txt  --  1.23 GB
[SKIPPED]  path/to/existing.txt (Already exists)

All operations complete!
```

#### Example: Basic Upload

```bash
python scripts/s3_manage.py upload \
  --account-id "my-account" \
  --access-key "my-key" \
  --secret-key "my-secret" \
  --bucket-name "my-bucket" \
  --endpoint-url "https://s3.example.com" \
  --local-folder ./build_artifacts
```

#### Example: Dry-Run with Hidden Files

```bash
python scripts/s3_manage.py upload \
  --account-id "my-account" \
  --access-key "my-key" \
  --secret-key "my-secret" \
  --bucket-name "my-bucket" \
  --endpoint-url "https://s3.example.com" \
  --local-folder ./config \
  --dry-run \
  --allow-hidden-files
```

**Output:**
```
Scanning files in ./config...
Found 10 files. Checking for existing files & starting upload...

[DRY-RUN]  config.json  --  0.01 GB (would upload)
[DRY-RUN]  .env  --  0.00 GB (would upload)
[SKIPPED]  settings.yaml (Already exists)

All operations complete!

--- DRY-RUN SUMMARY ---
Total files scanned: 10
Files that would be uploaded: 8
Files that would be skipped: 2
Total upload size: 2.45 GB

No files were actually uploaded (dry-run mode)
```

#### Example: High-Performance Upload

```bash
python scripts/s3_manage.py upload \
  --account-id "my-account" \
  --access-key "my-key" \
  --secret-key "my-secret" \
  --bucket-name "my-bucket" \
  --endpoint-url "https://s3.example.com" \
  --local-folder ./large_dataset \
  --max-threads 10
```

---

### 2. list - List Objects in S3 Bucket

List objects in an S3 bucket with optional prefix filtering and pagination support.

#### Usage

```bash
python scripts/s3_manage.py list \
  --account-id <id> \
  --access-key <key> \
  --secret-key <secret> \
  --bucket-name <bucket> \
  --endpoint-url <url> \
  [--prefix <prefix>] \
  [--max-keys <n>]
```

#### Arguments

| Argument | Required | Default | Description |
|----------|----------|---------|-------------|
| `--prefix PREFIX` | No | - | Filter objects by prefix (e.g., "folder/subfolder/") |
| `--max-keys N` | No | - | Maximum number of objects to list (paginate) |

#### Features

- **Prefix Filtering**: List only objects matching a specific prefix (folder path)
- **Pagination Support**: Limit results with `--max-keys` for quick inspection
- **Human-Readable Sizes**: File sizes displayed in B, KB, MB, GB, TB, PB
- **Aggregate Statistics**: Shows total object count and total storage size
- **Columnar Output**: Aligned columns for easy reading

#### Output Format

```
FILE PATH                                                    SIZE         LAST MODIFIED
-------------------------------------------------------------------------------------------------
path/to/file1.txt                                            1.23 MB      2024-01-15 10:30:45 UTC
path/to/file2.bin                                            456.78 KB    2024-01-16 14:22:10 UTC
-------------------------------------------------------------------------------------------------
Total: 2 objects, 1.69 MB
```

#### Example: List All Objects

```bash
python scripts/s3_manage.py list \
  --account-id "my-account" \
  --access-key "my-key" \
  --secret-key "my-secret" \
  --bucket-name "my-bucket" \
  --endpoint-url "https://s3.example.com"
```

#### Example: List with Prefix Filter

```bash
python scripts/s3_manage.py list \
  --account-id "my-account" \
  --access-key "my-key" \
  --secret-key "my-secret" \
  --bucket-name "my-bucket" \
  --endpoint-url "https://s3.example.com" \
  --prefix "builds/2024/"
```

**Output:**
```
Listing objects in bucket: my-bucket
Prefix filter: builds/2024/

FILE PATH                                                    SIZE         LAST MODIFIED
-------------------------------------------------------------------------------------------------
builds/2024/01/app-v1.0.tar.gz                               125.45 MB    2024-01-15 10:30:45 UTC
builds/2024/01/app-v1.1.tar.gz                               128.32 MB    2024-01-20 15:45:12 UTC
builds/2024/02/app-v1.2.tar.gz                               130.21 MB    2024-02-05 09:12:33 UTC
-------------------------------------------------------------------------------------------------
Total: 3 objects, 383.98 MB
```

#### Example: Quick Bucket Inspection (Limited Results)

```bash
python scripts/s3_manage.py list \
  --account-id "my-account" \
  --access-key "my-key" \
  --secret-key "my-secret" \
  --bucket-name "my-bucket" \
  --endpoint-url "https://s3.example.com" \
  --max-keys 10
```

**Output:**
```
Listing objects in bucket: my-bucket

FILE PATH                                                    SIZE         LAST MODIFIED
-------------------------------------------------------------------------------------------------
file1.txt                                                    1.23 KB      2024-01-15 10:30:45 UTC
file2.txt                                                    2.34 KB      2024-01-16 11:15:22 UTC
[... 8 more files ...]

(Results limited to 10 objects. Use --max-keys to adjust or remove to see all.)
-------------------------------------------------------------------------------------------------
Total: 10 objects, 45.67 KB
```

---

### 3. verify - Verify Files Against S3

Verify that local files match their S3 counterparts by comparing ETags (checksums). This command is useful for ensuring upload integrity or detecting local modifications.

#### Usage

```bash
python scripts/s3_manage.py verify \
  --account-id <id> \
  --access-key <key> \
  --secret-key <secret> \
  --bucket-name <bucket> \
  --endpoint-url <url> \
  --local-folder <path> \
  [--max-threads <n>] \
  [--allow-hidden-files]
```

#### Arguments

| Argument | Required | Default | Description |
|----------|----------|---------|-------------|
| `--local-folder PATH` | Yes | - | Local directory to verify against S3 |
| `--max-threads N` | No | 3 | Number of parallel verification threads |
| `--allow-hidden-files` | No | False | Include hidden files and directories (starting with `.`) |

#### Features

- **100% Accurate ETag Matching**: Uses deterministic algorithm to calculate ETags matching S3's method
- **Multipart Upload Support**: Correctly handles files uploaded via multipart (large files)
- **Parallel Verification**: Uses ThreadPoolExecutor for concurrent verification
- **Hidden File Filtering**: By default, skips hidden files and directories (starting with `.`)
- **Detailed Status Reporting**: Four verification states (VERIFIED, DIFFERENT, NOT UPLOADED, ERROR)
- **CI/CD Friendly**: Returns exit code 1 for failures, 0 for success
- **Comprehensive Summary**: Shows counts for all verification categories

#### ETag Calculation Algorithm

The verify command accurately calculates ETags for both simple and multipart uploads:

**Simple Uploads (< 8MB):**
- ETag = MD5 hash of file content
- Format: `xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx` (32 hex chars, no hyphen)

**Multipart Uploads (>= 8MB):**
- boto3 automatically uses multipart for large files
- Default chunk size: 8MB (can be 5MB, 16MB, 32MB, 64MB, 128MB, 256MB, 512MB)
- Algorithm:
  1. Deterministically find chunk size by testing standard sizes against file size and part count
  2. Split file into chunks
  3. Calculate MD5 for each chunk
  4. Concatenate all MD5 digests (binary)
  5. Calculate MD5 of concatenated digests
  6. Format: `xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx-N` (hash + hyphen + part count)

#### Verification States

| Status | Description | Exit Code |
|--------|-------------|-----------|
| `[VERIFIED]` | File matches S3 exactly (ETag match) | 0 |
| `[DIFFERENT]` | File exists in S3 but ETag differs (modified) | 1 |
| `[NOT UPLOADED]` | File does not exist in S3 | 1 |
| `[ERROR]` | Verification error (permissions, network, etc.) | 1 |

#### Output Format

```
Scanning files in ./build_artifacts...
Found 5 files. Starting verification...

[VERIFIED] file1.txt
[DIFFERENT] modified_file.txt (S3: abc123..., Local: def456...)
[NOT UPLOADED] new_file.txt
[ERROR] restricted.txt - File read error: Permission denied

All verifications complete!

--- VERIFICATION SUMMARY ---
Total files scanned: 5
Verified (match): 2
Different (mismatch): 1
Not uploaded to S3: 1
Errors: 1
```

**Exit code:** 1 (non-zero indicates failures)

#### Example: Basic Verification

```bash
python scripts/s3_manage.py verify \
  --account-id "my-account" \
  --access-key "my-key" \
  --secret-key "my-secret" \
  --bucket-name "my-bucket" \
  --endpoint-url "https://s3.example.com" \
  --local-folder ./build_artifacts
```

#### Example: Verify with Hidden Files

```bash
python scripts/s3_manage.py verify \
  --account-id "my-account" \
  --access-key "my-key" \
  --secret-key "my-secret" \
  --bucket-name "my-bucket" \
  --endpoint-url "https://s3.example.com" \
  --local-folder ./config \
  --allow-hidden-files
```

#### Example: High-Performance Verification

```bash
python scripts/s3_manage.py verify \
  --account-id "my-account" \
  --access-key "my-key" \
  --secret-key "my-secret" \
  --bucket-name "my-bucket" \
  --endpoint-url "https://s3.example.com" \
  --local-folder ./large_dataset \
  --max-threads 10
```

#### Example: Upload → Verify Workflow (CI/CD)

```bash
#!/bin/bash
# Deploy script with verification

# Step 1: Upload build artifacts
python scripts/s3_manage.py upload \
  --account-id "my-account" \
  --access-key "my-key" \
  --secret-key "my-secret" \
  --bucket-name "my-bucket" \
  --endpoint-url "https://s3.example.com" \
  --local-folder ./build_artifacts \
  --max-threads 5 || exit 1

# Step 2: Verify upload integrity
python scripts/s3_manage.py verify \
  --account-id "my-account" \
  --access-key "my-key" \
  --secret-key "my-secret" \
  --bucket-name "my-bucket" \
  --endpoint-url "https://s3.example.com" \
  --local-folder ./build_artifacts \
  --max-threads 5 || {
  echo "ERROR: Verification failed! Upload may be corrupted."
  exit 1
}

echo "SUCCESS: All files uploaded and verified"
```

---

### 4. indexupload - Generate Index Files

Generate HTML and Markdown index files listing all objects in an S3 bucket with clickable download links, then upload the index files to the bucket root. This creates static directory listings that can be viewed in a browser or Markdown viewer.

#### Usage

```bash
python scripts/s3_manage.py indexupload \
  --account-id <id> \
  --access-key <key> \
  --secret-key <secret> \
  --bucket-name <bucket> \
  --endpoint-url <url> \
  --url-prefix <url> \
  [--prefix <prefix>]
```

#### Arguments

| Argument | Required | Default | Description |
|----------|----------|---------|-------------|
| `--url-prefix URL` | Yes | - | Base URL for generating file download links (e.g., `https://storage.example.com/mybucket/`) |
| `--prefix PREFIX` | No | - | Filter objects by prefix (e.g., "folder/subfolder/") |

#### Features

- **Dual Format Output**: Generates both HTML and Markdown index files
- **Clickable Links**: All file paths are hyperlinked using the provided URL prefix
- **Aggregate Statistics**: Shows total object count and total storage size
- **Self-Exclusion**: Automatically excludes `index.html` and `index.md` from generated lists
- **Prefix Filtering**: Generate indexes for specific subdirectories
- **Automatic Pagination**: Handles buckets with thousands of objects
- **UTC Timestamps**: Includes generation timestamp in both formats
- **Character Escaping**: Properly escapes HTML (`&<>`) and Markdown (`|`) special characters

#### Generated Files

**index.html:**
- Simple, clean HTML table with minimal styling
- Responsive design (works on mobile devices)
- Hover effects on table rows
- Columns: File Path (linked), Size, Last Modified
- Summary footer with aggregate statistics

**index.md:**
- Standard Markdown table format
- GitHub/GitLab compatible
- Columns: File Path (linked), Size, Last Modified
- Summary footer with aggregate statistics

#### Output Format

**Console Output:**
```
Listing objects in bucket: my-bucket

Found 15 objects (excluded index.html, index.md)

Generated index files:
  - index.html (2341 bytes)
  - index.md (1523 bytes)

Uploading index files to bucket root...
[SUCCESS] index.html uploaded
[SUCCESS] index.md uploaded

Index files uploaded successfully!
```

**Example index.html (rendered in browser):**

| File Path | Size | Last Modified |
|-----------|------|---------------|
| [devenv/4/dist-devenv4-ub18.tar.gz](https://storage.example.com/my-bucket/devenv/4/dist-devenv4-ub18.tar.gz) | 1.23 GB | 2024-03-15 10:30:45 UTC |
| [file1.txt](https://storage.example.com/my-bucket/file1.txt) | 124 B | 2024-03-16 14:22:10 UTC |
| [data/report.pdf](https://storage.example.com/my-bucket/data/report.pdf) | 2.45 MB | 2024-03-16 15:00:00 UTC |

**Total:** 15 objects, 5.67 GB

*Generated on 2024-03-20 12:00:00 UTC*

#### Example: Basic Index Generation

```bash
python scripts/s3_manage.py indexupload \
  --account-id "my-account" \
  --access-key "my-key" \
  --secret-key "my-secret" \
  --bucket-name "my-bucket" \
  --endpoint-url "https://s3.example.com" \
  --url-prefix "https://storage.example.com/my-bucket/"
```

**Use Case:** Static website hosting - users can browse `https://storage.example.com/my-bucket/index.html` to see all available files.

#### Example: Generate Index for Subdirectory

```bash
python scripts/s3_manage.py indexupload \
  --account-id "my-account" \
  --access-key "my-key" \
  --secret-key "my-secret" \
  --bucket-name "my-bucket" \
  --endpoint-url "https://s3.example.com" \
  --url-prefix "https://storage.example.com/my-bucket/" \
  --prefix "devenv/7/"
```

**Use Case:** Build artifacts browser - developers can view `index.html` to browse available builds for a specific version.

#### Example: Complete Deployment Workflow

```bash
#!/bin/bash
# Deploy build artifacts with index generation

# Step 1: Upload build artifacts
python scripts/s3_manage.py upload \
  --account-id "my-account" \
  --access-key "my-key" \
  --secret-key "my-secret" \
  --bucket-name "build-artifacts" \
  --endpoint-url "https://s3.example.com" \
  --local-folder ./dist \
  --max-threads 5 || exit 1

# Step 2: Generate and upload index files
python scripts/s3_manage.py indexupload \
  --account-id "my-account" \
  --access-key "my-key" \
  --secret-key "my-secret" \
  --bucket-name "build-artifacts" \
  --endpoint-url "https://s3.example.com" \
  --url-prefix "https://builds.example.com/" || exit 1

# Step 3: Announce deployment
echo "Deployment complete! View index at: https://builds.example.com/index.html"
```

#### Use Cases

**1. Static Website Hosting**
- Generate browsable directory listing for static websites
- Users can click links to download files directly
- No server-side code needed

**2. Build Artifact Repository**
- Create index of build artifacts for developers
- Easy browsing of available versions
- Direct download links for CI/CD tools

**3. Documentation Repository**
- Generate Markdown index for GitHub/GitLab
- Users can view index.md in repository
- Links work directly in web interface

**4. Public File Distribution**
- Create professional-looking download page
- No custom web application needed
- Self-updating on each run

#### URL Prefix Handling

The `--url-prefix` parameter is used to construct download URLs:

- **With trailing slash:** `https://storage.example.com/my-bucket/`
  - Result: `https://storage.example.com/my-bucket/file.txt`

- **Without trailing slash:** `https://storage.example.com/my-bucket`
  - Result: `https://storage.example.com/my-bucket/file.txt` (slash added automatically)

#### Index File Exclusion

When generating indexes:
- Existing `index.html` and `index.md` files in the bucket are excluded from the list
- This prevents self-referencing and allows re-running the command safely
- After first run, index files exist in bucket but won't appear in their own listings

#### Empty Bucket Handling

If the bucket is empty or no objects match the prefix filter:
```
Bucket is empty
No index files will be generated.
```

The command exits gracefully without creating empty index files.

#### Character Escaping

**HTML Files:**
- Special characters in filenames are properly escaped
- `&` → `&amp;`, `<` → `&lt;`, `>` → `&gt;`
- Prevents XSS vulnerabilities

**Markdown Files:**
- Pipe characters in filenames are escaped
- `|` → `\\|`
- Prevents table formatting issues

---

## Error Handling

All commands handle errors gracefully and provide clear error messages:

### Upload Errors

- **File not found:** Skipped with warning
- **Permission denied:** Reported as failure
- **Network error:** Reported as failure with details
- **S3 API error:** Reported with error code and message

### List Errors

- **Empty bucket:** Displays "Bucket is empty"
- **No matches for prefix:** Displays "No objects found with prefix: {prefix}"
- **Access denied:** Displays error code and message
- **Bucket not found:** Displays error code and message

### Verify Errors

- **File not in S3:** Reported as `[NOT UPLOADED]`
- **File read error:** Reported as `[ERROR]` with details
- **S3 API error:** Reported as `[ERROR]` with error code
- **ETag calculation failure:** Reported as `[ERROR]` (rare)

### IndexUpload Errors

- **Empty bucket:** Displays message, exits gracefully without generating files
- **No matches for prefix:** Displays message, exits gracefully
- **Upload failure:** Displays error message, exits with code 1
- **Access denied:** Displays S3 error code and message
- **Disk full:** Propagates error during file generation

---

## Performance Considerations

### Threading

- **Default threads:** 3 (balances performance and API rate limits)
- **Recommended range:** 3-10 threads for most use cases
- **S3 rate limits:** Be cautious with very high thread counts (may trigger rate limiting)

### Memory Usage

- Files are read in chunks (8KB for verification, configurable for uploads)
- Large files do not load entirely into memory
- ThreadPoolExecutor limits concurrent file operations

### Network Efficiency

- **upload:** Uses `head_object()` to check existence before upload (minimal data transfer)
- **list:** Uses `list_objects_v2()` API (metadata only, no file downloads)
- **verify:** Uses `head_object()` for metadata (minimal data transfer, no file downloads)
- **indexupload:** Uses `list_objects_v2()` API (metadata only, no file downloads)

### IndexUpload-Specific Considerations

- **Full Bucket Scan:** Must paginate through entire bucket to collect all objects
- **Memory Usage:** Stores all objects in memory (~100 bytes per object, ~1MB for 10,000 objects)
- **Generation Time:** Large buckets (10,000+ objects) may take several minutes to list
- **File Size:** Generated HTML/MD files scale with object count (~500KB HTML for 10,000 objects)

---

## Security Notes

1. **Credentials:** Never hardcode credentials in scripts. Use environment variables or secure configuration management.
2. **Access Keys:** Rotate access keys regularly.
3. **Bucket Permissions:** Ensure the S3 user has appropriate permissions (read, write, list).
4. **Hidden Files:** By default, hidden files (like `.env`, `.git/`) are skipped to avoid accidentally uploading sensitive data.

---

## Troubleshooting

### Issue: "Command not found"

**Solution:** Ensure Python is installed and in your PATH:
```bash
python --version  # or python3 --version
```

### Issue: "ModuleNotFoundError: No module named 'boto3'"

**Solution:** Install boto3:
```bash
pip install boto3
# or
pip3 install boto3
```

### Issue: "Access Denied" errors

**Solution:** Verify your credentials and bucket permissions:
1. Check `--access-key` and `--secret-key` are correct
2. Ensure the S3 user has permissions for the requested operations
3. Verify `--bucket-name` is correct

### Issue: "Connection timeout"

**Solution:** Check network connectivity and endpoint URL:
1. Verify `--endpoint-url` is reachable
2. Check firewall/proxy settings
3. Test with a simple curl request

### Issue: Verify shows "[DIFFERENT]" for unchanged files

**Solution:** This can happen if:
1. File was modified locally after upload
2. File was uploaded with different tool (different chunk size)
3. S3 object was replaced (check last modified date with `list` command)

### Issue: Upload is very slow

**Solution:**
1. Increase `--max-threads` (try 5-10)
2. Check network bandwidth
3. Verify S3 endpoint is not rate limiting

---

## Examples Summary

### Quick Reference

```bash
# Upload files (default: skip hidden, 3 threads)
python scripts/s3_manage.py upload \
  --account-id <id> --access-key <key> --secret-key <secret> \
  --bucket-name <bucket> --endpoint-url <url> \
  --local-folder <path>

# List all objects in bucket
python scripts/s3_manage.py list \
  --account-id <id> --access-key <key> --secret-key <secret> \
  --bucket-name <bucket> --endpoint-url <url>

# Verify files against S3
python scripts/s3_manage.py verify \
  --account-id <id> --access-key <key> --secret-key <secret> \
  --bucket-name <bucket> --endpoint-url <url> \
  --local-folder <path>

# Generate index files with download links
python scripts/s3_manage.py indexupload \
  --account-id <id> --access-key <key> --secret-key <secret> \
  --bucket-name <bucket> --endpoint-url <url> \
  --url-prefix <base-url>
```

### Environment Variables Pattern

For convenience, you can use environment variables:

```bash
# Set environment variables
export S3_ACCOUNT_ID="my-account"
export S3_ACCESS_KEY="my-key"
export S3_SECRET_KEY="my-secret"
export S3_BUCKET_NAME="my-bucket"
export S3_ENDPOINT_URL="https://s3.example.com"

# Use in script
python scripts/s3_manage.py upload \
  --account-id "$S3_ACCOUNT_ID" \
  --access-key "$S3_ACCESS_KEY" \
  --secret-key "$S3_SECRET_KEY" \
  --bucket-name "$S3_BUCKET_NAME" \
  --endpoint-url "$S3_ENDPOINT_URL" \
  --local-folder ./build
```

---

## Version History

- **v1.0**: Initial implementation with upload command
- **v1.1**: Added list command with prefix filtering and pagination
- **v1.2**: Added verify command with ETag calculation for multipart uploads
- **v1.3**: Added indexupload command to generate HTML and Markdown index files with download links

---

## License

This tool is part of the swblocks-baselib project. See the main project LICENSE file for details.

---

## Support

For issues, questions, or contributions, please refer to the main project repository and CONTRIBUTING.md file.
