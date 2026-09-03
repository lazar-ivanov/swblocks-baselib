# Plan: Fix F-01 S3 Download Path Escape

TL;DR: S3 object keys are joined onto `--local-folder` with no validation, so hostile keys can write outside the download root. Add a strict key-to-path resolver, reject traversal/absolute/Windows-hostile keys and any symlink or reparse point in the destination chain, abort the whole command before any worker starts if any key is path-unsafe or collides in the destination namespace, skip zero-byte S3 folder-marker keys only after their components validate, and publish only after verifying a unique temp file written through `download_fileobj`.

Confirmed decisions:
- Symlinks, Windows directory junctions, and other reparse points: reject any in the destination chain (no-follow), even if the target stays inside the root. `os.path.islink()` alone is not enough. Do **not** fail open on Python &lt; 3.12: never use `getattr(os.path, "isjunction", lambda p: False)`.
- Path-unsafe keys and destination-namespace conflicts abort the **entire** command before any worker runs, including dry-run. Print `[ERROR]` for each offending key using `repr(key)` so ESC/newline cannot twist the terminal. Submit no workers, `sys.exit(1)`.
- Operational failures (404, I/O, ETag/`[DIFFERENT]`) stay per-key; they do not abort siblings. A listed object that 404s at `head_object` is an operational race, not F-01; after a clean preflight, creating the root before workers is allowed.
- Trailing-slash keys: validate components **first**. Only a key whose components are otherwise safe, that has exactly one trailing empty component, **and** whose listed `Size` is 0 is a folder marker (skip: no `[ERROR]`, no file, no mkdir for the marker itself). `../`, `/absolute/`, `foo//`, `CON/`, and nonzero trailing-slash objects are path-unsafe and abort the command. Real files under a marker prefix still create parents.
- Windows reserved names, rejected on all platforms, using CPython's compact table and match rule (portion before the first `.`, `rstrip` spaces, case-insensitive): `CON`, `PRN`, `AUX`, `NUL`, `CONIN$`, `CONOUT$`, `COM1`–`COM9`, `COM¹`/`COM²`/`COM³` (`\xb9\xb2\xb3`), same for `LPT`. Also reject ADS (`:`), trailing-dot/space, control characters `0x01–0x1F`, and `<>"|?*`. Do not reproduce the rest of the Microsoft naming encyclopedia (8.3 mangling, `\\.\`, extra historical wildcard rules).
- Threat model is hostile **S3 keys** and pre-existing dest-tree reparse points, not a concurrent local process swapping directories mid-run. Do not implement POSIX `dir_fd` / no-follow-open TOCTOU hardening. Document that the destination tree must not be concurrently writable by an untrusted local user.

## Steps

### Phase 1 — Path resolver (no I/O)

1. In scripts/s3_manage.py, add `UnsafeDownloadPathError` and `validate_s3_key_components(s3_key)` that treats the key as an S3 POSIX path (`/` only), never as an OS path. Do not call `os.path.join(local_folder, s3_key)`.
2. Validate all components **before** any folder-marker classification. Do not skip a key merely because it ends with `/`.
3. Reject before any join:
   - empty key, NUL (`\x00`), key that is only slashes
   - absolute / drive-qualified / UNC forms (`/…`, `X:…`, `\\…`, `//server/…`)
   - `\` anywhere (prevents Windows separator smuggling)
   - empty, `.`, or `..` components, except **one** trailing empty component produced by a final `/` (that trailing empty component is not itself a path segment; it only marks a possible folder marker)
   - `:` in a component (ADS / drive)
   - control characters `0x01–0x1F` and `<>"|?*` in any component
   - reserved device names on every component via the CPython table and match rule above (`CON`, `CON.txt`, `NUL .txt`, `conin$`, `COM¹`, `LPT³`, …)
   - components ending in `.` or space (Windows trailing-dot/space collapse), other than `.` / `..` which are already rejected
