# Windows LFN path normalization, plus two flaky-test repairs: Implementation Plan

**Date:** 2026-09-09
**Status:** **Proposed** — approved in outline, not yet implemented.
**Reviewed:** 2026-09-09 by Fable 5.1 — findings and evidence in
`windows-path-normalization-and-flaky-tests-plan-fable51-review.md`. This document is amended per
that review: Change 1 gained a placement precision, two existing assertions to update and a Boost
version dependency; Change 2 requires an atomic flag; Change 3 was downgraded to diagnose-first;
the HTTP root cause was withdrawn.
**Found:** while doing focused testing of the eight test modules affected by commit `79488fa`
("fable review and test enhancement plan residual issues - macos fixes") on the devenv7 Windows
host, branch `lazari2`.
**Scope decisions taken by the owner, 2026-09-09:** fix the path defect centrally in `PathImplT`
rather than narrowly in the unpackager or in the tests; fix the two cheap test-only flaky failures
(the second, Change 3, was later downgraded to diagnose-first — see below); record the HTTP one
rather than fix it (its IPv4/IPv6 explanation was withdrawn — see Records).

---

## Context

The `79488fa` pull introduced **no regressions**. Its one genuinely cross-platform fix — the timer
task cancel race in `TaskBase.h` — is proven on Windows: the new
`Tasks_TimerTaskCancelBeforeStartTests` passes 3/3 on `vc143` debug and `ccl16` release. The rest of
that commit is macOS/POSIX-only and does not execute here.

What the testing did surface is four **pre-existing** Windows defects, none previously exercised
because these modules had never been built or run on Windows.

The largest is not a test artefact. `WinLfnUtils::chk2AddPrefix` prepends the long-file-name prefix
`\\?\` **without converting `/` to `\`**, and `\\?\` *disables Windows path normalization* — so a
forward slash under the prefix is an illegal character. `FilesUnpackagerUnit` joins the relative path
stored in the package metadata onto the target directory, and that relative path is produced by
`fs::getRelativePath` on the **packaging** host. So a blob package created on Linux (which stores
`d/f.bin`) **cannot be unpacked on Windows** — which is the entire purpose of the module. The four
failing `BlobTransfer_Unpackager*` cases are correct; they faithfully model a UNIX-produced package.

Four facts settle the layer choice:

- The same defect already bit production once, via `JAVA_HOME` under MSYS, and was patched at a
  single call site — documented in `scripts/devenv7/AGENTS.md:423`.
- The library already normalizes **before** prefixing in `fs::temp_directory_path()`
  (`OSImplPlatformCommon.h:2247-2258`), with a comment saying exactly why. The correct ordering
  already exists; it is simply not universal. Today the convention is "every call site must
  remember".
- Two more per-call-site patches of the same defect exist: `OSImplWindows.h:3565-3573` copies the
  path and calls `make_preferred()` before `native()` reaches a Win32 API, and
  `src/local/apps/bl-tool/commands/PathSpaceUsed.h:149-151` does the same on a user-supplied path.
- Other consumers of separator spelling are silently wrong today for a `/`-spelled input and are
  fixed by the same change: `copyDirectoryWithContents` strips the source prefix by raw string
  replace (`FsUtils.h:994-1011`), so the strip is a no-op and files land in the wrong place;
  `createJunction` stores the target text verbatim in the reparse point
  (`OSImplWindows.h:2496-2531`); the `StpContained` symlink containment check compares raw
  strings (`FilesUnpackagerUnit.h:1143-1146`) and rejects every contained link; and the
  timestamp-restore map in `FilesUnpackagerUnit.h:279-298` keys on `.string()` with its two
  branches spelling keys differently.

`fs::path` is the library's own type and it unconditionally applies a prefix that makes forward
slashes illegal. The type creates the hazard, so the type should discharge it.

Note the fix is one-directional. `fs::getRelativePath` builds its result with Boost's
`operator/=` (`PathUtils.h:142-148`), so a package produced **on Windows** stores `d\f.bin`; on
Linux that is one legal filename, `chkEntryInfo` validates by element
(`FilesystemMetadataInMemoryImpl.h:311-348`) and accepts it, and the unpackager creates a single
file named `d\f.bin`. Nothing in the repo uses `generic_string()` and no in-tree serializer for
`relPath` exists (the store is in-memory), so the portable boundary is whatever out-of-tree store
persists the metadata. That direction is recorded, not fixed (see Out of scope).

---

## Change 1 — normalize separators in `chk2AddPrefix` (production)

**File:** `src/include/baselib/core/detail/OSImplPlatformCommon.h`,
`WinLfnUtilsT<>::chk2AddPrefix` (~line 1141).

Call `path.make_preferred()` as the **first statement inside `if( ! path.empty() )`** — before the
`const auto pathStr = path.string();` capture at ~line 1147, and therefore also before the
`is_absolute() || starts_with( g_lfnPrefix )` test (~line 1154), which reads `pathStr`.

**The placement is load-bearing.** The failing unpackager case is
`alreadyPrefixedPath / "d/sub"`, which re-enters `chk2AddPrefix` and takes the *already-prefixed*
early-out at ~line 1161. A fix confined to the prefixing branch would repair `fs::path( "C:/foo" )`
but would fix **none** of the observed failures.

**Before the `pathStr` capture, specifically.** If the call lands between lines 1147 and 1154, an
input such as `//?/C:/x` fails the `starts_with` test on the old string, enters the prefixing
branch with the new string `\\?\C:\x`, whose `psz[2] == '?'` satisfies the UNC test at ~line 1174,
and comes out as `\\?\UNC\?\C:\x`.

