# Residual Findings Only — After Filtering Everything Already Decided

**Assessment date:** 2026-09-03
**Verified against:** working tree at `e29eb06`
**Scope:** the Opus 5 review pair (`pr_review_analysis_opus5.md`, `..._deep_dive.md`), with every
finding covered by an existing written decision removed.

---

## Context and correction

My previous plan re-opened items that already carry a written decision — in a deferral record, a
plan record, or a comment in the source. That was wrong, and in one case I did it against a record
that explicitly says not to.

I re-read all eleven decision records and re-checked each finding against them. **Twelve of my
thirty rows are already decided and have been removed.** What follows is only what no record covers,
split into *full* residuals (nothing written about them) and *partial* residuals (a record exists but
leaves a specific piece open).

### The miss that matters

`notes/plans/issues/pr-review-gpt56sol-f01-f18-status.md` — which arrived in the `git pull` I ran
yesterday — already contains this:

> **The question that made it expensive is now answered: no `getObjectHash` output is persisted,
> signed, or used as a cache key outside the process** (confirmed by the author, 2026-09-02).
> There is nothing to migrate, so the deferral is **correct** rather than merely accepted.

You had already answered that question, and the record already drew the conclusion. I asked it again
anyway and then proposed reopening H-6 on the strength of your answer — directly against
`DataModelObject.h:287-306`, which states the contract and says in terms that it *"is not
re-litigated by review findings against this file."* **H-6 is removed entirely**, including the
performance variant I proposed. The one obligation that record creates is a release-note line saying
these hashes are process-local, and it is already tracked there.

Of the three questions I asked, only two produced new information: **IP-addressed TLS peers exist**
(§2.3, and no record covers it) and **the S3 index should render** (M-5, excluded from the XSS
commit by scope, not by decision).

---

## Removed — already decided, not resurfaced

| Finding | Decided where | Disposition |
|---|---|---|
| **H-6 / J-10** JSON hash ordering, canonical format | F-11 in `medium-severity-findings-f11-f17-plan.md`; `pr-review-gpt56sol-f01-f18-status.md` ("F-11 needs nothing further"); `DataModelObject.h:287-306` | Deferred, and now **correct** rather than merely accepted |
| **J-5, J-6** number / string formatting divergence | F-11 — *"not guaranteed byte-compatible … a design decision, not a cleanup"* | Deferred with the canonical format |
| **M-1 / J-1** duplicate keys | `json-duplicate-key-contract.md`; `JsonUtils.h:68-92` | Closed by decision — backend-defined |
| **M-6** S3 credentials on the command line | F-13, `medium-severity-findings-f11-f17-plan.md` | Deferred, ~1–2 d, own commit |
| **M-7** global `-Woverloaded-virtual` | F-18 item 1, `public-header-hygiene-deferral.md` | Deferred — **and the measurement spike I proposed is already prescribed there** |
| **H-2** `BOOST_ASIO_DISABLE_STD_CHRONO` half | F-18 item 2, same record | Deferred — and it **explicitly rejects** moving the define to `CPPFLAGS`, citing `pr_review_analysis_opus5.md:124`, because that breaks consumers who build with their own flags |
| **M-8 / C-2** copy skips special files | `residual-cxx-findings-deferral.md` item 2 | Deferred — **and the "log the skips" option I recommended is already named there** as the cheapest first step |
| **§2.1** EVP / provider migration | `evp-provider-migration-deferral.md` | Deferred behind OpenSSL 1.x retirement |
| **§2.1 FIPS/HSM note** | same record — *"non-extractable keys … become representable, which an `RSA*` cannot be"* | Already documented |
| **§2.5-minor** redundant `RSA_new()` | inside the EVP deferral's surface; removed by its step 3 | Subsumed |
| **L-6** Markdown/HTML escaping order | `generated-public-S3-index-permits-stored-XSS-plan.md` phase 2 prescribes exactly this order and this character set | **By design** — not a defect |
| **L-7 / CXX-11** deleted move assignment | `residual-cxx-findings-deferral.md` item 3 | Deferred; no release note, by explicit decision |
| **H-4** `-DestinationPath` | `supply-chain-verification-deferral.md` | Deferred to devenv8 |
| **H-5 / F-02** download integrity | same record | Deferred — but note the record now says it *"must be resolved as part of the devenv8 upgrade, **not deferred again by default**"*, and that steps 1–2 are ~1 day and retire most of the risk |

---

# Full residuals — no record covers these

## R-1 (High) — §2.3: `host_name_verification` drops IP-address matching

**Where:** `AsioSslCompat.h:41`; consumed at `AsioSslStreamWrapper.h:485`, called at `:151`.

The only record touching this file explains the Boost rename and nothing else. No deferral, no
source comment, and it is absent from the `pr-review-gpt56sol-f01-f18-status.md` "Still open" table.

**Your answer makes it live.** devenv7 ships Boost 1.90, the typedef is active at
`BOOST_VERSION >= 108900`, and `verifyCertificate` calls `rfc2818( preVerified, ctx )` straight into
`X509_check_host()`, which does not match IP addresses. **Every TLS handshake to an IP-addressed peer
is failing verification on current devenv7 builds.** `AsioSslStreamWrapper.h` is not in the branch's
changed-file list — its behaviour changed without its source changing.

- **Design intent.** The substitution is a genuine security improvement (correct SAN handling,
  CN ignored when a SAN is present per RFC 6125, embedded NULs rejected). Do not revert it. The
  library's forced-upgrade model tolerates the break; what it does not tolerate is the break being
  silent and unrecorded.
- **Fix.** Dispatch on whether `m_hostName` is an address literal — `boost::asio::ip::make_address`
  with an `error_code` is the test — and use `X509_check_ip_asc` for that branch, `X509_check_host`
  otherwise. Keep the shim in `AsioSslCompat.h` a pure Boost typedef; put the dispatch in a
  project-owned verifier near `AsioSslStreamWrapper.h:485`.
- **Size/cost: ~1.5 d.** The dispatch is small; producing an IP-SAN test certificate under `certs/`
  and wiring it into `utf_baselib_http` is the bulk.
- **Complexity: Medium. Risk: Low to fix, High to leave** — the fix only widens what verifies, so it
  cannot break a passing handshake. **Blast radius:** every IP-addressed TLS peer.
- **Performance:** `X509_check_ip` is a byte comparison against the SAN list. Immaterial.
- **Also needs a release-note line** — this belongs on the existing release-notes list.

## R-2 (Medium) — M-10: `BL_DM_DECLARE_CUSTOM_PROPERTY` lost its `std::move`

**Where:** `DataModelObjectDefs.h:504` — `m_ ## name = BL_JSON_ITER_VALUE( pos );`

Not mentioned in any record. F-11's sub-item A4 fixed nested `canonicalize` forwarding in the same
file; this is a different macro and was not touched.

**Design intent — decisive.** `BL_JSON_ITER_VALUE( pos )` is `pos -> value()`, returning `value&` on
a non-const object, so this deep-copies an arbitrarily large JSON subtree per custom property per
deserialize. It sits inside `a81dd34` ("JSON data model performance improvements — stage 1-4"),
whose stated purpose was eliminating this exact class of copy;
`json-data-model-performance-improvements-plan.md` lists six sibling sites where the same
`copy → move` fix was applied and measured. Adjacent code already gets it right —
`DataModelObjectDefs.h:573` and `DataModelObject.h:66`.

- **Performance.** The affected properties are `BrokerProtocol::passThroughUserData` and
  `FunctionInputData::arguments` — the opaque, caller-supplied, **unbounded** payloads. The
  equivalent Stage 4 two-line move fix measured a **46% reduction** in complex serialization
  overhead.
- **Size/cost: ~2 h.** One token; the work is confirming no caller reads `map` after
  `deserializeProperties` (same invariant Stage 1 relied on) plus a benchmark run.
- **Complexity: Low. Risk: Low. Blast radius:** in-repo.
- **This is the highest confidence-per-hour item on the list.**

## R-3 — M-3: `canonicalize` conflates three meanings — **DECIDED 2026-09-03: document, do not fix**

**Where:** the throw at `BoostJsonImpl.h:538` / `JsonSpiritImpl.h:763`; the data-model meaning at
`DataModelObjectDefs.h:181, 328, 400, 487, 548`.

> **DECIDED: keep the current behaviour exactly as it is, and record that decision in the code** so
> it stops being re-filed. No renames, no parameter split, no removal of the `prettyPrint` /
> `canonicalize` mutual exclusion.

**What the parameter does today.** One `bool` carries three behaviours:

1. **Data model:** emit properties even when unset (`if( canonicalize || m_..IsSet )`).
2. **Data model, currently undocumented:** it **suppresses the required-property check.** At
   `DataModelObjectDefs.h:185` the `else if( isRequired && ! IsSet )
   BL_DM_THROW_REQUIRED_PROPERTY_NOT_SET` branch is unreachable when `canonicalize` is true, so
   canonical serialization accepts an object that packed serialization rejects — and a canonical
   hash can therefore be computed over an object that is not valid. **This appears in no review and
   in no record; it is the substantive part of this item.**
3. **Serializer:** sort keys — and refuse to also pretty-print.

### Why keeping it is defensible, and what the record must say

The rationale below is what makes this a decision rather than an omission, and it belongs in the
comment:

- **No caller wants the meanings separated.** `getObjectHashCanonical` wants both true;
  `getObjectHash( canonicalize = false )`, `getDocAsPrettyJsonString` and
  `getDocAsPackedJsonString` want both false. Splitting is speculative generality, which
  `AGENTS.md` rules out directly.
- **"Canonical form" is a coherent single concept** — sorted keys *and* every property emitted — so
  one flag driving both is a reasonable expression of it rather than an accident, even though the
  two behaviours are implemented in different layers.
- **A compatible split is not available.** The old and new parameters would occupy the same
  positional slot, so `getJsonString( obj, true, true )` would compile under both spellings and mean
  different things — producing different bytes and a different hash. That is exactly the **F-12**
  failure mode (`saveToStream`'s third parameter went `unsigned` → `bool` and old call sites
  silently became "pretty print"), whose recorded fix was `= delete` precisely to make silent
  rebinding impossible. Any staged migration here reintroduces what F-12 removed.
- **The `prettyPrint` exclusion stays.** It is not a technical constraint —
  `prettyPrint( canonicalizeValue( val ) )` composes trivially — but the combination has no caller,
  and a throw that no caller reaches is not worth a behaviour change. The comment should say the
  exclusion is a deliberate narrowing, not an implementation limit, so the next reader does not
  file it as a bug.

### The deliverable

Comments at three places, cross-referencing each other:

| Location | What it records |
|---|---|
| `DataModelObjectDefs.h:178-186` | Meaning (1) **and (2)** — state plainly that a `true` value suppresses the required-property check, and what that implies for a canonical hash |
| `BoostJsonImpl.h:538` / `JsonSpiritImpl.h:763` | Meaning (3) and why the mutual exclusion is deliberate rather than a limitation |
| `DataModelObject.h`, near `getJsonString` | That one flag intentionally drives both layers, that this is "canonical form" as a single concept, and that a split was considered and rejected — with the F-12 reasoning |

- **Size/cost: ~0.25 d.** **Complexity: Low. Risk: none. Blast radius: none.**
- **Performance: unchanged.** Note for the record that canonical serialization costs ~5.5x
  non-canonical on Boost.JSON and ~1.0x on json-spirit
  (`notes/performance/json-library-performance-comparison.md`), because `canonicalizeValue` rebuilds
  the value tree — relevant to anyone who later reconsiders the default, and not currently written
  down anywhere.

## R-4 (Medium) — J-8: `saveToStream` has no `canonicalize`

**Where:** `JsonUtils.h:157-166`; `Manifest.h:447`.

F-12 is explicit that it took **reduced scope** — the deleted integral overload only. The missing
parameter is not covered.

- `saveToStream` exists so a large document need not be materialized as a string; the natural reason
  to want that is signing or hashing a large document, which is exactly the case needing canonical
  ordering. `Manifest::write` is therefore permanently non-canonical.
- **Worth knowing:** the Boost backend's `saveToStream` (`BoostJsonImpl.h:596`) delegates to
  `saveToString` and streams the result — it already materializes the string and delivers none of
  the streaming benefit it advertises.
- **Interaction with R-3's decision.** The parameter keeps the name `canonicalize` and the
  `prettyPrint` mutual exclusion applies to it too, so `saveToStream` gains *compact* canonical
  output only. That is the shape the signing use case wants, so the restriction costs nothing here —
  but the parameter's documentation should point at R-3's comment rather than restating it.
- **Cost: ~0.25 d** for the parameter and a test. **~1 d** more to make it genuinely streaming, and
  only worth it if signed documents get large. **Complexity: Low. Risk: none** (additive with a
  default).

## R-5 (Medium) — J-2: Boost.JSON's 32-level parse depth is unset, undocumented, untested

**Where:** no `parse_options` anywhere.

**Explicitly flagged as remaining** by `json-duplicate-key-contract.md`: *"The parser resource limits
sub-finding of CXX-01 (no shared limit on input size, depth, member count or string length) is
untouched by this record. Boost.JSON applies its own default maximum nesting depth of 32;
json-spirit applies none."* Acknowledged, not decided — so it is residual, and the record is the
right place for the decision to land.

- **Exposure.** The library's own models nest ~4–5 levels
  (`AsyncRpcPayload → AsyncRpcResponse → ServerErrorJson → ServerErrorResult → ExceptionProperties`).
  The risk is the **caller-controlled passthrough** — `BrokerProtocol::passThroughUserData`,
  `FunctionInputData::arguments` — which is unbounded. A devenv4 node accepts what a devenv7 node
  rejects.
> **DECIDED 2026-09-03: option A, `max_depth = 512`, Boost.JSON only.** Do not change or enforce
> anything on the json-spirit side.

**What this means concretely.**

- Set `parse_options::max_depth = 512` explicitly at the Boost.JSON `readFromString` and
  `readFromStream` entry points (`BoostJsonImpl.h:183` one-shot, `:216` streaming), replacing the
  implicit library default of 32.
- **This is a compatibility improvement, not a restriction.** Every document between 33 and 512
  levels deep is rejected today and will be accepted after. Nothing that parses now stops parsing.
- json-spirit keeps its unbounded behaviour. The limit therefore becomes an explicit, documented
  **backend-defined** property — exactly the shape of the duplicate-key contract, and it belongs in
  the same place in `JsonUtils.h`, cross-referencing `json-duplicate-key-contract.md`, whose
  "Related" section is where this sub-finding was flagged as untouched.
- The residual divergence after this change is documents deeper than 512, where Boost.JSON rejects
  and json-spirit accepts. That is the deliberate position, and writing it down is what stops it
  being re-filed.

- **Size/cost: ~0.75 d** — two call sites, the contract paragraph, and a depth test (a 600-level
  document must be rejected, a 500-level one accepted) on the Boost backend.
- **Complexity: Low. Risk: Low** — strictly more permissive on the only backend touched.
- **Blast radius:** none negative. **Performance:** none — `max_depth` is a guard, not a cost.
  20-level parse is 0.0018 ms/iter on Boost.JSON release.

## R-6 (Medium) — §1.2: the two JSON test helpers assert almost nothing

**Where:** `TestJsonAbstraction.h:53-75`, `:80-85`. No record.

`verifyCanonicalDeterminism` calls a pure function twice and compares — a tautology backing
`JsonSerializeCanonical` and `JsonSerializeOptions`. `verifyRoundTrip` compares only top-level
container kind and element count; a round-trip corrupting every string value passes.

- The comment *"exact string comparison may fail due to formatting differences"* is the finding.
- **Options: (A)** golden-string assertions — ~1 d, most valuable alongside R-11. **(B)** a recursive
  deep-equality helper — ~0.5 d, backend-neutral, catches corruption. **(C)** both.
- **Recommendation: B now, A with R-11.** B is cheap and independent.
- **Complexity: Low. Risk: Low. Blast radius:** tests.

## R-7 (Medium) — E-3: no test for the cancel/flush non-generation promise

**Where:** the promise is at `ExecutionQueueNotify.h:148-149`. No record.

Partly closed — `Tasks_ExecutionQueueThrottleFromObserverTest` and the delivery tests were added.
What remains: nothing asserts that removing the last pending task via cancellation or flush does
**not** generate `AllTasksCompleted`, and there is no randomized multi-thread soak observing
notifications.

- **Cheaper than previously estimated** — the harness exists:
  `ExecutionQueueNotificationTestContext`, `ExecutionQueueCompletionControl`,
  `recorder -> eventCount( eventId )`, `setHook`.
- **Options: (A)** the cancel/flush case only — **~2 h, ~30 lines**. **(B)** plus a soak — ~1.5 d,
  and easy to write flaky.
- **Recommendation: A now.** It protects a written contract for two hours. B when someone next
  touches the generation logic. **Risk: very low. Blast radius:** tests.

## R-8 (Medium) — E-4: event collapsing under load is undocumented

**Where:** `ExecutionQueueNotify.h:142-163`. No record — the execution-queue plan's only "collapse"
references are about throughput, not events.

The header documents point-in-time semantics and states plainly that `AllTasksCompleted` is not a
drain barrier. It does **not** say that a completed drain cycle can pass **producing no event at
all**, which the generation dedupe permits when a slow callback on generation G returns after a
second cycle published G+1. A consumer using the event as a batch boundary silently misses
boundaries exactly when the system is busiest.

