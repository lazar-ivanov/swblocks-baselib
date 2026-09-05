# devenv7 Breaking Changes — Release Note Material

Collected behavioural and source-compatibility changes on `lazari2` that a consumer of this library
must be told about. This is the staged material for whatever release vehicle is used; it is not
itself a shipped document.

Tracked as R-20 / S-E in `notes/plans/issues/pr-review-opus5-residual-findings-plan.md`, and carried
forward from the "Still open — Release notes" row of
`notes/plans/issues/pr-review-gpt56sol-f01-f18-status.md`.

Each entry states what changed, who is affected, and how the break presents — a compile error, a
runtime behaviour change, or a data/interop change. That last distinction is the one that matters
most to a reader deciding whether they are exposed.

---

## 1. Public and private key PEM formats changed on OpenSSL 3 builds

**Presents as:** an interop / data change. Silent for readers, visible for writers.

`src/include/baselib/security/JsonSecuritySerializationImpl.h`

| | Before (OpenSSL 1.x) | After (OpenSSL 3.x) |
|---|---|---|
| Public key **write** | PKCS#1 — `-----BEGIN RSA PUBLIC KEY-----` | **SPKI** — `-----BEGIN PUBLIC KEY-----` (`:292`) |
| Private key **write** | PKCS#1 | **PKCS#8** (`:256`) |
| Private key encryption | 3DES-CBC | **AES-256-CBC** (`:259`) |

**Reading is compatible in both directions.** The public-key loader tries `PEM_read_bio_PUBKEY`
first and falls back to `PEM_read_bio_RSAPublicKey` (`:530`, `:553`), so keys written by an older
build still load. `PEM_read_bio_PrivateKey` already accepted PKCS#1, PKCS#8 and encrypted PKCS#8.

**Who is affected:** anyone whose tooling or peers parse the *written* form with something stricter
than OpenSSL — a PKCS#1-only parser will not read the new public keys. Keys already at rest are
unaffected.

**Key derivation and import checks (2026-09-05).** An encrypted private key export now derives its
key with 600 000 PBKDF2 iterations instead of OpenSSL's default of 2 048 (PBES2, AES-256-CBC,
HMAC-SHA256 on OpenSSL 1.1.0+); readers need no change, the count travels in the file, and an
export takes a fraction of a second longer. On import, every loader now refuses a key whose public
exponent is even or not larger than 2^16 (a `SecurityException`, on every OpenSSL version), and the
PEM private key loader runs the full key pair check (`EVP_PKEY_check`) on OpenSSL 3.x as it already
did on 1.1.1, so a key with corrupted or forged CRT parameters is refused rather than loaded.

See `notes/plans/issues/openssl3-pem-key-format-compatibility-plan.md`.

---

## 2. `ExecutionQueue::setNotifyCallback` gained a mandatory delivery-policy parameter

**Presents as:** a compile error. Deliberately.

`src/include/baselib/tasks/ExecutionQueue.h:124`

```cpp
virtual void setNotifyCallback(
    SAA_in  om::ObjPtr< om::Proxy >&&                   notifyCB,
    SAA_in  const ExecutionQueueNotify::NotifyDelivery  delivery,          // <-- new, mandatory
    SAA_in  const unsigned                              eventsMask = ExecutionQueueNotify::AllEvents
    ) = 0;
```

**The underlying behaviour change is the important part.** In earlier releases the execution queue
held an internal mutex across `onEvent()`, so callbacks for one queue were mutually exclusive and an
observer could be written as if it were single-threaded. That is no longer the default:

- `DeliveryConcurrent` — current default behaviour. Callbacks for one queue may run simultaneously on
  different threads. **The observer must be thread-safe.**
- `DeliverySerialized` — restores mutual exclusion, with the hazards documented on the enumeration
  (it does not order callbacks, does not cover `maxReadyOrExecuting()`, is not a drain barrier, and
  carries a thread-pool starvation risk).

The parameter is mandatory precisely so this cannot be inherited silently — every call site must
state which behaviour it wants.

**Who is affected:** every out-of-tree `ExecutionQueueNotify` implementor. In-repo observers were
audited and are safe.

See `notes/plans/issues/execution-queue-notification-delivery-breaking-change.md`.