**Why this function.** Every `PathImplT< true >` constructor and `operator=` funnels through it
(`OSImplPlatformCommon.h:1256-1411`), and `PathImplT` defines no `operator/` of its own — the binary
`operator/` yields a `bfs::path` which converts back through a constructor. `PathImplT< false >`
(POSIX) never calls `chk2AddPrefix`, so the change is **Windows-only by construction** — which
matters, because backslash is a legal filename character on POSIX.

**Why `make_preferred()`.** On Windows Boost 1.90 implements it as a `std::replace` over the
internal string (`make_preferred_v3`, `libs/filesystem/src/path.cpp:461-464` in the dist source
tree): cheap, non-recursive (so no cycle with `fs::normalize`, which itself constructs `fs::path`),
and it leaves the all-backslash `\\?\` and `\\?\UNC\` prefixes untouched.

**Dependency to remember.** That body is selected by `BOOST_FILESYSTEM_VERSION`, which
`boost/filesystem/config.hpp:27-32` defaults to **3** for consumers (only the library's own sources
build as 4), and nothing in this repo defines it. The v4 body (`path.cpp:466-478`) deliberately
**skips the root name**, so under v4 `//server/share` would become `//server\share`, the UNC test
below would still miss it, and the `//server/share` test case below would fail. If the build ever
moves to v4, either extend the UNC detection at ~line 1174 to accept a forward-slash root or
retire that case knowingly.

**A second defect is fixed incidentally (under v3).** The UNC detection at ~line 1174 tests only
for `L'\\'`, so `//server/share` is absolute but unrecognised and becomes `\\?\//server/share`.
After normalization it is detected correctly.

