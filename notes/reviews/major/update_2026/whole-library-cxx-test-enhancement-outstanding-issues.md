# C++ test enhancement: outstanding issues requiring an owner decision

**Date:** 2026-09-08
**Source:** the implementation of `whole-library-cxx-test-enhancement-plan.md` (same directory), all 383 tasks, delivered as 49 commits on `lazari2` (`cb431f0` -> `7b4988e`).
**Companion:** `whole-library-cxx-test-enhancement-implementation-report.md` (same directory) holds the full statistics, per-module timings and the complete defect list including everything that was fixed.
**Scope of this document:** only the items that are **not** resolved and that need a decision from the maintainers. Everything fixed during the work is out of scope here and is recorded in the companion report.

Each item states what was observed, how it was established, why it was not actioned, and what the decision is.

---

## 0. Resolution status (2026-09-08)

Every decision this document asks for has been taken, and everything that could be executed on
Linux has been. The plan is
`whole-library-cxx-test-enhancement-outstanding-issues-plan.md` (same directory); it carries the
reasoning, the options considered and the sizing for each. This section is the index — the sections
below are left as they were written, as the statement of the question.

| Section | Decision taken | State |
|---|---|---|
| 1 — D-01, cancellation and the retry loop | change the semantics: a cancel latch in `RetryableWrapperTaskT` itself, not in `ForwarderTaskBase` or `SimpleTimerTaskT` | **done**, Linux |
| 2.1, 2.2 — the two Windows fixes | run the suite once on a Windows host | **done**, Windows, 2026-09-08; 2.1 needed a new discriminating case (see section 2) |
| 2.3 — `ComUtils.h` / `WindowsShellShortcut.h` | apply the include fixes | **done**, Windows, 2026-09-08; the three includes are exactly sufficient |
| 2.4 — the seven never-compiled Windows test tasks | compile and run once, repairing test code only | **done**, Windows, 2026-09-08; two test-only repairs, one production defect recorded |
| 3.1 — five unmapped exception types | map all five | **done** |
| 3.2 — the rejected `OpenSSL` category | carry the name and the numeric value as data; do not reject the document, and do not teach `data/eh` about `crypto/` | **done** |
| 3.3 — eight dropped `errinfo_*` fields | serialize all eight, redacting the two which disclose the server's internals | **done** |
| 4.1 — `exceptionMessage` / `properties.message` | redact, unless the exception is user friendly | **done** |
| 4.2 — the JSON-only escaper | one escaper per content type; refuse an unescapable content type at startup when escaping is on | **done** |
| 5 — `BoolSwitchOrMultiStringOption` | withdraw the typedef | **done** |
| 6 — the plan document's own defects | prepend an erratum; do not edit the specifications in place | **done**, plus five further errors found (eleven in total) |
| 7 — the pinned behaviours | eleven fixed, three kept, one documented rather than changed | **done** |
| 8 — the filed deferrals and decisions | three closed, three unchanged keep decisions | **done** |

The one item where the executed decision differs from the plan's own recommendation is the
subscription handle in section 7. The plan recommended holding a strong reference in the disposer;
what was implemented instead is the plan's first option — the ownership contract is now documented
on `ObserverDisposerT` in `reactive/ObservableBase.h`, and the weak reference is unchanged. The
reason is scale: the plan sized the change at one flipped test in `utf_baselib_tasks`, but a strong
reference makes a discarded handle unsubscribe at **every** call site, and there are about twenty of
them across `utf_baselib_tasks` and the two blob-transfer test files, all using the fire-and-forget
form `observable -> subscribe( observer );`. Making the handle always effective would turn that
idiom into a silent no-op in the other direction. No production code under `src/include` or
`src/local` calls `subscribe()` at all.

---

## 1. Blocked defect

### D-01 - cancellation does not stop `RetryableWrapperTask`'s retry loop

**Severity:** the practical impact is that a cancelled operation keeps contacting a remote endpoint
for its entire retry budget. That affects shutdown latency and it defeats back-off against a peer
that is already failing.

**Measured, not inferred.** After a single `requestCancel()`, the wrapper still performed
`factoryCalls == maxRetryCount` (5) and burned **8.01 seconds** of retry sleeps. The figures come
from the test written for task T203, which drives the real wrapper.

**Mechanism**, three parts combining:

- `ForwarderTaskBase::requestCancel` forwards the cancel to the **current** target only;
- each factory-produced task starts with `m_cancelRequested == false`, so the next retry is born
  uncancelled;
- `SimpleTimerTaskT::run()` returns `time::neg_infin` **without an exception** when cancelled, so
  `continuationTask()`'s `if( m_wrappedTask -> exception() ) return nullptr` guard never fires and
  the chain continues.

**Why it was not fixed.** A correct fix changes cancellation semantics in `ForwarderTaskBase` and
`SimpleTimerTaskT` - core task-layer classes sitting behind every retryable operation in the
library. That is a behavioural change with library-wide blast radius, not a local repair, and the
plan's own T203 specifies documenting rather than fixing. Guessing at it inside a test-coverage
change would have been the wrong call.

**Current state.** The test **pins today's behaviour** with a comment saying so explicitly, so the
suite is green and the behaviour is recorded rather than silently tolerated. If the semantics are
changed, that case must be updated deliberately - it will fail, and the failure is intentional.

**Decision needed:** whether to change the cancellation semantics, and if so, how the retry chain
should observe a cancel that arrives between attempts.

---

## 2. Fixed but unverified on this platform

> **Resolved on a Windows host, 2026-09-08.** All four sub-sections below are closed. The full
> outcome, with the verbatim pre-fix failures, is the "Outcome (2026-09-08)" section of
> `notes/plans/issues/windows-only-residual-findings-deferral.md` (items 15, 16 and 17 there).
> In summary:
>
> - **2.1 `USERDOMAIN`** - execution-proven, but **not** by the case this section names. The T061
>   oracle is correctly derived from the contract, yet on a host which is not domain joined
>   (`USERDNSDOMAIN` unset, `USERDOMAIN` equal to `COMPUTERNAME`) the expected answer is the empty
>   string and the **pre-fix implementation also returned the empty string** - so that case cannot
>   tell a fixed implementation from a broken one. A new case,
>   `BaseLib_OSUserDomainEnvironmentWindowsTests`, drives the three environment variables itself and
>   pins every leg including the discriminating one.
> - **2.2 the merged-redirect assertion** - execution-proven green, no abort, by
>   `BaseLib_OSCreateProcessRedirectedMergedTests` (2/2) and
>   `BaseLib_OSCreateProcessMergedWithFileCallbackTests` (8/8).
> - **2.3 `ComUtils.h` / `WindowsShellShortcut.h`** - the three includes applied on Linux are
>   exactly sufficient; T373's `_WIN32` block compiles both standalone and
>   `BaseLib_PublicHeadersAreSelfContainedAndInstantiable` is 34/34 on both toolchains.
> - **2.4 the seven never-compiled tasks** - compiled and run. The first repair is why `utf_baselib`
>   did not build on Windows **at all**: a local named `small` does not compile, because `rpcndr.h`
>   defines `small` as a macro for `char`. The second uncovered a real cross-platform production
>   defect - `os::fread( )` and `os::fwrite( )` distinguished a transfer error from a short transfer
>   at end of file using `errno`, which the Windows CRT does not set for this case (measured:
>   `errno == 0`, `ferror( ) == 1`), so a genuine error was reported as "Reading past the end of
>   file". It was first recorded rather than fixed, then **fixed on the owner's instruction** as item
>   18 of the deferral record: one `detail::getStdioTransferErrorCode( )` in
>   `OSImplPlatformCommon.h` now carries the rule for `fread`, `fwrite` and
>   `stdio_file_device_base::checkStream( )` alike - the last of which already held the correct
>   idiom, which is how the convention was identified. UNIX behaviour is unchanged by construction;
>   **that half still needs a Linux run.** T253's dropped hive assertion and T058's disabled argv
>   case are both live again.
>
> Two statements in this section are errata, corrected in the outcome record: the claim that the
> existing cases were sufficient for 2.1 (above), and the W-1 argv "scaffold" the companion handoff
> cites - a batch harness cannot be the oracle, because `cmd.exe` applies its own parsing rather
> than `CommandLineToArgvW`'s and its `shift` loop cannot represent an empty argument.

Two defects are **Windows-only**. They were confirmed by source inspection and each carries a
regression test, but `OSImplWindows.h` is `#error`-guarded to `_WIN32` and neither the Linux clang
lane nor the gcc integration build compiles it. **Neither fix was executed.**

