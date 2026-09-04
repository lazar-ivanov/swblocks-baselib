# s3_manage.py - S3 Management Tool

A command-line tool for managing S3-compatible object storage with support for upload, list, and verify operations.

## Overview

`s3_manage.py` is a Python script that provides five main commands for working with S3-compatible storage:
- **upload**: Upload local files to an S3 bucket with parallel execution and smart skip logic
- **list**: List objects in an S3 bucket with filtering and pagination
- **verify**: Verify local files against S3 objects by comparing a recorded SHA-256 checksum (falling back to the ETag for objects that have none)
- **indexupload**: Generate HTML and Markdown index files listing bucket contents with download links
- **download**: Download S3 objects to local folder with parallel execution and automatic verification

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

## Exit Codes

All five commands share the same exit code contract, so `command || exit 1` is safe to use in CI for any of them:

| Code | Meaning |
|------|---------|
| `0` | Every requested operation succeeded (or there was nothing to do, e.g. an empty bucket/folder) |
| `1` | At least one file/object failed, was missing, differed, or the input (bucket, local folder, URL prefix) was unusable, or a directory could not be scanned |
| `2` | Command-line usage error (invalid/missing arguments, from argparse) |

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
  [--allow-hidden-files] \
  [--force]
```

#### Arguments

| Argument | Required | Default | Description |
|----------|----------|---------|-------------|
| `--local-folder PATH` | Yes | - | Local directory to upload |
| `--max-threads N` | No | 3 | Number of parallel upload threads |
| `--dry-run` | No | False | Show what would be uploaded without actually uploading |
| `--allow-hidden-files` | No | False | Include hidden files and directories (starting with `.`) |
| `--force` | No | False | Re-upload objects that already exist instead of skipping them |

#### Features

- **Smart Skip Logic**: Automatically skips files that already exist in S3 (unless `--force`)
- **Content Checksum**: Records the file's SHA-256 as object metadata (`x-amz-meta-bl-content-sha256`) and pins the multipart layout (8 MiB threshold and chunk size) so the object has a reproducible ETag; `verify` and `download` prefer this checksum
- **Re-upload / Repair (`--force`)**: Ignores the skip check and re-uploads. Use it to backfill a `bl-content-sha256` onto objects uploaded before v1.6, or to repair an object whose recorded checksum is wrong (see the caveat below)
- **Torn-upload Guard**: The checksum is read from the file in a separate pass from the upload. The tool records the file's size and modification time before and after the upload; if either changed, the object is treated as torn — it is deleted (best effort) and the file is reported as `[FAILURE]`. A change that preserves *both* size and nanosecond mtime is not detected, so do not rewrite files while `upload` is running
- **Parallel Uploads**: Uses ThreadPoolExecutor for concurrent uploads (configurable)
- **Hidden File Filtering**: By default, skips hidden files and directories (starting with `.`)
- **Dry-Run Mode**: Preview what would be uploaded without making changes
- **Progress Tracking**: Real-time status messages for each file
- **Summary Statistics**: Shows upload counts, skip counts, failure counts, total upload size, and upload speed
- **Speed Measurement**: Automatically calculates and displays upload speed with auto-adapting units (B/s, KB/s, MB/s, GB/s, TB/s)
- **CI/CD Friendly**: Returns exit code 1 if the local folder is missing/unusable or any file fails to upload, 0 for success

#### Output Format

```
[STARTING] path/to/file.txt...
[SUCCESS]  path/to/file.txt  --  1.23 GB
[SKIPPED]  path/to/existing.txt (Already exists)
[FAILURE]  path/to/broken.txt - Access Denied

All operations complete!

--- UPLOAD SUMMARY ---
Total files scanned: 3
Files uploaded: 1
Files skipped (already exist): 1
Failed: 1
Total uploaded size: 1.23 GB
Upload speed: 45.67 MB/s
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
Upload speed: 125.67 MB/s

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

Verify that local files match their S3 counterparts. Objects uploaded by this tool carry a
SHA-256 of their content in metadata and are verified against it; other objects fall back to
an ETag comparison. This command is useful for ensuring upload integrity or detecting local
modifications.

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