- **Cost: ~1 h.** One paragraph, in a header whose existing documentation is unusually candid about
  weakened guarantees — this is the one thing it does not admit. **Risk: none.**

## R-9 (Medium) — C-3: `os::copy` shim guard keys on devenv version, not Boost version

**Where:** `OSImplPlatformCommon.h:1973`, `:1996`.

Not covered: F-14 swept **makefile** gates only, F-17 fixed the `ec` overload in the same file. The
`e29eb06` sweep that caught `msvc-default.mk` did not reach C++ preprocessor guards.

```cpp
#if ( defined( BL_DEVENV_VERSION ) && BL_DEVENV_VERSION > 5 ) || ( ( BOOST_VERSION / 100 ) >= 1084 )
```

The arithmetic is right; the `||` is not. The devenv disjunct activates the shim whenever
`BL_DEVENV_VERSION > 5` **regardless of the Boost in use**, and `BOOSTDIR` is overridable
(`3rd/boost/common.mk:4`).

- **Design intent.** The comment says the guard exists because *"the behavior of the copy function
  has changed for directories"* — a Boost behaviour. The condition should key on the thing that
  changed. Same class as F-14.
- **Options: (A)** drop the devenv disjunct — ~0.5 d incl. rebuilds on affected tags. **(B)** `&&`
  them — wrong for devenv4 on new Boost. **(C)** leave.
- **Recommendation: A. Complexity: Trivial. Risk: Low-Medium** — needs confirmation no supported
  devenv6/7 config ships Boost < 1.84. **Performance:** none.

## R-10 (Medium) — M-9: `ifdef`/`ifndef` on tri-state build flags

**Where:** `3rd/boost/common.mk:55, 72, 80`; `3rd/json-spirit/4.08.mk:5`. No record.

`make BL_USE_JSON_SPIRIT=0` selects **json-spirit**; `make NO_BOOST_LOCALE_LIB=0` **disables**
boost_locale. Both are user-facing knobs documented in `CONTRIBUTING.md`.

The internal logic is coherent — `BL_USE_JSON_SPIRIT := 1` is assigned inside the `ifndef` for
legacy devenvs, so auto-selection works. The bug is only for a user passing `=0`, which is the
natural way to say "no".

- **Options: (A)** `ifeq ($(strip $(X)),1)` at all four sites plus `$(error)` on an unrecognized
  value — ~0.5 d incl. a matrix rebuild. **(B)** without the `$(error)` — ~2 h, leaves typos silent.
- **Recommendation: A** — the `$(error)` is what makes it a real fix. **Complexity: Trivial.
  Risk: Low-Medium. Blast radius:** build configuration; fails at build time, not runtime.

## R-11 (Medium) — M-11: mandatory `boost_json` link dependency, undiscoverable

**Where:** `3rd/boost/common.mk:80-84`. No record.

A consumer including `JsonUtils.h` from their own build system gets the Boost.JSON path by default
(it is the `#else`) and hits unresolved symbols with nothing pointing at the cause. json-spirit was
header-only; the default backend is not.

- **Options: (A)** a note in `JsonUtils.h` + a CONTRIBUTING entry — ~1 h. **(B)** header-only
  Boost.JSON — investigate first: `BOOST_JSON_STANDALONE` was **removed in Boost 1.81**, so on 1.90
  this means `<boost/json/src.hpp>` in exactly one TU, which a header-only library cannot do on its
  consumer's behalf. Probably a dead end; ~1–2 d to confirm.
- **Recommendation: A.** **Complexity: Trivial. Risk: none.**

## R-12 (Medium) — M-5: S3 index uploaded with no `ContentType` or hardening headers

**Where:** `s3_manage.py:1388-1392`. `ExtraArgs` carries only the sha256 metadata.

**Prior scope note, not a decision.** `generated-public-S3-index-permits-stored-XSS-plan.md` says
*"Do not set `ContentType` on `upload_file` **in this change**"* and lists ContentType/CSP among
that commit's non-goals. That is a scope boundary for one commit; no deferral record covers it.

**Your answer makes it a functional defect.** S3 stores the object as `binary/octet-stream`, so
browsers download rather than render — and the index is meant to render.

- **The trap.** That accident is currently the only defence in depth behind the stored-XSS fix.
  Setting `ContentType` **re-arms** the surface, so it must ship with `nosniff` and a restrictive
  CSP (`default-src 'none'; style-src 'unsafe-inline'` suffices — the page has no scripts), plus a
  re-read of `validate_index_url_prefix` / `encode_s3_key_for_url` / `html.escape(quote=True)`.
- **Never ship `ContentType` alone** — that is strictly worse than the status quo.
- **Cost: ~0.5 d**, of which the headers are ~30 min. **Complexity: Low. Risk: Medium.**
  **Blast radius:** anyone opening the public index.

## R-13 — M-12: `set -e` missing in nine privileged scripts — **DEFERRED 2026-09-03**

**Where:** `scripts/devenv7/rosetta/{rhel,ubuntu}/`, `scripts/devenv7/docker/{rhel,ubuntu}/`.

> **DECIDED: defer, and write a deferral record.** These scripts must not be touched now because the
> change **cannot be validated in the current environment** — verifying `set -euo pipefail` on
> privileged provisioning scripts requires actually running them on rhel and ubuntu hosts. To be
> picked up aspirationally the next time the deployment scripts and their design are refreshed.

**The deliverable is the record, not the fix.** New file, following the established shape
(`residual-cxx-findings-deferral.md`, `public-header-hygiene-deferral.md`): a `## Decision` block
with date and status, what is not protected, why it is deferred, what limits the exposure, and
conditions to revisit.

Content it should carry, all verified:

- All nine set neither `set -e` nor `set -u`; `set -o pipefail` appears **nowhere** under `scripts/`.
- These are the scripts that install system components, register binfmt handlers and run privileged,
  so a failed step is followed by the next running against a half-configured system.
- **What limits the exposure:** the nine total **125 lines** and five are under 16, so they are
  reviewable in full at a glance; they already mark expected failures explicitly
  (`sudo docker buildx rm rosetta-builder || true`); and they are developer/CI provisioning, not
  library code — nothing shipped depends on them.
- **Why deferring is the right call rather than a cheap edit:** `set -e` on a script that has lived
  without it typically reveals two or three steps that were silently failing, and each is a decision
  that needs the script actually run to make. Adding it blind converts a silent half-configuration
  into a *loud* half-configuration at an arbitrary point, which is not obviously better.
- **Inconsistency to note:** all 19 devenv7 *build* scripts already set `-e` and `-u` correctly.
  These nine are an inconsistency, not a considered exception — the record should say so, so the
  next author does not have to re-derive it.
- **Conditions to revisit:** the next refresh of the deployment scripts; any report tracing to a
  half-configured provisioning run; any of the nine growing beyond a page.

- **Size/cost: ~0.25 d** to write the record. **Risk: none.** **Blast radius:** none.

## R-14 (Medium) — §2.1 residual: `HmacSha256` uses OpenSSL-3-deprecated APIs

**Where:** `HmacSha256.h:56-90`. **Not covered by the EVP deferral**, whose scope is the RSA key
type — its file list is `RsaKey.h`, `RsaEncryption.h`, `RsaSignVerify.h`, `OpenSSLTypes.h`,
`JsonSecuritySerializationImpl.h`. `HmacSha256.h` is absent, and `EVP_MAC` is never mentioned.

This is a code path that exists *specifically* for `OPENSSL_VERSION_NUMBER >= 0x30000000L` and is
written against `HMAC_CTX_new` / `HMAC_Init_ex` / `HMAC_Update` / `HMAC_Final` — all deprecated in
3.0 in favour of `EVP_MAC` / `EVP_MAC_CTX`. It will need rewriting again.

- **Independent of the deferred programme** — no `RSA*` involvement, no OpenSSL 1.x precondition.
- **Cost: ~1 d. Complexity: Low. Risk: Low** — HMAC-SHA256 output is defined by the algorithm, so a
  byte-comparison test against the current implementation makes this safe. **Performance:** neutral.
- Consistent with the deferral's own standing rule ("new code uses `evpKey()`"), extended to MAC.

## R-15 (Low) — J-3 and J-9: accept/reject divergences with no test

| ID | Divergence |
|---|---|
| J-3 | Invalid UTF-8 inside a string literal — json-spirit passes bytes through, Boost.JSON rejects |
| J-9 | Trailing data after a complete document — Boost.JSON raises `error::extra_data` |

Distinct from J-5/J-6 (formatting, deferred with F-11): these are **accept/reject** differences, so
a document one node accepts and another rejects is an interop defect, not a cosmetic one.

- **Cost: ~0.75 d** for both, run under R-18's matrix. **Complexity: Low. Risk: Low.**

