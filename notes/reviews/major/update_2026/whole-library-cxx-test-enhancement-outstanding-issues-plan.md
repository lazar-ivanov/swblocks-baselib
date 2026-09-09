# Plan: resolving the outstanding issues of the C++ test enhancement

**Date:** 2026-09-08 (revision 2: design intent, performance, sizing and the summary table added)
**Status:** **executed on Linux, 2026-09-08.** Stages A1-A5 and C are implemented, built and run;
stage B (item 5a, 5c and the verification of 5b) needs a Windows host and is handed off. Nothing is
committed. See "Execution outcome" below for what differed from this plan.
**Source:** `whole-library-cxx-test-enhancement-outstanding-issues.md` (same directory), sections 1-8.
**Baseline:** `lazari2` at `7b4988e`.

## Execution outcome (2026-09-08)

Everything below is as planned except where this section says otherwise.

| Item | Outcome |
|---|---|
| 1 (D-01) | as planned. `Tasks_RetryableWrapperTaskCancelTests` flipped; a second case, `Tasks_RetryableWrapperTaskCancelDuringRetrySleepTests`, covers the cancel landing on the sleep timer |
| 2a | as planned, plus one flip the plan did not name: sub-case (d) of `IO_MessagingMessageProcessingTestWrappers` in `utf_baselib_messaging` pinned `NotFoundException` arriving as `UnexpectedException` over a real wire. The fall-through arm is now unreachable with any declared `bl::` type, so `ErrorToJsonExceptionTypeMappingTests` covers it with an undeclared type name instead; the counts became 23 round-tripped / 1 fall-through |
| 2b | as planned, plus two flips the plan did not name: the `"non-generic"` rejection sub-case of `ErrorToJsonTests`, and the comment on its empty-category sub-case (which still passes, for a different reason) |
| 2c | as planned. Eight properties added; `errorUuid` and the 4 KiB stack-trace cap are written out of the macro list. Confirmed by grep that no reader parses `ServerErrorJson` strictly. The `BL_USE_JSON_SPIRIT=1` release run is green |
| 3a | as planned, plus one flip the plan did not name: `BaseLib_HttpServerBackendFailureStatusTest` in `utf_baselib_http` asserted the raw backend text in a redacted body |
| 3b | as planned. Three escapers (JSON, form-urlencoded, XML) and a startup rejection; sub-cases (4)-(7) of `AuthorizationServiceRest_TemplateEscapingTests` |
| 3c | wider than planned. `RestUtils` alone was not enough: the two pins the plan names are produced by `BaseRestServerProcessingContext` (a REST server behind the gateway) and by the echo server's own error body, not by `formatEhResponseSimpleJson`. All three paths now redact, with `RestUtils::formatEhResponseSimpleJsonUnredacted` and `BaseRestServerProcessingContext::redactErrorResponses( false )` as the opt-outs |
| 4 | as planned |
| 5a, 5c | **not done** - Windows host required. Handed off in section 4b of `notes/plans/issues/whole-library-windows-residuals-instructions.md` |
| 5b | source change made on Linux and **uncompiled**; verification is part of the same handoff |
| 6 | as planned, plus five further specification errors (eleven in total) |
| 7.1-7.6, 7.7, 7.10, 7.12, 7.13, 7.15 | as planned. 7.7 additionally let `utf_baselib_basictask` gain the timer case the plan anticipated |
| **7.8** | **decided the other way.** The plan recommended a strong reference in the disposer; what was implemented is its first option - the ownership contract is documented on `ObserverDisposerT`, the weak reference is unchanged, and `Tasks_ReactiveSubscriptionHandleLifetimeTests` keeps both directions. The plan sized this at one flipped test; a strong reference actually makes a discarded handle unsubscribe at about **twenty** call sites across `utf_baselib_tasks` and the two blob-transfer test files, every one of them using the fire-and-forget form `observable -> subscribe( observer );`. No production code under `src/include` or `src/local` calls `subscribe()` at all |
| 7.9, 7.11, 7.14 | kept, as planned. 7.11's wire limitation is recorded next to the M-3 deferral |
| 8.1, 8.2 | as planned; both records closed |
| 8.3-8.5 | no action, as planned |

**Validation actually run.** gcc1520 debug build and full-module run of `utf_baselib`,
`utf_baselib_tasks`, `utf_baselib_basictask`, `utf_baselib_data`, `utf_baselib_security`,
`utf_baselib_http`, `utf_baselib_rest`, `utf_baselib_cmdline`, `utf_baselib_loader` and
`utf_baselib_messaging`; the `VARIANT=release BL_USE_JSON_SPIRIT=1` run of `utf_baselib_data`; and
`utf_baselib_io`, `utf_baselib_async`, `utf_baselib_jni` and `utf_baselib_blobtransfer` as a safety
net for the library-wide header changes (7.3, 7.4, 7.7). A clang2010 release pass over the ten named
modules followed. This is the "gcc debug during, clang release at end" matrix the user chose in
place of the full four-unit-per-module matrix this plan budgeted.

## Review follow-ups (2026-09-08, second pass)

A review of the executed work raised six residuals. Five were accepted and fixed; the sixth is
recorded as a known gap because reaching it is not proportionate.