- **SHA-256 Content Verification**: For objects uploaded by this tool, compares a SHA-256 of the local content against the value recorded in object metadata
- **ETag Fallback**: For objects with no recorded checksum, compares against the S3 ETag; multipart ETags are reproduced by trying each standard chunk size and requiring an exact match (the chunk size is never inferred from the part count alone)
- **Parallel Verification**: Uses ThreadPoolExecutor for concurrent verification
- **Hidden File Filtering**: By default, skips hidden files and directories (starting with `.`)
- **Detailed Status Reporting**: Four verification states (VERIFIED, DIFFERENT, NOT UPLOADED, ERROR)
- **CI/CD Friendly**: Returns exit code 1 if any file is DIFFERENT, NOT UPLOADED, or ERROR (see Verification States below), or if the local folder is missing/unusable; 0 for success
- **Comprehensive Summary**: Shows counts for all verification categories and verify speed
- **Speed Measurement**: Automatically calculates and displays verification speed based on all processed files with auto-adapting units (B/s, KB/s, MB/s, GB/s, TB/s)

#### Verification Algorithm

**Recorded SHA-256 (preferred):**
- When the object has an `x-amz-meta-bl-content-sha256` value (written by this tool's
  `upload` command), `verify`/`download` compute the SHA-256 of the local file and require
  an exact match. A well-formed but non-matching value is reported as `[DIFFERENT]`; a
  malformed value is ignored and the ETag fallback is used.

**ETag fallback (objects with no recorded checksum):**

*Simple uploads (< 8 MiB):*
- ETag = MD5 of the file content; format `xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx` (32 hex chars, no hyphen).

*Multipart uploads (>= 8 MiB):*
- The chunk size cannot be recovered from the part count alone (several standard sizes can
  produce the same count), so the tool tries every standard size — 5, 8, 16, 32, 64, 128,
  256, 512 MiB — that is consistent with the file size and part count, computes the full
  multipart ETag for each (`MD5` per chunk -> concatenate the binary digests -> `MD5` of the
  concatenation -> `hash-N`), and reports `[VERIFIED]` only on an exact match. If no
  standard size reproduces the ETag (for example an object uploaded elsewhere with a
  non-standard chunk size), it is reported as `[DIFFERENT]`.
- This tool's own uploads pin an 8 MiB threshold and chunk size, so their multipart ETags
  are always reproducible.

#### Verification States

| Status | Description | Exit Code |
|--------|-------------|-----------|
| `[VERIFIED]` | File matches S3 exactly (SHA-256 or ETag match) | 0 |
| `[DIFFERENT]` | File exists in S3 but cannot be confirmed to match: content actually differs; or, with no recorded `bl-content-sha256`, the object was uploaded elsewhere with a non-standard multipart chunk size, or is SSE-KMS/SSE-C encrypted, or was created by `CopyObject` — in those cases the ETag is not a content MD5 and always reads as `[DIFFERENT]` (re-upload with `--force` to record a checksum) | 1 |
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
Verify speed: 234.56 MB/s
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
- **Character Escaping**: Percent-encodes S3 keys in download URLs and HTML-escapes labels and `href` attributes; Markdown labels escape `|`, `[`, `]`, and `\`

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

The `--url-prefix` parameter is the operator-selected base URL for download links. Any `http` or `https` origin with a host is approved; there is no host allowlist and no same-origin check against `--endpoint-url`.

- Scheme must be `http` or `https`. Relative, protocol-relative, `javascript:`, `data:`, and `file:` prefixes are rejected.
- A host is required. Userinfo and literal query or fragment delimiters are rejected, including trailing empty `?`, `#`, or `?#`.
- Percent-encoded `%3F` and `%23` remain valid path data.
- ASCII whitespace, controls, backslash, quotes, parentheses, and angle brackets are rejected so the prefix cannot break HTML attributes or Markdown `](url)` destinations.
- Malformed `%` escapes and empty or invalid ports are rejected. The prefix is not rewritten or percent-encoded.
- A missing trailing slash is added automatically.
- An invalid prefix fails with `[ERROR]` and exit code 1 **before** S3 client construction and listing.

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