---

## 3. Move assignment deleted on `ManifestT` and `PlatformIdentityT`

**Presents as:** a compile error.

- `src/include/baselib/loader/Manifest.h:101`
- `src/include/baselib/loader/Platform.h:90`

Both types have `const` members, which GCC 15 correctly refuses to assign to inside a template body.
The operators were deleted rather than the types redesigned.

**Who is affected:** downstream code that assigned to one of these types. No caller in this
repository does; both are used exclusively through `om::ObjPtr`, and the copy operations were already
deleted by `BL_DECLARE_OBJECT_IMPL_DEFAULT`.

The deferral record notes two things a reader should know: `PlatformIdentityT`'s move constructor
does not actually move (every member is `const`, so each `std::move` binds to a copy), and `ManifestT`
is only partly immutable. See `notes/plans/issues/residual-cxx-findings-deferral.md`, item 3.

---

## 4. JSON object hashes are process-local and must not be persisted

**Presents as:** no error at all — this is a constraint, not a change you can observe.

`src/include/baselib/data/DataModelObject.h:287-306`

`getObjectHash()` and `getObjectHashCanonical()` produce values that are **not stable across a change
of JSON backend** for any document containing non-ASCII text, or numbers whose shortest
representation differs between the two serializers. The canonical form is a project-specific stable
ordering and is **not** RFC 8785 / JCS.

A hash produced by these functions therefore must not be:

- persisted;
- used as a cache key across processes built differently;
- fed into a signature that another build has to reproduce —

unless the JSON backend is pinned for every participant.

**Who is affected:** anyone storing one of these digests. This is a known and accepted limitation
(F-11, `notes/plans/issues/medium-severity-findings-f11-f17-plan.md`); the deferral is correct
because no in-process consumer persists them, and **keeping that true is the obligation this note
creates.**

---

## 5. TLS peer verification semantics changed on Boost >= 1.89

**Presents as:** a runtime behaviour change. Silent - a handshake that used to succeed now fails.

`src/include/baselib/core/detail/AsioSslCompat.h:41` typedefs Asio's removed `rfc2818_verification`
to `host_name_verification`. These are **not the same implementation**:

| | `rfc2818_verification` (before) | `host_name_verification` (after) |
|---|---|---|
| Matching | RFC 2818, in Asio's own code | delegates to OpenSSL `X509_check_host()` |
| subjectAltName | partial | correct |
| CN when a SAN is present | consulted | **ignored**, per RFC 6125 |
| Embedded NUL in names | accepted | **rejected** |
| **IP addresses** | matched | matched, by Boost 1.89+ and by this library's own dispatch - see 5a |
| **Multi-label wildcards** | matched | **not matched** - see 5b |

The first three rows are a genuine security improvement and are the reason not to revert this.

### 5a. IP address literals - handled by this library on every Boost

**Correction (2026-09-05).** An earlier revision of this note, and the review finding it answered,
stated that `host_name_verification` cannot match IP address literals, so that a peer addressed by
IP had stopped verifying. That was wrong for every Boost version on which the typedef is active:
Boost 1.89 and later (verified against the 1.90 sources) recognize an address literal with
`ip::make_address` and call `::X509_check_ip_asc()` for it. No deployment was failing for that
reason.

What is true is that `src/include/baselib/crypto/TlsPeerVerification.h` now owns the dispatch: it
decides whether the peer name is an address literal through `::X509_check_ip_asc()` itself and uses
`::X509_check_host()` for everything else, so the matching rules are identical across devenv2-7
rather than a property of whichever Asio matcher a build picks up, and they are asserted directly by
`TestTlsPeerVerification.h` in `utf_baselib_http`. `AsioSslStreamWrapper.h` calls it in place of the
bound verifier.

Note for anyone reviewing that code: when the peer name is an address literal the IP result is
**final** and must not fall through to the DNS matcher. RFC 6125 section 6.4 is explicit that an
address literal is not a domain name, and falling through would let a certificate carrying
`DNS:10.11.12.13` authenticate the host at 10.11.12.13. There is a test for exactly that.

### 5b. Multi-label wildcard certificates - NOT fixed, by decision