| # | Residual | Action |
|---|---|---|
| 1 | Item 1 changes public cancellation semantics but had **no release note** - sections 12-14 covered every other change | **fixed:** section 12a of `devenv7-breaking-changes-release-notes.md` |
| 2 | A malformed `errorUuid` on the wire made `uuids::string2uuid` throw and **rejected the whole document** - the same failure class item 2b removed for categories, narrowed to one field | **fixed:** the conversion is gated on `uuids::isUuid`, so a corrupt value is dropped and the rest of the document survives. Sub-case (6) of `ErrorToJsonSystemCodeDerivationTests` pins both halves |
| 3 | An unresolvable category with **neither** `errorCode` nor `systemCode` set rebuilt a `SystemException` whose `code()` reports **success** | **fixed:** that degenerate document is refused with an `ArgumentException` naming the category, and the code is taken from whichever of the two is set. Sub-case (5) of the same case pins it, with a positive control |
| 4 | `Tasks_RetryableWrapperTaskCancelDuringRetrySleepTests` slept half the retry timeout and then asserted **exactly one** factory call - a stall over a second would have let the sleep elapse | **fixed:** the hand-off is now deterministic rather than timed. The verification callback signals from inside `continuationTask()` under the wrapper's lock, so `requestCancel()` blocks on that lock until the timer has been swapped in and therefore always lands on it; the retry timeout is 30 s so the sleep cannot elapse, and the case runs in **572 µs** because cancelling aborts the timer |
| 5 | The release note implied the gateway redaction had a **deployment** opt-out; `MessagingHttpGatewayApp.h:278-280` hard-wires the redacted formatter | **fixed in the wording only.** Section 11.6 now says plainly that the opt-out is API-level, that a stock gateway cannot turn redaction off without a code change, and that whether to add a command-line switch is an open product decision - adding one makes an information-disclosure setting operator controlled, which is what redaction exists to prevent being accidental |
| 6 | The proxy `isConnected()` fix is asserted only in the direction which **also passed before it** | **not fixed; recorded as a known gap** at the assertion itself and here. `ProxyBrokerBackendProcessingFactorySsl::create()` throws when no endpoint connects (see `ForwardingBackendConnectFailureTests`) rather than returning a live but disconnected backend, so the `false` half needs either the actual backend torn down underneath a running proxy or direct construction of the `detail::` type with a stub outgoing channel. The delegated expression is the one `ForwardingBackendProcessing` has used since before this change. A stale comment in `BackendProcessingDefaultsTests` which still claimed the proxy inherits the default was corrected |

**One pre-existing failure, not caused by this work.** `utf_baselib_jni` reports
`*** No errors detected` and then `Test setup error:` (an empty message) with exit code 200 - a
failure in the global fixture's teardown, after every case has passed. The baseline binary in the
`swblocks-baselib-lane2` worktree, built from `7b4988e` before any of these changes, produces the
identical output, so it is a property of this host rather than a regression. It is out of scope here
and is not fixed.

## How to read this plan

Every item in the source document is a **fix** (a defect with a local repair), a **decision** (a behaviour
the tests pin "as current, not endorsed") or a **record** (documentation). For each one the detail section
gives: where it is, what goes wrong, the original design intent and the idiom of the surrounding code, the
performance impact of that intent and of the fix, the options with a recommendation, the risk and blast
radius, the tests that pin today's behaviour and must flip in the same change, and the size, complexity and
cost.

Rating vocabulary, the same as the review's summary table: **Risk** None / Low / Low-Med / Medium (chance
the change breaks a consumer or a contract). **Blast radius** who observes the change. **Cost of testing** in
"units" = focused `make -k -j1 <module>` builds (toolchain × variant, so 4 units per module) plus the named
runs. **Complexity** Trivial / Low / Medium. **Cost of implementation** in hours, including the test flips.

Rules that apply when any of this is executed (`AGENTS.md`): Edit tool only for existing files; logic and
style never mixed; focused builds only, `-j1` when more than one module is built, both toolchains and both
variants; a pinning test is flipped in the same change as the fix, never softened; nothing committed
without an explicit request.

## Summary table

