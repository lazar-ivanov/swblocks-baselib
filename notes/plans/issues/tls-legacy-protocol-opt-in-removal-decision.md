# TLS Protocol Floor and Security Level: Decision Record

This document records the decision to remove the legacy TLS protocol opt-in from the library, to
fix the protocol floor at TLS 1.2 on every supported version of OpenSSL, and to pin the OpenSSL
security level to 2 wherever the API exists. It records what the removed opt-in actually permitted,
why removal was chosen over a configuration flag, and what a consumer of the library has to do.

**Finding:** H-3 in `notes/reviews/major/update_2025/v1/pr_review_analysis_fable51.md` (High):
"Legacy TLS opt-in sets OpenSSL security level 0 on the shared contexts; three shipped servers opt
in unconditionally".

**Plan:** `notes/plans/issues/pr-review-fable51-merge-gate-h1-h3-plan.md`, Part A.

**Supersedes:** workstream B1 of `notes/plans/issues/pr-review-residual-cxx-findings-plan.md`, which
introduced `TlsMinimumVersion` and kept `isEnableTlsV10()` as a forwarder, and the CXX-06 row of
`notes/plans/issues/pr-review-deep-dive-residual-findings.md`.

---

## The decision

**There is no way to negotiate anything below TLS 1.2 with this library, and no way to lower the
certificate and key strength floor. Neither is configurable.**

The following were removed from `src/include/baselib/crypto/CryptoBase.h`:

| Removed | Introduced |
|---|---|
| `enum class bl::crypto::TlsMinimumVersion { Tls12, Tls11Legacy, Tls10Legacy }` | this branch (B1) |
| `CryptoBase::tlsMinimumVersion( TlsMinimumVersion )` | this branch (B1) |
| `CryptoBase::isEnableTlsV10( bool )` | 2017, on `master` |
| the `SSL_CTX_set_security_level( ctx, 0 )` call made under the legacy values | this branch (B1) |

and the unconditional `isEnableTlsV10( true )` calls were deleted from the three shipped servers
(`bl-messaging-broker`, `bl-messaging-http-gateway`, `bl-messaging-echo-server`) and from the
`utf_baselib_security` test binary.

What every context - the process-global client context and every server context - now gets, in
`CryptoInitT::initNativeSslContext`:

| OpenSSL | Protocol floor | Security level |
|---|---|---|
| 1.0.2 (devenv2) | TLS 1.2, via `SSL_OP_NO_SSLv2 \| NO_SSLv3 \| NO_TLSv1 \| NO_TLSv1_1` | no such API on this version |
| 1.1.0 / 1.1.1 (devenv3-5, `BL_USE_OPENSSL_1X`) | TLS 1.2, via the option bits **and** `SSL_CTX_set_min_proto_version` | **2**, set explicitly and verified with `SSL_CTX_get_security_level` |
| 3.x (devenv6, devenv7) | same | **2** |

`TestTlsProtocolPolicy.h` in `utf_baselib_http` asserts all of it: a TLS 1.0 and a TLS 1.1 client
are refused by a server context, and both the client and a server context report a minimum protocol
version of TLS 1.2 and a security level of 2.

---

## What the removed opt-in actually did

The opt-in was described in its own comment as "permitting an older protocol version" plus relaxed
key sizes and SHA-1 signatures. That understated it. OpenSSL copies the SSL security level into the
X.509 authentication level used for chain verification, and level 0 disables every strength check:

- certificates signed with MD5, or with any digest at all, are accepted in the chain;
- RSA, DSA and DH keys below 1024 bits, and ECC keys below 160 bits, are accepted;
- nothing in the level refuses RC4, export or anonymous suites any more (the cipher list still did,
  but the cipher list was the only remaining guard).

The level is a property of the whole `SSL_CTX`. The opt-in applied it to the process-global client
context, so it weakened every outbound connection of the process, not only the connection to the
one legacy peer that motivated it. The three shipped servers set it unconditionally at startup,
before their command line was parsed, from a 2017 comment about "authorization servers that might
not yet support TLS 1.1".

