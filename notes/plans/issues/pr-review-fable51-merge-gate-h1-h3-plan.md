# Plan: merge-gate items 1-3 (H-1, H-2, H-3) from the Fable 5.1 PR review

**Date:** 2026-09-04  
**Status:** implemented and verified on `lazari2` (2026-09-04); see the Verification section for the matrix that was run

Source: `notes/reviews/major/update_2025/v1/pr_review_analysis_fable51.md`, section 11 items 1-3 and section 3 (H-1..H-3).

## Context

The `lazari2` branch review flagged three merge-blocking findings:

- **H-1** — `JsonSecuritySerializationImpl.h` and `HmacSha256.h` call OpenSSL 3.0-only APIs
  (`EVP_PKEY_private_check`, and 1.0.x-era `HMAC_CTX_init/cleanup` on an opaque 1.1.x `HMAC_CTX`)
  behind guards that also admit OpenSSL 1.1.1, so every 1.1.1 configuration (devenv4, devenv5,
  `BL_USE_OPENSSL_1X=1` on devenv6/7) stops compiling. The PEM plan's mandated 1.1.1 verification
  build was never run.
- **H-2** — the json-spirit backend passes `raw_utf8` to json-spirit's writer, which then copies
  U+0000..U+001F verbatim into string literals. That is invalid JSON (RFC 8259 §7) and a regression
  from `master` (where `rawUTF8=false` routed control characters through `\uXXXX`). Boost.JSON on a
  devenv7 peer rejects such documents.
- **H-3** — the branch's legacy TLS opt-in (`TlsMinimumVersion::Tls10Legacy` / `Tls11Legacy`, reached
  via the retained `isEnableTlsV10( true )` shim) now calls `SSL_CTX_set_security_level( ctx, 0 )`
  on both the process-global client context and every server context. Three shipped servers
  (broker, HTTP gateway, echo server) call `isEnableTlsV10( true )` unconditionally at startup, so on
  OpenSSL 3 they went from an effective TLS 1.2 / level 1 posture on `master` to TLS 1.0 / level 0.

**Policy decision from the user (H-3):** OpenSSL 1.x builds must be fully secure with the highest
floor the linked OpenSSL supports. No TLS 1.0 / 1.1, no security level 0, and no configurable
option that can lower either. The legacy opt-in is therefore removed outright rather than gated
behind a flag. Items 1 and 2 follow the review's recommended fixes as written.