## R-16 (Low) — the small, unambiguous ones

| ID | Where | Issue | Fix | Cost |
|---|---|---|---|---|
| §2.2-minor | `CryptoBase.h:409` | `( void ) ::OPENSSL_init_ssl( 0, nullptr );` discards a return that is 0 on failure — unlike `SSL_library_init`, whose always-1 contract the retained 1.1.x comment correctly notes. A failed init proceeds silently into `initRandomEngine()` | `BL_CHK_CRYPTO_API_NM(...)` | 0.5 h |
| L-2 / J-7 | `BoostJsonImpl.h:426-451` | Pretty-print emits `{\n\n}` / `[\n\n]` for empty containers — valid JSON, needless divergence from json-spirit's `{}`. No comment marks it deliberate | guard the newline on `! obj.empty()` | 1 h |
| L-9 | `Pinger.h:434-444`, `:486` | `g_patternAvgRttLinuxOrDarwin` holds the **Darwin-only** BSD pattern (`round-trip min/avg/max/stddev`) while `g_patternAvgRttLinux` holds the iputils one (`rtt min/avg/max/mdev`) — the name is simply wrong. Separately, on Windows both accessors return `g_patternAvgRttWindows`, so input is matched twice against an identical regex | rename; skip the redundant Windows match | 1 h |
| E-5 | `ExecutionQueueImpl.h:583`, `:587` | Cites `TaskBase.h:52-59` and `:65-71`; the passages actually span **51–60** and **66–72** | cite by section name | 15 min |
| L-5 | `AGENTS.md:24` | Hard-codes `/Users/lazar/...` on a `/home/lazar/...` checkout, and names `make python-install` where the target is `pytest-install`. **Wider than filed** — `make python-install` also appears in `generated-public-S3-index-permits-stored-XSS-plan.md:115` and `s3-download-escape-plan.md:95`. An agent reading these as instructions is misled | fix all three | 15 min |
| L-4 | `scripts/tests/pytest.ini:20-29` | Comments indented *inside* the `filterwarnings` linelist. Whether iniconfig strips them varies by version — **still unverified** | **run pytest once**, then move the comments above the key regardless. `.venv` does not exist here; `make pytest-install` first | 15 min + one run |

---

# Partial residuals — a record exists but leaves a piece open

## R-17 (High, partial) — H-2: no size pin on the wire frame

**Covered and removed:** the `BOOST_ASIO_DISABLE_STD_CHRONO` half (F-18 item 2) and the
include-order symptom (F-16, fixed). **F-18 item 2 explicitly rejects** moving the define to
`CPPFLAGS` — my previous option B — because that breaks consumers who compile with their own flags.
That reasoning applies to the UUID macro too. **Both options I previously recommended are therefore
off the table.**

**What is not covered.** F-18 item 2 says of `UuidBoostImports.h`: *"the same class of problem …
tracked separately as F-16 (Medium), where the consequence reaches binary wire layout. **F-16 should
be fixed on its own merits; it is not covered by this deferral.**"* F-16 was closed — but its fix was
the `<boost/version.hpp>` include, which cured a hard compile error (`BOOST_VERSION == 0`), not the
layout exposure. **No record pins the layout.**

**And the layout consequence is concrete.** `CommandBlock` is memcpy'd onto the socket, with
`sizeof` as the framing length:

```cpp
asio::buffer( &m_cmdBuffer, sizeof( m_cmdBuffer ) )   // TcpBlockTransferServer.h:467, 509, 1389
                                                      // TcpBlockTransferClient.h:339, 500
detail::chkPartialDataTransfer( sizeof( m_cmdBuffer ) == bytesTransferred );
```

With the macro in effect it is **72 bytes**; without it (`alignof(uuid) == 8`) `DataHeader` pads
28 → 32 and the struct becomes **80**. All three existing `static_assert`s still pass at 80:
`80 % 8 == 0`; `sizeof(data) == sizeof(data.reserved)` (both 32); `sizeof(data.raw)` is defined as
`sizeof(tagReserved)` so it tracks. A 72-byte peer and an 80-byte peer **desynchronize the stream**,
because the byte count is the framing.

- **Residual fix, and the only one left: `static_assert( sizeof( CommandBlock ) == 72 )`.**
- **Cost: ~2 h. Complexity: Low. Risk: Very low** — it changes no build and cannot break anything.
  If it fires, that is the finding. **Blast radius:** the build, and only if the hazard is already
  real.
- F-18 item 2 names an `#error` tripwire as *"the cheapest future mitigation and should be the first
  thing tried when this is revisited"* — the same shape would suit the UUID macro, but that is a
  downstream-visible change and belongs with the F-18 revisit, not here.

## R-18 (Medium, partial) — §1.1: the dual-backend build is prescribed but not runnable as a step

**Acknowledged in writing**, twice — `json-duplicate-key-contract.md`: *"no build or test target in
this repository sets `BL_USE_JSON_SPIRIT`, and there is no CI … 'Supported' means buildable on
demand and documented. It does **not** mean continuously verified."* And
`pr-review-residual-cxx-findings-plan.md:461` prescribes it as a verification step: *"**Also
`make -k -j4 BL_USE_JSON_SPIRIT=1` plus the same suites** — that backend has no CI; it must be built
and run explicitly."*

So the *practice* is decided and the limitation is recorded. **What is residual is that the
prescribed step is not a repeatable target**, so it depends on each author remembering it.

- **Much cheaper than I previously estimated.** The plumbing exists: `projects/make/ci-init-env.mk`,
  json-spirit installed by the devenv7 setup scripts, and
  `~/swblocks/dist-devenv7-ub24-gcc1520-clang2010-a64/json-spirit/4.08/source` present on this
  machine. This is a make target, not a CI project.
> **DECIDED 2026-09-03: option C — leave it manual, and record the decision** so it stops appearing
> in reviews.

**The deliverable is a decision record, not a target.** The practice is already correct and already
written down in two places; what is missing is a single record that says *this is deliberate*, so
the next reviewer reads "decided" rather than "gap".

It should state:

- The dual-backend run is **manual and prescribed**, not absent:
  `make -k -j1 utf_baselib_data VARIANT=release BL_USE_JSON_SPIRIT=1` — the focused form.

  **The record must correct the command it inherits.** `pr-review-residual-cxx-findings-plan.md:461`
  prescribes `make -k -j4 BL_USE_JSON_SPIRIT=1`, an all-targets parallel build, which contradicts
  `AGENTS.md:67` ("never do full parallel builds of the entire repo unless explicitly requested")
  and `:69` (more than one module → `-j1`). The dual-backend check does not need a repo build; it
  needs `utf_baselib_data`, which is where the JSON coverage lives. Restate it focused, and say why,
  so the wrong form is not copied forward again.
- **Run it in release.** json-spirit debug is 140x slower on medium documents
  (`json-library-performance-comparison.md`) and will look hung.
- It is required when a change touches either JSON backend adapter, the data-model macros, or
  `JsonUtils.h` — and not otherwise.
- **What limits the exposure:** shared production code is verified to sit inside the backend
  intersection today — repo-wide greps for `.kind()`, `is_number()`, `is_primitive()`,
  `is_structured()`, `if_contains(`, `boost::json::` return zero hits outside the two impl headers.
- **Why not automate it:** the repository has no CI at all, so this would mean introducing CI rather
  than adding a job — an organizational step disproportionate to the gap. `json-duplicate-key-contract.md`
  already states the honest position: *"'Supported' means buildable on demand and documented. It does
  **not** mean continuously verified."*
- **Conditions to revisit:** CI is introduced for any other reason; or a backend divergence reaches
  a release.

**Consequence for R-6 and R-15:** they are not blocked, but their verification step is this manual
run. Note it in each commit message rather than assuming a target exists.

- **Size/cost: ~0.25 d** to write the record. **Risk: none.**

## R-19 (Medium, partial) — §1.4: no documented portable backend subset

`json-duplicate-key-contract.md` notes json-spirit *"differs in ways beyond duplicate keys"* but does
not say which. Re-verified: production code **is** clean today — zero `.kind()`, `is_number()`,
`is_primitive()`, `is_structured()`, `if_contains(`, `boost::json::` outside the two impl headers.
The risk is future drift.

- **Residual: write the subset down** in `JsonUtils.h`, including the `as_string()` return-type
  workaround the tests already use (`TestDataModelDefault.h:192`). ~2 h.
- Mechanical enforcement is what R-18 provides; a `static_assert` cannot express "don't call
  `.kind()`".

## R-20 — carried forward from the author's own "Still open" list

These are in `pr-review-gpt56sol-f01-f18-status.md` and are neither closed nor deferred, so they
belong on a residual list:

| Item | Note |
|---|---|
| **Windows devenv7 verification** | Required for F-14's flipped gates, `OSImplWindows.h`'s `<cstring>` addition, and the real `cl.exe` half of F-15. None of it is verifiable from this checkout |
| **Release notes** | Four already listed: PEM formats (PKCS#1 → SPKI/PKCS#8), mandatory `NotifyDelivery`, deleted `Manifest`/`Platform` move assignment, process-local JSON hashes. **R-1 adds a fifth** — the `host_name_verification` semantics change |

---

# Staging — layers, slices and dependencies

Seven layers. A **layer** is a staging step; a **slice** is one logically coherent commit or small
group of commits. Slice names are meant to be used as commit prefixes.

### Dependencies — only two remain

```
  S-L · parser limits (max_depth)  ──prefer before──►  S-N · backend divergence tests
        both touch readFromString / readFromStream; writing the tests first means rewriting them

  L0 · TLS fix  ──first by urgency, not dependency──►  everything else
```

R-3's decision to document rather than rename **removed the old L4 → L5 hard dependency.** No
parameter names change, so the JSON test slices no longer wait on the JSON API slice. Everything
except the pair above is independent, and **L2, L3, L6 and L7 are fully parallel-safe.**

---

## L0 · "Stop the bleeding" — 1.5 d

| Slice | Items | Days |
|---|---|---:|
| **S-A · TLS peer verification** | R-1 — IP-aware verifier (`X509_check_ip_asc` dispatch), IP-SAN test cert under `certs/`, `utf_baselib_http` coverage | 1.5 |

Ships alone, before anything else — priority, not dependency. The only live production regression.

## L1 · "Record and pin" — 2.1 d

Do this before touching code. Three of its four slices exist to stop findings resurfacing; the
fourth tells you whether L4 has a bigger problem hiding behind it.

| Slice | Items | Days |
|---|---|---:|
| **S-B · Deferral and decision records** | R-13 (`set -e` deferral) · R-18 (manual dual-backend decision) | 0.5 |
| **S-C · Contract documentation in code** | **R-3** (canonicalize kept as-is — all three meanings, incl. the required-property bypass) · R-8 (event collapsing) · R-19 (portable backend subset) · R-11 (link dependency note) | 1.1 |
| **S-D · Invariant pin** | R-17 — `static_assert( sizeof( CommandBlock ) == 72 )` | 0.25 |
| **S-E · Release notes** | R-20 — five entries: PEM formats, mandatory `NotifyDelivery`, deleted `Manifest`/`Platform` move assignment, process-local JSON hashes, **and `host_name_verification` semantics from S-A** | 0.25 |

No behaviour change anywhere in this layer. **S-D is the one to watch** — if the assert fires, the
72/80-byte framing hazard is already real.

## L2 · "Micro-corrections" — 0.85 d *(parallel-safe)*

| Slice | Items | Days |
|---|---|---:|
| **S-F · Data-model performance regression** | R-2 — `std::move` in `BL_DM_DECLARE_CUSTOM_PROPERTY` | 0.25 |
| **S-G · Crypto error handling** | §2.2-minor — check `OPENSSL_init_ssl`'s return | 0.1 |
| **S-H · Text and citations** | L-2/J-7 (empty-container pretty print) · L-9 (Pinger regex naming + double Windows match) · E-5 (`TaskBase.h` line citations) · L-5 (`AGENTS.md` path and `pytest-install`, plus the two plan docs repeating `python-install`) · L-4 (verify `pytest.ini`, then move the comments) | 0.5 |

**S-F is the best value-per-hour item in the plan.**

## L3 · "Build conditionals" — 1.0 d *(parallel-safe)*

Grouped because both are conditional-compilation correctness whose failure mode is "wrong branch
taken silently", and both need the same multi-config rebuild campaign. One campaign covers the pair.

| Slice | Items | Days |
|---|---|---:|
| **S-I · Preprocessor guards** | R-9 — `os::copy` shim keys on `BOOST_VERSION`, not `BL_DEVENV_VERSION` | 0.5 |
| **S-J · Make flag semantics** | R-10 — `ifeq ($(strip $(X)),1)` + `$(error)` on unrecognized values, four sites | 0.5 |

## L4 · "JSON public surface" — 1.0 d

| Slice | Items | Days |
|---|---|---:|
| **S-K · Stream serialization parity** | R-4 — give `saveToStream` the `canonicalize` parameter `saveToString` already has | 0.25 |
| **S-L · Parser limits** | R-5 — `max_depth = 512`, Boost.JSON only, documented as backend-defined beside the duplicate-key contract | 0.75 |

## L5 · "Test integrity" — 1.75 d

| Slice | Items | Days |
|---|---|---:|
| **S-M · JSON helper strength** | R-6 — recursive deep-equality helper, plus golden-string assertions | 0.75 |
| **S-N · Backend divergence tests** | R-15 — J-3 (invalid UTF-8 in a literal) and J-9 (trailing data): accept/reject divergences, not formatting | 0.75 |
| **S-O · ExecutionQueue contract regression** | R-7 — assert cancel/flush does not generate `AllTasksCompleted`; ~30 lines on the existing harness | 0.25 |

**S-N prefers to follow S-L** — both touch the parse entry points. S-M and S-O are independent.

## L6 · "Scoped crypto modernization" — 1.0 d *(parallel-safe)*

| Slice | Items | Days |
|---|---|---:|
| **S-P · MAC to EVP** | R-14 — `HmacSha256` onto `EVP_MAC` / `EVP_MAC_CTX` | 1.0 |

Deliberately **outside** the deferred EVP programme: no `RSA*` involvement, no OpenSSL 1.x
precondition.

## L7 · "Tooling" — 0.5 d *(parallel-safe)*

| Slice | Items | Days |
|---|---|---:|
| **S-Q · S3 index rendering** | R-12 — `ContentType` **plus** `nosniff` **plus** CSP, together, with a re-read of the escaping path | 0.5 |

**Never ship `ContentType` alone** — `binary/octet-stream` is currently the only defence in depth
behind the stored-XSS fix.

---

# Grouping by verification

The staging above groups by *what the code is*. This groups by *what it costs to prove* — the axis
that actually determines how work batches, because for several slices the verification dominates the
edit.

**Governing rules — `AGENTS.md:64-77`, "Building and Testing Changes":**

- **Never do full parallel builds of the entire repo unless explicitly requested.**
- Validate with **focused builds of the individually affected test modules only**.
- More than one test module, or the whole repo → **`-j1`, never parallelized**.
- **Tests** may be parallelized, up to **5 test modules concurrently**.
- Build and test the relevant mix of toolchains and variants: Linux **`gcc1520` and `clang2010`**
  via `TOOLCHAIN=`, and **both `VARIANT=debug` and `VARIANT=release`**.

So no slice below carries a full-repo build. Each names the specific modules it affects, and the
"cost" column counts *module × variant × toolchain* build units.

| Bucket | How it is verified | Slices | Work | Affected modules | Units |
|---|---|---|---:|---|---:|
| **V-0 · Review only** | No build at all — Markdown | S-B (both records) · S-E (release notes) · S-H(L-5) | 1.0 d | — | 0 |
| **V-1 · Focused compile-proof** | Comments in public headers, plus one assertion where the compile *is* the test. Build only the modules that include the touched headers | S-C · S-D | 1.35 d | `utf_baselib_data`, `utf_baselib_tasks`, `utf_baselib_messaging` | 12 |
| **V-2 · Single-module test** | One module, both variants, both toolchains | S-F, S-G · S-H(L-2, L-9, E-5) · S-K · S-O | 1.1 d | one each: `utf_baselib_data`, `utf_baselib_security`, `utf_baselib`, `utf_baselib_tasks` | 4 each |
| **V-3 · Dual-backend** | V-2 **plus** a json-spirit run of the same module | S-L · S-M · S-N | 2.25 d | `utf_baselib_data` | 6 each |
| **V-4 · Multi-config rebuild** | The same module across devenv tags; a single config cannot show a wrongly-taken branch | S-I · S-J | 1.0 d | `utf_baselib` | ~16 |
| **V-5 · New test artifacts** | Needs an IP-SAN certificate that does not exist yet | S-A | 1.5 d | `utf_baselib_http` | 4 |
| **V-6 · Byte-comparison** | Golden output before and after; the algorithm defines the answer | S-P | 1.0 d | `utf_baselib_security` | 4 |
| **V-7 · Python** | `.venv/bin/pytest scripts/tests/` | S-Q · S-H(L-4) | 0.6 d | — | 0 |
| **V-8 · Not verifiable here** | Needs a Windows devenv7 machine | R-20's Windows items (F-14 gates, `OSImplWindows.h`, real `cl.exe`) | — | — | blocked |

### What this cross-cut shows

- **V-0 and V-1 are ~2.35 days of work for twelve focused build units** — the whole of L1, all
  documentation plus one assertion. Cheapest real progress on the list.
- **V-4 is the expensive one, and it is expensive for the right reason.** S-I and S-J are roughly an
  hour of editing apiece inside a multi-devenv rebuild campaign. Schedule them for the campaign, not
  the code, and **run them together** — one campaign covers both.
- **V-5's cost is the fixture**, not the dispatch.
- **V-3 is why S-L should precede S-N** — they share a verification run as well as a code path, so
  pairing them halves the json-spirit builds.
- **V-2 parallelises across people** — no two slices in it touch the same module, and the rules
  permit up to five modules under test concurrently.
- **V-8 is a hand-off**, not an open item; it cannot be scheduled from this checkout.

### Cross-tab — layer × verification

| | V-0 | V-1 | V-2 | V-3 | V-4 | V-5 | V-6 | V-7 |
|---|---|---|---|---|---|---|---|---|
| **L0** | | | | | | S-A | | |
| **L1** | S-B, S-E | S-C, S-D | | | | | | |
| **L2** | S-H(L-5) | | S-F, S-G, S-H | | | | | S-H(L-4) |
| **L3** | | | | | S-I, S-J | | | |
| **L4** | | | S-K | S-L | | | | |
| **L5** | | | S-O | S-M, S-N | | | | |
| **L6** | | | | | | | S-P | |
| **L7** | | | | | | | | S-Q |

# Bundle A — implementation status (2026-09-03)

Bundle A is implemented in the working tree, uncommitted. What follows records what was done, what
had to change from the plan, and what implementing it turned up.

## Delivered

| Slice | Items | Where |
|---|---|---|
| S-B | R-13 deferral record, R-18 decision record | `notes/plans/issues/privileged-script-error-handling-deferral.md`, `notes/plans/issues/json-backend-verification-decision.md` |
| S-C | R-3 (three sites), R-8, R-19, R-11 | `DataModelObjectDefs.h`, `BoostJsonImpl.h`, `JsonSpiritImpl.h`, `DataModelObject.h`, `ExecutionQueueNotify.h`, `JsonUtils.h` |
| S-D | R-17 | `TcpBlockTransferCommon.h` |
| S-E | R-20 release notes | `notes/plans/issues/devenv7-breaking-changes-release-notes.md` |
| S-F | R-2 | `DataModelObjectDefs.h` |
| S-G | OPENSSL_init_ssl return check | `CryptoBase.h` |
| S-H | L-2, L-9, E-5, L-5, L-4 | `BoostJsonImpl.h`, `Pinger.h`, `ExecutionQueueImpl.h`, `AGENTS.md` + 2 plan docs, `pytest.ini` |
| S-K | R-4 | `JsonUtils.h`, both impl headers |
| S-L | R-5 | `BoostJsonImpl.h`, `JsonUtils.h` |
| S-M | R-6 | `TestJsonAbstraction.h` |
| S-N | R-15 | `TestJsonAbstraction.h` |
| S-O | R-7 | `TestTasks.h` |
| S-P | R-14 | `HmacSha256.h`, new `TestHmacSha256.h` |
| S-Q | R-12 | `s3_manage.py`, `test_s3_manage_unit.py` |

## Verification performed

Per `AGENTS.md:64-77` — focused builds of the affected modules only, `-j1`, both toolchains, both
variants. No full-repo build was run.

| Check | Result |
|---|---|
| Build matrix: 5 modules x {clang2010, gcc1520} x {debug, release} | **20/20 ok** |
| Test matrix: the same 20 units | **20/20 ok, 0 failures** |
| Case counts per module | identical across all four configurations - `utf_baselib` 146, `utf_baselib_data` 79, `utf_baselib_tasks` 50, `utf_baselib_messaging` 27, `utf_baselib_security` 25 |
| Dual-backend: `make -k -j1 utf_baselib_data VARIANT=release BL_USE_JSON_SPIRIT=1` | ok, **75 cases** - 79 minus the four guarded `#if !defined( BL_USE_JSON_SPIRIT )` |
| `.venv/bin/pytest scripts/tests/` | **572 passed, 2 skipped** (was 570 + 2) |
| `git diff --check <merge-base> -- src` | clean |
| R-5 negative control (`MAX_PARSE_DEPTH` 32, rebuild) | test fails as expected; restored to 512 and passes |
| R-17 empirical size check (standalone TU, Boost 1.90) | 72 bytes with the macro, 80 without |

Identical case counts across configurations matter here: they show no test was silently compiled out
under a different toolchain or variant.

**Note on the json-spirit run.** It writes objects built with a different preprocessor state into
`bld/ub24-a64-clang2010-release`, and the dependency tracking keys on sources rather than flags, so
that tree was deleted before the normal matrix ran. Anyone repeating the dual-backend check must do
the same - this is the "must be validated on a clean tree, never incrementally" point from
`medium-severity-findings-f11-f17-plan.md` (F-16).

## Findings from implementing it

**R-17 — the invariant holds, and both sizes are now measured rather than reasoned.** A standalone
translation unit against Boost 1.90 confirms `sizeof( CommandBlock )` is **72** with
`BOOST_UUID_DISABLE_ALIGNMENT` and **80** without, with `DataHeader` at 28 and 32 respectively. The
`static_assert` passes, so the hazard is latent rather than already real in this build.

**R-5 — the depth test is a real regression test, proven by negative control.** Setting
`MAX_PARSE_DEPTH` to 32 and rebuilding makes `JsonParseDepthWithinLimitIsAccepted` fail with
Boost.JSON's `too deep`; restoring 512 makes it pass. Without that control the test would have been
indistinguishable from one that passes against unconfigured code.

**R-14 — `HmacSha256` has no in-repo consumer and had no tests.** A repository-wide search for
`HmacSha256` and `calculateMessageDigest` returns nothing outside the header itself. The EVP_MAC
migration would therefore have been entirely unverified, so it ships with RFC 4231 known-answer
tests (cases 1, 2, 3 and 6, plus empty key/message and a length check). Those vectors pass, which
verifies the migration against the published algorithm rather than against the previous
implementation.

**R-12 — `nosniff` cannot be delivered from S3, and the plan was wrong to imply it could.** S3 sets
only well-known response headers plus `x-amz-meta-*`; there is no way to set
`X-Content-Type-Options` on an object. The CSP is therefore delivered as a `<meta http-equiv>`
element inside the generated HTML, which works for CSP. `nosniff` is documented as requiring a
serving layer that can set response headers (CloudFront response headers policy or equivalent) and
is called out in the code. With an explicit correct `Content-Type` set, sniffing is a much narrower
concern than with none.

**L-4 was a latent portability issue, not a live bug.** pytest starts fine both before and after, so
the installed iniconfig does strip `#` lines indented inside a linelist value. The comments were
moved out of the value anyway, since the behaviour is version dependent, and a comment now says so.

**S-C carries one item the reviews never found.** The `canonicalize` flag makes the
required-property check unreachable (`DataModelObjectDefs.h`), so canonical serialization accepts an
object packed serialization would reject. That is now documented at all three sites.

## Deviation from plan

- **R-3 is documentation only**, per the decision of 2026-09-03. No renames, no parameter split, and
  the `prettyPrint` / `canonicalize` mutual exclusion is retained and explained.
- **R-13 and R-18 are records rather than fixes**, per the same decision.
- **R-4's json-spirit path gained the exclusion check** so both backends reject the same call, which
  the plan did not call out but which follows from R-3 keeping the exclusion.

## Not in Bundle A

R-1 (Bundle B), R-9 and R-10 (Bundle C), R-20's Windows verification (Bundle D). None started.

---

# Bundle B — implementation status (2026-09-03)

Implemented in the working tree, uncommitted.

## Delivered

| Item | Where |
|---|---|
| The verifier | **new** `src/include/baselib/crypto/TlsPeerVerification.h` |
| Wired into the handshake | `src/include/baselib/tasks/AsioSslStreamWrapper.h` (`verifyCertificate`) |
| Test certificate | **new** `certs/test-server-ip-cert.pem`, `certs/test-server-ip-key.pem`, `certs/ip-openssl.conf` |
| Embedded fixture | `src/utests/include/utests/baselib/UtfCrypto.h` (`getIpAddressServerCertificate`) |
| Tests | **new** `src/utests/utf_baselib_http/TestTlsPeerVerification.h`, wired into `UtfBaselibHttpMain.cpp` |
| Release notes | `devenv7-breaking-changes-release-notes.md` entry 5 rewritten as 5a / 5b |

## Verification performed

Per `AGENTS.md:64-77` - focused builds of the affected modules, `-j1`, both toolchains, both
variants. No full-repo build.

`AsioSslStreamWrapper.h` is reached by every test module through `UtfMain.h`, so the compile surface
is wide, but only `utf_baselib_http`, `utf_baselib_rest` and `utf_baselib_messaging` exercise TLS.
Those three plus `utf_baselib` were run across the matrix.

| Check | Result |
|---|---|
| 4 modules x {clang2010, gcc1520} x {debug, release} | **16/16 ok** |
| Case counts, identical across all four configurations | `utf_baselib` 146, `utf_baselib_messaging` 27, `utf_baselib_http` 21 (was 14), `utf_baselib_rest` 5 |
| New `TlsPeerVerification_*` cases | 7, all passing |
| `git diff --check` on `src` and `certs` | clean |
| Dispatch semantics probed against the real certificate before coding | see the finding below |

**One unit initially failed on `No space left on device`**, not on anything in the change - the host
was at 99% disk with 409 MB free and `bld/` at 4.2 GB. The fully verified
`bld/ub24-a64-clang2010-debug` tree was removed to make room and the unit was rebuilt and rerun
clean. Worth knowing when repeating this: a full four-config matrix of these modules needs several
GB of build output.

## Design decisions worth recording

**The IP literal test is `::X509_check_ip_asc()` itself.** It parses the name before comparing
anything and returns **-2** when the string is not an address, which makes OpenSSL's own parser the
arbiter. That avoids a separate literal predicate and, more importantly, avoids depending on a
particular Boost address parser across the devenv2-7 range. `a2i_ipadd()` was considered and
rejected - it is not declared in the public headers of OpenSSL 3.5.4.

**When the peer name is a literal the IP result is FINAL.** It must not fall through to
`::X509_check_host()`. This is the security-relevant part of the change and it is verified
empirically, not merely reasoned - see the finding below.

**The bound `rfc2818` parameter is retained in `verifyCertificate`'s signature** and marked
`BL_UNUSED`, rather than removed. Removing it would change the signature of a method reachable by
anyone who has overridden the verify callback, for no benefit.

**The shim in `AsioSslCompat.h` stays a pure Boost typedef.** The dispatch is project-owned code in
`baselib/crypto/`, not an extension of a third-party namespace.

## Findings from implementing it

**The typedef broke a second thing nobody had noticed: multi-label wildcards.**
`::X509_check_host()` permits a wildcard only as the complete leftmost label (RFC 6125 6.4.3); Asio's
own matcher was looser. So `*.*.example.com` certificates now match **nothing** - and this is not
hypothetical, the repository's own `certs/test-server-cert.pem` carries `DNS:*.*.mycompany.com`,
which is now dead. Only its `localhost` SAN still works.

This was found because a test I wrote asserting the *old* behaviour failed. It is **deliberately not
fixed**: restoring multi-label wildcards would loosen verification below RFC 6125, which is the
opposite of what the switch bought. Affected deployments need a reissued certificate. Recorded as
release-note item 5b and asserted by `TlsPeerVerification_MultiLabelWildcardsDoNotMatch`.

**The no-fall-through rule was verified against a real certificate before the code was written.** A
standalone probe against `certs/test-server-ip-cert.pem` shows, for the peer name `10.11.12.13`:

```
check_ip = 0    valid literal, no matching iPAddress SAN
check_host = 1  the DNS:10.11.12.13 entry matches the text
dispatch = no   refused, per RFC 6125 6.4
```

So a fall-through implementation would let a certificate carrying `DNS:10.11.12.13` authenticate the
host at 10.11.12.13. One certificate covers every branch of the match, which is why the SAN set is
what it is.

## Note on the fixture

`certs/test-server-ip-key.pem` is kept alongside the certificate even though the tests use only the
certificate, matching how every other pair in `certs/` is stored and leaving the fixture usable if a
real handshake test is ever wanted. `certs/ip-openssl.conf` documents how it was generated.

---

# Bundle C — implementation status (2026-09-03)

Implemented in the working tree, uncommitted.

## Delivered

| Slice | Item | Where |
|---|---|---|
| S-I | R-9 - the `os::copy` directory shim keys on Boost version, not devenv version | `src/include/baselib/core/detail/OSImplPlatformCommon.h` |
| S-J | R-10 - make flag semantics honour the VALUE, and reject unrecognized ones | `projects/make/3rd/boost/common.mk`, `projects/make/3rd/json-spirit/4.08.mk` |

## S-I — what changed and why it is safe

The two gates were

```
#if ( defined( BL_DEVENV_VERSION ) && BL_DEVENV_VERSION > 5 ) || ( ( BOOST_VERSION / 100 ) >= 1084 )
```

and are now a single named condition, `BL_FS_COPY_DIRECTORY_SHIM_REQUIRED`, keyed on `BOOST_VERSION`
alone. The devenv disjunct was wrong because the behaviour that changed is Boost's, not the
environment's, and `BOOSTDIR` is overridable - so a devenv6 or devenv7 build pinned to an older
Boost took the new path while `fs::copy()` still had its old semantics.

**It is a no-op for every default configuration**: devenv6 pins Boost 1.84.0 and devenv7 pins
1.90.0 (`projects/make/devenv-detect.mk`), so both conditions agree everywhere except the override
case the change exists to fix.

**A tripwire was added with it.** The gate now depends solely on `BOOST_VERSION`, and a
`BOOST_VERSION` which is undefined at that point expands to 0, silently turning the shim off - which
is exactly the failure mode F-16 was filed about in `UuidBoostImports.h`. A `#error` guard makes that
a build failure instead. Before writing the change I verified empirically, with a temporary `#error`
probe and a real compile, that `BOOST_VERSION` is in fact defined and non-zero there (it arrives via
`OSBoostImports.h` at the top of the file).

## S-J — what changed

`ifdef` / `ifndef` test definedness, not truth, so the value which plainly means "no" selected the
opposite of what was asked:

| Invocation | Before | After |
|---|---|---|
| `make BL_USE_JSON_SPIRIT=0` | selected **json-spirit** | selects **Boost.JSON** |
| `make NO_BOOST_LOCALE_LIB=0` | **disabled** boost_locale and defined `-DBL_NO_BOOST_LOCALE_LIB` | **links** boost_locale |
| `make BL_USE_JSON_SPIRIT=yes` | silently selected json-spirit | **hard error** naming the bad value |
| unset, devenv7 | Boost.JSON | unchanged |
| unset, devenv2-6 | auto json-spirit | unchanged |
| `=1` | json-spirit | unchanged |

Unset is deliberately NOT the same as `0` for `BL_USE_JSON_SPIRIT`: unset means "choose from the
devenv version", `0` means "Boost.JSON even on a legacy devenv". Asking for `0` on a devenv which
ships no `boost_json` is allowed and fails loudly at link; that is the caller's explicit request and
is left to fail rather than being second-guessed, which is noted in the makefile.

The two include guards at the top of each file remain `ifndef` - those test definedness correctly.

## Verification performed

The interesting half of this bundle is provable without building anything, which is fortunate
because a full build matrix is what it would otherwise cost.

**S-J is a proven no-op for every default configuration.** Following the method F-14 used, the
resolved `LDLIBS`, `CPPFLAGS`, `BOOSTDIR`, `BL_USE_JSON_SPIRIT` and `BL_DEVENV_IS_LEGACY` were
dumped with `make -p -n` across **devenv7, devenv6 (`TOOLCHAIN=clang1500`), devenv5
(`TOOLCHAIN=gcc1110`) and devenv2 (`TOOLCHAIN=gcc492`), each in debug and release** - eight
configurations - before and after the change. The two dumps are **byte identical**.

**The behaviour changes were confirmed by direct comparison against `HEAD`**, not merely asserted:

| Case | HEAD | with the change |
|---|---|---|
| `BL_USE_JSON_SPIRIT=0` | `-DBL_USE_JSON_SPIRIT` set, no `boost_json` | no define, `boost_json` linked |
| `NO_BOOST_LOCALE_LIB=0` | no `boost_locale` | `boost_locale` linked |
| default | `boost_json` + `boost_locale` | identical |

Invalid values were confirmed to stop the build with the intended message, and devenv6 was confirmed
to still auto-select json-spirit and still print its `$(info)` line.

**Builds and tests, 13/13 ok.** Three modules - `utf_baselib` (which carries the ten `FsUtils_*`
cases, the coverage that matters for S-I), `utf_baselib_io` and `utf_baselib_data` - across
clang2010 and gcc1520, debug and release, plus one `BL_USE_JSON_SPIRIT=1` build to confirm the new
`ifeq` form still selects json-spirit through the changed makefiles.

| Config | utf_baselib | utf_baselib_io | utf_baselib_data |
|---|---|---|---|
| clang2010 debug | 146 | 20 | 79 |
| clang2010 release | 146 | 20 | 79 |
| gcc1520 debug | 146 | 20 | 79 |
| gcc1520 release | 146 | 20 | 79 |
| gcc1520 release, `BL_USE_JSON_SPIRIT=1` | - | - | 75 |

Case counts are identical across every configuration, and the json-spirit run shows 75 rather than
79 - the four cases guarded `#if !defined( BL_USE_JSON_SPIRIT )` - which is the same split Bundle A
produced, so the flag is still being honoured end to end.

The `BL_USE_JSON_SPIRIT=0` path was deliberately NOT given its own build: the variable comparison
above shows it resolves byte identically to the default configuration, so a build of it would
exercise the same compilation as the default rows and prove nothing further.

**Note on disk.** The matrix clears each toolchain's build trees before moving to the next rather
than holding four configurations at once; the host was at 95% with 1.9 GB free when it started. A
switch of JSON backend additionally requires a clean tree, since dependency tracking keys on sources
rather than on flags.

# Execution bundles

Bundled by **verifiability**, not by subsystem or urgency. Each bundle is a unit of work that can be
proven with one kind of environment.

## Bundle A — verifiable here — ~7.3 d

Everything except V-4, V-5 and V-8. All of it can be built, run and proven on this machine with the
toolchains and dist root already present.

| Verification | Slices | Items |
|---|---|---|
| V-0 · review only | S-B, S-E, S-H(L-5) | R-13 deferral record · R-18 decision record · release notes · `AGENTS.md` + 2 plan docs path/target fix |
| V-1 · focused compile-proof | S-C, S-D | R-3 (canonicalize decision, 3 sites) · R-8 · R-19 · R-11 · R-17 `static_assert` |
| V-2 · single-module test | S-F, S-G, S-H, S-K, S-O | R-2 `std::move` · `OPENSSL_init_ssl` check · L-2 · L-9 · E-5 · R-4 `saveToStream` param · R-7 cancel/flush test |
| V-3 · dual-backend | S-L, S-M, S-N | R-5 `max_depth = 512` · R-6 helper strength · R-15 J-3/J-9 |
| V-6 · byte-comparison | S-P | R-14 `HmacSha256` → `EVP_MAC` |
| V-7 · python | S-Q, S-H(L-4) | R-12 S3 index rendering · L-4 `pytest.ini` |

## Bundle B — needs a new test fixture — ~1.5 d

| Verification | Slice | Item |
|---|---|---|
| V-5 | S-A | R-1 — IP-aware TLS peer verification |

Separated because it cannot be proven without an **IP-SAN certificate that does not exist yet**.
Producing and wiring that fixture is most of the cost.

> **Sequencing note.** This bundle holds the only *live production regression* in the set — every
> TLS handshake to an IP-addressed peer is failing verification on current devenv7 builds. Bundling
> by verifiability places it second. If urgency should win over verification convenience, B goes
> first; the two bundles are independent and can be swapped freely.

## Bundle C — needs a multi-devenv rebuild campaign — ~1.0 d

| Verification | Slices | Items |
|---|---|---|
| V-4 | S-I, S-J | R-9 `os::copy` guard · R-10 make flag semantics |

Separated because both are conditional-compilation correctness whose failure mode is "wrong branch
taken silently" — unprovable in a single configuration. Both need resolution across `clang1500`
(devenv6), `gcc1110` (devenv5) and `gcc492` (devenv2) alongside devenv7. **Run the two slices in one
campaign**; the campaign is most of the cost.

## Bundle D — not implementable here — blocked

| Verification | Item |
|---|---|
| V-8 | R-20's Windows verification — F-14's flipped gates, `OSImplWindows.h`'s `<cstring>` addition, and the real `cl.exe` half of F-15 |

**Do not attempt this bundle from this checkout.** It requires a Windows devenv7 machine, and the
whole point of the item is that the change must be *verified* there rather than assumed. It stays in
the plan as a tracked hand-off, not as work to be scheduled here.

| Bundle | Days | Environment | Status |
|---|---:|---|---|
| **A** | ~7.3 | this machine | ready |
| **B** | ~1.5 | this machine + a new IP-SAN fixture | ready |
| **C** | ~1.0 | multi-devenv toolchains | ready |
| **D** | — | Windows devenv7 | **blocked — leave alone** |

---

---

## Totals

| Layer | Name | Days | Parallel-safe | Depends on |
|---|---|---:|---|---|
| L0 | Stop the bleeding | 1.5 | — | — |
| L1 | Record and pin | 2.1 | yes | — |
| L2 | Micro-corrections | 0.85 | **yes** | — |
| L3 | Build conditionals | 1.0 | **yes** | — |
| L4 | JSON public surface | 1.0 | yes | — |
| L5 | Test integrity | 1.75 | yes | S-N prefers after S-L |
| L6 | Scoped crypto modernization | 1.0 | **yes** | — |
| L7 | Tooling | 0.5 | **yes** | — |
| | **Total** | **~9.7 d** | | |

**Three of the twenty residuals are now documents rather than code changes** — R-3, R-13 and R-18,
all by your decision.

---

# Verification

**Rules that govern every command below** (`AGENTS.md:64-77`): no full-repo build; focused builds of
the affected test modules only; `-j1` whenever more than one module is built; tests may run up to
five modules concurrently; cover `gcc1520` **and** `clang2010`, `debug` **and** `release`.
`make clean` does not work here — use `rm -rf ./bld`.

### The per-module matrix

Each slice runs its own modules through this. Four build units per module:

```bash
make -k -j1 <module> TOOLCHAIN=clang2010
make -k -j1 <module> TOOLCHAIN=clang2010 VARIANT=release
make -k -j1 <module> TOOLCHAIN=gcc1520
make -k -j1 <module> TOOLCHAIN=gcc1520   VARIANT=release
```

`clang2010` is the devenv7 default, so the `TOOLCHAIN=` argument may be omitted on the first two —
it is written out here so the matrix is explicit.

### Per slice

| Slice | Modules | Verification |
|---|---|---|
| **S-A** | `utf_baselib_http` | New IP-SAN certificate under `certs/`, then **both directions**: an IP-addressed peer whose cert carries the IP in a SAN must verify; one that does not must still fail. No existing test exercises IP-addressed TLS, so this is new coverage, not a regression check |
| **S-C** | `utf_baselib_data`, `utf_baselib_tasks`, `utf_baselib_messaging` | Comment-only in public headers. Build the modules that include them to prove nothing broke — do **not** reach for a repo build |
| **S-D** | `utf_baselib_messaging` | The `static_assert` fires at compile time in the module that includes `TcpBlockTransferCommon.h`. No clean rebuild needed; no global `-D` is being added |
| **S-F** | `utf_baselib_data` | Release build, then `--run_test="*Performance*" -- --is-client`, compared against the Stage 4 numbers in `json-data-model-performance-improvements-plan.md` |
| **S-G** | `utf_baselib_security` | Full matrix |
| **S-H** | `utf_baselib` (L-9), `utf_baselib_data` (L-2) | E-5 and L-5 are text only — no build |
| **S-I, S-J** | `utf_baselib` | The multi-config campaign. Beyond the four devenv7 units, resolve across devenv tags — `TOOLCHAIN=clang1500` (devenv6), `gcc1110` (devenv5), `gcc492` (devenv2) — comparing `make -p` variable resolution, as F-14 did for its no-op proof. **Run both slices in one campaign** |
| **S-K, S-L** | `utf_baselib_data` | Full matrix. S-L adds a depth test: 500 levels accepted, 600 rejected, Boost.JSON only |
| **S-M, S-N** | `utf_baselib_data` | Full matrix **plus** the json-spirit run recorded by S-B: `make -k -j1 utf_baselib_data VARIANT=release BL_USE_JSON_SPIRIT=1`. **Release, not debug** — json-spirit debug is 140x slower on medium documents and will look hung. Prerequisites are present on this machine (`~/swblocks/dist-devenv7-ub24-gcc1520-clang2010-a64/json-spirit/4.08/source`). Cite the command in the commit message |
| **S-O** | `utf_baselib_tasks` | Full matrix |
| **S-P** | `utf_baselib_security` | Byte-compare HMAC-SHA256 output against the current implementation before and after; write the comparison **first** |
| **S-Q**, **S-H(L-4)** | — | `make pytest-install` (**not** `python-install` — that is S-H's L-5 fix), then `.venv/bin/pytest scripts/tests/`. `.venv` does not exist here. L-4 is settled by the first run |

### Merge gate per layer

- Every module the layer touched, green across all four units.
- Test runs may be batched — **up to five modules concurrently** is permitted, and L1 and L2 each
  touch fewer than five.
- `git diff --check <merge-base> -- src` clean — the rescoped gate from F-18 item 5, not the
  repo-wide form.
- **A full-repo build is not part of any gate here.** If one is wanted before merging the whole set,
  that is an explicit request and per `AGENTS.md` it is `make -k -j1`, not `-j4`.
