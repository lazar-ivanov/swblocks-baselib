# The two Windows flakes left open by the path-normalization plan: mechanism, diagnosis and fix plan

**Date:** 2026-09-09
**Status:** **Approved 2026-09-09; implemented 2026-09-09** (the first part committed as
`215b891`, the rest in the working tree). Part A (blobtransfer) is fixed and verified — the cause
turned out to be the unpackager, not the packager, see the correction in section A. Part B (HTTP)
was **root-caused and fixed later the same day**: candidate 2, with the twist that the
`MachineGlobalTestLock` which should have prevented it has excluded nothing on Windows since
`6db5ec1` — see the end of the implementation log and the HTTP record.
**Found:** the two intermittent failures recorded by
`windows-path-normalization-and-flaky-tests-plan.md` (Change 3 and its HTTP record) on the devenv7
Windows host, branch `lazari2`. Their mechanisms below come from a read-only trace of the code by
Fable 5.1 on 2026-09-09; nothing had been run at the time of writing.
**Scope decisions taken by the owner, 2026-09-09:** implement this plan; build only three
flavours — `ARCH=a64 TOOLCHAIN=vc143 VARIANT=release`, `ARCH=x64 TOOLCHAIN=ccl16 VARIANT=debug`,
`ARCH=x86 TOOLCHAIN=vc143 VARIANT=release` — not the full matrix; run focused tests of the affected
modules only, never the whole suite.
**Related:** `utf-baselib-http-windows-intermittent-failures-record.md` (the HTTP record this plan
corrects), `windows-path-normalization-and-flaky-tests-plan.md` (Change 3),
`windows-path-normalization-and-flaky-tests-plan-fable51-review.md` (finding 6 is corrected here),
`blobtransfer-utest-object-leak-record.md` (the earlier retention defect in the same fixture).

---

## Context

Opus implemented Changes 1, 2 and 4 of the path-normalization plan, ran the modules, and noted that
`utf_baselib_blobtransfer` and `utf_baselib_http` passed that run but "both are ~20-33% and simply
didn't hit". A green run says nothing about a failure that hits one run in three to five; per the
amended plan the HTTP one stayed recorded with root cause unknown and the blobtransfer cleanup
warning stayed in the diagnose-first state (no warning suppression was added).

Correction carried from the review: the HTTP run was **52 passed, 4 failed**, not the reverse. The
four failing assertions (from the record): `BaseLib_HttpServerPerfTest` at
`HttpServerHelpers.h:262` (status mismatch in the *sequential* block); the **positive controls** of
`TlsHandshake_NameMismatchIsReportedThroughErrorInfo` (`:349`) and
`TlsHandshake_AllowUntrustedRecordsAndClearsEndpointInfo` (`:530`) — a plain HTTPS GET to
`localhost` against a correctly configured server had to *succeed* and did not; and
`TlsHandshake_SniOmittedForAddressLiterals` (`:649`, `UTF_REQUIRE( ! acceptEc )` on a raw acceptor).

## Do they need fixing?

Yes, both — as diagnosed defects, not as flaky tests to be quieted:

- A 20-33% per-run failure rate on a debug build makes the Windows debug matrix useless as a gate.
- The blobtransfer one is a **production** lifetime defect in the packager: a cancelled read keeps
  the input file open until the unit object dies.
- The HTTP one is not yet root-caused, but the code has two genuine Windows socket-close defects on
  production paths (the server resets peers that are still sending; the client fails a completed
  request on a reset during TLS shutdown) plus a port-sharing hazard, and all three must be
  settled either way.

---

## A. Blobtransfer teardown warning

> **Correction (2026-09-09, during implementation).** The mechanism below (A.1) attributed the
> warning to the packager's block reader holding the *input* file. The captures refuted that:
> every failing warning names `%TEMP%\.<uuid>`, which is the **unpackager's staging directory**
> (`FilesUnpackagerUnit.h:1054-1059`); a test `TmpDir` is `%TEMP%\.bl-temp-dir-<uuid>` (the
> input roots are logged by the test) and none was ever named. The warning is emitted by the
> unpackager's own `safeDeletePathNothrow( m_targetTmpDir )` in the failure branch of
> `flushAllPendingTasks()` (`:1514-1518`), which discarded the staging tree without closing the
> output files of entries whose chunks had not all arrived — the multi-chunk file's handle is kept
> open between chunks (`:576-579`) — unlike the incomplete-content branch (`:1584-1589`). The
> packager-only fix left the failure rate unchanged (x64 debug 1/10 and 5/20, a64 release 2/10,
> x86 release 1/10), which is what exposed the error. The fix applied is in the unpackager
> (close the in-progress handles before the deletion); the packager change is kept as a separate
> hardening of a real latent defect. Full account: `blobtransfer-cancel-teardown-warning-record.md`.