S3 object keys are untrusted. Every listed key is included after encoding; keys are not omitted because they contain quotes, controls, Unicode, fragments, `.` / `..`, or repeated slashes.

**HTML Files:**
- Each key is percent-encoded as URL path data (`urllib.parse.quote`, `/` kept as a separator) and the complete `href` is passed through `html.escape(..., quote=True)`.
- Visible labels use `html.escape(..., quote=True)`.
- `.` and `..` path segments are left as-is. Browsers may normalize those links out of the prefix path without changing scheme or origin; that is link correctness, not XSS.

**Markdown Files:**
- Download URLs match the HTML encoding so `)` in a key cannot close `](url)`.
- Labels collapse CR/LF/TAB to a space, HTML-escape, then escape `\`, `[`, `]`, and `|` (`|` → `\|`).

---

### 5. download - Download Files from S3

Download S3 objects to a local folder with parallel execution, automatic verification, and intelligent handling of existing files. The command downloads only missing files and verifies existing files without re-downloading them.

#### Usage

```bash
python scripts/s3_manage.py download \
  --account-id <id> \
  --access-key <key> \
  --secret-key <secret> \
  --bucket-name <bucket> \
  --endpoint-url <url> \
  --local-folder <path> \
  [--max-threads <n>] \
  [--dry-run] \
  [--prefix <prefix>]
```

#### Arguments

| Argument | Required | Default | Description |
|----------|----------|---------|-------------|
| `--local-folder PATH` | Yes | - | Local directory to download files to |
| `--max-threads N` | No | 3 | Number of parallel download threads |
| `--dry-run` | No | False | Preview what would be downloaded without downloading |
| `--prefix PREFIX` | No | - | Filter S3 objects by prefix (e.g., "folder/subfolder/") |

#### Features

- **Smart Download Logic**: Downloads missing files, verifies existing files without re-downloading
- **Secure Path Preflight**: Validates every listed key and its destination namespace before creating the root or starting workers
- **Prefix as Filter Only**: Local paths always match full bucket structure (prefix doesn't act as new root)
- **Automatic Verification**: Every file is verified against its recorded SHA-256, or the ETag when there is none (same as the verify command)
- **Atomic Verified Publish**: Downloads to a unique private temporary file, verifies it, then publishes it with an atomic replace
- **Parallel Execution**: Uses ThreadPoolExecutor for concurrent downloads
- **Dry-Run Mode**: Runs the same key and collision preflight without making changes
- **Folder Markers**: Skips only validated, zero-byte keys with one trailing slash
- **No Hidden File Filtering**: Downloads ALL safe S3 objects matching prefix (no --allow-hidden-files flag)
- **Real-Time Status**: Reports DOWNLOADING, DOWNLOADED, VERIFIED, DIFFERENT, SKIPPED, and ERROR states
- **CI/CD Friendly**: Exit code 1 if any files are DIFFERENT or errors occur
- **Bandwidth Efficient**: Skips re-downloading existing files even if checksums differ
- **Speed Measurement**: Automatically calculates and displays download speed based on downloaded files with auto-adapting units (B/s, KB/s, MB/s, GB/s, TB/s)

#### Download Behavior

Before any local mutation, the command validates all listed S3 keys as POSIX paths. It rejects traversal, absolute, drive/UNC, backslash, empty/dot components, Windows-invalid characters, ADS, trailing-dot/space, controls, and CPython's reserved device names. It also aborts on case-folded or Unicode-normalized duplicate destinations and file-versus-directory conflicts such as `a` and `a/b`. Any such key aborts the entire command, including dry-run, before workers start.

A validated trailing-slash key is skipped as a folder marker only when its listed size is zero. Other trailing-slash objects are unsafe.

**For each downloadable S3 object:**
1. If the file **doesn't exist locally**: Download through `download_fileobj` to a unique `.s3dl-` temporary file → Verify size/ETag → Atomically publish → Report status
2. If the file **exists locally**: Verify only (don't re-download even if different)

Temporary files use mode `0o600` on POSIX. A failed download, verification, or publish removes the temporary file and does not create the final destination. Operational per-key errors and `[DIFFERENT]` results continue processing safe sibling keys.

**This prevents:**
- S3 keys escaping the selected download root
- Writes through pre-existing symlinks, junctions, or other reparse points below the root
- Data loss from overwriting modified local files
- Publishing an unverified or partial download
- Unnecessary bandwidth usage
- Accidental corruption of local work

#### Prefix Handling (Critical Design)

The `--prefix` parameter filters which S3 objects to download but **does NOT act as a new root**. Local paths always mirror the full bucket structure.

**Example:**
- S3 objects: `builds/2024/app.tar.gz`, `builds/2024/README.md`, `configs/app.conf`
- Command: `download --prefix "builds/2024/" --local-folder ./downloads`
- Local paths created:
  - `./downloads/builds/2024/app.tar.gz` ✅
  - `./downloads/builds/2024/README.md` ✅
- **NOT created:**
  - `./downloads/app.tar.gz` ❌ (prefix stripping would be wrong)

#### Download States

| Status | Description | Action Taken | Exit Code |
|--------|-------------|--------------|-----------|
| `[DOWNLOADING]` | File download in progress | Downloading file | - |
| `[DOWNLOADED] → [VERIFIED]` | File downloaded and verified | Downloaded, verified | 0 |
| `[VERIFIED]` | Existing file matches S3 | Verified only (not downloaded) | 0 |
| `[DIFFERENT]` | Existing file differs from S3 | Verified only (kept local file) | 1 |
| `[ERROR]` | Download or verification error | Error reported | 1 |
| `[SKIPPED]` | Validated zero-byte folder marker | No file or marker directory created | 0 |

#### Output Format

**Fresh Download (no existing files):**
```
Listing objects in bucket: my-bucket

