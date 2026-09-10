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

---

## 11. Whole-library C++ review (2026-09): six user-visible changes

**Source:** `notes/reviews/major/update_2026/whole-library-cxx-review-fable51.md`, implemented in
stages 3-10 on 2026-09-06/07. Everything else in that review is an internal correctness or
robustness fix with no observable contract change; these six are the ones a consumer must be told
about.

### 11.1 Authorization template variables are escaped by default (S-2)

**Presents as:** a data / interop change on the wire between the broker and its authorization
service. Silent unless a token carries a reserved character.

`security/AuthorizationServiceRest.h`, `core/StringTemplateResolver.h`,
`data/models/ServicesConfig.h`

The token text substituted into `urlPathTemplate` and `requestTemplate` used to be spliced in
verbatim, so a token containing `"`, `,` or `}` could add or override fields in the JSON body the
broker sends under its own TLS identity, and one containing a space, `?`, `#` or `/../` could
change the request line. `resolve()` now takes an optional escaper callback, and the REST
authorization service supplies one: **percent-encoding for the URL path template and JSON escaping
for the JSON body.** Tokens containing CR, LF or NUL are rejected up front with a
`SecurityException` instead of being sent.

**Who is affected:** deployments whose authorization tokens contain characters outside the
unreserved URI set, *and* whose authorization service was (knowingly or not) relying on receiving
them raw. The service now receives `%2F` where it used to receive `/`.

**Opt-out:** the new `escapeTemplateVariables` boolean on `AuthorizationServiceRestConfig`. It
defaults to **true** (escape) and, when the property is absent from the configuration, escaping is
on — an existing configuration file gets the new behaviour. Set it to `false` to restore the old
byte-for-byte substitution.

**Amendment (2026-09-08): the body escaper now covers every content type, and an unescapable
content type is refused at startup.**

The first implementation installed the body escaper only when the configured `contentType`
contained `json`, so a deployment posting `application/x-www-form-urlencoded` or `application/xml`
had its request line escaped and its **body left raw** — the very asymmetry S-2 exists to close.
Three escapers are now selected by content type: JSON, `str::uriEncode` for
`x-www-form-urlencoded`, and `&`/`<`/`>`/`"`/`'` entity escaping for `xml`.

**Who is additionally affected:** a deployment whose `contentType` is none of those three *and*
which leaves `escapeTemplateVariables` at its default now **fails to construct the authorization
service**, with an `ArgumentException` naming the content type and both remedies. This is
deliberate — the alternative is client-controlled bytes reaching a structured body unencoded — and
it fails at startup rather than at the first authorization request. Set `escapeTemplateVariables`
to `false` to accept the old behaviour for such a content type.

### 11.2 HTTP server connection timeouts and a connection cap are now on by default (N-2, M-8)

**Presents as:** a runtime behaviour change. Connections that used to be held open forever are now
closed.

`httpserver/HttpServer.h`, `tasks/TcpBaseTasks.h`, `tasks/TcpSslBaseTasks.h`,
`messaging/AsyncExecutorWrapperBlocks.h`

| | Before | After |
|---|---|---|
| Receive / send inactivity (HTTP server) | none | **60 s** (`DEFAULT_CONNECTION_TIMEOUT_IN_SECONDS`), settable with `setConnectionTimeout` |
| TLS handshake and TLS shutdown (every TLS server) | none | **60 s** |
| Concurrent connections (every TCP server) | unbounded | derived cap, ceiling **4096** (`MAX_CONNECTIONS_CEILING`) |
| Outstanding allocated blocks (blob server) | unbounded | capped; over the cap the peer is answered `no_buffer_space` |

The connection cap is `min( 4096, soft descriptor limit / 2, 0.8 × physical RAM / per-connection
footprint )`; the effective value is logged once at startup. An explicitly configured value wins,
and **0 means unbounded**, which restores the old behaviour exactly. The blob server deliberately
has **no** idle timer: auto-push connections are legitimately idle up to their 30 s heartbeat.