`::X509_check_host()` permits a wildcard only as the complete leftmost label, per RFC 6125 section
6.4.3. Asio's own matcher was looser and accepted a wildcard in more than one label.

So a certificate whose SAN is `*.*.example.com` now matches **nothing**. This is not hypothetical -
the repository's own `certs/test-server-cert.pem` carries `DNS:*.*.mycompany.com`, and that SAN is
now dead; only its second SAN, `localhost`, is usable.

**This is deliberately not restored.** Multi-label wildcards are non-conformant, and accepting them
again would loosen name verification below what RFC 6125 allows - the opposite of what the switch to
`::X509_check_host()` bought. **A deployment using such a certificate needs a reissued certificate**
with either an explicit SAN per host or a single leftmost wildcard. Asserted by
`TlsPeerVerification_MultiLabelWildcardsDoNotMatch`.

### 5c. Partial wildcards - no longer matched, by decision

`::X509_check_host()` is now called with `X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS`, so a wildcard which
is only part of the leftmost label (`f*.example.com`) matches nothing; the whole-label form
(`*.example.com`) is unaffected. RFC 6125 tolerates the partial form, but the CA/Browser Forum
baseline requirements define a wildcard as a whole leftmost label and public CAs do not issue the
partial one, so only a privately issued certificate can be affected; such a certificate needs to be
reissued with a whole-label wildcard or an explicit SAN. Asserted by
`TlsPeerVerification_PartialWildcardsDoNotMatch`.

### 5d. Common name fallback - disabled, by decision (2026-09-05)

`::X509_check_host()` is now also called with `X509_CHECK_FLAG_NEVER_CHECK_SUBJECT`. Without it,
OpenSSL matches the peer name against the subject common name when the certificate carries no
dNSName subjectAltName at all (when a SAN is present the common name was already ignored, per
RFC 6125). The common name is untyped free text and was never a defined host identity; the
CA/Browser Forum baseline requirements have required a subjectAltName on every server certificate
since 2017 and browsers have ignored the common name since then. Boost's own matcher still falls
back to the common name, so this is stricter than the Asio behaviour on every version.

**Presents as:** a handshake failure against a server certificate which carries no subjectAltName.
Public CAs have not issued such certificates for years, so only a privately issued certificate can
be affected; it needs to be reissued with the host name as a dNSName SAN. Asserted by
`TlsPeerVerification_CommonNameIsNeverConsulted`.

---

## 6. TLS floor fixed at TLS 1.2, OpenSSL security level pinned to 2, legacy opt-in removed

**Presents as:** a compile error for callers of the removed API; a runtime behaviour change
(handshake failure, or server startup failure) for peers and certificates below the floor.

`src/include/baselib/crypto/CryptoBase.h`

| | Before | After |
|---|---|---|
| Minimum protocol | TLS 1.1 by default; TLS 1.0 via `isEnableTlsV10( true )` | **TLS 1.2**, on every supported OpenSSL, not configurable |
| OpenSSL security level (1.1.0+) | whatever the linked OpenSSL was compiled with (1 upstream) | **2**: RSA/DSA/DH >= 2048 bits, ECC >= 224 bits, no RC4, no SSL 3.0, no compression; no SHA-1 signatures on 3.x |
| `CryptoBase::isEnableTlsV10( bool )` | present | **removed** |
| `bl-messaging-broker`, `bl-messaging-http-gateway`, `bl-messaging-echo-server` | relaxed the floor to TLS 1.0 unconditionally at startup | no longer touch the policy |

Note for readers of the intermediate state of this branch: the `TlsMinimumVersion` enum and
`tlsMinimumVersion()` setter which briefly replaced `isEnableTlsV10()` are gone as well; while they
existed, the legacy values also dropped the security level to 0, which accepted MD5-signed and
sub-1024-bit certificates on every connection of the process.

**Who is affected:**

- code calling `isEnableTlsV10()` - delete the call;
- deployments whose peers cannot negotiate TLS 1.2 - the peer needs upgrading, there is no library
  option;
- deployments whose certificate chains carry keys below 2048 bits (or SHA-1 signatures on OpenSSL
  3.x) - clients fail chain verification at handshake; a **server** with such a certificate of its
  own fails at startup, in `createAsioSslServerContext`, because the level is applied before the
  key and certificate are loaded.

