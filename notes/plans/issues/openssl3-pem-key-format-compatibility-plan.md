# Plan: Fix F-08 OpenSSL 3 PEM Key Format Compatibility

TL;DR: The branch changed the RSA PEM encoding as a side effect of the OpenSSL 3 migration, so the same public method emits different bytes and different PEM labels depending on which devenv built the library. Make the emitted format a property of the library rather than of the build environment (SPKI public, PKCS#8 private, on every OpenSSL version), make both readers accept the legacy PKCS#1 forms forever, clear the OpenSSL error queue on failed read attempts, and upgrade private-key encryption from 3DES to AES-256. Lock all of it with golden fixtures in both formats.

## Correction to the finding as written

F-08 states that the OpenSSL 3 loader "has no PKCS#1 fallback" and that the repository's `test-public-key.pem` fixture will therefore stop loading on devenv7. **That headline scenario does not reproduce.** Verified empirically against OpenSSL 3.0.13:

```
PEM_read_bio_PUBKEY on '-----BEGIN RSA PUBLIC KEY-----' : ACCEPTED
   type=RSA  bits=2048
   re-emitted as: -----BEGIN PUBLIC KEY-----
```

In OpenSSL 3.x, `PEM_read_bio_PUBKEY` is implemented over `OSSL_DECODER`, which tries the `type-specific` (PKCS#1) structure in addition to `SubjectPublicKeyInfo`. It is strictly *more* tolerant than the 1.x implementation, which was `d2i_PUBKEY`-based and SPKI-only. The reviewers correctly described 1.x semantics and incorrectly assumed they carried forward into 3.x. The `RsaEncryption_*` cases in `src/utests/utf_baselib_security/TestCryptoUtils.h` will pass on devenv7 as-is.

The finding nonetheless identifies three real defects, one of which points in the **opposite** direction from the write-up:

1. **The interop break is 1.x-reads-3.x-output, not 3.x-reads-1.x-output.** A devenv2–6 build calls `PEM_read_bio_RSAPublicKey` / `PEM_read_bio_RSAPrivateKey`, both PKCS#1-only. It cannot read the SPKI public key or the PKCS#8 private key that a devenv7 build now writes. Confirmed by inspection of `JsonSecuritySerializationImpl.h:481` and `:414`.
2. **The emitted format is an unversioned, silent function of the build environment.** Same library, same method, different wire bytes and different PEM labels. This is the substantive objection and it stands.
3. **The OpenSSL error queue is left dirty after a failed read, and stale entries survive into later operations.** Verified:

```
pkcs1 public       PUBKEY=ACCEPT   errq_after=clean
garbage            PUBKEY=REJECT   errq_after=DIRTY
good read while queue dirty  = ACCEPT
queue still dirty afterwards = YES (stale errors leak)
```

Because `BL_CHK_CRYPTO_API` builds its message from the queue, a later unrelated failure can be reported with a stale reason string. `BL_CHK_CRYPTO_API_RESET_ERROR()` is defined at `src/include/baselib/crypto/ErrorHandling.h:31` and is currently called from nowhere in the tree.

Verification environment was OpenSSL 3.0.13; devenv7 pins 3.5.4. The decoder only gained structures between those releases, so a regression is very unlikely, but the new fixture test proves it on the real toolchain rather than relying on that assumption.

## Confirmed decisions

- **Emit modern formats on every OpenSSL branch.** SPKI (`BEGIN PUBLIC KEY`) for public, PKCS#8 (`BEGIN PRIVATE KEY` / `BEGIN ENCRYPTED PRIVATE KEY`) for private, identical on devenv2 through devenv7. This removes the "different bytes per devenv" defect without reverting to PKCS#1, and aligns with the goal of retiring OpenSSL 1.x rather than entrenching compatibility with it.
- **Readers accept legacy PKCS#1 permanently.** Keys already written to disk by older builds outlive the toolchain. Dropping devenv2–5 from the makefile does not delete a user's existing `key.pem`. This is not a transitional shim and must not be removed when 1.x support is dropped.
- **Upgrade the private-key cipher to AES-256-CBC in this change.** `EVP_des_ede3_cbc()` → `EVP_aes_256_cbc()`. It touches the same lines and alters the same bytes as the format move, so doing it together means one migration instead of two. Note that moving to PKCS#8 already replaces the legacy `DEK-Info` KDF (MD5, one iteration, 8-byte salt) with PBES2/PBKDF2; confirmed on the generated fixture as PBKDF2 + hmacWithSHA256 + 2048 iterations.
- **Keep the existing PKCS#1 fixtures as permanent legacy regression vectors.** Do not regenerate `test-public-key.pem` or `test-private-key.pem` in place. They are the only thing that keeps the fallback path exercised.
- **Scope is F-08 only.** Do not touch the `RsaKey`/`RSA*` design, the JOSE JSON paths, `RsaSignVerify`, TLS protocol floors, the `OPENSSL_API_COMPAT` macro, or `bl-tool` command-line surface. Do not mix style changes.

## Why the `#if` gates can go away entirely

Every API needed for the modern path exists on all supported OpenSSL versions — `EVP_PKEY_new`, `EVP_PKEY_set1_RSA`, `EVP_PKEY_get1_RSA`, `PEM_write_bio_PUBKEY`, `PEM_write_bio_PrivateKey`, `PEM_read_bio_PUBKEY`, `PEM_read_bio_PrivateKey`, `PEM_read_bio_RSAPublicKey`, `EVP_PKEY_base_id`, `EVP_aes_256_cbc`. So all four PEM functions in `JsonSecuritySerializationImpl.h` lose their `OPENSSL_VERSION_NUMBER >= 0x30000000L` split. The fix is a net **reduction** in conditional code and directly advances the 1.x retirement.

`PEM_read_bio_RSAPublicKey` is deprecated-in-3.0 but available under the `-DOPENSSL_API_COMPAT=0x10100000L` already set at `projects/make/devenv-detect.mk:244,259`. This adds no new deprecation debt: `RsaKey.h`, `RsaEncryption.h`, `RsaSignVerify.h`, `X509Cert.h`, `OpenSSLTypes.h` and this file already depend on `RSA_new`, `RSA_generate_key_ex`, `RSA_size`, `RSA_sign`, `RSA_verify`, `RSA_public_encrypt`, `RSA_get0_key`, `RSA_set0_key`, `EVP_PKEY_{get,set}1_RSA` and friends. Removing that macro requires rewriting `RsaKey` onto `EVP_PKEY`/`OSSL_PARAM`, which is a separate program of work; the PEM functions are not what blocks it.

## Steps

All changes are in `src/include/baselib/security/JsonSecuritySerializationImpl.h` unless stated otherwise. Use the Edit tool, not Write.

### Phase 1 — shared helper

1. Add a private static `createMemoryBio( pemKeyText )` returning `crypto::bio_ptr_t`, wrapping the `BIO_new_mem_buf` + `BL_CHK_CRYPTO_API_NM` pair currently duplicated at `:368-375` and `:435-442`. The public loader needs a **second, fresh** BIO for its fallback attempt; creating one sidesteps `BIO_reset` return-value semantics on a read-only memory BIO entirely.
2. Do **not** factor out the duplicated random-password block at `:377-394` / `:444-461`. It is not needed by this fix and would widen the diff. Its behaviour must be preserved verbatim — a probe confirmed that passing a null password callback *and* null userdata makes OpenSSL prompt on the terminal for an encrypted key, which is exactly what that block prevents.

### Phase 2 — writers (`getPublicKeyAsPemString`, `getPrivateKeyAsPemString`)

3. In `getPrivateKeyAsPemString` (`:183-227`), delete the `#if`/`#else`/`#endif` and keep only the EVP path: `EVP_PKEY_new` → `EVP_PKEY_set1_RSA` → `PEM_write_bio_PrivateKey`. Change the cipher argument from `::EVP_des_ede3_cbc()` to `::EVP_aes_256_cbc()`. Keep the `password.empty() ? nullptr : ...` conditional for both the cipher and the password argument, so an empty password still writes an unencrypted key. Update the trailing comment `/* Triple DES encryption */` to name AES-256.
4. In `getPublicKeyAsPemString` (`:229-259`), delete the `#if`/`#else`/`#endif` and keep only `EVP_PKEY_new` → `EVP_PKEY_set1_RSA` → `PEM_write_bio_PUBKEY`.
5. Replace the now-stale `OpenSSL 3.x+: Use EVP_PKEY-based PEM write functions` comments with a comment stating the emitted format is SPKI / PKCS#8 on all OpenSSL versions **by deliberate choice**, that legacy PKCS#1 input is still accepted on read, and that changing the emitted format is a breaking change requiring a release note.

### Phase 3 — private reader (`loadPrivateKeyFromPemString`)

6. Delete the `#if`/`#else`/`#endif` at `:396-424` and keep only the EVP path: `PEM_read_bio_PrivateKey` → `EVP_PKEY_get1_RSA`. This is a one-branch deletion that by itself fixes the 1.x-cannot-read-PKCS#8 gap, because `PEM_read_bio_PrivateKey` already accepts PKCS#1, PKCS#8 and encrypted PKCS#8 on every supported version. Verified: `PEM_read_bio_PrivateKey(PKCS#1) = OK`, `(encrypted pkcs8) = OK`, `(wrong pw) = NULL`.
7. Add an `EVP_PKEY_base_id( pkey ) == EVP_PKEY_RSA` check before `EVP_PKEY_get1_RSA` so a non-RSA key fails with a clear error rather than a bare null.

### Phase 4 — public reader (`loadPublicKeyFromPemString`)

8. Replace the `#if` block at `:463-491` with a two-attempt sequence, no version gate:
   - Attempt 1: `PEM_read_bio_PUBKEY` on a fresh BIO. On success, validate `EVP_PKEY_base_id == EVP_PKEY_RSA`, then `EVP_PKEY_get1_RSA`. This handles SPKI on all versions, and additionally handles PKCS#1 on 3.x where the decoder is multi-structure.
   - Attempt 2, only if attempt 1 yielded nothing: call `BL_CHK_CRYPTO_API_RESET_ERROR()`, build a **fresh** BIO via the Phase 1 helper, then `PEM_read_bio_RSAPublicKey`. This is the path that matters on 1.x, where `PEM_read_bio_PUBKEY` is SPKI-only.
   - `BL_CHK_CRYPTO_API_NM( rsa )` once at the end, after the fallback.
9. Keep passing `passwordBytes` to both calls. Neither format is ever encrypted, but the bogus-password guard must stay for the reason in step 2.
10. Comment the sequence: state that attempt 1 is expected to fail for legacy input on OpenSSL 1.x, that the error queue must be cleared before attempt 2 or the eventual exception message will carry a stale reason, and that a fresh BIO is used rather than rewinding.

### Phase 5 — fixtures

11. Add to `src/utests/utf_baselib_security/data/`, all derived from the **existing** key so components can be compared across formats. Commands verified to produce the expected labels:

```
openssl rsa   -RSAPublicKey_in -in test-public-key.pem -pubout \
              -out test-public-key-spki.pem
openssl pkcs8 -topk8 -nocrypt -in test-private-key.pem \
              -out test-private-key-pkcs8.pem
openssl pkcs8 -topk8 -v2 aes-256-cbc -in test-private-key.pem \
              -passout pass:Password1 -out test-private-key-pkcs8-enc.pem
```

12. Leave `test-public-key.pem` and `test-private-key.pem` byte-identical. No makefile change is needed: `projects/make/common.mk:503-509` globs `data/*` and copies it to `utf-baselib-security-data/`.

### Phase 6 — tests

13. New `src/utests/utf_baselib_security/TestPemKeyFormats.h`, registered with one `#include` in `src/utests/utf_baselib_security/UtfBaselibSecurityMain.cpp` (append after the existing `TestCryptoUtils.h` line). Compare keys through `getPublicKeyAsJsonObject(...)->modulus()` / `->exponent()` and `getPrivateKeyAsJsonObject(...)->privateExponent()`; load fixtures with the existing `utest::TestUtils::loadDataFile`.
    - Legacy PKCS#1 public fixture loads; SPKI public fixture loads; both yield identical modulus and exponent.
    - Legacy PKCS#1 private fixture loads; PKCS#8 private fixture loads; both yield identical modulus, exponent and private exponent, matching the public fixtures.
    - Encrypted PKCS#8 fixture loads with `Password1` and yields the same components; loading it with a wrong password throws `bl::SystemException`.
    - Emitted-label assertions that pin the format decision: `getPublicKeyAsPemString` starts with `-----BEGIN PUBLIC KEY-----`; `getPrivateKeyAsPemString` with an empty password starts with `-----BEGIN PRIVATE KEY-----`; with a password, `-----BEGIN ENCRYPTED PRIVATE KEY-----`. All three labels confirmed by probe against OpenSSL 3.0.13.
    - Round-trip: generate a key, write each form, read it back, assert components survive.
    - Error-queue hygiene regression: attempt to load malformed PEM text, assert it throws, then assert a subsequent load of a **valid** fixture still succeeds. This is the direct test for step 8's `BL_CHK_CRYPTO_API_RESET_ERROR()`.
14. Do not modify `TestCryptoUtils.h`. Its `RsaEncryption_*` cases keep consuming the PKCS#1 fixture and are now the incidental coverage of the legacy read path.
15. Do not modify `src/utests/include/utests/baselib/TestUtils.h`. Its `getRsaKeyFromString` heuristic keys off the substring `PRIVATE`, which routes `BEGIN PUBLIC KEY`, `BEGIN RSA PUBLIC KEY`, `BEGIN PRIVATE KEY`, `BEGIN RSA PRIVATE KEY` and `BEGIN ENCRYPTED PRIVATE KEY` correctly.

### Phase 7 — documentation

16. Record the compatibility change where release notes for this branch are collected: public export moves PKCS#1 → SPKI, private export moves PKCS#1 → PKCS#8, private-key encryption moves 3DES/MD5-KDF → AES-256/PBES2, and **all** legacy formats continue to load. Call out that a key exported by devenv7 `bl-tool rsakeyexport --pemformat` cannot be imported by a pre-fix devenv2–6 build, and that after this change every devenv emits the same bytes.

## Files touched

| File | Change |
| --- | --- |
| `src/include/baselib/security/JsonSecuritySerializationImpl.h` | All four PEM functions; net removal of four `#if` gates |
| `src/utests/utf_baselib_security/data/test-public-key-spki.pem` | New fixture |
| `src/utests/utf_baselib_security/data/test-private-key-pkcs8.pem` | New fixture |
| `src/utests/utf_baselib_security/data/test-private-key-pkcs8-enc.pem` | New fixture |
| `src/utests/utf_baselib_security/TestPemKeyFormats.h` | New test file |
| `src/utests/utf_baselib_security/UtfBaselibSecurityMain.cpp` | One `#include` |

No change to `bl-tool`: `CryptoRsaKeyExport.h` and `CryptoRsaKeyCommandBase.h` call the getters and loaders by their existing signatures, which are unchanged. No change to `X509Cert.h`, whose `getEvpPkeyAsPemString` already writes unencrypted PKCS#8 and is a separate code path.

## Verification

```bash
# primary: devenv7 / OpenSSL 3.5.4
make -k -j4 utf_baselib_security
make test_utf_baselib_security

# release variant
make -k -j4 utf_baselib_security VARIANT=release
```

Then, because the point of the change is cross-environment agreement, build and run the same target on **one** OpenSSL 1.x environment — devenv6 with `BL_USE_OPENSSL_1X=1` is the cheapest, since devenv7 also accepts that flag:

```bash
make -k -j4 utf_baselib_security BL_USE_OPENSSL_1X=1
make test_utf_baselib_security BL_USE_OPENSSL_1X=1
```

The label-assertion cases in step 13 are what actually prove the goal: they must produce identical PEM labels under both builds. If they diverge, the emitted format is still environment-dependent and the change has not achieved its purpose.

Cross-build interop check, done by hand once:

```bash
# write with the OpenSSL 3 build, read with the OpenSSL 1.x build, and vice versa
bl-tool crypto rsakeygenerate --pemformat --path key.pem
bl-tool crypto rsakeyexport --sourcepemformat --sourcepath key.pem \
        --publickeyonly --destinationpemformat --destinationpath pub.pem
```

Both directions must succeed after the fix, and the second direction is the one that fails today.

Finally: `git diff --check master...lazari2`, and confirm the diff contains no style-only churn and no changes outside the six files listed above.