**Who is affected:** clients that keep an HTTP connection open with no traffic for more than a
minute, servers fronting more than 4096 concurrent connections, and any deployment that sized its
host for unbounded blob-server buffering. The two new helpers behind the derived cap,
`os::getPhysicalMemorySize()` and `os::getFileDescriptorSoftLimit()`, are tested on UNIX only; on
Windows the descriptor term reports "not applicable" and the cap falls back to the RAM term and the
ceiling (`notes/plans/issues/windows-only-residual-findings-deferral.md`, item 14).

### 11.3 Request header names are normalized to lower case (N-4)

**Presents as:** a source / behaviour change for anyone reading `headers()` off a parsed request.

`httpserver/detail/ParserHelpers.h`

The server-side request parser now lower-cases every header name before storing it, so
`headers().at( "Host" )` no longer finds the entry — use `"host"`. HTTP header names are
case-insensitive by RFC, and the map was previously keyed by whatever case the client happened to
send, which meant a lookup could succeed or fail depending on the client. The parser also now
rejects whitespace before the colon, allows empty values, and caps the request URI at 8192 bytes;
a request carrying `Transfer-Encoding` is rejected (the server implements `Content-Length` framing
only).

**Who is affected:** in-tree, `HttpServerHelpers.h` and two tests were updated. Any consumer
indexing the header map with a capitalized name gets a `std::out_of_range` (or an end iterator) at
runtime; there is no compile error. Fix: lower-case the key.

### 11.4 Six `cpp::function` typedefs lost their `NOEXCEPT` specification (O-9)

**Presents as:** a compile-time change for a consumer that names one of these types explicitly.

`core/CPP.h` (×2), `core/ErrorHandling.h`, `core/ObjModel.h`, `tasks/TaskBase.h`,
`core/AsioSslStreamWrapper.h`, `core/AsyncOperation.h`

A `noexcept` specification inside a *type-id* is ill-formed in C++11 and C++14, and in C++17 it
becomes part of the function type — so the same header would have declared different types
depending on the language level the consumer compiles with. The specification was removed from all
six typedefs. The `noexcept` contract of those callbacks is unchanged; it is enforced where it
always was, by the `BL_NOEXCEPT_BEGIN` / `BL_NOEXCEPT_END` macros in the implementations.

**Who is affected:** a consumer that spelled one of these function types out by hand with
`noexcept` and assigned it across. Compile-proofed on every test module in this repository.

### 11.5 JOSE and JWT array-valued claims changed accessor type (T-10)

**Presents as:** a compile error.

`data/models/Jose.h`, `data/models/Jwt.h`, `data/DataModelObjectDefs.h`

RFC 7515/7517 define these as arrays, and the models declared them as single strings, so a
compliant document either failed to parse or silently kept one element:

| Property | Before | After |
|---|---|---|
| `x5c` (`x509CertificateChain`), `key_ops` (`keyOperations`), `crit` (`critical`) | `std::string` | `std::vector< std::string >` |
| `aud` (`audience`, four claim-set models) | `std::string` | `std::vector< std::string >` |
| `zip` | required | optional |

`aud` uses a new `BL_DM_DECLARE_STRING_OR_ARRAY_ALTERNATE_PROPERTY` macro: it accepts **either** a
single string or an array on the wire, and serializes back as a single string when the vector holds
exactly one element, so a document that used the scalar form round-trips unchanged. `x5c`,
`key_ops` and `crit` accept the array form only, which is what the RFCs specify.

**Who is affected:** any consumer calling those accessors. Fix: index the vector, or use
`.front()`.

### 11.6 HTTP status lines and error bodies (N-9)

**Presents as:** a wire change on error responses.

`http/Globals.h`, `httpserver/Response.h`, `httpserver/HttpServer.h`,
`rest/HttpServerBackendMessagingBridge.h`, `httpserver/ServerBackendProcessingImplDefault.h`,
`data/eh/ServerErrorHelpers.h`

- **Status lines are now truthful.** 429 and 504 gained their cases, and any other code is emitted
  numerically with a generic reason phrase for its class (`getStatusLine`, `genericReasonPhrase`).
  Previously every code without an explicit case was sent as `HTTP/1.0 500 Internal Server Error`
  while the JSON body and `Response::status()` carried the real value — status line, response
  status and body could disagree.