### 2.1 `OSImplWindows.h` - `tryGetUserDomain()` read the wrong environment variable

The fall-back block read `tryGetEnvironmentVariable( "USERDNSDOMAIN" )` a second time where it
plainly meant `"USERDOMAIN"`. Because the first block already returns whenever `USERDNSDOMAIN` is
set, the variable in the fall-back was **always null**: the function returned empty whenever
`USERDNSDOMAIN` was unset, and the `computerName == userDomain` comparison was unreachable dead
code. Fixed by the one-word change.

Note the accompanying finding: the **existing test's own helper** `isLocalUserOnWindows()` carried
the identical copy-paste bug, which is why the case passed unconditionally - it compared the
implementation against a copy of itself. The helper was replaced by
`expectedUserDomainFromEnvironment()`, derived from the contract.

### 2.2 `OSImplWindows.h` - the merged-redirect assertion

`BL_ASSERT( out )` in the merge branch was changed to `BL_ASSERT( ! callbackIos || out )`, matching
the fix made and **executed** on the UNIX twin `OSImplUNIX.h`, where the same combination aborted
the test binary with `SIGABRT` (exit 134). The Windows edit is the same one line, kept in sync
deliberately.

**Decision needed:** run the suite once on Windows to convert both from inspection-confirmed to
execution-proven. The regression tests are already in place and will execute there.

### 2.3 Two further Windows-only headers, reported and not fixed

`core/specific/ComUtils.h` and `core/specific/WindowsShellShortcut.h` are **not self-contained** -
they name `BL_THROW`, `SystemException`, `cpp::SafeUniquePtr`, `BL_LOG`, `Logging::`, `fs::path` and
`fs::path_exists` while including only `<objbase.h>` / `<objidl.h>`. Confirmed by inspection; not
fixed, because they cannot be compiled or verified on this host. This is the same class of defect as
the four self-containment failures that **were** fixed and verified (`HttpServerPorts.h`,
`loader/Version.h`, `jni/JvmHelpers.h`, `crypto/HmacSha256.h`).

**Decision needed:** apply the equivalent include fixes on a Windows host, where
`TestPublicHeaderInstantiation.cpp` will then cover them.

### 2.4 Seven Windows-only test tasks have never compiled

*Added 2026-09-08; an earlier revision of this section omitted it.*

The Windows arms of T054, and the whole of T058, T060, T061, T062, T253 and T357, are written but
have never been compiled or executed — for the same reason as 2.1 and 2.2, `OSImplWindows.h` is
`#error`-guarded to `_WIN32`. They are catalogued in section 13.4.1 of the plan.

Two of them additionally carry an assertion which is **deliberately switched off** and which becomes
live only once the corresponding Windows production fix lands:

- **T253** dropped its HKLM hive assertion; and
- **T058** has a disabled argv case, gated by `productionArgvQuotingIsFixed = false` at
  `src/utests/utf_baselib/TestBaselibDefault5.h:649`.

Both gates are tied to items W-1 and W-3 of
`notes/plans/issues/whole-library-windows-residuals-instructions.md`; each flag is to be flipped in
the same change as the fix it waits for, never separately.

---

## 3. Information lost crossing the server-error wire

Three independent tasks converged on the same area from different angles. None of these is a bug in
the sense of a crash or corruption; each is a **design decision** about how much a peer is told.

### 3.1 Five declared exception types collapse to `UnexpectedException`

`BufferTooSmallException`, `NotFoundException`, `UserAuthenticationException`,
`NumberCoerceException` and `PrintableWrapperException` have **no arm** in
`createExceptionFromObject()`. All five are reconstructed on the far side as `bl::UnexpectedException`,
so a client catching on type cannot distinguish them.

Task T130 now pins the full mapping table - **18 mapped, 5 unmapped, plus `SystemException`** - with
a **count guard asserting the total is 24**. Adding a 25th declared type without giving it an arm
will fail that case rather than silently joining the unmapped set.

### 3.2 An entire error category is rejected on the wire

`crypto::getException()` produces a `SystemException` whose `errinfo_category_name` is `"OpenSSL"`.
`createServerErrorResultObject` writes that name onto the wire, and `createExceptionFromObject`
(`data/eh/ServerErrorHelpers.h:275-301`) then **rejects it** with
`ArgumentException "Unknown error category: 'OpenSSL'"`. The peer of a TLS failure therefore
receives a deserialization error naming an internal category instead of the real error.