### A.1 Mechanism (from the code — the packager part; superseded as the cause, see above)

**Symptom.** `BlobTransfer_FilesPackagerInMemoryCancelUploadTests` fails when `TmpDir::~TmpDirT`
→ `safeDeletePathNothrow` (`FsUtils.h:570-608`, `:1377-1380`) logs a WARNING, which
`UtfMain.h:120-126` turns into a case failure. `trySafeRemove` retries 20 × 100 ms first
(`FsUtils.h:55-91`, `:126-131`), so something held a handle for more than two seconds after the
cancel.

**The holder.** `BlockReaderTaskT::m_filePtr`
(`src/include/baselib/transfer/FilesPackagerUnit.h:241`), opened with `os::fopen` (`:271-274`) =
`::_wfopen` on Windows (`OSImplWindows.h:1839`, no `FILE_SHARE_DELETE` — unlike the `CreateFileW`
sites at `:1973`/`:2804`), and closed **only** when the last byte of the file has been read
(`:312-320`) or in the destructor. An open `FILE*` blocks deleting the file and every ancestor
directory on Windows; POSIX unlinks regardless — hence Windows-only.

**Why cancel does not close it.** `onExecute` (`:323-340`) does
`if( false == isCanceled() ) doExecute();` — a cancelled reader completes *successfully* with
`m_filePos` unchanged, so `hasMoreBlocks()` (`:344-347`) stays true and `m_filePtr` stays open.
`processTopReadyTask` then re-attaches a block and **re-pushes** it (`:501-517`), and the unit's
worker queue is `OptionKeepAll` (`FixedWorkerPoolUnitBase.h:130-132`), so a completed reader is
retained in the ready list rather than dropped. The unit's own shutdown path only drains the queue
when the unit is *failing*: `chk2LoopUntilShutdownFinished` gates its `cancelAll( false )` on
`isFailedOrFailing()` (`:149-161`), and `flushAllPendingTasks` passes `isFailedOrFailing()` as
`force` (`FilesPkgUnpkgBase.h:231-240`), so on a plain stop the reader is recycled with the file
open until the packager unit object itself is destroyed and `~ObjPtrDisposable` disposes
`m_eqWorkerTasks`. The unpackager, by contrast, closes its per-file handles explicitly on the
cancel path (`FilesUnpackagerUnit.h:1584-1587`).

**Why only sometimes, and since when.** The only fixture file larger than one 1 MB data block is
`foo/bar/multiChunkFile.bin` (2 MB + 12345 B, `TestFsUtils.h:430`), added in `be529b7` ("give the
blob transfer fixture real multi-chunk content"). It is the only file whose `FILE*` is held across
reschedules, so the cancel has to land inside that file's few-hundred-millisecond window —
consistent with 20-33%. The flake is therefore not older than `be529b7`; it is not a `79488fa`
regression, but it is recent.

**Not yet pinned down.** What keeps the packager unit alive for more than two seconds after the
test lambda unwinds (the unit graph hangs off the scanner local; strong edges run
observable → subscriber only, `ProcessingUnit.h:90`, `ObservableBase.h:1277-1279`). Candidates:
the unit task completing its shutdown loop early (`FanoutTasksObservable.h:131-138`) while
`m_eqWorkerTasks` still holds the reader (the queue ↔ task cycle through `SimpleTaskBase::m_eq`,
`TaskBase.h:1320/1335`, is only broken by `dispose()`), or a `ScanDirectoryTask` still iterating
after the scanner's `cancelAll( false )` (`FanoutTasksObservable.h:151-154`; that would name a
*directory* in the warning, not the file). The fix removes the dependence on that retention either
way.

### A.2 Confirming capture (no code)

1. Run the single case on a debug flavour with stdout captured to a file until it fails. The
   warning text names the path and the error code: **32 (`ERROR_SHARING_VIOLATION`) on
   `...\foo\bar\multiChunkFile.bin`** confirms the reader holder; 32 or 5 on a *directory* points
   at the scanner iterator (or at the file seen through its parent — check which was deleted
   first; `trySafeRemoveAll` returns on the first failure).