- **Backend failures no longer all report 400.** A processing failure now maps to 500, and a
  timeout to 504.
- **A status a remote backend supplies is validated** to 100..599 and replaced with 502 otherwise,
  instead of being `static_cast` through.
- **HTTP error bodies are redacted.** `getRedactedServerErrorAsJson` blanks
  `exceptionFullDump`, `fileName`, `fileOpenMode`, `functionName`, `taskInfo`, `hostName`,
  `serviceName`, `endpointAddress`, `httpUrl`, `httpRedirectUrl`, `externalCommandOutput` and
  `parserFile` before the error leaves over HTTP. **The full detail is unchanged in the server
  logs.**
- `Response` now rejects a custom header whose name or value is malformed, or which would override
  `Content-Type`, `Content-Length`, `Transfer-Encoding` or `Connection`.

**Who is affected:** clients that parsed the reason phrase rather than the numeric code; anything
that scraped `exceptionFullDump` or the file/function fields out of an HTTP error body for
diagnostics — those consumers must read the server log instead. Deployments of an *internal*
trusted service that relied on the full dump over HTTP have no opt-out flag; say so if one is
requested.

**Amendment (2026-09-08): the redaction reaches the gateway, covers the message strings, and now
has an opt-out.**

N-9's redaction landed only in `httpserver/ServerBackendProcessingImplDefault.h`, so the two paths
which actually front an internet-facing deployment kept emitting the full dump:
`rest/RestUtils.h`'s `formatEhResponseSimpleJson` (the HTTP gateway's error response) and
`rest/BaseRestServerProcessingContext.h` (the error body a REST server behind the gateway produces,
which the gateway forwards verbatim). Both are redacted now. The gateway is the app the review named
as network-facing — `MessagingHttpGatewayApp.h` runs an `HttpSslServer`.

Two further changes to what redaction removes:

- **`exceptionMessage` and `properties.message` are redacted too.** `BL_MSG()` text in this library
  routinely carries file paths, endpoint addresses and internal identifiers. `exceptionMessage` is a
  required property of the model, so it is **replaced** by the friendly message the model already
  computes rather than emptied, and `properties.message` is cleared. An exception raised as *user
  friendly* is exempt: its text was written for the caller, and it is passed through unchanged.
- **`originalStackTrace` and `originalThreadName` are blanked**, together with the rest of the
  disclosing set. These are two of the eight new properties described in section 12 below.

**Opt-out - at the API level only, and deliberately so:**

- `rest::RestUtils::formatEhResponseSimpleJsonUnredacted( ... )` has the same signature as
  `formatEhResponseSimpleJson` and can be handed in wherever the eh-format callback is taken;
- `BaseRestServerProcessingContext::redactErrorResponses( false )` restores the full document for a
  REST server behind the gateway.

Both default to redacting. Use them only where the HTTP endpoint is reachable by trusted hosts only.

**Note what this does *not* give a deployment.** `bl-messaging-http-gateway` still selects the
redacted formatter unconditionally (`MessagingHttpGatewayApp.h:278-280`), so **a stock gateway
deployment cannot turn redaction off without a code change** - the paragraph above about "no opt-out
flag" therefore still holds for that app. Whether the gateway should expose a command-line switch
for it is an open product decision: adding one makes an information-disclosure setting operator
controlled, which is precisely what redaction is meant to prevent being accidental. Nothing here
depends on that decision; the API opt-out is what an embedder uses.

## 12. Server error documents carry eight more fields, and rehydrate more types (2026-09-08)

**Source:** items 2 and 3 of
`notes/reviews/major/update_2026/whole-library-cxx-test-enhancement-outstanding-issues-plan.md`.

**Presents as:** an additive wire change, plus two changes to what
`ServerErrorHelpers::createExceptionFromObject` produces.

`data/models/ErrorHandling.h`, `data/eh/ServerErrorHelpers.h`

