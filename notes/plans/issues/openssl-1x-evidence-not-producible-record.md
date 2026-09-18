# The OpenSSL 1.1.1w half of the S0.4 evidence could not be produced

**Origin:** slice **S0.4** of the HTTP/2 client plan
(`notes/plans/http2-implementation-plan.md` §2), implementing `notes/plans/http2-design.md`
§3.3 and §3.8 "Commit 4" - the split of `initNativeSslContext` in
`src/include/baselib/crypto/CryptoBase.h`.

**Date:** 2026-09-17. **Status:** OPEN - evidence owed, no production change pending.

---

## What is owed

Design §3.8 commit 4 and the plan's S0.4 work order both require the evidence for that change on
**two OpenSSL flavors**: 3.5.4, and 1.1.1w selected with `BL_USE_OPENSSL_1X=1`. Two things were
produced on 3.5.4 and **not** on 1.1.1w:

1. **The mechanical proof.** The probe which dumps the full observable configuration of the process
   global client context and of a server context - option bits, minimum and maximum protocol
   version, security level, ordered cipher suite list (TLS 1.3 suites included), session cache mode,
   verify mode and depth, trust anchor count - run on the parent commit and on the change, with the
   two dumps required to be identical.
2. **The test module.** `utf_baselib_http2`, which carries `TestTlsProtocolPolicy.h`,
   `TestTlsPeerVerification.h` and `TestAsioSslStreamWrapper.h`, built and run under
   `BL_USE_OPENSSL_1X=1`.

The same obligation applies to the G1 gate (S0.5), which names the TLS modules on both flavors.

## Why it could not be produced

**OpenSSL 1.1.1w is not on this machine and building it is out of scope for the slice.**

- `projects/make/devenv-detect.mk:257` selects `BL_DEVENV_OPENSSL_VERSION=1.1.1w` when
  `BL_USE_OPENSSL_1X` is set, for devenv7.
- The devenv7 dist here,
  `/home/lazar/swblocks/dist-devenv7-ub24-gcc1520-clang2010-a64/openssl/`, contains exactly one
  entry: `3.5.4`. The x64 devenv7 dist,
  `/home/lazar/x64_home/swblocks/dist-devenv7-ub24-gcc1520-clang2010-x64/openssl/`, is the same.
- The only 1.1.x tree on disk is `1.1.1k`, under `old-envs/dist-devenv5-ub20-gcc1110-clang1201-arm`.
  That is a different devenv, a different toolchain and a different Boost, so it is not a substitute:
  selecting it would change far more than the OpenSSL version.

**There is also a second, pre-existing blocker**, so provisioning the dist alone is not sufficient.
`notes/reviews/major/update_2026/whole-library-cxx-review-fable51-decisions.md` (item recorded
2026-09-07) established that `BL_USE_OPENSSL_1X=1` selects `openssl/1.1.1w` correctly and then fails
the build with `-Werror,-Wdeprecated-declarations` on `RSA_free` (`crypto/OpenSSLTypes.h`), the
`SHA512_*` and `SHA384_*` functions (`crypto/HashCalculator.h`) and the `RSA`/`EVP_PKEY` conversions
(`crypto/RsaKey.h`). The 3.x configuration is spared because it is compiled with
`-DOPENSSL_API_COMPAT=0x10100000L`; the 1.1.1w configuration gets no equivalent. Those headers are
reached by anything which includes `crypto/CryptoBase.h`, so the probe hits them too.

## What would produce it

1. Build OpenSSL 1.1.1w into the devenv7 dist for this platform, i.e.
   `openssl/1.1.1w/ub24-a64-clang2010-{debug,release}` alongside `3.5.4`.
2. Clear the `-Wdeprecated-declarations` blocker above - either by giving the 1.1.1w configuration a
   deprecation policy of its own in `devenv-detect.mk`, or by fixing the call sites.
3. Re-run, with `BL_USE_OPENSSL_1X=1`:
   - the context-dump probe against the parent commit of the S0.4 split and against it, with the two
     dumps compared. The probe is `scripts/utests/tls_context_dump.{cpp,sh}`, which takes a worktree
     root, so it is built once per revision - against two worktrees, or one checkout at each
     revision. Its build flags are a capture pinning a toolchain, a Boost and an OpenSSL; the dist
     root and the versions are environment overrides, so the 1.1.1w run is
     `BL_OPENSSL_VERSION=1.1.1w` plus a dist that actually carries it;
   - `make -k -j1 utf_baselib_http2`, and the run.

## How much risk the gap actually carries

Bounded, and worth stating precisely rather than leaving open.

The concern behind the two-flavor requirement is that `initNativeSslContext` is full of
`#if OPENSSL_VERSION_NUMBER` branches, so a refactor could be correct on one flavor and wrong on the
other. In the region which was split there are exactly three preprocessor regions:
`#ifdef SSL_OP_NO_RENEGOTIATION`, and two `#if OPENSSL_VERSION_NUMBER >= 0x10100000L` (one of them in
`chkUsableCipherSuitesAvailable`, which was not touched).

- **Every one of them resolves the same way on 1.1.1w as on 3.5.4.** Both are at or above 1.1.0 and
  neither region has a 3.x-specific arm; `SSL_OP_NO_RENEGOTIATION` exists from 1.1.0h.
- **No new function boundary crosses a preprocessor region.** Step 1 ends after the `#endif` which
  closes the 1.1.0+ region, and step 2 (the cipher policy) contains no directive at all. So the
  preprocessed token stream of each step is the same text on either flavor as it was before the
  split.

What the missing run would still add is the empirical check that the *values* those steps produce on
1.1.1w are unchanged - the cipher list resolved from the aliases in particular, which is a property
of the linked OpenSSL rather than of this source. That is exactly what the 3.5.4 dump comparison
proved on this flavor, and it is what is owed on the other.

## Precedent

`notes/plans/issues/tls-legacy-protocol-opt-in-removal-decision.md` ("Status") is the change this
requirement was inherited from; its verification matrix did cover both flavors, on a machine where
1.1.1w was available. `notes/plans/issues/windows-only-residual-findings-deferral.md` item 8 carries
a still-open 1.1.1w half of the same kind.