4. If the remaining components are safe and the key ends with `/`: return a marker classification plus the parent components. `command_download` treats it as a skip **only** when listed `Size` is 0; otherwise it is path-unsafe. Direct `download_worker()` calls use `head_object` `ContentLength` for the same zero/nonzero decision and return category `"skipped"` only for a zero-byte marker (do not invent a silent no-op and do not reuse `"verified"` / `"downloaded"`).
5. Otherwise return the validated component list using the original S3 spelling for on-disk names. Keep full S3 key mirroring (do not strip `--prefix`).

### Phase 2 — Containment, no-follow dirs, verify-then-publish

6. Split root handling into two operations. Key preflight does **not** create or `realpath` the download root.
   - `resolve_download_root(local_folder, create=False)`: non-mutating. Used after a clean key preflight for dry-run and existing-file inspection. Always call `os.path.realpath`, including for a missing final root, so any existing symlinked parents are resolved without creating anything. Require an existing root to be a directory; otherwise keep the resolved prospective path as a not-yet-created dry-run root.
   - `ensure_download_root(local_folder)`: mutating. Called **once** after preflight is clean **and** this run will actually download at least one missing file. Create the root if missing, then `realpath` it (the user-controlled root may be a symlink), require it is a directory.
7. Add `resolve_download_path(root, components)`: join only validated components under the resolved root with `os.path.join`, `os.path.normpath`, then prove containment with `os.path.commonpath([root, dest]) == root` and `dest != root`. `commonpath` is already component-aware; do not add a separate `os.sep` prefix check as the containment rule.
8. Add `is_hostile_reparse(path, st=None)` that **fails closed**:
   - Always reject `os.path.islink(path)`.
   - If `os.path.isjunction` exists, reject when it returns true.
   - On Windows, if the `lstat` result has `st_file_attributes` with `FILE_ATTRIBUTE_REPARSE_POINT`, reject (covers junctions and other reparse points without requiring Python 3.12).
   - Never default a missing `isjunction` to “not a junction.”
9. Add `lstat_walk_parents(root, dest)` used on **both** the existing-file verify path and the new-file download path, before any `exists`/`getsize`/hash/`download_fileobj`. For each relative component, `lstat` (not `stat`/`exists`/`isfile`, which follow links). Reject hostile reparse points, and reject any existing entry that is not a directory until the final component. The final component, if it exists, must be a regular file (`stat.S_ISREG` on the lstat result); reject directories, FIFOs, sockets, devices, symlinks, junctions, and other special files.
10. Add `ensure_parent_dirs_nofollow(root, dest)`: same walk; if a parent is missing, `os.mkdir` that component (do not use `os.makedirs`). On `FileExistsError`, immediately `lstat` and accept only a real directory that is not a hostile reparse; otherwise error. If an existing non-dir blocks a parent, error.
11. Change `download_worker` (scripts/s3_manage.py ~333–465):
    - Resolve/validate the key before any local I/O. On `UnsafeDownloadPathError`, return `("[ERROR] {repr(key)} - {reason}", 0, "error")` and write nothing. On a folder-marker key, return `("[SKIPPED] {repr(key)} (folder marker)", 0, "skipped")`.
    - Walk parents with `lstat` before the verify/download branch split.
    - Existing regular file: keep current verify-only / no-overwrite behavior (hash the dest, do not download).
    - New file, dry-run: report would-download; create nothing.
    - New file, real download: ensure parents no-follow; `fd, tmp = tempfile.mkstemp(prefix=".s3dl-", dir=parent)`. **Keep `fd` open.** Sequence: `os.fdopen(fd, "wb")` as `fileobj` → `s3_client.download_fileobj(bucket, key, fileobj)` → close `fileobj` → verify `tmp` (size/ETag) → `lstat` dest again → `os.replace(tmp, dest)` only if dest is still absent. Do **not** call `download_file()` on the temp path: s3transfer writes to its own staging file and renames over the supplied name, replacing the `0o600` inode. If dest now exists, do not replace; report `[ERROR]` (no silent overwrite). If verify fails, do not publish. `finally`: unlink `tmp` if it still exists (download, verify, and replace failures). Leave `mkstemp` permissions at `0o600`; document and test that on POSIX only (do not chmod up to umask; do not assume POSIX modes on Windows).