2. After the failing run exits, list `%TEMP%\.bl-temp-dir-*` (hidden directories). The leftover
   tree shows the failing file and everything after it; if the directory can now be deleted by
   hand, the holder was in-process (handle released at exit), which rules out AV/indexer
   interference.
3. Check the exit line: `Utf.h:218-234` fails the process with
   `FATAL: N baselib object references leaked at exit` when objects leak; a failing run with only
   the TmpDir warning and no FATAL line means the unit was released later in the same run (a
   retention window, not a permanent leak).

### A.3 The packager hardening (written as "the fix" before the correction above; the fix for the observed warning is the unpackager change in the implementation log)

1. **A cancelled reader must release its file.** In `BlockReaderTaskT::onExecute`
   (`FilesPackagerUnit.h:323-340`), when `isCanceled()` is true, `m_filePtr.reset()` and mark the
   read abandoned so `hasMoreBlocks()` returns false (a cancelled flag consulted by
   `hasMoreBlocks()`). Do it on the worker thread inside `onExecute`, not in a `cancelTask()`
   override: `doExecute` may be inside `fread` on the worker thread when a cancel arrives, and
   closing the `FILE*` from another thread would race it.
2. **The unit must not recycle or retain a cancelled reader.** With `hasMoreBlocks()` false the
   re-push at `:501-517` is skipped and `waitForReadyWorkerTask` pops it
   (`FilesPkgUnpkgBase.h:185-195`). Additionally make the packager's shutdown drain its worker
   queue on a stop, not only on a failure: in
   `FixedWorkerPoolUnitBase::chk2LoopUntilShutdownFinished` (`:138-167`) the `cancelAll( false )`
   should run whenever the unit is stopping and the queue is non-empty, not only when
   `isFailedOrFailing()`. Both hunks are a few lines; one logic change, with the comment explaining
   the Windows sharing-mode consequence.
3. **Not doing:** opening input files with delete sharing (`CreateFileW` + `_open_osfhandle`) —
   wider change, and it would only hide the lifetime bug; and any `warningToDebugLineLogger` in
   the test — the warning is a real report.

Test coverage to add with the fix (test-only): a unit-level case that cancels a packager mid-file
and asserts the input file can be deleted immediately afterwards on Windows (the deletion is the
observable), so the property is pinned without relying on the pipeline race.

---

## B. HTTP resets

### B.1 What the code settles

**Failure semantics (why 10054 fails a case).** `BL_TASKS_HANDLER_CHK_EC` fails the task for any
error; `isExpectedException` only suppresses the log line (`TaskBase.h:139-156`). After the TLS
handshake has completed, `TcpSslSocketAsyncBaseT::isExpectedException` (`TcpSslBaseTasks.h:399-424`)
falls through to `TaskBase::isExpectedException` (`:948-959`) = false, so a reset during the
client's TLS **shutdown** also fails the task (`onShutdownCompleted`, `:553-608`, line 600) —
**even though the whole HTTP response has already been received**. The plain HTTP client completes
the body only on a clean `eof` (`SimpleHttpTask.h:832-835`); an RST that discards the client's
receive buffer (Windows behaviour) leaves `m_httpStatus == 0`, which is exactly the `:262`
signature for a 301/404/400 request (the 200 request would have failed at `:246` instead). A
client-side 10054 is logged only at DEBUG (`TaskBase.h:846-853`) and the TLS cases print the full
`diagnostic_information` at DEBUG (`TestTlsHandshakeVerification.h:265-272`) — the default level
*is* DEBUG (`UtfArgsParser.h:318`, `UtfMain.h:164/193`), so a captured stdout already contains the
phase, endpoint and code of every failure. The earlier runs did not keep stdout; that is the gap.

**Refuted candidates from the record:**

- Candidate 3, the 3 s handshake deadline: `TestHttpServer.h:2065` is inside
  `TimeoutHttpSslServerT`, instantiated only at `TestHttpServer.h:2629` in
  `BaseLib_HttpSslServerProtocolHandshakeTimeoutTest`. None of the four cases uses it; the stock
  default is 60 s (`TcpSslBaseTasks.h:73`) and `setProtocolTimeout` has no production caller.
