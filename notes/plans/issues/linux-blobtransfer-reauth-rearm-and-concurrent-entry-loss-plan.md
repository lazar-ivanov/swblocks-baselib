# Linux `utf_baselib_blobtransfer`: the re-auth "0 of 1" and the concurrent "14 of 25" failures — one exception-ordering race

**Found:** 2026-09-10, validating the full Linux matrix (`ARCH=a64`, `gcc1520` and `clang2010`,
`debug` and `release`) of all 19 test modules after pulling `215b891` and `01562e0` onto `lazari2`.

**Status:** **fix (A) approved and implemented in the working tree, 2026-09-10; Linux verification
complete, Windows and macOS not yet run** — production code `src/include/baselib/reactive/InputConnector.h`, tests in
`TestBlobTransferFilesystem.h` and `TestTasks5.h`; see "Implementation log" below. **Reviewed
2026-09-10** by Fable 5.1 (code analysis only): the fix is sound for everything the suite
exercises, with one residual to close before the commit — the nested exception chain is still
shared between the two tasks, R-1 — and four smaller items; see "Residual issues after the
implementation". **Residuals R-1, R-2 (minimal) and R-4 implemented and verified on `gcc1520`
debug and `clang2010` release, 2026-09-10, Fable 5.1**; R-3 deferred; R-5 (Windows and macOS)
still outstanding. **R-6** (a null nested link aborted the process in `copyForTarget()`, found
reviewing R-1) fixed in `2bef17c`, 2026-09-10, Opus 5; the whole tree passed the full Linux
stress run, see R-6. **Revised 2026-09-10** after a read-only trace of
the code by Fable 5.1 (nothing was built or run for the revision): the two issues are most likely
one defect with two faces — a race in *which* of two failing pipeline units gets reported — and
the original re-arm hypothesis for issue 1 is not supported by the trace. See "The mechanism both
failures share" below. The original measurements are kept as they were taken; the interpretation
around them is what changed.

**Attribution (revised).** The race needs two things which both predate the pull: the unpackager
failing on an incomplete tree (`351f5a0`, 2026-09-05, review item T-2) and a download which is
*expected* to fail on the receiver side (the post-deletion "not found" download, whose assertion
was tightened to `UTF_REQUIRE_EXCEPTION` in `6913710` / `924288a`, 2026-09-08). Neither `215b891`
nor `01562e0` touches anything on that path: the unpackager change in `215b891` moves the closing
of in-progress files, the `TcpBaseTasks.h` linger change is comment-only, and the lock rewrite is
test-only. Issue 1 was in any case measured at `27e337f`. So both issues are pre-existing on
`lazari2` and date from 2026-09-05 at the earliest; the pull only changed how often the second was
seen, because it was found by a 5-module run on a 2-core host. The baseline comparison for issue 2
proposed in the first version of this document is no longer the first step — see the plan.