12. Add `tempfile`, `stat`, and `unicodedata` to the module imports. Do not touch the local `tempfile` import inside `command_indexupload`.

### Phase 3 — Pure preflight abort in `command_download`

13. After listing objects and **before** `ensure_download_root` or `ThreadPoolExecutor` (scripts/s3_manage.py ~1021–1138):
    - Classify each key: path-unsafe, zero-byte folder marker, or validated components. Nonzero trailing-slash objects are path-unsafe.
    - Print `[ERROR] {repr(key)}` for every path-unsafe key.
    - Build a component trie keyed by **collision identity** of each component: `NFD(casefold(NFD(component)))` via `unicodedata.normalize("NFD", …)`. On-disk names stay the original spelling. Detect (1) duplicate leaf destinations, (2) file-versus-ancestor conflicts such as `a` and `a/b`. `normcase(normpath(dest))` and NFC-then-`casefold` are not sufficient.
    - If any path-unsafe key or any namespace conflict exists: print `[ERROR]` for every colliding key, **submit no workers**, do not create the download root, print summary (include skipped-marker count even if 0), `sys.exit(1)`. This includes dry-run.
    - If preflight is clean: do not queue folder markers; count them as skipped, not errors. Queue only validated unique keys. Call `resolve_download_root(create=False)` for dry-run / verify-only inspection. Call `ensure_download_root()` once if this run will actually download. Existing exit-1 if later `error_count or different_count`.
14. Keep `download_worker` self-validating so direct unit-test callers stay safe.

### Phase 4 — Tests and docs

15. Add a pure (no moto) test class in scripts/tests/test_s3_manage_unit.py for the resolver: safe nested keys; classify `folder/` as a marker only after components validate; reject `../`, `../outside.txt`, `../../escape/`, `/absolute/`, `foo//`, `CON/`, `foo/../bar`, `/etc/passwd`, empty, `.`, `..`, NUL, `\\server\share`, `C:\…`, `C:/…`, `//server/share`, `\`, `foo:bar`, control chars, `<>"|?*`, `CON`, `con.txt`, `NUL .txt`, `CONIN$`, `CONOUT$`, `COM1`, `COM¹`, `LPT9`, `LPT³`, `foo.`, `foo `. Add a mocked/pure unit test of `is_hostile_reparse` so Linux CI covers the Windows reparse reject path without creating junctions.
16. Add worker tests (moto + `temp_dir`):
    - Hostile key does not create files outside the download root (sibling sentinel untouched).
    - Existing in-root symlink file, symlink parent with an existing dest, and symlink parent with a missing dest are `[ERROR]` and do not write through the link (`pytest.mark.skipif` win32 for *creating* symlinks in tests, same as scripts/tests/conftest.py). Production still rejects junctions/reparse points via `is_hostile_reparse`.
    - Direct `download_worker` on a folder-marker key returns `"skipped"`.
    - FIFO/socket/device dest rejected where the OS allows creating them.
    - Download failure, verification mismatch, and `os.replace` failure each leave neither the final dest nor a leftover temp.
    - Successful nested download still works; the published final file retains the verified temp inode's `0o600` mode on POSIX.
    - Root itself being a symlink remains allowed.