**Deliberately NOT doing — overriding the inherited mutators on `PathImplT< true >`.** Three
things bypass `chk2AddPrefix`: the in-place mutators inherited from Boost (`/=`, `+=`, `swap`),
and `auto x = fsPath / rhs`, which deduces `boost::filesystem::path` and stays un-normalized until
converted. The in-tree inventory (2026-09-09 review): twelve `/=` sites (`PathUtils.h:142,148`,
`PathSpaceUsed.h:277`, `TestFsUtils.h:488,492`, `UtfMain.h:233,236,239`,
`TestBaselibDefault.h:3534,4042`, plus `FsUtils.h:792` and `FilesUnpackagerUnit.h:1140`, which are
on `auto`-deduced Boost paths), two `+=` sites (`FilesUnpackagerUnit.h:805`, `FsUtils.h:640`) and
three `swap()` splices (`FsUtils.h:1371`, `FilesUnpackagerUnit.h:1063`, `PathUtils.h:152`). Every
operand is a single iterated component, a UUID or a separator-free literal, so the gap is
theoretical, not live. Two sites are worth naming in the record: `JvmHelpers.h:46` → `:63`
(`basePath / jarFileName` with a caller-supplied name, `.string()` handed to
`normalizePathCliParameter` without ever becoming an `fs::path` — harmless, Java accepts mixed
separators) and the `StpContained` containment check at `FilesUnpackagerUnit.h:1117-1146`, a raw
string prefix compare which is safe because the `..` guard at 1130 never pops past the root and
whose two sides now agree in spelling. `getRelativePath` delivers its result by `swap`
(`PathUtils.h:152`) and so never passes `chk2AddPrefix` at all; it is `\`-separated on Windows
regardless because Boost's `operator/=` uses the preferred separator. Do not widen the change.

`bl::fs::nolfn::path` (`OSImplPlatformCommon.h:1045`; used by `UtfDirectoryFixture.h`,
`TestUtils.h:69`, `PathRemove.h:70`) is a raw Boost path and is untouched by this change — it is
the tool for a test that must keep forward slashes.

**Tests.** Extend the existing boundary case at
`src/utests/utf_baselib/TestBaselibDefault.h:7823-7876`, which today covers only backslash inputs:

- `cbAddPrefix( "c:/foo" )` → `\\?\c:\foo`
- `cbAddPrefix( "//server/share" )` → `\\?\UNC\server\share`
- `cbAddPrefix( "relative/path" )` → `relative\path`

The four `utf_baselib_blobtransfer` cases become the end-to-end coverage. **Leave their `"d/sub"`,
`"d/f.bin"`, `"a/b/c"` and `"d/empty.bin"` literals exactly as they are** — rewriting them to
backslashes would hide the defect rather than fix it.

**Two existing assertions flip on Windows and must be updated in the same change.** Both are
cross-platform and unguarded, and pass today only because a relative `fs::path` keeps its slashes
on Windows:

- `src/utests/utf_baselib/TestBaselibDefault.h:7759` —
  `cbCheckRemovePrefix( "relative/path", "relative/path" )` sits *above* the
  `if( ! bl::os::onWindows() ) return;` guard at 7763, and its lambda constructs
  `bl::fs::path( input )` first (7728), so the expected literal must become
  `bl::fs::path( expected ).string()` (or the case moves below the guard with a `\` expectation).
- `src/utests/utf_baselib_data/TestFilesystemMetadataInMemory.h:243` —
  `UTF_REQUIRE_EQUAL( entryInfo.relPath -> value().string(), posExpected -> second )` against
  `"foo/bar1/baz"` / `"foo/bar/baz2"` (149-153); `makeEntry` builds the entry through
  `bl::fs::path path( relPath )` (137-138). Same fix shape. This is `utf_baselib_data`, not one of
  the eight modules of the original round.

Nothing in the `cbAddPrefix` block at 7823-7876 or the `cbCheckRoundTrip` cases at 7874-7875
contains a forward slash, so the three additions above are all new. Rows to watch rather than
change: the `isValidPath` row `TestBaselibDefault.h:4944` (`//HOST-123/share$/...` is the one
existing input whose prefixing branch changes — it becomes UNC-detected and should still be valid)
and the forward-slash roots at `TestBaselibDefault.h:4694/4718` (internal representation changes,
the element walk should not).

---

## Change 2 — settle before asserting destruction (test only)

**File:** `src/utests/utf_baselib_tasks/TestTasks7.h`,
`Tasks_ExecutionQueueOwnershipCycleTests`, arms 3 and 4 — the `UTF_REQUIRE( destroyed )` at ~1419
and ~1457. Measured **3 failures in 15 runs**, hitting either arm, on both toolchains.

The test asserts *synchronous* destruction. But the queue's completion callback captures a strong
`om::ObjPtrCopyable< Task >` (`ExecutionQueueImpl.h:692-701`), and `TaskBase.h:698-713` swaps it into
a local and invokes it **after** releasing the task lock — so `flush()` / `disposeQueue()` can return
while a worker thread still holds a reference on its stack. The production behaviour is correct; the
test's expectation was not.