- Candidate 1 as a *cascade*: `allowUntrustedCertificates`, `SuppressExpectedWarningsScope`,
  `RealVerifyCallbackScope` and the perf test's `LevelPusher` are all RAII and unwind on
  `UTF_REQUIRE` throws. The one non-RAII item — the manual seed of `localhost:28100` into the
  untrusted-endpoints map at `TestTlsHandshakeVerification.h:513-516` — is only removed by the
  success at `:530`, so it stays seeded for the rest of a failing run; nothing later reads it, so
  it is a hygiene defect, not a cause.
- Candidate 2 (warning amplification): the run reported exactly four failed assertions
  (985/989), so no extra `BOOST_ERROR` from a warning fired.
- Candidate 5 (`BL_ASSERT`): an assert aborts the process; not what was observed.
- Backlog/connection cap: `listen()` uses `SOMAXCONN`; the cap is hundreds to 4096; and the perf
  failure was in the sequential block (one connection at a time).
- `linger( false, 0 )` (`TcpBaseTasks.h:1206`, `:318`) is linger *off* — graceful close, not an
  abortive one; the two comments describing it as "closed immediately" are wrong.

**Live candidates** (each explains "success-requiring assertions fail with 10054, debug-only,
intermittent" differently; the capture separates them):

1. **Windows `shutdown( SD_BOTH )` racing a peer that is still sending.** The only close helper,
   `shutdownSocket` (`TcpBaseTasks.h:241-335`), does `shutdown( shutdown_both )` then `cancel`,
   with `close()` deferred to the destructor. On Windows `SD_RECEIVE` resets the connection if data
   is queued or arrives afterwards. On the server this runs after the TLS `async_shutdown` on the
   normal path but **without** it on the failed-handshake, cancel/idle-timeout, connection-cap and
   late-handshake paths (`TcpSslBaseTasks.h:627-635`, `:352-360`; `TcpBaseTasks.h:2434-2437`,
   `:1966-1978`); the client-side handshake retry destroys the stream with no shutdown at all
   (`TcpBaseTasks.h:1403`). Whether any of these is on the *success* path of the positive
   controls is what the capture must show (the server's `async_shutdown` waits for the client's
   close_notify, so the plain success path should be clean — a reset there points elsewhere).
2. **Port 28100 shared with a foreign listener.** Every server module defaults to 28100
   (`UtfArgsParser.h:46`); `utf_baselib_tasks`/`_io` bind `"localhost"`, which resolves to
   `[::1]:28100` on this host (`TestTaskUtils.h:838`), and the HTTP client tries `::1` **first**.
   If any other test process was listening there (a concurrently running module, or a leftover
   process — Windows `SO_REUSEADDR` at `TcpBaseTasks.h:1205` lets a second `0.0.0.0` bind succeed
   silently as well), the client connects to the wrong server and is reset — and the readiness
   probe (`TestTaskUtils.h:717-772`, same `localhost`) would *pass* against it. This is also the
   best fit for the SNI case: its raw IPv4 acceptor would then never see the connection and its
   30 s deadline fires, which yields `acceptEc == operation_aborted` — note `acceptCompleted` is
   set on that path too (`:613-638`), so `:648` passing does not exclude the timeout. The
   toolchain correlation would then be an artefact of how the two runs were scheduled.
3. **Server responding to an unparsable request and closing while request bytes are unread**
   (`HttpServer.h:183-235` throws on `PARSING_ERROR` before the body is consumed;
   `scheduleStdErrorResponse` `:536-574` sends the 400 and the connection then closes). The client
   writes the whole request in one `async_write` (`SimpleHttpTask.h:312`), so on loopback the bytes
   are normally in one segment and already in the server's application buffer — this only bites if
   the read was split. Low prior, but it is the one candidate specific to the 400 request in the
   perf loop.

### B.2 Confirming capture (no code)

1. Run `utf-baselib-http.exe` repeatedly with **stdout and stderr redirected to a file per run**,
   default logging level (DEBUG). In the failing run, read the lines just before each failed
   assertion: the task exception dump gives the phase (connect / handshake / response read /
   `SSL shutdown`), the real error code, and for `attemptConnection` the full
   `diagnostic_information`. For the perf test, the `HTTP status is ...` line
   (`HttpServerHelpers.h:253-260`) shows which of the four URIs failed and the status (expect 0).
2. Before and during each run: `tasklist | findstr utf-` and `netstat -ano -p tcp | findstr 281`
   — a second PID on 28100 (either family) decides candidate 2 outright.
3. Grep the failing log for `Cancelling a TLS connection` (`TcpSslBaseTasks.h:144-150`),
   `Failed to establish a connection` (`TcpBaseTasks.h:1126`), `Non-fatal error during socket
   shutdown` (`:291-298`), `Unexpected exception during SSL shutdown` (`TcpSslBaseTasks.h:591-596`)
   and `An HTTP request failed from the following endpoint` (`HttpServer.h:557-564`) — each is
   the server-side fingerprint of one candidate.
4. Test-only tweak worth making regardless: `TestTlsHandshakeVerification.h:649` should be
   `UTF_REQUIRE_EQUAL( eh::error_code(), acceptEc )` so the failure prints the code.

### B.3 Fixes, by what the capture shows

- **Candidate 2 confirmed** → isolation, not socket code: give each server module its own port in
  the harness (`--port` per module, or a per-module default offset next to the existing `+1/+2/+7`
  in-module offsets), never run two server modules concurrently on Windows, and — as production
  hardening independent of the flake — use `SO_EXCLUSIVEADDRUSE` instead of `SO_REUSEADDR` on
  Windows acceptors (`TcpBaseTasks.h:1205`; Microsoft's recommendation for servers, turns a silent
  hijack into `WSAEADDRINUSE`). The `MachineGlobalTestLock` 1 s settle
  (`MachineGlobalTestLock.h:68-78`) can then be revisited.
  **Outcome (2026-09-09): confirmed, in its lock form** — the concurrently running modules held
  the machine-global lock *simultaneously*, because the lock itself has been broken on Windows
  since `6db5ec1`; the fix is in the lock (implementation log, "Root cause of part B"). Per-module
  ports and the `SO_EXCLUSIVEADDRUSE` hardening remain optional follow-ups and were not made.
- **Candidate 1 confirmed** (failure during response read or TLS shutdown with no foreign
  listener) → two production changes, separately reviewable:
  1. Client tolerance: in `onShutdownCompleted` (`TcpSslBaseTasks.h:553-608`) treat
     `connection_reset` / `eof` / `stream_truncated` during the shutdown as benign when the task
     has no original exception (the response is complete by construction at that point); and let
     `doReadContent` (`SimpleHttpTask.h:832-835`) accept a reset once `Content-Length` bytes have
     been received, mirroring the existing truncation arm.
  2. Server close ordering on Windows: in `shutdownSocket` (`TcpBaseTasks.h:241-335`) use
     `shutdown( shutdown_send )` for the graceful (non-`force`) path and let the peer's FIN /
     close_notify drain before the destructor closes, and ensure the TLS `async_shutdown` is
     attempted on the paths that currently skip it. This has the widest blast radius (every
     server) and must not be made without the capture.
- **Candidate 3 confirmed** (only the 400 request fails, server log shows the parse error before
  the body arrived) → in the HTTP server's error path, read and discard the announced body (or
  `shutdown( send )` + drain with the connection timeout) before closing.