17. Add `command_download` tests:
    - Path-unsafe key: `[ERROR]` with `repr(key)`, no workers, no `download_file` / `download_fileobj`, root not created, `SystemExit` 1 — even when other keys are safe, and even in dry-run. Includes traversal-file forms such as `../outside.txt` and trailing-slash forms such as `../`.
    - Colliding leaves and `a` versus `a/b`: same abort, no files.
    - Case-fold and NFD/NFC Unicode collisions abort the command.
    - Collision-only, invalid-only, and dry-run-with-no-existing-root runs do not create the root.
    - Zero-byte trailing-slash folder markers are skipped, appear in the skipped-marker count, and do not fail the command by themselves.
    - Nonzero trailing-slash object aborts the command.
    - Shared-parent mkdir of two new files succeeds (FileExistsError revalidation).
    - After a clean preflight, a later per-key 404 may leave an empty created root; that is allowed and is not F-01.
18. Document in scripts/s3_manage.md Download Behavior / Download Errors / Security Notes: untrusted keys; reject list including CPython reserved names; no-follow including junctions/reparse points; verify-then-publish via `download_fileobj`; POSIX `0o600` temps; folder-marker skip only for validated zero-byte trailing-slash keys; path-unsafe keys and namespace conflicts abort the whole command before mutation; operational `[ERROR]`/`[DIFFERENT]` remain per-key; destination tree must not be concurrently writable by an untrusted local user; print keys with `repr`.

**Do not** mix F-02–F-18, style-only churn, or `indexupload`/`upload` path logic.

## Relevant files

- scripts/s3_manage.py — `download_worker` (join at ~366, `makedirs`+`download_file` at ~421–431), `command_download` (~1021–1138)
- scripts/tests/test_s3_manage_unit.py — `TestDownloadWorker` (~1334), `TestCommandDownload` (~2547); add resolver + traversal + collision classes
- scripts/tests/conftest.py — reuse `temp_dir`, `download_dir`; symlink skip pattern
- scripts/s3_manage.md — download section ~615+, errors ~973+, security ~1027+

## Verification

1. `.venv/bin/python -m pytest scripts/tests/test_s3_manage_unit.py -v` (create venv with `make pytest-install` if missing). Interpreter: project `.venv`.
2. Confirm existing `TestDownloadWorker` / `TestCommandDownload` still pass (nested paths, prefix mirroring, overwrite-protection, dry-run).
3. Manual sanity: resolver unit tests cover every rejected form above; worker test with key `../outside.txt` leaves `temp_dir` parent untouched; a mixed safe+`../` listing creates no files and starts no workers.

## Decisions

- Scope is F-01 only.
- Fail closed on path-unsafe keys and destination-namespace conflicts for the **entire** command, before filesystem mutation.
- Zero-byte validated folder markers are skipped, not errors; nonzero or unsafe trailing-slash keys abort.
- Validator is platform-agnostic so Linux CI catches Windows-hostile keys.
- Collision identity is `NFD(casefold(NFD(component)))` per component in a trie (leaf duplicates and file/ancestor). Not `normcase(normpath)` alone; not NFC-then-`casefold`; not live APFS filesystem probing. `README` vs `readme` aborting on Linux is an acceptable fail-closed false positive.
- User root may be a symlink; attacker-controlled components may not.
- Download through the still-open `mkstemp` fd with `download_fileobj`. POSIX `0o600` is the intended temp permission; assert it only on POSIX.
- Error lines print `repr(key)`. Worker skip category is `"skipped"`. Command summary includes a skipped-marker count.

## Out of scope (review items rejected)

- POSIX `dir_fd` / no-follow open against a concurrent local attacker.
- The full Microsoft file-naming encyclopedia beyond CPython's compact reserved-name table and the character/ADS/trailing-dot rules already listed.
- Aborting sibling downloads because of ETag mismatch or operational `[ERROR]`.
- Requiring Python 3.12+ solely for `os.path.isjunction`.
- Lazy/synchronized root creation so a later 404 cannot mkdir; after a clean preflight, creating the root before workers is allowed.
- APFS-aware filesystem probing beyond `NFD(casefold(NFD(…)))` identity.
- Calling `download_file()` on a `mkstemp` path (s3transfer would replace the inode).
