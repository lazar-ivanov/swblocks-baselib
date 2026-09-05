The second review is right. I would take issues 1–5, the small improvements, and the compact CPython device table. The plan is close, but it is not implementation-ready until those are written in.

## Issues 1–5 — accept

**1. Junction helper must not fail open**  
`os.path.isjunction()` is 3.12+; docs still say “Python 3.x”. `getattr(..., False)` on older Windows Python would allow junction traversal.

Do **not** bump the script to 3.12 only for this. Fail closed on the `lstat` result:

- Always reject `islink()`.
- Call `isjunction()` when it exists.
- On Windows, if `st_file_attributes` has `FILE_ATTRIBUTE_REPARSE_POINT`, reject (covers junctions and other reparse points without 3.12).

Keep the Windows *creation* skip in tests. Add a pure/mocked unit test of the helper so Linux CI still covers the reject path.

**2. Do not skip trailing-slash keys before validation**  
Classifying any nontrivial `…/` as a marker first would skip `../`, `/absolute/`, `foo//`, `CON/`. They cannot write, but they would not abort, which breaks the new abort-all-unsafe policy.

Validate every component first. Allow **one** trailing empty component only after the rest are safe. Treat it as a folder marker **only if listed `Size` is 0**. A nonzero trailing-slash object cannot be a local file; that is an `[ERROR]` and aborts the command like other unsafe keys.

**3. Split resolve vs create; drop the 404-must-not-mkdir claim**  
Dry-run and existing-file verify need a canonical root without creating it.

- `resolve_download_root(create=False)` — non-mutating, after key preflight.
- `ensure_download_root()` — mutating, **once**, after preflight is clean and this run will actually download.

A listed object that 404s at `head_object` is an operational race, not F-01. Do not add lazy/synchronized mkdir for that. After a clean preflight, creating the root before workers is allowed.

**4. Do not use `download_file()` on a `mkstemp` path**  
s3transfer’s filename path writes to **its own** temp and `rename`s onto the supplied name. That replaces the `0o600` inode, so the plan’s permission claim is false. Boto is unpinned here.

Use `download_fileobj()` on the still-open `mkstemp` fd (`os.fdopen(fd, "wb")` → download → close → verify temp → `os.replace` if dest still absent → `finally` unlink). Assert `0o600` on POSIX only; do not assume POSIX modes on Windows.

**5. Collision identity is `NFD(casefold(NFD(component)))`**  
Unicode caseless match is NFD, then casefold, then NFD again — not NFC then `casefold()`. Keep on-disk names as the original S3 spelling. `README` vs `readme` aborting on Linux remains an acceptable fail-closed false positive.

## Device names — accept the compact table, still reject the encyclopedia

CPython’s set is small and is the right model:

- `CON`, `PRN`, `AUX`, `NUL`, `CONIN$`, `CONOUT$`
- `COM1`–`COM9` and `COM¹` `COM²` `COM³` (`\xb9\xb2\xb3`)
- same for `LPT`

Match like CPython: portion before the first `.`, `rstrip` spaces, case-insensitive compare (`NUL.txt`, `NUL .txt`). That is not the full Microsoft naming doc. Still out of scope: 8.3 mangling, `\\.\`, and every historical wildcard rule beyond the chars already rejected.

## Small improvements — accept all four

- Print hostile keys with `repr()` so ESC/newline cannot twist the terminal.
- Direct `download_worker()` must not invent a silent no-op: command_download never queues markers; if the worker is called with one, return a distinct skip (e.g. category `"skipped"`). Do not overload `"verified"` / `"downloaded"`.
- Command summary: skipped-marker count so totals make sense.
- Drop “Use `os.sep`”. `commonpath([root, dest]) == root` and `dest != root` is the containment check; `commonpath` is already component-aware.

## Still reject

| Item | Why |
|---|---|
| POSIX `dir_fd` / local TOCTOU | Hostile S3 keys, not a racing local user. Docs should say the dest tree must not be writable by an untrusted local user. |
| Abort siblings on 404 / I/O / ETag / `[DIFFERENT]` | Operational, per-key. Review 1 did not ask for this; keep the distinction. |
| Live APFS probing | Pure NFC/NFD+casefold identity is enough once issue 5 is fixed. |

**Bottom line:** Review 2 is correct. Fold in 1–5, `repr`/skip-count/worker skip/`commonpath` wording, and CPython’s reserved-name table. After that the plan is ready to implement. No files changed.