Reuse the bounded-poll shape already present in this file group at `TestTasks6.h:592-612`
(1000 × 10 ms, with the comment "the sleep below is the polling interval of a bounded loop, not a
timing assumption") — but note that precedent polls `Task::getState()`, which is lock-protected,
whereas the destructor flag here is written by whichever worker thread drops the last reference
(`TestTasks7.h:86-89`). Polling a plain `bool` across threads is a data race and the Linux
ThreadSanitizer run (`async-executor-tsan-inversion-record.md`) will report it. So: change
`TaskLifetimeProbeT::m_destroyed` to `std::atomic< bool >*`, the four `bool destroyed` locals
(`TestTasks7.h:192, 1288, 1393, 1434`) to `std::atomic< bool >` (the file already uses it at 528
and 598), and add a small file-local helper taking `const std::atomic< bool >&` alongside
`waitForOutstandingObjectRefs`. Use it at both sites, so each assertion stays a single
`UTF_REQUIRE` and the failure message is unchanged. The reads at 208/212 and 1288-1310 happen on
the releasing thread or behind `settledOutstandingObjectRefs`, so only the type changes there.

---

## Change 3 — the cancelled block reader keeps the input file open (production; was "absorb the warning")

**Superseded 2026-09-09 by `windows-blobtransfer-cancel-handle-and-http-reset-flakes-plan.md`,
section A, and `blobtransfer-cancel-teardown-warning-record.md`.** The warning is emitted by the
**unpackager**, which discards its staging directory (`%TEMP%\.<uuid>` — a `TmpDir` is
`.bl-temp-dir-<uuid>`) in the failure branch of `flushAllPendingTasks()`
(`FilesUnpackagerUnit.h:1514-1518`) without first closing the output files of entries whose chunks
have not all arrived; the multi-chunk file's handle is kept open between chunks, so the deletion
hits a sharing violation whenever the cancel lands mid-download. The fix closes those handles
before the deletion, as the incomplete-content branch (`:1584-1589`) already did. A first trace had
attributed the hold to the packager's block reader (which did keep the input file open across
blocks and on cancel — fixed as well, as a separate hardening), but the failing paths were never
input trees. The text below is kept as the record of why the first proposal was withdrawn.

**File:** `src/utests/utf_baselib_blobtransfer/TestBlobTransferFilesystem.h`,
`BlobTransfer_FilesPackagerInMemoryCancelUploadTests` (line 33). Failed on `vc143`, passed on
`ccl16` — intermittent.

What is known: the case fails through `safeDeletePathNothrow` (`FsUtils.h:570-608`) logging a
**warning** from `TmpDir`'s destructor, and `UtfMain.h:116-133` maps `LL_WARNING` onto
`UTF_ERROR_MESSAGE`. Linux unlinks an open file regardless, which is why it is Windows-only.

**Withdrawn diagnosis.** The original explanation — the test cancels with
`cancelAll( false /* wait */ )` and unwinds while a worker thread still holds a read handle on its
stack — does not fit two facts in the code:

- `trySafeRemove` retries **20 × 100 ms** on Windows before giving up (`FsUtils.h:55-91`,
  `:126-131`), so the handle was held for more than two seconds after the unwind.
- The pipeline runs under `scheduleAndExecuteInParallel( ..., eqTopLevel )`, whose catch block
  calls `forceFlushNoThrow()` (wait = true, cancelExecuting = true) and asserts the queue is empty
  before rethrowing (`Algorithms.h:78-94`, `ExecutionQueueImpl.h:1420-1438`); every top-level unit
  task has finished before `executeTheFilesPackagerAndTransmitterPipelineInternal` unwinds and
  destroys `tmpDir` (`TestBlobTransferUtils.h:803-815`).

So the holder outlives the queue by seconds — an object retained on the cancel path (this fixture
had exactly such a retention fixed on 2026-09-07, `blobtransfer-utest-object-leak-record.md`), or a
lingering `directory_iterator` from the scanner (an open find handle blocks `RemoveDirectory` with
the same error). Pushing `warningToDebugLineLogger` around the whole eight-iteration case, as first
proposed, would hide that, hide any other warning the case ever emits, and leave `bl-temp-dir-*`
directories in `%TEMP%` on every lost race. The `TestBaselibDefault4.h:187-202` precedent is a
deterministic single-FIFO case and does not carry over.

**Do instead:**

1. Rerun the case on `vc143` until it fails and capture the warning text verbatim: which path
   could not be deleted, and the error code — 32 (`ERROR_SHARING_VIOLATION`, an open handle) and 5
   (`ERROR_ACCESS_DENIED`) point at different holders. Capture the outstanding-object count at
   teardown for the same run.
2. If a holder is identified, fix it where the evidence says (test support or production), and
   let the deletion succeed.
3. Only if the holder is provably a bounded in-flight read may the logger pusher be used — scoped
   as narrowly as the `TmpDir` lifetime allows, with the on-disk leak stated in the record.

---

## Change 4 — already applied in the working tree

