# EVP / Provider Migration of the RSA Key Type: Deferral Record

This document records the decision **not** to migrate the stored RSA key type from `::RSA` to
`EVP_PKEY` in this cycle, the phase-0 step which was taken instead, and the precondition which
governs when the full migration should happen. It is a risk acceptance, not an assessment that the
concern is absent.

**Finding:** CXX-06, "The migration remains dependent on deprecated low-level RSA APIs",
`notes/plans/issues/pr-review-deep-dive-residual-findings.md:106-119` and
`notes/reviews/swblocks-baselib/major/update_2025/v1/pr_review_analysis_gpt56sol_deep_dive.md:440-459`

**Analysis and decision:** `notes/plans/issues/pr-review-residual-cxx-findings-plan.md`

---

## Decision

**Date:** 2026-09-02
**Status:** Phase 0 done; the full migration is deferred behind the retirement of OpenSSL 1.x

| # | Item | Disposition |
|---|---|---|
| 1 | `EVP_PKEY` becomes the stored key type; `RsaKey::get()` stops returning `::RSA&` | **Deferred** — gated on retiring OpenSSL 1.x |
| 2 | `EVP_DigestSign` / `EVP_DigestVerify` replace `RSA_sign` / `RSA_verify` | **Deferred** with item 1 |
| 3 | `EVP_PKEY_encrypt` / `EVP_PKEY_decrypt` replace `RSA_public_encrypt` / `RSA_private_decrypt` | **Deferred** with item 1 |
| 4 | `EVP_PKEY_fromdata` / `OSSL_PARAM` replace the `RSA_get0_*` / `RSA_set0_*` JWK path | **Deferred** — this is the item which forces the precondition |
| 5 | An `EVP_PKEY` accessor exists so new code need not touch `::RSA` | **Done** — `RsaKey::evpKey()` |

---

## What is deferred, and how large it is

The library stores `::RSA*` (`src/include/baselib/crypto/OpenSSLTypes.h:159`) and exposes it
publicly as `RsaKey::get() -> ::RSA&` (`src/include/baselib/crypto/RsaKey.h`). Every consumer writes
`&rsaKey -> get()`.

The surface is contained: **19 direct `RSA_*` calls across five headers** — `RsaKey.h`,
`RsaEncryption.h`, `RsaSignVerify.h`, `OpenSSLTypes.h` and
`src/include/baselib/security/JsonSecuritySerializationImpl.h` — plus four `bl-tool` command files,
`X509Cert.h`, and four test files. A repository-wide search for
`RsaKey|RsaSignVerify|RsaEncryption` returns 16 files in total.

**Nothing in the messaging, REST, HTTP, transfer or task layers references RSA at all.**

---

## Why this is deferred: the real cost is OpenSSL 1.x, not EVP

Items 2 and 3 are mechanical. Item 4 is not, and it is what sets the price.

`JsonSecuritySerializationImpl.h` reaches RSA structure members through pointer-to-member
(`&::RSA::e` and friends), which only compiles because `OpenSSLTypes.h:69-77` includes OpenSSL's
**private** headers on the 1.1.x branch:

```cpp
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    /* OpenSSL 3.x+: RSA is fully opaque, no internal headers available */
#elif OPENSSL_VERSION_NUMBER >= 0x101010bfL
    #include <crypto/rsa/rsa_local.h>
#elif OPENSSL_VERSION_NUMBER >= 0x1010004fL
    #include <crypto/rsa/rsa_locl.h>
#endif
```

The EVP replacement for that path is `EVP_PKEY_fromdata` + `OSSL_PARAM_BLD` and
`EVP_PKEY_get_bn_param`, which are **OpenSSL 3.0 only**. While devenv2-5 are supported the JWK path
would need two parallel implementations, so conditional code **grows** rather than shrinks and the
private-header include stays. Done after 1.x is retired, the same work is a net code reduction and
removes the private-header dependency outright.

That dependency is the strongest technical argument for eventually doing this: it binds the build to
the internal layout of `rsa_st`, and it breaks on any OpenSSL point release that moves it.

---

## What it buys, and why that is not urgent

- No dependency on OpenSSL private headers.
- `-DOPENSSL_API_COMPAT=0x10100000L` (`projects/make/devenv-detect.mk`) could be dropped, and builds
  with `OPENSSL_NO_DEPRECATED` would succeed.
- Provider and FIPS policy become expressible, and non-extractable keys — held in a provider, an HSM
  or a PKCS#11 token — become representable, which an `RSA*` cannot be.

None of these is a current requirement of this library.

## Performance is not a reason either way — and probably favours migrating

There is **no served request path in this library which performs an RSA operation**: RSA appears
only in key generation, PEM and JWK import/export, and `bl-tool` commands. So there is no hot path
to regress.

