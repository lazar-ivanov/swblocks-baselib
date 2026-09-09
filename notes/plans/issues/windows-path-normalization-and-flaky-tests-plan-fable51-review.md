# Review of `windows-path-normalization-and-flaky-tests-plan.md`

**Date:** 2026-09-09
**Reviewer:** Fable 5.1
**Reviewed document:** `notes/plans/issues/windows-path-normalization-and-flaky-tests-plan.md`
(Opus, 2026-09-09, status Proposed; nothing implemented except the `TestAsyncCB.h` `static const`
fix already in the tree).
**Method:** every claim checked against the current tree read-only; no builds were run. Three
fan-out searches covered: every in-place mutator on `fs::path` that bypasses `chk2AddPrefix`; every
place a forward-slash `fs::path` string is expected, compared or serialized; and the HTTP failure
diagnosis. The Boost 1.90 sources in the devenv7 dist were read for `make_preferred()`,
`operator/` and path comparison.
**Disposition:** accepted by the owner on 2026-09-09; the plan was amended in place per the
findings below. Implementation had not started at the time of writing.

---

## Verdict

- **Change 1 (normalize in `chk2AddPrefix`) is the right fix at the right layer.** Approve, with
  one placement precision, two existing assertions that will go red and are not in the plan, one
  Boost-version dependency to record, and a corrected residual note.
- **Change 2 (bounded poll) is right, but the flag must be `std::atomic< bool >`** or the Linux
  ThreadSanitizer run will report it.
- **Change 3 (absorb the teardown warning) should not be implemented as written.** Its diagnosis
  contradicts two facts in the code, and the mitigation would hide what is probably a lingering
  object or handle on the cancel path while leaking temp directories on disk.
- **Change 4 is fine.** Keep it.
- **The HTTP deferral record must not assert the IPv4/IPv6 root cause.** The code refutes it on
  every load-bearing point (finding 6). Record the evidence and mark the cause unknown.

---

## Findings that must change the plan

### 1. Change 1: `make_preferred()` must precede the `pathStr` capture, not just the `is_absolute()` test

The plan anchors the insertion "before the `is_absolute() || starts_with` test (~line 1154)". The
function captures `const auto pathStr = path.string();` at
`src/include/baselib/core/detail/OSImplPlatformCommon.h:1147`, and `pathStr` is what the
already-prefixed early-out tests. If the call lands between 1147 and 1154, an input such as
`//?/C:/x` fails the `starts_with` test on the *old* string, enters the prefixing branch with the
*new* string `\\?\C:\x`, whose `psz[2] == '?'` satisfies the UNC test at line 1174, and comes out
as `\\?\UNC\?\C:\x`. Place the call as the first statement inside `if( ! path.empty() )`, before
line 1147, and say so in the plan.

Verified facts the plan relies on, all true today: binary `operator/` returns
`boost::filesystem::path` by value (`boost/filesystem/path.hpp:1361-1375` in the dist source
tree); `PathImplT< false >` never references `WinLfnUtils`; the typedef at
`OSImplPlatformCommon.h:1531` selects on `os::isWindows`, a compile-time constant; `fs::normalize`
cannot recurse into it (its `make_preferred()` at `FsUtils.h:802` is on a raw Boost path and the
Boost body constructs nothing).

**One dependency the plan should record.** Boost 1.90 ships two `make_preferred()` bodies and
picks one by `BOOST_FILESYSTEM_VERSION` (`path.hpp:1621-1627`):

