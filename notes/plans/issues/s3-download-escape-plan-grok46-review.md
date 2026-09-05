The GPT review is largely right: the original F-01 direction holds, but several of these are real holes, not nits. I would **not** take the review verbatim (especially `dir_fd` TOCTOU and superscript `COM¹`). I **would** change the plan on the items below.

## Necessary — I agree

**1. Walk parents on both branches**  
The plan only no-follow-walks when creating a new file. The existing-file path still uses `os.path.exists` / `getsize` / hashing, which **follow** a symlinked parent. That breaks the no-follow decision you already locked. Walk every parent with `lstat` before verify **or** download. Require an existing dest to be a regular file (reject dir/FIFO/socket/device/symlink).

**Junctions, not just `islink()`**  
On Windows a directory junction is not a symlink. Production should treat `islink()` **or** `isjunction()` (3.12+) as hostile. The planned Windows *test skip* is still fine for *creating* links in CI; it must not mean “ignore junctions in the worker.”

**Not required:** POSIX `dir_fd` / no-follow open against a concurrent local attacker. F-01 is hostile **S3 keys**, not a local process swapping directories mid-run. State that explicitly.

**2. Verify temp, then publish**  
F-01 already said a failed download must not become a “real” dest. The plan’s `download_file(temp) → os.replace → existing verify` publishes a bad file first. Sequence should be:

`mkstemp → close fd → download into temp → verify temp → if dest still absent, replace → `finally` unlink temp`

Also agree: close the `mkstemp` fd before `download_file` (Windows), always clean up in `finally`, and do not let `os.replace` silently overwrite a dest that appeared after the existence check (re-`lstat`; if it now exists, skip replace and report error/different). `mkstemp`’s `0o600` is a behavior change vs umask; document or chmod — either is fine if tested.

**3. Collision preflight is too weak — in part**  
Agree these are real and in scope:

- **`a` vs `a/b`** — file/ancestor clash; workers race file vs mkdir. Treat like a destination collision: `[ERROR]` both, **no workers**, exit 1.
- **`mkdir` races** — two safe keys sharing a new parent. Catch `FileExistsError`, re-`lstat`, accept only a real non-symlink directory.

`normcase(normpath)` is **not** enough on macOS (case-insensitive APFS, NFD/NFC). I would canonicalize components with `casefold` + NFC **for collision identity only** (not for the on-disk name). Cost: a Linux bucket with both `README` and `readme` aborts. That is rare for this tool and matches “fail closed on collisions.”

**4. Do not create the root in preflight/dry-run**  
Agree. Docs say dry-run makes no changes. Invalid-only, collision-only, 404, and dry-run must not `mkdir` the root. Preflight is pure; create + re-resolve the root only when a real download is about to write. Handle “root exists but is not a directory” at command level.

**5. Trailing-slash markers**  
Agree this must be an explicit policy. Rejecting empty components makes `folder/` (AWS console folder marker) a hard error on ordinary buckets. **Skip** keys that end with `/` (do not `[ERROR]`, do not write a file). Do not mkdir just because the marker exists; real files under that prefix already create parents.

**Windows reject list**  
Agree to add: control chars `0x01–0x1F`, and `<>"|?*`. Already covering `\`, `:`, reserved names, trailing `.` / space. **Disagree** on superscript `COM¹` / `LPT²` — not needed for F-01.

## Policy — I now lean toward GPT

**Abort the whole command on any path-unsafe key**, same as collisions, **before any worker mutates the tree**. Partial download + exit 1 is how CI consumes a half-written tree.

Keep per-key `[ERROR]` for operational failures (404, I/O, ETag mismatch). That is different from “this bucket key is not a safe relative path.”

That revises your earlier “one `../` should not block a large prefix.” GPT’s extraction argument is stronger for a **critical** path-escape fix. If you want to keep partial success, it has to be documented and tested exactly as GPT says.

## Tests — agree with the useful ones

Add: symlink parent (existing and missing dest); `a` vs `a/b`; shared-parent mkdir; verify-fail / download-fail / replace-fail leave neither dest nor temp; dry-run/collision/invalid-only do not create root; trailing-slash skip; root may be a symlink; no `download_file` after fatal preflight.

FIFO/junction tests: only where the OS allows; production checks matter more than Linux CI covering junctions.

## What I would not take from the review

| GPT item | Take? |
|---|---|
| `dir_fd` / concurrent local attacker | No — out of threat model |
| Superscript `COM¹`/`LPT²` | No |
| Full MS naming encyclopedia | No — control chars + `<>"|?*` is enough |
| Abort-all because of ETag/`[DIFFERENT]` | No — those stay per-key |
| Implement APFS-aware FS probing | No — cheap NFC+casefold identity is enough |

**Bottom line:** GPT correctly found plan bugs (follow-on-verify, verify-after-replace, `a`/`a/b`, mkdir race, dry-run mkdir, `folder/`). Fold those in before implementation. Treat `dir_fd` and superscript devices as out of scope.

If you want, next step is to revise `s3-download-escape-plan.md` with these accept/reject calls (including abort-all on unsafe keys vs keep partial success).

I'll revise only `s3-download-escape-plan.md` to match the accepted GPT review items.

Updated `s3-download-escape-plan.md` with the accepted review items only.

Main plan changes:
- Abort the whole command on any path-unsafe key or namespace conflict (`a` vs `a/b`, leaf duplicates, NFC+casefold aliases) before workers or root creation
- Walk parents with `lstat` on both verify and download; reject junctions as well as symlinks
- Verify the temp file, then `os.replace` only if dest is still absent; close `mkstemp` fd first; always unlink temp in `finally`
- Skip trailing-slash folder markers; do not create the root on dry-run / invalid-only / collision-only
- Reject control chars and `<>"|?*`; keep `0o600` temps
- Explicitly out of scope: `dir_fd` TOCTOU, `COM¹`/`LPT²`, abort-all on ETag/`[DIFFERENT]`