- **Regardless:** fix the seed hygiene in `TlsHandshake_AllowUntrustedRecordsAndClearsEndpointInfo`
  (remove the manual `localhost:28100` seed with a `BL_SCOPE_EXIT`), print `acceptEc` on failure,
  and correct the two comments that misdescribe `linger( false, 0 )`.

---

## Documents updated (all done 2026-09-09)

- `utf-baselib-http-windows-intermittent-failures-record.md`: candidate 3 (deadline) refuted with
  the `TimeoutHttpSslServerT`-only evidence; candidate 1 narrowed to the seed-hygiene defect; the
  failure-semantics facts added (the flag only suppresses logging; TLS shutdown resets fail
  completed requests; `acceptCompleted` is true on the timeout path too; `acceptEc` was never
  recorded and a peer reset before accept surfaces as 10053); the foreign-listener variant of
  candidate 2 added; the solo-capture evidence and the lock audit added.
- `windows-path-normalization-and-flaky-tests-plan-fable51-review.md` finding 6: "52 of 56 failed"
  corrected to "4 of 56 failed", the socket-free-cases argument struck.
- `windows-path-normalization-and-flaky-tests-plan.md`, Change 3: superseded note pointing here,
  with the corrected (unpackager) mechanism.
- New record `blobtransfer-cancel-teardown-warning-record.md` (an earlier
  `blobtransfer-cancelled-reader-holds-input-file-record.md` carried the wrong attribution and was
  removed before it was ever committed).

