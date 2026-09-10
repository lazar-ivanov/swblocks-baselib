# Windows `utf_baselib_blobtransfer`: `ERROR_SHARING_VIOLATION` deleting a test `TmpDir` and a server chunk file — Deferral Record

This document records two Windows-only `utf_baselib_blobtransfer` failures seen during the full
12-combo Windows matrix run of 2026-09-10, why neither was fixed at the time, and what a fix would
have to consider. **It is a risk acceptance, not an assessment that the concern is absent:** the
second of the two is a production path that takes the blob server down, and it remains in the tree.

**Found:** 2026-09-10, validating the full Windows matrix (`ARCH` a64/x64/x86 × `TOOLCHAIN`
vc143/ccl16 × `VARIANT` debug/release) of all 19 test modules on branch `lazari2`, at `01562e0`
plus the x86 build fixes committed as `c17cf2c`.

**Status:** **Deferred, not reproduced at HEAD.** Both failures were observed once each during the
matrix run. Nine subsequent targeted runs at HEAD (after pulling `79e1f74`) have not reproduced
either. See "Reproduction attempts" — the failure to reproduce is itself unexplained and is the main
reason this is a record rather than a fix.

**Platform:** Windows only, and in practice `ARCH=x86` only. Nothing here can occur on POSIX.

**Related:** `windows-blobtransfer-cancel-handle-and-http-reset-flakes-plan.md` (same fixture, the
staging-directory variant, **fixed** in `215b891`), `blobtransfer-cancel-teardown-warning-record.md`
(the full account of that fix), `linux-blobtransfer-reauth-rearm-and-concurrent-entry-loss-plan.md`
(the Linux exception-ordering race; its R-5 "Windows and macOS not yet run" is partly discharged by
the matrix run that found these two).

---

## The problem in plain terms

On Windows you cannot delete a file while something still has it open. On Linux you can — `unlink`
succeeds and the file disappears once the last handle closes. That single difference is the whole
issue.

The blobtransfer tests write files and then delete them. If anything still holds a file open when
the delete runs, Windows returns error 32, "The process cannot access the file because it is being
used by another process". The code already expects this: it retries the delete 20 times, 100 ms
apart, for a total of **2 seconds**. Normally whatever held the file lets go well inside that
window.

Under heavy load the machine is slow enough that the holder does not let go in time, the 2 seconds
run out, and the delete fails for real. What happens next depends on who was deleting:

- **A test cleaning up its temporary directory** logs a warning. The test harness turns any warning
  into a test failure, so the test fails even though nothing it was actually testing went wrong.
- **The blob server deleting a stored chunk** treats the error as fatal, shuts the server down, and
  the client's connection drops. The test then fails with a connection error that looks nothing like
  a file-deletion problem. This one is production code.

Both are timing-dependent, and both need a machine slow enough to blow through the 2-second window.
That is why they showed up on x86 — those binaries run under emulation on this ARM64 host, so they
are markedly slower than the a64 and x64 ones — and only when five test modules were running at once.

---

## Decision

**Date:** 2026-09-10
**Status:** **Deferred.** Recorded, not fixed. Neither failure blocks the matrix today; both are
real.