Asserted by `TestTlsProtocolPolicy.h` in `utf_baselib_http`. See
`notes/plans/issues/tls-legacy-protocol-opt-in-removal-decision.md`.

**Cipher suites (2026-09-05).** The TLS 1.2 cipher list is now AEAD-only: ephemeral ECDH or DH key
exchange with AES-GCM (`EECDH+AESGCM:EDH+AESGCM`, plus the explicit denials). The previous list
also admitted the AES-CBC suites with SHA-1, SHA-256 and SHA-384 HMACs, which a server could steer a
client to; those are gone. TLS 1.3 suites are unaffected (they are always AEAD and are left at the
OpenSSL default). **Presents as:** a handshake failure against a TLS 1.2 peer which offers no
AES-GCM suite. Asserted by `TlsProtocolPolicy_CipherSuitesAreAeadOnly`, which checks every suite
the contexts offer with `SSL_CIPHER_is_aead`.

**Server name indication (2026-09-05).** A client no longer sends the SNI extension when the host it
connects to is an IP address literal (RFC 6066 section 3 forbids a literal there and a strict server
may abort); peer verification of an IP-addressed server is unaffected, it uses the iPAddress SAN.

---

## 7. json-spirit backend now escapes control characters in strings

**Presents as:** an interop / data change, visible only in the serialized bytes.

`src/include/baselib/core/detail/JsonSpiritImpl.h`

On builds using json-spirit (devenv2-6, or `BL_USE_JSON_SPIRIT=1`), the raw UTF-8 output mode
introduced on this branch copied the control characters U+0000-U+001F into string literals verbatim,
which RFC 8259 forbids and which Boost.JSON (and every conformant parser) rejects. They are now
escaped: `\b \t \n \f \r` in their short forms and the others as six-character escapes with lowercase
hex digits, which is byte-identical to what the Boost.JSON backend emits. `master` escaped them too,
but with uppercase hex digits; the lowercase form was chosen so the two backends agree, and no
in-repo consumer compares or persists the text (item 4).

**Who is affected:** nobody negatively; a devenv2-6 node's messages containing such characters are
parseable again by a devenv7 peer. Asserted by `JsonSerializeEscapesControlCharacters` in
`utf_baselib_data`, on both backends.

---

## 8. Peer certificate verification now fails closed, against a bundled trust set only

**Presents as:** a runtime behaviour change. Silent until a client handshake fails.

`src/include/baselib/crypto/CryptoBase.h:633`, `:774-790`

`CryptoBase::allowUntrustedCertificates()` now defaults to **false**. On `master` it was `true`,
and because the "report it and let the user continue" half of that design was never implemented,
the practical effect was that peer certificate verification was disabled for every client
connection. It is now enforced: a client whose peer chain does not verify fails the handshake, and
`enhanceException` attaches the verification error.

What the chain is verified against is the important part. Trust anchors are **only** the roots
bundled in `TrustedRoots.h` plus whatever the application passes to `registerTrustedRoot()`; the
platform certificate store is **deliberately not consulted on any operating system** (the
`set_default_verify_paths` TODO at `CryptoBase.h:439-444`). The default bundled set is three roots
(VeriSign Class 3, VeriSign Class 3 G5, Entrust G2) and `initAdditionalCommonTrustedRoots()` opts in
four more (DigiCert Global, DigiCert High Assurance EV, GeoTrust Primary, and GeoTrust Global, which
expired in May 2022). Most public endpoints are therefore not trusted out of the box. Note also that
the security level 2 pinned by item 6 refuses the 1024-bit VeriSign Class 3 root as a chain anchor
(`X509_V_ERR_CA_KEY_TOO_SMALL`), so on OpenSSL 1.1.0+ the effective default set is the two 2048-bit
roots; see the trust-store paragraph of the TLS decision record.

**Who is affected:** every client of this library that connects to an endpoint whose issuing root is
not in that set. It connected on `master`; it fails now.

**What to do:** register the issuing root explicitly with `registerTrustedRoot()` (or install a
replacement set through `initGlobalTrustedRootsCallback()`). Do **not** reach for
`allowUntrustedCertificates( true )`: it is process-global and disables chain verification for every
connection, which recreates exactly the state this change removed. Server contexts request no client
certificate, so servers are unaffected. The test suites register `certs/test-root-ca.pem` through
`UtfMain.h`; those fixtures were reissued to 2054 so this default does not turn into a test cliff.