On `master` under OpenSSL 3 the same call was inert: it only cleared `SSL_OP_NO_TLSv1`, and the
default security level of 1 refuses TLS 1.0 and 1.1 regardless. So those servers ran at TLS 1.2 and
level 1 on `master`, and would have run at TLS 1.0 and level 0 after B1. That is a regression, and
it is the part of B1 which this decision reverses.

---

## Why removal, not a flag

The review offered two dispositions: gate the servers' opt-in behind a command line flag, or record
that level 0 on the shared contexts is accepted. Both were rejected in favour of removing the
mechanism.

| Option | Why not |
|---|---|
| Keep the opt-in, gate the servers behind a flag | The mechanism itself is the problem. A single `SSL_CTX` has a single security level, so any use of the opt-in weakens certificate verification for every connection of that role, and a flag only moves the decision to whoever writes the deployment script. Nothing in this repository needs TLS 1.0 or 1.1 any more |
| Keep `isEnableTlsV10()` as a no-op or as a throwing shim | An opt-in which silently does nothing is exactly the failure B1's own comment warned against; a throwing shim moves the surprise from compile time to the first run. A compile error is the honest signal, and the project has precedent for it (`ExecutionQueue::setNotifyCallback`, release note item 2) |
| Leave the OpenSSL default security level in place | The default is a compile-time property of the linked OpenSSL (`OPENSSL_TLS_SECURITY_LEVEL`, 1 upstream, raised to 2 by some distributions), so the floor would depend on who built OpenSSL rather than on this library. Level 1 still accepts 1024-bit RSA keys in chains |
| Pin level 3 | Level 3 refuses RSA keys below 3072 bits, which would refuse the 2048-bit certificates that are still the deployed norm (including every test certificate in this repository). Level 2 is the highest usable level |

The policy stated by the author: the library must be fully secure with the highest floor the linked
OpenSSL supports, on the 1.x builds as much as on 3.x, and nothing below that floor may be
reachable through configuration.

---

## Consequences a consumer has to know about

**Presents as a compile error.** Any out-of-tree caller of `CryptoBase::isEnableTlsV10()` or
`CryptoBase::tlsMinimumVersion()` stops compiling. The fix is to delete the call.

**Presents as a handshake failure, or as a startup failure.**

- A client connection to a peer which cannot negotiate TLS 1.2 fails at handshake. A peer which
  needs TLS 1.0 or 1.1 needs to be upgraded; there is no library option for it.
- A client connection to a peer whose certificate chain contains an RSA, DSA or DH key below 2048
  bits, an ECC key below 224 bits, or (on OpenSSL 3.x) a SHA-1 signature, fails chain verification.
- A **server** whose own certificate or key is below that floor fails when its context is created,
  i.e. at startup in `createAsioSslServerContext`, because the level is applied before the key and
  certificate are loaded. This is deliberate: a misconfigured server fails loudly once rather than
  refusing every client.

**Interaction with the bundled trust store.** The bundled roots in `TrustedRoots.h` include the
1024-bit VeriSign Class 3 root; at level 2 it cannot anchor a chain (`X509_V_ERR_CA_KEY_TOO_SMALL`).
It could not verify anything current before this change either; the trust store question is M-5 in
the same review and is not addressed here.

**Test fixtures.** Every certificate under `certs/`, in `src/utests/include/utests/baselib/UtfCrypto.h`
and under `src/utests/utf_baselib_security/data/` is RSA-2048 with a SHA-256 signature, so the test
suite passes at level 2 unchanged. Fixtures regenerated in the future must stay at or above that.

**Cipher lists.** A `@SECLEVEL=` token inside a cipher string overrides `SSL_CTX_set_security_level`.
The library's cipher list carries none and must not gain one; the test helper in
`TestTlsProtocolPolicy.h` uses `@SECLEVEL=0` only on the *client* test context it builds for itself,
which is the side not under test.

---

## Status

Implemented on `lazari2` together with the H-1 and H-2 fixes of the same review; see the plan for
the verification matrix (OpenSSL 3.5.4 and 1.1.1w, gcc and clang).