**Related:** `windows-blobtransfer-cancel-handle-and-http-reset-flakes-plan.md` (the Windows
counterpart, same fixture), `blobtransfer-cancel-teardown-warning-record.md`,
`timer-cancel-race-and-macos-getpeername-einval-plan.md`,
`notes/reviews/major/update_2026/whole-library-cxx-review-fable51-decisions.md` (the T-2 / T-3
decisions which introduced the unpackager's completeness check and the re-arm code).

**Host:** Ubuntu 24.04, aarch64, **2 cores** — the low core count matters for issue 2.

---

## Matrix result

Builds are clean everywhere: 19/19 modules linked in all four combinations, zero warnings, zero
errors. Tests: 17 of the 18 runnable modules pass in all four combinations.

| Combination | Modules passing | Non-zero |
|---|---|---|
| `gcc1520` debug | 16/18 | `blobtransfer` (issue 1), `jni` (known) |
| `gcc1520` release | 17/18 | `jni` (known) |
| `clang2010` debug | 17/18 | `jni` (known) |
| `clang2010` release | 16/18 | `blobtransfer` (issue 2), `jni` (known) |

`utf_baselib_plugin` builds `utf-baselib-plugin.so` and has no executable of its own; it is
exercised by `utf_baselib_loader`, which passes in all four. `utf_baselib_jni` exits 200 after
`*** No errors detected` in all four — the pre-existing teardown error, out of scope here.

---

## The mechanism both failures share

Both reports come from the same line: the completeness check in
`FilesUnpackagerUnit::flushAllPendingTasks()` (`FilesUnpackagerUnit.h:1585-1614`). It fires when
the unpackager's input has completed and its target is not complete. What the check cannot see is
*why* the input completed, and there are two ways it can:

1. The receiver delivered every chunk and completed normally.
2. The receiver **failed**. `ObservableBase::run()` (`ObservableBase.h:1160-1164`) fans a failure
   out as `onError` followed by `onCompleted`. The unpackager's input connector has no `onError`
   override — `InputConnector.h:107-175` overrides only `onCompleted` and `onNext` — so the error
   lands in `ObserverBase::onError()` (`ObserverBase.h:52`), which **logs it at debug level and
   returns**. The `onCompleted` which follows sets `m_inputDisconnected`, exactly as a normal end
   of input would.

So a receiver failure always turns into the unpackager's "incomplete" report a few loop iterations
later, with the receiver's own exception visible only as a debug line
(`ObserverBase:onError() was called with the following exception`). The fixture already knows this
for the cancel case: the comment at `TestBlobTransferUtils.h:1590-1615` accepts either the
`operation_aborted` or the "incomplete" report of an interrupted download. It does not know it for
the case below.

### The download which is *supposed* to fail on the receiver

Every case which runs through `executeTheFilesPackagerAndTransmitterPipelineInternal` ends with a
deleter phase and then one more download, `cbDownloadTest( noOfDownloads )`, which must fail with
`ServerErrorException` / `no_such_file_or_directory` because the chunks are gone
(`TestBlobTransferUtils.h:1446-1471`, `UTF_REQUIRE_EXCEPTION`). In that download:

- The receiver's first `GetDataBlockSize` gets an error acknowledgement; `getSuccessfulTopTask()`
  rethrows the `ServerErrorException` without retrying (`ChunksSendRecvBase.h:321-327`) and the
  receiver fails **before a single chunk is delivered**.
- The unpackager, told its input completed, finishes its own work — the entries which need no
  chunk — and reports "incomplete".
- `executeQueueAndCancelOnFailure()` (`TasksUtils.h:199-244`) pops the queue with `pop( true )`,
  keeps the exception of the **first task to complete**, cancels the rest and rethrows that one.
  Nothing prefers the upstream unit's exception. The suite already documents this property:
  `BlobTransfer_TransmitterServerUnreachableTests` says "Which task executeQueueAndCancelOnFailure()
  reports first is not deterministic, so the transmitter itself is inspected below"
  (`TestBlobTransferFilesystem.h:983-985`).

When the receiver's task completes first — the usual order — the `ServerErrorException` is reported
and the assertion passes. When the unpackager's task completes first, the reported exception is the
`UnexpectedException` "The unpackaged content is incomplete", which is not a `ServerErrorException`,
escapes `UTF_REQUIRE_EXCEPTION`, and fails the case with **precisely the two messages recorded in
this document**:

| Fixture | Entries the unpackager creates without any chunk | Message |
|---|---|---|
| Full tree (`TestFsUtils.h:389-467`, UNIX): 9 directories, 12 files of which 1 is zero-length, 4 symlinks = 25 | 9 + 1 + 4 = **14** | `0 entries have missing chunks and 14 of 25 entries were created` |
| `singleFileInput` (one 1 KB file): 1 entry | **0** | `0 entries have missing chunks and 0 of 1 entries were created` |

"0 entries have missing chunks" in both is the same fact: no chunk ever reached the unpackager. The
14 are exactly the directories and zero-length files the scheduler creates in its first phase
(`FilesUnpackagerUnit.h:194-206`) plus the symlinks it creates in its second phase once the input
is disconnected (`:375`). The identical count in the two issue-2 occurrences is therefore not a
repeatable stopping point of a download; it is the size of the chunkless subset of the fixture.

A passing run leaves the same footprint at the default logging level. In
`bld/ub24-a64-gcc1520-debug/utflogs/utf_baselib_blobtransfer.log` (2026-09-08) the plain case shows
`****** downloadId: 1`, then a run of `ServerErrorException` diagnostics (one per client connection
which got "not found"), then `ObserverBase:onError() was called with the following exception`
carrying that `ServerErrorException` — the receiver's failure being swallowed by the unpackager's
connector — and then the case leaves. A failing run is the same sequence with the unpackager's
report escaping instead of the receiver's.

### Why the ordering flips

Both units complete through their timer loops, one iteration per `DEFAULT_WAIT_TIME_IN_MILLISECONDS`
(50 ms) when nothing is ready. After it fails, the receiver needs its worker queue drained:
`cancelAll()` clears the ready transfer tasks at once, but every authentication task still
executing when the failure landed has to be aborted and collected, which costs a loop iteration or
two. After it is told the input completed, the unpackager needs its scheduler task to go round at
least twice for the single-file fixture (phase two, the directory timestamps, then the final flush)
and about four times for the full tree (phase two schedules the 4 symlinks, their writers run, then
timestamps and flush). That is roughly 100 ms of unpackager work for the single file and 200 ms for
the full tree, against 0 to 100 ms of receiver work depending on how many of the 16 connections
(`UtfArgsParser::connections()` default) were still authenticating when the first of them got
"not found" — with all 16 re-authenticating over pooled sockets while the first ready one already
requests a chunk, "some still in flight" is common. The single-file case is therefore close to a
coin flip whenever the receiver needs two iterations, which fits the 10 % seen in isolation; the
full tree gives the receiver a 100 ms head start, which only CPU starvation from four other modules
on two cores erodes, which fits "only in the 5-module batch". The first version's observation that
spinning CPU hogs do not reproduce it is consistent as well: hogs on two cores do not produce the
particular interleaving that four test modules with their own thread pools and timers do.

This ordering is inferred from the loop structure, not measured. Steps 1 and 2 of the plan settle
it.

---

## Issue 1 — the single-file re-auth case reports "0 of 1 entries were created"

### Symptom

`BlobTransfer_FilesPackagerInMemoryReauthAfterDropOnLoadTests` fails intermittently with

```
FilesUnpackagerUnit.h(1604): The unpackaged content is incomplete;
0 entries have missing chunks and 0 of 1 entries were created
```

Zero entries in progress and zero created: the unpackager was told the input was complete without
ever receiving the single chunk of the transfer. As shown above, that is what *every* failure of
this case's post-deletion download looks like, and it is also what a masked receiver failure in the
first download would look like; the message alone does not say which download it was.

### Measured rates (`gcc1520` debug, one case per process)

| Build | Runs | Failures | Rate |
|---|---:|---:|---:|
| HEAD `01562e0` | 50 | 5 | 10% |
| `27e337f` (pre-pull, isolated worktree) | 60 | 1 | 1.7% |

The defect is present at both commits, so it is **pre-existing**. The apparent rate difference is
not significant at these sample sizes (Fisher exact p≈0.09) and the two sets ran under different
background load, so no conclusion should be drawn from it without a controlled re-measurement.

### Which download failed is not yet known

The case runs the receiver three times: the faulted first download (drop, reconnect,
re-authenticate, re-request), the deleter, and the post-deletion download which must fail with
"not found". The failure was recorded from the exception text only. Two things already in the log
output decide it without any new instrumentation: the `****** downloadId: N` banner (info level)
which precedes the failure, and the `ObserverBase:onError() was called` block (debug level, on by
default through `--bl-logging-level`) which carries the receiver's exception. If the banner reads
`downloadId: 1` and the swallowed exception is the `ServerErrorException` with
`no_such_file_or_directory`, this is the shared mechanism and not a transfer defect. The counter
assertions at `TestBlobTransferFilesystem.h:166-167` never ran in the failing runs — the exception
escapes before them — so they say nothing either way.

### The original re-arm hypothesis, re-examined

The first version of this document proposed that the receiver loses the postponed chunk because
its `flushAllPendingTasks()` (`ChunksReceiverDeleterBase.h:499-508`) returns `isEmpty()` without
the `hasPostponedDataChunks()` guard the transmitter has (`ChunksTransmitter.h:259-265`). The
asymmetry is real, but the trace does not produce a lost chunk from it:

- A chunk is postponed only by `scheduleAuthenticationTask()` (`ChunksSendRecvBase.h:643-683`),
  which is reached from `pushReadyTask()` for the task at the **top of a non-empty worker queue**
  and pushes that same task back. The queue is therefore never empty at the moment of
  postponement, and `flushAllPendingTasks()` cannot return true before that task has completed at
  least once more.
- When it completes successfully (the re-authentication), the unwind loop pops it and
  `tryRearmPostponedChunk()` (`ChunksReceiverDeleterBase.h:364`) puts the chunk on it before the
  queue-empty check. When it fails again, `getSuccessfulTopTask()` starts another reconnect and
  returns null, the loop breaks without popping, and the queue is still non-empty. There is no
  third outcome for a popped task, and the scheduler is finished by then, so nothing else consumes
  the postponed entry. The three "candidate holes" of the first version are all of the second kind:
  they break the loop *without popping*, which leaves the queue non-empty and the flush false.
- The transmitter's guard protects `m_fsmd -> finalize()`, a commit the receiver does not have.
  Adding the guard to the receiver would, as the first version already noted, turn a hypothetical
  lost chunk into a certain hang, because a false return with an empty queue leaves nothing to
  re-arm onto.

Two properties of the fixture were not in the first analysis and were checked because they could
have opened a hole; they do not:

- The injected fault cancels **every** server-side connection, not only the faulted one
  (`FaultInjectingBlobServer::dropAllConnections()`, `TestBlobTransferUtils.h:685-699`). The 15
  idle client connections are dead from that moment, and most or all of them are already back in
  the `SendRecvContext` pool, because the receiver's unwind returns idle connections to the pool on
  every loop iteration once the scheduler is done (`ChunksReceiverDeleterBase.h:400`). The
  reconnect then takes those dead pooled sockets one by one (`ChunksSendRecvBase.h:207`): each
  attempt "connects" immediately, the re-authentication fails with a socket error, the
  classification at `:321-336` treats it as retriable, and `resetRetry()` (`:474`) runs after every
  pooled "success", so the endpoint retry budget is never consumed and there is no 6-second
  back-off between attempts. The postponed chunk survives every round because a failed
  authentication task carries `chunkId == nil()` by the time `scheduleAuthenticationTask()` looks
  (`tidyUpTransferTaskOnFailure()` clears it), so nothing is postponed twice.
- An idle task popped after the postponement is re-armed with the chunk although its own
  connection is dead. Its request fails with a socket error, it goes through the same reconnect and
  re-authentication, the chunk is postponed again from that task, and re-armed again. Slow, not
  lost.

The hypothesis is parked, not closed. If the log evidence of step 1 says the failure is in the
**first** download (`downloadId: 0`), the swallowed receiver exception is the lead, and the
candidates are the classes `getSuccessfulTopTask()` does not retry: a `ServerErrorException` from
the server (for example a `permission_denied` acknowledgement on a connection the server considers
unauthenticated), `operation_aborted`, or any `UnexpectedException` from the client's protocol
checks (`chkPartialDataTransfer`, "Invalid control code", "Invalid chunk size"). Only if that
exception is none of these — the receiver *completed* rather than failed — does the re-arm path
come back into scope, with the instrumentation the first version proposed (tracing at the loop
exits and at `flushAllPendingTasks()`, recording `isEmpty()` and `hasPostponedDataChunks()`).

---

## Issue 2 — the full-tree cases report "14 of 25 entries were created", only under module concurrency

### Symptom

In the `clang2010` release leg, the **first** case of the module,
`BlobTransfer_FilesPackagerInMemoryTests` — a plain `CancelType::NoCancel` transfer with no fault
injection — failed with

```
FilesUnpackagerUnit.h(1614): The unpackaged content is incomplete;
0 entries have missing chunks and 14 of 25 entries were created
```

The first version of this document read this as eleven entries lost on an unfaulted transfer. The
case has no injected fault, but it does end with a download in which the receiver is *meant* to
fail — the post-deletion download — and 14 is the number of entries the unpackager creates without
receiving anything (see "The mechanism both failures share"). Nothing points at data loss in the
first, verified download: that download is followed by `TestFsUtils::compareFolders` and a byte
comparison of the multi-chunk file, and both passed in every run which reached them.

### Reproduction attempts (all `clang2010` release, HEAD)

| Condition | Runs | Failures |
|---|---|---:|
| Matrix: 5 test modules concurrently, 2 cores | 1 | **1** |
| Isolated, single case per process | 25 | 0 |
| Isolated, 4 spinning CPU hogs on 2 cores | 15 | 0 |
| Same 5-module batch replayed (`utf_baselib`, `_apps`, `_async`, `_basictask`, `_blobtransfer`) | 3 | **1** |

**It is reproducible under module concurrency and only there** — roughly one run in three of the
5-module batch, and never in 40 isolated runs, with or without CPU starvation. Plain CPU shortage is
therefore not the trigger; something about the other modules running alongside is.

The second occurrence landed on a **different case** — `BlobTransfer_FilesPackagerInMemoryWithheldChunkTests`
rather than the plain one — but produced the identical count, `14 of 25 entries were created`.

### What this means (revised)

Nothing was lost. The count is the fixture's chunkless subset, and the receiver delivered no chunk
because in the download where the failure occurred the receiver is meant to fail. Two observations
from the first version now read differently:

- "It hits whichever download is in flight" — both occurrences hit the post-deletion download. In
  the withheld-chunk case that is the only download in which completeness "was expected to hold",
  and what was expected there was a `ServerErrorException`, not success: the withheld first
  download expects the unpackager's `UnexpectedException` (`TestBlobTransferUtils.h:1229`) and has
  no competing failure, so it cannot race.
- "The count being identical suggests the download stops at a repeatable point" — it is the number
  of directories, zero-length files and symlinks, which the unpackager creates itself.

Why only under module concurrency: for the full tree the receiver has about a hundred milliseconds
of unpackager work as head start (see "Why the ordering flips"). Four other modules with their own
thread pools and timers on two cores is what erodes it; spinning hogs do not.

The threads opened by the first version are closed by the code, without needing the baseline run:

- The machine-global lock. The two new lock cases in `utf_baselib` (`01562e0`) use unique names
  (`BL-Test-Mutex-<uuid>`, `BL-Test-Machine-Lock-<uuid>`, `TestBaselibDefault5.h:442-443, 659`) and
  remove only their own semaphores, so they cannot touch the default lock's semaphore; and none of
  the four other modules in the batch (`utf_baselib`, `_apps`, `_async`, `_basictask`) binds
  `UtfArgsParser::port()` at all, so there was nothing for the lock to exclude.
- The fixed test port. The acceptor sets only `reuse_address` (`TcpBaseTasks.h:1208`), never
  `SO_REUSEPORT`, so on Linux a second listener on 28100 fails to bind rather than sharing the port;
  a client cannot silently reach a wrong server.
- The per-run server token (step 2 of the first plan) would not have caught this: the server the
  client talked to was the right one and answered correctly ("not found").

---

## Plan

### 1. Confirm the phase and the swallowed exception (no code change)

Run `BlobTransfer_FilesPackagerInMemoryReauthAfterDropOnLoadTests` in a loop, gcc debug, isolated,
as before, but keep the output of every run. In each failing run read the last `****** downloadId: N`
banner before the failure and the `ObserverBase:onError() was called with the following exception`
block which precedes it. Both are emitted at the default `--bl-logging-level` (debug), so the
driver only needs to keep the log; no logging change is required. Expected: `downloadId: 1` and a
swallowed `ServerErrorException` with `no_such_file_or_directory`. Do the same for the two issue-2
cases in the 5-module batch (`--run_test=BlobTransfer_FilesPackagerInMemoryTests` and
`...WithheldChunkTests`).

If any failing run of the re-auth case shows `downloadId: 0`, record the swallowed exception and go
to the parked hypothesis in issue 1; the fix below does not address that run.

### 2. Measure the race across every case which has it

Every pipeline case with `expectNotFoundAfterDelete == true` runs the post-deletion download and
therefore has the race: the plain case, the three re-auth cases, the withheld-chunk case, the
throttled-subscribers case and the peer-sessions case. Run each 100 times in gcc debug, isolated,
and record the failure rate per case. This replaces both the "run OnSave and OnRemove the same
number of times" step and the baseline comparison. If the rates are of the same order across the
cases, the shared mechanism is confirmed and the "OnSave is stable" observation was a small-sample
artifact (it rests on a handful of runs). If the re-auth-on-load case is clearly worse than the
others, the reconnect churn of its post-deletion download — a pool which may still hold dead
sockets from the drop — is the likely reason, and it should be understood before the fix below is
judged sufficient.

### 3. Fix

The defect is that a downstream unit cannot tell "my input ended" from "my input failed", and the
harness then reports whichever unit finished first. Two fixes are possible; the first is
recommended.

**(A) Product: propagate the upstream error through the input connector.** Give
`InputConnectorT` an `onError` override which dispatches the exception into the target when a
dispatcher is present (`m_errorDispatcher -> dispatchException( eptr )`, exactly what `onNext` and
`onCompleted` already do for exceptions of their own at `InputConnector.h:130` and `:169`), and
falls back to `ObserverBase::onError()` otherwise. `ObservableBase::run()` rethrows a dispatched
exception at the top of its next iteration (`ObservableBase.h:1113`), before any loop work, so:

- the unpackager fails with the receiver's `ServerErrorException` — the same `exception_ptr`, so
  the error-code and errno predicates of the assertion hold — whichever task the harness pops
  first; the race disappears rather than being tolerated. `onError` is queued ahead of
  `onCompleted` on the same stranded events queue, so the dispatched exception is always seen
  before `m_inputDisconnected` can lead to the "incomplete" report;
- the unpackager still closes its files and discards the staging directory, through the existing
  failure branch of `flushAllPendingTasks()`, and the download lambda's own cleanup at
  `TestBlobTransferUtils.h:1257` becomes a no-op;
- the cancel cases become deterministic too: a stopped receiver fans out `operation_aborted`, which
  the unpackager then reports itself; the fixture's `eh::system_error` arm just above the comment
  at `TestBlobTransferUtils.h:1590-1615` already accepts it, and the "incomplete" arm becomes dead
  (keep it for one release with a note, or remove it with the change);
- the same change closes a product gap on the upload side: today a scanner or packager failure
  reaches the transmitter as `onCompleted`, and a caller which does not cancel the whole queue on
  the first failure sees the transmitter finalize the metadata of a partial upload. With the
  connector dispatching, the transmitter fails with the upstream error and does not finalize.

Blast radius: `bindInputConnector` has no caller in product code in this repository (only
`TestBlobTransferUtils.h`, `TestBlobTransferFilesystem.h`, `TestTasks.h` and `TestTasks5.h`), so
the behavioural change is visible to downstream consumers of the library and to those four files.
`ObserverBase::onError()` itself keeps swallowing, so the observers which pin that
(`TestTasks.h:3464-3475`, `TestTasks5.h:1431-1450`) are unaffected, and the `BlocksReceiver` test
observer (`TestTaskUtils.h:628`) has no dispatcher and keeps logging. This is a product behaviour
change and needs the owner's decision.

**(B) Harness only.** In the download lambda's `catch( std::exception& )`
(`TestBlobTransferUtils.h:1257`), if `unitChunksReceiver` failed, rethrow *its* exception instead
of the one the harness popped — the pattern `BlobTransfer_TransmitterServerUnreachableTests` uses.
This makes the assertion deterministic without touching the product, but it leaves every other
consumer of the pipeline with a non-deterministic and misleading error, and it leaves the upload
gap above open. Use it only if (A) is rejected.

**Tests to add with (A):**

- Tighten `BlobTransfer_UnpackagerFailureClosesOpenFilesBeforeDiscardingStagingTests`
  (`TestBlobTransferFilesystem.h:1390-1436`). It already calls `input -> onError( ... )` with
  "injected download failure" and passes today only because the "incomplete" report happens to be
  the same exception type. Assert the message with `UTF_REQUIRE_THROW_MESSAGE`: on current code
  that fails, with (A) it passes — the red/green pin of the change.
- A reactive-level case next to the InputConnector cases in `utf_baselib_tasks` (T224): an
  observable which fails, subscribed through `bindInputConnector` to an otherwise idle unit; the
  unit's task must fail with the observable's exception, and a connector without a dispatcher must
  still swallow.

**Not a fix:** adding the `hasPostponedDataChunks()` guard to the receiver's
`flushAllPendingTasks()`. The trace shows it cannot fire in a state which is not already handled,
and if it did fire it would hang the unit.

### 4. Verification

- Build `utf_baselib_blobtransfer` and `utf_baselib_tasks`, `-j1`, gcc and clang, debug and
  release.
- The tightened and the new case red on current code, green on the fix.
- Step 2's loop again on the fixed build: every case 100 times, gcc debug, isolated; the
  post-deletion download must report `ServerErrorException` 100 of 100.
- The 5-module batch which found issue 2 (`utf_baselib`, `_apps`, `_async`, `_basictask`,
  `_blobtransfer`, clang release), ten or more replays, zero failures of the blob module.
- The three cancel cases (`...CancelUploadTests`, `...CancelDownloadTests`,
  `...CancelRemoveTests`) in a loop, since the set of exceptions they accept changes shape.
- Windows and macOS for the same modules; nothing in the mechanism is platform-specific.

## Implementation log

**2026-09-10, Opus 5.** The owner approved the proposal; fix (A) was implemented, not the
harness-only (B). The analysis above was checked against the code before any change and its
mechanism holds; two details of it did not, see "Corrections" below.

### The change

- `InputConnectorT::onError()` (`InputConnector.h`): when the connector has an error dispatcher it
  calls `m_errorDispatcher -> dispatchException( eptr )` and returns; without one it falls back to
  `ObserverBase::onError()`, which still logs and swallows. It never throws —
  `ObservableBase::notifyObserverError()` wraps the call in `BL_WARN_NOEXCEPT`, so a throw would
  surface as a WARNING, which the test harness turns into a failure.
- The ordering argument of the proposal was confirmed in code: `ObservableBase::run()` runs under
  the task's `m_lock` for its whole duration (`ObservableBase.h:312-319`) and `dispatchException()`
  takes the same lock, so a dispatched error cannot land in the middle of an iteration; and since
  it is delivered ahead of `onCompleted()` on the same events queue, the rethrow at the top of the
  next iteration always precedes any iteration which could see the input disconnected.

### Tests, red on the unfixed code and green on the fix (gcc debug)

| Test | Unfixed | Fixed |
|---|---|---|
| `BlobTransfer_UnpackagerFailureClosesOpenFilesBeforeDiscardingStagingTests`, tightened to `UTF_REQUIRE_THROW_MESSAGE( ..., "injected download failure" )` | fails: the unit throws its own "incomplete" report, the injected error is swallowed at debug level | passes |
| `Tasks_ReactiveInputConnectorTests`, new arm (9): a connector with a dispatcher dispatches `onError()` exactly once, the same `exception_ptr`, and does not throw | fails: `dispatchCount()` is 0 | passes |
| `Tasks_ReactiveInputConnectorTests`, new arm (12) (numbered (10) when written, renumbered under R-4): a connector without a dispatcher still swallows | passes | passes |
| `Tasks_ReactiveInputConnectorPropagatesUpstreamErrorTests` (new): a failing observable bound through `bindInputConnector()` to an idle `ProcessingUnit`; the unit must fail with the observable's exception | fails: "exception expected but not raised" — the unit *completed successfully* after its input failed | passes |

The last row is the defect in its plainest form, with no transfer code involved.

Full suites of both modules on the fixed build, gcc debug: `*** No errors detected`, build with
zero warnings. The only ERROR line in the logs is the deliberate `RIP:` of the
`Tasks_ReactiveUnsubscribeFromCallbackTests` child process, present before the change too.

### Corrections to the analysis

1. **Step 1's logging premise.** The `downloadId` banner and the `ObserverBase:onError()` block are
   emitted at the default `--bl-logging-level`, but a test binary run directly prints no bl logging
   at all after the ARGPARSE dump: `UtfMain` routes it through Boost.Test's `UTF_MESSAGE`, which
   is only emitted at `--log_level=test_suite` — the flag the `test_` make target passes through
   `UTF_FLAGS` (`common.mk:204`). The failure logs kept from the first round were captured without
   it and contain neither line, so they cannot answer step 1. The measurement runs pass the flag.
2. **The cancel cases do not become fully deterministic.** `ObservableBase::requestCancel()` only
   sets `m_stopRequested`. A stopped receiver fans out `onError( operation_aborted )` only if its
   loop reaches `chk2ThrowIfStopped()`; an observable whose loop simply finishes delivers a plain
   `onCompleted()`, which the fix does not touch. The "incomplete" arm of the cancel fixture
   (`TestBlobTransferUtils.h:1590-1615`) is therefore not established to be dead and was left
   unchanged, as was the rest of the fixture.

3. **Sharing the upstream `exception_ptr` is not a feature, it is a data race.** The proposal
   says the target fails with "the same `exception_ptr`, so the error-code and errno predicates of
   the assertion hold". The first implementation did exactly that and **crashed**: in the
   cancel-loop verification (`...CancelUploadTests`, gcc debug) the process died with SIGSEGV
   once in 50 runs, and once in 36 under gdb. Rethrowing an `exception_ptr` throws the *same
   object*, so the receiver and the unpackager ended up failing with one exception object; each
   task, when it completes, enhances that object in place (`TaskBase::enhanceException()`,
   `TaskBase.h:932`, `<< errinfo_task_info`) and dumps it (`chk2DumpException()` →
   `diagnostic_information()`, which also writes a cached string inside the object) — on two
   threads, with no ordering between them. The backtrace shows exactly that: thread 21 in
   `TimerTaskBaseT::onTimer → notifyReady → chk2DumpException → diagnostic_information`,
   formatting the `errinfo_task_info` string of the receiver's `ServerErrorException` through a
   freed pointer. The same `CancelUpload` case on the unfixed binary showed no crash (see the
   verification status for the count).
4. **The 10 % was measured under load, not in isolation.** The rates in issue 1's table (5 of 50 at
   HEAD, 1 of 60 at `27e337f`) were taken while the matrix build was compiling on one of the two
   cores; the first version of this document said "one case per process" and did not say that.
   Isolated, the same case failed 0 times in 100 on the unfixed build, and the seven post-deletion
   cases together once in 700 (see the verification status). The single-file case is therefore
   not "close to a coin flip on its own". Two things differed between the runs, the load and the
   `--log_level=test_suite` flag, which slows every thread that logs; the loaded comparison in the
   verification status runs without the flag. Either way an isolated loop cannot show a
   before/after difference for this race.

