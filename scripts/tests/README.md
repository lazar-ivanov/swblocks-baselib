# bl_tool.py Test Suite

Comprehensive test suite for [bl_tool.py](../bl_tool.py) utility using pytest.

## Test Organization

- **test_bl_tool_unit.py**: Stage 1 - Integration-style unit tests (~35 tests)
- **test_bl_tool_functional.py**: Stage 2 - Functional/end-to-end tests (~20 tests)
- **conftest.py**: Shared fixtures and utilities
- **pytest.ini**: Pytest configuration

## Setup

### Install Dependencies

```bash
pip install -r requirements-test.txt
```

### Required Packages

- pytest >= 7.4.0
- pytest-cov >= 4.1.0
- pytest-timeout >= 2.1.0

## Running Tests

### All Tests

```bash
# Via pytest directly
cd scripts/tests
pytest -v

# Via Make (from project root)
make pytest
```

### Stage 1: Unit Tests

```bash
# Via pytest
cd scripts/tests
pytest -v test_bl_tool_unit.py

# Via Make
make pytest-unit
```

### Stage 2: Functional Tests

```bash
# Via pytest
cd scripts/tests
pytest -v test_bl_tool_functional.py

# Via Make
make pytest-functional
```

### With Coverage

```bash
# Terminal coverage report
make pytest

# Detailed HTML coverage report
make pytest-coverage
```

## Test Coverage

**Target:** 80%+ code coverage

**Current:** 86% coverage for [bl_tool.py](../bl_tool.py)

**Coverage includes:**
- All functions in bl_tool.py
- Error handling paths
- Edge cases (empty directories, large files, symlinks)
- Platform-specific behavior

## Fixtures

### Temporary Directories

- `empty_dir`: Empty directory
- `dir_with_files`: Directory with files and subdirectories
- `dir_with_hidden_files`: Directory with hidden files
- `dir_with_symlink`: Directory with symlink (Unix only)

### Temporary Files

- `temp_file_small`: < 1 MB file
- `temp_file_medium`: ~10 MB file (tests chunked reading)
- `temp_file_large`: ~100 MB file (tests memory efficiency)
- `temp_symlink`: Symlink to file (Unix only)

### Performance Testing

- `large_directory_tree`: Directory with 100 files in 10 subdirectories

## Platform-Specific Tests

Tests that require symlinks are automatically skipped on Windows:

```python
@pytest.mark.skipif(sys.platform == "win32", reason="Symlinks may not be supported on Windows")
def test_symlink_behavior(self, temp_symlink):
    ...
```

## Test Categories

### Stage 1: Unit Tests (35 tests)

**TestFormatting** (9 tests):
- Tests `format_size()` and `format_speed()` functions
- Covers all units: B, KB, MB, GB, TB, B/s, KB/s, MB/s, GB/s
- Edge cases: zero time, large values

**TestFileCollection** (6 tests):
- Tests `collect_files()` function
- Empty directories, multiple files, subdirectories
- Hidden file filtering
- Symlink detection
- Sorted output verification

**TestSingleFileHandling** (2 tests):
- Tests `handle_single_file()` function
- Regular files and symlink error handling

**TestDirectoryHandling** (1 test):
- Tests `handle_directory()` function
- File list format verification

**TestHashWorker** (5 tests):
- Tests `hash_worker()` function
- Small, medium, and large files (memory efficiency)
- Deterministic hashing
- Relative path inclusion
- Error handling

**TestCombineHashes** (5 tests):
- Tests `combine_hashes()` function
- Empty lists, single hashes, multiple hashes
- Order sensitivity
- Block-based processing (100,000 hashes)

**TestCommandHash** (7 tests):
- Tests `command_hash()` function (integration tests)
- Single file and directory hashing
- Dry-run mode
- Hash verification (match and mismatch)
- Nonexistent path error handling
- Empty directory handling

### Stage 2: Functional Tests (20 tests)

**TestCommandExecution** (2 tests):
- Help flag display
- Error on missing arguments

**TestSingleFileHashing** (4 tests):
- Small, medium, large files
- Deterministic hashing

**TestDirectoryHashing** (4 tests):
- Empty directories
- Directories with files
- Hidden file handling (included/excluded)

**TestVerification** (2 tests):
- Hash verification match
- Hash verification mismatch

**TestDryRun** (2 tests):
- Dry-run for single file
- Dry-run for directory

**TestVerboseMode** (2 tests):
- Verbose output (shows individual hashes)
- Non-verbose output (hides individual hashes)

**TestThreading** (1 test):
- Multi-threading option

**TestErrorHandling** (2 tests):
- Nonexistent path error
- Symlink error

**TestConsistency** (1 test):
- File vs directory hash consistency

## Troubleshooting

### Tests Fail on Windows

Some tests (symlink-related) are skipped on Windows. This is expected.

### Large File Tests Timeout

Increase timeout with:

```bash
pytest -v --timeout=60
```

### Coverage Below Target

Run with `--cov-report=term-missing` to see uncovered lines:

```bash
cd scripts/tests
pytest --cov=../ --cov-report=term-missing
```

## Contributing

When adding new features to bl_tool.py:

1. Add unit tests to `test_bl_tool_unit.py`
2. Add functional tests to `test_bl_tool_functional.py`
3. Ensure coverage remains above 80%
4. Update fixtures in `conftest.py` if needed

## Test Results Summary

**Total Tests:** 55 (35 unit + 20 functional)

**Coverage:** 86% for [bl_tool.py](../bl_tool.py)

**All tests passing:** ✅

**Platform support:** Linux, macOS, Windows (with conditional skips)