Task T381 pins this. The assertion is written to be **green against today's code and to flip loudly
when the fix lands**, with both candidate fixes named in-source: teach the category-name chain an
`"OpenSSL"` arm, or stop copying a non-generic / non-system category name into the document.

### 3.3 Eight `errinfo_*` fields are dropped

Task T292 enumerates them and asserts each is absent after the round trip - every "dropped"
assertion paired with a **positive control on the source object**, so none can pass vacuously. The
case emits a `UTF_MESSAGE` listing all eight, so the gap is visible in the log rather than only in
test source.

**Decision needed for 3.1-3.3:** how much of a server-side error should reach the peer. The tests
currently document the status quo precisely; they do not assert that it is correct.

---

## 4. Redaction gaps

### 4.1 `exceptionMessage` and `properties->message()` are not redacted

`getRedactedServerErrorAsJson()` redacts structured fields but leaves both message strings intact,
even though `BL_MSG()` text routinely carries **file paths**. Flagged, pinned, not changed - whether
these should be redacted is a product decision, not one for a test-coverage change.

### 4.2 The content escaper is installed only for JSON content types

`AuthorizationServiceRest` installs the content escaper only when
`str::icontains( contentType, "json" )`. For `application/x-www-form-urlencoded` or
`application/xml`, a **client-controlled token value reaches the request body unescaped**, while the
URL path is still escaped. Escaping of client-controlled token values is the stated defence against
query-parameter injection, so the asymmetry is worth an explicit decision. Pinned by sub-case (4) of
`AuthorizationServiceRest_TemplateEscapingTests` (T029).

*Correction (2026-09-08):* an earlier revision of this section said the sub-case carries "a count
guard". It does not — `AuthorizationServiceRest_TemplateEscapingTests` has no count assertion of any
kind, and no such guard was ever written for it. The coupling between the escaper table and the
content types it covers is carried by the named sub-cases only.

**Decision needed:** whether either should be tightened.

---

## 5. A published API that has never worked

### `BoolSwitchOrMultiStringOption`

The typedef is published in `cmdline/Option.h` and had **never been instantiated anywhere in the
repository**. Task T364 is its first use. The behaviour was established empirically before any
assertion was written:

| input | observed |
|---|---|
| absent | no throw, `hasValue() == false`, empty vector |
| `--flag` | no throw, `hasValue() == true`, **still an empty vector** |
| `--flag a` | throws `bl::po::too_many_positional_options_error` |
| `--flag a b` | throws `bl::po::too_many_positional_options_error` |

It cannot carry the multi-string values its name promises. The case pins the current behaviour with
a `TODO`, and the finding is filed at
`notes/plans/issues/cmdline-boolswitch-or-multistring-option-deferral.md`.

**Decision needed:** fix the option type, or withdraw the typedef.

---

## 6. Defects in the plan document itself

Six specification errors were found by attempting the work. They matter because the plan document in
this directory still contains them, so anyone re-deriving work from it will hit them again.

| # | Where | Problem |
|---|---|---|
| 1 | **T006 step 4 and T010 step 4** | Wrapping the nine exception macros in `UTF_IMPL_WRAPPER_BEGIN/END` **deadlocks the test binary**. `UtfMain.h:116`'s `utfLineLogger` takes the same non-recursive `test::UtfGlobals::g_lock` for *every* log line, so holding it across an expression that logs self-deadlocks. Making the lock recursive does not help: a worker thread logging while the main thread waits on tasks deadlocks across threads. **Both steps were dropped.** |
| 2 | **T028** | The assertion `UTF_REQUIRE_EQUAL( Task::Completed, lastTaskHandedToUpdate()->getState() )` is **impossible** - the task settles at `PendingCompletion` - **and vacuous**, since reading state after settling proves nothing about ordering. Re-specified to capture state inside the mock at the instant `update()` is called. |
| 3 | **T006 step 3** | The `TestDataChunkStorageFilesystem.h` join point is not implementable: that file reaches `BackendImplTestT` only through a **static** helper, so there is no recorder instance to drain. |
| 4 | **T012** | `brokerProtocol->tokenType()` does not exist. The real path is `principalIdentityInfo()->authenticationToken()->type()`. |
| 5 | **T082** | The `readAllBytes` helper the plan attributes to it does not exist; T016/T017 defined their own. |
| 6 | **T378** | The suggested write-once `std::size_t*` out-parameter cannot express T377's requirement to sample the counter **before and after** individual probes. Implemented as a live callback view instead. |