### The corrected change

`onError()` now dispatches `copyForTarget( eptr )`:

- a clone-enabled boost exception — every exception thrown with `BL_THROW`, whose
  `BL_EXCEPTION_IMPL` applies `enable_current_exception` — is **deep-copied** through
  `clone_base::clone()`, which clones the error info container entry by entry
  (`copy_boost_exception()`), and the copy is rethrown and captured as a new `exception_ptr`;
- a non-boost exception is dispatched as is: a task never enhances one in place, it wraps it in a
  printing-only exception instead (`TaskBase.h:630-667`), so sharing it is safe;
- a boost exception which cannot be cloned is not dispatched at all: it can be neither copied nor
  shared safely, so the connector falls back to logging it, as before the fix.

Taking the copy inside `onError()` is itself race-free: an observable does not complete until the
events of its subscribers have been delivered (`ObservableBase::chk2WaitAllEvents2Flush()`,
`areAllEventsFlushed()`), so the upstream task cannot be enhancing its exception while the
connector copies it. The pre-fix code relied on the same ordering — `ObserverBase::onError()`
formatted the shared exception on the events thread too — which is why the sharing never crashed
before the exception reached a second *task*.

Arm (9) of `Tasks_ReactiveInputConnectorTests` now requires a distinct object carrying the same
type and message, and a deep copy (enhancing the copy must leave the original without
`errinfo_task_info`); arms (10) and (11) pin the non-boost and the non-clonable branches (they were
(11) and (12) when written; renumbered into file order under R-4).