- `make_preferred_v3` — `std::replace` over the whole string
  (`libs/filesystem/src/path.cpp:461-464`). This is what consumers get: `config.hpp:27-32`
  defaults consumers to version 3 (only the library's own sources build as 4), and nothing in this
  repo defines `BOOST_FILESYSTEM_VERSION`.
- `make_preferred_v4` — **skips the root name** (`path.cpp:466-478`, "Avoid converting slashes in
  the root name"), so `//server/share` becomes `//server\share`, the UNC test at line 1174 still
  fails, and the result is `\\?\//server\share`.

So the "second defect fixed incidentally" claim and the proposed
`cbAddPrefix( "//server/share" )` case hold **under v3 only**. Put that in the "Why
`make_preferred()`" paragraph, or make the UNC detection at line 1174 accept a forward-slash root
too; otherwise the day someone sets `BOOST_FILESYSTEM_VERSION=4` the test fails for a reason nobody
will remember.

### 2. Change 1 breaks two existing assertions the plan does not mention

Both are cross-platform, unguarded, and pass today only because a relative `fs::path` keeps its
slashes on Windows.

**(a)** `src/utests/utf_baselib/TestBaselibDefault.h:7759`

```cpp
cbCheckRemovePrefix( "relative/path", "relative/path" );
```

It sits *above* the `if( ! bl::os::onWindows() ) return;` guard at line 7763. The lambda
constructs `bl::fs::path( input )` first (line 7728), which after Change 1 yields `relative\path`,
so the `UTF_REQUIRE_EQUAL` against the literal fails. The plan's own new case
(`cbAddPrefix( "relative/path" )` → `relative\path`) asserts the behaviour this line contradicts.
Fix: compare against `bl::fs::path( expected ).string()` rather than the raw literal, or move the
case below the Windows guard with a `\` expectation.

**(b)** `src/utests/utf_baselib_data/TestFilesystemMetadataInMemory.h:243`

```cpp
UTF_REQUIRE_EQUAL( entryInfo.relPath -> value().string(), posExpected -> second );
```

with expectations `"foo/bar1/baz"` and `"foo/bar/baz2"` (lines 149-153) and entries built by
`bl::fs::path path( relPath ); info.relPath -> lvalue().swap( path );` (lines 137-138) — the
constructor under change. The file has no platform guards. Same fix shape. Note that
`utf_baselib_data` is **not** in the plan's targeted-regression list (step 3); only the full
19-module matrix in step 1 would have caught it.

The neighbouring `cbCheckRoundTrip` cases at 7874-7875 and the whole `cbAddPrefix` block at
7823-7876 contain no forward slash and do not conflict; the plan's three additions are all new.

### 3. The residual paragraph is inaccurate; the real residual is `auto` deduction and `swap`

The plan says exactly four `/=` sites exist and all are on `fs::path`. Neither half is right:

- `FsUtils.h:792` and `FilesUnpackagerUnit.h:1140` operate on `auto`-deduced
  `boost::filesystem::path` values (`auto result = * it++;` at 767, `auto resolved =
  symlinkPath.parent_path();` at 1117), not on `fs::path`.
- Eight further `/=` sites (`PathSpaceUsed.h:277`, `TestFsUtils.h:488,492`, `UtfMain.h:233,236,239`,
  `TestBaselibDefault.h:3534,4042`), two `+=` sites (`FilesUnpackagerUnit.h:805`, `FsUtils.h:640`)
  and three `swap()` splices of unconverted Boost results into `fs::path` (`FsUtils.h:1371`,
  `FilesUnpackagerUnit.h:1063`, `PathUtils.h:152`) were missed. Nothing in `src/` calls
  `append`, `concat`, `assign`, `replace_extension`, `replace_filename` or `remove_filename` on a
  path.

Every one of those appends a single iterated component, a UUID, or a literal without a separator,
so **the conclusion (theoretical, not live) stands** — but the record should describe the gap
correctly: `auto x = fsPath / rhs` deduces `boost::filesystem::path` and stays un-normalized until
converted, and `swap()` is a fourth entry point besides constructors and assignment. The sites
that matter:

- `src/include/baselib/jni/JvmHelpers.h:46` → `:63`: `basePath / jarFileName` with a
  caller-supplied `jarFileName`; `.string()` reaches `normalizePathCliParameter` without ever
  becoming an `fs::path`. Harmless (Java accepts mixed separators), but it is the one library site
  where a caller's `/` survives Change 1.
- `src/include/baselib/transfer/FilesUnpackagerUnit.h:1117-1146`: the `StpContained` symlink
  containment check is a raw string prefix compare between a Boost-built `resolved` and
  `m_targetTmpDir.string()`. It is safe (the `..` guard at 1130 refuses to pop past the root) and
  Change 1 makes the two spellings agree rather than disagree — today a `/`-spelled target
  directory rejects every contained link. Worth a sentence because it is the security-sensitive
  consumer of separator spelling.
- `src/include/baselib/core/PathUtils.h:142-152` (`getRelativePath`): the result is built with
  `rp /= …` and delivered by `rp.swap( relPath )`, so it never passes `chk2AddPrefix`. It is
  already `\`-separated on Windows because Boost's `operator/=` uses the preferred separator, so
  no behaviour changes — but the plan's "every constructor and `operator=` funnels through it" is
  not the whole story.

Also note the escape hatch `bl::fs::nolfn::path` (`OSImplPlatformCommon.h:1045`; used by
`UtfDirectoryFixture.h`, `TestUtils.h:69`, `PathRemove.h:70`) is a raw Boost path and is untouched
by Change 1 — the tool for a test that must keep forward slashes.

### 4. Change 2: poll an atomic, not a plain `bool`

`TaskLifetimeProbeT::~TaskLifetimeProbeT` writes `*m_destroyed = true` on whichever worker thread
drops the last reference (`TestTasks7.h:86-89`); the proposed helper reads it from the test thread
with only `os::sleep` between reads. That is a data race in the language sense. The precedent the
plan cites (`TestTasks6.h:600-610`) polls `Task::getState()`, which is lock-protected, so it is not
the same idiom. This project runs ThreadSanitizer on Linux
(`notes/plans/issues/async-executor-tsan-inversion-record.md`), and Changes 2 and 3 are
cross-platform test code, so a plain-`bool` poll will be reported there.

Fix: make `m_destroyed` a `std::atomic< bool >*` and the four `bool destroyed` locals
(`TestTasks7.h:192, 1288, 1393, 1434`) `std::atomic< bool >`; the file already uses
`std::atomic< bool >` at lines 528 and 598. The helper takes `const std::atomic< bool >&`. The
reads at 208/212 and 1288-1310 happen on the releasing thread or behind
`settledOutstandingObjectRefs`, so only the type changes there. Diff stays small and each
assertion site stays a single `UTF_REQUIRE`.

The race analysis itself is correct: `ExecutionQueueImpl.h:692-701` binds
`om::ObjPtrCopyable< Task >` into the ready callback and `TaskBase.h:705-711` invokes the swapped
copy after releasing the lock, so `flush()` can return while the functor on the worker's stack
still owns a reference.

### 5. Change 3: the diagnosis does not fit the code; do not absorb the warning yet

Two facts contradict "TmpDir's destructor runs while a worker still holds a read handle":

- `safeDeletePathNothrow` → `trySafeRemoveAll` → `trySafeRemove` → `trySafeRemoveInternal` →
  `trySafeFileOperation`, which on Windows retries **20 × 100 ms** before giving up
  (`FsUtils.h:55-91`, `:126-131`). The handle was therefore held for more than two seconds after
  the test unwound, not for the tail of a `notifyReady` call.
- The pipeline runs under `scheduleAndExecuteInParallel( ..., eqTopLevel )`, whose catch block
  calls `eq -> forceFlushNoThrow()` (wait = true, cancelExecuting = true) and asserts the queue is
  empty before rethrowing (`Algorithms.h:78-94`, `ExecutionQueueImpl.h:1420-1438`). Every
  top-level unit task has finished before `executeTheFilesPackagerAndTransmitterPipelineInternal`
  unwinds and destroys `tmpDir` (`TestBlobTransferUtils.h:803-815`).

Whatever holds the input file — or the directory: a live `directory_iterator` from the scanner
also blocks `RemoveDirectory` with a sharing violation — outlives the queue by seconds, most
plausibly an object retained on the cancel path. This fixture had exactly such a retention fixed
two days ago (`blobtransfer-utest-object-leak-record.md`). Pushing `warningToDebugLineLogger` for
the whole eight-iteration case would (a) hide that, (b) hide any other warning the case ever
emits, and (c) leave `bl-temp-dir-*` directories in `%TEMP%` on every lost race. The
`TestBaselibDefault4.h:200` precedent is a deterministic single-FIFO case; it does not carry over.

Replace Change 3 with a diagnosis step: rerun the case on `vc143` and capture the actual warning
(which path, and the error code — 32 `ERROR_SHARING_VIOLATION` vs 5 `ERROR_ACCESS_DENIED` point at
different holders) plus the outstanding-object count at teardown. If a holder is found, fix it
where the evidence says. Only if the holder is provably a bounded in-flight read should the logger
pusher be used, scoped as narrowly as possible, with the on-disk leak stated in the record.

### 6. HTTP deferral: the IPv4/IPv6 root cause is refuted by the code; record evidence, not theory

**Correction (2026-09-09, later the same day):** this finding read the plan's "failed 52/56" as 52
failures. The run was **52 passed, 4 failed** — `BaseLib_HttpServerPerfTest`,
`TlsHandshake_NameMismatchIsReportedThroughErrorInfo`,
`TlsHandshake_AllowUntrustedRecordsAndClearsEndpointInfo`, `TlsHandshake_SniOmittedForAddressLiterals`,
all of which open sockets. The "19 socket-free cases" argument below is therefore void and is struck;
the other three arguments stand on their own. The later trace of the four cases is in
`windows-blobtransfer-cancel-handle-and-http-reset-flakes-plan.md`, section B.

The plan's "Records to write" bullet asserts an acceptor-is-IPv4-only / client-resolves-`::1`
mechanism. The code says otherwise on every load-bearing point:

- The path it cites, `src/include/baselib/http/HttpServerHelpers.h`, does not exist; the bind is
  at `src/utests/include/utests/baselib/HttpServerHelpers.h:363-382` (same literal in
  `TestHttpServer.h` ×5 and `TestRestUtils.h:503`). It binds the numeric literal `"0.0.0.0"`,
  which resolves to exactly one endpoint, so `TcpBaseTasks.h:1194-1214` taking the first one
  discards nothing.
- The client tries **every** resolved endpoint: `TcpBaseTasks.h:1372-1381` uses the range form of
  `async_connect` through the repo's shim (`BoostAsioCompat.h:389-411`), which falls through to
  `127.0.0.1` after `::1` is refused. A refused `::1` yields `10061`, is swallowed, and never
  surfaces.
- The readiness probe `waitForAcceptorReady` (`TestTaskUtils.h:717-772`) connects with the same
  connector to the same `localhost` before any case runs. If the theory held, every case would
  fail there, deterministically, on both toolchains, with "The acceptor did not become ready".
- ~~19 of the 56 cases (`TestTlsProtocolPolicy.h`, `TestTlsPeerVerification.h`,
  `TestAsioSslStreamWrapper.h`) open no socket at all. "52 of 56 failed" therefore includes at
  least 15 in-process certificate/policy cases, which no connectivity defect can explain.~~
  (Struck — see the correction above; only 4 cases failed and all of them open sockets.)
- `system:10054` (`WSAECONNRESET`) means a peer accepted and then reset, or a listening backlog
  overflowed; "nothing listening on `::1`" produces `10061`.

Where the idea came from: `TcpBaseTasks.h:727-735` decorates every client exception with the
**first** resolved endpoint (`m_endpoint`, set at line 785), so on this host every `localhost`
failure is labelled `::1` whatever actually failed. That is already a recorded finding
(`notes/reviews/major/update_2026/whole-library-cxx-review-fable51.md:297`).

Candidates that do fit "vc143-debug fails, ccl16-release passes, 10054, 52/56", for the record to
list as **unconfirmed**: (1) process-wide state leaking across cases —
`allowUntrustedCertificates()`, the untrusted-endpoints map, the global log level pushed by
`SuppressExpectedWarningsScope` (`TestTlsHandshakeVerification.h:121-134`) — the only class that
reaches the 19 socket-free cases; (2) warning-as-failure amplification: `UtfMain.h:124-126` fails a
case on any `LL_WARNING`, and `tryCatchLog` defaults to `Logging::warning()` around per-connection
setup at `TcpBaseTasks.h:1125-1131`; (3) short wall-clock deadlines against a slow debug build —
the 3-second server-side TLS handshake deadline at `TestHttpServer.h:2065` is the textbook producer
of a client-side 10054, and `TestClientHttpTasks.h:2323/2339` has a one-second margin; (4) port
28100 hijack under Windows `SO_REUSEADDR` (`TcpBaseTasks.h:1205`) by a leftover exe or a
concurrently running module (every server module defaults to 28100, `UtfArgsParser.h:46`; the
project rules permit five modules at once) — the bind succeeds silently and every request resets;
(5) `BL_ASSERT` is live only in debug (`BaseDefs.h:63`, `NDEBUG` release-only).

Keep the two true facts: the default certificate has DNS SANs only and
`TestTlsHandshakeVerification.h:349-360` pins that `127.0.0.1` must fail — but the module already
ships a certificate with `IP:127.0.0.1` / `IP:::1` SANs (`UtfCrypto.h:139-155`), so the constraint
is narrower than "the peer name cannot be changed".

The record should carry: the exact failing case names, the first failing message per case, whether
the readiness probe passed, whether another `utf_*` process was running, and the
`netstat -ano -p TCP` state for 281xx at the time — and it should say the root cause is unknown.

### 7. Release note: say precisely what becomes visible

"`fs::path` now normalizes `/` to `\` on construction" understates what was already true and
overstates the new part. Absolute paths already came back prefixed and different from the input;
what changes is that **relative** paths and **already-prefixed** paths also come back with `\`.
Concretely visible on Windows after Change 1:

- `.string()` of a relative `fs::path` (the two assertions in finding 2 are the in-tree examples).
- Every filesystem error message, via `normalizePathParameterForPrint`
  (`OSImplPlatformCommon.h:2393-2399`).
- The `.symlink` placeholder file the unpackager writes on Windows carries the target as text
  (`FilesUnpackagerUnit.h:809`): `../d/sub` becomes `..\d\sub`. A persisted artifact.
- `PluginAccess::getLibrary()` returns `\` for a `/`-spelled registration (`PluginAccess.h:89`).
- `//server/share` is now recognised as UNC (v3 only, finding 1); the `isValidPath` row
  `TestBaselibDefault.h:4944` is the one existing test whose prefixing branch changes (it should
  still pass — `UNC` is a valid name).

Mention `bl::fs::nolfn::path` as the way to keep a raw Boost path.

### 8. Verification section: corrections

- 19 `utf_baselib*` modules confirmed under `src/utests/`.
- Add `utf_baselib_data` (finding 2b) and `utf_baselib_blobtransfer` (the end-to-end coverage) to
  step 3's targeted list; the original names only `utf_baselib`, `_io`, `_loader`, `_jni`.
- Line endings: step 5's direction is backwards for this session. `git ls-files --eol` shows the
  index is LF for all touched files while the working tree is *mixed* (`TestAsyncCB.h` and
  `OSImplPlatformCommon.h` LF; `TestTasks7.h`, `TestBlobTransferFilesystem.h`,
  `TestBaselibDefault.h` CRLF). Commits are LF either way under `core.autocrlf=true`; the check is
  about on-disk consistency, and `git ls-files --eol` shows both sides in one command.
- Add to step 3's watch list: `TestBaselibDefault.h:7759`, `TestFilesystemMetadataInMemory.h:243`,
  the `isValidPath` row at `TestBaselibDefault.h:4944`, and the two forward-slash roots at
  `TestBaselibDefault.h:4694/4718` (internal representation changes; element walk should not).

---

## Additional problems the plan should record (not fix here)

- **Mirror direction, Windows → Linux, is not covered.** `getRelativePath` builds with Boost's
  `operator/=`, so a Windows packaging host stores `d\f.bin`; on Linux that is one legal filename,
  and `chkEntryInfo` (`FilesystemMetadataInMemoryImpl.h:311-348`) validates by element, so it is
  accepted and unpacked as a single file named `d\f.bin`. Nothing in the repo uses
  `generic_string()` (zero hits), and no in-tree serializer for `relPath` exists — the store is
  in-memory — so the portable boundary is whatever out-of-tree store persists the metadata. Change 1
  fixes only the Linux → Windows direction; the plan's framing ("the module's whole purpose")
  should say so.
- **Further defects Change 1 fixes incidentally** — belong in the plan's Context as evidence for
  the layer choice: `copyDirectoryWithContents` strips the source prefix by raw string replace
  (`FsUtils.h:994-1011`), so a `/`-spelled `sourceDir` silently copies to the wrong target today;
  `createJunction` stores the target text verbatim in the reparse point
  (`OSImplWindows.h:2496-2531`); the `StpContained` prefix compare (finding 3); the
  timestamp-restore map keyed on `.string()` in `FilesUnpackagerUnit.h:279-298`, whose two branches
  spell keys differently until every path passes the constructor.
- **Per-call-site normalizations that become redundant** (leave them; harmless):
  `OSImplWindows.h:3565-3573`, `PathSpaceUsed.h:149-151`, `JavaVirtualMachine.h:233-235`,
  `temp_directory_path` at `OSImplPlatformCommon.h:2258/2274`, and `fs::normalize`'s own call at
  `FsUtils.h:802`. The first two are the third and fourth instances of the pattern the plan's
  Context cites as evidence.