Separately, **the plan's line numbers are stale throughout**. Ten chunks shifted the two large test
headers; cases must be located by **name**, never by the line numbers the plan cites.

**Decision needed:** whether to correct the plan document in place, or leave it as the historical
record with this list as the erratum.

---

## 7. Behaviours pinned as current, not endorsed

These were characterised deliberately, each with a comment at the assertion saying it records
current behaviour rather than approving it. They are listed so that a future change to any of them
is understood to be a deliberate contract change, not an unexplained test failure.

| Area | Pinned behaviour |
|---|---|
| `fs::safeDeletePathNothrow` | returns `true` while the directory survives - `BL_WARN_NOEXCEPT_END` only logs the escaping exception and execution falls through to `return true` |
| `encoding::writeTextFile` | opens (and truncates) the target **before** the encoding switch, so an invalid-encoding throw leaves an existing file empty |
| `ScopeGuardT` | move-assignment silently drops the target's pending cleanup |
| `DataBlock::write( const std::string& )` | not atomic - the 4-byte length prefix is already appended when it throws, so `size()` is 4 afterwards |
| `IcmpHeader::computeChecksum` | never zeroes the checksum field before summing; safe only because the header is freshly constructed per ping |
| `ExcludedPathsControlToken` | keys lookups on the **normalised** path while storing the caller's strings verbatim, so an un-normalised exclusion entry silently excludes nothing |
| `TimerTaskBaseT::resetTimer` | ignores the queue's local thread pool while `SimpleTaskBaseT` honours it |
| subscription handles | a discarded handle unsubscribes only when the caller holds a `std::shared_ptr` to the observable |
| `http::StatusStrings::get()` | substitutes the 500 status line for **any** unknown code, while `getStatusLine()` does not |
| `g_charsetRegex` | its trailing `\b` makes a **quoted** charset (`charset="UTF-8"`) capture as `"UTF-8` and fall through to the unsupported-charset arm, though RFC 7231 permits the quoting |
| `TcpBlockTransferServer` `chk4ServerErrors()` | narrows a backend failure to one `uint32` from `errinfo_errno`, else from `errinfo_error_code` **only if generic** - so an uncoded or `system_category` failure sets no `ErrBit` and the client gets a bare transport failure carrying none of the server's reason |
| `ProxyBrokerBackendProcessing` | does not override `isConnected()`, inheriting the always-true default even when fully disconnected and the control token is cancelled |
| `ManifestFactoryT::read()` | calls `value.as_object()` with no precondition check, so a malformed manifest from a plug-in vendor escapes as a raw JSON-backend exception rather than the user-friendly `bl::UnexpectedException` every other malformed-manifest path yields |
| `RemoveChunk` `IgnoreIfNotFound` | removing a never-saved chunk succeeds |
| `CommandBase::removeOption` / `removeCommand` | erase the map entry **by name** but the vector entry **by pointer identity**, so removing a different object carrying a registered name desynchronises them and trips `BL_ASSERT` at `CommandBase.h:279`, aborting a debug run. The plan's own T109 verifier amendment forbids testing this until the production fix lands, so it is **not** covered |

*Addition (2026-09-08):* an earlier revision of this table omitted four pins which belong in it. They
are listed separately below only because they were found after the table was written; they carry the
same status as every row above — recorded as current, not endorsed.

