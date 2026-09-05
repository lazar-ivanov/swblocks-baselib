# Fix F-10 — Directory hash is not an unambiguous commitment to a tree

## Context

`scripts/bl_tool.py hash` is documented as a tool for build-artifact verification, release-package
integrity, and directory comparison. Its current digest does not support those claims.

The leaf hash is `SHA256(file_contents ‖ relative_path_utf8)` with no framing between the two fields
([bl_tool.py:209-220](scripts/bl_tool.py#L209-L220)), and [combine_hashes](scripts/bl_tool.py#L252-L254)
returns a lone leaf verbatim rather than re-hashing it. So a one-file tree holding content `a` at path
`bc` and a one-file tree holding content `ab` at path `c` both hash `b"abc"` and report the identical
final digest. No SHA-256 weakness is needed — the framing is simply absent.

Three further gaps in the same commitment:

- `os.walk(..., followlinks=False)` ([bl_tool.py:110](scripts/bl_tool.py#L110)) leaves symlinked
  directories in `dirs` and silently drops their subtrees, despite the documented "fails immediately on
  symlink" contract. Windows junctions evade the check entirely — `Path.is_symlink()` is `False` for them.
- Empty directories emit no record, so trees differing only in directory structure collide.
- `str(relative_path)` yields `a\b` on Windows and `a/b` on POSIX, so an identical tree hashes differently
  per platform; a filename with undecodable bytes on Linux raises `UnicodeEncodeError` mid-run; and a FIFO
  inside a directory is collected by `os.walk` and blocks forever in `open()`.

Nothing in the repository pins a bl_tool digest — no makefile, script, or CI step passes
`--expected-hash`. Only [bl_tool.md](scripts/bl_tool.md) and the two test suites encode the current
algorithm, so the format change breaks externally-saved hashes only, never the build.

**Outcome:** a domain-separated, length-prefixed, versioned digest that is an unambiguous commitment to a
tree, with symlinks/junctions/special files rejected before traversal.

## Decisions taken

| Decision | Choice |
|---|---|
| v1 compatibility | Clean break. v2 only, no `--hash-format` flag. Old digests become unreproducible by design. |
| File vs one-file-tree scope | Separated by distinct root tags — the two digests now differ. |
| `--exclude-paths` | Stays an unframed raw-content mode. Single-file output still equals `sha256sum`. Documented as *not* a tree commitment. |
| Hardening | All four: reject directory symlinks/junctions, empty-directory records, path normalization + encoding, reject non-regular files. |

Adding no new CLI argument matters beyond simplicity: the existing unit tests build
`argparse.Namespace(...)` with a fixed attribute set (e.g.
[test_bl_tool_unit.py:526-537](scripts/tests/test_bl_tool_unit.py#L526-L537)), so any new option read by
`command_hash` would break every one of them with `AttributeError`.

## The v2 format

Constants to add near `FILE_CHUNK_SIZE` in [bl_tool.py](scripts/bl_tool.py#L34-L36):

```python
LEAF_TAG_FILE = b"blhash/v2/file\x00"
LEAF_TAG_DIR  = b"blhash/v2/dir\x00"
ROOT_TAG_TREE = b"blhash/v2/tree\x00"
ROOT_TAG_FILE = b"blhash/v2/single\x00"
```

All lengths are unsigned 64-bit big-endian (`n.to_bytes(8, 'big')`).

```
path_bytes   = relative_path.encode('utf-8', 'surrogateescape')   # already '/'-normalized at collection

file leaf    = H( LEAF_TAG_FILE ‖ u64(len(path_bytes)) ‖ path_bytes ‖ u64(size) ‖ H(contents) )
dir  leaf    = H( LEAF_TAG_DIR  ‖ u64(len(path_bytes)) ‖ path_bytes )

root         = H( root_tag ‖ algo_name ‖ 0x00 ‖ u64(entry_count) ‖ leaf_1 ‖ leaf_2 ‖ … ‖ leaf_N )
```

- `root_tag` is `ROOT_TAG_TREE` for a directory target, `ROOT_TAG_FILE` for a single-file target — this is
  what separates the two scopes.
- `algo_name` is `sha256` / `sha1` as ASCII, binding the digest to its algorithm.
- Records are sorted by `path_bytes` (a file and a directory can never share a path, so this is a total
  order). Sorting bytes rather than `str` keeps the order locale- and platform-independent.
- The file leaf hashes `H(contents)` rather than the contents inline, so the existing 1 MB chunked
  streaming is preserved and the framing hash stays fixed-size. Size is the byte count actually read.
- An empty tree yields `H(root_tag ‖ algo ‖ 0x00 ‖ u64(0))`, distinct from today's `H(b"")`.

## Implementation

All edits via the Edit tool. This is a logic-only change — no formatting or naming passes mixed in.

### `scripts/bl_tool.py`

**1. Path canonicalization helper.** Add `encode_path(relative_path)` returning
`relative_path.encode('utf-8', 'surrogateescape')`. Normalize separators once at collection time by
building relative paths with `PurePath.as_posix()` instead of `str()`, so both the sort key and the
hashed bytes are already `/`-separated.

**2. `collect_files`** ([bl_tool.py:93-136](scripts/bl_tool.py#L93-L136)):
- Walk the *resolved* root (`os.walk(folder_path_obj)`) and compute relative paths lexically. This drops
  the per-file `resolve()` at [line 129](scripts/bl_tool.py#L129) — its only purpose was reconciling the
  unresolved walk prefix against the resolved root, which walking the resolved root handles directly.
  Guard `relative_to` with a clean error message instead of letting `ValueError` escape as a traceback.
- After the hidden-directory filter, reject any entry left in `dirs` that is a symlink or junction, before
  descending. Use a small `is_reparse_link(path)` helper: `Path.is_symlink()` or `os.path.isjunction` when
  present (3.12+; the project venv is 3.12.3) via `getattr(os.path, 'isjunction', None)` so older
  interpreters degrade rather than crash.
  **Superseded (2026-09-04):** that degrade-on-older-interpreters form failed *open* on junctions
  below 3.12, which `s3-download-escape-plan.md` forbids; `is_reparse_link` now falls through to
  the `st_file_attributes` reparse flag of an `lstat()`, the same fail-closed shape as
  `s3_manage.is_hostile_reparse` (review finding L-17 in
  `notes/reviews/major/update_2025/v1/pr_review_analysis_fable51.md`).
- Replace `file_path.is_symlink()` with a single `os.lstat`, checking `stat.S_ISLNK` (symlink → existing
  error) and `stat.S_ISREG` (non-regular → new error naming the FIFO/socket/device). One syscall replaces
  two and closes the blocking-`open()` hole.
- Return directory records alongside file records so empty directories are committed. Record every
  directory except the root — uniform, and it captures dirs holding only filtered-out entries for free.
  Directories pruned by the hidden filter are never visited and correctly emit nothing.

**3. `hash_worker`** ([bl_tool.py:183-232](scripts/bl_tool.py#L183-L232)): when `exclude_paths` is false,
finish the content hash, then frame it into the file leaf per the spec above. Keep the return tuple as
`(relative_path, hash_bytes, file_size)` with element 0 still the `str` — the existing `TestHashWorker`
assertions then continue to hold unchanged. When `exclude_paths` is true, return `H(contents)` exactly as
today.

**4. New `compute_root_digest(records, root_tag, hash_algo)`.** Emits the root prefix, then feeds leaves in
`HASH_BLOCK_SIZE` blocks — the existing memory strategy from
[combine_hashes](scripts/bl_tool.py#L259-L266) carried over. **Leave `combine_hashes` itself untouched**;
it remains the raw-mode combiner, which keeps its whole `TestCombineHashes` suite valid.

**5. `command_hash`** ([bl_tool.py:271-404](scripts/bl_tool.py#L271-L404)):
- Reject a symlinked or junction root before the `is_file()`/`is_dir()` branch at
  [lines 293-303](scripts/bl_tool.py#L293-L303) — today a symlinked *directory* root is silently accepted
  while a symlinked file root is rejected by
  [handle_single_file](scripts/bl_tool.py#L154-L157). Leave `handle_single_file`'s own check in place; a
  unit test calls it directly.
- Merge directory records with the worker's file results, sort the combined list by `encode_path`, and
  call `compute_root_digest` with `ROOT_TAG_FILE` or `ROOT_TAG_TREE`. Route `--exclude-paths` to the
  existing `combine_hashes` path instead.
- Keep `Total files processed` and `Total size` counting **files only** — directory records must not
  inflate them, or the empty-directory tests and the summary's meaning both change.
- Add one summary line: `Hash format: blhash/v2` (or `raw-content (no tree commitment)` under
  `--exclude-paths`), so a digest's provenance is visible in ops output.

### `scripts/bl_tool.md`

Rewrite **Hash Algorithm Design** ([bl_tool.md:169-260](scripts/bl_tool.md#L169-L260)): the v2 record
format and root construction, the version tag, empty-directory behavior, path normalization and
`surrogateescape` encoding, the symlink/junction/special-file rejection contract, and the raw-mode caveat.
Replace the **Hash Consistency** subsection ([bl_tool.md:225-241](scripts/bl_tool.md#L225-L241)) — the
file/one-file-directory equivalence it advertises is now deliberately false. Add a short compatibility note
that v1 digests cannot be reproduced.

> Out of scope, worth a separate commit: the Use Cases section
> ([bl_tool.md:617-720](scripts/bl_tool.md#L617-L720)) documents a `--verify-sha256` flag that does not
> exist — the real option is `--expected-hash`. Pre-existing doc bug, unrelated to F-10; per AGENTS.md,
> not mixed into this change.

### Tests

Update in place:
- `test_single_file_matches_directory` ([test_bl_tool_functional.py:359](scripts/tests/test_bl_tool_functional.py#L359))
  — invert to assert the two digests now **differ**, and rename to reflect scope separation.
- Sweep both suites for any other assertion tied to the old construction. `TestCombineHashes`,
  `TestExcludePaths`, `TestSha1Mode`, and the empty-directory tests should all survive untouched given the
  decisions above — verify rather than assume.

Add:
- **The F-10 vector itself**: tree A = file `bc` containing `a`; tree B = file `c` containing `ab`. Assert
  different digests. This test fails on current `master` and is the regression guard for the finding.
- **Scope separation**: `hash --path dir/f` ≠ `hash --path dir/` for a one-file `dir`.
- **Structural commitment**: `{f, a/}` (with `a` empty) ≠ `{f}`; nested empty chains `a/b/c/` recorded.
- **Symlinked directory rejection**: exit 1 with the symlink error, replacing today's silent subtree drop.
  Reuse the `dir_with_symlink` fixture pattern in
  [conftest.py:88-102](scripts/tests/conftest.py#L88-L102) (it already skips on Windows).
- **FIFO inside a directory**: exit 1 rather than hanging. Mark `skipif win32`, matching
  [test_command_hash_special_file](scripts/tests/test_bl_tool_unit.py#L546).
- **Unicode and non-UTF-8 paths**: NFC/NFD-distinct names hash differently (no silent Unicode
  normalization — it would conflate genuinely distinct Linux filenames); a surrogate-escaped name hashes
  without raising.
- **Golden vectors**: hardcoded expected hex for a fixed small tree, a single file, and an empty
  directory, computed once at implementation time. These catch accidental format drift, which is otherwise
  invisible.

## Verification

```bash
# Test deps (creates .venv if absent)
make pytest-install

# Full bl_tool suite
.venv/bin/pytest scripts/tests/test_bl_tool_unit.py scripts/tests/test_bl_tool_functional.py -v

# Whole Python suite — confirm nothing else regressed
make pytest
```

Manual confirmation of the finding, before and after:

```bash
mkdir -p /tmp/f10/a /tmp/f10/b
printf 'a'  > /tmp/f10/a/bc
printf 'ab' > /tmp/f10/b/c
.venv/bin/python scripts/bl_tool.py hash --path /tmp/f10/a
.venv/bin/python scripts/bl_tool.py hash --path /tmp/f10/b
# before: identical digests.  after: different digests.

# Symlinked directory is now rejected instead of silently skipped
mkdir -p /tmp/f10/real/sub && touch /tmp/f10/real/sub/x
ln -s /tmp/f10/real/sub /tmp/f10/a/link
.venv/bin/python scripts/bl_tool.py hash --path /tmp/f10/a   # expect exit 1 + symlink error

# Raw mode still matches sha256sum for a single file
.venv/bin/python scripts/bl_tool.py hash --path /tmp/f10/a/bc --exclude-paths
sha256sum /tmp/f10/a/bc
```

Cross-platform separator normalization cannot be exercised from Linux alone; the golden vectors plus a
direct unit test on the path encoder cover it, and the Windows leg should be run under devenv7 if
available.

Finally, `git diff` should touch exactly four files — [scripts/bl_tool.py](scripts/bl_tool.py),
[scripts/bl_tool.md](scripts/bl_tool.md), and the two `scripts/tests/test_bl_tool_*.py` modules — with no
formatting or naming changes mixed in.
