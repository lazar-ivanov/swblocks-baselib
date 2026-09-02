# Closing the residual CXX findings from the deep-dive review

## Context

`notes/plans/issues/pr-review-deep-dive-residual-findings.md` records fourteen findings from the
`master...lazari2` deep-dive review and their status after 193 commits. Seven are closed. This plan
disposes of the remaining CXX findings (F-07 excluded, already closed) so the branch can be merged
with every finding either fixed or carrying a written, defensible risk acceptance.

The work splits into four workstreams that touch disjoint file sets and can land independently:
**A — JSON layer** (CXX-01, CXX-02, CXX-08), **B — crypto/TLS** (CXX-06), **C — two cheap standalone
fixes** (CXX-09 decay, CXX-12), **D — documentation-only closes** (CXX-10, CXX-11, and the residual
halves of CXX-02 and CXX-09).

Two things discovered during this analysis are not in the review and change the shape of the work:

- **Certificate verification is effectively disabled by default.** `CryptoBase::allowUntrustedCertificates()`
  (`CryptoBase.h:575-586`) hard-returns `true` and `AsioSslStreamWrapper.h:239` ends with
  `return ( ok || allowUntrustedCertificates() )`. Expired, untrusted and hostname-mismatched
  certificates are accepted, recorded in a global map and logged as a warning. Nothing in the repo
  consumes `hasUntrustedEndpoints()` / `getUntrustedEndpointsInfo()`, so the "the caller will prompt
  the user" contract in the comment is unimplemented. As a default-security matter this outweighs
  the TLS 1.1 floor; it is in scope for this cycle.
- **CXX-12 is far cheaper than filed.** The residual doc estimates 3-4 days for "touching every
  `std::basic_string<unsigned char>` use site". There are exactly **two**, both local typedefs, and
  `SerializationUtils.h:318` already provides `base64UrlDecodeVector`.

## Decisions taken

| ID | Decision | Effort |
|---|---|---|
| CXX-01 | Document the duplicate-key contract as backend-defined; no behaviour change. The comment must tell consumers who need the rejecting behaviour that the json-spirit backend is still supported (`BL_USE_JSON_SPIRIT=1`). | ~2h |
| CXX-02 | Closed as an aspirational deferral. Add code comments so it is not re-flagged. Fix the nested `canonicalize` forwarding, which is a separate correctness bug inside the canonical path. | ~0.5d |
| CXX-06 | Fix in full: TLS 1.2 floor with named legacy opt-in, modern cipher policy, fail-closed cipher check, `SSL_CTX_check_private_key`, key validation on import, explicit private-key protection policy, **and** fail-closed certificate trust for the client role. Plus EVP phase 0. | ~5-6d |
| CXX-08 | `rawUtf8`: keep every signature, document it, and align json-spirit to raw UTF-8. Numeric policy: add the existing guards to json-spirit's `value_to`. | ~1.5d |
| CXX-09 | Apply the `std::decay_t` fix. Record the completion-token / associated-executor half as a deferral (it is **not** covered by the existing `public-header-hygiene-deferral.md`, which only covers `BOOST_ASIO_DISABLE_STD_CHRONO`). | ~1h + doc |
| CXX-10 | Aspirational deferral + code comment. | ~1h |
| CXX-11 | Code comment only. No release note. | ~0.5h |
| CXX-12 | **Fix**, do not defer. Two typedefs to `std::vector<unsigned char>`, then delete the 100-line `std::char_traits` specialization. | ~0.5d |
| EVP migration | Phase 0 now (`RsaKey::evpKey()`, no new `RSA_*` sites); full migration deferred with "retire OpenSSL 1.x" named as its gating precondition. | ~1d + doc |

## Dependencies between sub-plans

- **A2 → A3** both edit `JsonSpiritImpl.h` (different functions; sequence them, do not merge).
- **A1/A5 → A2** all add comments to `BoostJsonImpl.h` (different lines; sequence them).
- **B7 (`evpKey()`) → B8/B9**: the key-validation checks should consume `evpKey()` rather than
  building a throwaway `EVP_PKEY` around `&rsaKey -> get()`. Land B7 first.
- **B1 → B2**: the negative TLS test needs `TlsMinimumVersion`.
- **B6 (cert trust)** is independent of B1-B5 but shares `CryptoBase.h` with them; sequence.
- **C1, C2** are independent of everything.
- **D** depends on nothing; write the records as the corresponding code lands.