The current code already pays the OpenSSL 3.x legacy-bridge cost in the wrong direction:
`RsaKey::generate()` calls `EVP_PKEY_keygen` and then immediately `EVP_PKEY_get1_RSA` to
**downgrade** a provider-backed key into a legacy RSA structure, after which every `RSA_sign`,
`RSA_verify` and `RSA_public_encrypt` runs through 3.x's legacy compatibility layer. Migrating
removes that round trip rather than adding one.

---

## Phase 0, which was done

`RsaKey::evpKey()` returns an `evppkey_ptr_t` built with `EVP_PKEY_new` + `EVP_PKEY_set1_RSA`, and
`crypto::evppkeyctx_ptr_t` was added alongside it in `OpenSSLTypes.h`. `get()` and `releaseRsa()`
keep their signatures and are marked legacy in comments.

It is additive, breaks nothing, and it gave the CXX-06 key-validation work
(`EVP_PKEY_public_check` / `EVP_PKEY_private_check` in `JsonSecuritySerializationImpl.h`) an
`EVP_PKEY` handle to consume instead of constructing throwaway wrappers around `&get()`.

**Standing rule for the interim: no new `RSA_*` call site is to be added.** New code uses
`evpKey()`. Every `RSA_*` site added now is one that has to be rewritten later.

**Baseline the rule counts from (2026-09-04).** The `RSA_get0_key` / `RSA_get0_factors` /
`RSA_get0_crt_params` and `RSA_set0_key` / `RSA_set0_factors` / `RSA_set0_crt_params` sites in
`JsonSecuritySerializationImpl.h` are the OpenSSL 3.x half of item 4 above, i.e. the accessor
replacement for the direct `rsa_st` member access of the 1.x branch, and predate the rule, as do the
`EVP_PKEY_set1_RSA` / `EVP_PKEY_get1_RSA` bridge calls in the same file and in `RsaKey.h`, which
exist only because the stored type is `::RSA` and disappear with item 1. The one `RSA_*` site added
after phase 0, `RSA_size` in `chkRsaKeyIsAcceptable` (the minimum-modulus check, B8), was replaced
by `EVP_PKEY_bits( evpKey() )` under review finding L-11 of
`notes/reviews/major/update_2025/v1/pr_review_analysis_fable51.md`, which returns the inventory to
the 19 direct calls recorded above. The public exponent policy added on 2026-09-05
(`chkPublicExponentIsAcceptable()` in the same file) reads the exponent through
`EVP_PKEY_get_bn_param( OSSL_PKEY_PARAM_RSA_E )` on 3.x and through the structure member on the 1.x
branch, exactly as the JWK export already does, so the count is unchanged.

---

## Interop contracts which must not drift during the migration

These are the traps. Each is preserved today by an EVP **default**, which means the migration can
break them silently by "modernising" something.

| Contract | Today | Requirement |
|---|---|---|
| JOSE `RS512` signatures | `RSA_sign` — PKCS#1 v1.5 | Must stay PKCS#1 v1.5. `EVP_DigestSign` defaults to it for RSA, which is correct — do **not** switch to PSS. If PSS is wanted, add `PS512` as a separate algorithm |
| RSA encryption | `RSA_PKCS1_OAEP_PADDING`, `src/include/baselib/crypto/RsaEncryption.h` | OAEP with the **SHA-1** MGF1 default. `EVP_PKEY_encrypt` matches only if the digest is left alone; changing it to SHA-256 breaks every existing ciphertext and every peer |
| PEM formats | SPKI public, PKCS#8 private, AES-256 when encrypted | Unchanged; see `notes/plans/issues/openssl3-pem-key-format-compatibility-plan.md` |

Golden vectors for both signature and ciphertext should be added **before** the migration starts, not
during it, so that a drift is caught by a failing test rather than by a peer.

---

## Conditions to revisit

- **The gating precondition:** a decision to retire OpenSSL 1.x, i.e. to drop devenv2-5 and the
  `BL_USE_OPENSSL_1X` build option. Until then the JWK path cannot be simplified and the migration
  adds conditional code instead of removing it.
- A requirement appears for FIPS mode, a hardware token, or any key which cannot be extracted into
  an `RSA*`.
- A build with `OPENSSL_NO_DEPRECATED` or a stricter `OPENSSL_API_COMPAT` becomes necessary.
- An OpenSSL release moves `rsa_st` and breaks the private-header include, which would force the
  item-4 work on an unplanned schedule.

## Sequencing when it does happen

1. Retire OpenSSL 1.x — the precondition.
2. Add golden signature and ciphertext vectors against the current implementation.
3. `RsaKey` stores `EVP_PKEY`; `get()` kept as a deprecated shim for one release, then removed.
4. Sign/verify and encrypt/decrypt onto `EVP_DigestSign*` / `EVP_PKEY_encrypt`, verified against
   step 2.
5. JWK onto `EVP_PKEY_fromdata` / `OSSL_PARAM`; delete the private-header include in
   `OpenSSLTypes.h`.
6. Drop `-DOPENSSL_API_COMPAT` and prove a build with `OPENSSL_NO_DEPRECATED`.

Step 3 is the source-breaking one for downstream holders of `RSA*` and needs a release note.