See B6 in `notes/plans/issues/pr-review-residual-cxx-findings-plan.md`.

---

## 9. Serialized JSON text differs between the two backends: key order and double formatting

**Presents as:** an interop / data change, visible only to a consumer that byte-compares, hashes or
stores the serialized text. Every document parses identically on both backends.

`src/include/baselib/core/detail/BoostJsonImpl.h`, `JsonSpiritImpl.h`

| | json-spirit (devenv2-6) | Boost.JSON (devenv7) |
|---|---|---|
| Object key order | sorted (`std::map`) | **insertion order**: declared-property order for data model objects, unmapped properties appended |
| Double text | `setprecision( 17 )` | shortest round-trip form, e.g. `1.5E0` |
| Pretty print | `{\n}`, `"key" : v` | `{}`, `"key": v` |

Canonicalization (`getObjectHashCanonical`, `saveToString( …, canonicalize = true )`) sorts keys
bytewise on both backends but does not normalize number text, so it removes the first difference
only. This is the concrete reason behind item 4: a hash or a byte comparison of the text is only
meaningful between processes built with the same backend.

**Double values, not only their text (2026-09-05).** The Boost.JSON backend parses a double as the
correctly rounded value of its literal (`number_precision::precise`, set explicitly; Boost's default
mode can be one ULP off for literals with more than 17 significant digits, and then a document
passed through unchanged would re-serialize to a different literal). The json-spirit backend uses
Spirit.Classic's floating point accumulation and can be a few ULPs off for such literals; a consumer
which needs the exact value of a long literal on devenv2-6 has to carry it as a string. And a double
which is not finite is now refused with a `JsonException` at serialization on both backends: JSON has
no representation for it, json-spirit used to write the text `inf`/`nan` (invalid JSON) and
Boost.JSON an out-of-range literal or `null`. Such a value can only arise in memory or from a literal
which overflowed on parse (`1e400`). Asserted by `JsonParseDoublesAreCorrectlyRounded` and
`JsonSerializeRejectsNonFiniteDoubles`.

**Who is affected:** anyone comparing serialized documents textually, or persisting the text as a
canonical form. No consumer in this repository does either (traced in the review's §7.1).

---

## 10. Header expectations for consumers who compile with their own flags

**Presents as:** two compile-time changes in public headers, one a fix and one a new diagnostic.

**`BL_DEVENV_VERSION` is no longer required by any public header.** `NetUtils.h` (resolver results
API) and `OpenSSLTypes.h` (the Windows `_InterlockedExchangeAdd` shim) used to select their branch
with a bare `BL_DEVENV_VERSION >= 4`, a macro only the project makefiles define; a consumer building
without it took the legacy branch and, on Boost 1.66+, `getCanonicalHostName` did not compile. Both
now key on `BOOST_VERSION` / `OPENSSL_VERSION_NUMBER`, the same rule the `fs::copy` shim already
followed. Presents as: a compile error that goes away.

**`UuidBoostImports.h` now enforces its include-order requirement.** Boost 1.86+ aligns
`boost::uuids::uuid` to 8 bytes; the header defines `BOOST_UUID_DISABLE_ALIGNMENT` (now
unconditionally) to keep alignment 1, because `uuid_t` members sit inside the 72-byte `CommandBlock`
wire frame of the blob transfer protocol and would otherwise grow it to 80 bytes and desynchronize
the stream. The macro only takes effect if it is defined before the *first* inclusion of
`<boost/uuid/uuid.hpp>` in a translation unit, so a consumer whose own code included that Boost
header before any baselib header was silently compiling a differently laid out `uuid_t`. That is now
a build error (`#error`), and a `static_assert` pins the alignment where the type is defined.

**Who is affected:** consumers whose translation units include `<boost/uuid/uuid.hpp>` (or
`<boost/uuid.hpp>`) before the first baselib header. Fix: include the baselib headers first, or
define `BOOST_UUID_DISABLE_ALIGNMENT` globally in the build. A build that fails here was already
producing mismatched object layouts.
