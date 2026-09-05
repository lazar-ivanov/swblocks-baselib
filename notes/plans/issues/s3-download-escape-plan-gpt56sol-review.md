## Verdict

The plan is directionally strong, but I would not implement it verbatim yet. It fixes the obvious traversal cases identified in [F-01](/home/lazar/dev/github/swblocks-baselib/notes/reviews/swblocks-baselib/major/update_2025/v1/pr_review_analysis_gpt56sol.md:31), but several gaps could leave the implementation incomplete or introduce nondeterministic behavior.

The current vulnerability is confirmed: the worker directly joins the untrusted key at [s3_manage.py:366](/home/lazar/dev/github/swblocks-baselib/scripts/s3_manage.py:366), creates its parents at [line 424](/home/lazar/dev/github/swblocks-baselib/scripts/s3_manage.py:424), and downloads to it at [line 431](/home/lazar/dev/github/swblocks-baselib/scripts/s3_manage.py:431). Every key is then submitted concurrently at [line 1091](/home/lazar/dev/github/swblocks-baselib/scripts/s3_manage.py:1091).

### Changes I consider necessary

1. **Apply no-follow validation before both branches.**

   The plan only invokes `ensure_parent_dirs_nofollow()` for a new file ([plan line 34](/home/lazar/dev/github/swblocks-baselib/notes/plans/issues/s3-download-escape-plan.md:34)). An existing file reached through a symlinked parent would still be followed during `getsize()` and hashing.

   Walk and validate every parent before checking or reading the destination. Use one `lstat()` per entry and require an existing destination to be a regular file; reject directories, FIFOs, sockets, devices, symlinks, and other special files.

   On Windows, `islink()` is insufficient because directory junctions and other reparse points can redirect traversal. Python exposes junction and reparse-point information separately. The proposed Windows test skip at [plan line 49](/home/lazar/dev/github/swblocks-baselib/notes/plans/issues/s3-download-escape-plan.md:49) therefore leaves an important platform-specific hole. See Python’s [`isjunction()` documentation](https://docs.python.org/3/library/os.path.html#os.path.isjunction).

   Also, path-based `lstat()` checks only protect against a static directory tree. If the intended guarantee includes concurrent local mutation, use directory handles/`dir_fd` plus no-follow opens on POSIX and an equivalent reparse-point-safe implementation on Windows—or explicitly state that concurrent local attackers are outside the threat model.

2. **Verify the temporary file before publishing it.**

   The intended sequence should be explicit:

   `download to temp → verify temp → atomically publish → cleanup`

   The current wording suggests `download_file(temp) → os.replace(dest)` followed by the existing verification logic. That would publish a size/ETag mismatch before discovering it.

   Also specify that:

   - `mkstemp()` returns an open descriptor that must be closed before passing the path to `download_file()`, especially on Windows. [Python documents that the caller must close it](https://docs.python.org/3/library/tempfile.html#tempfile.mkstemp).
   - Cleanup happens in `finally`, including download, verification, and replace failures.
   - The resulting file-permission behavior is tested.
   - The no-overwrite policy is defined. `os.replace()` silently overwrites a destination that appears after the existence check, so it does not fully preserve the stated verify-only/no-overwrite contract.

3. **Strengthen collision preflight.**

   `normcase(normpath(dest))` is not a sufficient filesystem identity ([plan line 42](/home/lazar/dev/github/swblocks-baselib/notes/plans/issues/s3-download-escape-plan.md:42)):

   - Python leaves `normcase()` unchanged on non-Windows systems. [That misses case aliases on default case-insensitive APFS](https://docs.python.org/3/library/os.path.html#os.path.normcase).
   - APFS is also normalization-insensitive, so composed and decomposed Unicode filenames can alias. [Apple documents this behavior](https://developer.apple.com/library/archive/documentation/FileManagement/Conceptual/APFS_Guide/FAQ/FAQ.html).
   - It misses file-versus-directory namespace conflicts such as keys `a` and `a/b`. Those workers race over whether `a` is a file or directory.
   - Two workers creating the same missing parent can both see it absent and then race in `os.mkdir()`.

   I would build a preflight component trie using a documented, conservative canonical component identity, and detect:

   - duplicate leaf destinations;
   - file/ancestor conflicts;
   - portable case/Unicode aliases;
   - optionally different spellings of the same directory component.

   Handle shared-parent creation serially, under a lock, or catch `FileExistsError` and immediately revalidate the new entry.

4. **Do not create the download root during preflight or dry-run.**

   `resolve_download_root()` creates the root at [plan line 27](/home/lazar/dev/github/swblocks-baselib/notes/plans/issues/s3-download-escape-plan.md:27). Consequently, an invalid key, collision-only run, missing S3 object, or dry-run could still create a directory. That conflicts with the documented promise that dry-run makes no changes at [s3_manage.md:649](/home/lazar/dev/github/swblocks-baselib/scripts/s3_manage.md:649).

   Keep preflight pure/non-mutating. Create and re-resolve the root only after validation succeeds and only for a real download. Add explicit command-level handling for an inaccessible root or a root that is an existing non-directory.

5. **Clarify key-name policy.**

   Rejecting empty components also rejects `folder/`. S3 commonly creates zero-byte trailing-slash objects as folder markers. [AWS documents that behavior](https://docs.aws.amazon.com/AmazonS3/latest/userguide/object-keys.html). Decide explicitly whether to skip such markers, create the directory, or reject them with a distinct error; otherwise ordinary console-created buckets may always exit nonzero.

   If “reject Windows-hostile keys on every platform” is literal, the list is incomplete. Windows also excludes control characters and `< > " | ? *`, and reserves superscript forms such as `COM¹` and `LPT²`. [Microsoft’s naming rules list the complete set](https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file). Otherwise, narrow the wording to “Windows path aliases and security-sensitive forms.”

6. **Reconsider partial success for unsafe keys.**

   Allowing safe files to download after discovering a hostile key is defensible, but it leaves a partial tree that callers may consume despite exit code 1. For a security-critical extraction-style operation, I would abort the entire command before filesystem mutation whenever any unsafe key or namespace conflict is found.

   If partial success remains the chosen policy, document it prominently and test the exact summary/error counts.

### Tests I would add

In addition to those already listed:

- Symlinked parent with both an existing and missing target file.
- Windows junction/reparse-point rejection.
- Destination FIFO/socket/device rejection where supported.
- `a` versus `a/b`.
- Case and Unicode-normalization collisions.
- Concurrent downloads sharing newly created parent directories.
- Partial download followed by exception, verification mismatch, and `os.replace()` failure; each must leave neither final nor temp files.
- Dry-run, invalid-only, and collision-only runs do not create the root.
- Explicit trailing-slash folder-marker behavior.
- Root itself being a symlink remains allowed.
- Assert no worker or `download_file()` call occurs after a fatal preflight error.

With those revisions, the plan would be a solid F-01 remediation. No files were changed and, since this is an unimplemented plan review, no tests were run.
