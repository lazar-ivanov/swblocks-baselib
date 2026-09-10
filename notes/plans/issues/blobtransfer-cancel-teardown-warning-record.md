# `utf_baselib_blobtransfer` cancel teardown warning: the unpackager discards its staging directory with output files still open

**Found:** 2026-09-09, diagnosing the intermittent Windows failure of
`BlobTransfer_FilesPackagerInMemoryCancelUploadTests` (Change 3 of
`windows-path-normalization-and-flaky-tests-plan.md`). Fix plan, captures and verification:
`windows-blobtransfer-cancel-handle-and-http-reset-flakes-plan.md`, section A.

**Status:** **fixed in the working tree, 2026-09-09** (production code,
`src/include/baselib/transfer/FilesUnpackagerUnit.h`); verification results in the plan document.

**Since:** `be529b7` gave the fixture its first file which spans more than one data block
(`foo/bar/multiChunkFile.bin`, 2 MB + 12345 B, `TestFsUtils.h:430`), which is the first fixture on
which the test can show it. The defect itself is older: any download which is cancelled or fails
while a multi-chunk file is between two chunks.

---

## What happens

The cancel-upload case arms a timer which calls `eqTopLevel -> cancelAll( false )` on a growing
delay ladder (`TestBlobTransferUtils.h:1503-1557`); in the later iterations the cancel lands in
the **download** phase. On Windows, roughly one run in three to five, the run then logs

```
WARNING: Could not delete path "C:\Users\lazar\AppData\Local\Temp\.<uuid>" due to the following
reason: 'The process cannot access the file because it is being used by another process' ;
error code value: 32
```

and `UtfMain.h:120-126` turns the warning into a case failure.

