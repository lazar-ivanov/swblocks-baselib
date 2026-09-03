# Residual findings from `pr_review_analysis_gpt56sol_deep_dive.md`

## Context

The deep-dive review of `master...lazari2` raised 14 findings (F-07 plus CXX-01…CXX-13) and
called the branch un-mergeable. Since that review, 193 commits landed on `lazari2`, several of
them explicitly targeting review findings (`e60b3b3`, `fb85810`, `055eb55`, `4f58080`, `da98bd2`,
`eae1d03`, `d60a046`, `22ffa9b`, `5ef3aa8`, `3347670`). Two findings were also formally deferred
with a written risk acceptance (`notes/plans/issues/medium-severity-findings-f11-f17-plan.md`,
`notes/plans/issues/public-header-hygiene-deferral.md`).

This document records which findings are **still live** after verifying each one against the
current working tree, and what it would cost to close them. It is an assessment, not an
instruction to implement — no code change is proposed here without a separate decision.

Note the two reviews use different ID spaces. The shallow review
(`pr_review_analysis_gpt56sol.md`) uses `F-01…F-18`; the deep dive uses `F-07` plus `CXX-01…13`.
Only `F-07` is shared. Where a deferral record exists it is cited under its `F-` id.

---

## Disposition summary

| ID | Severity (as filed) | Status now | Evidence |
|---|---|---|---|
| F-07 | High | **Closed** — contract change retained, made explicit and opt-outable | `execution-queue-notification-delivery-breaking-change.md`, `ExecutionQueueNotify.h` (`NotifyDelivery`) |
| CXX-01 | High | **Closed** — contract documented, backend-defined by decision | `json-duplicate-key-contract.md`, `JsonUtils.h` (`readFromString`) |
| CXX-02 | High | **Closed** — deferral recorded in code; nested-canonicalize bug fixed | `DataModelObject.h` (`getObjectHash`), `BoostJsonImpl.h` (`canonicalizeValue`) |
| CXX-03 | High | **Closed** | `JsonSecuritySerializationImpl.h:444-490` |
| CXX-04 | High | **Closed** | `BoostAsioCompat.h:252-266,300-320` |
| CXX-05 | High | **Closed** | `UuidBoostImports.h` (`d60a046`) |
| CXX-06 | High | **Closed** — TLS floor, cipher policy, key/cert checks, explicit key protection, fail-closed certificate trust | `CryptoBase.h` (`TlsMinimumVersion`), `TestTlsProtocolPolicy.h` |
| CXX-07 | Medium | **Closed** — no user code runs under `m_lock` any more | `execution-queue-notification-delivery-plan.md` (CXX-07 section) |
| CXX-08 | Medium | **Closed** — `rawUtf8` documented and backends aligned; numeric policy unified | `JsonSpiritImpl.h` (`checkedInt` / `checkedUInt64`), `TestJsonAbstraction.h` |
| CXX-09 | Medium | **Closed** — decay fixed; shim documented as internal | `BoostAsioCompat.h`, `residual-cxx-findings-deferral.md` item 1 |
| CXX-10 | Medium | **Closed** — throw fixed previously; skip behaviour documented, policy deferred | `FsUtils.h` (`copyDirectoryWithContents`), `residual-cxx-findings-deferral.md` item 2 |
| CXX-11 | Medium | **Closed** — documented in code, break accepted | `Platform.h:75`, `Manifest.h:91`, `residual-cxx-findings-deferral.md` item 3 |
| CXX-12 | Medium | **Closed** — specialization removed, not deferred | `BignumBase64Url.h`, `RsaSignVerify.h`; `CPP.h` specialization deleted |
| CXX-13 | Medium | **Closed** | no Boost.Locale reference remains in `TestBaselibDefault.h` |

**All fourteen are now closed**, either by a code change or by a written risk acceptance. The
remainder of this document is the assessment as it stood before that work and is retained as the
record of what was found; the sections below are **not** updated in place, so where they disagree
with the table above the table is current.

What was actually done, and where it is recorded:

| Workstream | Findings | Record |
|---|---|---|
| Crypto and TLS security defaults | CXX-06 | `pr-review-residual-cxx-findings-plan.md`, `evp-provider-migration-deferral.md` |
| JSON compatibility surface | CXX-01, CXX-02, CXX-08 | `json-duplicate-key-contract.md`, `medium-severity-findings-f11-f17-plan.md` (F-11) |
| Standalone fixes | CXX-09 (decay), CXX-12 | `residual-cxx-findings-deferral.md` |
| Documentation-only closes | CXX-09 (residual), CXX-10, CXX-11 | `residual-cxx-findings-deferral.md` |

Two estimates in the per-finding sections below turned out to be wrong and are corrected here rather
than in place:

- **CXX-12 was costed at ~3-4 days** on the assumption that removing the `std::char_traits`
  specialization meant touching every `std::basic_string< unsigned char >` use site. There were
  exactly **two**, both local typedefs feeding a base64url decode, and
  `SerializationUtils::base64UrlDecodeVector` already existed. It was done in well under a day and
  removed 100 lines.
- **CXX-10 is described as silently skipping symlinks.** It does not. `fs::is_regular_file` and
  `fs::is_directory` follow links, so a link to a regular file **is** copied, dereferenced; what is
  actually dropped is special files, dangling links, and the contents of linked directories. The gap
  is real but narrower than filed — see `residual-cxx-findings-deferral.md` item 2.

---

## Group 1 — High severity, still live

### CXX-01 — Boost.JSON accepts duplicate object keys; json-spirit rejects them

**Still valid, unmodified.** `BoostJsonImpl.h:183` calls `boost::json::parse( input )` with default
options and no member-level duplicate check; the stream path at `:216` uses a bare
`boost::json::stream_parser`. `JsonSpiritImpl.h:349` still raises `"Duplicate entry encountered for
property with name ..."`. The parser differential the review described is intact: the same document
is rejected on one build and silently last-value-wins on the other.

The related DoS sub-finding (no shared limits on input size, depth, member count, string length) is
also untouched — no `parse_options` is constructed anywhere.

- **Complexity: high.** Cannot be done post-parse — the Boost DOM has already dropped the earlier
  member. Needs a SAX/`basic_parser` handler or a custom `on_key`/`on_object_end` layer that
  rejects a repeat within the same object, applied to both the one-shot and streaming entry points,
  plus a shared limits policy object threaded through both backends.
- **Risk: medium.** Rejecting duplicates is a behavior tightening for anything that currently
  parses lenient input. Needs a named permissive escape hatch. Getting the limits wrong breaks
  legitimate large payloads.
- **Cost: ~4–6 engineer-days** including a shared accept/reject corpus run against both backends.

### CXX-02 — JSON ordering / hashing / wire compatibility