| # | Item | Disposition |
|---|---|---|
| 1 | F1 — test `TmpDir` cleanup fails with error 32, warning becomes a test failure | **Deferred** |
| 2 | F2 — `DataChunkStorageFilesystem::remove()` rethrows error 32, blob server shuts down | **Deferred** — the production one; the more serious of the two |
| 3 | Identify which handle was still open in each case | **Not done** — needs a reproduction; see "Reproduction attempts" |
| 4 | Widen the 2 s retry window (owner's suggestion, 4–5 s) | **Deferred, undecided** — see "Options" |

---

## The two observed failures

Both from the matrix run of 2026-09-10. Both name a path under `bld/<tree>/tmp/.bl-temp-dir-<uuid>`,
which is an `fs::TmpDir` created by the test — **not** the unpackager's `%TEMP%\.<uuid>` staging
directory that `215b891` fixed. Both carry Windows error code 32.

### F1 — test `TmpDir` cleanup (`x86-ccl16-debug`)

```
UtfMain.h(125): error: in "BlobTransfer_FilesPackagerInMemoryCancelDownloadTests":
WARNING: Could not delete path
"...\bld\win-x86-ccl16-debug\tmp\.bl-temp-dir-631bfb00-cb74-47a1-a5ab-1abf01249f84"
due to the following reason: 'The process cannot access the file because it is being used by
another process' ; error code value: 32
```

The other 17 modules in that combo passed; this was the only failure.

**Note the path names the tree root, not the locked file.** `safeDeletePathNothrow`
(`FsUtils.h:570`) warns with the path it was handed, while `trySafeRemoveAll` (`FsUtils.h:405`)
recurses and returns on the *first* failure. So F1 tells us something under that tree was open; it
does not say what. Identifying the file needs the leftover tree from a failing run (item 3 above).

### F2 — server chunk delete (`x86-vc143-debug`)

```
Fatal server exception has occurred (server will initiate shutdown):
FsUtils.h(143): Throw in function ...chkDeleteInternal(...)
std::exception::what: Could not delete
"...\bld\win-x86-vc143-debug\tmp\.bl-temp-dir-a1cb7868-...\chunks\0d87e131-6eb1-443a-bbd7-9307e175ff9a":
The process cannot access the file because it is being used by another process [system:32]
ControlCode='6'; ChunkId in control buffer is '0d87e131-...'; ChunkId in connection object is 'bcf2ea41-...'
```

which cascaded to

```
TcpBlockTransferClient.h(522): System error ... An established connection was aborted by the
software in your host machine [system:10053]
ChunksSendRecvBase.h(393): ServerNoConnectionException: An error has occurred while trying to
connect to a blob server node
UtfMain.h(147): error: in "BlobTransfer_FilesPackagerInMemoryTests": Unhandled exception was caught
```

Failing test case `BlobTransfer_FilesPackagerInMemoryTests`; the failure landed in the "deleter
tests" phase of the fixture (`TestBlobTransferUtils.h:1356`). Unlike F1 this names the **specific
file** — a chunk in the blob server's storage root, which in this fixture is itself a test `TmpDir`.

### Matrix distribution

| Arch | vc143-debug | vc143-release | ccl16-debug | ccl16-release |
|---|---|---|---|---|
| a64 | pass | pass | pass | pass |
| x64 | pass | pass | pass | pass |
| x86 | **F2** | pass | **F1** | pass |

Two failures in twelve combos, both x86, both debug, in different test cases and by different
mechanisms. The a64 and x64 combos ran under the same `-j5` concurrency and never failed.

---

## Mechanism

### Why Windows only

`os::fopen` on Windows is `::_wfopen` (`OSImplWindows.h:1839`) with the caller's mode string. The
CRT opens with share-read/share-write but **not** `FILE_SHARE_DELETE`, so any live `FILE*` on a file
blocks deleting that file *and every ancestor directory*. POSIX `unlink` is unaffected by open
descriptors, so none of this can happen on Linux or macOS.

This is a known property of the codebase, recorded before: see
`blobtransfer-cancel-teardown-warning-record.md`, which contrasts `_wfopen` with the `CreateFileW`
call sites that do pass delete-sharing.

### The retry window

`trySafeFileOperation` (`FsUtils.h:60`) retries the operation `getRetryCount()` times,
`DEFAULT_TIMEOUT_IN_MILLISECONDS` apart:

- `getRetryCount()` (`FsUtils.h:55`) returns **20 on Windows**, 1 elsewhere
- `DEFAULT_TIMEOUT_IN_MILLISECONDS` (`FsUtils.h:52`) is **100**

so the effective window is **2 seconds** on Windows. The comment there attributes the need to
anti-virus software holding freshly written files — which remains a live candidate here and has not
been excluded.

Both failures are what it looks like when that window is exhausted: `trySafeRemove` returns the
error code, and `chkDeleteInternal` (`FsUtils.h:133`) turns it into a thrown `bl::SystemException`.

### F1 escalation — warning becomes a test failure

`safeDeletePathNothrow` (`FsUtils.h:570`) is deliberately best-effort and `NOEXCEPT`: it returns
`false` and logs a `Logging::warning()` rather than throwing. That is a reasonable contract for
cleanup.

`UtfMain.h:124-126` routes **every** `LL_WARNING` through `UTF_ERROR_MESSAGE`, so any best-effort
cleanup that fails transiently fails the enclosing test case. The tension is structural: one side
is deliberately tolerant, the other deliberately strict.

### F2 escalation — transient error becomes a server shutdown

`DataChunkStorageFilesystem::remove()` (`DataChunkStorageFilesystem.h:289`) calls
`fs::safeRemove( chunkPath )` (`:307`) and catches:

```cpp
catch( std::exception& )
{
    if( ! fs::path_exists( chunkPath ) )
    {
        base_type::throwChunkDoesNotExist( chunkId );   // benign: ServerErrorException
    }
    throw;                                              // everything else propagates
}
```

The catch tolerates exactly one race — another client deleted the chunk first — and converts it into
`ServerErrorException` (`:109`), which the server reports to the client without dying. A sharing
violation leaves the file **present**, so `path_exists` is true and the exception is rethrown
unchanged.

`chk4ServerErrors` (`TcpBlockTransferServer.h:255`) then classifies it:

- `ServerErrorException` → non-fatal, reported to the client
- `eh::system_error` → `m_isFatalServerError = ( asio::error::operation_aborted != errorCode )`
  (`:325`) → **fatal**
- anything else → fatal

`bl::SystemException` is an `eh::system_error` carrying `system:32`, so it takes the fatal branch and
the node shuts down.

**This is deliberate, and defensible in production.** The comment at `:259-267` says so: other
exceptions mean "something is wrong with the service or some other fatal situation which will require
to take down the server so the client can re-connect to a different one in the case the error
situation is transient and / or node specific". With several blob nodes that is a sound policy. The
test fixture has one node, so it is terminal there.

Whether a Windows sharing violation *should* be in that bucket is the open design question. It is
transient and node-specific — exactly what the comment describes — but it is also not a signal that
the node is unhealthy, and the delete simply did not happen.

---

## Reproduction attempts

All at HEAD after pulling `79e1f74` (which added the Linux `InputConnector.h` fix) and rebuilding
`utf_baselib_blobtransfer` in both x86 debug trees. **Neither failure has reproduced.**

| Attempt | Condition | Result |
|---|---|---|
| 5 × single case | `BlobTransfer_FilesPackagerInMemoryTests`, 4 CPU busy-loops alongside | 5/5 pass, 0 sharing violations, ~90 s per run |
| 3 × five modules | `make -k -j5` over blobtransfer, messaging, io, http, tasks | 2 of 3 complete, both pass, 0 sharing violations, ~1235 s per iteration |

**The synthetic-load attempt was a poor model and should not be repeated as-is.** Those runs took
~90 s where the same case took **470 s** during the failing matrix run — four busy-loops on two cores
are roughly five times weaker than five concurrent test modules doing real file and socket I/O. For
a race whose window is "does a handle stay open longer than 2 s", contention profile matters more
than raw CPU pressure.

**A methodology note for whoever picks this up.** The first capture harness reported 5/5 failures
that were really passes: with a `--run_test=` filter Boost.Test prints `Test module ... has passed
with`, not `*** No errors detected`, and the classifier only matched the latter. Check the verdict
string against a filtered run before trusting any pass/fail loop.

**What has not been tried**, in rough order of expected value:

1. A full 18-module `make -k -j5 testutf` on `x86-vc143-debug`, which is the exact condition that
   produced F2 — the 5-module subset may simply not load the machine enough.
2. The same on `x86-ccl16-debug` for F1.
3. Running with the machine otherwise busy (a parallel build in another tree), to push past what
   `-j5` alone achieves.
4. Excluding anti-virus as the holder: exclude the `bld` tree from Windows Defender and see whether
   the rate changes. Cheap, and it splits the candidate holders in half.

---

## Why these are not the failures already fixed

`215b891` (2026-09-09) fixed a closely related Windows failure in the same fixture, and it is worth
being precise about the difference so this is not closed as a duplicate.

- That fix was for the **unpackager's staging directory**, `%TEMP%\.<uuid>` — created as
  `targetDir.parent_path() / uuid` (`FilesUnpackagerUnit.h:1054-1059`) — discarded with output files
  still open. The record for it states that a test `fs::TmpDir` (`%TEMP%\.bl-temp-dir-<uuid>`) "was
  never named" in any observed failure of that class.
- **Both failures here name `.bl-temp-dir-<uuid>`**, a test `TmpDir`.
- F2 is not a cleanup path at all: it is the blob server's own `remove()` on a chunk it stores, and
  it escalates to a server shutdown rather than a warning.

The staging directory in this fixture is a **sibling** of the download target, not nested inside it:
the target *is* a test `TmpDir` (the test logs "Files download test pushing from blob storage to path
...\.bl-temp-dir-<uuid>"), so `parent_path()` is `bld/<tree>/tmp`. A failure naming the test `TmpDir`
therefore points at a file inside that tree, not at the staging directory.

---

## Options when this is picked up

Not mutually exclusive; (1) is a mitigation, (2)–(4) are fixes for different parts.

### 1. Widen the retry window — *the owner's suggestion, undecided*

Raise the Windows retry budget from 2 s to 4–5 s: either `getRetryCount()` from 20 to 40–50, or
`DEFAULT_TIMEOUT_IN_MILLISECONDS` from 100 to 200–250 (`FsUtils.h:52-58`).

- **For:** one-line change, no lifetime reasoning, and it addresses both failures at once. If the
  holder is anti-virus, it is the *only* one of these options that helps.
- **Against:** it is a mitigation, not a fix — it moves the threshold rather than removing the race,
  and a slower machine or a bigger fixture walks back into it. It also doubles the worst-case latency
  of every genuinely failing delete on Windows, on every code path that uses these helpers, which is
  a global cost paid for a local problem.
- **Note:** `getRetryCount()` is shared by *every* `trySafeFileOperation` caller, not just deletes.
  If the window is widened, consider whether it should be widened globally or only for the delete
  paths.

### 2. Make F2 non-fatal

Have `DataChunkStorageFilesystem::remove()` classify a sharing violation as a reportable
`ServerErrorException` rather than letting it reach the fatal branch — the chunk still exists and was
not deleted, so it must **not** be reported as "chunk does not exist"; it needs its own error, or a
retryable one.

- **For:** a transient, node-local file lock is a poor reason to take a blob node down, and this is
  the only one of the failures on a production path.
- **Against:** changes a deliberate server policy (`TcpBlockTransferServer.h:259-267`); the client
  contract for a delete that did not happen needs deciding, and the caller may have no better
  recourse than the node restart it gets today.

### 3. Open chunk files with delete-sharing

Give the chunk-storage `os::fopen` calls (`DataChunkStorageFilesystem.h:255`, `:281`) Windows
delete-sharing semantics — `CreateFileW` with `FILE_SHARE_DELETE` plus `_open_osfhandle`, or a new
`os::fopenShared`.

- **For:** removes the root cause for these files; POSIX behaviour on Windows.
- **Against:** the prior plan explicitly declined this as a general change — "wider change, and it
  would only hide the lifetime bug" (`windows-blobtransfer-cancel-handle-and-http-reset-flakes-plan.md`,
  A.3 item 3). `os::fopen` is cross-platform API whose UNIX twin has no equivalent flag, so this
  should be a *new* narrow entry point, never a change to `os::fopen` itself.

### 4. Find and fix the actual holder

The approach `215b891` took: identify the handle and close it earlier. Requires item 3 of the
decision table first — a reproduction with the leftover tree captured.

- **For:** the only option that removes the defect rather than widening a window around it.
- **Against:** blocked on a reproduction nobody has yet achieved at HEAD.

### Not recommended

Suppressing the cleanup warning in the test harness, or exempting `safeDeletePathNothrow` from the
warning-to-failure rule. The prior plan rejected this for the same fixture — "the warning is a real
report". If the strictness of `UtfMain.h:124-126` is revisited it should be as a deliberate decision
about best-effort cleanup reporting, not as a way to quiet this failure.

---

## Conditions to revisit

- **Either failure recurs**, on any combo. Capture the leftover `bld/<tree>/tmp/.bl-temp-dir-*` tree
  before anything deletes it — that names the locked file and is the missing datum for every option
  above.
- **The x86 matrix is used as a gate.** Two failures in twelve combos is tolerable for an
  investigation run and not tolerable for a gate.
- **A blob server node is seen shutting down in production** with `system:32` in the log. F2 is a
  production path, and outside the test fixture it manifests as a node restart, not a test failure.
- **The blobtransfer fixture grows**, particularly if more or larger multi-chunk files are added.
  The existing multi-chunk file is what made the sibling defect observable at all (`be529b7`), and
  more of them widen the window in which handles are held.
- **Anyone touches `getRetryCount()` or `DEFAULT_TIMEOUT_IN_MILLISECONDS`** for another reason —
  option 1 costs nothing extra at that point.

---

## Records to update when it lands

- This document: replace the Decision table dispositions with the outcome, in the shape the sibling
  records use.
- `windows-blobtransfer-cancel-handle-and-http-reset-flakes-plan.md`: section A, if the holder here
  turns out to be one it already describes.
- `linux-blobtransfer-reauth-rearm-and-concurrent-entry-loss-plan.md`: R-5, which asks for Windows
  and macOS runs; the matrix run of 2026-09-10 covers the Windows half for `utf_baselib_blobtransfer`
  and `utf_baselib_tasks` and should be recorded there either way.