Nothing in A touches anything in B or C. `CryptoBase.h` is the only file three sub-plans share.

---

## Workstream A — JSON layer

### A1. CXX-01 — document the duplicate-key contract (doc only, ~2h)

**Design intent.** RFC 8259 says object names *SHOULD* be unique and leaves duplicate handling
implementation-defined. Boost.JSON's parser reference states: *"if multiple elements in an object
have keys that compare equal, only the last equivalent element will be inserted."* That matches
`JSON.parse`, Python, Go, and RFC 7515 §4 / RFC 7519 §4, which explicitly permit last-value-wins for
JOSE and JWT. `boost::json::parse_options` has no duplicate-key knob and the standing I-JSON request
([boostorg/json#127](https://github.com/boostorg/json/issues/127)) is unresolved; detection would
require a per-object key set on every parse, against the library's stated performance goal.

**Change.** No code. Comments at:

- `src/include/baselib/core/JsonUtils.h`, next to `readFromString` (`:65`) — the contract statement,
  naming both behaviours, stating that duplicate-key input is not a supported document shape, and
  stating that consumers who require the rejecting behaviour can build with `BL_USE_JSON_SPIRIT=1`,
  which remains supported (`CONTRIBUTING.md:70-73`, `projects/make/3rd/boost/common.mk:66-85`).
- `src/include/baselib/core/detail/BoostJsonImpl.h:183` (`boost::json::parse`) and `:216`
  (`stream_parser`) — one line each pointing at the contract comment.
- `src/include/baselib/core/detail/JsonSpiritImpl.h:337-356` — note that this rejection is
  backend-specific and is not part of the library contract. Also record that it throws
  `UserMessageException` via `BL_THROW_USER`, not `JsonException`.

**Written record:** `notes/plans/issues/json-duplicate-key-contract.md`, in the shape of
`execution-queue-notification-delivery-breaking-change.md`.

- **Risk: none** (no code change). **Blast radius: none.** **Perf: none.**
- Caveat to state honestly in the record: json-spirit has no CI coverage — no build or test target
  anywhere sets `BL_USE_JSON_SPIRIT`, so "still supported" means buildable on demand and documented,
  not continuously verified.

### A2. CXX-08 `rawUtf8` — document and align json-spirit (~0.5d)

**Why not implement it on Boost.** `json_spirit_writer_template.h` `add_esc_chars` escapes **per
byte** when `raw_utf8` is false: `unsigned_c = (c >= 0) ? c : 256 + c`, then `iswprint(unsigned_c)`
decides. UTF-8 "é" (`0xC3 0xA9`) becomes `Ã©`, which any conformant parser reads back as
"Ã©". It is mojibake, it is locale-dependent, and it does not round-trip across parsers. Boost.JSON's
always-raw-UTF-8 output is correct per RFC 8259 §8.1. Emulating the legacy behaviour would also put
an escaping pass on the **default** path — `rawUtf8` defaults to `false` and all ~25 production
serialization sites take that default.

**Change.**

- `src/include/baselib/core/detail/JsonSpiritImpl.h:697-701` and `:735-739`: pass
  `json_spirit::raw_utf8` unconditionally, so both backends emit correct raw UTF-8.
- `src/include/baselib/core/JsonUtils.h` (`saveToString` `:83-91`, `saveToStream` `:97-105`) and
  `src/include/baselib/data/DataModelObject.h` (`getDocAsPrettyJsonString` `:255`,
  `getDocAsPackedJsonString` `:269`): document `rawUtf8` as retained for source compatibility and
  without effect — output is always raw UTF-8.
- `src/include/baselib/core/detail/BoostJsonImpl.h:455`: replace the bare `BL_UNUSED( rawUtf8 )` with
  the same statement.

**No signature changes anywhere.**

- **Complexity: low.** **Cost: ~0.5d** including exact-byte tests for a non-ASCII payload on both
  backends.
- **Risk: low, and confined to legacy builds.** devenv7 output does not change at all. devenv2-6
  output changes for non-ASCII strings — from broken escaping to correct UTF-8. A devenv7 reader of
  data already persisted by a devenv2-6 writer still gets mojibake for those strings; that is
  pre-existing and must be stated in the comment, not fixed here.
- **Perf: none** on the default backend; marginally faster on json-spirit (no escaping loop).

### A3. CXX-08 numeric policy — guard json-spirit's `value_to` (~1d)

**The real divergence.** The data model uses `value_to<T>` (`DataModelObjectDefs.h:243`), not the
guarded `bl::json::get_uint64`. On Boost that path is range-checked by Boost itself (negative →
unsigned and out-of-range both yield `error::not_exact`, surfaced as `JsonException` through
`remapIncorrectValueTypeException`). On json-spirit, `value_to<uint64_t>` is `v.as_uint64()`, which
turns `-1` into `18446744073709551615` silently, and `value_to<int>` silently narrows. **The Boost
backend is already the safe one.**

**Change.** `src/include/baselib/core/detail/JsonSpiritImpl.h:415-431`: give the `value_to<T>`
specializations the same signedness and range checks the file's own `get_uint64` (`:469-494`) already
performs, throwing `JsonException`. Also reconcile `get_int` (unchecked `static_cast<int>` in *both*
adapters, `BoostJsonImpl.h:95-98` / `JsonSpiritImpl.h:459-462`) and `get_real` (Boost throws on an
int-typed JSON number; json-spirit converts) — pick Boost's range checking and json-spirit's
int→double widening as the single policy, since JSON has one number type.

- **Complexity: low-medium.** **Cost: ~1d** with boundary tests (negative→unsigned, `int64` overflow
  into `int`, `-0`, int-read-as-double) run against both backends.
- **Risk: low.** Tightens a legacy backend that currently corrupts values silently.
- **Perf: none on the default backend** — no Boost-side code changes.

### A4. CXX-02 — forward `canonicalize` through nested collections (~0.5d)

`DataModelObjectDefs.h:629` (`item -> serializeProperties( tempContext )`) and `:702`
(`pair.second -> serializeProperties( tempContext )`) drop the flag and fall back to the `= false`
default at `:824`, while the single-complex-property path forwards it correctly (`:530-556`). A child
inside a vector or map therefore omits unset properties while the same child hung directly off the
model emits them — the canonical hash of logically identical content differs by nesting shape. This
is a bug **inside** the canonical path, not part of the deferred canonical-format question.

**Change.** Two macro edits, using the existing
`BL_DM_SERIALIZATION_CONTEXT_IMPL_INVOKE_SERIALIZE( tempContext, canonicalize )` helper (`:41-43`)
exactly as the single-property path does.

- **Cost: ~0.5d** including a permuted nested-property hash case in `TestDataModelDefault.h` (the
  fixture at `:289-293` already populates `complexVector` and `complexMap`).
- **Risk: very low.** `getObjectHash`/`getObjectHashCanonical` have **no production caller** — only
  `TestDataModelDefault.h:311-317` and `TestAsyncRpcDataModel.h:135,154` — so no persisted hash can
  change. It does change canonical serialized output for nested models, which no production code
  requests.
- **Perf: negligible** (canonical serialization is not on any hot path).

### A5. CXX-02 — record the deferral in code (doc only, ~1h)

- `src/include/baselib/data/DataModelObject.h:282-309` (`getObjectHash`): state that non-canonical
  hashing follows Boost.JSON insertion order by design, that `getObjectHashCanonical` is the
  supported form, and that neither has a production caller. Fix the misleading local name
  `canonicalizedProperties`, which is used even when `canonicalize == false`.
- `src/include/baselib/core/detail/BoostJsonImpl.h:281-340` (`canonicalizeValue`): name it a
  **project-specific stable ordering** — recursive byte-wise UTF-8 key sort — and state explicitly
  that it is *not* RFC 8785 / JCS (no number or string normalization; JCS orders by UTF-16 code
  units, which differs for some Unicode keys).

Both comments should cite `notes/plans/issues/medium-severity-findings-f11-f17-plan.md` (F-11).

---

## Workstream B — crypto and TLS (CXX-06)

`initNativeSslContext` (`CryptoBase.h:191-240`) is the single policy chokepoint for both the
process-global client context (`:285`) and every server context (`:311-313`), so the protocol and
cipher work is genuinely small. Landing order below is deliberate: the highest-blast-radius change
first with its test immediately behind it, the source-breaking API change last so it can be reverted
alone.

### B1. TLS 1.2 protocol floor with a named legacy opt-in (~1d)

**Change.** In `src/include/baselib/crypto/CryptoBase.h`:

- New `enum class TlsMinimumVersion { Tls12, Tls11Legacy, Tls10Legacy }` at `crypto` namespace scope.
  `enum class` is established here (12 uses under `src/include`) and, as with
  `ExecutionQueueNotify::NotifyDelivery`, admits no implicit conversion from `bool`/`int`.
- Replace `g_isEnableTlsV10` (`:64`, `:453`, `:443-446`) with `g_tlsMinimumVersion`, defaulting to
  `Tls12`.
- In `initNativeSslContext` (`:202-220`): set `SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1` for the default,
  and additionally call `SSL_CTX_set_min_proto_version` under
  `#if OPENSSL_VERSION_NUMBER >= 0x10100000L`. Both mechanisms, because `SSL_OP_NO_TLSv1_1` exists
  since 1.0.1 and is the only lever available on the devenv2 / 1.0.2d pin, while
  `SSL_CTX_set_min_proto_version` is the authoritative modern API and is queryable, which makes the
  policy assertable in a test.
- Keep `isEnableTlsV10( bool )` (`:611-614`) as a documented deprecated forwarder:
  `true → Tls10Legacy`, `false → Tls12`. **All four existing call sites keep compiling and keep their
  behaviour**: `MessagingEchoServerApp.h:257`, `MessagingBrokerApp.h:273`,
  `MessagingHttpGatewayApp.h:358`, `UtfBaselibSecurityMain.cpp:18`.
- Keep `SSL_OP_ALL` unchanged. On 3.x it is only interop workarounds and the historically dangerous
  bits are no-ops; note in the comment that `SSL_OP_DONT_INSERT_EMPTY_FRAGMENTS` disables the BEAST
  countermeasure (irrelevant at a TLS 1.2 floor, relevant in the legacy modes) and that on OpenSSL
  < 3.0 `SSL_OP_ALL` also carried `SSL_OP_LEGACY_SERVER_CONNECT`.

- **Blast radius: the largest in this plan.** Any peer that only speaks TLS 1.0/1.1 stops connecting
  — messaging brokers/gateways, REST authorization servers, any HTTPS endpoint the client context
  reaches. Mitigated by the named opt-in; in-repo behaviour is unchanged. Needs a release note.
- **Perf: none.**
- Follow-up worth flagging but out of scope: the three sample apps weaken the global floor
  unconditionally, with a comment about authorization servers that "might not yet support TLS 1.1"
  — 2015-era reality. Moving them to `Tls11Legacy` or behind a command-line switch is a separate
  behavioural decision.

### B2. Negative TLS regression test (~0.5d)

New `src/utests/utf_baselib_http/TestTlsProtocolPolicy.h` plus one include line in
`UtfBaselibHttpMain.cpp`. No makefile change (`projects/make/common.mk:473` globs `*.cpp` per
project, headers arrive through the `Main.cpp` include list).

It must **not** live in `utf_baselib_security`: `UtfBaselibSecurityMain.cpp:18` sets
`isEnableTlsV10( true )` for that whole binary, so the default policy is not observable there.

**No new production API is required.** The server side is the policy chokepoint and is already
public — `createAsioSslServerContext` calls `initNativeSslContext` per invocation, so it picks up a
runtime policy change, unlike `g_sslContext` which is built once. The client side is constructed
locally in the test with `SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_2` (+ `SSL_OP_NO_TLSv1_3` under
`#ifdef`) and `SSL_VERIFY_NONE`. Drive the handshake over `::BIO_new_bio_pair` rather than a socket —
no port, no `MachineGlobalTestLock`, no timing flakiness, and the assertion isolates `SSL_CTX` policy
with nothing else in the path.

Three cases: TLS 1.1 refused under the default; **TLS 1.2 accepted against the same context** (this
is what proves the first assertion failed on protocol version and not on the fixture or the BIO
plumbing); TLS 1.1 accepted under `Tls11Legacy`. Wrap the policy change in `BL_SCOPE_EXIT` —
`g_tlsMinimumVersion` is process-global and this binary also runs
`Client_SimpleSecureHttpSslGetTests` and the whole `TestHttpServer.h` suite. **Do not assert on the
OpenSSL error reason code** — `SSL_R_UNSUPPORTED_PROTOCOL` / `VERSION_TOO_LOW` / `WRONG_SSL_VERSION`
are produced inconsistently across versions and sides. Assert only that the handshake did not
complete. Guard the legacy half against an OpenSSL built `no-tls1_1` or an `openssl.cnf` with
`MinProtocol = TLSv1.2`; the default-policy half must never be skipped.

### B3. Drop 3DES from the cipher policy (~2h)

`CryptoBase.h:232-236` becomes:

```
"ECDH+AESGCM:DH+AESGCM:ECDH+AES256:DH+AES256:ECDH+AES128:DH+AES:"
"!aNULL:!eNULL:!kRSA:!PSK:!SRP:!MD5:!RC4:!3DES:!DES:!EXPORT"
```

Removing `RSA+AESGCM:RSA+AES:RSA+3DES` is a no-op: `RSA` is an alias for `kRSA` and `!kRSA` is
already present, so those tokens already select nothing. `!eNULL` is added alongside `!aNULL` (they
exclude NULL encryption and anonymous authentication respectively). No cipher **names** are used,
only aliases that exist on 1.0.2 through 3.5. `CHACHA20` is deliberately **not** added — that alias
does not exist before 1.1.0 and OpenSSL silently ignores unknown tokens, which would make legacy and
modern builds differ for a reason invisible in the source.

- **Verified: byte-identical cipher list on OpenSSL 3.0.13** at default, `@SECLEVEL=0` and
  `@SECLEVEL=2` (35 entries each) — 3DES suites are not compiled in on 3.x. **This commit is a no-op
  on devenv7** and only removes `ECDHE-RSA-DES-CBC3-SHA` / `EDH-RSA-DES-CBC3-SHA` on devenv2-6.
- **Risk: low.** Only a peer with no AES support at all is affected. **Perf: none.**
- **Do not call `SSL_CTX_set_ciphersuites`.** It does not exist before 1.1.1, `set_cipher_list` does
  not touch TLS 1.3 suites so nothing is being accidentally disabled, and OpenSSL's default TLS 1.3
  set is already exactly the three AEAD suites. Put a one-sentence comment saying so, because the
  asymmetry reads like an oversight.

### B4. Fail closed when no usable cipher suite survives (~2h)

`SSL_CTX_set_cipher_list` is already wrapped in `BL_CHK_CRYPTO_API_NM`, but from OpenSSL 1.1.1 it
returns success as long as any TLS 1.3 ciphersuite is configured, **even when zero TLS 1.2 suites
survived**. Count suites usable below TLS 1.3 via `SSL_CTX_get_ciphers` + `sk_SSL_CIPHER_num`, and
`SSL_CIPHER_get_min_version` under `#if OPENSSL_VERSION_NUMBER >= 0x10101000L` (the guard is on
`TLS1_3_VERSION`, which only exists from 1.1.1; below that the loop degenerates to a plain non-empty
check, which is correct because there are no TLS 1.3 suites to exclude).

- **Risk: none on any devenv build** — it can only fire on a stripped OpenSSL (`no-ec`, `no-dh`),
  where it converts a handshake-time failure into a loud init-time one. **Perf: once per context.**

### B5. Verify the server key matches its certificate (~1h)

`SSL_CTX_check_private_key( context -> native_handle() )` at `CryptoBase.h:357-359`, **after**
`use_certificate_chain` (the API compares against the currently loaded certificate). Returns 1/0,
which is exactly the `BL_CHK_CRYPTO_API` contract, and pushes `X509_R_KEY_VALUES_MISMATCH` on
failure. Available on every supported version.

- **Risk: none.** Only fires on a deployment already running a mismatched pair, which cannot have
  been serving successfully. **Perf: once per server context.**

### B6. Fail closed on certificate verification, client role only (~1-2d)

`allowUntrustedCertificates()` becomes a policy read from a static defaulting to **false**, with a
`CryptoBase::allowUntrustedCertificates( bool )` setter following the `g_tlsMinimumVersion` pattern.
The getter keeps its signature, so this is a behaviour change, not a source break.

**Principal design hazard, and the reason this is scoped to the client role.**
`AsioSslStreamWrapper.h:418` sets `verify_peer` **unconditionally, including in server mode**, where
it means "request a client certificate". Today that is harmless because the verify callback always
returns true. Fail-closed verification applied to the server role would start rejecting clients that
present a certificate the server cannot chain — and the role is already known at that point
(`m_isServer`, used for handshake role selection at `:479`). **Set `verify_none` for the server role
and fail-closed `verify_peer` for the client role**, and say so explicitly in the commit message.

Trust anchors come solely from `TrustedRoots.h` + `registerTrustedRoot`; `set_default_verify_paths`
is deliberately not called (TODO at `CryptoBase.h:278-283`), so a fail-closed default depends
entirely on the bundled root set. The suites register `certs/test-root-ca.pem` in `UtfMain.h:212-217`
and already carry a `--no-rfc2818-verify` switch.

- **Blast radius: high, and the largest correctness risk in this plan.** Every TLS client in the repo
  and downstream. Must be validated by running `utf_baselib_http`, `utf_baselib_io`,
  `utf_baselib_messaging`, `utf_baselib_blobtransfer` and `utf_baselib_rest` **before** deciding
  whether the fixtures need SAN entries added or whether specific suites opt in.
- **Perf: none.** **Needs a release note** — this is the most user-visible change on the branch.

### B7. EVP phase 0 — `RsaKey::evpKey()` (~1d)

Additive only. `RsaKey.h` gains `evpKey() const -> crypto::evppkey_ptr_t` built via `EVP_PKEY_new` +
`EVP_PKEY_set1_RSA`; `get()` (`:142`) and `releaseRsa()` (`:152`) keep their signatures and gain a
comment marking them legacy. A rule goes in the deferral record: **no new `RSA_*` call sites**.

This exists so B8/B9 consume an `EVP_PKEY` handle rather than building throwaway wrappers, and so
the eventual full migration has a landing point.

- **Risk: none** (nothing removed). **Perf: none** — and see the deferral record for why the full
  migration would, if anything, be net positive: `RsaKey::generate()` already calls `EVP_PKEY_keygen`
  and then `EVP_PKEY_get1_RSA` at `RsaKey.h:115` to downgrade a provider-backed key into a legacy RSA
  structure, after which every `RSA_sign`/`RSA_verify`/`RSA_public_encrypt` runs through 3.x's legacy
  bridge.

### B8. Minimum RSA modulus size on import (~0.5d)

`RSA_KEY_SIZE_MINIMUM = 2048` added to the anonymous enum at `RsaKey.h:45-51`, enforced by a private
`chkRsaKeyIsAcceptable` helper in `JsonSecuritySerializationImpl.h` called from all four loaders:
`loadPublicKeyFromPemString` (before `:493`), `loadPrivateKeyFromPemString` (before `:408`),
`loadPublicKeyFromJsonObject` (before `:277`), `loadPrivateKeyFromJsonObject` (before `:344`). Use
`RSA_size() * 8` — `RSA_bits` is 1.1.0+.

- **Verified safe: all twelve committed key and certificate fixtures are 2048-bit** (the four
  `utf_baselib_security/data` PEMs, the two embedded in `UtfCrypto.h:68-140`, and the six under
  `certs/`), and `RsaKey::generate()` produces 2048 (`RsaKey.h:51`). Nothing in the suite breaks.
- One 1024-bit item exists and is **out of scope**: the VeriSign Class 3 root embedded in
  `TrustedRoots.h:70`. It reaches OpenSSL through `X509_STORE_add_cert`
  (`CryptoBase::loadTrustedRootFromPem`, `:150-171`), not through `JsonSecuritySerialization`.
  Retiring it is a separate change; say so in the commit message so nobody folds it in.
- **Risk: downstream keys below 2048 bits stop loading.** Release note. **Perf: none.**

### B9. `EVP_PKEY_public_check` / `private_check` on import (~0.5d)

Same helper, under `#if OPENSSL_VERSION_NUMBER >= 0x10101000L` (both APIs are 1.1.1+; on devenv2/3
the modulus floor is the only check).

**Critical: `loadPrivateKeyFromJsonObject` must use the public-only depth.** That loader treats
`p`/`q`/`dmp1`/`dmq1`/`iqmp` as optional (`:310`, `:317-319`, `:338-342`), so a JWK carrying only
`n`/`e`/`d` is legal and loads today. `EVP_PKEY_private_check` on 3.x runs the SP800-56B test, which
requires `p` and `q`, and would silently start rejecting such keys. PEM inputs always carry the
factors, so the full check is safe only on that path.

Two behaviour changes to record in the commit message: `EVP_PKEY_public_check` on 3.x rejects public
exponents outside `2^16 < e < 2^256`, so keys with `e = 3` stop loading (desirable — the code already
argues for `RSA_F4` at `RsaKey.h:53-60` — but it *is* a break); and `private_check` runs primality
tests, measured at well under 51 ms end-to-end for a 2048-bit key, which is fine on import and must
not be moved onto a per-request path. All committed fixtures pass both checks on OpenSSL 3.0.13.

### B10. Explicit private-key protection policy (~0.5d, source-breaking by design)

`getPrivateKeyAsPemString` (`JsonSecuritySerializationImpl.h:183-186`) defaults `password` to
`str::empty()`, and an empty password means `cipher = nullptr`, i.e. **plaintext PKCS#8 written
silently**. Add `enum class KeyProtection { Encrypted, PlaintextExplicit }` as a **mandatory**
parameter placed *before* `password` so `password` keeps its default and exactly one parameter
becomes mandatory — the same placement argument as `NotifyDelivery` before `eventsMask` in
`execution-queue-notification-delivery-breaking-change.md`. Enforce the invariant
`isEncrypted != password.empty()`, which closes both silent failure modes at once.

Four call sites, one line each. `bl-tool`'s legitimate unencrypted export is preserved exactly —
`CryptoRsaKeyCommandBase.h:133` already proves intent, because the guard at `:123-129` throws when
`--encrypt` is given with an empty password; add a warning log on the plaintext branch. The other
three are `TestRsaSignVerify.h:46` (`Encrypted`), `TestPemKeyFormats.h:257` (`PlaintextExplicit` —
that test asserts the `BEGIN PRIVATE KEY` label) and `:263` (`Encrypted`).

- **Risk: source-breaking for downstream, by design** — that is the mechanism. Needs a
  breaking-change record. **Perf: none.**

---

## Workstream C — standalone fixes

### C1. CXX-09 — `std::decay_t` on the resolve handler (~1h)

`BoostAsioCompat.h:303`: `resolve_handler_wrapper< std::decay_t< ResolveHandler > >`. `ResolveHandler`
is deduced from a forwarding reference at `:301`, so an lvalue handler makes the member at `:281` a
reference into the caller's frame, held across an async operation. Aggregate initialization still
works after the change.

- All three in-repo call sites pass prvalues (`TcpBaseTasks.h:815-823`, `Pinger.h:582-590`,
  `TestBoostAsioCompat.h:180-193`), so the bug is latent; the exposure is downstream and any future
  refactor that hoists the bind into a named local. **Risk: essentially zero. Perf: none.**
- Add a regression case in `TestBoostAsioCompat.h` using a named handler local.

### C2. CXX-12 — remove the `std::char_traits<unsigned char>` specialization (~0.5d)

Two use sites, both local typedefs immediately fed to `base64UrlDecodeT`:

- `BignumBase64Url.h:62-68` → `BN_bin2bn( buffer.c_str(), ... )`
- `RsaSignVerify.h:120-127` → `RSA_verify( ..., signature.c_str(), ... )`

Switch both to `std::vector< unsigned char >` — `SerializationUtils.h:318` already has
`base64UrlDecodeVector`, and `base64DecodeT<T>` constructs `T( begin, end )`, which a vector
satisfies — change `.c_str()` to `.data()`, then delete `CPP.h:1363-1462` in full along with its
`#if defined(__clang__) && (__clang_major__ >= 20)` gate.

- **Why fix rather than defer.** Specializing `std::char_traits` for a fundamental type is UB
  regardless of gate ([namespace.std] permits specialization only for program-defined types), and the
  gate is on the *compiler* rather than the standard library, so on Clang 20 with libstdc++ it
  specializes a template libstdc++ may itself provide. This removes ~100 lines and the UB.
- **Risk: low and immediately verifiable** — clang 20.1 is the current default toolchain, so
  `make -k -j4` proves it directly. **Perf: none.**
- Verify on GCC 15.2 and MSVC vc143 as well, since the deleted specialization was previously compiled
  out on those and their behaviour should be unchanged.

---

## Workstream D — documentation-only closes

| # | Finding | Where |
|---|---|---|
| D1 | CXX-09 residual — completion tokens, associated executor/allocator/cancellation, `void` return, and the injection of project declarations into `boost::asio::ip` | Comment at `BoostAsioCompat.h:272-290` declaring the shim **internal, not public API**, plus a new item in a deferral record. Not covered by `public-header-hygiene-deferral.md`, which addresses `BOOST_ASIO_DISABLE_STD_CHRONO` only. |
| D2 | CXX-10 — filesystem copy link/special-file policy | Comment at `FsUtils.h:919` + deferral record. **State the behaviour precisely**: `is_regular_file`/`is_directory` there *follow* links, so a symlink to a file **is** copied, dereferenced. What is actually dropped is special files (FIFO/socket/device), dangling symlinks, and the contents of symlinked directories (`recursive_directory_iterator` does not descend, so an empty directory is created). Narrower and more defensible than the finding states. |
| D3 | CXX-11 — deleted move assignment | Extend the existing comments at `Platform.h:71-75` and `Manifest.h:87-91` to address the external source-compatibility break. Worth adding: `PlatformIdentityT`'s move constructor silently **copies**, because all three members are `const`; and `ManifestT` is only partly const (`m_classIds`, `m_pluginName`, `m_pluginDescription`, `m_platform` are mutable), so the stated justification is weaker than it reads. No other class in the repo uses this idiom — the house pattern is `BL_NO_COPY` / `BL_NO_COPY_OR_MOVE`, and copy operations are already deleted by `BL_DECLARE_OBJECT_IMPL_DEFAULT`. **No release note.** |
| D4 | EVP/provider migration | Deferral record naming **"retire OpenSSL 1.x" as the gating precondition**: on 3.x the JWK path becomes `EVP_PKEY_fromdata` + `OSSL_PARAM`, but those are 3.0-only, so while devenv2-5 are supported the JWK code would need two implementations and conditional code grows rather than shrinks. Record the interop contracts that must not drift (RS512 stays PKCS#1 v1.5; `RsaEncryption` stays OAEP with **SHA-1** MGF1, `RsaEncryption.h:61`) and the fact that removing the private-header include at `OpenSSLTypes.h:64-76` is the main structural prize. |
| D5 | CXX-01 duplicate-key contract | `notes/plans/issues/json-duplicate-key-contract.md` (see A1). |
| D6 | Disposition update | Update `pr-review-deep-dive-residual-findings.md` — status column, and correct the CXX-12 estimate (2 use sites, not "every use site") and the CXX-10 characterisation. |

Deferral records follow `public-header-hygiene-deferral.md`: H1 `... : Deferral Record`, a `**Finding:**`
line citing `pr-review-deep-dive-residual-findings.md:<line>`, `## Decision` with Date/Status and a
disposition table, then per item Location + code quote + `### Why this is deferred` + `### What limits
the exposure` + `### Review consensus` + `### Conditions to revisit`, and `## What was fixed` at the
end.

---

## Commit sequence

Logic-only commits, no style or comment churn riding along (`AGENTS.md`). Documentation commits are
separate from code commits.

**A:** A1 (doc) → A2 → A3 → A4 → A5 (doc)
**B:** B1 → B2 → B3 → B4 → B5 → B6 → B7 → B8 → B9 → B10
**C:** C1, C2 (independent, any order)
**D:** as the corresponding code lands; D6 last.

A, B and C touch disjoint files and can be worked in parallel or in any interleaving.

## Verification

Baseline for every commit: `make -k -j4` and `make -k -j4 VARIANT=release`.

| Sub-plan | Suites |
|---|---|
| A2, A3, A4 | `utf_baselib_data` (`TestJsonAbstraction`, `TestDataModelDefault`), `utf_baselib_messaging`. **Also `make -k -j4 BL_USE_JSON_SPIRIT=1` plus the same suites** — A2 and A3 change json-spirit behaviour and that backend has no CI; it must be built and run explicitly. |
| A4 | New permuted nested-property hash case in `TestDataModelDefault.h`. |
| B1, B3, B4, B6 | `utf_baselib_http`, `utf_baselib_io`, `utf_baselib_messaging`, `utf_baselib_blobtransfer`, `utf_baselib_rest`, `utf_baselib_security`. |
| B2 | New `TestTlsProtocolPolicy.h` in `utf_baselib_http`, debug and release. |
| B5 | `utf_baselib_http`, `utf_baselib_io`, `utf_baselib_messaging`, `utf_baselib_blobtransfer` — all build server contexts from `UtfCrypto`. |
| B8, B9 | `utf_baselib_security` (`TestPemKeyFormats`, `TestRsaSignVerify`, `TestCryptoUtils`), `utf_baselib_rest`. Run **both** a 3.x build and a `BL_USE_OPENSSL_1X=1` build — B9 is a no-op below 1.1.1. |
| B10 | The compile failure at the four call sites *is* the test. Then `utf_baselib_security`, plus a manual `bl-tool` RSA key export with and without `--encrypt`. |
| C1 | `utf_baselib` (`TestBoostAsioCompat`) with the new named-lvalue-handler case. |
| C2 | Full build on clang 20.1, **and** GCC 15.2 and MSVC vc143; `utf_baselib_security`. |

Two things no existing suite covers and which must be added: the negative TLS handshake (B2) and the
exact-byte `rawUtf8` comparison across backends (A2). `JsonPerformance*` in `utf_baselib_data` is the
vehicle if any JSON-side perf claim needs measuring, though none of A2/A3/A4 touches the Boost path.
