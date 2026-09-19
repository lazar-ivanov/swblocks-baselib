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
- **The TLS test modules** built and run under `BL_USE_OPENSSL_1X=1` - `utf_baselib_http2` and, since
  S3.3, `utf_baselib_h2client`, plus whatever later slices add.
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
  - **S3.3 (the stranded TLS stream policy) adds no OpenSSL call of its own**, and that is what it
    owes here and nothing more. `TcpSslSocketAsyncStrandedBase` constructs the wrapper through the
    strand-taking constructor S1.6 added and then calls the base's `configureClientStream()`, so
    the only OpenSSL entry point on its path is the `SSL_set_tlsext_host_name` that
    `TcpSslBaseTasks.h` already called before S0.3 moved it. There is therefore no API-availability
    question to answer on 1.1.1w - what is owed is the **run**: the four stranded-policy cases of
    `utf_baselib_h2client`, two of which handshake for real, have been run on 3.5.4 only. They are
    also the first TLS cases in the tree to have a read and a write outstanding on one
    `asio::ssl::stream` at the same time, so the flavor's engine behavior under that shape is
    likewise measured on 3.5.4 only.
  - **S3.4 has landed, and the debt it adds is `utf_baselib_h2profiles` on 1.1.1w.** What the slice
    did to keep the debt small, and what it deliberately did not claim:
    - Every OpenSSL entry point it calls was read in the dist's own 3.5.4 headers rather than
      recalled - `SSL_CTX_set1_cert_store` (`ssl.h:1625`), `SSL_CTX_set_ciphersuites` (`:1668`),
      `SSL_CIPHER_get_kx_nid` (`:1644`), `SSL_CIPHER_is_aead` (`:1647`), `SSL_CTX_clear_options`
      (`:623`), and `NID_kx_ecdhe` / `NID_kx_dhe` / `NID_kx_any` in `obj_mac.h`. **Nothing was read
      or inferred for 1.1.1w**, because no header for it exists here.
    - The new names in `crypto/CryptoBase.h` are declared only from `OPENSSL_VERSION_NUMBER >=
      0x10100000L`, since `CryptoBase.h` is built on every devenv and the floor check's two cipher
      accessors do not exist on 1.0.x. Inside that, the body of `createAsioSslClientContext` is
      behind `>= 0x30500000L`, so on the 1.1.1w flavor that function is the
      `NotSupportedException` throw and nothing else. That is a property of this source which can
      be read off it; it is **not** a claim that the flavor was compiled, and the surrounding
      header may still fail there for the pre-existing reasons above.
    - The version rule is a function of a version number
      (`isTlsClientProfileSupportedOnOpenSslVersion`) rather than a bare `#if`, so
      `TlsClientContext_ClientProfilesRequireOpenSsl35Tests` asserts **both** answers on whichever
      flavor is linked. The numbers it passes are points either side of the threshold and are not
      a claim about the value any release's header carries.
    - **Still owed:** build and run `utf_baselib_h2profiles` under `BL_USE_OPENSSL_1X=1`. Three
      things only that run can settle. First, that the entry point really does take the
      `NotSupportedException` branch there - the `else` arm of that case never executes on 3.5.4.
      Second, that `SSL_CIPHER_get_kx_nid` and `SSL_CIPHER_is_aead` classify the same suites the
      same way, which is a property of the linked OpenSSL and not of this source. Third, that the
      three suites `TlsClientContext_NegotiatedParametersFloorTests` names -
      `AES128-GCM-SHA256`, `ECDHE-RSA-AES128-SHA` and `ECDHE-RSA-AES128-GCM-SHA256` - are
      resolvable there; the case fails with a clear message naming the suite if one is not.
  - **S3.6 has landed** - `crypto/TlsClientHello.h`, the ClientHello parser and the JA3 and JA4
    fingerprints. It is the least version-sensitive thing in this list and it still owes the same
    run:
    - The parser is arithmetic over bytes with no OpenSSL call in it at all, and the two hand-built
      vectors its cases assert against are byte arrays written in the test. Those assertions cannot
      differ between flavors.
    - The two digests do use OpenSSL, through `MD5_Init` / `SHA256_Init` and their `_Update` and
      `_Final` - the same low-level idiom `HashCalculator.h` already uses for `SHA512_Init`.
      **That is where this header is most likely to hit the pre-existing 1.x build failure**, since
      the `SHA512_*` and `SHA384_*` family is named above as exactly what
      `-Werror,-Wdeprecated-declarations` fails on there. That is a prediction about a known defect
      and not a finding; read the diagnostic that actually appears.
    - **Still owed, and specific:** `TlsClientHello_CapturedFromRealHandshakeTests` is the one case
      here whose expected values belong to the linked OpenSSL rather than to the test. It parses a
      hello the library really emitted and requires a `supported_versions` extension carrying
      0x0304, a server name, and the ALPN offer in order. Whether the older branch emits a hello of
      that shape is not known here and is not assumed; the case is where it would show. On 3.5.4 it
      prints the fingerprints it computed - JA3 hash `7f6ef6ebeba3cb0b7fe0b727b1fa8bba` and JA4
      `t13d0511h2_1f640057409a_c3976d268853` for the profile its sibling case builds - so the two
      flavors can be compared directly once the second one runs.
    - It consumes S1.6's `enableClientHelloCapture()`, so it inherits that slice's debt above
      rather than creating a second one.

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
