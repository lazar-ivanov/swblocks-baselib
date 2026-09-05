# Plan: merge-gate items 4-8 from the Fable 5.1 PR review — residual work only, assessed for decision

**Date:** 2026-09-04
**Status:** implemented on `lazari2` (2026-09-04), all recommendations taken as approved by the user, delivered in one pass (no slicing); see "Implementation status" at the end for the verification that was run

Source: `notes/reviews/major/update_2025/v1/pr_review_analysis_fable51.md`, section 11 items 4-8. Items 1-3 (H-1..H-3) are done (`pr-review-fable51-merge-gate-h1-h3-plan.md`, commit `2da162a`). Nothing under items 4-8 has changed since the review baseline `873f0ba` except `TestTlsProtocolPolicy.h` (H-3 work); the working tree is clean.

This is the record copy of the assessment plan; the working copy lived in the planning directory during review.

---

## 1. Already decided or already done — excluded from this plan

Every decision record under `notes/plans/issues/` and `scripts/devenv7/docs/` was searched for each finding's subject. These parts of items 4-8 are covered by a standing decision or a landed fix and are **not** re-opened here:

| Review item | Covered part | Where it was decided | What remains (carried into §3) |
|---|---|---|---|
| M-1 | Non-recursive `m_lockNotify`; lock ordering `m_lockNotify` outer to `m_lock`; concurrent-by-default delivery | `execution-queue-notification-delivery-plan.md:532-548`, breaking-change record | Only the false "cannot be re-entered on the same thread" premise and the contract gap it created |
| M-3 | Numeric policy alignment for `int` and `uint64` (`checkedInt`, `checkedUInt64`), CXX-08 closed | `pr-review-residual-cxx-findings-plan.md` A3; `pr-review-deep-dive-residual-findings.md:34` | Only the `int64` accessor, which A3's test list ("`int64` overflow into `int`") never covered |
| M-5 | Fail-closed default; platform store deliberately not consulted; server role `verify_none` | B6 in `pr-review-residual-cxx-findings-plan.md:297-320`; `CryptoBase.h:400-406, 717-741` | Only the release note B6 itself demanded ("Needs a release note — the most user-visible change on the branch") and that was never written |
| M-7 | The principle "key on `BOOST_VERSION`, not devenv" and the `#error` tripwire pattern | R-9 / Bundle C, `OSImplPlatformCommon.h:1964-1990` | Only the two (three) sites the sweep did not reach |
| M-8 | `BOOST_VERSION` defined before the guard (F-16); 72-byte `CommandBlock` pinned (R-17); moving the define to `CPPFLAGS` rejected (F-18 item 2) | `medium-severity-findings-f11-f17-plan.md` F-16; `pr-review-opus5-residual-findings-plan.md:432-450, 753-757` | Wrong version in gate/comment, the include-order `#error`, an alignment pin. The header-hygiene record says F-16's class "should be fixed on its own merits; it is not covered by this deferral" |
| M-11 … M-17 (all devenv7 deployment-script findings) | The devenv7 bootstrap scripts as an area: download integrity and atomic download deferred to devenv8; the never-executed Windows source-download path deferred with the same trigger; `set -e` in privileged scripts deferred | `scripts/devenv7/docs/supply-chain-verification-deferral.md` (F-02; "Deferred defect" section `:184-218`); `notes/plans/issues/privileged-script-error-handling-deferral.md` | **No fix work in this plan** (user decision: deployment-script changes are too expensive to test — no Windows/macOS host, no CI). The residual is to **append the newly found defects to the deferral record** so they stop resurfacing — see §3, "Deferral record update" |
| Item 8 (I-3, L-7) | Process-local hashes; canonical form not JCS | Release note §4; F-11 | Only the wire-format sentence for key order and double text |

Everything else in items 4-8 (M-2, M-4, M-6, M-10) has **no** prior record and is a full residual.

---

## 2. Summary table — residuals only

Cost of testing is in *module × variant × toolchain* build units per `AGENTS.md` (focused builds, `-j1`, tests ≤ 5 concurrent), plus any non-build proof. Verification constraints that apply to every row: host is Ubuntu 24.04 arm64 with no PowerShell, no shellcheck, no macOS, no CI; local dist has Boost 1.90, OpenSSL 3.5.4 and 1.1.1w, json-spirit 4.08; `.venv/bin/pytest` runs as uid 1000.