Found 5 objects (125.45 MB total)
Local folder: ./downloads

[DOWNLOADING] builds/2024/app-v1.0.tar.gz (45.23 MB)...
[DOWNLOADING] builds/2024/README.md (1.23 KB)...
[DOWNLOADED] builds/2024/README.md (1.23 KB) → [VERIFIED]
[DOWNLOADED] builds/2024/app-v1.0.tar.gz (45.23 MB) → [VERIFIED]
[DOWNLOADING] configs/app.conf (512 B)...
[DOWNLOADED] configs/app.conf (512 B) → [VERIFIED]

All operations complete!

--- DOWNLOAD SUMMARY ---
Total files found: 5
Downloaded (new files): 5 (125.45 MB)
Verified (existing files, match): 0 (0 B)
Different (existing files, mismatch): 0
Errors: 0
Download speed: 45.23 MB/s
```

**Incremental Download (some files exist):**
```
Listing objects in bucket: my-bucket
Prefix filter: builds/2024/

Found 3 objects (175.78 MB total)
Local folder: ./downloads

[VERIFIED] builds/2024/app-v1.0.tar.gz (45.23 MB)
[DOWNLOADING] builds/2024/app-v1.1.tar.gz (50.12 MB)...
[DOWNLOADED] builds/2024/app-v1.1.tar.gz (50.12 MB) → [VERIFIED]
[DIFFERENT] builds/2024/app-v1.2.tar.gz (SHA-256 mismatch)

All operations complete!

--- DOWNLOAD SUMMARY ---
Total files found: 3
Downloaded (new files): 1 (50.12 MB)
Verified (existing files, match): 1 (45.23 MB)
Different (existing files, mismatch): 1
Errors: 0
Download speed: 50.12 MB/s
```

**Exit code:** 1 (due to DIFFERENT status)

#### Example: Basic Download

```bash
python scripts/s3_manage.py download \
  --account-id "my-account" \
  --access-key "my-key" \
  --secret-key "my-secret" \
  --bucket-name "my-bucket" \
  --endpoint-url "https://s3.example.com" \
  --local-folder ./downloads
```

#### Example: Download with Prefix Filter

```bash
python scripts/s3_manage.py download \
  --account-id "my-account" \
  --access-key "my-key" \
  --secret-key "my-secret" \
  --bucket-name "my-bucket" \
  --endpoint-url "https://s3.example.com" \
  --local-folder ./downloads \
  --prefix "builds/2024/"
