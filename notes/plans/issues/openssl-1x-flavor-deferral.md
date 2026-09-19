# The OpenSSL 1.1.1w flavor is deferred, not abandoned

**Decision:** 2026-09-18, by the maintainer. **Status:** DEFERRED - closed as an open question, kept
as owed evidence. Nothing in the HTTP/2 work waits on it.

This supersedes the open question in `notes/plans/issues/openssl-1x-evidence-not-producible-record.md`,
which stays as the evidence: it records precisely what could not be produced and why. This record is
the decision about it.

## What is deferred

Verification on the second OpenSSL flavor. Design decision **D2** promises that everything except
impersonation works on both devenv7 flavors - 3.5.4, and 1.1.1w under `BL_USE_OPENSSL_1X` - and every
slice that touches OpenSSL inherits that promise. What is owed, for L0 and for every later slice
until this is reopened:

- **S0.4's mechanical proof** on 1.1.1w: the context-dump probe
  (`scripts/utests/tls_context_dump.{cpp,sh}`, `BL_OPENSSL_VERSION=1.1.1w`) run on both sides of the
  `initNativeSslContext` split, with the two dumps required to be identical.
- **The TLS test modules** built and run under `BL_USE_OPENSSL_1X=1` - `utf_baselib_http2` today, and
  whatever later slices add.
- **The G1 gate's "both flavors for the TLS modules" clause**, which was recorded as unmet.
- **S1.6 onwards.** Every slice adding OpenSSL calls - the ALPN offer and selected-protocol getters,
  the ClientHello capture hook, the TLS client contexts of S3.4 - adds to this debt rather than
  creating a new one.
  - **S1.6 has landed, and `enableClientHelloCapture()` is the thing to build first.** On 3.5.4
    `SSL_set_msg_callback` is a **function** taking the exact callback prototype - verified in the
    dist's own `ssl.h:663`, where the `SSL_set_msg_callback_arg` beside it at `:668` is the macro
    over `SSL_ctrl` - so the callback binds with no cast, and the gcc and clang builds of
    `utf_baselib_http2` are both clean. **What happens on 1.1.1w is genuinely unknown here and
    should not be guessed at.** An earlier version of this record asserted that 1.1.1w defines it as
    a macro casting the callback, and named the diagnostic that would follow; that was inferred
    rather than measured, no 1.1.1w header exists on this machine to check it against, and the
    analogous claim turned out to be **false** on the flavor we do have. Build `utf_baselib_http2`
    first on that flavor because it is the module which instantiates the hook - but read whatever
    diagnostic appears, rather than expecting a particular one. Nothing else S1.6 added is version-sensitive - the
    ALPN pair is 1.0.2 and the rest older still - and the pre-handshake `SSL_get_version` value its
    getters deliberately do not forward is *more* misleading on 3.5.4, which reports "TLSv1.3" for a
    stream that has never handshaked, than the "unknown" 1.x documents, so the rule those getters
    use - report nothing until there is a current cipher - holds on both.

## Why deferring is reasonable, and where the risk actually sits

Two independent blockers, neither of which is about this work: no devenv7 dist on the development
machine carries 1.1.1w at all, and `BL_USE_OPENSSL_1X=1` **does not build** for pre-existing reasons -
the 3.x configuration is compiled with `-DOPENSSL_API_COMPAT=0x10100000L` and the 1.x configuration
gets no equivalent, so `-Werror,-Wdeprecated-declarations` fails on `RSA_free`, the `SHA512_*` and
`SHA384_*` family and the `RSA`/`EVP_PKEY` conversions. So the flavor is untested for the **whole
library** right now, not merely for this feature.

The residual risk for what has already landed is bounded and was argued in the evidence record: in the
region S0.4 split there are three preprocessor regions, every one resolves the same way on 1.1.1w as
on 3.5.4, and no new function boundary crosses one. What a run would add is the empirical check that
the *values* match - the cipher list resolved from the aliases in particular, which is a property of
the linked OpenSSL rather than of this source.

The risk is higher for slices not yet written. An API added in S1.6 or S3.4 that is absent or behaves
differently on 1.1.1w would not be caught until this is reopened. **So a slice touching OpenSSL should
still guard by version where the design already says to** (design §6 guards impersonation at
`OPENSSL_VERSION_NUMBER >= 0x30500000L`), and should state in its acceptance that the second flavor is
owed rather than silently claim both.

## What would reopen it

1. Build OpenSSL 1.1.1w into the devenv7 dist for the platform, i.e.
   `openssl/1.1.1w/<plat>-{debug,release}` alongside `3.5.4`.
2. Give the 1.1.1w configuration a deprecation policy of its own in `devenv-detect.mk`, or fix the
   call sites the `-Wdeprecated-declarations` failure names.
3. Re-run the owed evidence above. The probe takes a worktree root and its pinned versions are
   environment overrides, so the run is `BL_OPENSSL_VERSION=1.1.1w` plus a dist that carries it.

Whoever does this should expect to find either nothing, or something that has been broken for a long
time and has nothing to do with HTTP/2.

## The honest statement of D2 until then

**D2 is a design intent that is currently unverified on one of its two flavors, for the whole
library.** It is not a claim this work has demonstrated. Anything that needs it demonstrated - a
release, a support statement, a downstream consumer on 1.1.1w - has to reopen this first.
