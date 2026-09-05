# Fable 5.1 PR review: residual findings, status record

**Date:** 2026-09-04
**Source:** `notes/reviews/major/update_2025/v1/pr_review_analysis_fable51.md`, every finding outside
the merge gate (§11), which was closed by `pr-review-fable51-merge-gate-h1-h3-plan.md` and
`pr-review-fable51-merge-gate-items4-8-plan.md`.
**Decision:** every recommendation of the assessment plan was approved as-is on 2026-09-04, with one
exception: the Windows-only items went to their own record,
`windows-only-residual-findings-deferral.md`, rather than into this one. Implemented in one pass;
nothing committed by the implementer.

This record exists so that no residual finding of that review resurfaces as new: each one below is
**fixed**, **recorded** (deferred with a named trigger) or **accepted** (a risk acceptance with the
reason), and the record which holds the details is named. Findings which already carried a written
decision before this round (M-5, I-3, I-4, the `--account-id` and `0600` halves of I-13, the pin
sites of L-32, and the parts of L-1, L-2, L-5, L-7, L-11, L-14, I-5, I-7, I-14 and I-17 that the
assessment plan lists in its §1) are not repeated here.

---

## Dispositions

| Finding | Disposition | Where | Test |
|---|---|---|---|
| M-9 compat `async_connect` unconstrained | **Fixed**: `constraint_t<!is_endpoint_sequence<Iterator>>` on the reinstated overload, the constraint Boost ≤ 1.88 carried | `core/detail/BoostAsioCompat.h` | `BoostAsioCompat_AsyncConnectIteratorForm`, `BoostAsioCompat_AsyncConnectRangeForm` (the range form is the compile-time regression test) |
| M-18 clang-tidy fragment name collapses under `FORCE_O1` | **Fixed**: the `CXXFLAGS :=` filter-out is gone; `-O1` is appended (the last `-O` wins) and the file says why `:=` is forbidden | `projects/make/toolchain/clang-analysis.mk` | `make -n` shows one `-MJ<object>.o.json` per object; real UBSAN + tidy build of `utf_baselib_utils` |
| M-19 clang-cl global `-Wno-*` under `-WX` | **Fixed** (2026-09-04, Windows session): external headers passed with `-imsvc` in the `ccl16` branch, sixteen of the eighteen `-Wno-*` removed, the four project warnings they masked fixed | `projects/make/toolchain/msvc-default.mk`; `core/detail/OSImplWindows.h`, `utf_baselib/TestBaselibDefault.h`, `utf_baselib_io/TestIO.h`; `windows-only-residual-findings-deferral.md` outcome 1 | clean `-WX` `ccl16` builds of all 21 targets in all six architecture/variant combinations |
| M-20 scan-build on macOS unreachable | **Fixed** (truthfully): scan-build and clang-tidy are refused on macOS with an explicit error, devenv7+ is required there, Windows is refused outright; docs aligned | `clang-analysis.mk`, `notes/clang_analysis_tools.md` | read-through (no macOS host) |
| L-1 throttle setters do not pad | **Fixed**: both setters call `padExecutingQueueNothrow()` | `tasks/ExecutionQueueImpl.h` | `Tasks_ExecutionQueueRaisedThrottleLimitAdmitsPendingTasksTest`, `Tasks_ExecutionQueueReregisteredObserverLimitAdmitsPendingTasksTest` |
| L-2 user code under `m_lock` undocumented | **Fixed** (comments): `scheduleTask()` and `scanQueue()` contracts | `tasks/TaskBase.h`, `tasks/ExecutionQueue.h` | compile-proof |
| L-3 proxies disconnected after `UTF_REQUIRE` | **Fixed**: `BL_SCOPE_EXIT` at both sites | `utf_baselib_tasks/TestTasks.h` | the module's own run |
| L-4 `JsonException` escapes the DM `runtime_error` catch | **Fixed**: `bl::json::rethrowWithContext()` + a second catch clause in `BL_DM_IMPL_PROPERTY`; wrong Boost comment corrected | `core/JsonUtils.h`, `data/DataModelObjectDefs.h`, `core/detail/BoostJsonImpl.h` | `DataModelPropertyErrorsCarryPropertyContext` (both backends) |
| L-6 `3.0` into an integer: Boost accepted, json-spirit rejected | **Fixed** (aligned, strict): `bl::json::value_to<T>` wrapper refuses a double for integral `T` | `core/detail/BoostJsonImpl.h` | `JsonNumericDoubleIntoIntegralIsRejected` (both backends) |
| L-8 raw Boost error text marked user-friendly | **Fixed**: the recognized `boost::json::error` codes get readable text, anything else stays non-friendly, raw text nested | `core/detail/BoostJsonImpl.h` | the `strValue` case of `DataModelPropertyErrorsCarryPropertyContext` |
| L-9 JSON coverage gaps | **Fixed**: the four Boost-only cases now assert json-spirit's behaviour under `BL_USE_JSON_SPIRIT`; duplicate-key case; double round trips and a text-independent fidelity check; the `isClient()` gates and the missing include guard of `TestJsonPerformance.h` are the house convention (left) | `utf_baselib_data/TestJsonAbstraction.h` | `JsonParseDuplicateKeysAreBackendDefined`, the widened `JsonRoundTrip`, the four dual-backend cases |
| L-10 `SSL_CTX_get_ciphers` unguarded on 1.0.2 | **Fixed**: the check body is guarded on OpenSSL ≥ 1.1.0 (below it `SSL_CTX_set_cipher_list` returning zero, which the call site checks, is the whole check); the overstated comment corrected (1.1.1w and 3.x already fail closed) | `crypto/CryptoBase.h` | 3.5.4 and 1.1.1w builds of `utf_baselib_http` (1.0.2 desk-checked only, no dist) |
| L-11 modulus rounded up to a byte | **Fixed**: `EVP_PKEY_bits( evpKey() )`; removes the one `RSA_*` site added after EVP phase 0 | `security/JsonSecuritySerializationImpl.h` | `PemKeyFormats_ModulusBelowMinimumIsRejected` with the new 2047-bit fixture |
| L-12 non-RSA key rejected through an empty error queue | **Fixed**: `chkPemKeyIsRsa()` throws `SecurityException` naming the key type | same header | `PemKeyFormats_NonRsaKeysAreRejected` with the new EC fixtures |
| L-13 no `NO_COMPRESSION` / `NO_RENEGOTIATION` | **Fixed**: both bits plus `SSL_OP_CIPHER_SERVER_PREFERENCE`, `NO_RENEGOTIATION` under `#ifdef` (1.1.0h+) | `crypto/CryptoBase.h` | `TlsProtocolPolicy_HardeningOptionsArePinned` |
| L-14 no end-to-end negative handshake test | **Fixed**: two cases (name mismatch with a positive control; untrusted self-signed chain via the ip certificate and its previously orphaned key) driving a real handshake and asserting the `errinfo_ssl_is_verify_*` fields | new `utf_baselib_http/TestTlsHandshakeVerification.h`; `UtfCrypto.h` (`getIpAddressServerKey()`); `HttpServerHelpers.h` (optional key/certificate parameters) | `TlsHandshake_NameMismatchIsReportedThroughErrorInfo`, `TlsHandshake_UntrustedChainIsReportedThroughErrorInfo`, also run under `--no-rfc2818-verify` |
| L-15 resolver tests need a configured address | **Fixed**: the four `address_configured` resolves skip with `UTF_MESSAGE` when the host has no configured IPv4; the handler-storage case uses `numeric_host` | `utf_baselib/TestBoostAsioCompat.h` | the module's own run |
| L-16 `LoadLibraryExW` flags, byte-widening | **Fixed** (2026-09-04, Windows session): `utf8ToUtf16()` over `MultiByteToWideChar( CP_UTF8, MB_ERR_INVALID_CHARS )` at the three sites, `LOAD_WITH_ALTERED_SEARCH_PATH` for absolute paths only | `core/detail/OSImplWindows.h`; `utf_baselib/TestBaselibDefault.h`; `windows-only-residual-findings-deferral.md` outcome 4 | registry 7/7 and shared-library 6/6 on both toolchains; `Jni_CreateJniEnvironments` passes 11/11 with the JDK absent from `PATH` |
| L-17 `bl_tool` junction check fails open | **Fixed**: fail-closed `st_file_attributes` fallback, the `s3_manage.is_hostile_reparse` shape; the older plan record superseded | `scripts/bl_tool.py`, `directory-hash-tree-commitment-plan.md` | `TestReparseLinkDetection` (3 cases); **Windows runtime confirmed 2026-09-04**: a tree containing a real `mklink /J` junction fails the hash with "Symlink encountered" and exit 1, the same tree without it hashes and exits 0 |
| L-18 `list --paths-only` prints hostile keys, errors on stdout | **Fixed**: in that mode diagnostics go to stderr and a key with a control character is reported in `repr()` form and fails the command | `scripts/s3_manage.py`, `scripts/s3_manage.md` | `test_list_paths_only_rejects_keys_with_control_characters`, `test_list_paths_only_errors_go_to_stderr` |
| L-19 upload/verify symlink policy | **Fixed**: links (dangling included), junctions, reparse points and non-regular files are rejected with `[ERROR] Rejected entry`, the rest of the tree is processed, exit code 1 (the `bl_tool` policy and the M-10 shape) | `scripts/s3_manage.py`, `scripts/s3_manage.md` | `TestTreeEntryPolicy` (5 cases) |
| L-20 include-note paths with spaces | **Fixed**: prefix parsing (`parse_include_note`) and make escaping (`escape_dependency_path`) in both identical wrappers | `scripts/cl.py`, `scripts/clang-cl.py` | `TestIncludeNoteParsing` (5 cases, one parses a generated `.d` with GNU make); **Windows runtime confirmed 2026-09-04**: a real `cl.exe` compile through the wrapper with the header under a directory containing a space emits the path whole with the space escaped, and GNU make reads it as one prerequisite |
| L-21 `.bat` compiler shim | **Accepted**: deliberate (the test harness drives the wrapper through `cl.bat`); `PATHEXT` order makes a real `cl.exe` win; every argument comes from the makefiles and the tree | comment at the resolution site in both wrappers | none |
| L-22 `debug_harness` `KeyError` on an unknown platform | **Fixed**: `cfg.get()`, a stderr note, the child's exit code preserved | `scripts/debug_harness.py` | `test_handle_failure_skips_unknown_platform` |
| L-23 … L-29 devenv7 deployment scripts | **Recorded** | `scripts/devenv7/docs/supply-chain-verification-deferral.md`, rows 12-18 | on the next provisioning run per platform |
| L-30 gradle `$(firstword $(wildcard gradle/*))` | **Fixed** (makefile half): selects on the presence of a launcher, prefers `latest`; the installer layout split is recorded | `projects/make/3rd/gradle/latest.mk`; deferral row 19 | `make -pn` shows the same launcher; `utf_baselib_jni` build |
| L-31 `BL_CLANG_USE_GCC_LIBS` read before it is auto-set | **Fixed**: the standard-library selection block moved above the path selection | `projects/make/toolchain/gcc-default.mk` | `make -pn ... BL_EXPECTED_BOOSTDIR=/nonexistent` shows the GCC library paths in auto mode; default invocation unchanged |
| L-32 devenv8 divergence | **Fixed**: `BL_EXPECTED_BOOSTDIR` keyed on the legacy predicate like `BOOSTDIR`; a tripwire fails a devenv without declared pins. Note: this supersedes the F-14 correction's classification of that one gate as a deliberate equality test, because it must agree with `3rd/boost/common.mk` | `projects/make/devenv-detect.mk` | `make -n ... DEVENV_VERSION_TAG=devenv8` hits the tripwire; devenv7 dry run unchanged |
| L-33, L-34 Windows toolchain makefiles | **Fixed** (2026-09-04, Windows session): the arch makefiles accept `ccl16` via `$(filter vc143 ccl16,...)`; the toolset and SDK are chosen with `$(lastword $(sort ...))` and both are printed. L-34 was live on this dist, which carries two SDKs: the build moved from 10.0.22621.0 to 10.0.26100.0 | `msvc-default-x64.mk`, `-x86.mk`, `-a64.mk`, `msvc-default.mk`; `windows-only-residual-findings-deferral.md` outcomes 2-3 | `make -pn` diffs of `PATH` and both version tags across all six combinations; `ccl16` builds on a64/x64/x86 and a `vc143` build |
| I-1 per-registration policy binding undocumented | **Fixed** (comment) | `tasks/ExecutionQueueNotify.h`, `tasks/ExecutionQueue.h` | compile-proof |
| I-2 widened sleep-based test timeouts | **Accepted**: the affected cases (`TestTasks.h` cancel and timer tests, `TestIO.h` heartbeat) are inherently time-based; rewriting them as event-driven waits is real work for no product value, and the widening only removed false failures on slow hosts | this record | none |
| I-5 "opt-in" wording; debug dump of rejected input | **Fixed**: wording corrected; the dumps of rejected documents moved to trace level on both backends (**accepted** at trace level: an operator enables trace deliberately) | `core/JsonUtils.h`, `core/detail/BoostJsonImpl.h`, `core/detail/JsonSpiritImpl.h` | compile-proof |
| I-6 exception in the verify callback loses diagnostics | **Fixed**: explicit `try/catch` recording `X509_V_ERR_APPLICATION_VERIFICATION` and the message before the fail-closed return; the name-mismatch branch now also records `X509_V_ERR_HOSTNAME_MISMATCH` | `tasks/AsioSslStreamWrapper.h` | not provokable; the two normal branches are proved by the L-14 cases |
| I-7 `RSA_*` inventory versus the standing rule | **Fixed** (record): the baseline paragraph | `evp-provider-migration-deferral.md` | none |
| I-8 Windows `_InterlockedExchangeAdd` shim on 3.x | **Fixed** (blind, safe by construction): the shim is bounded to OpenSSL < 3.0, where the private header which needs it is included; on 3.x the overload was unreferenced, so removing it there cannot break a compile | `crypto/OpenSSLTypes.h` | **Compiled and tested on Windows 2026-09-04** against OpenSSL 3.5.4: `utf_baselib_security` builds and its tests report no errors with `ccl16` (debug and release) and with `vc143`. The 1.1.1w side stays unverified: this dist has no `openssl/1.1.1w` (`windows-only-residual-findings-deferral.md` outcome 8) |
| I-9 no zeroisation | **Accepted**: `randomPassword` is a deliberately invalid password, not a secret; the caller's `std::string` cannot be scrubbed by the library; the HMAC digest is copied into the returned hex string, so cleansing the stack copy is symbolic; OpenSSL zeroises the key material it owns | this record | none |
| I-10 partial wildcards permitted | **Fixed**: `X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS`; release note 5c | `crypto/TlsPeerVerification.h`, `devenv7-breaking-changes-release-notes.md` | `TlsPeerVerification_PartialWildcardsDoNotMatch` (certificates generated in-test) |
| I-11 JVM lifetime; manifest version ignored | **Fixed**: the JVM library handle is released (never unloaded), a create-attempted latch refuses a retry, a detach failure destroys the JVM before throwing with the right code; `manifestVersion` is checked | `jni/JavaVirtualMachine.h`, `loader/Manifest.h` | `TestManifestUnsupportedVersionIsRejected`; the JVM paths are not provokable from the JNI suite |
| I-12 two weakened assertions | **Fixed**: the two-byte NUL-continuation literal; the JNI thread name recorded on the exception is compared for equality with `Thread.currentThread().getName()` captured on the test thread (it is `Thread-N`, not `main`: the JVM constructor detaches its creating thread, which is re-attached later under a fresh name) | `utf_baselib/TestBaselibDefault.h`, `utf_baselib_jni/TestJni.h` | the modules' own runs |
| I-13 `indexupload` destination, unused import, botocore timeouts | **Fixed** (doc + import): the bucket-root destination is documented; the unused `NoCredentialsError` import removed; connect/read timeouts and retries belong to the shared client factory the F-13 deferral already calls for | `scripts/s3_manage.md`, `scripts/s3_manage.py` | pytest unchanged |
| I-14 `python-tests.mk` cwd-relative, 2.7 `venv` | **Fixed**: anchored on `TOPDIR` / `TOPDIRABS`; `pytest-install` refuses an interpreter below 3.6 with a clear message | `projects/make/python-tests.mk` | `make pytest-check` from the root and via `make -f ../Makefile` from `scripts/`; `PYTHON=false` errors as intended |
| I-15 unconditional `$(shell)`; "on Linux" text | **Fixed**: pure-make sanitizer counting (zero forks, verified with `strace`); Windows refused explicitly so the Linux branch is really Linux | `clang-analysis.mk` | `make -n` with 0, 1 and 2 sanitizer flags |
| I-16 version script dropped for lld; clang OpenSSL visibility | **Fixed** (makefile half): the version script is applied under lld with `--undefined-version` (lld ≥ 16 otherwise rejects a script naming symbols an executable does not define, which is what "stricter" meant); the plugin export surface is now the two entry points on both toolchains. The OpenSSL half is recorded | `projects/make/toolchain/gcc-default.mk`; deferral row 20 | `utf_baselib_loader` build and run; `nm -D` on the plugin |
| I-17 documentation drift, editor settings | **Fixed**: the JDK path lines and the OpenSSL CRT sentence in `scripts/devenv7/AGENTS.md`, the include-site sentence in `notes/clang_analysis_tools.md`; `settings/vscode/linux/a64/c_cpp_properties.json` refreshed to the devenv7 layout with `${workspaceFolder}` (the sibling `launch.json` was missed in this round and refreshed on 2026-09-05, see the addendum); **the Windows settings were rewritten on 2026-09-04** in three configurations (msvc-arm64, msvc-x64, clang-arm64) with `launch.json` moved to the devenv7 build tree and normalised to LF; the x64 and macOS settings and the two script sites stay recorded, as they belong to their own platforms | as named; `windows-only-residual-findings-deferral.md` outcome 7; deferral row 21 | `ls` of every path in the a64 file; every `includePath`/`compilerPath` in the Windows file checked to exist, both files parse |
| I-18 `DISTROOT` never defined | **Fixed**: the rule that actually applied (absolute → `-isystem`, relative → `-I`) is written out, and an absolute `TOPDIR` is refused with an explanation (it would silently mute every warning in the project's own headers) | `projects/make/toolchain/gcc-default.mk` | compile lines byte-identical before/after; `make -f <abs>/Makefile` from another directory hits the guard |

---

## Verification

Host: Ubuntu 24.04 aarch64, devenv7 dist (gcc 15.2 = `TOOLCHAIN=gcc1520`, clang 20.1 = default
`clang2010`, Boost 1.90, OpenSSL 3.5.4 and 1.1.1w, json-spirit 4.08, JDK 25). Every build `-j1`,
every module run through `make test_<module>`; "ok" means the build succeeded and the run reported
no errors. Build directories were removed after each run because the disk was at 92 %.

| Module | clang debug | clang release | gcc debug | gcc + OpenSSL 1.1.1w | other |
|---|---|---|---|---|---|
| `utf_baselib_tasks` | ok | ok | ok | — | new cases run by name |
| `utf_baselib` | ok | ok | ok | — | new cases run by name |
| `utf_baselib_data` | — | ok | ok (release) | — | ok with `VARIANT=release BL_USE_JSON_SPIRIT=1` after clearing the module objects (the decision record's command) |
| `utf_baselib_http` | ok | ok | ok | ok | the two handshake cases also pass under `--no-rfc2818-verify` (the real callback is forced for their duration) |
| `utf_baselib_security` | ok | ok | — | ok | new cases run by name |
| `utf_baselib_loader` | ok | — | — | — | new case run by name; the plugin's dynamic export list is exactly `registerPlugin`, `registerResolver` under lld |
| `utf_baselib_jni` | — | ok (JDK 25; JDK 8 unavailable) | — | — | — |
| `utf_baselib_messaging`, `utf_baselib_rest` | ok | — | — | — | smoke after the TLS and ExecutionQueue changes |
| `bl-tool` | ok | — | — | ok | the shipped includer of the PEM loaders |
| `utf_baselib_utils` | — | — | — | — | built with `BL_CLANG_ENABLE_SA_TIDY=1 BL_CLANG_ENABLE_RA_UBSAN=1 BL_CLANG_ENABLE_RA_FORCE_O1=1`: one `UtfBaselibUtilsMain.o.json` fragment produced (was `.json`) |

Makefile proofs (dry runs): the compile lines of `make -n utf_baselib_utils` are byte-identical
before and after for both toolchains (only the link line gained the version script under lld);
`make -pn` shows the GCC library paths in `LD_LIBRARY_PATH` in the auto-detected GCC-libs mode;
`make -n ... DEVENV_VERSION_TAG=devenv8` stops at the new tripwire; `make -f <abs>/Makefile` from
another directory stops at the repository-root guard; `make pytest-check` works from the root and
via `make -f ../Makefile` from `scripts/`; `strace` counts zero shells during a plain `make -n`;
two sanitizer flags error and one does not.

Python: `make pytest`: 593 passed, 2 skipped (baseline 577 / 2; the 16 new cases, none skipped at
uid 1000), coverage above the 80 % gate.

Not run at the time of the Linux round: Windows and macOS (no host); OpenSSL 1.0.2 (no dist: L-10
and L-13 are desk-checked against the 1.0.2 API surface); JDK 8; the Windows halves of L-17 and L-20
and the I-8 bound.

**Windows round, 2026-09-04** (ARM64 Windows 11 devenv7 host, MSVC 14.38.33130, clang-cl 16.0.5,
Boost 1.90.0, OpenSSL 3.5.4, JDK 25; every build `-j1`). This closed M-19, L-33, L-34, L-16 and the
Windows halves of L-17, L-20, I-8 and I-17 — see `windows-only-residual-findings-deferral.md`.

| Build | Result |
|---|---|
| `ccl16`, all six combinations (a64, x64, x86 × debug, release) — all 17 test modules, the plugin and the four apps | clean under `-WX` (zero warnings, zero errors) |
| `vc143` a64 debug — `utf_baselib`, `utf_baselib_security`, `utf_baselib_jni` | clean (zero `cl.exe` diagnostics) |

Tests: `test_utf_baselib_security` and `test_utf_baselib_jni` report no errors on `ccl16` (security
also in release) and on `vc143`; `test_utf_baselib_io` no errors on `ccl16`; the registry and
shared-library cases pass on both toolchains (7/7 and 6/6); `Jni_CreateJniEnvironments` passes 11/11
on both toolchains with the JDK absent from `PATH`. Python suite on Windows: **565 passed, 30
skipped, 0 failed** (595 collected, against 593 passed / 2 skipped on Linux).

Still not run: OpenSSL 1.1.1w on Windows (no such directory in this dist, so the 1.1.1w side of I-8
stays unverified there); macOS; OpenSSL 1.0.2; JDK 8. The Windows deployment-script deferrals
(`scripts/devenv7/docs/supply-chain-verification-deferral.md`) were **not** picked up in this
session: they need a provisioning run into a scratch dist root, which was not authorised, so those
scripts are untouched.

---

## Addendum: closure audit of 2026-09-05

Every one of the 75 findings was re-checked against the code at `7b80980` (not against the records
above). All three Highs and 70 of the 75 dispositions hold exactly. The rest is listed here so it
does not resurface, with what was done about it.

| Residual | Disposition (2026-09-05) |
|---|---|
| L-16: two byte-widening `std::wstring( name.begin(), name.end() )` sites survived (`OSImplWindows.h:2806` `RobustNamedMutex`, `:3396` `GetNamedSecurityInfoW`) | **Recorded**: `windows-only-residual-findings-deferral.md` item 9; not edited blind |
| M-1: the comment at `ExecutionQueueImpl.h:358-360` still said no user code runs under `m_lock`, contradicting the L-2 contracts; the same sentence in `execution-queue-notification-delivery-breaking-change.md` ("This closes CXX-07") | **Fixed** (comments): both now say observer code is lock-free and name the two documented exceptions (`scheduleTask()`, `scanQueue()`) |
| L-3: a third proxy site, `Tasks_ExecutionQueueThrottleFromObserverTest`, still had a `UTF_REQUIRE` between proxy creation and a plain `disconnect()` | **Fixed**: `BL_SCOPE_EXIT`, same shape as the two sites above |
| L-5: `BoostJsonImpl.h` pretty-print comments claimed the empty-container output matches json-spirit; `JsonPrettyPrintEmptyContainers` pins the opposite | **Fixed** (comments): both state the divergence and point at the test |
| I-17: `settings/vscode/linux/a64/launch.json` was not refreshed (devenv5 build paths, user-specific repo path, JDK 8 `JAVA_HOME`); the row above claimed the whole directory | **Fixed**: `${workspaceFolder}/bld/ub24-a64-clang2010-*`, `openjdk/25/a64`; the row corrected |
| Record inaccuracies: M-19 row said fourteen of sixteen `-Wno-*` (sixteen of eighteen); `TestJsonAbstraction.h` header said "four cases" (five); release note §8 code references stale and silent on the 1024-bit VeriSign root at level 2 (the TLS decision record has it); the H-1..H-3 plan pointed at its own prescription as the run record | **Fixed** (text) |
| Verification gap: the review's §10 asked for a TSan run of the ExecutionQueue notification tests and no record reported one | **Run**, see below |
| `notes/reviews/` is untracked in git while every record here points into it | **Open**, for the maintainer |

Noted, not changed: `devenv-detect.mk:204` (`win7` → `win`) is a layout gate on the `devenv7`
literal, classified as a deliberate per-version block in `medium-severity-findings-f11-f17-plan.md`;
`bl_tool.is_reparse_link` returns `False` on `OSError` where `s3_manage.is_hostile_reparse` raises
(deliberate, pinned by `test_bl_tool_unit.py`, and closed by the re-`lstat` in `collect_tree`);
`g_rfc2818VerifyCallback` bypasses the `preVerified` gate by design and pre-exists on `master`.

### TSan run (clang2010 debug, `BL_CLANG_ENABLE_RA_TSAN=1 BL_CLANG_ENABLE_RA_FORCE_O1=1`)

`utf_baselib_tasks`: all 53 cases pass ("No errors detected"); ThreadSanitizer reported **257
warnings**, none of them in any ExecutionQueue notification, throttle, disposal or admission test
case:

- **255 lock-order-inversion reports**, all from `Tasks_FilesPackagerUnitTests` (154),
  `Tasks_ExternalCompletionTaskTests` (101) and `Tasks_ProcessingUnitWithObserverTests` (2), all
  the same two-mutex cycle between two *task* locks (`TaskBase::m_lock` of an `AsyncExecutor`
  `ExecutorTask` and of a completion `SimpleTask`): one thread holds the executor task's lock inside
  `scheduleNothrow()` (called from `padExecutingQueueNothrow()` under the queue lock,
  `ExecutionQueueImpl.h:661/745/767`) and, through `postCompletionCallback()` →
  `m_completionTasksQueue -> push_back()` (`AsyncExecutorImpl.h:374, 536, 1085`), takes the
  completion task's lock while scheduling it; the other thread executes that completion task under
  its own lock and its callback takes the executor task's lock. The two acquisitions are in
  different phases of the same task (scheduling versus execution), which the queue lock serializes,
  so a real deadlock needs the same task to be in both phases at once. The structure is identical on
  `master` (`padExecutingQueueNothrow()` calling `scheduleNothrow()` under `m_lock`,
  `push_back()` holding `m_lock`, and the three `AsyncExecutorImpl.h` sites are unchanged), so this
  is pre-existing and not touched by any finding of this review; it is the "not required to be
  TSan-clean" class the delivery plan anticipated. Whether it should be silenced by a lock-order
  annotation or restructured is a separate decision.
- **2 data-race reports** inside Boost.Filesystem's `readdir` path (`libs/filesystem/src/directory.cpp`,
  `path_traits.hpp`) between two concurrent `ScanDirectoryTask` instances in
  `Tasks_FilesPackagerUnitTests`, each holding a different task lock and iterating its own
  directory; the reported address is the `DIR` buffer, so this is either glibc-internal locking
  TSan cannot see or heap-address reuse across `closedir`/`opendir`. Third-party, not in any code
  this review touched, not determined further.

Log kept for this round only (scratch, not committed); rerun with the flags above to reproduce.

### Verification of this round

| Build (`-j1`, clang2010 debug) | Result |
|---|---|
| `utf_baselib_tasks` TSan + `-O1` | build ok; 53 cases, no errors; TSan reports as above |
| `utf_baselib_tasks` (sanitizer objects removed first) | build ok; 53 cases, no errors, `Tasks_ExecutionQueueThrottleFromObserverTest` passes |
| `utf_baselib_data` | build ok; 83 cases, no errors, `JsonPrettyPrintEmptyContainers` passes |
| `git diff --check` | clean |
| `launch.json` paths | build-tree paths follow `PLAT`; `openjdk/25/a64/bin/java` exists on the dist |

Not run: gcc, release, json-spirit (comment-only changes in `BoostJsonImpl.h` and the tasks header;
the test edit is in one case of one module), Windows, macOS.