### Not changed

The cancel fixture; the receiver's `flushAllPendingTasks()` (no postponed-chunk guard); the
harness-only alternative (B).

### Verification status

**First implementation (shared exception object), superseded.** All four Linux combinations of
both modules built with zero warnings and passed their full suites once each — and then the cancel
loop crashed it (correction 3). A single suite pass per combination did not see a 1-in-40 crash.

**Crash attribution**, `BlobTransfer_FilesPackagerInMemoryCancelUploadTests`, gcc debug, one case
per process, `--log_level=test_suite`, two loops sharing the 2 cores:

| Binary | Runs | Failures | Crashes |
|---|---:|---:|---:|
| unfixed snapshot | 150 | 0 | 0 |
| first implementation | 50 (plain) / 36 (gdb) | 1 / 1 | 1 / 1 (SIGSEGV) |
| corrected implementation | 100 | 0 | 0 |

**Corrected implementation.**

- All four Linux combinations (`gcc1520` and `clang2010`, `debug` and `release`): both modules
  build with zero warnings and zero errors and both full suites pass; the tightened and the new
  cases are confirmed to have run in every one.
- The three cancel cases, gcc debug, 50 runs: 0 failures — on top of the 100 `CancelUpload` runs
  above.
- Steps 1 and 2 — the seven post-deletion cases, gcc debug, isolated, one case per process,
  `--log_level=test_suite`, 100 runs each, failures per case:

  | Case | Unfixed | Corrected |
  |---|---:|---:|
  | `...InMemoryTests` | 0 | 0 |
  | `...WithheldChunkTests` | 1 | 0 |
  | `...ReauthAfterDropOnSaveTests` | 0 | 0 |
  | `...ReauthAfterDropOnLoadTests` | 0 | 0 |
  | `...ReauthAfterDropOnRemoveTests` | 0 | 0 |
  | `...ThrottledSubscribersTests` | 0 | 0 |
  | `...PeerSessionsTests` | 0 | 0 |

  The one unfixed failure is step 1's evidence, and it is the shared mechanism exactly: the last
  banner before it is `downloadId: 1` (the post-deletion download), the connector swallowed the
  receiver's `ServerErrorException`, and the escaped report was `0 entries have missing chunks and
  14 of 25 entries were created`. Nothing points at the first download, so the parked re-arm
  hypothesis gets no support. At one failure in 700 the isolated loop cannot show a before/after
  difference (correction 4); what it does show is 700 runs of the corrected build across every
  post-deletion case without a failure or a crash.
- The 5-module batch which found issue 2 (`utf_baselib`, `_apps`, `_async`, `_basictask`,
  `_blobtransfer`), clang release, without the logging flag as in the matrix: 10 replays, every
  module of every replay passing. On the unfixed code the same batch failed the blob module in 2 of
  4 runs (the matrix run and 1 of the 3 replays). That sample is small, but even at one failure in
  three replays, 10 clean replays would occur by chance only about 2 % of the time. The unfixed
  batch could not be re-run as a same-session control: its clang release binaries had been rebuilt.
- `...ReauthAfterDropOnLoadTests`, gcc debug, 100 runs on each build with one core kept busy by a
  spinning process and without the logging flag: unfixed 1 failure (the familiar `0 of 1 entries
  were created`), corrected 0. One spinning core does not recreate the original 10 % — a real
  compile loads memory and caches as well as a core — so this comparison is **inconclusive** as
  before/after evidence (1 in 100 against 0 in 100). What it adds is another 100 clean runs of the
  corrected build under load.

**What the evidence amounts to.** The fix is established by the deterministic red/green tests,
which reproduce the defect directly, and the mechanism by the captured failure above. The rate of
the original flake depends too much on load to measure a before/after in isolation; the only loaded
comparison with discriminating power is the 5-module batch (10 clean replays against 2 failures in
4 before). No run of the corrected build failed or crashed: 150 runs of `...CancelUploadTests` and
50 of each of the other two cancel cases, 700 isolated and 100 loaded post-deletion runs, 10 batch
replays, and the full suites of both modules in all four Linux combinations. Windows and macOS have
not been run.

## Residual issues after the implementation

**2026-09-10, Fable 5.1, code analysis only.** The three diffs, the implementation log and the code
the fix's safety argument rests on were read; nothing was built or run. The verdict: the corrected
fix is right for everything the suite exercises, the SIGSEGV diagnosis of correction 3 is right, and
the deep copy is real — Boost's container clone copies every error-info entry
(`boost/exception/info.hpp:142-153` in the devenv7 dist's Boost 1.90.0, the one the build uses;
identical in 1.84), the copy is taken on the events
thread before the upstream task enhances its exception (the handler-macro catch at
`TaskBase.h:201-239` runs only when the rethrown `m_exception` leaves `run()`, which
`chk2WaitAllEvents2Flush()` allows only after `areAllEventsFlushed()`), and every exception built
with `BL_THROW` or the task handler macros is clone-enabled. All four corrections to the analysis
hold; correction 2 was traced independently: once `tryStopObservable()` has cleared the worker
queue, `getSuccessfulTopTask()` returns null before it reaches `chk2ThrowIfStopped()`, the unwind
ends cleanly, `run()` calls `notifyOnComplete()`, and only `chk2WaitAllEvents2Flush()` throws the
`operation_aborted` — the subscriber sees `onCompleted()` alone, so both arms of the cancel fixture
stay.

What remains is listed below in priority order. R-1 should be closed before the commit; the rest
can follow it or be deferred, as decided.

### R-1 — the nested exception chain is still shared between the two tasks (closed 2026-09-10, see the implementation below)

**What.** `copyForTarget()` deep-copies the top-level exception, but the container clone copies
each entry through `error_info_base::clone()`, and for `eh::errinfo_nested_exception_ptr` the entry
holds a `std::exception_ptr`. The clone's nested chain therefore refers to the very same nested
exception objects as the original's.

**Why it matters.** `eh::diagnostic_information()` (`ErrorHandling.h:502-525`) walks that chain: it
rethrows every nested pointer and formats the nested exception through Boost's formatter, which for
a `boost::exception` writes the mutable cached string inside that exception's container
(`error_info_container_impl::diagnostic_information( header )`, the same write which freed the
pointer under correction 3's backtrace). Each task dumps its exception at completion
(`chk2DumpException()`, `TaskBase.h:834-852`); the upstream and the downstream task complete on
different threads with no ordering between them; the copy was taken before either dump. So two
concurrent formatter calls land on one nested container — the crash of correction 3, one level
down. Only the top level is enhanced (`enhanceException()`, `TaskBase.h:926-940`), so the
map-insert half of the original race does not recur; the string-write half does.

**Reachability.** Not by the suite today: `ServerErrorException` and the `operation_aborted`
`SystemException` carry no nested pointer, which is why 700 post-deletion runs and 250 cancel runs
could not see it. In production, any upstream failure with a nested pointer, and the receiver has
one on a path this whole document is about: `ServerNoConnectionException` nests the last socket
error (`ChunksSendRecvBase.h:388-390`, the server going down mid-download, after the retry budget).
Other nesting sites in the library: `TcpBaseTasks.h:133`, `BackendProcessingBase.h:214`,
`ForwardingBackendProcessingFactory.h:200`, `ProxyDataChunkStorageImpl.h:245`. The harness has the
same exposure: a test which formats the reported exception with `eh::diagnostic_information()` (the
cancel fixture's catch arms do) races the other unit's completion dump. `TaskBase.h:662` is not a
case: the printable wrapper nests a non-boost original, which has no container.

**Fix, all inside `copyForTarget()`, after `clone()` has succeeded and before `rethrow()`:**

1. Cross-cast the clone to its `boost::exception` face:
   `const auto& copy = dynamic_cast< const eh::exception& >( *clone );` — the clone object is
   polymorphic, so the cast from `clone_base` works.
2. `const auto* nested = eh::get_error_info< eh::errinfo_nested_exception_ptr >( copy );`
3. When non-null, `nestedCopy = copyForTarget( *nested )` — the recursion copies the whole chain,
   its depth is the chain length (a cap of a few dozen levels which returns null is cheap
   insurance against a malformed chain). When `nestedCopy` is null, i.e. some link can be neither
   copied nor shared, return null for the whole copy, so `onError()` falls back to logging — the
   same rule already applied to a non-clonable top level. **Never store a null nested pointer:**
   the formatter rethrows each link through `safeRethrowException()`, which aborts the process on
   a null pointer (`CPP.h:646-660`).
4. Otherwise replace the entry on the clone's own container:
   `copy << eh::errinfo_nested_exception_ptr( nestedCopy );` — `operator<<` accepts a const
   reference because `boost::exception::data_` is mutable, and its enable-if holds for
   `boost::exception` itself.
5. Continue with `rethrow()` and `std::current_exception()` as now.

The entry must be replaced on the clone object *before* the rethrow. A mutation of the object
caught after `rethrow()` would not survive on MSVC, whose `rethrow_exception` and
`current_exception` copy the exception object; a `clone_impl` copy shares the container by
reference count, so a container replaced before the throw is what every copy carries. A non-boost
link comes back from `copyForTarget()` as the same pointer, which is safe: it has no container, and
a task wraps such an exception for printing rather than enhancing it (`TaskBase.h:622-672`).

**Test to add**, arm (13) of `Tasks_ReactiveInputConnectorTests` (`TestTasks5.h`):

- `inner`, a `BL_THROW` exception captured with `std::current_exception()`; `outer`, thrown as
  `BL_THROW( UnexpectedException() << eh::errinfo_nested_exception_ptr( inner ), ... )` — the
  pattern of `ChunksSendRecvBase.h:388-390`; dispatch `outer` through a connector with the
  recording dispatcher.
- The dispatched copy must carry a non-null nested pointer which differs from `inner`; enhancing
  the copy's nested exception, as arm (9) does for the top level, must leave `inner` without
  `errinfo_task_info`; and `eh::diagnostic_information( dispatched )` must still contain the inner
  message, proving the chain was copied and not dropped.
- Two sub-arms for the fallback rules: a chain whose inner link is a `std::runtime_error` must be
  dispatched with the inner pointer shared (`== inner`), and a chain whose inner link is a
  non-clonable boost exception (arm 12's class) must not be dispatched at all (`dispatchCount() == 0`).

**Verification when the change is made** (not run here): `utf_baselib_tasks` and
`utf_baselib_blobtransfer`, `-j1`, gcc and clang, debug and release; arm (13) red on the current
tree and green on the fix; the full suites of both modules; and the three cancel cases in the same
loops as the verification status above (150 / 50 / 50), since the crash of correction 3 was found
only by looping.

### R-2 — a non-clonable boost exception is swallowed silently (minimal option implemented 2026-09-10)

`copyForTarget()` returns null for a `boost::exception` which is not a `clone_base`, and
`onError()` then falls back to `ObserverBase::onError()` — for that class the connector behaves
exactly as before the fix, i.e. the original defect. It is reachable only by a hand-written `throw`
of a `boost::exception`-derived type outside `BL_THROW` and `boost::throw_exception`, which both
apply `enable_current_exception`; no product site is known.

- Minimal: an info-level `BL_LOG` in `onError()` before the fallback, saying the upstream error
  could not be copied for the target and is only logged. Info, not warning — `UtfMain` turns a
  warning into a test error. Arm (12) stays as it is.
- Optional: synthesize a replacement, `BL_MAKE_EXCEPTION_PTR( UnexpectedException(), ... )` with the
  original's `what()` in the message and its `errinfo_error_code` copied when present — but never
  the original as a nested pointer, which would re-share a container (R-1). If adopted, arm (12)
  changes to expect one dispatch of an `UnexpectedException`.

### R-3 — the copy does not name the unit which failed first (optional; deferred)

The copy keeps the upstream throw site, message and error info, but `enhanceException()` stamps
`errinfo_task_info` with the *downstream* task at its completion, so a reader of the downstream
failure sees the receiver's throw location under the unpackager's task name. Correct, and
sufficient to find the origin from the location; if the unit name is wanted as well, the place to
stamp it is `ObservableBase::notifyObserverError()`, which knows the failing observable, on the
original before delivery — sequentially safe, since the upstream task has not completed yet. Not
needed for this fix; recorded for the next time the diagnostics of the pipeline are touched.

### R-4 — small things in the change (done 2026-09-10)

- The arms of `Tasks_ReactiveInputConnectorTests` are numbered (9), (11), (12), (10) in file
  order; renumber in order, and arm (13) above goes at the end.
- `copyForTarget()` uses `boost::exception_detail::clone_base`, an implementation namespace. The
  public route, `boost::current_exception()` and `boost::rethrow_exception()`, clones the same way
  for clone-enabled exceptions but converts a non-clonable boost exception into a wrapper type
  instead of refusing it, so the explicit use is the right choice; a sentence in the comment
  saying so will save the next reader the question.

### R-5 — outstanding verification (still outstanding)

- Windows (`msvc` and `clang`) and macOS runs of `utf_baselib_tasks` and
  `utf_baselib_blobtransfer`, both variants, including the cancel loops. The MSVC copy semantics
  (`rethrow_exception` and `current_exception` copy the object; the container is shared by
  reference count among the copies and every temporary dies) are fine for the current code and for
  R-1 done as described.
- Optional, for a real control on the only discriminating measurement: the unfixed tree in a
  worktree (appendix procedure), the 5-module batch replayed 10 times unfixed against the 10 fixed
  replays already taken, clang release. Today the unfixed side is 2 failures in 4 runs.

### Implementation of R-1, R-2 and R-4 — 2026-09-10, Fable 5.1

Done in the working tree on top of Opus's change; nothing committed. R-3 stays deferred as written
above. R-5 stays outstanding: Windows and macOS were not run from here, and no stress loops were
run — the owner asked for focused builds and single suite runs of the affected modules only.

**Production, `InputConnector.h` (`copyForTarget()` and `onError()`):**

- `copyForTarget()` takes a depth (default 0) and returns null beyond
  `MAX_NESTED_EXCEPTIONS_DEPTH` (32). After a successful `clone()` it cross-casts the clone to
  `boost::exception`, reads its `errinfo_nested_exception_ptr`, copies that link recursively, and —
  on the clone object, before `rethrow()` — replaces the entry with the copy. A link which comes
  back null makes the whole copy null; a non-boost link comes back as itself and stays shared. The
  comment says why `clone_base` is used rather than `boost::current_exception()` (R-4).
- `onError()` logs one info line before falling back to `ObserverBase::onError()` when no copy
  can be made (R-2, the minimal option). Info, so the harness does not count it as a failure.

**Tests, `TestTasks5.h`, `Tasks_ReactiveInputConnectorTests`:**

- Arms renumbered into file order: the non-boost arm is (10), the non-clonable arm is (11), the
  no-dispatcher arm is (12) (R-4).
- New arm (13): a `BL_THROW` exception nesting another; the dispatched copy carries a non-null
  nested pointer different from the inner one, enhancing it leaves the inner exception without
  `errinfo_task_info`, and `eh::diagnostic_information()` of the copy still contains the inner
  message. Arm (14): a chain with a non-boost inner link is dispatched with that link shared.
  Arm (15): a chain with a non-clonable boost inner link is not dispatched at all.

**Red/green.** With the new arms in place and the connector untouched, gcc debug:
`Tasks_ReactiveInputConnectorTests` fails at `TestTasks5.h(1680)`, `critical check
( nestedCopy != inner ) has failed` — the shared link, exactly as R-1 describes. With the fix the
case passes, and the info line of R-2 appears exactly twice, for arms (11) and (15).

**Builds and suites**, `-j1`, one module at a time, each suite run once per combination:

| Combination | `utf_baselib_tasks` | `utf_baselib_blobtransfer` |
|---|---|---|
| `gcc1520` debug | 0 warnings; 104 cases, no errors | 0 warnings; 26 cases, no errors |
| `clang2010` release | 0 warnings; 104 cases, no errors | 0 warnings; 26 cases, no errors |

The tightened blobtransfer case and both connector cases ran in every one of the four runs.
`gcc1520` release and `clang2010` debug were not built. The Boost whose headers the build uses is
1.90.0 from the devenv7 dist; its container clone, `copy_boost_exception()` and the formatter's
cached string are as described under R-1 (checked in those headers, not only in the 1.84 tree
first consulted).

### R-6 — a null nested link aborts the process in `copyForTarget()` (fixed 2026-09-10, Opus 5)

**Found** reviewing the R-1 implementation, after `726e5ba`. `copyForTarget()` followed the nested
link without checking it for null: `copyForTarget( *nested, depth + 1U )` rethrows the link through
`cpp::safeRethrowException()`, which calls `BL_RIP_MSG` on a null `exception_ptr`
(`CPP.h:646-660`). An upstream exception carrying `errinfo_nested_exception_ptr` with a null
pointer therefore aborted the process inside the connector's `onError()`, on the events thread, at
every logging level.

**Severity.** Low — a null nested pointer is itself a defect at the throw site — but R-1 made it
worse than before. Before R-1 the same exception reached `ObserverBase::onError()`, whose
`utils::tryCatchLog( ..., LogFlags::DEBUG_ONLY )` formats it with `eh::diagnostic_information()`,
which does abort on a null link, but only when debug logging is enabled: `BL_LOG_MULTILINE`
evaluates its message only when its level is on (`Utils.h:425-433`). R-1 turned that debug-only
abort into an unconditional one.

**Fix.** `if( nested && *nested )`: a null link is not followed and stays in the copy as it is — it
shares no object, so there is nothing to copy. The only abort left on such an exception is the
library's existing one, a debug-level dump of the chain, which the upstream task's own completion
dump performs anyway. Both comments in `copyForTarget()` say so.

**Test.** Arm (16) of `Tasks_ReactiveInputConnectorTests`: an exception thrown with a null nested
pointer must be dispatched as a distinct copy which still carries the null link. Red on the
unguarded code, gcc debug: the test process aborts with exit 134, `RIP: ... CPP.h(650): Attempting
to rethrow a nullptr exception_ptr`. Green on the guarded code: the case passes, and R-2's info
line appears exactly twice, for arms (11) and (15) — arm (16) dispatches, so it adds none.

**Stress run of the whole tree** (`2bef17c`; gcc debug unless stated; `--log_level=test_suite`
except for the batch and the loaded runs):

| Check | Result |
|---|---|
| Both modules, all four Linux combinations: build and full suites | 0 warnings, 0 errors, every suite passing; the connector and the tightened cases ran in every one; R-2's info line twice in every tasks suite, never in a blobtransfer suite |
| `...CancelUploadTests` × 100 | 0 failures, 0 crashes |
| The three cancel cases × 50 | 0 failures, 0 crashes |
| The seven post-deletion cases × 100 each | 0 failures, 0 crashes |
| The 5-module batch × 10, clang release | every module of every replay passing |
| `...ReauthAfterDropOnLoadTests` × 100, one core kept busy | 0 failures, 0 crashes |

R-2's info line appeared in none of the 850 pipeline runs taken with the logging flag, so no real
pipeline error fell back to the swallow path. As before, no pipeline case sends a nested chain
through a connector, so R-1 and R-6 themselves are proven by arms (13) to (16), not by the stress
run.

## Do not

Do not quiet the failure by accepting `UnexpectedException` in the post-deletion assertion, by
retrying the case, or by softening the unpackager's completeness check. The check is correct and is
the last line between a partial tree and a reported success; what is wrong is that the error which
caused the incompleteness never reaches the unit whose report the harness shows.

## Side observations (out of scope, recorded so they are not lost)

- The receiver and the deleter request a `FlushPeerSessions` at the end of every run even when
  peer-session tracking is off (`ChunksReceiverDeleterBase.h:374-389` checks
  `m_sessionsFlushRequested` only), while the transmitter gates the same request on
  `m_peerId != uuids::nil()` (`ChunksTransmitter.h:218-223`). Harmless, but it is a second
  transmitter/receiver asymmetry next to the one the first version of this document found.
- `tidyUpTransferTaskOnFailure()` clears the chunk id of a failed authentication task only because
  `onTaskStoppedNothrow()` in `TcpBlockTransferClient.h` has already reset the command id to
  `NoCommand`; `setCommandInfoStateInternal()` forces `chunkIdDefault()` for a non-Normal block
  type under any other command id. If that reset ever became conditional — its own comment says
  the command id is meant to be *kept* on failure — a failed re-authentication would postpone the
  authentication block as if it were a chunk. Worth an assertion when the transfer code is next
  touched.
- The fault fixture's `dropAllConnections()` runs on the storage thread inside `load()`, and the
  server connection's `cancelTask()` releases the async operation which is executing at that
  moment. The executor's contract (`AsyncExecutorImpl.h:1208-1243`) says a released operation must
  have no call pending; the test passes, so the cancel path evidently satisfies it, but the fixture
  exercises an edge of that contract which a reviewer of the fault injection should know about.

---

## Appendix — how the measurements were taken

- Isolated worktree at `27e337f` via `git worktree add --detach`; the gitignored
  `projects/make/ci-init-env.mk` has to be copied into it by hand or the build stops at
  `common.mk:8`.
- Builds `-j1`, one combination at a time; tests at most 5 modules concurrently, per `AGENTS.md`.
- On a 2-core host the 5-way test concurrency is itself a load source. Any timing-sensitive failure
  seen in the matrix should be re-measured in isolation before it is characterized, which is how
  both issues here were separated.