## Verification

Builds: `make -k -j1 <module> TOOLCHAIN=<tc> VARIANT=<variant> ARCH=<arch>` for the three
flavours above only; affected modules are `utf_baselib_blobtransfer`, `utf_baselib_http` and, if
the new coverage lands there, `utf_baselib_tasks`. Tests are driven from the exes directly with
stdout captured, never through the whole suite.

- A.2 capture before the fix on the debug flavour; after A.3, the cancel-upload case ≥ 20 runs on
  each flavour with no TmpDir warning; the new coverage passes on all three.
- B.2 capture before any HTTP change (≥ 10 runs per flavour, module alone on the box); after the
  chosen B.3 change, `utf_baselib_http` ≥ 10 runs per flavour.
  *As done:* solo runs never reproduced it (16 clean on the two debug trees), so the criterion
  became the concurrent reproduction — 3/3 rounds failing before the fix, 3/3 clean after it on
  `win-a64-vc143-debug` — plus the deterministic red/green lock case; see the implementation log.
- `git ls-files --eol` / `file -b` on every touched file; nothing committed.

## Out of scope

- The Windows → Linux packaging direction (already recorded).
- A dual-stack acceptor (no evidence it is involved).
- Linux runs from this host (not possible here; say so in the records).

---

## Implementation log (2026-09-09)

**Code applied (working tree, uncommitted):**

- `src/include/baselib/transfer/FilesUnpackagerUnit.h` — **the fix for the observed warning:**
  `flushAllPendingTasks()` closes the `filePtr` of every entry in `m_entriesInProgress` as soon as
  the writers have flushed, before any of its outcome branches — on a failure the unit then
  discards the staging directory itself; on a **stop** (the cancelled-pipeline case) the unit
  completes without an error and the pipeline driver deletes the staging directory while the
  unit is still alive (`TestBlobTransferUtils.h:1272-1281`), which is where the warning came from.
  (A first version closed the handles in the failure branch only and left the rate unchanged:
  upload 5/20, 1/10, 2/10 and download 4/5, 2/5, 2/5 on the three flavours.) Pinned by two new
  cases: `BlobTransfer_UnpackagerFailureClosesOpenFilesBeforeDiscardingStagingTests` (fail via
  `onError`, staging gone) and `BlobTransfer_UnpackagerStopClosesOpenFilesTests` (stop via
  `requestCancel` with the unit kept alive, staging deletable on the first attempt).
- `src/include/baselib/transfer/FilesPackagerUnit.h` — separate hardening of a real latent
  defect (not the source of the warning): `BlockReaderTaskT` **no longer holds the input file
  between blocks**: `doExecute()` opens the file, seeks to `m_filePos`, reads the
  block and closes it before the task completes (`m_filePtr` removed). A cancellation observed in
  `onExecute()` — before or after `doExecute()` — releases the data block and marks the reader
  abandoned (`hasMoreBlocks()` false, `isAbandoned()`, cleared by `attachAllData()`), and
  `processTopReadyTask` returns an abandoned reader as available without offering its stale
  block or crediting a checksum.
- **Superseded on the way:** the first version only closed the file when `onExecute()` saw the
  cancel. Its post-fix loop on `win-x64-ccl16-debug` (0/20 before the fix) failed twice in its
  first run with the same warning and `foo/bar/multiChunkFile.bin` again first in the leftover
  tree — a cancel request is only visible while the reader executes (`TaskBase::scheduleNothrow`
  clears it on re-schedule, and one landing after the last check is never seen), so no
  flag-based close can be airtight. A `scheduleEvenIfAlreadyCanceled()` override and a change to
  `FixedWorkerPoolUnitBase::chk2LoopUntilShutdownFinished` (drain on stop) were tried and
  **reverted**; the tree carries neither.
- `src/include/baselib/tasks/TcpBaseTasks.h` — comment-only: the two comments describing
  `linger( false, 0 )` as an immediate close now say what it does (linger disabled, graceful
  close, inherited by accepted sockets on Windows).
- `src/utests/utf_baselib_blobtransfer/TestBlobTransferFilesystem.h` — new
  `BlobTransfer_PackagerCancelledBlockReaderReleasesFileTests`: a deterministic reader-contract
  case. A test subclass of `BlockReaderTaskT` requests its own cancellation at the start of its
  next execution (the only shape a reader can observe one in), on a three-block file after the
  first block was read: on Windows the file is proven undeletable while held; after the cancelled
  execution the reader must have completed successfully, report abandoned / no more blocks / no
  block, and the file must delete on the first attempt; a reattached reader starts clean. The
  file size is read back after writing because the text writer adds a preamble.