| Item | Recommendation | Risk | Blast radius | Cost of testing | Complexity | Cost of implementation |
|---|---|---|---|---|---|---|
| 1 D-01 retry loop ignores cancel | cancel latch in `RetryableWrapperTaskT`, `operation_aborted` result | Low | `RetryableWrapperTask` (no in-tree production consumer; public API) | 4 units `utf_baselib_tasks` + 1 new case + 1 flipped | Low | 3 h |
| 2a five unmapped exception types | add the five arms (`UserAuthenticationException` before its base) | None | client-side rehydration | 4 units `utf_baselib_data` + 1 flipped | Trivial | 1 h |
| 2b unknown error category rejected | carry the name as data, rebuild no `error_code` | Low | every `createExceptionFromObject` caller | 4 units `utf_baselib_data` + 4 `utf_baselib_security` + 1 flipped | Low | 2 h |
| 2c eight `errinfo_*` fields dropped | add 8 optional model properties, both directions; redact 2 | Low | wire document (additive, optional) | 4 units `utf_baselib_data` + json-spirit release run + 1 flipped | Low | 4 h |
| 3a `exceptionMessage` not redacted | replace with the friendly message unless user-friendly | Low-Med (client-visible text change) | HTTP error bodies of the default backend | 4 units `utf_baselib_data` + 4 `utf_baselib_http` + 2 flipped | Trivial | 2 h |
| 3b escaper only for JSON | per-content-type escaper; reject unknown types when escaping is on | Low-Med (startup failure for an unknown type) | broker → auth service hop | 4 units `utf_baselib_security` + 1 flipped + 2 cases | Low | 3 h |
| 3c gateway error bodies unredacted | redact in `RestUtils`, explicit opt-out | Low-Med (internal deployments lose the dump over HTTP) | HTTP gateway error responses | 4 units `utf_baselib_rest` + 2 flipped | Trivial | 2 h |
| 4 `BoolSwitchOrMultiStringOption` | withdraw the typedef | None (compile error for a consumer naming it) | none in-tree | 4 units `utf_baselib_cmdline` | Trivial | 0.5 h |
| 5a two Windows fixes unverified | run the existing cases on a Windows host | None | Windows only | `utf_baselib` on `vc143` + `ccl16`, debug + release | Trivial | 1 h (Windows) |
| 5b `ComUtils.h` / `WindowsShellShortcut.h` includes | add the three includes | None | Windows only | `utf_baselib` (the `_WIN32` block of the header sweep) | Trivial | 0.5 h (Windows) |
| 5c seven never-compiled Windows test tasks | compile and run once, test-only repairs | None | Windows test code | same runs as 5a | Low | 3 h (Windows) |
| 6 plan errata / report corrections | prepend an erratum section; correct the two reports | None | documents | none | Trivial | 1.5 h |
| 7.1 `safeDeletePathNothrow` returns true on escape | return the real result | None | `safeDeletePathNothrow` callers (3 in-tree) | 4 units `utf_baselib` + 1 flipped | Trivial | 0.5 h |
| 7.2 `writeTextFile` truncates before validating | validate the encoding before `fopen` | None | `writeTextFile` callers | 4 units `utf_baselib` + 1 flipped | Trivial | 0.5 h |
| 7.3 `ScopeGuardT` move-assign drops cleanup | run the target's callback first | Low | every `BL_SCOPE_GUARD` user (move-assign only) | 4 units `utf_baselib` + 1 flipped | Trivial | 0.5 h |
| 7.4 `DataBlock::write( string )` not atomic | check `4 + size` before the first write | None | JNI helpers, tests | 4 units `utf_baselib` + 1 flipped | Trivial | 0.5 h |
| 7.5 ICMP checksum field not zeroed | `checksum( 0 )` first | None | pinger | 4 units `utf_baselib` + 1 flipped | Trivial | 0.25 h |
| 7.6 excluded paths stored verbatim | normalise in the constructor | Low | `ExcludedPathsControlToken` users | 4 units `utf_baselib_tasks` + 1 flipped | Trivial | 0.5 h |
| 7.7 timer tasks ignore the local pool | use `getThreadPool( eq )` | Low-Med (moves where every observable's timer runs when a local pool is set) | all timer tasks with a local-pool queue | 4 units `utf_baselib_tasks` + 4 `utf_baselib_basictask` + 1 flipped | Low | 2 h |
| 7.8 subscription handle needs a `shared_ptr` holder | decision (13.9 item 11): document, or hold a strong ref in the disposer | Low-Med | reactive pipelines | 4 units `utf_baselib_tasks` + 1 flipped | Low | 2 h |
| 7.9 `StatusStrings::get()` substitutes 500 | keep; no wire caller remains | None | none | none | Trivial | 0 h |
| 7.10 quoted charset unsupported | strip quotes from the capture | None | HTTP client charset decode | 4 units `utf_baselib_http` + 1 flipped | Trivial | 0.5 h |
| 7.11 `chk4ServerErrors` narrows to one `uint32` | keep (wire protocol); record | None | none | none | Trivial | 0.25 h |
| 7.12 proxy `isConnected()` always true | override with the channel's state | Low | proxy REST admission | 4 units `utf_baselib_messaging` + 4 `utf_baselib_rest` + 1 flipped | Trivial | 1 h |
| 7.13 manifest `as_object()` unguarded | `BL_CHK_USER_FRIENDLY( is_object() )` | None | plug-in loading errors | 4 units `utf_baselib_loader` + 1 case | Trivial | 0.5 h |
| 7.14 `RemoveChunk` + `IgnoreIfNotFound` on a never-saved chunk | keep (flag semantics) | None | none | none | Trivial | 0 h |
| 7.15 `removeOption` / `removeCommand` desync | erase both structures by the same key | None | cmdline framework teardown | 4 units `utf_baselib_cmdline` + 1 case | Trivial | 1 h |
| 8.1 dry-run hide / unhide not RAII | `BL_SCOPE_EXIT` | None | help rendering | 4 units `utf_baselib_cmdline` + 1 case | Trivial | 0.5 h |
| 8.2 watchdog negative interval / horizon | two `BL_CHK`s | None | `Watchdog` constructor domain | 4 units `utf_baselib` + 2 cases | Trivial | 0.5 h |
| 8.3-8.5 three keep decisions | no action | None | none | none | - | 0 h |

Totals: about 32 h on Linux across 5 reviewable stages, about 5 h on a Windows host, 1.5 h of documents.

## Decisions requested up front

| # | Decision | Recommendation | Item |
|---|---|---|---|
| D1 | Change `RetryableWrapperTask` cancellation semantics? | Yes, wrapper-local latch | 1 |
| D2 | Map the five unmapped exception types? | Yes | 2a |
| D3 | Unknown error category on the wire | Rehydrate as data, do not reject | 2b |
| D4 | The eight dropped `errinfo_*` fields | Serialize all eight; redact the two stack/thread fields | 2c |
| D5 | Redact `exceptionMessage` / `properties.message`? | Yes, unless user-friendly | 3a |
| D6 | Escaper for non-JSON content types | Per content type; reject unknown when escaping is on | 3b |
| D7 | Gateway error bodies (13.9 item 5) | Redact, with an explicit opt-out | 3c |
| D8 | `BoolSwitchOrMultiStringOption` | Withdraw | 4 |
| D9 | Plan document errata | Prepend an erratum section; no in-place spec edits | 6 |
| D10 | The pinned behaviours of section 7 | Per-row recommendations in item 7 (eleven fixes, three keeps, one open decision) | 7 |

---

## Item 1 - D-01: cancellation must stop the retry loop (Fix, D1)

**Where.** `src/include/baselib/tasks/TaskBase.h`: `ForwarderTaskBaseT::requestCancel()` (`:380`) forwards
to the current target only; `RetryableWrapperTaskT::continuationTask()` (`:2100-2163`) swaps in a
`SimpleTimerTask` for the sleep and then a fresh factory task; `SimpleTimerTaskT::run()` (`:1987-1996`)
returns `time::neg_infin` **without an exception** when cancelled, so the `m_wrappedTask -> exception()`
guard in the retry branch never fires and a new work task is born uncancelled.

**Design intent.** `ForwarderTaskBase` exists so that "one task can execute multiple other tasks
dynamically and switch the tasks as necessary on each call or via continuations" (`:295-301`);
`WrapperTaskBase` keeps the execution queue's view of a single task while the wrapped task is replaced
(`:394-399`). The retry wrapper composes those with the timer task to express "attempt, sleep, attempt"
without a dedicated thread. Cancellation in this library is a **request** observed cooperatively:
`TaskBase::m_cancelRequested` is an atomic that async tasks act on in `cancelTask()` and CPU tasks only
mark (`:512-525`), and the forwarder assumes the current target *is* the operation. That assumption is
exactly what the wrapper breaks: the operation is the whole retry sequence, and only the wrapper knows it.
The idiomatic fix is therefore a cancel latch **in the wrapper**, not a change to the timer task or to the
forwarder, both of which serve other users correctly.

**Performance.** The retry path runs only after a failure, so the intended design costs nothing on the
success path; the fix adds one atomic load per continuation on the failure path and nothing elsewhere.

**Change.** In `RetryableWrapperTaskT` only: (1) `std::atomic< bool > m_cancelRequested`; (2) override
`requestCancel()` to set it and then `base_type::requestCancel()` (the in-flight work task or sleep timer
is still cancelled); (3) in `continuationTask()`, inside the existing `BL_MUTEX_GUARD( m_lock )` and before
either branch, if the latch is set: keep the work task's own exception when present, otherwise set an
`operation_aborted` `SystemException` (`errinfo_is_expected( true )`, the shape the handler macros already
treat as expected) on `m_wrappedTask`, and `return nullptr`. The check sits under the same lock that
guards the swap, so a cancel arriving between the check and the swap reaches the new target through the
forward, completes it cancelled, and the next `continuationTask()` stops.

**Options.** (1) The latch above, recommended. (2) Make `SimpleTimerTaskT::run()` throw on cancel:
rejected, it changes every `SimpleTimer` user and still leaves a fresh factory task uncancelled.

**Risk and blast radius.** Low. No consumer under `src/include` or `src/local`; public API only. Visible
change: a cancelled retryable task completes failed with `operation_aborted` (or the last work error)
instead of running out its budget.

**Tests.** `Tasks_RetryableWrapperTaskCancelTests` (`src/utests/utf_baselib_tasks/TestTasks3.h:887`) flips:
`factoryCalls <= 2`, elapsed well under `maxRetryCount × retryTimeout`, `isFailed()`, exception is
`operation_aborted` or the work error. New case: cancel while the sleep timer is the target, asserting no
further factory call. `Tasks_RetryableWrapperTaskCancelStressTests` and `Tasks_RetryableWrapperTaskTests`
unchanged.

**Size / complexity / cost.** ~30 lines production, ~50 lines test; Low; 3 h including the 4-unit matrix.

## Item 2 - server-error wire fidelity (Fix, D2-D4)

All in `src/include/baselib/data/eh/ServerErrorHelpers.h` and the model
`src/include/baselib/data/models/ErrorHandling.h`.

**Design intent (shared).** `createServerErrorResultObject` (`:51-150`) copies every `errinfo_*` tag the
model knows into `ExceptionProperties`, and `createExceptionFromObject` (`:246-412`) rebuilds a **typed**
`bl::` exception so a client's `catch( SpecificException& )` works across the wire; an unknown type
deliberately falls back to `UnexpectedException` (`:406-412`) so a newer server never breaks an older client.
The model is additive by design: every property is optional, unknown JSON keys are retained in the data
model's unmapped set, and no reader parses `ServerErrorJson` strictly (to be confirmed with one grep at
execution). Performance of the intent: the whole path runs only when an error is serialized; a string
ladder of ~25 comparisons per error and one JSON document per error are the costs, and none of the fixes
below changes that order.