| Item | Residual | Recommendation | Risk | Blast radius | Cost of testing | Complexity | Cost of implementation |
|---|---|---|---|---|---|---|---|
| **M-1** | Wrong re-entry claim; contract permits a pattern that self-deadlocks | Fix: correct both comments, add always-on per-queue re-entry guard, add cross-queue positive test | Very low: concurrent path untouched; serialized path has zero production users | `ExecutionQueueImpl.h` (wide recompile, no behaviour change); out-of-tree violators go from hang to diagnosed abort | 4 units (`utf_baselib_tasks`) | Low-medium (guard primitive) | 1.5 h |
| **M-2** | Public doc states a per-queue convention as a general invariant; 5 dependent comments | Fix: qualify the doc, add one sentence to each dependent comment | None (comments) | None | 2 units compile-proof (`utf_baselib_tasks`, `utf_baselib_blobtransfer`) | Trivial | 0.5 h |
| **M-3** | `value_to<int64_t>`, `get_int64`, `as_int64` unchecked on json-spirit | Fix: add `checkedInt64`, wire the three accessors, extend the existing test | Low: only values > `INT64_MAX`, only json-spirit, no production consumer | json-spirit backend only | 4 units incl. the mandated `VARIANT=release BL_USE_JSON_SPIRIT=1` run | Low | 0.75 h |
| **M-4** | Callback form lacks the `preverified` gate its doc claims; zero coverage of that form | Fix: add `preVerified` parameter, fail closed, update the one caller; add callback-form test | Very low: function is branch-new, caller already gates | `TlsPeerVerification.h`, `AsioSslStreamWrapper.h`, one test | 4 units (`utf_baselib_http`) | Low | 0.5 h (+1 h test) |
| **M-6** | Root + server test certs expire 2027-08-29; PEM is also embedded in `UtfCrypto.h`; aws/gcp fixtures are dead | Fix now: regenerate root (v3) + server cert, keep keys/CSR/SANs, re-embed two literals, add a regen script; aws/gcp: decision (delete recommended) | Low: key material, DN, SANs unchanged; mistakes fail loudly | Test-only; every utest binary loads the root, four modules handshake | 6 units + literal `diff` proof + one smoke run | Low-medium | 1.5 h |
| **M-7** | Bare `BL_DEVENV_VERSION >= 4` in `NetUtils.h`, `TestBoostAsioCompat.h` (+ `OpenSSLTypes.h:41`) | Fix: `BOOST_VERSION >= 106600` per Bundle C precedent; optionally the third site | None: same branch for every supported default (table in §3) | Public header; identical output for all in-repo builds | 2 units (`utf_baselib`) + preprocessor equivalence table | Trivial | 0.3 h |
| **M-8** | Gate/comment say 1.89 (truth: 1.86); no ordering diagnostic; no alignment pin | Fix: unconditional define, corrected comment, `#error` if `uuid.hpp` came first, `sizeof`-probe alignment pin | Low: `#error` can only fail a build that is already silently mis-laid-out (decision point, see §3) | Every TU recompiles; zero behaviour change | 4 units (`utf_baselib`, `utf_baselib_messaging`) + scratch negative probe | Low | 0.5 h |
| **M-10** | `os.walk` default `onerror=None` in three commands | Fix: `onerror` at all three sites; bl_tool fails fast, s3 records + forces `EXIT_FAILURE`; five tests | Low: behaviour changes only on `OSError` (silent success → failure) | `upload`, `verify`, `hash` | pytest only (0 units) | Low code; new test idiom | 1.5 h |
| **M-11 … M-17** | Seven deployment-script findings, of which one (M-11 atomic rename) and one (M-12 sibling typo) are already in the deferral record; the rest are new | **Record, do not fix:** append the new residual defects to the deferral record, each with location, symptom, the `AGENTS.md` rule it violates, and the one-line fix, under the record's existing "Deferred defect" pattern | None (Markdown) | None | None | Low | 0.75 h |
| **Item 8** | Release notes: fail-closed trust (M-5), key order + double text (I-3, L-7), header expectations (M-7, M-8) | Write three sections | None | None | None | Low | 0.75 h |

**Totals:** ~9.5 h editing; ~26 focused build units; two decision points (§5).

---

## 3. Residual details

Legend: **Design intent** = what the 2017 author, the branch's records, and this library's idioms say the right shape is. **Perf** = runtime cost of the fix.

### M-1 — `DeliverySerialized` same-thread re-entry (residual: premise + contract + guard)

**Verified.** `invokeNotifyCB` (`ExecutionQueueImpl.h:361-378`) takes non-recursive `m_lockNotify` and calls the observer; the comment at `:355-358` and the delivery plan (`:544-548`) say same-thread re-entry into `onReady()` is "provably impossible". It is not: `ExternalCompletionTaskIfT::markCompleted()` (`TaskBase.h:1498-1513`) → `notifyReadyImpl` → `cbReady()` (`:711`) runs synchronously on the caller's thread → `onReadyObserver` → `onReady()` → `invokeNotifyCB`. A serialized observer that completes a sibling `ExternalCompletionTask` of the same queue from inside `onEvent` double-locks a `std::mutex` on one thread. The public contract (`ExecutionQueueNotify.h:87-95`) permits `push_back`/`wait`/`flush`/`dispose` and "block waiting for another task" and forbids only "waiting on another delivery"; it does not exclude synchronous completion. `TaskBase.h:1483-1486` already forbids the same pattern for the *cancel* callback, so the rule has precedent.

**Exposure:** zero. All five `setNotifyCallback` sites use `DeliveryConcurrent`; the only `DeliverySerialized` user is `TestTasks.h:1651`.

**Design intent:** keep the non-recursive mutex (decided; "recursion would silently grant thread-exclusion while violating the logical exclusion being bought"). Narrow the contract. Make the violation diagnosable the way this library handles lock-order violations: `BL_RT_ASSERT` / `BL_RIP_MSG` (`core/CPP.h`, always on).

**Guard.** The library has no thread-id helper (`grep` for `this_thread::get_id`, `thread::id`, `gettid`, `pthread_self` in `src/include/baselib` is empty; `<atomic>` is at `ExecutionQueueImpl.h:32`, `<thread>` is not included). Options:

| Option | Verdict |
|---|---|
| `std::atomic< std::thread::id > m_notifyOwner` + `#include <thread>` | **Recommended.** Exact semantics; lock-free on libstdc++ (GCC 4.9+). MSVC 2013's 16-byte `thread::id` compiles with the spin-lock fallback — fine on a path about to take a mutex |
| `boost::this_thread::get_id()` | Rejected: not trivially copyable in older Boost, cannot be `std::atomic` |
| `m_lockNotify.try_lock()` | Rejected: UB on a mutex the caller owns, and `false` cannot separate "other thread holds it" (block) from "I hold it" (abort) |
| Fallback if devenv2 Windows rejects the first | `std::atomic< std::size_t >` of `std::hash< std::thread::id >` (idiom at `TestBaselibDefault.h:994`), 3-line swap |
| `BL_ASSERT` vs `BL_RT_ASSERT` | **Always-on.** Replaces a silent release hang; cost is one atomic load + two stores on the serialized path only |

Shape, serialized branch only: `BL_RT_ASSERT( m_notifyOwner.load() != std::this_thread::get_id(), "... re-entered the same execution queue on the same thread ..." )` before `BL_MUTEX_GUARD( m_lockNotify )`; store own id after acquiring; store `std::thread::id()` after `onNotify()` (a `void_callback_noexcept_t`, so no RAII wrapper). Per-queue by construction: a serialized callback on queue A may synchronously complete a task on queue B.

**Test.** A RIP aborts the process and the utf tree has no death-test facility, so the forbidden case cannot be asserted. Add `Tasks_ExecutionQueueNotificationSerializedCrossQueueReentryTest` (~50 lines on the existing `ExecutionQueueNotificationTestContext` / `ExecutionQueueCompletionControl` fixtures): two `DeliverySerialized` contexts, hook on A calls `controlB.completeNext()` from inside `onEvent`; both `TaskDiscarded` deliveries must arrive nested on the same thread with no hook failure. Pins the guard's per-queue granularity (a thread-local flag would fail it) and documents the forbidden mirror case in its comment. The existing `maxDepth == 1` test remains the exclusion proof. Confirm the RIP message manually during implementation (not committed).