- `src/utests/utf_baselib_http/TestTlsHandshakeVerification.h` — the two test-only items from
  B.3 "regardless": `UTF_REQUIRE_EQUAL( eh::error_code(), acceptEc )` so a failure prints the
  code, and a `BL_SCOPE_EXIT` that clears the manual `localhost:28100` seed.

**Captures (unmodified binaries):**

- `utf_baselib_blobtransfer` cancel-upload case, `win-x64-ccl16-debug`, unmodified binary:
  **0 failures in 20 runs** (30-100 s each) — the x64 debug flavour under emulation does not
  reproduce it.
- Same case, `win-a64-vc143-debug` (the pre-existing tree, unmodified binary): **reproduced — 3
  failures in 20 runs** (runs 10, 11, 12), error 32 on the tree root, `foo/bar/multiChunkFile.bin` the
  first surviving file in every leftover tree, no leak at exit, every leftover tree deletable
  after the process exited. Mechanism A.1 confirmed; details in
  `blobtransfer-cancel-teardown-warning-record.md`. This flavour is outside the three build
  flavours of this plan; the tree was nevertheless rebuilt from `215b891` afterwards and the same
  case then ran **0/20** on it with the fix (3/20 before).
- `utf_baselib_http`, full module, unmodified `win-x64-ccl16-debug` binary, alone on the box:
  **0 failures in 6 runs** (~222 s each); `tasklist`/`netstat` before each run showed no other
  `utf-*` process and no listener on 281xx; after the runs the only 281xx state was the expected
  TIME_WAIT population on `127.0.0.1:28100` (about 90 entries).
- Same, unmodified `win-a64-vc143-debug` binary (the tree the failure was first seen on), alone
  on the box: **0 failures in 6 runs** (~222 s each), no foreign listener before any run.
- **Conclusion for B: twelve clean solo runs on the two debug trees, 4 failures out of 56 cases
  in the one original observation.** An in-process race with a 20-33% per-run rate would have
  shown up; the original failure is far more likely to have depended on the conditions of that
  run (several modules in flight, or a leftover process) than on the module itself. Candidate 2
  is therefore the leading hypothesis but has no proven mechanism — which is directly testable
  by running the module concurrently with a server-starting module. Checked: the server-starting
  cases of `utf_baselib_io` (`TestIO.h:3483`,
  `:4715`, `:5333` — block servers on `localhost:28100`, i.e. `[::1]:28100` here) all hold
  `MachineGlobalTestLock` (`:3325`, `:4674`, `:5292`), as do the `utf_baselib_tasks` ones and every
  HTTP case through `startHttpServerAndExecuteCallback` (`HttpServerHelpers.h:355`); so a
  concurrently running module is serialized against the HTTP server by the named mutex, and
  candidate 2 needs either a server start outside the lock or a leftover process, neither of
  which has been found.

**Builds:** `utf_baselib_blobtransfer` and `utf_baselib_http` built for all three flavours
(`win-x64-ccl16-debug`, `win-a64-vc143-release`, `win-x86-vc143-release`); blobtransfer rebuilt
with the fix and the reworked test on all three, zero warnings under `-WX`.

**Fix verification so far:**

- `BlobTransfer_PackagerCancelledBlockReaderReleasesFileTests`: **19/19 assertions on all three
  flavours.** The first version of the test (cancel a completed reader, then push it) could not
  pass by design — `TaskBase::scheduleNothrow` clears the cancel request on re-schedule — which is
  also why the `scheduleEvenIfAlreadyCanceled()` override was dropped. A before/after red run of
  this exact test is not possible (it uses the new accessor), so the "failing before the fix"
  evidence is the capture on `win-a64-vc143-debug` above.
- Cancel-upload loops with the packager-only changes (both versions): `win-x64-ccl16-debug`
  1/10 then 5/20, `win-a64-vc143-release` 0/2 then 2/10, `win-x86-vc143-release` 1/10 — the
  rate did not move, which exposed the wrong attribution (the warning names the unpackager's
  staging directory; see the correction at the top of section A).
- With the failure-branch-only unpackager fix: cancel-upload 5/20, 1/10, 2/10; cancel-download
  4/5, 2/5, 2/5 — the failing logs show the unit completing successfully before the warning, i.e.
  the stop path with the staging tree deleted by the driver.