**The path is the unpackager's staging directory, not a test temporary directory.** A
`fs::TmpDir` is named `%TEMP%\.bl-temp-dir-<uuid>` (the test logs it: "Files upload test pushing
to blob storage for path ..."); a bare `%TEMP%\.<uuid>` is `FilesUnpackagerUnit::m_targetTmpDir`,
created as `targetDir.parent_path() / uuid` and made hidden (`FilesUnpackagerUnit.h:1054-1059`),
and the warning is emitted by the unit's own `fs::safeDeletePathNothrow( m_targetTmpDir )` in the
failure branch of `flushAllPendingTasks()` (`:1514-1518`).

## The holder

`entry_obj_t::filePtr` (`FilesUnpackagerUnit.h:104`) of an entry whose chunks have not all arrived.
The output file is opened when the first chunk is written and **kept open across chunks**
(`:576-579`); it is closed by the writer task when the entry completes (`:504`), or by
`onTaskStoppedNothrow` when a *running* writer task is stopped (`:945-948`). An entry which is
merely waiting for its next chunk has no running task, so its handle stays open, and once the
unit's writers have flushed nothing but the unit can close it. `flushAllPendingTasks()` did not:

- on a **failure** its failure branch deleted the staging tree without closing the handles
  (unlike the incomplete-content branch a few lines below, `:1584-1589`, which closed every
  in-progress entry first);
- on a **stop** — which is what a cancelled pipeline delivers, and after which the unit
  completes *without an error* (the failing logs show "Task 'success:Files_Unpackager' completed
  successfully" right before the warning) — neither branch runs at all, the handles stay open,
  and the pipeline driver deletes the staging tree itself while the unit is still alive
  (`TestBlobTransferUtils.h:1272-1281`: "The pipeline has failed, but the unpackager itself did
  not. We need to cleanup the temporary directory it has created").

On Windows the open handle (no delete sharing) makes that deletion fail with
`ERROR_SHARING_VIOLATION` after the 20 × 100 ms retry of `trySafeRemove` (`FsUtils.h:55-91`); the
handles are released only when the unit object is destroyed, which is why every leftover tree
deletes normally after the process exits and no object leak is reported. The stop path is the
common one in the two cancel cases; a first version of the fix which only covered the failure
branch left the failure rate unchanged (upload 5/20, 1/10, 2/10; download 4/5, 2/5, 2/5).

## Why intermittent

The cancel has to land during the download while the three-chunk file is between two chunks —
a few hundred milliseconds of a run of about a second — and the timer ladder only reaches the
download phase in the later iterations. Captures on the unmodified `win-a64-vc143-debug` binary:
3 failures in 20 solo runs; with only the packager changed (see below) the rate was unchanged
(`win-x64-ccl16-debug` 1/10 then 5/20, `win-a64-vc143-release` 2/10, `win-x86-vc143-release`
1/10), which is what exposed the wrong attribution.

## The wrong turn, kept on record

The first trace attributed the held file to the **packager**: `BlockReaderTaskT` kept the input
file open across the blocks of a multi-block file and, when cancelled, completed with the file
still open and `hasMoreBlocks()` still true, so the unit recycled or retained it. That is a real
latent defect in the code and it has been fixed as well (below), but it was never the source of
this warning: the input tree is a `TmpDir`, named `.bl-temp-dir-<uuid>`, and no failing warning
ever named one. The leftover-tree signature (`foo/bar/multiChunkFile.bin` first surviving file)
fit both stories, because the unpackager's staging tree mirrors the input tree; the directory
name is what tells them apart.

## The fix

`FilesUnpackagerUnit::flushAllPendingTasks()`: as soon as the writers have flushed (`flushed` is
true — every worker task is done, so nothing else touches the handles), close the `filePtr` of
every entry in `m_entriesInProgress`, *before* deciding between the failure branch (which then
discards the staging directory) and the other outcomes. The loop the incomplete-content branch
used to run for the same purpose is folded into it. Whoever deletes the staging tree afterwards —
the unit on a failure, the pipeline owner after a stop — finds no open file.

Coverage (`utf_baselib_blobtransfer`), both deterministic, both driving a standalone unpackager
with one 64 KB chunk of a three-chunk file so its output file is open and waiting for chunk two:

- `BlobTransfer_UnpackagerFailureClosesOpenFilesBeforeDiscardingStagingTests` fails the unit
  through its input connector's `onError` and requires the staging directory to be gone;
- `BlobTransfer_UnpackagerStopClosesOpenFilesTests` stops the unit (`requestCancel`), lets it
  finish while keeping the unit object alive, and requires the staging tree to delete on the
  first attempt (`fs::unsafe::remove_all`, no retry) — on Windows exactly the deletion which
  failed in the pipeline driver.

## Packager hardening done on the way (separately reviewable)

`BlockReaderTaskT` (`FilesPackagerUnit.h`) no longer holds the input file between blocks: each
block is read with a local handle (open, seek to `m_filePos`, read, close), and a reader which
observes a cancellation in `onExecute()` releases its data block and marks itself abandoned so
the unit neither offers its stale block downstream nor re-schedules it. Pinned by
`BlobTransfer_PackagerCancelledBlockReaderReleasesFileTests`. This removes the latent hold
described above; it did not change the observed failure rate, which is the evidence that the
warning came from the unpackager.

## Evidence log

- 2026-09-09: mechanism traced (packager attribution), captures on `win-a64-vc143-debug`
  (unmodified): 3/20, error 32, `.<uuid>` trees, `multiChunkFile.bin` first, deletable after exit,
  no leak at exit; `win-x64-ccl16-debug` (unmodified): 0/20.
- 2026-09-09: packager-only fix — rate unchanged (x64 debug 1/10 and 5/20, a64 release 2/10,
  x86 release 1/10); the warning paths re-read against the logged input roots → unpackager
  staging directory; attribution corrected, unpackager fix applied.
- 2026-09-09, first (failure-branch-only) unpackager fix: rate unchanged — cancel-upload
  5/20 (`win-x64-ccl16-debug`), 1/10 (`win-a64-vc143-release`), 2/10 (`win-x86-vc143-release`);
  cancel-download 4/5, 2/5, 2/5. The failing logs show the unit completing *successfully* before
  the warning → the stop path, deleted by the driver.
- 2026-09-09, **complete fix (close on flush)** on the same three flavours: cancel-upload
  **0/20, 0/10, 0/10**; cancel-download **0/5, 0/5, 0/5**; the three new cases pass on all three
  (`BlobTransfer_UnpackagerStopClosesOpenFilesTests` 9/9,
  `BlobTransfer_UnpackagerFailureClosesOpenFilesBeforeDiscardingStagingTests` 9/9,
  `BlobTransfer_PackagerCancelledBlockReaderReleasesFileTests` 21/21). Against the unfixed header
  on `win-x64-ccl16-debug`, `BlobTransfer_UnpackagerStopClosesOpenFilesTests` **fails** at
  `UTF_REQUIRE( ! ec )` (the staging tree cannot be removed while the unit is alive) and passes
  with the fix; the failure-branch case passes either way and is kept as a guard.
- 2026-09-09, `win-a64-vc143-debug` (the flavour of the original observation, outside the three
  build flavours of the plan) rebuilt from `215b891`: cancel-upload **0/20** with the fix (3/20
  before it, above).