`src/utests/utf_baselib_async/TestAsyncCB.h` — `utf_baselib_async` did not compile on Windows at all
(pre-existing, from `90d6a44`). MSVC and clang-cl demand **opposite** things for a non-odr-use inside
a lambda with an explicit capture list: `C3493` "cannot be implicitly captured" versus
`-Wunused-lambda-capture` "not required to be captured", both fatal under `-WX`. Making the constant
`static const` removes the need for a capture on either compiler.

Already verified compiling **and** passing 20/20 on both toolchains. Keep it in this change set.

---

## Records to write

- **New deferral for the HTTP failures**, in the shape of
  `blobtransfer-utest-object-leak-record.md`. Observed: `utf_baselib_http` **passed 52 of 56 cases
  on `vc143` debug — 4 failed** (`BaseLib_HttpServerPerfTest`,
  `TlsHandshake_NameMismatchIsReportedThroughErrorInfo`,
  `TlsHandshake_AllowUntrustedRecordsAndClearsEndpointInfo`,
  `TlsHandshake_SniOmittedForAddressLiterals`) — and passed **56/56** on `ccl16` release, with
  `system:10054` (`WSAECONNRESET`) in the failures. **The IPv4/IPv6 explanation first attached to this is withdrawn** — the code refutes
  it on every point: the acceptor binds the numeric literal `"0.0.0.0"`
  (`src/utests/include/utests/baselib/HttpServerHelpers.h:363-382`; the `src/include/.../http/`
  path cited earlier does not exist), which resolves to one endpoint, so `TcpBaseTasks.h:1194-1214`
  taking the first discards nothing; the client tries **every** resolved endpoint through the range
  form of `async_connect` (`TcpBaseTasks.h:1372-1381` via `BoostAsioCompat.h:389-411`), so a refused
  `::1` yields `10061`, is swallowed and falls through to `127.0.0.1`; the readiness probe
  `waitForAcceptorReady` (`TestTaskUtils.h:717-772`) uses the same connector against the same
  `localhost` before any case runs and would fail deterministically on both toolchains if the theory
  held. (A fourth argument offered by the review — that the failures must include in-process
  certificate/policy cases which open no socket — does **not** apply: it read "52 of 56" as the
  failure count, whereas 52 **passed** and the 4 which failed all open sockets. The three arguments
  above stand on their own.) The `::1` in the failure messages comes from
  `TcpBaseTasks.h:727-735`, which decorates every client exception with the **first** resolved
  endpoint (already recorded at `whole-library-cxx-review-fable51.md:297`). The record must list
  the root cause as **unknown** and carry: the failing case names, the first failing message per
  case, whether the readiness probe passed, whether another `utf_*` process was running, and the
  `netstat -ano -p TCP` state for 281xx. Candidates to name, unconfirmed: process-wide state
  leaking across cases (`allowUntrustedCertificates()`, the untrusted-endpoints map, the global log
  level pushed by `SuppressExpectedWarningsScope` at `TestTlsHandshakeVerification.h:121-134`) —
  the only class that reaches the socket-free cases; warning-as-failure amplification
  (`UtfMain.h:124-126` plus `tryCatchLog` defaulting to `Logging::warning()` around per-connection
  setup at `TcpBaseTasks.h:1125-1131`); short deadlines against a slow debug build (the 3 s
  server-side TLS handshake deadline at `TestHttpServer.h:2065` is the textbook producer of a
  client-side 10054; `TestClientHttpTasks.h:2323/2339` has a one-second margin); port 28100 hijack
  under Windows `SO_REUSEADDR` (`TcpBaseTasks.h:1205`; every server module defaults to 28100,
  `UtfArgsParser.h:46`, and the project rules permit five modules at once) — the bind succeeds
  silently and every request resets; and `BL_ASSERT` being live only in debug (`BaseDefs.h:63`).
  Two constraints stay true: the default test certificate carries **DNS SANs only** and
  `TestTlsHandshakeVerification.h:349-360` asserts that connecting as `127.0.0.1` **must fail** —
  though the module already ships a certificate with `IP:127.0.0.1` / `IP:::1` SANs
  (`UtfCrypto.h:139-155`), so that constraint is narrower than it first looked.
