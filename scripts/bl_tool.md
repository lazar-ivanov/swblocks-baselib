# bl_tool.py - Baselib Repository Utility

A command-line utility for performing various operations on the baselib repository, including cryptographic hashing of files and directories.

## Table of Contents

- [Overview](#overview)
- [Features](#features)
- [Commands](#commands)
  - [hash](#hash-command)
- [Hash Algorithm Design](#hash-algorithm-design)
- [Architecture and Implementation](#architecture-and-implementation)
- [Usage Examples](#usage-examples)
- [Use Cases](#use-cases)

---

## Overview

`bl_tool.py` is a Python-based utility designed for baselib repository operations. It provides cryptographically secure hashing capabilities for both individual files and entire directory trees, with built-in support for large files, parallel processing, and memory efficiency.

**Key Capabilities:**
- Hash individual files or entire directories
- Memory-efficient processing (handles files 10+ GB, directories with millions of files)
- Parallel processing for improved performance
- Cryptographic verification with SHA256
- Dry-run mode for previewing operations
- Comprehensive statistics and speed measurements

---

## Features

### Core Features

- **Universal Path Support**: Works with both files and directories through a unified interface
- **Memory Efficient**: Chunked file reading (1 MB chunks) and block-based hash combining
- **Parallel Processing**: Configurable multi-threading for directory hashing
- **Large File Support**: Handles files of any size without memory exhaustion
- **Scalable**: Processes directories with millions of files efficiently
- **Verification**: Compare computed hashes against expected values
- **Verbose Mode**: Optional detailed output for each file processed
- **Dry-Run Mode**: Preview operations without computing hashes
- **Speed Metrics**: Real-time performance statistics with auto-adapting units

### Technical Features

- **Chunked Reading**: 1 MB chunk size prevents memory issues with large files
- **Block-Based Combining**: Processes hashes in 32,000-hash blocks (~1 MB) for memory efficiency
- **Framed Records**: Domain-separated, length-prefixed records make the digest an unambiguous tree commitment
- **Symlink Protection**: Fails gracefully when symlinks, junctions or non-regular files are encountered
- **Hidden File Control**: Optional inclusion of hidden files (directories only)
- **Deterministic Output**: Same input always produces same hash, on any platform (canonical ordering and path encoding)

---

## Commands

### hash Command

Calculate SHA256 hash of a file or directory contents.

#### Synopsis

```bash
bl_tool.py hash --path PATH [OPTIONS]
```

#### Parameters

| Parameter | Required | Description |
|-----------|----------|-------------|
| `--path` | Yes | Path to file or directory to hash |
| `--verify-sha256` | No | Expected SHA256 hash for verification (hex string) |
| `--allow-hidden-files` | No | Include hidden files/directories (applies to directories only) |
| `--max-threads` | No | Maximum parallel threads (default: 4) |
| `--dry-run` | No | Scan and report statistics without hashing |
| `--verbose` | No | Print status for each file being hashed |

#### Features

- **Unified Interface**: Single command for both files and directories
- **Automatic Detection**: Automatically detects if path is file or directory
- **Memory Efficient**: Handles large files and large directories without memory issues
- **Parallel Processing**: Multi-threaded directory hashing for performance
- **Symlink Protection**: Fails immediately if symlinks, junctions or non-regular files are encountered
- **Hidden File Control**: For directories, optionally include/exclude hidden files
- **Verification**: Compare computed hash against expected value
- **Speed Measurement**: Displays processing speed with auto-adapting units (B/s to TB/s)

#### Output Format

**Single File:**
```
Hashing single file...

All operations complete!

--- HASH SUMMARY ---
Path: /path/to/file.txt
Total files processed: 1
Total size: 1.23 MB
Combined SHA256: abc123def456...
Hash format: blhash/v2
Hashing speed: 234.56 MB/s
```

**Directory:**
```
Scanning directory: /path/to/dir
Found 1234 files to process
Hashing files with 4 threads...

All operations complete!

--- HASH SUMMARY ---
Path: /path/to/dir
Total files processed: 1234
Total size: 5.67 GB
Combined SHA256: def789abc012...
Hash format: blhash/v2
Hashing speed: 456.78 MB/s
```

**Dry-Run Mode:**
```
Scanning directory: /path/to/dir
Found 100 files to process
[DRY-RUN] Would hash file1.txt
[DRY-RUN] Would hash file2.txt
...

All operations complete!

--- DRY-RUN SUMMARY ---
Path: /path/to/dir
Total files found: 100
Total size: 2.45 GB

No files were actually hashed (dry-run mode)
```

**Verification Success:**
```
--- HASH SUMMARY ---
...
Combined SHA256: abc123def456...
Hash format: blhash/v2
Hashing speed: 234.56 MB/s
Verification: MATCH
```

**Verification Failure:**
```
--- HASH SUMMARY ---
...
Combined SHA256: abc123def456...
Hash format: blhash/v2
Hashing speed: 234.56 MB/s
Verification: MISMATCH
Expected: def789abc012...
Actual:   abc123def456...
```

#### Exit Codes

| Code | Meaning |
|------|---------|
| 0 | Success (hash computed, or verification matched) |
| 1 | Failure (error encountered, or verification mismatch) |

---

## Hash Algorithm Design

### Overview

The hash command uses SHA256 cryptographic hashing with a two-stage algorithm designed for deterministic, verifiable results across both individual files and directory trees.

The digest format is versioned and identified as **`blhash/v2`**, reported on the `Hash format:` line of the summary. Every hashed field is domain separated and length prefixed, so the digest is an unambiguous commitment to the tree: no two structurally different trees can produce the same digest without breaking SHA256 itself.

### Record Format

All length and size fields are unsigned 64-bit big-endian. `H` is the selected algorithm (SHA256 by default, SHA1 with `--use-sha1`).

**File record:**
```
H( "blhash/v2/file\0" ‖ u64(len(path)) ‖ path ‖ u64(size) ‖ H(contents) )
```

**Directory record:**
```
H( "blhash/v2/dir\0" ‖ u64(len(path)) ‖ path )
```

**Root digest:**
```
H( root_tag ‖ algorithm_name ‖ "\0" ‖ u64(record_count) ‖ record_1 ‖ record_2 ‖ ... ‖ record_N )
```

Where `root_tag` is:

| Target | Tag |
|--------|-----|
| Single file | `blhash/v2/single\0` |
| Directory tree | `blhash/v2/tree\0` |

**Why each field exists:**

| Field | Prevents |
|-------|----------|
| Length-prefixed path | Content bytes being read as path bytes, or vice versa. Without it, content `a` at path `bc` and content `ab` at path `c` both hash the stream `abc` and collide. |
| Record type tag | A directory record being substituted for a file record |
| Root scope tag | A file digest being presented as a tree digest |
| Algorithm name | A digest being reinterpreted under a different algorithm |
| Record count | Records being appended to or truncated from a tree |
| File size | Content and the following field being shifted across the boundary |

### Path Canonicalization

Relative paths are committed in a canonical form so the same tree hashes identically everywhere:

- **Separators** are normalized to `/`, so a tree hashed on Windows matches the same tree hashed on POSIX.
- **Encoding** is UTF-8 with `surrogateescape`, so file names that are not valid UTF-8 (possible on POSIX) round-trip to their original bytes rather than aborting the run.
- **Unicode normalization is deliberately not applied.** NFC and NFD names are distinct files on filesystems that do not normalize, and folding them would make the digest ambiguous.
- **Ordering** is by canonical path bytes, which is independent of platform and locale.

### Directory Hashing

**Process:**
1. Scan directory recursively, collecting all files and all directories
2. Reject symlinks, junctions and non-regular files (see below)
3. Hash each file in parallel into a file record
4. Build a directory record for every directory below the root
5. Sort all records by canonical path bytes
6. Combine into the root digest:
   - Emit the root tag, algorithm name and record count
   - Process records in blocks of 32,000 (~1 MB of record data)
   - Feed each block to a single streaming hasher
   - Finalize the root digest

Directory records mean the tree **shape** is committed, not just its file contents. Two trees that differ only by an empty directory receive different digests, and an empty tree has its own well-defined digest rather than the hash of empty input.

**Why this design?**
- **Unambiguous**: Framed records admit no structural collisions
- **Deterministic**: The same tree always produces the same digest, on any platform
- **Verifiable**: An entire directory tree is verified by a single digest
- **Efficient**: Block-based combining keeps memory usage constant
- **Versioned**: The format name is part of the hashed input and of the reported output

### File Scope and Tree Scope Are Distinct

Hashing a file directly and hashing a directory that contains only that file produce **different** digests — a file and a tree containing that file are different objects, and the root scope tag separates them.

```bash
# These produce different hashes:
python bl_tool.py hash --path /tmp/singledir/file.txt   # blhash/v2/single
python bl_tool.py hash --path /tmp/singledir            # blhash/v2/tree
```

### Raw Content Mode (`--exclude-paths`)

`--exclude-paths` selects an unframed raw content digest, reported as `raw-content (no tree commitment)`:

- For a **single file** the output is exactly `SHA256(contents)`, matching `sha256sum` and equivalent tools. This is its intended use.
- For a **directory** it is the combined hash of the ordered content digests only. It commits to file contents, **not** to names, structure, or ordering, and it is **not** a tree commitment. Renaming or moving a file does not change it.

Use the default mode for integrity verification; use `--exclude-paths` only for interoperability with content-only hashes.

### Symlink and Special File Handling

**Behavior:** The tool **fails immediately** if a symlink, a Windows junction, or a non-regular file is encountered — whether it is the path given on the command line or an entry found during traversal.

**Rationale:**
- Symlinks can point outside the directory tree (breaks determinism)
- Symlink targets can change (breaks reproducibility)
- Circular symlinks can cause infinite loops
- Cross-platform symlink behavior is inconsistent
- Silently skipping a linked directory would omit its contents from a digest that claims to cover the whole tree
- Opening a FIFO, socket or device can block indefinitely

**Error Messages:**
```
[ERROR] Symlink encountered: /path/to/link
Symlinks are not supported by hash command
```
```
[ERROR] Not a regular file: /path/to/pipe
Only regular files are supported by hash command
```

### Format Compatibility

`blhash/v2` replaces the earlier unversioned format, which concatenated file contents and the relative path without framing and returned a lone file hash verbatim as the digest of a one-file tree. That format admitted structural collisions and could silently omit symlinked subtrees, so it was withdrawn rather than kept as an option.

**Digests produced by the earlier format cannot be reproduced and will not verify.** Any stored expected hash must be regenerated. The `Hash format:` line identifies which construction produced a digest.

---

## Architecture and Implementation

### Design Principles

1. **Memory Efficiency**: Constant memory usage regardless of file/directory size
2. **Performance**: Parallel processing with configurable thread count
3. **Reliability**: Fail-fast on errors, deterministic output
4. **Simplicity**: Unified interface for files and directories
5. **Verifiability**: Cryptographic guarantees with SHA256

### Memory Management

#### Problem Statement

**Naive Implementation Issues:**
- Loading entire files into memory fails with 10+ GB files
- Concatenating all hashes for large directories (10M files × 32 bytes = 320 MB) is inefficient
- Parallel processing can multiply memory usage

#### Solution: Two-Stage Memory Optimization

**1. Chunked File Reading (1 MB chunks)**

```python
FILE_CHUNK_SIZE = 1024 * 1024  # 1 MB

hasher = hashlib.sha256()
with open(file_path, 'rb') as f:
    while True:
        chunk = f.read(FILE_CHUNK_SIZE)
        if not chunk:
            break
        hasher.update(chunk)
        file_size += len(chunk)
```

**Benefits:**
- Handles files of any size (tested with 10+ GB)
- Only 1 MB in memory per thread at a time
- No performance penalty for small files

**2. Block-Based Hash Combining (32,000-hash blocks)**

```python
HASH_BLOCK_SIZE = 32000  # ~1 MB of hash data (32000 × 32 bytes)

final_hasher = hashlib.sha256()
for block_start in range(0, total_hashes, HASH_BLOCK_SIZE):
    block_end = min(block_start + HASH_BLOCK_SIZE, total_hashes)
    block = hash_list[block_start:block_end]
    block_bytes = b''.join(block)
    final_hasher.update(block_bytes)
```

**Benefits:**
- Handles millions of files efficiently
- Constant memory usage (~1 MB per block)
- Produces identical result to naive concatenation

**Why This Works:**
SHA256 is a **streaming hash algorithm** with the property:
```
hasher.update(a); hasher.update(b) ≡ hasher.update(a + b)
```

Processing in blocks is purely a memory optimization - the final hash is mathematically identical to concatenating all hashes at once.

### Parallel Processing

**Thread Pool Executor:**
```python
with ThreadPoolExecutor(max_workers=args.max_threads) as executor:
    future_to_file = {
        executor.submit(hash_worker, file_path, relative_path, args.verbose):
            (file_path, relative_path)
        for file_path, relative_path in files_to_process
    }

    for future in as_completed(future_to_file):
        result = future.result()
        results.append(result)
```

**Benefits:**
- Configurable thread count (default: 4)
- Handles both single files and directories uniformly
- Thread-safe output with locking mechanism

**Design Decision:**
Even single files use ThreadPoolExecutor for code simplicity - there's no performance impact, and it maintains consistent code paths.

### File Collection and Sorting

**Canonical Order Guarantee:**
```python
# 1. Collect files and directories with canonical relative paths
files = [(absolute_path, relative_path), ...]
directories = [relative_path, ...]

# 2. Sort by canonical path bytes
files.sort(key=lambda x: encode_path(x[1]))

# 3. Hash files (may complete in any order due to parallelism)
results = hash_all_files(files)

# 4. Merge file and directory records, then restore canonical order
records.sort(key=lambda x: encode_path(x[0]))
```

**Why sort twice?**
- First sort: Ensures consistent processing order across runs
- Parallel execution: Files may complete in any order
- Second sort: Restores canonical order before combining hashes

### Hidden File Handling

**Directory Behavior:**
- Default: Skip files/directories starting with `.`
- With `--allow-hidden-files`: Include hidden files/directories

**Single File Behavior:**
- Hidden file flag is **ignored**
- If user explicitly specifies a hidden file path, it is hashed
- Rationale: User explicitly requested that specific file

**Implementation:**
```python
if path_obj.is_file():
    # Single file: ignore allow_hidden_files flag
    files_to_process = handle_single_file(path, path_obj)
elif path_obj.is_dir():
    # Directory: respect allow_hidden_files flag
    files_to_process = handle_directory(path, args.allow_hidden_files)
```

### Verbose Mode

**Behavior:**
- Default (non-verbose): Only show summary statistics
- Verbose mode: Show `[HASHED]` message for each file

**Implementation:**
```python
if verbose:
    safe_print(f"[HASHED] {relative_path} -- {hash_hex}")
```

**Why control verbosity?**
- Large directories (millions of files) produce too much output
- Users typically only need summary statistics
- Verbose mode useful for debugging or monitoring specific files

### Thread-Safe Printing

**Problem:** Multiple threads printing simultaneously causes garbled output.

**Solution:** Thread lock for all print operations.

```python
print_lock = threading.Lock()

def safe_print(message):
    with print_lock:
        print(message)
```

**Usage:**
- All worker threads use `safe_print()` instead of `print()`
- Ensures messages appear intact and in order

---

## Usage Examples

### Basic Examples

#### Hash a Single File

```bash
python scripts/bl_tool.py hash --path /path/to/file.txt
```

**Output:**
```
Hashing single file...

All operations complete!

--- HASH SUMMARY ---
Path: /path/to/file.txt
Total files processed: 1
Total size: 1.23 MB
Combined SHA256: a1b2c3d4e5f6...
Hash format: blhash/v2
Hashing speed: 234.56 MB/s
```

#### Hash a Directory

```bash
python scripts/bl_tool.py hash --path /path/to/directory
```

**Output:**
```
Scanning directory: /path/to/directory
Found 1234 files to process
Hashing files with 4 threads...

All operations complete!

--- HASH SUMMARY ---
Path: /path/to/directory
Total files processed: 1234
Total size: 5.67 GB
Combined SHA256: d4e5f6a1b2c3...
Hash format: blhash/v2
Hashing speed: 456.78 MB/s
```

#### Hash with Verification

```bash
python scripts/bl_tool.py hash \
  --path /path/to/directory \
  --verify-sha256 d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5
```

**Output (match):**
```
...
Combined SHA256: d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5
Hash format: blhash/v2
Hashing speed: 456.78 MB/s
Verification: MATCH
```

**Exit code:** 0 (success)

**Output (mismatch):**
```
...
Combined SHA256: a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2
Hash format: blhash/v2
Hashing speed: 456.78 MB/s
Verification: MISMATCH
Expected: d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5
Actual:   a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2
```

**Exit code:** 1 (failure)

### Advanced Examples

#### Include Hidden Files (Directory Only)

```bash
python scripts/bl_tool.py hash \
  --path /path/to/directory \
  --allow-hidden-files
```

**Note:** This flag only affects directories. Hidden files specified directly are always hashed.

#### High-Performance Hashing (More Threads)

```bash
python scripts/bl_tool.py hash \
  --path /path/to/large/directory \
  --max-threads 16
```

**When to use:**
- Large directories with many files
- Fast storage (SSD/NVMe)
- CPU-bound operations (small files)

**Thread count guidelines:**
- Default (4 threads): Good for most cases
- 8-16 threads: Fast storage, many files
- 1 thread: Network storage, large files, debugging

#### Dry-Run Mode (Preview Only)

```bash
python scripts/bl_tool.py hash \
  --path /path/to/directory \
  --dry-run
```

**Output:**
```
Scanning directory: /path/to/directory
Found 100 files to process
[DRY-RUN] Would hash file1.txt
[DRY-RUN] Would hash file2.txt
...

All operations complete!

--- DRY-RUN SUMMARY ---
Path: /path/to/directory
Total files found: 100
Total size: 2.45 GB

No files were actually hashed (dry-run mode)
```

**Use cases:**
- Preview what files will be processed
- Check total size before hashing
- Verify hidden file filtering behavior
- Test path detection (file vs directory)

#### Verbose Mode (Show Each File)

```bash
python scripts/bl_tool.py hash \
  --path /path/to/directory \
  --verbose
```

**Output:**
```
Scanning directory: /path/to/directory
Found 10 files to process
Hashing files with 4 threads...
[HASHED] file1.txt -- a1b2c3d4e5f6...
[HASHED] file2.txt -- b2c3d4e5f6a1...
[HASHED] file3.txt -- c3d4e5f6a1b2...
...

All operations complete!

--- HASH SUMMARY ---
Path: /path/to/directory
Total files processed: 10
Total size: 5.67 MB
Combined SHA256: d4e5f6a1b2c3...
Hash format: blhash/v2
Hashing speed: 123.45 MB/s
```

**Use cases:**
- Debugging hash mismatches
- Monitoring progress on slow storage
- Verifying specific files are included
- Small directories where output is manageable

#### Combined Options

```bash
python scripts/bl_tool.py hash \
  --path /path/to/directory \
  --allow-hidden-files \
  --max-threads 8 \
  --verbose \
  --verify-sha256 expected_hash_here
```

---

## Use Cases

### Build Artifact Verification

**Scenario:** Verify that build artifacts haven't changed between builds.

```bash
# After build 1
python scripts/bl_tool.py hash --path ./build > build1_hash.txt

# After build 2
python scripts/bl_tool.py hash --path ./build > build2_hash.txt

# Compare
diff build1_hash.txt build2_hash.txt
```

**Alternative with verification:**
```bash
# Save hash from build 1
BUILD_HASH=$(python scripts/bl_tool.py hash --path ./build | grep "Combined SHA256:" | awk '{print $3}')

# Verify build 2 matches
python scripts/bl_tool.py hash --path ./build --verify-sha256 $BUILD_HASH
```

### Release Package Integrity

**Scenario:** Generate hash for release package, allow users to verify integrity.

```bash
# Create release package
tar czf release-v1.0.tar.gz ./dist

# Generate hash
python scripts/bl_tool.py hash --path release-v1.0.tar.gz

# Publish hash alongside release
echo "release-v1.0.tar.gz SHA256: abc123..." > release-v1.0.sha256
```

**User verification:**
```bash
# Download release and hash
# ...

# Verify
python scripts/bl_tool.py hash \
  --path release-v1.0.tar.gz \
  --verify-sha256 abc123...
```

### Directory Comparison

**Scenario:** Verify two directories have identical contents.

```bash
# Hash directory 1
HASH1=$(python scripts/bl_tool.py hash --path /path/to/dir1 | grep "Combined SHA256:" | awk '{print $3}')

# Hash directory 2 and verify
python scripts/bl_tool.py hash --path /path/to/dir2 --verify-sha256 $HASH1
```

**Exit code interpretation:**
- Exit 0: Directories are identical
- Exit 1: Directories differ

### CI/CD Pipeline Integration

**Scenario:** Verify build reproducibility in CI pipeline.

```yaml
# .github/workflows/verify-build.yml
- name: Build artifacts
  run: make build

- name: Hash artifacts
  run: |
    python scripts/bl_tool.py hash --path ./build > current_hash.txt
    cat current_hash.txt

- name: Verify against reference
  run: |
    EXPECTED_HASH=$(cat reference_hash.txt)
    python scripts/bl_tool.py hash --path ./build --verify-sha256 $EXPECTED_HASH
```

### Large File Integrity Check

**Scenario:** Verify integrity of large files (VM images, datasets, backups).

```bash
# Hash large file (10+ GB)
python scripts/bl_tool.py hash --path /path/to/large-file.img

# Later, verify integrity
python scripts/bl_tool.py hash \
  --path /path/to/large-file.img \
  --verify-sha256 expected_hash
```

**Benefits:**
- Memory-efficient (only 1 MB in memory at a time)
- Fast (parallel I/O if storage supports it)
- Progress indication with speed measurement

### Selective Directory Hashing

**Scenario:** Hash only visible files (ignore .git, .cache, etc.)

```bash
# Default: skips hidden files
python scripts/bl_tool.py hash --path /path/to/project

# Include hidden files (e.g., for dotfile repositories)
python scripts/bl_tool.py hash --path ~/.config --allow-hidden-files
```

### Preview Before Hashing

**Scenario:** Check what will be hashed before running expensive operation.

```bash
# Dry-run to see files and total size
python scripts/bl_tool.py hash --path /large/directory --dry-run

# If size is acceptable, run actual hash
python scripts/bl_tool.py hash --path /large/directory
```

---

## Performance Considerations

### Thread Count Guidelines

**Optimal thread count depends on:**
- **Storage type:**
  - SSD/NVMe: 8-16 threads
  - HDD: 1-4 threads
  - Network storage: 1-2 threads
- **File sizes:**
  - Many small files: More threads (CPU-bound)
  - Few large files: Fewer threads (I/O-bound)
- **CPU cores:**
  - Generally: threads ≤ CPU cores

**Benchmarking:**
```bash
# Test different thread counts
time python scripts/bl_tool.py hash --path /dir --max-threads 1
time python scripts/bl_tool.py hash --path /dir --max-threads 4
time python scripts/bl_tool.py hash --path /dir --max-threads 8
time python scripts/bl_tool.py hash --path /dir --max-threads 16
```

### Memory Usage

**Expected memory usage:**
- **Base overhead:** ~50 MB (Python interpreter, libraries)
- **Per thread:** ~1 MB (file chunk buffer)
- **Hash combining:** ~1 MB (block buffer, regardless of file count)

**Total:** ~50 MB + (threads × 1 MB) + 1 MB

**Examples:**
- 4 threads: ~55 MB
- 16 threads: ~67 MB

**Large directories (10M files):**
- Memory: ~55 MB (independent of file count!)
- Without optimization: Would need 320+ MB for hash combining

### Storage Considerations

**Sequential vs. Random I/O:**
- Alphabetical sorting ensures sequential directory traversal on most filesystems
- Better cache locality and read-ahead performance
- Especially important for HDDs

**Network Storage:**
- Reduce thread count (network latency dominates)
- Consider `--dry-run` first to estimate time
- Monitor network bandwidth utilization

---

## Troubleshooting

### Symlink Errors

**Error:**
```
[ERROR] Symlink encountered: /path/to/link
Symlinks are not supported by hash command
```

**Solution:**
- Remove symlink before hashing
- Copy symlink target to regular file
- Hash symlink target directly (resolve path first)

### Path Not Found

**Error:**
```
[ERROR] Path not found: /path/to/missing
```

**Solution:**
- Verify path exists: `ls -la /path/to/missing`
- Check path is accessible (permissions)
- Use absolute path to avoid ambiguity

### Verification Mismatch

**Error:**
```
Verification: MISMATCH
Expected: abc123...
Actual:   def456...
```

**Possible causes:**
- Files have changed since reference hash was created
- Hidden files included/excluded differently (check `--allow-hidden-files`)
- Different file order (shouldn't happen - canonical sorting is automatic)
- Symlinks in one version but not the other

**Debug steps:**
```bash
# Hash both directories verbosely
python scripts/bl_tool.py hash --path /dir1 --verbose > dir1.log
python scripts/bl_tool.py hash --path /dir2 --verbose > dir2.log

# Compare file lists and hashes
diff dir1.log dir2.log
```

---

## Version History

- **v1.0**: Initial implementation with `hashdir` command for directories
- **v1.1**: Added memory-efficient chunked file reading (1 MB chunks) and block-based hash combining (32K blocks)
- **v1.2**: Added `--verbose` flag to control per-file output
- **v1.3**: Redesigned to support both files and directories with unified `hash` command
- **v2.0**: Replaced the unframed digest with the versioned `blhash/v2` record format. Records are domain separated and length prefixed, directories are committed so tree shape is covered, file and tree scopes are distinguished, paths are canonicalized across platforms, and symlinked directories, junctions and non-regular files are rejected rather than silently skipped. **Digests from earlier versions cannot be reproduced.**

---

## License

This tool is part of the swblocks-baselib project. See the main project LICENSE file for details.

---

## Support

For issues, questions, or contributions, please refer to the main project repository and CONTRIBUTING.md file.
