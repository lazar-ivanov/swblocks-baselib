# Deep-dive review of the `lazari2` C++ changes: findings and status record

**Date:** 2026-09-05
**Scope:** every C++ change on `lazari2` against `master` (merge base `18cc4b11`, head `ced5d02`:
100 files, +14 667 / -945), with emphasis on the JSON abstraction layer, the ExecutionQueue
notification rework, TLS and security, and the OpenSSL 3 upgrade. Three subsystem passes, every
High and Medium item re-verified against the pinned OpenSSL 3.5.4 and 1.1.1w sources, the Boost
1.90 headers, json-spirit 4.08 and the local `openssl` CLI. Items already carrying a written
decision under `notes/plans/issues/` were excluded and are listed at the end.
**Decision:** every recommendation was approved as-is on 2026-09-05 and implemented in one pass;
nothing committed by the implementer. The plan copy lived in the planning directory.

**Verdict of the review.** No High-severity defect. The branch is secure by default where it
matters most: TLS 1.2 floor on every OpenSSL, security level 2 pinned and asserted, fail-closed
peer verification, correct and RFC 6125-strict IP-literal dispatch, version-independent PKCS#8 and
SPKI emission, private key checked at server start, no untrusted input logged above trace, parse
depth bounded on Boost.JSON, sound lock discipline in the queue. Eight Medium items, a set of Lows
and Info items, all dispositioned below.

This record exists so that none of these findings resurfaces as new.

---

## Dispositions

C = crypto/TLS, J = JSON/data model, Q = queue/core. Severity is the lead reviewer's.