- **Release note** in `devenv7-breaking-changes-release-notes.md` for Change 1. Absolute paths
  already came back prefixed and different from their input; what is new on Windows is that
  **relative** paths and **already-prefixed** paths also come back with `\`. Visible to anyone who
  round-trips a relative path through `.string()` and compares (the two assertions in Change 1 are
  the in-tree examples); in every filesystem error message via `normalizePathParameterForPrint`
  (`OSImplPlatformCommon.h:2393-2399`); in the `.symlink` placeholder file the unpackager writes on
  Windows, whose content is the target text (`FilesUnpackagerUnit.h:809` — `../d/sub` becomes
  `..\d\sub`, a persisted artifact); and in `PluginAccess::getLibrary()`, which returns `\` for a
  `/`-spelled registration (`PluginAccess.h:89`). `//server/share` is now recognised as UNC (v3
  only). `bl::fs::nolfn::path` remains the raw Boost path for callers who need one.
- Note the residual from Change 1 (the inherited mutators and `auto`-deduced Boost paths, with the
  sites named there) in this plan's record or the release note.
- **Mirror direction, recorded not fixed:** a package produced on Windows stores `d\f.bin` (see
  Context) and unpacks on Linux as a single file of that name. The portable form would be
  `generic_string()` at whatever out-of-tree boundary serializes `relPath`; nothing in-tree does.

---

## Verification

Change 1 has a wide Windows blast radius — 293 `fs::path` mentions across the baselib headers — so it
needs more than the eight modules affected by the pull.

1. **Full Windows matrix for Change 1.** Build and run **all 19 `utf_baselib*` modules**, `-j1`,
   `TOOLCHAIN=vc143 VARIANT=debug` and `TOOLCHAIN=ccl16 VARIANT=release`, `ARCH=a64`. Zero warnings
   under `-WX`. Drive the exes directly with a per-module timeout — `scripts/debug_harness.py` has no
   timeout of its own, so a hang would be unbounded.
2. **Show each defect failing before its fix.** The four `BlobTransfer_Unpackager*` cases fail today
   with `system:123` ("The filename, directory name, or volume label syntax is incorrect") and
   `generic:22` (EINVAL) on paths such as `...\.bl-temp-dir-...\d/sub`. Re-run the tasks ownership
   case ~15× before and after — the current rate is ~20% — and the blobtransfer cancel-upload case.
3. **Targeted regression for Change 1.** `utf_baselib` (path, filesystem and LFN coverage),
   `utf_baselib_data` (`TestFilesystemMetadataInMemory.h:243`), `utf_baselib_blobtransfer` (the
   end-to-end coverage), `utf_baselib_io`, `utf_baselib_loader`, and `utf_baselib_jni` — the last
   because the `JAVA_HOME` normalization documented in `scripts/devenv7/AGENTS.md:423` is the
   per-call-site patch this change generalises. Watch specifically: `TestBaselibDefault.h:7759`,
   `TestFilesystemMetadataInMemory.h:243`, the `isValidPath` row `TestBaselibDefault.h:4944`, and
   the forward-slash roots at `TestBaselibDefault.h:4694/4718`.
4. **Linux.** POSIX is untouched by construction (`PathImplT< false >` never calls
   `chk2AddPrefix`), but Changes 2 and 3 are cross-platform test code. A Linux run of
   `utf_baselib_tasks` and `utf_baselib_blobtransfer` should confirm no change. If that run is not
   performed from the Windows host, say so plainly in the records rather than implying it was.
5. `git diff --check`, **and** verify line endings with `git ls-files --eol` (index and working
   tree in one line) or `file -b` on every touched file: `core.autocrlf` is `true` in this
   checkout, so `git diff --check` is **blind** to a whole-file line-ending rewrite. The commit is
   LF either way; the check is about on-disk consistency. As of 2026-09-09 the working tree is
   already mixed — `TestAsyncCB.h` and `OSImplPlatformCommon.h` are LF, `TestTasks7.h`,
   `TestBlobTransferFilesystem.h` and `TestBaselibDefault.h` are CRLF — so preserve each file's
   current ending rather than assuming one direction.
6. Nothing is committed; the tree is left for review.

---

## Out of scope

- The HTTP failures — recorded with root cause unknown, not fixed (see Records).
- The Windows → Linux packaging direction (backslash relative paths) — recorded, not fixed (see
  Context and Records).
- `13(c)`, the Windows handle-inheritance race, remains closed as a deferral in
  `windows-handle-inheritance-race-deferral.md`.
- The OpenSSL 1.1.1w leg, still blocked for the pre-existing cross-platform
  `-Wdeprecated-declarations` reason.