```

**Use Case:** Download only files from a specific subdirectory, but preserve full bucket structure locally.

#### Example: Dry-Run Mode

```bash
python scripts/s3_manage.py download \
  --account-id "my-account" \
  --access-key "my-key" \
  --secret-key "my-secret" \
  --bucket-name "my-bucket" \
  --endpoint-url "https://s3.example.com" \
  --local-folder ./downloads \
  --dry-run
```

**Output:**
```
Listing objects in bucket: my-bucket

Found 10 objects (2.34 GB total)
Local folder: ./downloads

Running in DRY-RUN mode (no files will be downloaded)

[DRY-RUN] file1.txt (1.23 MB would be downloaded)
[DRY-RUN] file2.bin (456.78 KB would be downloaded)
[VERIFIED] existing_file.txt (2.34 MB)
[DRY-RUN] file3.tar.gz (1.12 GB would be downloaded)

All operations complete!

--- DOWNLOAD SUMMARY ---
Total files found: 10
Downloaded (new files): 7 (2.12 GB)
Verified (existing files, match): 3 (220.45 MB)
Different (existing files, mismatch): 0
Errors: 0
Download speed: 156.78 MB/s

No files were actually downloaded (dry-run mode)
```

#### Example: High-Performance Download

```bash
python scripts/s3_manage.py download \
  --account-id "my-account" \
  --access-key "my-key" \
  --secret-key "my-secret" \
  --bucket-name "my-bucket" \
  --endpoint-url "https://s3.example.com" \
  --local-folder ./downloads \
  --max-threads 10
```

**Use Case:** Download large datasets faster with more parallel connections.

#### Example: Backup/Restore Workflow

```bash
#!/bin/bash
# Restore backup from S3 with verification

# Download all files from backup bucket
python scripts/s3_manage.py download \
  --account-id "my-account" \
  --access-key "my-key" \
  --secret-key "my-secret" \
  --bucket-name "backups" \
  --endpoint-url "https://s3.example.com" \
  --local-folder ./restore \
  --max-threads 5 || {
  echo "ERROR: Download/verification failed!"
  exit 1
}

# Check if any files were DIFFERENT
# Exit code 1 means verification issues
echo "SUCCESS: All files downloaded and verified"
```

#### Example: Incremental Sync Pattern

```bash
#!/bin/bash
# Incremental download - only get new/missing files

# First run: Downloads all files
python scripts/s3_manage.py download \
  --account-id "my-account" \
  --access-key "my-key" \
  --secret-key "my-secret" \
  --bucket-name "data-lake" \
  --endpoint-url "https://s3.example.com" \
  --local-folder ./data

# Second run (later): Only downloads new files, verifies existing
# Existing files with matching checksums show [VERIFIED]
# Modified local files show [DIFFERENT] but are NOT overwritten
python scripts/s3_manage.py download \
  --account-id "my-account" \
  --access-key "my-key" \
  --secret-key "my-secret" \
  --bucket-name "data-lake" \
  --endpoint-url "https://s3.example.com" \
  --local-folder ./data
```

#### Use Cases

**1. Backup Restoration**
- Download backups from S3 to restore data
- Automatic verification ensures data integrity
- Exit code 1 alerts if any files are corrupted

**2. Incremental Synchronization**
- Run periodically to download new files
- Skips re-downloading existing files (saves bandwidth)
- Verifies existing files haven't changed

**3. Build Artifact Distribution**
- Download build artifacts from CI/CD bucket
- Parallel downloads for faster retrieval
- Automatic verification of downloaded artifacts

**4. Data Lake Mirroring**
- Mirror S3 data lake to local storage
- Prefix filtering for specific datasets
- Preserves full bucket structure locally

**5. Disaster Recovery**
- Quick restoration of critical files
- Verification ensures no corruption
- Prefix filtering for selective recovery

#### Edge Cases and Behavior

**Empty Bucket:**
```
Listing objects in bucket: my-bucket

Bucket is empty
Nothing to download.
```

Exit code: 0

**No Prefix Matches:**
```
Listing objects in bucket: my-bucket
Prefix filter: missing/prefix/