| Finding | Disposition | Where | Test |
|---|---|---|---|
| **C1 (Medium)** On OpenSSL 3.x the `Full` key check called `EVP_PKEY_private_check`, which for RSA verifies only `1 <= d < n` (`rsa_kmgmt.c:404-409` → `ossl_rsa_sp800_56b_check_private`); the comment and the B8 record claimed a full check, which 1.1.1 really did | **Fixed**: `EVP_PKEY_check` for `Full` on every version (selects the key pair → `rsa_validate_keypair_multiprime`), `EVP_PKEY_public_check` for `PublicOnly`; comment rewritten; correction blockquote in `pr-review-residual-cxx-findings-plan.md` B8 | `security/JsonSecuritySerializationImpl.h` | `PemKeyFormats_InconsistentPrivateKeyIsRejected` with the new `test-private-key-inconsistent.pem` (one bit of `exponent1` flipped, re-encoded as PKCS#1; `openssl rsa -check` reports "dmp1 not congruent to d") |
| **C2 (Medium)** The comment claimed 3.x rejects small public exponents; `ossl_rsa_check_public_exponent` bounds `e` only under `FIPS_MODULE`, the default provider accepts any odd `e > 1` (reproduced: `e = 3` passes `pkey -pubcheck`) | **Fixed**: `chkPublicExponentIsAcceptable()` enforces odd and `> 2^16` on every OpenSSL, reading `e` through `EVP_PKEY_get_bn_param` on 3.x and the structure member on 1.x (no new `RSA_*` site; EVP deferral record annotated); `SecurityException`, clean error queue | same header | `PemKeyFormats_SmallPublicExponentIsRejected` with `test-private-key-e3.pem` / `test-public-key-e3.pem` (`openssl genrsa -3`) |
| **C3 (Medium)** Encrypted PKCS#8 export used `PKCS5_DEFAULT_ITER = 2048` PBKDF2 iterations (fixture confirmed `INTEGER :0800`) | **Fixed**: the PBES2 envelope is built with `EVP_PKEY2PKCS8` + `PKCS8_encrypt( iter = 600000 )` + `PEM_write_bio_PKCS8`; plaintext export unchanged; readers unchanged; release note §1 | same header (`PKCS8_PBKDF2_ITERATIONS`) | `PemKeyFormats_EncryptedExportUsesStrongKeyDerivation` (DER of the count pinned, default absent, round trip) |
| **C4 (Medium)** TLS 1.2 cipher list admitted CBC suites with SHA-1/256/384 HMACs (16 of 35 resolved) against a "modern bulk cipher" comment | **Fixed**: `EECDH+AESGCM:EDH+AESGCM` plus the denials (aliases valid on 1.0.2 through 3.x); 11 suites remain, all AEAD; release note §6 | `crypto/CryptoBase.h` | `TlsProtocolPolicy_CipherSuitesAreAeadOnly` (every suite from `SSL_CTX_get_ciphers` is `SSL_CIPHER_is_aead`, no `kRSA`, no `aNULL`); `HardeningOptionsArePinned` also pins `NO_SSLv3`, `NO_TLSv1`, `NO_TLSv1_1`, `NO_TICKET` |
| **C5 (Medium, doc)** `TlsPeerVerification.h`, release note §5a and the opus5 plan stated Boost's `host_name_verification` cannot match IP literals; Boost 1.90's `host_name_verification.ipp:46-56` dispatches on `ip::make_address` | **Fixed** (text): header premise rewritten (the class owns the dispatch across devenv2-7 and adds the flags); §5a carries a dated correction; §5 table row corrected | `crypto/TlsPeerVerification.h`, release notes | none (doc) |
| **J1 (Medium)** Boost.JSON `parse_options::numbers` left at `imprecise` (two roundings, up to 1 ULP off for long literals); undocumented; pass-through payloads could re-serialize differently and hash differently per hop | **Fixed**: `number_precision::precise`; contract paragraph "DOUBLE PRECISION, backend-defined" in `JsonUtils.h` (json-spirit stays a few ULPs off, documented); release note §9 | `core/detail/BoostJsonImpl.h`, `core/JsonUtils.h` | `JsonParseDoublesAreCorrectlyRounded` (exact vs `strtod` on Boost, 8-ULP tolerance on json-spirit) |
| **Q1 (Medium)** `task -> continuationTask()` runs the `setContinuationCallback` body under `m_lock` (`ExecutionQueueImpl.h` `onReady()`), omitted from the branch's "user code under m_lock" lists; with the accessors now locking, a continuation callback calling `size()` self-deadlocks where it worked on `master` | **Fixed** (documented, hoisting rejected: admission must stay atomic with completion): contract on `setContinuationCallback()` in `TaskBase.h`, the list in `ExecutionQueueImpl.h`, the breaking-change record | `tasks/TaskBase.h`, `tasks/ExecutionQueueImpl.h`, `execution-queue-notification-delivery-breaking-change.md` | existing `Tasks_TaskContinuationsTests` cover the permitted shape (return a stored task) |
| **Q2 (Medium)** `HttpServerBackendMessagingBridge::completeRequest()` passed no `ignoreIfDisposed`, so a response arriving while `dispose()` drains the backend threw `UnexpectedException` from the dispatch task; `closeRequest()` already passed `true` | **Fixed**: `true /* ignoreIfDisposed */` with a comment | `rest/HttpServerBackendMessagingBridge.h` | not added: a dispose-with-inbound-in-flight case needs a driven REST server and backend; recorded as an open gap below |
| C6 (Low) `SSL_library_init()` return discarded on 1.1.x, where it is `OPENSSL_init_ssl( 0, NULL )` and can fail (`OPENSSL_API_COMPAT` is only defined on the 3.x branches) | **Fixed**: checked under `>= 1.1.0`, `( void )` kept for 1.0.x, comments corrected | `crypto/CryptoBase.h` | 1.1.1w build and run |
| C7 (Low) Locking-callback block guarded `< 3.0`; on 1.1.x `CRYPTO_num_locks()` is `(1)` and `CRYPTO_set_locking_callback` a no-op macro, so a mutex was allocated for nothing | **Fixed**: guard `< 1.1.0`, comment corrected | same | 1.1.1w build |
| C8 (Low) CN fallback enabled (`X509_CHECK_FLAG_NEVER_CHECK_SUBJECT` unset): a SAN-less certificate authenticated on its common name, refused by CABF since 2017 and by browsers | **Fixed** (decided: secure by default): the flag is set; the wildcard test now builds SAN-bearing certificates; release note §5d | `crypto/TlsPeerVerification.h`, `TestTlsPeerVerification.h` (`addSubjectAltName` helper) | `TlsPeerVerification_CommonNameIsNeverConsulted`; `PartialWildcardsDoNotMatch` rewritten on SANs |
| C9 (Low) SNI sent for IP-literal hosts (RFC 6066 §3 forbids it) | **Fixed**: no `SSL_set_tlsext_host_name` when the host parses with `ip::make_address` (`address::from_string` below Boost 1.66); release note §6 | `tasks/TcpSslBaseTasks.h` | the IP handshake case in `TestTlsHandshakeVerification.h` still passes; no capture-level SNI assertion (gap) |
| C10 (Low) `g_allowUntrustedCertificates` a plain `bool` read on IO threads | **Fixed**: `std::atomic< bool >` | `crypto/CryptoBase.h` | compile-proof |
| J2 (Low) `BL_DM_DECLARE_COMPLEX_MAP_PROPERTY` lacked the `is_null()` guard its siblings have | **Fixed** | `data/DataModelObjectDefs.h` | `DataModelNullComplexCollectionsMeanAbsent` (both backends) |
| J3 (Low) `value_to< int64_t >` / `get_int64` diverged for an in-memory `uint64` that fits: json-spirit's wrapper refused any unsigned kind (introduced by the M-3 fix), Boost's `get_int64` refused it through `as_int64()` | **Fixed** on both: the unsigned kind is accepted when `<= INT64_MAX`, refused as out of range above | `core/detail/JsonSpiritImpl.h`, `core/detail/BoostJsonImpl.h` | `JsonNumericSmallUint64ReadsAsInt64` (both backends) |
| J4 (Low) json-spirit wrote non-finite doubles as `inf`/`nan` (invalid JSON); Boost as `1e99999`/`null` | **Fixed** (decided: reject on both): `chkNoNonFiniteDoubles()` walks the tree before serialization and throws `JsonException`; contract note in `JsonUtils.h`; release note §9 | both impl headers, `core/JsonUtils.h` | `JsonSerializeRejectsNonFiniteDoubles` (every entry point, nested, NaN, and the `1e400` parse either rejected or refused on output) |
| J5 (Low) `loadFromJsonText` / `loadFromJsonValue` / the string context constructor called `as_object()` on a non-object top level, surfacing the backend's own error | **Fixed**: `BL_CHK_T( is_object(), JsonException, "must be an object at the top level" )` at the three sites | `data/DataModelObject.h` | `DataModelLoadRequiresObjectAtTopLevel` (both backends) |
| J6 (Low, perf) four `std::string( BL_JSON_PAIR_KEY( pair ) )` temporaries per key in `BL_DM_PROPERTIES_IMPL_HANDLE_UNMAPPED` on the message hot path | **Fixed** (three of four): `find`/`emplace` take the key as is on both backends; the `containsProcessedProperty` lookup keeps its temporary (`std::unordered_set< std::string >` has no heterogeneous lookup before C++20) | `data/DataModelObjectDefs.h` | compile-proof on both backends; no benchmark isolates it |
| J7 (Low, doc) `saveToStream` documented as avoiding materialization; both backends buffer (R-4) | **Fixed** (text) | `core/JsonUtils.h` | none |
| Q3 (Low) A final `AllTasksCompleted` was lost when work admitted after a candidate formed was removed without completing (pending task cancelled or flushed before scheduling): the generation moved, the candidate was dropped, and no completion could ever follow | **Fixed**: validation now requires the queue empty, no newer *completion candidate* formed since (`m_lastCandidateGeneration`) and nothing newer published; a candidate superseded by a newer completion is still collapsed (the `ObsoleteCandidate` test pins that); contract text extended | `tasks/ExecutionQueueImpl.h`, `tasks/ExecutionQueueNotify.h` | `Tasks_ExecutionQueueAllTasksCompletedSurvivesRemovedAdmissionTest` (fails under the old rule by timing out) |
| Q4 (Low, Plausible) `m_notifyOwner` not cleared if a serialized callback unwinds (`NOEXCEPT` is `throw()` on MSVC < 1916) | **Fixed**: `BL_SCOPE_EXIT` clears it while the lock is still held | `tasks/ExecutionQueueImpl.h` | compile-proof |
| Q5 (Low) `Pinger.h` Darwin arrival matcher lacked the Linux-form fallback the RTT matcher gained | **Fixed** | `tasks/utils/Pinger.h` | `Tasks_PingerMatchersTests` |
| L-6 (Info) `getEventNotifyCB()` wrote its `SAA_out` on one path only | **Fixed**: initialised first | `tasks/ExecutionQueueImpl.h` | compile-proof |
| F-13 (Info) `RSA_KEY_EXPONENT_DEFAULT` unused on the 3.x keygen path | **Fixed**: `EVP_PKEY_CTX_set1_rsa_keygen_pubexp` | `crypto/RsaKey.h` | `PemKeyFormats_EmittedFormatsAreSpkiAndPkcs8` and every generated-key test |

**Accepted / recorded, no change:** `SSL_OP_NO_TICKET` still allows stateful TLS 1.3 resumption
on the server (forward secrecy preserved; comment implication only); session-id context is the
constant `42` (benign, per-context caches); `tryVerify` leaves the error queue dirty (pre-existing,
documented as deliberate); `HmacSha256` has no production consumer and there is no constant-time
compare helper in `src/` (the first authentication-tag consumer needs `CRYPTO_memcmp`); 1.0.2
server contexts may have no usable key exchange without `SSL_CTX_set_ecdh_auto` (pre-existing,
desk-check only, no dist); no CRL/OCSP (pre-existing); `remapIncorrectValueTypeException` labels
`not_int64` as "expected an integer"; dead `BL_ASSERT( true )` and the inert `jsonGetter` macro
parameter; `escapeControlCharacters` copies every document on json-spirit; `io_context_compat`
lacks an `#error` guard against a future Boost reinstating `io_service`; `BL_SAFE_MEMSET` gated on
GCC 15 while `-Wclass-memaccess` exists since GCC 8; `TestBoostAsioCompat.h` runs `io_context`
without a deadline.

## Coverage gaps still open after this round

- A bridge dispose with an inbound response in flight (Q2).
- SNI presence and absence asserted at capture level (C9).
- A chain with a 1024-bit or SHA-1 intermediate refused at level 2; verify callback at depth > 0
  through a real multi-level handshake; `allowUntrustedCertificates( true )` positive path; IPv6
  shapes (`[::1]`, zone id, IPv4-mapped); wrong password vs corrupt PEM distinguishable; the
  `PEM_read_bio_RSAPublicKey` fallback with a non-RSA key; `EVP_MAC_fetch` failure; RFC 4231
  cases 4, 5, 7; TLS 1.3 negotiated (`SSL_version == TLS1_3_VERSION`).
- Every §10 divergence on the `readFromStream` path; lone surrogate `\ud800`; depth boundary
  exactly 512/513; strings beyond `MAX_DUMP_STRING_LENGTH`; empty key `{"":1}`; the round-trip
  assertions in `TestJsonPerformance.h` only run under `isClient()`.
- Throttle combined with `AllTasksCompleted`; a second thread parked on `m_lockNotify` while the
  disposing thread's serialized callback runs; `setNotifyCallback` re-registration during a
  serialized delivery; `utf8ToUtf16` astral-plane and embedded NUL; `copyDirectoryWithContents`
  skip of FIFO and dangling link.

## Excluded, already decided

Duplicate keys backend-defined; json-spirit unbounded depth; manual dual-backend check; process-local
hashes; key order and double text per backend (§9); `saveToStream` buffering (R-4); canonicalize
semantics; platform trust store not loaded (M-5) and the bundled root set; no TLS 1.0/1.1 opt-in;
`RSA_*` sites under `OPENSSL_API_COMPAT`; `g_rfc2818VerifyCallback` opt-out; no zeroization; PEM
format change (§1); multi-label wildcard (§5b); resolver shim not a completion-token adapter;
`BOOST_ASIO_DISABLE_STD_CHRONO` and `-Woverloaded-virtual`; move assignment deletions;
`copyDirectoryWithContents` skip policy; mt19937 UUID generator; the `AsyncExecutor` task-lock TSan
inversion; the same-thread re-entry abort; `devenv-detect.mk` `win7` rename.

---

## Verification

Host: Ubuntu 24.04 aarch64, devenv7 dist (clang 20.1 = default `clang2010`, gcc 15.2 =
`TOOLCHAIN=gcc1520`, Boost 1.90, OpenSSL 3.5.4 and 1.1.1w, json-spirit 4.08). Every build `-j1`,
every module run through `make test_<module>`; "ok" means the build succeeded and the run reported
no errors. **Note for the next person:** `BL_USE_OPENSSL_1X=1` shares the build directory with the
3.5.4 build and nothing forces a rebuild when the dist changes, so the module objects were removed
before and after the 1.1.1w builds; the json-spirit release objects were removed afterwards per
`json-backend-verification-decision.md`.

| Module | clang debug 3.5.4 | clang debug 1.1.1w | other |
|---|---|---|---|
| `utf_baselib_security` | ok, 30 cases | ok, 30 cases | new cases run by name on both |
| `utf_baselib_http` | ok, 29 cases (11 offered suites, all AEAD) | ok, 29 cases | new cases run by name on both |
| `utf_baselib_data` | ok, 88 cases | — | new cases run by name; ok with `VARIANT=release BL_USE_JSON_SPIRIT=1` (88 cases, every new case runs on json-spirit too); ok clang release; ok gcc debug |
| `utf_baselib_tasks` | ok, 54 cases | — | new case run by name; ok gcc debug |
| `utf_baselib_http` (again) | — | — | ok clang release; ok gcc debug |
| `utf_baselib_security` (again) | — | — | ok gcc debug |
| `utf_baselib_messaging`, `utf_baselib_rest` | ok, 27 / 5 cases (smoke after the TLS and data model changes) | — | — |
| `bl-tool` | ok | ok | the shipped includer of the key loaders |

Fixtures: `test-private-key-inconsistent.pem` generated by re-encoding the components of
`test-private-key.pem` with one bit of `exponent1` flipped (a 40-line DER writer, not committed);
`test-private-key-e3.pem` / `test-public-key-e3.pem` by `openssl genrsa -3 2048` and `pkcs8` /
`rsa -pubout`.