| Area | Pinned behaviour | Task |
|---|---|---|
| `BignumBase64UrlT` | **encoding zero throws.** `bignumToBase64Url( zero )` computes `BN_num_bytes == 0` and `::BN_bn2bin` returns 0, which `BL_CHK_CRYPTO_API_NM` treats as failure — so the caller gets a `SystemException` whose error code is 0, whose message is the generic crypto one, and behind which the OpenSSL error queue is **empty**. Decoding is also **not canonical**: leading zero bytes are dropped, so `"AAEAAQ"` and `"AQAB"` decode to the same value and re-encode to `"AQAB"` | T331 |
| `RsaSignVerifyT::tryVerify` | **the "try" form throws** for a malformed signature — its first action is `base64UrlDecodeVector`, which raises `ArgumentException` for a length of 1 modulo 4, for more than two `=`, or for any byte outside the alphabet. When it does return `false`, `::RSA_verify()` has left entries on the process-global OpenSSL error queue which nothing drains | T335 |
| `CryptoInitT::setUntrustedEndpointInfo` | uses `emplace`, so a **second, different** failure reason for the same endpoint id is silently discarded and `getUntrustedEndpointsInfo()` keeps reporting the first one until the entry is cleared. Changing `emplace` to `operator []` — the natural "fix" — would silently alter what every consumer reads | T368 |
| `ThreadPoolDefault::disposeGlobalThreadPool` | disposes the pool **twice** — once explicitly and once through the slot being cleared. It is harmless because `dispose()` is idempotent, but it is not what the ordering comment describes | T255 |

---

## 8. Deferrals and decisions already filed

Six documents were written into `notes/plans/issues/` during the work, following that directory's
existing convention:

- `cmdline-boolswitch-or-multistring-option-deferral.md` - item 5 above
- `cmdline-dryrun-hide-unhide-not-raii-deferral.md` - `getOptionsHelp()`'s hide/unhide pair has no RAII guard
- `watchdog-interval-and-horizon-validation-deferral.md` - two argument-validation gaps T362 forbids fixing from a test
- `string-utils-dead-public-helpers-keep-decision.md` - four dead public helpers, kept
- `objmodel-single-threaded-config-keep-decision.md` - unused single-threaded `ObjectImpl`, kept
- `timezonedata-get-default-timezone-keep-decision.md` - accessor with no callers, kept

Note the plan repeatedly refers to `notes/issues/`, which does not exist in this repository. The
actual convention is **`notes/plans/issues/`**.

### 8.1 What the 2026-09-08 execution changed in those documents

Executing
`whole-library-cxx-test-enhancement-outstanding-issues-plan.md`
closed three of the six and added one record elsewhere:

| Document | State after 2026-09-08 |
|---|---|
| `cmdline-boolswitch-or-multistring-option-deferral.md` | **closed** — decision D8 withdrew the typedef (item 5 of this document); the record carries the resolution and the reasoning for choosing withdrawal over a fix |
| `cmdline-dryrun-hide-unhide-not-raii-deferral.md` | **closed** — fixed with `BL_SCOPE_EXIT`; a throwing-render sub-case was added to `CmdLine_DryRunNotApplicableHidesParentOption` |
| `watchdog-interval-and-horizon-validation-deferral.md` | **closed** — both gaps fixed with one `BL_CHK` each; two negative cases added to `TestWatchdogArgumentValidation` |
| `string-utils-dead-public-helpers-keep-decision.md` | unchanged — keep decision, no action |
| `objmodel-single-threaded-config-keep-decision.md` | unchanged — keep decision, no action |
| `timezonedata-get-default-timezone-keep-decision.md` | unchanged — keep decision, no action |
| `broker-outbound-peer-identity-deferral.md` | **gained a section** — the `chk4ServerErrors()` one-`uint32` wire limitation (section 7 of this document), recorded there because lifting it is a V1/V2 protocol change subject to the same compatibility constraints |
| `whole-library-windows-residuals-instructions.md` | **gained section 4b**, "Test-enhancement residuals", carrying items 5a, 5b and 5c for the Windows session |
| `devenv7-breaking-changes-release-notes.md` | **gained three entries** — the withdrawn typedef, the escaper's startup rejection of an unescapable content type, and the redaction of gateway error bodies |

---

## Summary of decisions requested

*All six were answered on 2026-09-08; see section 0 for what was decided and what is done. The list
below is left as the statement of the questions.*

1. **D-01** - change `RetryableWrapperTask` cancellation semantics, or accept and document the retry-after-cancel behaviour.
2. **Windows run** - execute the suite once on Windows to prove the two inspection-only fixes, and apply the two remaining self-containment fixes there.
3. **Server-error fidelity** - decide how much of an error should reach the peer (five unmapped types, the rejected OpenSSL category, eight dropped `errinfo_*` fields).
4. **Redaction** - whether `exceptionMessage` should be redacted, and whether the content escaper should cover non-JSON content types.
5. **`BoolSwitchOrMultiStringOption`** - fix or withdraw.
6. **The plan document** - correct the six specification errors in place, or keep this list as the erratum.