Decision on the merge-gate item-1 question ("do devenv4/5 and `BL_USE_OPENSSL_1X` remain
supported?"): **yes** — the user wants 1.x support kept and secure, so H-1 is fixed with version
guards rather than by dropping the configurations.

---

## Part A — H-3: remove the legacy TLS opt-in (policy decision)

### What the code does today

`src/include/baselib/crypto/CryptoBase.h`

- `TlsMinimumVersion { Tls12, Tls11Legacy, Tls10Legacy }` (`:50-55`) is a process-global static
  `g_tlsMinimumVersion` (`:84`, default `Tls12` at `:641`).
- `initNativeSslContext()` (`:263-398`) is run once for the global client context in `initSsl()`
  (`:449-451`) and once per `createAsioSslServerContext()` (`:479`). It:
  - sets `SSL_OP_NO_SSLv2|NO_SSLv3|SSL_OP_ALL|NO_TICKET`, and on the default adds
    `NO_TLSv1|NO_TLSv1_1` (`:287-308`);
  - on OpenSSL >= 1.1.0 calls `SSL_CTX_set_min_proto_version( TLS1_2_VERSION )` (`:328-330`);
  - **if a legacy value is selected, calls `SSL_CTX_set_security_level( ctx, 0 )`** (`:332-356`).
    Level 0 disables *all* strength checks: MD5-signed chain certificates, RSA/DH < 1024 bits,
    RC4/export/anonymous suites are no longer refused by the level (the cipher list still
    excludes most of them, but the certificate-chain checks have no other guard). The level is a
    property of the whole `SSL_CTX`, so it applies to every outbound connection of the client
    context, not just the legacy peer that motivated the opt-in.
  - sets the forward-secret AES cipher list (`:387-393`), checks a TLS 1.2 suite survived, loads the
    bundled roots.
- Public API: `CryptoBase::tlsMinimumVersion()` (`:824`) and the deprecated shim
  `CryptoBase::isEnableTlsV10( bool )` (`:839-844`), which maps `true` to `Tls10Legacy`.

Callers of the opt-in (all to be removed):

| Site | Call |
|---|---|
| `src/local/apps/bl-messaging-broker/MessagingBrokerApp.h:268-273` | `isEnableTlsV10( true )` + 2017 comment |
| `src/local/apps/bl-messaging-http-gateway/MessagingHttpGatewayApp.h:353-358` | same |
| `src/local/apps/bl-messaging-echo-server/MessagingEchoServerApp.h:252-257` | same |
| `src/utests/utf_baselib_security/UtfBaselibSecurityMain.cpp:18` | `#define UTF_TEST_APP_INIT_PHASE1_INIT ...isEnableTlsV10( true );` |
| `src/utests/utf_baselib_http/TestTlsProtocolPolicy.h:288-320` | `Tls11IsPermittedUnderTheLegacyOptIn` sets `Tls11Legacy` |

On `master`, `isEnableTlsV10` only cleared `SSL_OP_NO_TLSv1`; on OpenSSL 3 it was ineffective
(default level 1 refuses TLS 1.0/1.1 regardless), so the three servers ran TLS 1.2+ at level 1.
On OpenSSL 1.1.1 builds it genuinely enabled TLS 1.0. Either way the 2017 rationale ("authorization
servers that might not yet support TLS 1.1") is dead.

### Design

1. **Delete the opt-in surface entirely** from `CryptoBase.h`:
   - remove `enum class TlsMinimumVersion` and its doc block (`:36-55`), `g_tlsMinimumVersion`
     (`:84`, `:641-642`), `detail::CryptoInit::tlsMinimumVersion()` (`:621-624`),
     `CryptoBase::tlsMinimumVersion()` (`:824-827`) and `CryptoBase::isEnableTlsV10()` (`:829-844`).
   - `TlsMinimumVersion` is new on this branch, so its removal costs `master` consumers nothing.
     `isEnableTlsV10` exists on `master`; removing it (**decided by the user**) is a deliberate
     compile-time break (same
     philosophy as the `setNotifyCallback` change in release-note item 2): a silent no-op would be
     exactly the "opt-in which silently does not do what it says" the branch's own comment warns
     against, and a throwing shim only moves the surprise to runtime.
2. **Simplify `initNativeSslContext()`** to the fixed policy:
   - `options |= NO_SSLv2|NO_SSLv3|SSL_OP_ALL|NO_TICKET|NO_TLSv1|NO_TLSv1_1` unconditionally
     (the option bits are the only mechanism on OpenSSL 1.0.2, where `set_min_proto_version` and
     security levels do not exist);
   - on `>= 0x10100000L`: `SSL_CTX_set_min_proto_version( TLS1_2_VERSION )` (keep), delete the
     `isLegacyProtocolRequested` block and the `SSL_CTX_set_security_level( 0 )` call;
   - **explicitly pin the security level** on `>= 0x10100000L`:
     `BL_CHK_CRYPTO_API_NM( 1 == ... )` is not applicable (void return) — call
     `::SSL_CTX_set_security_level( ctx, 2 )` and assert `2 == ::SSL_CTX_get_security_level( ctx )`.
     Rationale: the default level is a *compile-time* property of the linked OpenSSL
     (`OPENSSL_TLS_SECURITY_LEVEL`, normally 1; distro builds may pick 2), so without pinning the
     floor depends on who built the library. Level 2 = 112-bit: RSA/DSA/DH >= 2048, ECC >= 224,
     no RC4, no SSLv3, compression off; on OpenSSL 3 it additionally denies SHA-1 signatures in
     the chain (already denied at level 1 there). This is the modern distro baseline and it is what
     "highest floor supported by this version" means in practice — level 3 would refuse the 2048-bit
     test and deployment certificates. **Decided by the user: pin level 2.** All in-repo test
     certificates (`certs/*.pem`, and the inline copies in `UtfCrypto.h`) are RSA-2048 / SHA-256,
     so they satisfy level 2.
   - rewrite the comment block at `:267-285` and `:334-353` to describe the fixed policy and to
     drop the BEAST / `SSL_OP_LEGACY_SERVER_CONNECT` notes that only mattered under the opt-in.
     Two facts the new comment must carry: a `@SECLEVEL=` token inside a cipher string overrides
     `SSL_CTX_set_security_level`, so the cipher list at `:390` must never gain one; and because
     the level is set before `use_private_key` / `use_certificate_chain` (`:516-522`), a server's
     own sub-2048-bit or SHA-1 certificate is refused at `createAsioSslServerContext` time (server
     startup), not at the first handshake.
   - No DH parameters are installed anywhere in `src/` (no `set_tmp_dh` / `DH_new`), ECDHE is
     automatic with P-256 (>= 224 bits), tickets are already off and only level 3 would touch
     them, so nothing else in the repo changes at level 2.
   - Note for the record: `SSL_OP_ALL` includes `SSL_OP_DONT_INSERT_EMPTY_FRAGMENTS`, irrelevant at
     a TLS 1.2 floor; leave `SSL_OP_ALL` as is (out of scope to change).
3. **Three apps:** delete the comment + `isEnableTlsV10( true )` line in each `main()`. No new
   command-line flag (the user rejected a configurable weakening).
4. **`UtfBaselibSecurityMain.cpp`:** delete line 18 (`#define UTF_TEST_APP_INIT_PHASE1_INIT ...`);
   the macro is optional (`UtfMain.h:220-222`).
5. **`TestTlsProtocolPolicy.h`:**
   - fix the header comment (`:28-30`) which says the security test binary enables TLS 1.0;
   - delete `TlsProtocolPolicy_Tls11IsPermittedUnderTheLegacyOptIn`;
   - extend `TlsProtocolPolicy_Tls11IsRefusedUnderTheDefaultPolicy` (or add a sibling) to also drive
     a `TLS1_VERSION` client and require refusal;
   - add `TlsProtocolPolicy_SecurityLevelAndFloorArePinned` (under `#if OPENSSL_VERSION_NUMBER >=
     0x10100000L`): create a server context via `createAsioSslServerContext` and also read the client
     context via `CryptoBase::getAsioSslContext()`; assert
     `SSL_CTX_get_min_proto_version( ctx ) == TLS1_2_VERSION` and
     `::SSL_CTX_get_security_level( ctx ) == 2` on both. Note `SSL_CTX_get_min_proto_version` is a
     macro over `SSL_CTX_ctrl` on both 1.1.x and 3.x and must be written **unqualified** (the
     convention recorded at `CryptoBase.h:234-236`); `SSL_CTX_get_security_level` is a real
     function. This is the assertion that makes the policy verifiable and guards against a future
     re-lowering.
   - the test helper's `"DEFAULT:@SECLEVEL=0"` cipher string (`:139`) is on the *client* test
     context only and is fine to keep.
6. **Decision record + release note:**
   - new file `notes/plans/issues/tls-legacy-protocol-opt-in-removal-decision.md` (same shape as
     `json-duplicate-key-contract.md`): the finding, the policy ("highest secure floor, nothing
     configurable below it"), what level 0 actually permitted, why removal beat a flag, the exact
     posture per OpenSSL version (1.0.2: option bits only, no level API; 1.1.x / 3.x: TLS 1.2 floor +
     level 2), and the consumer migration (delete the `isEnableTlsV10` call; a peer that needs
     TLS < 1.2 needs a reissued endpoint, not a library option).
   - append item **6** to `notes/plans/issues/devenv7-breaking-changes-release-notes.md`:
     "`CryptoBase::isEnableTlsV10` removed; TLS 1.2 is the floor on every build; OpenSSL security
     level pinned to 2 on 1.1.0+. Presents as a compile error for callers and as a handshake failure
     for peers below TLS 1.2 or with sub-2048-bit / SHA-1 certificates." Also mention that the
     three shipped servers no longer relax anything at startup. Say explicitly that a server whose
     own certificate is below the floor now fails at context creation (startup), and a client
     talking to such a peer fails at handshake.
   - one-line "superseded by" pointers in the two status records that describe the enum:
     `pr-review-residual-cxx-findings-plan.md` (B1, `:185-216`) and
     `pr-review-deep-dive-residual-findings.md:32`.

### Interaction with other findings (not fixed here, but must not be contradicted)

- L-10: `SSL_CTX_get_ciphers` at `:228` is 1.1.0+ and unguarded while devenv2 pins 1.0.2 — the new
  `get_security_level` assertion goes under the same `>= 0x10100000L` guard so it does not add a
  second 1.0.2 break.
- M-5 / trust store: at level 2 the bundled 1024-bit VeriSign Class 3 root cannot anchor a chain;
  it already could not verify anything current. Note this in the decision record; do not touch
  `TrustedRoots.h`.
- M-6: test certificates must be >= 2048-bit RSA with SHA-256 signatures for level 2 (to be
  confirmed with `openssl x509 -text` before implementation; if any fixture is weaker it is
  regenerated as part of this work, since the tests would otherwise fail).

---

## Part B — H-1: OpenSSL 1.1.1 configurations must compile again

Blast radius: five lines in two headers, plus one wrong comment each.

### B1. `src/include/baselib/security/JsonSecuritySerializationImpl.h:629-653` (`chkRsaKeyIsAcceptable`)

Current: `#if OPENSSL_VERSION_NUMBER >= 0x10101000L` then
`BL_CHK_CRYPTO_API_NM( Full == depth ? ::EVP_PKEY_private_check(ctx) : ::EVP_PKEY_public_check(ctx) )`.

Facts: `EVP_PKEY_check` / `EVP_PKEY_public_check` / `EVP_PKEY_param_check` exist from 1.1.1;
`EVP_PKEY_private_check` only from 3.0 (confirmed in the local 3.5.4 `evp.h:2099-2104`, and by the
review against 1.1.1w). All return `1` ok / `0` invalid / negative "unsupported or error", and
`BL_CHK_CRYPTO_API_NM` (`crypto/ErrorHandling.h:34-45`) only rejects zero, so a `-2` today passes
as success.

Change:

```cpp
#if OPENSSL_VERSION_NUMBER >= 0x10101000L
    ... ctx creation unchanged ...
    const int rc =
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
        KeyCheckDepth::Full == depth ? ::EVP_PKEY_private_check( ctx.get() ) : ::EVP_PKEY_public_check( ctx.get() );
#else
        /* 1.1.1: EVP_PKEY_check runs the full RSA key check (public part + private consistency);
           EVP_PKEY_private_check does not exist before 3.0 */
        KeyCheckDepth::Full == depth ? ::EVP_PKEY_check( ctx.get() ) : ::EVP_PKEY_public_check( ctx.get() );
#endif
    BL_CHK_CRYPTO_API_NM( 1 == rc );
#else
    BL_UNUSED( depth );
#endif
```

Fix the comment (`:630-637`) to say which API arrived in which version. The `1 ==` test is what
turns an "unsupported" negative return into a thrown error instead of a silent pass.
**Open point to settle while running the 1.1.1 build:** if `EVP_PKEY_public_check` returns `-2`
for RSA on 1.1.1w (the review suspects the 1.1.1 RSA method table only supplies `pkey_check`),
then on `< 3.0` the `PublicOnly` depth must run **no EVP check at all** (same as the `< 1.1.1`
branch; the modulus-size floor still applies). It must *not* fall back to `EVP_PKEY_check`: on
1.1.1 that routes to `RSA_check_key_ex`, which needs `p, q, d` and returns 0 for a public-only
key, so every legitimate public key would be rejected. Confirm against the 1.1.1w source under
`openssl/1.1.1w/source/crypto/rsa/rsa_pmeth.c` once the dist exists; the `isErrorQueueClean()`
checks in `TestPemKeyFormats.h:137,166` show whether the chosen call leaves the queue dirty.

### B2. `src/include/baselib/crypto/HmacSha256.h:142-181` (the `#else` branch, `< 3.0`)

Current branch uses a stack `::HMAC_CTX` plus `HMAC_CTX_init` / `HMAC_CTX_cleanup` (1.0.x only;
`HMAC_CTX` is opaque and those two functions are gone since 1.1.0). Replace the whole branch with
the one-shot form, which exists identically in 1.0.2, 1.1.x and 3.x (and is not deprecated even in
3.5.4):

```cpp
#else
    /*
     * OpenSSL < 3.0: the one-shot HMAC() exists unchanged on every supported version; the
     * HMAC_CTX API changed shape in 1.1.0 (opaque context, _new/_free) and is avoided
     */
    BL_CHK_CRYPTO_API_NM(
        ::HMAC(
            ::EVP_sha256(),
            key.c_str(),
            static_cast< int >( key.length() ),
            reinterpret_cast< const unsigned char* >( message.c_str() ),
            message.length(),
            messageDigest,
            &digestLength
            )
        );
    BL_CHK_CRYPTO_API_NM( digestLength == sizeof( messageDigest ) );
#endif
```

`key` / `message` are `const std::string&`, `messageDigest` is `unsigned char[ SHA256_DIGEST_LENGTH ]`
and `digestLength` is an `unsigned int` already declared at `:53-59`, so no new locals are needed;
`<openssl/hmac.h>` is already included (`:24`). Signature confirmed against the 3.5.4 `hmac.h:54-56`.
Keep the digest hex-formatting tail (`:183-198`) untouched.

### B3. Keep `BL_USE_OPENSSL_1X` / devenv4 / devenv5 (decision)

No makefile or doc change. `CONTRIBUTING.md:49` already advertises the option; after this fix it is
true again.

### B4. Produce the missing OpenSSL 1.1.1w dist and run the prescribed build

There is **no** 1.1.1 OpenSSL under `~/swblocks/dist-devenv7-ub24-gcc1520-clang2010-a64/openssl/`
(only `3.5.4`), so the merge-gate build has never been runnable on this machine. The existing
script handles it — the URL `https://www.openssl.org/source/openssl-1.1.1w.tar.gz` still resolves
(301 to the GitHub release asset, 200) and 1.1.1w's `Configure` accepts every flag the script
passes (`linux-aarch64`, `--debug -g -O0` / `-O3`, `no-shared`, `--prefix`, `--openssldir`,
`--libdir=lib`):

```bash
scripts/devenv7/linux/build-openssl-linux.sh gcc 1.1.1w devenv7 15.2.0 gcc1520-clang2010
```

It installs to `.../openssl/1.1.1w/ub24-a64-gcc1520-{debug,release}` and extracts `source/`,
which `projects/make/3rd/openssl/common.mk:37-39` and `OpenSSLTypes.h:72`
(`crypto/rsa/rsa_local.h`, `>= 0x101010bfL`) need for 1.1.1w. Builds debug+release and runs
OpenSSL's own `make test`; budget 20-40 minutes. Only the gcc toolchain is needed for the gate
(clang optional). If 1.1.1w fails to build with GCC 15 (unlikely; warnings only), report it rather
than patching the script.

Then, per `openssl3-pem-key-format-compatibility-plan.md:130-140` and
`pr-review-residual-cxx-findings-plan.md:466`:

```bash
make -k -j1 utf_baselib_security BL_USE_OPENSSL_1X=1
make -k -j1 utf_baselib_http     BL_USE_OPENSSL_1X=1      # second TestUtils.h includer; also hosts TestTlsProtocolPolicy
make test_utf_baselib_security BL_USE_OPENSSL_1X=1
make test_utf_baselib_http     BL_USE_OPENSSL_1X=1
```

`utf_baselib_http` is the second module because it carries the TLS policy tests from Part A, so the
same run proves the fixed TLS floor on 1.1.1w as well.

---

## Part C — H-2: json-spirit backend must escape C0 control characters

### What is wrong

`src/include/baselib/core/detail/JsonSpiritImpl.h` `saveToString` (`:750-803`) and the
independent `saveToStream` (`:825-877`) both pass `json_spirit::raw_utf8`. json-spirit 4.08
(`json_spirit_writer_template.h:50-102`) then escapes only `" \ \b \f \n \r \t` and copies every
other byte, including U+0000..U+001F, verbatim — for values **and object keys**. json-spirit 4.08
has no option combination that escapes C0 but keeps >= 0x80 raw (`json_spirit_writer_options.h`).
Pre-escaping the strings before `write_string` does not work either: `add_esc_chars` would turn
the inserted `\` into `\\`. Boost.JSON (`serializer.ipp:203-212`) emits `\b \t \n \f \r`, `\"`,
`\\`, and lowercase `\u00xx` for the remaining C0 bytes; 0x7F and above pass raw. That is the
target output.

### Design: a string-literal-aware post-pass over json-spirit's output

Add one private static helper to `JsonUtilsImplT` in `JsonSpiritImpl.h`:

```cpp
static std::string escapeControlCharacters( SAA_in const std::string& jsonText )
```

A single linear scan with one bit of state (inside a string literal or not):

- outside a literal: copy; `"` enters a literal;
- inside a literal: `\` copies itself and the next byte (json-spirit only ever emits two-byte
  escapes `\" \\ \b \f \n \r \t` in raw mode, so this is exact); `"` leaves the literal; a byte
  `< 0x20` becomes `\u00` + two **lowercase** hex digits; everything else copies.

Why this shape and not a hand-written serializer: it cannot disturb json-spirit's pretty layout,
double formatting or integer formatting (`JsonSerializePretty`, `JsonPrettyPrintEmptyContainers`
and the L-5/L-7 parity notes all depend on those), it covers keys and values alike, and its
correctness rests only on the writer's output grammar (outside literals json-spirit emits only
structural characters, digits, `true/false/null` and pretty-print whitespace). Cost is one extra
linear pass on the legacy backend only. A full replacement serializer stays the fallback if a
reviewer insists, but it is a much larger diff with new parity surface.

Wiring:

- `saveToString` (`:798-802`): `return escapeControlCharacters( write_string( val, options ) );`
- `saveToStream` (`:825-877`): collapse to `output << saveToString( val, prettyPrint, rawUtf8,
  canonicalize );`, the same delegation `BoostJsonImpl.h:650-659` already uses, so the escaping
  cannot drift between the two paths. Delete the duplicated comment block (`:853-868`).
- Extend the `raw_utf8` comment at `:779-796` with one paragraph: json-spirit's raw mode also
  disables C0 escaping, which RFC 8259 §7 forbids, hence the post-pass; lowercase hex is chosen to
  be byte-identical with Boost.JSON (note: `master` emitted uppercase via json-spirit's
  `non_printable_to_string`; backend parity wins over `master` spelling since no consumer hashes
  or persists the text — release-note item 4).
- Extend the note at `JsonUtils.h:181-193` (and the mirror at `DataModelObject.h:238-241`) with:
  "control characters U+0000..U+001F are always emitted as JSON escapes on both backends".

### Test (both backends, outside the `#if !defined( BL_USE_JSON_SPIRIT )` block at `TestJsonAbstraction.h:1663-1754`)

New `UTF_AUTO_TEST_CASE( JsonSerializeEscapesControlCharacters )` next to
`JsonSerializeAlwaysEmitsRawUtf8` (`:1422`), in `src/utests/utf_baselib_data/TestJsonAbstraction.h`:

- object with key `"k\x01"` and a value holding, in order: NUL, 0x01, backspace, tab, LF, FF, CR,
  ESC (0x1B), 0x1F, DEL (0x7F), then the text `Café` as UTF-8. Build it with the explicit-length
  `std::string( literal, 15 )` constructor because of the embedded NUL, and split the literal
  after `\x7f` (`"...\x7f" "Caf\xC3\xA9"`), since `"\x7fCaf"` is one out-of-range hex escape;
- assert the exact compact bytes, written out in the source as a plain string literal
  (spelled here in words because the escapes themselves cannot be typed into this file): the
  key becomes `k` followed by the six-character escape `\u` + `0001`; the value becomes the
  escapes `\u` + `0000` and `\u` + `0001`, then the short forms `\b`, `\t`, `\n`, `\f`, `\r`,
  then `\u` + `001b` and `\u` + `001f`, then the raw DEL byte and the raw UTF-8 bytes of `Café`
  (every C0 byte escaped, lowercase hex for the ones without a short form, 0x7F and >= 0x80
  untouched);
- assert `saveToStream` produces the same bytes, and that the `prettyPrint = true` output
  contains no raw byte below 0x20 inside a literal (`find( '\x1b' ) == npos` is enough, since
  pretty-print newlines only ever occur outside literals) and re-parses to an equal value;
- assert the round trip through `readFromString` returns the original key and value bytes;
- pin the hex case both ways: the six-character escape for ESC with lowercase `b` must be found,
  and the uppercase-`B` spelling must not be.

Also update `notes/plans/issues/json-backend-verification-decision.md`: the "rawUtf8 ... closed by
aligning the backends (CXX-08)" line gets a sentence noting the C0 regression that alignment
introduced and this fix.

### Verification for H-2

```bash
make -k -j1 utf_baselib_data                                      # Boost.JSON backend
make -k -j1 utf_baselib_data VARIANT=release BL_USE_JSON_SPIRIT=1  # json-spirit (release, -j1: per the decision record)
```
then run both `utf-baselib-data` binaries (`make test_utf_baselib_data` and the same with
`VARIANT=release BL_USE_JSON_SPIRIT=1`). The json-spirit dist is present locally
(`json-spirit/4.08/source`), and the devenv7 tag does not auto-select it, so the flag is required.

---

## Suggested commit split (user commits; nothing is committed by me)

1. H-1 code fix (two headers) — after the 1.1.1w dist exists and both builds pass.
2. H-2 code fix + test + comment/doc lines.
3. H-3 code removal (CryptoBase.h, three apps, two test files) + decision record + release note.

Each is independently reviewable; no style-only changes mixed in.

## Verification (end to end)

| Step | Command |
|---|---|
| Build 1.1.1w dist | `scripts/devenv7/linux/build-openssl-linux.sh gcc 1.1.1w devenv7 15.2.0 gcc1520-clang2010` |
| H-1 + H-3 on 1.1.1w | `make -k -j1 utf_baselib_security BL_USE_OPENSSL_1X=1` and `... utf_baselib_http ...`, then `make test_...` for both |
| H-1 + H-3 on 3.5.4 | same two modules without the flag, debug; plus `VARIANT=release` for `utf_baselib_http` |
| Apps compile | `make -k -j1 bl-messaging-broker bl-messaging-http-gateway bl-messaging-echo-server bl-tool` (targets enumerated from `src/apps/*` by `common.mk:265`); `bl-tool` is the one shipped binary that includes `JsonSecuritySerializationImpl.h`, so build it with and without `BL_USE_OPENSSL_1X=1` |
| H-2 | the two `utf_baselib_data` builds/runs above |
| clang | repeat `utf_baselib_http` and `utf_baselib_data` with `TOOLCHAIN=clang` (debug) |

Run the full matrix once after all three changes are in the tree (the 1.1.1w `utf_baselib_http`
row only proves the new TLS floor once the H-3 change is applied). Test runs may go up to 5
modules in parallel; builds stay at `-j1`.
