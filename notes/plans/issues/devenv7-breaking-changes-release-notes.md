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

## 5. TLS peer verification semantics changed on Boost ≥ 1.89

**Presents as:** a runtime behaviour change. Silent — a handshake that used to succeed now fails.

`src/include/baselib/core/detail/AsioSslCompat.h:41` typedefs Asio's removed `rfc2818_verification`
to `host_name_verification`. These are **not the same implementation**:

| | `rfc2818_verification` (before) | `host_name_verification` (after) |
|---|---|---|
| Matching | RFC 2818, in Asio's own code | delegates to OpenSSL `X509_check_host()` |
| subjectAltName | partial | correct |
| CN when a SAN is present | consulted | **ignored**, per RFC 6125 |
| Embedded NUL in names | accepted | **rejected** |
| **IP addresses** | matched | **not matched** — needs `X509_check_ip` |

The first three rows are a genuine security improvement and are the reason not to revert this.

**The fourth row is a break.** A deployment that connects to a peer **by IP address** against a
certificate carrying that IP now fails verification where it previously succeeded, with no
diagnostic beyond a handshake failure. Note that `AsioSslStreamWrapper.h` is not in the branch's
changed-file list — its behaviour changed without its source changing.

**Status:** IP-address support is being restored (R-1 / Bundle B of
`notes/plans/issues/pr-review-opus5-residual-findings-plan.md`), by dispatching on whether the host
is an address literal and using `X509_check_ip_asc` for that case. **If that lands before release,
trim this entry to the first three rows** — the improvement still deserves a note, the break does
not.