**Still valid and explicitly deferred** as F-11 in
`notes/plans/issues/medium-severity-findings-f11-f17-plan.md` ("Deferred — canonical format +
custom serializer + golden vectors, High effort"). Verified live:

- `getObjectHash( ..., canonicalize = false )` still defaults to non-canonical
  (`DataModelObject.h:282-290`), so hashes follow Boost.JSON insertion order.
- The nested-collection canonicalization gap is real. The single complex-property path forwards the
  flag, but the vector and map paths call `item -> serializeProperties( tempContext )` and
  `pair.second -> serializeProperties( tempContext )` with no `canonicalize` argument
  (`DataModelObjectDefs.h:629` and `:702`), falling back to the `= false` default at `:824`.
- `canonicalizeValue` (`BoostJsonImpl.h:281`) sorts keys but is not RFC 8785/JCS and is not named
  as project-specific.

- **Complexity: high**, and it is the one finding with a genuine data-migration question attached
  (any persisted hash, cache key, or signature input derived from the old map-ordered serialization).
- **Risk: high** if hashes are load-bearing anywhere persisted; **low** if they are only
  in-process. That determination is the gating question, not the code.
- **Cost: the nested-canonicalize propagation alone is ~0.5 day** (two macro edits plus tests) and
  is worth doing independently of the rest. The full canonical-format decision, golden vectors, and
  migration is **~8–15 days**, dominated by deciding and executing the migration, not by coding.

### CXX-06 — TLS and key-material defaults

**Half closed.** The private-key half was fixed: `JsonSecuritySerializationImpl.h:213` now uses
`EVP_aes_256_cbc()` and PKCS#8, so the 3DES-at-rest criticism no longer applies. The TLS half is
untouched:

- `CryptoBase.h:209` sets `SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_ALL | SSL_OP_NO_TICKET` and
  only adds `SSL_OP_NO_TLSv1` when `g_isEnableTlsV10` is false. **TLS 1.1 remains enabled by
  default.** No `SSL_CTX_set_min_proto_version` call exists anywhere in the tree.
- The cipher list at `:232` still contains `ECDH+3DES:DH+3DES:RSA+3DES`.
- `SSL_CTX_check_private_key` and `EVP_PKEY_public_check` appear nowhere in `src/include/`.
- Export with an empty password still writes plaintext key material (`:185`, `:213`).

The related EVP migration is also unchanged: `RsaKey.h:79` still does `RSA_new()`,
`RsaSignVerify.h:66,123` still call `RSA_sign`/`RSA_verify`, and the JWK path still uses
`RSA_get0_*`/`RSA_set0_*`. Builds with `OPENSSL_NO_DEPRECATED` will still fail.

- **Complexity: low for the policy floor, high for the EVP migration.** Setting a TLS 1.2 minimum
  and modernizing the cipher string is a ~20-line change in one file. Moving the stored key type
  from `RSA*` to `EVP_PKEY` touches `RsaKey.h`, `RsaSignVerify.h`, `OpenSSLTypes.h`, and the whole
  JWK serialization path.
- **Risk: low-to-medium for the floor** — it is a compatibility break for any peer stuck on TLS
  1.1, which is exactly the point; mitigate with a named legacy opt-in mirroring the existing
  `g_isEnableTlsV10` pattern. **High for the EVP migration** (signature bytes must stay
  PKCS#1 v1.5 for `RS512` interop — do not let it drift to PSS).
- **Cost: ~1–2 days** for the TLS floor + cipher policy + `SSL_CTX_check_private_key` + policy
  tests. **~10–15 days** for the full EVP/provider migration and JWK validation. These are
  separable and should be separate commits.

---

## Group 2 — Medium severity, still live

### CXX-08 — JSON public API compatibility

Partly closed by `5ef3aa8`: `JsonUtils.h:118-122` now `= delete`s the `unsigned int` overload of
`saveToStream`, so an old `OutputOptions` call site fails to compile instead of silently switching
to pretty-print. That was the sharp edge and it is gone.

Still open: `rawUtf8` is accepted and ignored on the Boost backend (`BoostJsonImpl.h:455`,
`BL_UNUSED( rawUtf8 )`), so `raw=true` and `raw=false` produce identical bytes there; and the
numeric-conversion policy at the boundaries (negative → unsigned, out-of-range, `-0`) is still
backend-dependent.

- **Complexity: low for `rawUtf8`** — either implement it or remove the parameter and let the call
  sites fail. **Medium for numeric policy** (project-owned checked conversions in both adapters).
- **Risk: low.** Removing `rawUtf8` is a compile-time break, which is the desired failure mode.
- **Cost: ~1 day** for `rawUtf8` (decide + exact-byte tests), **~2–3 days** for conversion
  normalization with boundary tests.

### CXX-09 — Asio handler and namespace hygiene

The protocol/flags half (CXX-04) is fixed. The handler half is not: `BoostAsioCompat.h:278-283`
still declares `template <typename Handler> struct resolve_handler_wrapper { Handler handler; }`
and instantiates it at `:303` as `resolve_handler_wrapper<ResolveHandler>` from a forwarding
reference. For an lvalue handler `ResolveHandler` deduces to `T&` and the member becomes a
reference — the exact lifetime hazard the review named. `std::decay_t` appears nowhere in the file.
Associated executor/allocator/cancellation are still not propagated, and `async_resolve` returns
`void` so completion tokens are unsupported.

`BOOST_ASIO_DISABLE_STD_CHRONO` (`OSBoostImports.h:29`) is a **recorded risk acceptance**, not an
oversight — see `public-header-hygiene-deferral.md` item 2.

- **Complexity: trivial for the decay fix** (one `std::decay_t`); **high** for a faithful
  completion-token shim.
- **Risk: very low for the decay fix.** All current call sites pass bound callables, so it is
  latent, but it costs nothing to close.
- **Cost: ~1 hour for the decay fix.** A real token-conforming adapter is ~5+ days and is only
  worth it if the shim is to be treated as public API — otherwise document it as internal.

### CXX-10 — filesystem copy

The throwing-`is_directory`-in-an-`error_code`-overload half is **fixed** (`22ffa9b`;
`OSImplPlatformCommon.h:2007-2016` now uses the `ec` overload and returns early). The symlink /
special-file policy half is untouched: recursive copy still silently skips them with no reporting
and no caller-selectable policy.

- **Complexity: medium.** Needs a policy enum plumbed through the copy helpers plus a
  skipped-entry report channel.
- **Risk: low** to add, but the current silent-skip behavior is the risk — incomplete
  deployments/backups with no signal.
- **Cost: ~2–3 days** including symlink/FIFO/permission tests, which do not exist today.

### CXX-11 — deleted move assignment on public immutable types

**Still valid.** `Platform.h:75` deletes `PlatformIdentityT& operator=(PlatformIdentityT&&)` and
`Manifest.h` gained the same for `ManifestT`, each with an explanatory comment. The comment
documents *why* the operation is deleted but does not address the external source-compatibility
break the review raised.

- **Complexity: trivial to document, medium to redesign.**
- **Risk: low.** No internal caller assigns these; the exposure is downstream consumers only.
- **Cost: ~1 hour** for a release-note entry. Redesigning the immutable value types so their
  supported operations are unambiguous is ~2–3 days and probably not worth it in this cycle.

### CXX-12 — `std::char_traits<unsigned char>` gated on compiler version

**Still valid.** `CPP.h:1368-1370` specializes `std::char_traits< unsigned char >` under
`#if defined(__clang__) && (__clang_major__ >= 20)`. Specializing a standard template for a
fundamental type remains non-portable, and the gate is on the compiler rather than on whether the
active standard library supplies it. The neighbouring `-Woverloaded-virtual` global suppression
(`Compiler.h:36`) is a **recorded risk acceptance** (F-18 item 1), not an open finding.

- **Complexity: medium.** Replacing it with project-owned traits means touching every
  `std::basic_string<unsigned char>` use site.
- **Risk: medium.** The current code works on the toolchains actually built; the failure mode is a
  future libc++/libstdc++ that also defines it, giving an ODR/redefinition break.
- **Cost: ~3–4 days**, or ~0 if consciously deferred alongside F-18 items 1–2, which is the
  consistent choice.

---

## Group 3 — Closed since the review

- **F-07** — `ExecutionQueueImpl.h:369-500` now captures the task callback under `m_lock`, releases
  the lock before invoking it, and then re-validates all-complete at delivery time against
  `m_activeWorkGeneration` / `m_lastPublishedGeneration` before capturing the *current* observer.
  That is exactly the review's §3 design. `TestTasks.h:970-1360` adds reentrant, obsolete-candidate,
  and observer-selection cases.
  **One residual, and it is a deliberate divergence:** the review asked for a serialized notification
  queue so callbacks never overlap. The branch went the other way and documents concurrent,
  potentially out-of-order callbacks as the contract (`ExecutionQueueNotify.h:82-91`). That is a
  defensible choice that avoids the dispatcher machinery, but it is a **public behavior change from
  `master`**, where `m_lockEvents` serialized callbacks.

  Two attributions in the original text of this entry were wrong and are corrected here:

  - The behavior change was made by **`188fe03`** ("fix potential deadlock issue", 2025-11-17), which
    narrowed `m_lockEvents` to exclude the callback invocations. `fb85810` only deleted the by-then
    vestigial member; it changed no behavior.
  - `4f58080` does **not** annotate nine observers. It is documentation-only (120 insertions, zero
    executable lines) and its annotations concern *queue-state accessor* snapshot semantics
    (`size()`, `hasReady()`, `top()`, `pop()`) across nine **files**, not observer thread safety.

  The exhaustiveness question is now answered: there are exactly **five** `ExecutionQueueNotify`
  implementations repo-wide, and since there is no `std::function`/lambda registration path the
  inventory is closed by construction. Four are no-ops or return constants; the one substantive
  observer, `TcpServerBase::onEvent` (`TcpBaseTasks.h:1743`), is correctly guarded — every
  `m_activeEndpoints` access is under `TaskBase::m_lock`. **No in-repo observer is unsafe**; the
  residual exposure is entirely external.

  Disposition: analysis in `execution-queue-notification-delivery-plan.md`, breaking-change record
  and migration in `execution-queue-notification-delivery-breaking-change.md`. Resolved by making the
  delivery policy a **mandatory** parameter of `setNotifyCallback`, so the silent runtime break
  becomes a compile-time one, with `DeliverySerialized` available as an opt-in that restores mutual
  exclusion without restoring the `188fe03` deadlock.
- **CXX-03** — `JsonSecuritySerializationImpl.h:444-490` tries `PEM_read_bio_PUBKEY` then falls back
  to `PEM_read_bio_RSAPublicKey`, with SPKI/PKCS#8 fixtures and `TestPemKeyFormats.h` (327 lines).
- **CXX-04** — `BoostAsioCompat.h` forwards `q.protocol()`, host, service, and `q.flags_value()` on
  both sync and async paths, with 446 lines of new tests.
- **CXX-05** — `d60a046` adds the `<boost/version.hpp>` include before the version test.
- **CXX-07** — `hasReady`/`hasExecuting`/`hasPending`/`size`/`setOptions`/`setThrottleLimit`/
  `setNotifyCallback` all take `BL_MUTEX_GUARD( m_lock )`; the thread-pool accessors are atomic.
  **Residual: now closed.** `getMaxReadyOrExecuting()` used to call the user-overridable
  `notifyCB -> maxReadyOrExecuting()` while `m_lock` was held, once per scheduled task. It was not
  the ~0.5 day "hoist" originally estimated — that shape is infeasible, because one caller of
  `padExecutingQueueNothrow()` sits inside a `condition_variable` wait predicate re-evaluated with
  `m_lock` held. Resolved instead by sampling the limit once in `setNotifyCallback()`, outside the
  lock, and caching it; `getMaxReadyOrExecuting()` is gone. See the CXX-07 section of
  `notes/plans/issues/execution-queue-notification-delivery-plan.md`.
- **CXX-13** — `TestBaselibDefault.h` has no Boost.Locale reference left; the converter tests stand
  alone.

---

## What I would actually do, in order

Ranked by value per day, not by the review's severity labels:

1. `std::decay_t` on the resolve handler — **1 hour**, zero risk, removes a latent lifetime bug.
2. Forward `canonicalize` through the nested vector/map macros — **0.5 day**, closes the one part
   of CXX-02 that needs no migration decision.
3. ~~Hoist `getMaxReadyOrExecuting()` out of `m_lock`~~ — **done**, though not as a hoist. The
   described shape is infeasible (one caller sits inside a `condition_variable` wait predicate
   re-evaluated with `m_lock` held). Resolved by sampling the limit once at registration instead;
   see `execution-queue-notification-delivery-plan.md`.
4. TLS 1.2 floor + modern cipher list + `SSL_CTX_check_private_key`, with a named legacy opt-in —
   **1–2 days**, the highest security value on the list.
5. Decide `rawUtf8`: implement or delete the parameter — **1 day**.
6. Duplicate-key rejection + shared parser limits — **4–6 days**, the largest genuinely open
   security item.
7. Everything else (full canonical/JCS format + hash migration, EVP migration, filesystem link
   policy, `char_traits`) is a **multi-week program** that should be scoped as its own workstream
   rather than folded into this merge.

Items 1–5 total roughly **one week** and clear every residual finding that is cheap and unambiguous.

## Verification

- Items 1–3 and 5 are covered by existing suites: `make -k -j4 utf_baselib`,
  `utf_baselib_tasks`, `utf_baselib_data`; item 2 needs a new permuted-property hash case in
  `TestDataModelDefault.h`.
- Item 4 needs a new negative test asserting a TLS 1.1 handshake is refused under the default
  policy and succeeds under the legacy opt-in — no such test exists today.
- Item 6 needs a shared accept/reject corpus executed against binaries built with each JSON
  backend; that harness does not exist and is a real part of its cost.