### 2a. Five missing arms (D2)

`bl::BufferTooSmallException`, `bl::NotFoundException`, `bl::UserAuthenticationException`,
`bl::NumberCoerceException`, `bl::PrintableWrapperException` (declared at `core/ErrorHandling.h:752-771`)
have no arm. Add one `else if` each in the same shape; `UserAuthenticationException` derives from
`UserMessageException` (`:768`) and its arm must precede the base's. Replacing the ladder with a
`{ name, factory }` table (the review's T-6 idea) is optional and only if the diff stays readable.
Tests: `ErrorToJsonExceptionTypeMappingTests` (`src/utests/utf_baselib_data/TestServerErrorHelpers.h:457`)
moves the five to the mapped half; the 24 count guard stays. **None / client rehydration / 4 units
`utf_baselib_data` + 1 flipped / Trivial / 1 h.**

### 2b. Unknown category (D3)

The category chain (`:275-301`) resolves `"generic"` and `"system"`, accepts `""`, and throws
`ArgumentException` for anything else. Intent: an `error_category` is a process-local object, so a code
can be rebuilt only for the two categories every process can name; refusing to fabricate a code with the
wrong category is right. What is wrong is refusing the **whole document**. Change the `else` arm to
`errorCategory = nullptr`, and in `exceptionFromProperties` (`:170-178`) attach `errinfo_category_name`
whenever the property is non-empty (today only together with a rebuilt code) so the name and
`systemCode` survive as data. A `SystemException` with an unknown category rehydrates as
`SystemException` from `exceptionProperties -> systemCode()` (the `SystemException` arm at `:385-404`
needs the same relaxation of its `! errorCategory` check). Do **not** teach the chain `"OpenSSL"`:
`data/eh` must not depend on `crypto/`. Tests:
`CryptoErrorHandling_OpenSslCategoryDoesNotSurviveServerErrorRoundTrip`
(`src/utests/utf_baselib_security/TestCryptoUtils.h:680`) flips to "survives as a string category";
a positive control in `ErrorToJsonSystemCodeDerivationTests` (`:640`). **Low / every
`createExceptionFromObject` caller (rest bridge, messaging clients) / 4 units `utf_baselib_data` +
4 units `utf_baselib_security` / Low / 2 h.**

### 2c. Eight dropped fields (D4)

`errinfo_hint`, `errinfo_original_type`, `errinfo_original_thread_name`, `errinfo_original_stack_trace`,
`errinfo_service_status`, `errinfo_service_status_category`, `errinfo_service_status_message`,
`errinfo_error_uuid` (`core/ErrorHandling.h:466-497`) post-date the model. Intent of each: the
`service_status*` trio carries the authorization service's verdict, `error_uuid` is the retry
discriminator of `isRetryableMessagingBrokerError`, `hint` is operator guidance, the `original_*` trio
records a cross-thread or JNI rethrow. Add eight optional properties to `ExceptionProperties`
(`errorUuid` as a string, converted at the boundary), one serializer line each in
`createServerErrorResultObject` (`:80-115`) and one deserializer line each in `exceptionFromProperties`
(`:180-225`); blank `originalStackTrace` and `originalThreadName` in `getRedactedServerErrorAsJson`
(`:435-472`). Performance: only set fields are serialized; a stack trace can be kilobytes, so cap
`originalStackTrace` at the serializer (e.g. 4 KiB) rather than let one error inflate a response. Wire
compatibility both ways by construction (optional, additive). Tests: `ErrorToJsonUnmappedErrorInfoTests`
(`:774`) flips from "dropped" to "round-trips" for all eight; `ServerErrorHelpersRedactionTests` (`:1162`)
gains the two blanked fields. **Low / wire document / 4 units `utf_baselib_data` + one
`make -k -j1 utf_baselib_data VARIANT=release BL_USE_JSON_SPIRIT=1` run for the model change / Low / 4 h.**

## Item 3 - redaction (Fix, D5-D7)

### 3a. `exceptionMessage` and `properties.message` (D5)

`getRedactedServerErrorAsJson` (`ServerErrorHelpers.h:425-472`) documents its intent: "the full diagnostic
information is intended for trusted internal services and it stays in the server logs; a response which
can reach an untrusted client must not carry the exception dump, the source file / function names, the
task information or the addresses of the server side endpoints". The model already applies a
friendliness rule to `result.message` (`createServerErrorResultObject:134`; model comment
`models/ErrorHandling.h:141-146`); `exceptionMessage` (raw `what()`) and `properties.message`
(`errinfo_message`) were left out of the redaction by oversight, and `BL_MSG()` text routinely carries
paths. Change: when `isUserFriendly` is not set, replace `exceptionMessage` with `result -> message()`
and clear `properties -> message()`. Performance: none (error path). Risk: Low-Med, a client-visible text
change on internal errors; user-friendly errors keep their text. Tests: `ServerErrorHelpersRedactionTests`
(`:1162`) and `BaseLib_HttpServerStdErrorResponseRedactionTest`
(`src/utests/utf_baselib_http/TestHttpServer.h:1209`) flip, each keeping a user-friendly positive control.
**Low-Med / default-backend HTTP error bodies / 4 units `utf_baselib_data` + 4 `utf_baselib_http` /
Trivial / 2 h.**

### 3b. Escaper for non-JSON content types (D6)

`AuthorizationServiceRest.h:436-446` installs `escapeForJson` only when the configured content type
contains `json`. Intent: the S-2 decision made escaping the default with an opt-out
(`escapeTemplateVariables`, absent = true), and `StringTemplateResolver::escaper_callback_t` states the
principle: "a caller which builds a structured document out of a template - a URL, a JSON body - has to
encode them for that structure". The JSON-only condition implemented the review's literal wording ("JSON
escape for a JSON content type") and left every other body structure unencoded while the path stays
encoded. Change: add `escapeForFormUrlEncoded` (`str::uriEncode`, selected for `x-www-form-urlencoded`)
and `escapeForXml` (`&`, `<`, `>`, `"`, `'`, selected for `xml`); with escaping on and no escaper for the
configured type, `BL_THROW( ArgumentException )` when the service is constructed, so the failure is at
startup with a clear message rather than raw bytes at the first authorization. Performance: one encode
per authorization round trip, already paid for JSON. Risk: Low-Med, a deployment with an unusual content
type and escaping on fails at startup; the release note (`notes/plans/issues/devenv7-breaking-changes-release-notes.md`,
section 11.1) gains a paragraph. Tests: `AuthorizationServiceRest_TemplateEscapingTests`
(`src/utests/utf_baselib_security/TestAuthorizationServiceRest.h:84`) sub-case (4) flips to "escaped";
add an XML case and the reject case. **Low-Med / broker → auth service hop / 4 units
`utf_baselib_security` / Low / 3 h.**

### 3c. Gateway error bodies (D7, 13.9 item 5)

`rest/RestUtils.h:130` (`formatEhResponseSimpleJson`) calls the unredacted `getServerErrorAsJson`;
`httpserver/ServerBackendProcessingImplDefault.h:172` calls the redacted variant. Intent: the review's N-9
targeted "an internet-facing gateway", but its fix landed only in the default backend; the gateway's
tests then pinned the disclosure (`src/utests/utf_baselib_rest/TestRestDefault.h:736`, `:870`). The
gateway is the app the review named as network-facing (`MessagingHttpGatewayApp.h` runs `HttpSslServer`),
so the same redaction belongs there. Change: a `redactErrorResponses` flag on the gateway's processing
context (default true) selecting the redacted helper in `RestUtils`; the opt-out keeps internal
deployments that scrape the dump over HTTP working. Performance: none. Tests: the two pins flip to
`"<redacted>"`; one sub-case exercises the opt-out. Release note under section 11.6. **Low-Med / HTTP
gateway error responses / 4 units `utf_baselib_rest` / Trivial / 2 h.**

## Item 4 - `BoolSwitchOrMultiStringOption` (Fix, D8)

**Where.** `src/include/baselib/cmdline/Option.h:329`; `SwitchImpl::decorateSemantic` (`:284-291`) applies
`zero_tokens()` after `Option::getSemantic()` has applied `multitoken()` (`:145-149`), so the option can
never consume a token. Record: `notes/plans/issues/cmdline-boolswitch-or-multistring-option-deferral.md`.

**Design intent.** The framework composes `Option< T, IMPL >` from a value type and an `IMPL` policy:
`OptionImpl` for valued options, `SwitchImpl` for value-less switches ("The switch does not accept any
value on the command line", `:321-327`), with the `MultiValue` flag gating `multitoken()`. The typedef
tried to express a hybrid, "a flag, optionally followed by values", which Boost.Program_options models
with `implicit_value`, not `zero_tokens`; the policy chosen contradicts the name. It has no consumer.
Performance: none either way.

**Options.** (1) **Withdraw**, recommended: delete the typedef, the `CmdLine_BoolSwitchOrMultiStringOption`
case and its fixture (`src/utests/utf_baselib_cmdline/TestCmdLine.h:1788-1860`), close the record; a
consumer naming the typedef gets a compile error and uses `BoolSwitch` plus `MultiStringOption`. (2) Make
it work: a `SwitchImpl< std::vector< std::string >, ... >` specialisation applying
`implicit_value( std::vector< std::string >() )`; Boost consumes an implicit value only in the adjacent
form (`--flag=a`) and the `multitoken` interaction must be verified against Boost 1.90 in the fix's own
test. Only worth it if a downstream consumer is known.

**Risk and blast radius.** None in-tree; source-compatibility note in the release notes.
**Size / complexity / cost.** ~5 lines production, ~80 lines removed from tests; Trivial; 0.5 h.

## Item 5 - Windows session (Fix, needs a Windows host)

Extend `notes/plans/issues/whole-library-windows-residuals-instructions.md` (the existing handoff for items
10-14 of the deferral record) with a section "Test-enhancement residuals":

- **5a. Execute the two inspection-only fixes.** `OSImplWindows.h:3151` (`USERDOMAIN`) and `:1402`
  (`BL_ASSERT( ! callbackIos || out )`). Intent: `tryGetUserDomain()` is the UNIX/Windows twin of a
  local-vs-domain user check, and the merged-redirect branch only needs the merged pipe, the ios stream
  being optional; both fixes are the one-token changes the UNIX twins already carry. Cases in the tree:
  the T061 oracle (`expectedUserDomainFromEnvironment()` in `TestBaselibDefault.h`) and the T059
  file-callback block of `BaseLib_OSCreateProcessRedirectedMergedTests`. Run `utf_baselib` on `vc143`
  and `ccl16`, debug and release. **None / Windows only / Trivial / 1 h.**
- **5b. Self-containment of `core/specific/ComUtils.h` and `WindowsShellShortcut.h`.** Intent: every
  public header compiles standalone, enforced on Linux by `TestPublicHeaderInstantiation.cpp` (T373),
  whose `_WIN32` block (`:57-59`, `:308-323`) arms the check for these two only on Windows. Same shape as
  the four Linux fixes (`git diff cb431f0..HEAD -- src/include/baselib/loader/Version.h`): `ComUtils.h`
  gains `#include <baselib/core/BaseIncludes.h>` (covers `BL_THROW`, `SystemException`,
  `cpp::SafeUniquePtr`); `WindowsShellShortcut.h` gains `<baselib/core/Logging.h>` and
  `<baselib/core/FsUtils.h>`. Performance: none (compile-time). **None / Windows only / Trivial / 0.5 h.**
- **5c. The seven Windows-only test tasks that have never compiled** (plan 13.4.1: T054's Windows arm,
  T058, T060, T061, T062, T253, T357): compile and run once, repair test code only. T253's dropped HKLM
  hive assertion and T058's disabled argv case (`TestBaselibDefault5.h:649`,
  `productionArgvQuotingIsFixed = false`) become live once W-1 and W-3 of the same handoff are fixed.
  **None / Windows test code / Low / 3 h.**
- Record outcomes in the deferral record's outcome table and in the source document, section 2.

## Item 6 - plan document errata and report corrections (Record, D9)

Prepend an "Errata (2026-09-08)" section to `whole-library-cxx-test-enhancement-plan.md` listing the six
errors already recorded (T006/T010 step 4, T028, T006 step 3, T012, T082, T378) plus T041 (a non-void
`tryCatchLog` without a callback trips `Utils.h`'s `BL_ASSERT`), T193 (`/authorize?t=opaque-token-value`
is unattainable, `-` is percent-encoded), T334 (the first bundled root is X.509 v1), T376 (the placeholder
literal belongs to the `dm::Payload` operator, the trace site streams `AsyncRpcPayload`), T181 (the
"present token on the acknowledgment path" guard does not exist), and the stale-line-numbers note; specs
are not edited in place. Correct the source document: section 4.2 attributes "a count guard" to T029
(none exists); section 7 omits the pins made by T331, T335, T368 and T255 (`disposeGlobalThreadPool`
disposes twice, harmlessly); section 2 omits the T253 hive assertion and the disabled T058 case; section 8
gains the records this plan produces. **None / documents / Trivial / 1.5 h.**

## Item 7 - the pinned behaviours of section 7 (D10)

Executed as one change per module so each diff stays reviewable; each fix flips exactly the case named.

### 7.1 `fs::safeDeletePathNothrow` returns `true` when the deletion throws
`core/FsUtils.h:570-597`. Intent: "nothrow" delete for cleanup paths, logging and returning `false` on a
reported error; the `BL_WARN_NOEXCEPT_END` swallows an *escaping* exception and control falls through to
`return true`. Fix: `bool result = false;` before the block, set `true` only on the success path, return it
after. Performance: none. Risk: None; three in-tree callers (`makeHidden` rollback at `:809`, two more)
treat `false` as "log and continue". Test: `FsUtils_UnsupportedFileTypeRemovalTests`
(`TestBaselibDefault4.h:169`). **Trivial / 0.5 h.**

### 7.2 `encoding::writeTextFile` truncates before the encoding switch
`core/FileEncoding.h:57-135`. Intent: a one-call "write text with encoding"; validation lives in the
`default:` arm (`:127-133`) and the unsupported UTF-16 arm on UNIX (`:113-121`), both reached after
`os::fopen( "wb" )`. Fix: move the validation ahead of the `fopen` (a small `chkEncodingSupported`), so an
invalid or unsupported encoding never truncates an existing file; write-to-temp-and-rename is more than the
defect needs. Performance: none. Risk: None. Test: the truncation block of `BaseLib_TextFilesEncodingTests`
(`TestBaselibDefault.h:8753`). **Trivial / 0.5 h.**

### 7.3 `ScopeGuardT` move-assignment drops the target's pending cleanup
`core/CPP.h:559-600`. Intent: RAII cleanup that can be moved so a guard can be returned or stored
(`BL_SCOPE_GUARD`); the move constructor dismisses `rhs`, the move assignment overwrites `m_cb` without
running the callback already armed in `*this`. Fix: `runNow()` on the target first (matching the destructor
rule "an armed guard always runs once"), then take `rhs`. Performance: none. Risk: Low, only code that
move-assigns into an armed guard changes; no such site was found in-tree. Test: `BaseLib_TestScopeGuard`
(`TestBaselibDefault.h:5409`). **Trivial / 0.5 h.**

### 7.4 `DataBlock::write( const std::string& )` is not atomic
`data/DataBlock.h:360-366`; `write( ptr, size )` (`:327-340`) checks capacity per call and throws
`BufferTooSmallException`. Intent: a minimal length-prefixed codec for the JNI helpers; the two-step write
was never meant to be transactional but leaves a dangling 4-byte prefix on failure. Fix: check
`m_size + 4 + textSize <= m_capacity` once, before the first `write`. Performance: one comparison. Risk:
None. Test: `BaseLib_DataBlockReadWriteCodecTests`. **Trivial / 0.5 h.**

### 7.5 `IcmpHeader::computeChecksum` never zeroes the checksum field
`core/NetUtils.h:576-598`. Intent: RFC 1071 checksum over a header the pinger builds fresh per request, so
the field is zero by construction; the omission is a latent trap for any reuse. Fix: `checksum( 0 )` before
summing (the sum already skips the field, so this only makes the contract explicit). Performance: none.
Risk: None. Test: the checksum case in `TestNetUtils.h`. **Trivial / 0.25 h.**

### 7.6 `ExcludedPathsControlToken` normalises lookups but stores entries verbatim
`tasks/utils/ExcludedPathsControlToken.h:42-66`. Intent: compare scanned entries against the exclusion set
after `fs::normalize` (and lower-casing on Windows); the constructor trusts the caller to pass already
normalised strings. Fix: apply the same normalisation to each entry in the constructor. Performance: once
per entry at construction. Risk: Low, an un-normalised entry now excludes what the caller meant. Test:
the T216 case in `utf_baselib_tasks`. **Trivial / 0.5 h.**

### 7.7 `TimerTaskBaseT::resetTimer` ignores the queue's local thread pool
`tasks/TaskBase.h:1721-1729` binds the timer to `ThreadPoolDefault::getDefault( getThreadPoolId() )`, while
`TaskBaseT::getThreadPool( eq )` (`:789-802`) honours `eq -> getLocalThreadPool()` for every other task.
Intent: timer tasks predate local pools; `ShutdownTaskT` documents that it ignores the local pool, the
timer base documents nothing (13.9 item 12). Because every `ObservableBase` is a timer task, this decides
where all reactive pipelines' timers run, and it is why `utf_baselib_basictask` cannot host any timer task.
Fix: `resetTimer( eq )` using `getThreadPool( eq ) -> aioService()`. Performance: none per tick; the
scheduling location changes only when a local pool is configured. Risk: Low-Med, a behaviour change for
consumers that set a local pool and relied on timers staying on the default pool. Test: the T382 case in
`utf_baselib_tasks` flips; `utf_baselib_basictask` can then gain a timer case. Land as its own change.
**Low / 2 h.**

### 7.8 A discarded subscription handle unsubscribes only with a `std::shared_ptr` holder
`reactive/ObservableBase.h:55-92` (`ObserverDisposerT` holds `std::weak_ptr< ObservableBase >`) and
`subscribe()`, which creates the only `shared_ptr` as a local. Intent: the disposer must not keep the
observable alive (no strong back-reference), so it holds a weak pointer that is meaningful only when the
caller manages the observable through `om::getSharedPtr`; four automated cases discard the handle and rely
on nothing happening. Decision (13.9 item 11): either document `om::getSharedPtr` as the contract, or hold
an `om::ObjPtr< ObservableBase >` in the disposer (the handle is owned by the subscriber, so no cycle) and
make the disposer always effective. Recommendation: the second, since the first leaves a silent no-op.
Performance: one refcount per subscription. Risk: Low-Med, the four discarding cases start unsubscribing
and must be reviewed. Test: T348 flips. **Low / 2 h.**

### 7.9 `StatusStrings::get()` substitutes the 500 line for any unknown code
`http/Globals.h:373-452`. Intent: a static table of full status lines for the codes the library emits; N-9
added `getStatusLine()` (`:454`) for the wire and routed `Response::buildResponse` through it. No
production caller of `get()` remains (`grep 'StatusStrings::get('` under `src/include` and `src/local` is
empty), so the substitution is unobservable. Recommendation: keep; optionally remove `get()` in a later
tidy-up. **Trivial / 0 h.**

### 7.10 `g_charsetRegex` rejects a quoted charset
`http/SimpleHttpTask.h:1036` (`\bcharset\s*=\s*([^;]+)\b`), used at `:1014`. Intent: pull the charset
token out of `Content-Type` to decide the ISO-8859-1 conversion (`:946-975`); RFC 7231 allows the quoted
form, which the trailing `\b` captures as `"UTF-8` and sends down the unsupported-charset arm. Fix: strip a
surrounding quote pair from the capture (or `"?([^;"]+)"?` in the regex). Performance: none. Risk: None,
a quoted charset that used to be rejected now decodes. Test: the charset case in `TestClientHttpTasks.h`.
**Trivial / 0.5 h.**

### 7.11 `chk4ServerErrors()` narrows a backend failure to one `uint32`
`messaging/TcpBlockTransferServer.h:331-380`. Intent (comment): specific exceptions propagate to the client
as `ServerErrorException`, everything else is fatal for the server; the wire `CommandBlock` carries a single
`uint32` error value, so only `errno` or a generic-category code can travel. Changing that is a V1/V2
protocol change with the M-3 deferral's compatibility constraints. Recommendation: keep; record the
limitation next to the M-3 deferral. **Trivial / 0.25 h.**

### 7.12 `ProxyBrokerBackendProcessing` inherits `isConnected() == true`
`messaging/ProxyBrokerBackendProcessingFactory.h:52` (no override), `isFullyDisconnected()` at `:373-395`;
`BackendProcessing::isConnected()` is pure virtual (`BackendProcessing.h:81`) and
`ForwardingBackendProcessing` overrides it (`ForwardingBackendProcessingImpl.h:211`). Intent: the base
default treats a backend as connected unless it tracks a connection; the proxy does track one (for
pruning and logging) and both REST consumers gate admission on `isConnected()`
(`BaseRestServerProcessingContext.h:482`, `HttpServerBackendMessagingBridge.h:662`). Fix: override
`isConnected() const NOEXCEPT` as `m_outgoingBlockChannel -> isConnected()` (not via
`isFullyDisconnected()`, which logs and is non-const). Performance: one delegated call per admission,
already what the forwarding backend pays. Risk: Low, requests to a disconnected proxy now fail fast with
the same error the forwarding backend gives. Test: the T170 proxy pin (`TestMessagingDefault.h:9650`)
flips. **Trivial / 1 h.**

### 7.13 `ManifestFactoryT::read()` calls `as_object()` with no precondition
`loader/Manifest.h:316-330`. Intent: manifests are produced by the library's own tooling, and every other
malformed-manifest path yields a user-friendly `UnexpectedException` (e.g. `getRequiredProperty`); the
top-level object check is the one missing. Fix: `BL_CHK_USER_FRIENDLY( value.is_object(), ... )` before the
cast. Performance: none. Risk: None. Test: a malformed-manifest case in `TestManifest.h`.
**Trivial / 0.5 h.**

### 7.14 `RemoveChunk` with `IgnoreIfNotFound` on a never-saved chunk succeeds
Intent: that is what the flag means (idempotent removal). Recommendation: keep. **0 h.**

### 7.15 `CommandBase::removeOption` / `removeCommand` erase by name and by pointer identity
`cmdline/CommandBase.h:193-215` (`removeCommand`), `:229-262` (`addOption` / `findOption`; `removeOption`
in the same block). Intent: options and commands are members of the command class, destroyed in reverse
order ("it's most likely that the command being removed will be right at the end", `find_last`); the map
is keyed by name, the vector keeps registration order. The desync only happens when a *different* object
carrying a registered name is removed, i.e. a misuse, but it fires `BL_ASSERT` at `:211` / `:247` and
aborts a debug run. Fix: erase the vector entry by the same key (`getName()` / `getCommandName()`), or
`BL_CHK` that the pointer matches the registered one before erasing. Performance: none. Risk: None.
Test: a new case in `TestCmdLine.h` (T109's amendment lifts once fixed). **Trivial / 1 h.**

## Item 8 - the six records of section 8

- **8.1 `cmdline-dryrun-hide-unhide-not-raii-deferral.md`**: fix now. `CommandBase::getOptionsHelp()`
  (`CommandBase.h:778-780`) hides the root's `dryrun,n` option for non-applicable commands and unhides it
  after rendering; intent is a scoped presentation tweak, and a throw between the two leaves `--dryrun`
  hidden for the process. The record's own `BL_SCOPE_EXIT` is the idiomatic fix. Performance: none.
  Risk: None. Test: `CmdLine_DryRunNotApplicableHidesParentOption` unchanged; add a throwing-render
  sub-case. **Trivial / 0.5 h.**
- **8.2 `watchdog-interval-and-horizon-validation-deferral.md`**: fix now. `core/Watchdog.h:88-104`
  rejects a zero interval only; intent is "must be greater than zero", and the `uint64_t` cast turns a
  negative interval into a huge one that silently disables the watchdog, while `expiringMonitors( horizon )`
  reports every monitor for a negative horizon. Two `BL_CHK`s (`is_negative()` in the constructor and on
  the horizon). Performance: none (construction and a per-call comparison). Risk: None, the accepted
  domain shrinks to what the documentation already states. Tests: two negative cases in
  `TestWatchdogArgumentValidation` (`src/utests/utf_baselib/TestWatchdog.h`). **Trivial / 0.5 h.**
- **8.3-8.5** `string-utils-dead-public-helpers-keep-decision.md`,
  `objmodel-single-threaded-config-keep-decision.md`, `timezonedata-get-default-timezone-keep-decision.md`:
  keep decisions, no action. `cmdline-boolswitch-or-multistring-option-deferral.md` is closed by item 4.

## Staging and order

| Stage | Contents | Host | Modules built (`-j1`, 4 units each) |
|---|---|---|---|
| A1 | Item 1 | Linux | `utf_baselib_tasks` |
| A2 | Item 2 (2a, 2b, 2c) | Linux | `utf_baselib_data` (+ json-spirit release run), `utf_baselib_security`, `utf_baselib_rest`, `utf_baselib_http` |
| A3 | Item 3 (3a, 3b, 3c), after A2 (same files) | Linux | `utf_baselib_data`, `utf_baselib_http`, `utf_baselib_security`, `utf_baselib_rest` |
| A4 | Item 4, items 8.1 and 8.2 | Linux | `utf_baselib_cmdline`, `utf_baselib` |
| A5 | Item 7 fixes, one change per module; 7.7 as its own change | Linux | per row |
| B | Item 5 (5a-5c) with items 10-14 of the Windows handoff | Windows | `utf_baselib`, `vc143` + `ccl16`, debug + release, `a64` |
| C | Item 6; D10 outcomes recorded; source document updated; release notes | none | none |

A1, A2, A4 are independent; A3 follows A2; A5 last so each module's test flips land together. Each stage
is one reviewable, logic-only diff per module, stopped for review and commit by the user before the next.

## Estimates

| Stage | Production | Tests | Effort |
|---|---|---|---|
| A1 | ~30 lines | ~50 | 3 h |
| A2 | ~120 | ~150 | 7 h |
| A3 | ~60 | ~120 | 7 h |
| A4 | ~10 | ~40 | 1.5 h |
| A5 | ~80 across 12 fixes | ~150 | 10 h |
| B | ~6 | test-only compile fixes | 5 h on the Windows host, plus the handoff's own items 10-14 |
| C | documents only | - | 1.5 h |

## Out of scope

The reconciliation itself (`whole-library-cxx-reconciliation-plan.md`); the two missing toolchain/variant
runs on the existing tree (a separate decision); section 13.10 defects not named in the source document;
any TSan, OpenSSL 1.1.1w or full-suite run; commits.