- **Eight new optional properties** on `ExceptionProperties`: `hint`, `originalType`,
  `originalThreadName`, `originalStackTrace`, `serviceStatus`, `serviceStatusCategory`,
  `serviceStatusMessage` and `errorUuid`. They carry the `errinfo_*` tags of the same names, which
  post-dated the model and were being dropped in both directions. `errorUuid` travels as a string
  and is converted at the boundary; `originalStackTrace` is capped at 4 KiB by the serializer so one
  error cannot inflate a whole response. Every property is optional and unset properties are not
  emitted, so the change is compatible in both directions — an old peer ignores them, a new peer
  simply finds them absent.
- **Five more exception types survive the round trip:** `bl::BufferTooSmallException`,
  `bl::NotFoundException`, `bl::UserAuthenticationException`, `bl::NumberCoerceException` and
  `bl::PrintableWrapperException` had no arm in `createExceptionFromObject` and were all rehydrated
  as `bl::UnexpectedException`. **A client which catches on one of those five types starts matching
  where it used to fall through to a generic arm.** The fall-back to `UnexpectedException` is
  unchanged for a type name this process does not know.
- **An unknown error category no longer rejects the whole document.** `createExceptionFromObject`
  used to throw `ArgumentException "Unknown error category: '<name>'"` for any category other than
  `generic` and `system` — so a well-formed server error describing, say, a TLS failure (whose
  category name is `OpenSSL`) reached the peer as a deserialization error about an internal
  category. The name and the numeric value now survive as `errinfo_category_name` and
  `errinfo_system_code` data instead. The error **code** still cannot be rebuilt for such a
  category, because an `eh::error_category` is a process-local object; a `bl::SystemException` with
  an unresolvable category is rebuilt from `systemCode` and carries the real category name
  alongside.

**Who is affected:** a consumer which relied on `createExceptionFromObject` throwing for an unknown
category (it no longer does), or on one of the five types above arriving as
`bl::UnexpectedException`. Anything reading the JSON document itself is unaffected — the change is
purely additive there.

## 12a. Cancelling a `RetryableWrapperTask` now stops the retry loop (2026-09-08)