**Comment edits.** `ExecutionQueueImpl.h:355-358`: state the real chain, that it is a documented contract violation detected by `m_notifyOwner` and reported with `BL_RT_ASSERT`, and keep the sentence on why a recursive mutex would defeat the exclusion. `ExecutionQueueNotify.h:87-95`: add "must never *synchronously complete* another task of the same queue from inside `onEvent()` (`markCompleted()`, `notifyReady()` on a sibling) — completion delivers that task's notification on the calling thread; the queue aborts rather than deadlocks; a *different* queue is fine", and qualify "may block waiting for another task" with "provided that completion happens on another thread". Cross-reference `TaskBase.h:1483-1486`. Also update `execution-queue-notification-delivery-plan.md:544-548` with a one-line "premise corrected by …" pointer.

**Perf:** serialized path only; concurrent path zero. **Verify:** `utf_baselib_tasks` debug/release gcc, debug clang; run all.

### M-2 — "size() can only decrease concurrently" (residual: full)

**Verified.** `onReady()` (`ExecutionQueueImpl.h:449-476`) pushes a *non-self* continuation via `pushInternalNoLock< false >` on the completion thread, growing `m_allTasks`, so `size()` (`:1146-1150`) increases with no `push_back` caller. `ExecutionQueue.h:73-74, 81` state the opposite as general. Dependent comments: **five**, not four — `FixedWorkerPoolUnitBase.h:183-186`, `ObservableBase.h:711-717`, `FilesPkgUnpkgBase.h:216-218`, `FilesPackagerUnit.h:590-595`, and `FilesUnpackagerUnit.h:1058-1066` (missed by the review). Nine continuation producers exist (the review's seven plus `TcpBlockTransferClient.h:1971` and `TaskBase.h:1379-1383`); none pushes into any of the three annotated queues (`m_eqChildTasks`, `SubscriptionInfo::eventsQueue`, `m_eqWorkerTasks`), so the code is correct today **by convention**, and the base classes have out-of-tree derivation surface.

**Design intent:** the invariant is real for those queues; a structural enforcement is not idiomatic. Do what the branch did for `canonicalize` and event collapsing: state the assumption where it is relied upon.

**Edit:** `ExecutionQueue.h`: "unless a task returns a *different* task from `continuationTask()` / `setContinuationCallback`, in which case the continuation is pushed to the front of the pending queue on the completion thread and `size()` increases". Each of the five comments: "assuming no task in this queue uses a non-self continuation, which none of the tasks pushed here does". **Verify:** compile `utf_baselib_tasks`, `utf_baselib_blobtransfer` (the only utest modules including those headers), debug gcc.

### M-3 — json-spirit `int64` accessors unchecked (residual of CXX-08)

**Verified.** `JsonSpiritImpl.h:517-521` (`value_to< std::int64_t >`) and `:560-563` (`get_int64`) return `v.as_int64()`; `value_wrapper::as_int64` (`:219-222`) inherits json-spirit's `get_int64`, which does `static_cast< int64_t >( get_uint64() )` (`json_spirit_value.h:442`). `checkedInt` / `checkedUInt64` exist; no `checkedInt64`. Boost.JSON: `value_to< int64_t >` on a `uint64` kind → `error::not_exact` (`value.hpp:3227`); `as_int64()` throws `not_int64`. The parity comment (`:414-429`) promises rejection rather than wrapping.

**Exposure:** latent. `BL_DM_DECLARE_INT64_*` (`DataModelObjectDefs.h:298-316`, via `value_to` at `:222`) has zero users; `get_int64` / `value_to< int64_t >` appear only in tests. So no production document is mis-read; it is a hole in the closed CXX-08 alignment, and it makes the `remapIncorrectValueTypeException` funnel (`DataModelObjectDefs.h:849-871`) unreachable for `int64`.

**Fix:** `detail::checkedInt64` — `if( v.is_uint64() )` throw the existing "out of range for the requested integer type" message, else `as_int64()` (json-spirit and Boost.JSON both store as `uint64` only above `INT64_MAX`, so parity is exact); wire into the three accessors. Test: extend `JsonNumericOutOfRangeIntIsRejected` (`TestJsonAbstraction.h:1613-1650`) with the `18446744073709551615` literal already at `:1660`, asserting rejection on both backends (message asserted only for `get_int64`, per the pattern at `:1597-1601`).

**Perf:** one branch on the legacy backend. **Verify:** `utf_baselib_data` debug gcc, debug clang, and `make -k -j1 utf_baselib_data VARIANT=release BL_USE_JSON_SPIRIT=1` on a clean tree (decision record: cite the command in the commit message).

### M-4 — `verifyPeerName` `preverified` gate (residual: full)

**Verified.** `TlsPeerVerification.h:126-148` takes `( peerName, verifyContext )`; the doc mentions `'preVerified'` but the function never sees it. Boost 1.90 `host_name_verification::operator()` (`host_name_verification.ipp:32-44`) begins `if (!preverified) return false;`. Sole caller `AsioSslStreamWrapper.h:161` is inside `if( preVerified )` (`:143`). `g_rfc2818VerifyCallback` (`:478-487`) is the public hook where a downstream could install it unguarded. `TestTlsPeerVerification.h` covers only `certificateMatchesPeerName`.

**Fix:** add `SAA_in const bool preVerified` as first parameter; `if( ! preVerified ) return false;`; rewrite the doc; caller passes `preVerified` (keeps its own block for the diagnostics at `:184-247`). Optional test (~40 lines, `#if OPENSSL_VERSION_NUMBER >= 0x10100000L` for `X509_STORE_CTX_set_error_depth` / `set_current_cert`): build an `X509_STORE_CTX`, wrap in `asio::ssl::verify_context`, assert `false` with `preVerified=false` and `true` with a matching name. Function is branch-new, so the signature change has no consumer. **Perf:** one bool test. **Verify:** `utf_baselib_http` debug/release gcc, debug clang.

### M-6 — expiring test certificates (residual: full)

**Verified.** `test-root-ca.pem` / `test-server-cert.pem` expire 2027-08-29; `aws-` 2027-09-07; `gcp-` 2027-10-04. `test-root-ca-key.pem` present. Nothing reads `certs/*.pem`: the PEM text is embedded in `src/utests/include/utests/baselib/UtfCrypto.h` (`getDevRootCA :38`, `getDefaultServerKey :68`, `getDefaultServerCertificate :105`, `getIpAddressServerCertificate :136`), one `"<line>\n"` C string per PEM line, 16-space indent; all four byte-match their files today (extraction `diff` verified). `UtfMain.h:212` registers the root in every utest binary. `aws-`/`gcp-` fixtures are referenced by nothing in `src/`. Procedure is prose in `notes/certs_creation.txt` (Windows env vars, `-days 3600`, `dos2unix`).

**Constraints (verified with local OpenSSL 3.0.13):** keep both RSA-2048 keys and the existing CSR `test-server-req.pem`; re-signing it with `-extfile openssl.conf -extensions v3_req` reproduces CN, `DNS:*.*.mycompany.com, DNS:localhost` (first SAN must stay — `TlsPeerVerification_MultiLabelWildcardsDoNotMatch`, release note 5b), `CA:FALSE`, key usage; OpenSSL 3 adds SKI/AKI on the leaf (harmless). The 2017 root is v1 with no extensions; OpenSSL 3.0 `req -x509` with this conf still emits v1, 3.2+ would not — regenerate as **v3 with explicit `-addext`** (`basicConstraints=critical,CA:TRUE`, `keyUsage=critical,keyCertSign,cRLSign`, `subjectKeyIdentifier=hash`) for version independence; new pair passes `openssl verify -x509_strict -auth_level 2` (the 2017 pair fails strict). Validity **10 000 days** (matches the ip cert, → 2054). `-subj` to suppress prompts, `-set_serial 01`, Linux so no `dos2unix`.

**Deliverables:** two regenerated certs; two re-embedded literals (`getDevRootCA`, `getDefaultServerCertificate`; key literal unchanged); `certs/regenerate-test-certs.sh` (~35 lines, idempotent: creates keys/CSR only if missing, regenerates, verifies strict) with `notes/certs_creation.txt` reduced to rationale + "run the script"; aws/gcp (8 files): **delete** recommended (history keeps them; leaving them re-creates this finding in 2027), or one `readme.txt` line marking them historical. **Design intent:** the fail-closed default (B6) turned this from a warning into a hard cliff; the branch already chose long validity for the ip cert.

**Perf:** none. **Verify:** `openssl verify -x509_strict -auth_level 2`; extraction `diff` for both literals; rebuild+run `utf_baselib_http` (debug, release, clang debug), `utf_baselib_messaging`, `utf_baselib_rest`, `utf_baselib_io` (debug gcc); one smoke run of any other binary (v3 root accepted by `registerTrustedRoot`).

### M-7 — bare `BL_DEVENV_VERSION` gates (residual: the sites)

**Verified.** `NetUtils.h:198` `#if BL_DEVENV_VERSION >= 4`; undefined → `#else` → `endpoints -> host_name()` on `basic_resolver_results`, which on Boost 1.90 derives **privately** from the iterator (`basic_resolver_results.hpp:51-53`): hard compile error for any external consumer of this public header. `TestBoostAsioCompat.h:59` same gate (test header, low risk). Third bare site not in the review: `OpenSSLTypes.h:41` `#if defined( _WIN32 ) && BL_DEVENV_VERSION >= 4 && OPENSSL_VERSION_NUMBER >= 0x1010104fL` (public header, Windows only). All nine other sites use the defensive `!defined( X ) || …` form. `BL_DEVENV_VERSION` is defined only by `devenv-detect.mk:270-286` (devenv2 gets none).

| Configuration | `BL_DEVENV_VERSION >= 4` | `BOOST_VERSION >= 106600` | Same branch |
|---|---|---|---|
| devenv2 (undefined, Boost 1.58) / devenv3 (=3, 1.63) | false | false | yes |
| devenv4 (1.72) … devenv7 (1.90) | true | true | yes |
| external consumer, Boost ≥ 1.66, no `-D` | **false (wrong)** | true | fixed |
| `BOOSTDIR` override to < 1.66 on devenv4+ | true (wrong) | false | fixed |

**Fix:** `#if BOOST_VERSION >= 106600` at both sites (Boost 1.66 introduced `results_type`, as the existing comment says); `NetUtils.h` includes `OS.h` first, which supplies `BOOST_VERSION` via `OSBoostImports.h` — confirm with a probe and add the Bundle C `#error` tripwire. `OpenSSLTypes.h:41`: drop the devenv conjunct (the OpenSSL-version conjunct carries the meaning). **Perf:** none. **Verify:** `utf_baselib` debug gcc + clang (hosts `getCanonicalHostName` tests and `TestBoostAsioCompat`); the table above stands in for a multi-devenv rebuild (no other dist is local).

### M-8 — UUID alignment gate (residual: version, tripwire, pin)

**Verified.** `UuidBoostImports.h:28-34` says "Boost 1.89+" and gates `>= 108900`; the local Boost 1.90 changelog (`libs/uuid/doc/uuid/changes.adoc:16,50`) records the alignment change under **1.86.0**. `uuid.hpp:57-75` implements it as a `std::uint64_t` union member under `#if !defined(BOOST_UUID_DISABLE_ALIGNMENT)`; include guard `BOOST_UUID_UUID_HPP_INCLUDED`. In-repo include order is safe (only the unused `boost/uuid.hpp` pulls `uuid.hpp` from outside `boost/uuid/`; `seed_rng.hpp` only < 1.72). R-17's `static_assert`s (`TcpBlockTransferCommon.h:288-289`) fire only in TUs instantiating `CommandBlock`; a TU that reached `uuid.hpp` first elsewhere silently gets `alignof == 8` — an ODR split with no diagnostic. `alignof` is used nowhere in `src/include` (it arrived in MSVC only with VS2015).

**Decision point on the `#error`.** The header-hygiene deferral (`public-header-hygiene-deferral.md:120-126`) considered an equivalent `#error` for `BOOST_ASIO_DISABLE_STD_CHRONO` and deliberately did not take it, because it "would break consumers whose builds currently appear to work". The same record then says the UUID case "should be fixed on its own merits; it is not covered by this deferral" (`:130-132`) — because here the consequence is wire layout, not a chrono type. The recommendation is to take the `#error` for UUID: a consumer build it fails is one that is already emitting 80-byte frames against 72-byte peers.

**Fix:** (1) `#if defined( BOOST_UUID_UUID_HPP_INCLUDED ) && !defined( BOOST_UUID_DISABLE_ALIGNMENT )` → `#error` naming the rule; (2) define `BOOST_UUID_DISABLE_ALIGNMENT` **unconditionally** (ignored below 1.86; removes the `BOOST_VERSION` dependency at this point; F-18's `CPPFLAGS` rejection is respected — the define stays in the header); (3) comment says 1.86; (4) after `typedef uuids::uuid uuid_t;` (`:90`) pin alignment with a portable probe, `static_assert( 17 == sizeof( struct { char c; uuid_t u; } ), … )`-style, since `alignof` is unavailable on the oldest MSVC. **Perf:** none. **Verify:** `utf_baselib`, `utf_baselib_messaging` debug gcc + clang; scratch TU including `<boost/uuid/uuid.hpp>` first to confirm the `#error` fires (not committed).

### M-10 — `os.walk` silent drop (residual: full)

**Verified.** `s3_manage.py:846` (upload), `:1043` (verify), `bl_tool.py:214` (`collect_tree`) use default `onerror=None`. `bl_tool.py:223-224` handles the same hazard for symlinked directories and states the rationale; `bl_tool.md:279, 302-307` and the `blhash/v2` "unambiguous commitment" comment (`bl_tool.py:40-43`) promise fail-fast; `s3_manage.md:31-39` defines exit `1` = "at least one file … was missing". In both s3 commands the walk completes before any S3 call. Worst current case: unreadable root → "Found 0 files", exit 0. Test suite: pytest + moto, `temp_dir` fixture (`conftest.py:35-39`), `monkeypatch.setattr` idiom (`test_bl_tool_unit.py:593`, `test_s3_manage_unit.py:1209`), no permission test, no `geteuid` skip; coverage gate 80 %.

**Behaviour:** `bl_tool`: fail fast in the `fail_on_link` shape (`[ERROR] Cannot read directory: <path> (<strerror>)`, `sys.exit(1)`; `SystemExit` from `onerror` propagates out of the generator). `s3_manage`: **record and continue, then force `EXIT_FAILURE`** — matches the per-item aggregation pattern of both commands and the exit contract, and lets a rerun after `chmod` pick up only what was missed. One module-level helper returning `(onerror, errors)`; summary gains `Directories not scanned: N`; return conditions gain `or scan_errors`; `s3_manage.md` row for `1` gains "or a directory could not be scanned".

**Tests:** primary = monkeypatch **`os.scandir`** (looked up on `os` at call time by `os.walk`; verified on 3.12 that a patched `scandir` raising `PermissionError` reaches `onerror` with `.filename`) — deterministic, root-proof, counts toward coverage; secondary = one real `chmod 0o000` test for `bl_tool hash`, skipped on Windows and as root, `finally: chmod(0o700)`. Cases: upload unreadable subdir (asserts `EXIT_FAILURE`, `[ERROR]` line, summary count, and that the sibling good file was still listed; `dry_run=True, force=True` so a sentinel client suffices), upload unreadable root, verify unreadable-only tree, bl_tool monkeypatch, bl_tool real permissions. Out of scope: unguarded `os.path.getsize` at `s3_manage.py:866` (different failure class; still exits non-zero).

**Perf:** none. **Verify:** `.venv/bin/pytest` from `scripts/tests` (`make pytest`); confirm new cases run (uid 1000) and the coverage gate passes.

### M-11 … M-17 — devenv7 deployment scripts (residual: deferral record update only)

**Decision (user):** no deployment-script change in this plan. Verification would need a provisioning run per OS × architecture (no Windows, macOS or CI host here), which is the same cost argument the F-02 record already makes (`supply-chain-verification-deferral.md:110-119`). The residual is to record the newly found defects where the next person working in these scripts will meet them, following the record's own precedent for the `download-sources.ps1` typo (`:184-218`: Status / why not fixed now / why recorded here / scope when picked up).

**Home:** a new section "Deferred defects in the devenv7 deployment scripts" appended to `scripts/devenv7/docs/supply-chain-verification-deferral.md` (same devenv8 trigger, same audience; the record already hosts one non-integrity defect under that pattern). Alternative if the user prefers a separate file: `scripts/devenv7/docs/deployment-script-defects-deferral.md` with a one-line pointer from the F-02 record and from the devenv7 `AGENTS.md` "Tool Versions and Compatibility" section, where the F-02 pointer already lives.

**Entries to add** (each: location, symptom, rule violated, one-line fix, how it will be validated when picked up). Facts verified against the current tree:

| # | Finding | Location | Symptom | Rule / precedent | One-line fix when picked up |
|---|---|---|---|---|---|
| 1 | M-11 residual | macOS `build-boost-macos.sh:117`, `build-openssl-macos.sh:328`, `install-openjdk-macos.sh:104`, `install-gradle-macos.sh:99`, `install-json-spirit-macos.sh:97` | `curl -L` without `--fail` exits 0 on HTTP 4xx/5xx; the error page is cached under the archive name forever by `[ ! -f ]`. Not the truncation case the record's `:100-104` paragraph describes | `set -e` cannot catch it | `curl -fL`; fold into scope step 1 (atomic download). Linux `wget` already exits non-zero |
| 2 | M-12 (1) | `windows/build-openssl-windows.bat:463-468` | `^` continuation inside a quoted `-Command "…"` string; and `%PS_SCRIPT_DIR%` set at `:463` and read inside the same `if (` block → empty at parse time | root `AGENTS.md` "Line continuation" and "Delayed expansion" rules; devenv7 `AGENTS.md:37` names this file | single-line `-Command`, `!PS_SCRIPT_DIR!` |
| 3 | M-12 (2) | `windows/build-boost-windows.bat:296-298` | `%SCRIPT_DIR%` set and read inside the same `if not exist (` block → `Import-Module 'internal\common.ps1'` relative to cwd | same delayed-expansion rule | `!SCRIPT_DIR!` |
| 4 | M-12 (3) | `windows/internal/download-sources.ps1:151, 267` | `Copy-DirectoryWithProgress -SourcePath … -DestinationPath …`; function (`common.ps1:212-221`) declares `-Source`/`-Destination`; six other callers are correct | same family as the recorded `-OutputPath` typo, same two functions | rename parameters |
| 5 | M-12 (4) | `windows/build-openssl-windows.bat:465-468` | imports only `download-sources.ps1`, never `common.ps1`, so `Copy-DirectoryWithProgress` is unresolved even after #4 (`build-boost-windows.bat:298` imports both) | — | import `common.ps1` first |
| 6 | M-13 | `windows/build-openssl-windows.bat:673-675` | `wmic` absent on Windows 11 24H2+ → `PARALLEL_JOBS` empty → `jom -j` at `:686` fails; the ARM64 Windows 11 hosts that are the primary target are exactly the affected machines | `NUMBER_OF_PROCESSORS` unused anywhere in `scripts/devenv7` | `set /a "PARALLEL_JOBS=%NUMBER_OF_PROCESSORS% * 4"` (and `HARNESS_JOBS`) |
| 7 | M-14 | `docker/ubuntu/Dockerfile:2, 40`; `docker/rhel/build-notes.txt:12, 18` | `ubuntu:latest` no longer 24.04: `build-*-linux.sh` derive `OS_TAG` from `VERSION_ID` (`build-gcc-linux.sh:82-84` and four siblings) → `dist-devenv7-ub26-…`, which `platform.mk:278-293` rejects; `unminimize` also gone after 24.04; `ubi9/ubi:latest`, `ubi10/ubi:latest` float likewise (and `platform.mk` maps only `rhel5..rhel8`) | reproducible builds | `ubuntu:24.04@sha256:<digest>`; pinned `ubi` tags |
| 8 | M-15 | `macos/build-boost-macos.sh:271` | `architecture=arm` unconditional while `:49-54` sets `ARCH_FLAGS` per `uname -m`; with `--layout=tagged` an Intel Mac produces `-a64` libraries that `boost/common.mk:38` (`-x64`) cannot link — fails loudly at consumer link time | Windows script derives `BOOST_ARCHITECTURE` per arch (`build-boost-windows.bat:144-156`) | derive from `ARCH_TAG` |
| 9 | M-16 | `windows/build-openssl-windows.bat:743-749, 760, 827, 850` | test failure sets `TESTS_FAILED=1`, prints a warning, then installs, verifies, copies to dist and archives with exit 0; Linux/macOS abort under `set -e` (`build-openssl-linux.sh:554`, `-macos.sh:417`). No record says warn-only is intended; whether the Windows suite currently passes is unknown from this host, so the fix is a policy call for the provisioning run | platform parity | `exit /b 1` on failure, keep `-skip-tests` and the cross-execution auto-skip |
| 10 | M-17 (a) | `windows/internal/common.ps1:548` | `Start-Process -ArgumentList @("-xf", $ArchivePath, "-C", $DestinationPath)` elements unquoted; a `%USERPROFILE%` with a space breaks every non-`.7z.exe` extraction. `download-tools.ps1:888` uses the opposite convention (pre-quoted elements, which PS 5.1 can double) — the two cannot both be right | — | settle one quoting convention on a Windows host |
| 11 | M-17 (b) | `windows/build-openssl-windows.bat:596-597, 620-621` | `--prefix=%OPENSSL_ROOT_PATH%\out` / `--openssldir=` unquoted; default `DIST_ROOT` is under `%USERPROFILE%`; failure lands in `log_bootstrap.log` | Unix scripts quote (`build-openssl-linux.sh:542-544`) | quote both |

Also update the record's line-number table only if any of the above shifts it (none do — no script is edited). **Verify:** read-through. **Cost:** 0.75 h, V-0.

### Item 8 — release notes (residual: three sections)

**Verified gap.** `devenv7-breaking-changes-release-notes.md` (7 sections) never mentions `allowUntrustedCertificates` or the trust store. Facts: default `false` (`CryptoBase.h:595`); platform store never loaded on any OS (`:400-406`; no `set_default_verify_paths` in `src/`); default anchors are three roots (`TrustedRoots.h:61-165`: two retired VeriSign roots, Entrust G2), four more behind opt-in `initAdditionalCommonTrustedRoots()` (one already expired May 2022); escape hatch is process-global; `CryptoBase.h:736-740` already states "register your root explicitly". Section 4 covers hash instability; I-3 (insertion key order on Boost.JSON) and L-7 (double text `1.5E0` vs `setprecision(17)`) are unwritten wire-format facts.

**Write:** §8 fail-closed trust default + bundled-root limitation + escape hatch guidance; §9 key order and double text differ by backend, no consumer may byte-compare; §10 external-consumer header expectations (`NetUtils.h` no longer needs `BL_DEVENV_VERSION` after M-7; `UuidBoostImports.h` must precede any `boost/uuid` include or the build fails with an explanatory `#error` after M-8). Same "presents as / who is affected / pointer" shape. §10 last, after the M-7/M-8 decisions.

---

## 4. Grouping by verification cost, and slices

| Bucket | Proof | Items | Edit | Modules | Units |
|---|---|---|---|---|---|
| **V-0 review only** | Markdown | Item 8 · devenv7 deferral record update (M-11 … M-17) | 1.5 h | — | 0 |
| **V-1 focused compile-proof** | Build includers; no behaviour to test | M-2 · M-1 comments · M-7 (+ equivalence table) · M-8 (+ negative probe) | 1.5 h | `utf_baselib_tasks`, `utf_baselib_blobtransfer`, `utf_baselib`, `utf_baselib_messaging` | ~10 |
| **V-2 single-module test** | One module, both variants, both toolchains | M-1 guard + test · M-4 | 2.5 h | `utf_baselib_tasks`, `utf_baselib_http` | 8 |
| **V-3 dual-backend** | V-2 plus json-spirit release run on a clean tree | M-3 | 0.75 h | `utf_baselib_data` | 4 |
| **V-5 fixture regeneration** | Regenerate, prove embedding, run every TLS-bearing module | M-6 | 1.5 h | `utf_baselib_http`, `_messaging`, `_rest`, `_io` | 6 |
| **V-7 Python** | `.venv/bin/pytest` | M-10 | 1.5 h | — | 0 |

| Slice (commit prefix) | Items | Why together | Shared run |
|---|---|---|---|
| **S-1 ExecutionQueue contract** | M-2 + M-1 comments (doc commit); M-1 guard + test (logic commit) | same header and module; doc/logic split per the mixing rule | one `utf_baselib_tasks` matrix |
| **S-2 Boost header gates** | M-7 (+ `OpenSSLTypes.h`), M-8 | same failure mode ("wrong preprocessor branch, silently"), same precedent, same compile-proof campaign | `utf_baselib` + `utf_baselib_messaging` |
| **S-3 json-spirit int64** | M-3 | only dual-backend item | `utf_baselib_data` × 2 backends |
| **S-4 TLS fixtures and gate** | M-6, M-4 | both in `utf_baselib_http`; M-4's test uses the certs | `utf_baselib_http` matrix + three TLS modules once |
| **S-5 Python tree walking** | M-10 | standalone | pytest |
| **S-6 records** | devenv7 deferral entries (M-11 … M-17); release notes §8-10 | Markdown; §10 after S-2 | none |

Dependencies: S-6 §10 after S-2 decided; S-4's M-4 test after M-6's certs. Everything else independent. Order: S-1, S-2, S-3 (one afternoon, all header-only) → S-5 → S-4 → S-6.

---

## 5. Decisions requested (recommendation is the default if approved without comment)

1. **M-6 fixtures:** delete the unreferenced aws/gcp files (recommended) or mark them historical? Regenerate now (recommended) or defer?
2. **M-8 `#error` tripwire:** take it for UUID (recommended, wire-layout consequence; the header-hygiene record explicitly leaves this case open) or comment + pin only, consistent with the chrono deferral's caution?
3. **Deferral home for M-11 … M-17:** append to `supply-chain-verification-deferral.md` (recommended: it already hosts a non-integrity script defect under the "Deferred defect" pattern, and its devenv8 trigger is the earliest one), or `notes/plans/issues/privileged-script-error-handling-deferral.md` (trigger: "next refresh of the deployment scripts and their design"), or a new `scripts/devenv7/docs/deployment-script-defects-deferral.md`? Whichever is chosen gets a one-line pointer from the other two so the entries cannot resurface unseen.

Minor, already defaulted: M-1 always-on guard + cross-queue test; M-4 test included; M-7 third site included.

---

## 6. Verification (end to end, after approved slices land)

| Slice | Command |
|---|---|
| S-1 | `make -k -j1 utf_baselib_tasks` debug, release, `TOOLCHAIN=clang` debug; `make test_utf_baselib_tasks` each; `make -k -j1 utf_baselib_blobtransfer` debug compile-proof |
| S-2 | `make -k -j1 utf_baselib` and `utf_baselib_messaging` debug gcc + clang; run both; scratch negative probe for the `#error`; M-7 equivalence table recorded in the status section |
| S-3 | `make -k -j1 utf_baselib_data` debug gcc, debug clang; `make -k -j1 utf_baselib_data VARIANT=release BL_USE_JSON_SPIRIT=1` on a clean tree; run all three; cite the json-spirit command in the commit message |
| S-4 | `openssl verify -x509_strict -auth_level 2 -CAfile certs/test-root-ca.pem certs/test-server-cert.pem`; extraction `diff` for both literals; `make -k -j1 utf_baselib_http` debug, release, clang debug; `utf_baselib_messaging`, `utf_baselib_rest`, `utf_baselib_io` debug gcc; run all |
| S-5 | `.venv/bin/pytest -q` from `scripts/tests` (or `make pytest`); new cases not skipped; coverage gate |
| S-6 | Read-through; `git diff` confirms no file under `scripts/devenv7/{linux,macos,windows,docker}` changed |

All builds `-j1`; up to 5 test modules concurrently.

---

## 7. Corrections to the review, for the record

- M-2: five dependent comments (`FilesUnpackagerUnit.h:1058-1066` missed), nine continuation producers.
- M-6: PEM is embedded in `UtfCrypto.h`; nothing reads the `.pem` files; two of four certs are dead.
- M-7: a third bare site, `OpenSSLTypes.h:41`.
- M-8: the "1.89+" comment is off by three releases (1.86.0).
- M-11: the atomic-download half is already a recorded deferral; only the `--fail` half is new.
- M-12: a fourth defect on the same path (`common.ps1` never imported by the OpenSSL batch; `PS_SCRIPT_DIR` parse-time expansion).
- M-16: no record shows warn-only was intended, but the Windows suite's current state is unknown from this host.
- Item 7 as a whole: the review's "can follow in a scripts-only commit" understates the cost; every one of those edits is unverifiable without a provisioning run per OS, which is why they are recorded rather than made.

---

# Implementation status (2026-09-04)

Implemented in the working tree in one pass, uncommitted, every recommendation in §2 taken as
approved: M-1 always-on guard with the cross-queue test; M-4 with the callback-form test; M-6
regenerated now, aws/gcp fixtures deleted, regeneration script added; M-7 including
`OpenSSLTypes.h:41`; M-8 with the `#error` tripwire; M-11 … M-17 recorded in the supply-chain
deferral record (with pointers from the privileged-script record); release notes §8-10 written.

## Delivered

| Item | Files |
|---|---|
| M-1 | `tasks/ExecutionQueueImpl.h` (`<thread>`, `m_notifyOwner`, guard + corrected comment), `tasks/ExecutionQueueNotify.h` (contract), `TestTasks.h` (`Tasks_ExecutionQueueNotificationSerializedCrossQueueReentryTest`), one-paragraph premise correction in `execution-queue-notification-delivery-plan.md` |
| M-2 | `tasks/ExecutionQueue.h` doc; the five dependent comments (`FixedWorkerPoolUnitBase.h`, `ObservableBase.h`, `FilesPkgUnpkgBase.h`, `FilesPackagerUnit.h`, `FilesUnpackagerUnit.h`) |
| M-3 | `core/detail/JsonSpiritImpl.h`: the int64 check lives in `value_wrapper::as_int64()` (both `value_to< std::int64_t >` and `get_int64` route through it; a `detail::checkedInt64` helper was not possible because the wrapper is defined before namespace `detail`); parity comment extended; `TestJsonAbstraction.h` `JsonNumericOutOfRangeIntIsRejected` extended with the `uint64 > INT64_MAX` case and the in-range controls |
| M-4 | `crypto/TlsPeerVerification.h` (`preVerified` first parameter, fail closed, doc), `tasks/AsioSslStreamWrapper.h` (caller), `TestTlsPeerVerification.h` (`TlsPeerVerification_CallbackFormFailsClosedWithoutPreverification`, on OpenSSL >= 1.1.0) |
| M-6 | `certs/test-root-ca.pem` (v3, CA:TRUE, keyCertSign/cRLSign, SKI), `certs/test-server-cert.pem` (same CSR, same SANs, SKI/AKI added by OpenSSL 3), both to 2054-01-20; keys and CSR unchanged; `UtfCrypto.h` `getDevRootCA` / `getDefaultServerCertificate` re-embedded (extraction `diff` empty for all four literals); new `certs/regenerate-test-certs.sh`; `notes/certs_creation.txt` rewritten; `certs/readme.txt`; the eight unreferenced `aws-*` / `gcp-*` files deleted |
| M-7 | `core/NetUtils.h` (`BOOST_VERSION >= 106600` + `#error` tripwire + rationale), `TestBoostAsioCompat.h`, `crypto/OpenSSLTypes.h` (devenv conjunct dropped) |
| M-8 | `core/detail/UuidBoostImports.h`: unconditional define, 1.86 comment, `#error` on prior `uuid.hpp` inclusion, `sizeof`-probe `static_assert` on alignment 1 |
| M-10 | `scripts/bl_tool.py` (`fail_on_walk_error`), `scripts/s3_manage.py` (`make_walk_error_handler`, both commands record + continue + `EXIT_FAILURE`, `Directories not scanned:` summary line), `scripts/s3_manage.md` exit-code row, five tests in `test_s3_manage_unit.py` / `test_bl_tool_unit.py` (scandir monkeypatch primary, one real `chmod 000` case skipped as root / on Windows) |
| M-11 … M-17 | new section "Deferred defects in the devenv7 deployment scripts" in `scripts/devenv7/docs/supply-chain-verification-deferral.md` (11 entries, scope when picked up); pointer paragraph in `privileged-script-error-handling-deferral.md` |
| Item 8 | `devenv7-breaking-changes-release-notes.md` §8 (fail-closed trust), §9 (key order / double text), §10 (`BL_DEVENV_VERSION` no longer needed; UUID include-order `#error`) |

## Deviations from the plan

- **M-6 verification flag:** the script verifies with `openssl verify -auth_level 2`, not
  `-x509_strict`. Strict mode demands an authority key identifier on the leaf, which the reused
  CSR path (`x509 -req -extfile openssl.conf -extensions v3_req`) does not emit — the 2017 pair
  failed strict mode too. Level 2 is what the library actually enforces
  (`SSL_CTX_set_security_level( 2 )`, no `X509_V_FLAG_X509_STRICT`); the script header says so.
- **M-3 shape:** check placed in `value_wrapper::as_int64()` rather than a `detail::checkedInt64`
  helper (declaration order); behaviour identical to the plan.
- **Disk:** the host root filesystem hit 100 % during the gcc release builds (`No space left on
  device`); the regenerable separate debug-info files (`*.dbg`, 1.1 GB) produced by today's builds
  were deleted to finish the matrix. Nothing outside `bld/` was touched.

## Verification performed

Every build `-j1`; every test module run through `make test_<module>`; all runs "No errors
detected". The default toolchain on this host resolves to `clang2010`; gcc is `TOOLCHAIN=gcc1520`.

| Configuration | Modules built | Modules run (cases) |
|---|---|---|
| clang2010 debug | tasks, data, http, baselib, messaging, rest, io, blobtransfer | tasks (51), data (80), http (23), baselib (146), messaging (27), rest (5), io (20), blobtransfer (9) |
| gcc1520 debug | same eight | tasks (51), data (80), http (23) |
| clang2010 release | tasks, data, http | tasks, data, http |
| gcc1520 release | tasks, data, http | tasks, data, http |
| clang2010 release, `BL_USE_JSON_SPIRIT=1` | data | data (76 = 80 minus the four `#if !defined( BL_USE_JSON_SPIRIT )` cases), objects removed afterwards per the decision record |
| Python | — | `pytest --cov`: 577 passed, 2 skipped (baseline 572/2 + 5 new), coverage 87.46 % |

New cases confirmed present and passing by name in the logs: the cross-queue re-entry test
(tasks), the callback-form gate test (http), the widened `JsonNumericOutOfRangeIntIsRejected`
(data, both backends), the five `os.walk` tests (Python, none skipped at uid 1000).

**Probes (scratch translation units, not committed):**

- M-8 negative: a TU including `<boost/uuid/uuid.hpp>` before `<baselib/core/BaseIncludes.h>`
  fails with the new `#error` *and* the alignment `static_assert`; the reverse order compiles and a
  local `sizeof` probe confirms alignment 1.
- M-1 negative: a scratch test binary whose serialized hook completes a sibling task of the
  *same* queue aborts with
  `ERROR: RIP: ... ExecutionQueueNotify::onEvent() under DeliverySerialized re-entered the same execution queue on the same thread ...`
  at `ExecutionQueueImpl.h:395` — where the previous code hung.
- M-6: `openssl verify -auth_level 2` OK; certificate/key modulus match for both pairs; SAN order
  `DNS:*.*.mycompany.com, DNS:localhost` preserved (`TlsPeerVerification_MultiLabelWildcardsDoNotMatch` still passes).

**Not run:** OpenSSL 1.1.1w (`BL_USE_OPENSSL_1X=1`) — no item in this round touches an OpenSSL
version gate except the M-4 test, which is guarded `>= 0x10100000L` and so compiles on 1.1.1w
identically; the H-1..H-3 matrix already covered that configuration for the TLS code. Windows and
macOS: not available.