No objects found with prefix: missing/prefix/
Nothing to download.
```

Exit code: 0

**Modified Local File:**
- Local file has different checksum than S3
- Status: `[DIFFERENT]`
- Action: Local file is **NOT overwritten** (preserved)
- Exit code: 1 (indicates verification issue)

**Download Failure:**
- Network error, permission denied, disk full, etc.
- Status: `[ERROR]` with error details
- Action: Error reported, other files continue
- Exit code: 1

---

## Error Handling

All commands handle errors gracefully and provide clear error messages:

### Upload Errors

- **Local folder missing or not a directory:** Displays `[ERROR]`, exits with code 1 before scanning
- **File not found:** Skipped with warning
- **Permission denied:** Reported as `[FAILURE]`, counted in the `Failed:` summary line, exits with code 1
- **Network error:** Reported as `[FAILURE]` with details, counted in the `Failed:` summary line, exits with code 1
- **S3 API error:** Reported as `[FAILURE]` with error code and message, counted in the `Failed:` summary line, exits with code 1
- **File changed during upload:** Size or modification time differed before vs. after the upload — the object is deleted (best effort) and reported as `[FAILURE]`, exits with code 1

### List Errors

- **Empty bucket:** Displays "Bucket is empty", exits gracefully with code 0
- **No matches for prefix:** Displays "No objects found with prefix: {prefix}", exits gracefully with code 0
- **Access denied:** Displays error code and message, exits with code 1
- **Bucket not found:** Displays error code and message, exits with code 1

### Verify Errors

- **Local folder missing or not a directory:** Displays `[ERROR]`, exits with code 1 before scanning
- **File not in S3:** Reported as `[NOT UPLOADED]`, exits with code 1
- **File read error:** Reported as `[ERROR]` with details, exits with code 1
- **S3 API error:** Reported as `[ERROR]` with error code, exits with code 1
- **Malformed S3 ETag:** Reported as `[ERROR]` (rare), exits with code 1
- **Multipart ETag not reproducible by any standard chunk size:** Reported as `[DIFFERENT]`, exits with code 1

### IndexUpload Errors

- **Invalid URL prefix:** Displays `[ERROR]`, exits with code 1 before client construction and listing
- **Empty bucket:** Displays message, exits gracefully without generating files (code 0)
- **No matches for prefix:** Displays message, exits gracefully (code 0)
- **Upload failure:** Displays error message, exits with code 1
- **Access denied / bucket not found (listing):** Displays S3 error code and message, exits with code 1
- **Disk full:** Propagates error during file generation

### Download Errors

- **Empty bucket:** Displays message "Nothing to download", exits gracefully with code 0
- **No prefix matches:** Displays message, exits gracefully with code 0
- **Path-unsafe key:** Printed as `[ERROR] repr(key)`; aborts the entire command before local mutation
- **Destination namespace conflict:** Every colliding key is printed with `repr(key)`; aborts the entire command before local mutation
- **File download failed:** Reported as `[ERROR]` with details; removes its temporary file and continues with other safe files
- **Local file different:** Reported as `[DIFFERENT]`, local file kept (not overwritten), exits with code 1
- **Permission denied (local):** Reported as `[ERROR]` (cannot create directory or write file)
- **Disk full:** Reported as `[ERROR]` during download
- **Network error:** Reported as `[ERROR]` with details
- **Malformed S3 ETag:** Reported as `[ERROR]` (rare)
- **SHA-256 or multipart-ETag mismatch:** Reported as `[DIFFERENT]`, local file kept, exits with code 1

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
- **download:** Uses `head_object()` for metadata and `download_fileobj()` into a verified temporary file for new files only

### IndexUpload-Specific Considerations

- **Full Bucket Scan:** Must paginate through entire bucket to collect all objects
- **Memory Usage:** Stores all objects in memory (~100 bytes per object, ~1MB for 10,000 objects)
- **Generation Time:** Large buckets (10,000+ objects) may take several minutes to list
- **File Size:** Generated HTML/MD files scale with object count (~500KB HTML for 10,000 objects)

### Download-Specific Considerations

- **Bandwidth Optimization:** Existing files are verified without re-downloading (saves bandwidth)
- **Incremental Downloads:** Only new/missing files are downloaded on subsequent runs
- **Parallel Downloads:** Uses ThreadPoolExecutor for concurrent downloads (configurable with --max-threads)
- **Verification Cost:** ETag calculation for large multipart files can be CPU-intensive
- **Disk Space:** Ensure sufficient disk space before downloading (check with --dry-run first)
- **Network Stability:** Large downloads may fail if network is unstable (no automatic retry yet)
- **Memory Efficiency:** Files are downloaded directly to disk (not loaded entirely into memory)

---

## Security Notes

1. **Credentials:** Never hardcode credentials in scripts. Use environment variables or secure configuration management.
2. **Access Keys:** Rotate access keys regularly.
3. **Bucket Permissions:** Ensure the S3 user has appropriate permissions (read, write, list).
4. **Hidden Files:** By default, hidden files (like `.env`, `.git/`) are skipped during upload to avoid accidentally uploading sensitive data.
5. **Untrusted S3 Keys:** Download treats object keys as untrusted input. Unsafe keys and destination namespace conflicts abort the whole command before the download root is created or workers start.
6. **No-Follow Destinations:** Download rejects any symlink, Windows junction, other reparse point, or special file below the resolved root. The user-supplied root itself may be a symlink.
7. **Local Concurrency Boundary:** Do not let an untrusted local user modify the destination tree during a download. The no-follow checks protect against pre-existing hostile entries, not concurrent directory swapping.
8. **Untrusted Index Keys:** Generated `index.html` / `index.md` treat S3 keys as untrusted. Keys are percent-encoded and HTML/Markdown-escaped so they cannot introduce a scheme or break out of `href` / `](url)`. Operators must not pass a hostile `--url-prefix`; invalid prefixes are rejected. Documented static-hosting use is why this is stored XSS, not a console-only issue. Keys containing `.` / `..` segments can still be path-normalized by browsers; that is link correctness, not XSS.

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
2. S3 object was replaced (check last modified date with `list` command)
3. The object has no recorded `bl-content-sha256` and its ETag is not a content MD5 — it was uploaded by another tool with a non-standard multipart chunk size, or it is SSE-KMS/SSE-C encrypted, or it was produced by `CopyObject`. Re-upload it with `upload --force` to record a checksum.

### Issue: Upload is very slow

**Solution:**
1. Increase `--max-threads` (try 5-10)
2. Check network bandwidth
3. Verify S3 endpoint is not rate limiting

### Issue: Download shows "[DIFFERENT]" but I want to re-download

**Solution:** The download command intentionally preserves existing local files to prevent data loss. If you want to force re-download:
1. Delete the local file: `rm path/to/file`
2. Run download again
3. Alternatively, use a different local folder to download to a fresh location

### Issue: Download doesn't preserve my modified local files

**Solution:** The download command is working correctly. When a local file differs from S3:
- The local file is **NOT overwritten** (preserved)
- Status shows `[DIFFERENT]`
- Exit code is 1 (indicates verification issue)
- This protects your local changes from being accidentally lost

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

# Download files from S3
python scripts/s3_manage.py download \
  --account-id <id> --access-key <key> --secret-key <secret> \
  --bucket-name <bucket> --endpoint-url <url> \
  --local-folder <path>
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
- **v1.4**: Added download command with parallel execution, automatic verification, and intelligent handling of existing files
- **v1.5**: Added speed measurement to upload, download, and verify commands with auto-adapting units (B/s to TB/s)
- **v1.6**: `upload` records a SHA-256 of each file in object metadata and pins the multipart layout (8 MiB); `verify`/`download` prefer that checksum and no longer infer the multipart chunk size from the part count
- **v1.7**: Added `upload --force` (re-upload / repair / checksum backfill) and a torn-upload guard that fails and removes an object whose file changed mid-upload; multipart-ETag verification now hashes each part with a bounded read instead of loading a whole part into memory

---

## License

This tool is part of the swblocks-baselib project. See the main project LICENSE file for details.

---

## Support

For issues, questions, or contributions, please refer to the main project repository and CONTRIBUTING.md file.