- **With the complete unpackager fix (close the in-progress handles on flush):** cancel-upload
  **0/20 (`win-x64-ccl16-debug`), 0/10 (`win-a64-vc143-release`), 0/10 (`win-x86-vc143-release`)**;
  cancel-download **0/5, 0/5, 0/5**; the three new cases pass on all three flavours (9/9, 9/9,
  21/21).
- Red/green against the unfixed header (`win-x64-ccl16-debug`, header reverted, module rebuilt,
  cases run, fix re-applied, rebuilt, run again): `BlobTransfer_UnpackagerStopClosesOpenFilesTests`
  **fails without the fix** at `UTF_REQUIRE( ! ec )` — the staging tree cannot be removed while
  the unit is alive — and passes with it; `BlobTransfer_UnpackagerFailureClosesOpenFilesBeforeDiscardingStagingTests`
  passes either way (a failure delivered through `onError` already ended with the file closed),
  so it is a guard for the failure branch, not the proof. The stop case is the pipeline's shape.
- `utf_baselib_http`, full module, one run per flavour with the test-only tweaks in: **56/56 on
  all three.**

**Root cause of part B (2026-09-09, later the same day; `win-a64-vc143-debug` rebuilt from
`215b891`):**

- Reproduced: `utf_baselib_http` alone on the box **0/10** (plus the 6 earlier); with
  `utf_baselib_tasks`, `utf_baselib_io` and `utf_baselib_messaging` running concurrently
  **3/3 rounds fail, 15 of 56 cases each**, all `system:10054` on `localhost:28100`; a 5 s
  `netstat` sampler shows two processes listening on `0.0.0.0:28100` at once (the other one is
  `utf_baselib_messaging`, a broker on 28100 + 28101) and the HTTP client's connections
  established to the other process's listener — candidate 2.
- The lock audit above was right about the cases and wrong about the lock: the DEBUG lines of
  the three processes put on one timeline show io and messaging **inside `MachineGlobalTestLock`
  at the same time** — 36, 34 and 41 overlapping acquisitions in the three rounds. `6db5ec1`
  (2026-09-08) had made `acquireWithWatchdog()` take the `RobustNamedMutex::Guard` on a detached
  helper thread which then exits; on Windows a named mutex is owned by the acquiring thread, so
  the mutex is abandoned as soon as that thread exits, the next waiter anywhere gets
  `WAIT_ABANDONED` (an acquisition, by design of the robust mutex) and the later `ReleaseMutex`
  from the main thread fails with `ERROR_NOT_OWNER`, swallowed by Boost's `scoped_lock`
  destructor. UNIX is unaffected (System V semaphore, process-scoped `SEM_UNDO`). Full
  mechanism in the HTTP record.
- Fix (`MachineGlobalTestLock.h`): the holder thread keeps the guard until the destructor
  signals the release, unlocks on the acquiring thread and is joined; the watchdog is unchanged
  and the thread is detached only on the rip path. No production code is touched.
- Coverage: `BaseLib_MachineGlobalTestLockExcludesConcurrentAcquirerTests` (`utf_baselib`,
  `TestBaselibDefault5.h`). Red on `win-a64-vc143-debug`: built with the new case against the
  unfixed header it **fails** at `UTF_REQUIRE( ! acquired.wait( 500U ) )` — the second acquirer
  went straight through the held lock, and the log still printed "was released" afterwards.
- Verification with the fix (`win-a64-vc143-debug`; `utf_baselib`, `utf_baselib_tasks`, `_io`,
  `_messaging` and `_http` rebuilt with `-j1`, zero warnings): the new case **5/5**, the two
  existing named-mutex cases pass; the concurrent reproduction **3/3 rounds 56/56** (was 3/3
  rounds with 15/56 failing); two listeners on `0.0.0.0:28100` in **0 of 360** netstat samples
  (was 45 of 81); lock holds overlap by milliseconds only — the log order — where they overlapped
  by seconds before. Details in the HTTP record. Second toolchain: `utf_baselib` rebuilt on
  `win-x64-ccl16-debug` (clang-cl), zero warnings, the new case and the named-mutex robustness
  case **3/3** there too. The server modules of the other flavours have not been rebuilt with the
  header change; it is platform-generic, and the a64 debug tree is the one the failure was
  observed on.