**Source:** item 1 (decision D1, the review's D-01) of
`notes/reviews/major/update_2026/whole-library-cxx-test-enhancement-outstanding-issues-plan.md`.

**Presents as:** a runtime behaviour change on a public task type. Silent - a cancelled task now
finishes early instead of continuing to work.

`tasks/TaskBase.h`

`requestCancel()` on a `bl::tasks::RetryableWrapperTask` used to be dropped on every swap of the
wrapped task. The forwarding `requestCancel()` reaches only the task which happens to be wrapped at
the instant of the call; each task the factory produces starts uncancelled, and the retry sleep
timer completes **without** an exception when it is cancelled, so the "did the wrapped task fail?"
guard never fired and a fresh work task was born uncancelled. A cancelled retryable task therefore
ran out its full `maxRetryCount` - it kept hammering the remote endpoint after the caller had given
up.

`RetryableWrapperTaskT` now holds a cancel latch of its own, checked under the same lock which
guards the swap. Once cancelled, no further work task is created no matter which task is wrapped.

**What a caller observes:** the task completes **failed**, promptly, carrying the work task's own
last error if it had one, and otherwise a `bl::SystemException` with
`asio::error::operation_aborted` marked `errinfo_is_expected( true )`. Previously it completed after
running `maxRetryCount` attempts with `( maxRetryCount - 1 )` sleeps of `retryTimeout` between them.

**Who is affected:** anything which cancels a retryable task and then relied on it continuing - no
such consumer exists in this repository - and anything which measured or waited on the old
duration. Neither `ForwarderTaskBase` nor `SimpleTimerTaskT` changed, so every other user of those
two is unaffected; the latch is entirely inside the retry wrapper, which is the only class that
knows the operation is the whole retry sequence rather than the task currently wrapped.

## 13. `bl::cmdline::BoolSwitchOrMultiStringOption` withdrawn (2026-09-08)

**Source:** item 4 (decision D8) of
`notes/reviews/major/update_2026/whole-library-cxx-test-enhancement-outstanding-issues-plan.md`;
full analysis in `notes/plans/issues/cmdline-boolswitch-or-multistring-option-deferral.md`.

**Presents as:** a source-compatibility break — a compile error, never a silent behaviour change.

`cmdline/Option.h`

```
typedef Option< std::vector< std::string >, detail::SwitchImpl< std::vector< std::string >, true > >  BoolSwitchOrMultiStringOption;
```

has been deleted. The typedef never worked: `SwitchImpl::decorateSemantic` applies `zero_tokens()`
last, so the option could not consume a token at all — `--flag` set `hasValue()` with an empty
vector, and `--flag a` failed the whole command line with
`bl::po::too_many_positional_options_error`, a message which does not mention `--flag`. What its
name promises is modelled in Boost.Program_options by `implicit_value`, not by `zero_tokens`.

**Who is affected:** any consumer naming the typedef. It had no instantiation anywhere in this
repository. Replace it with `bl::cmdline::BoolSwitch` for the flag, plus a separate
`bl::cmdline::MultiStringOption` for the values.

## 14. Smaller behaviour corrections (2026-09-08)

**Source:** item 7 of
`notes/reviews/major/update_2026/whole-library-cxx-test-enhancement-outstanding-issues-plan.md`.
Each of these was pinned as "current, not endorsed" by the test-enhancement work and is now fixed;
none is expected to affect a correct consumer, but each is an observable change.

| Where | Was | Is |
|---|---|---|
| `fs::safeDeletePathNothrow` | returned `true` even when the deletion threw and the path survived — the noexcept guard logged the escaping exception and control fell through to `return true` | returns the real result; the three in-tree callers already treat `false` as "log and continue" |
| `encoding::writeTextFile` | validated the encoding *after* `fopen( ..., "wb" )`, so an invalid or unsupported encoding truncated an existing file before throwing | validates first; a rejected call leaves the file untouched |
| `cpp::ScopeGuardT::operator=( ScopeGuardT&& )` | overwrote the target's callback without running it, silently dropping a cleanup the target was still holding | runs the target's callback first, the same rule the destructor implements; self-assignment is a no-op |
| `data::DataBlock::write( const std::string& )` and `write( const char* )` | wrote the 4-byte length prefix and the bytes as two capacity-checked writes, so a failure left a dangling prefix behind | checks `4 + size` once, before either write |
| `net::IcmpHeader::computeChecksum` | never zeroed the checksum field, so recomputing on a reused header produced a wrong value | zeroes the field first; a freshly built header is unaffected |
| `tasks::ExcludedPathsControlToken` | compared against `fs::normalize`d scanned paths while storing the caller's entries verbatim, so an un-normalised exclusion entry excluded nothing | normalises each entry in the constructor, with the same rule (and the same Windows lower-casing) as the lookup |
| `tasks::TimerTaskBaseT::resetTimer` | bound the timer to `ThreadPoolDefault::getDefault( ... )` and ignored the execution queue's local thread pool, which every other task honours | uses `getThreadPool( eq )`. **Every `ObservableBase` is a timer task**, so a consumer which sets a local thread pool on a queue will see its reactive pipelines' timers move onto that pool |
| `http` client charset decode | `g_charsetRegex`'s trailing `\b` captured a quoted charset (`charset="UTF-8"`) as `"UTF-8`, which matched no known charset and took the unsupported-charset arm | strips the quotes; a quoted charset now decodes, as RFC 7231 intends |
| `messaging::ProxyBrokerBackendProcessing` | did not override `isConnected()` and inherited the always-connected default, even once fully disconnected | delegates to its outgoing block channel, as the forwarding backend does. **A REST request to a proxy which has lost the actual backend now fails fast** instead of being admitted |
| `loader::ManifestFactoryT::read` | called `value.as_object()` with no precondition, so a manifest whose top-level value is not an object escaped as a raw JSON-backend exception | throws the user-friendly `bl::UnexpectedException` every other malformed-manifest path yields |
| `cmdline::CommandBase::removeOption` / `removeCommand` | erased the map entry by name but the vector entry by pointer identity, so removing a different object carrying a registered name desynchronised the two and tripped a `BL_ASSERT`, aborting a debug run | erases both by the same key |
| `cmdline::CommandBase::getOptionsHelp` | hid the root's `dryrun,n` option, rendered, and unhid — with no RAII guard, so a throw in between left `--dryrun` hidden for the life of the process | wraps the unhide in `BL_SCOPE_EXIT` |
| `Watchdog` | accepted a negative checking interval, which wrapped into a huge unsigned value and silently disabled the watchdog; `expiringMonitors( horizon )` accepted a negative horizon and reported every monitor | rejects both. The accepted domain now matches what the documentation always stated |

Three related behaviours were reviewed and deliberately **kept**: `http::StatusStrings::get()`
substituting the 500 status line for an unknown code (it has no production caller left — the wire
goes through `getStatusLine()`), `chk4ServerErrors()` narrowing a backend failure to one `uint32`
(a V1/V2 protocol change — recorded in `broker-outbound-peer-identity-deferral.md`), and
`RemoveChunk` with `IgnoreIfNotFound` succeeding on a never-saved chunk (that is what the flag
means).

---

## 15. Windows: `fs::path` now normalizes forward slashes to backslashes (2026-09-09)

On Windows, `bl::fs::path` applies the long file name prefix `\?\` to every absolute path it is
constructed from. That prefix **switches off path parsing in the kernel**, and under it a forward
slash is not a separator but an illegal character. Until now the separators were left alone, so a
path spelled with `/` — legal everywhere else on Windows — became unusable once it was prefixed.

`WinLfnUtils::chk2AddPrefix` (`core/detail/OSImplPlatformCommon.h`) now calls `make_preferred()`
before it does anything else, so every `fs::path` construction and assignment on Windows yields
`\`-separated text. **POSIX is unaffected** — `PathImplT< false >` never calls that function, and
backslash is a legal filename character there.

### Why it mattered

A blob package produced on Linux stores its relative entry paths as `d/f.bin`
(`fs::getRelativePath` builds them with the platform separator). `FilesUnpackagerUnit` joins that
onto an already-prefixed target directory, so unpacking such a package on Windows failed with
`ERROR_INVALID_NAME` (123) from `CreateDirectoryW` / `SetFileTime` and `EINVAL` (22) from
`_wfopen` — i.e. a package produced on one supported platform could not be consumed on another.

The same class of defect had already been patched at three individual call sites — `JAVA_HOME`
under MSYS (`scripts/devenv7/AGENTS.md:423`), `fs::temp_directory_path()` and
`getFileOwner()` — each remembering to normalize on its own. The type now discharges the hazard it
creates.

### What becomes visible

Absolute paths already came back prefixed and different from their input. What is new is that
**relative** paths and **already-prefixed** paths also come back `\`-separated:

- code that round-trips a relative path through `.string()` and compares against a `/`-spelled
  literal (two in-tree tests were updated: `TestBaselibDefault.h:7759` and
  `TestFilesystemMetadataInMemory.h:243`);
- filesystem error messages, via `normalizePathParameterForPrint`;
- the `.symlink` placeholder file the unpackager writes on Windows, whose content is the target
  text — `../d/sub` is now persisted as `..\d\sub`;
- `PluginAccess::getLibrary()`, which returns `\` for a `/`-spelled registration.

`//server/share` is now correctly recognised as a UNC share instead of becoming
`\?\//server/share`. That holds for `BOOST_FILESYSTEM_VERSION` 3, which is what consumers get by
default; the version 4 `make_preferred()` deliberately leaves the root name alone.

`bl::fs::nolfn::path` remains the raw Boost path for a caller who needs forward slashes preserved.

### Not covered

The mirror direction is unchanged and still open: a package produced **on Windows** stores
`d\f.bin`, which on Linux is a single legal filename rather than a two-level path. The portable
form would be `generic_string()` at whatever boundary persists the metadata; nothing in-tree
persists it (the store is in-memory).

Three things still bypass the guarantee, all benign today: the in-place mutators inherited from
Boost (`/=`, `+=`, `swap`) and `auto x = fsPath / rhs`, which deduces `boost::filesystem::path`.
Every in-tree operand is a single iterated component, a UUID or a separator-free literal.
