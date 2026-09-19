# Supplying HTTP Content Decoders: Deferral Record

This document records the decision to ship the content-decoding **seam** with the new HTTP client and
**no decompressor**, what that costs in the meantime, and the options for supplying decoders when the
question is taken up. It is a deferral of a dependency decision, not a judgement that decoding is
unimportant - for browser impersonation it is close to essential, as set out below.

**Design:** `notes/plans/http2-design.md`, decision D9 and sections 5.6 and 6.5.

---

## Decision

**Date:** 2026-09-17
**Status:** the seam is part of the client design; how decoders are supplied is deferred.

| # | Item | Disposition |
|---|---|---|
| 1 | `httpclient::ContentDecoder` interface, per-session registry, output and expansion-ratio caps | **In scope now** |
| 2 | `accept-encoding` sent as the profile's list intersected with the registered decoders; strict mode | **In scope now** |
| 3 | A `gzip` / `deflate` decoder | **Deferred** |
| 4 | A `br` (Brotli) decoder | **Deferred** |
| 5 | A `zstd` decoder | **Deferred** |
| 6 | TLS certificate compression, by rebuilding OpenSSL with a compressor | **Deferred** - follows from 3-5 if external libraries are chosen |

---

## Why there is a question at all

The library has no decompressor. Not directly; not through Boost, which is built without `iostreams`
(`scripts/devenv7/linux/build-boost-linux.sh:367`); not through OpenSSL, which is configured with
`no-shared` and nothing else (`scripts/devenv7/linux/build-openssl-linux.sh:539`).

The existing client never needed one: `SimpleHttpTask` sends no `accept-encoding`
(`src/include/baselib/http/SimpleHttpTask.h:612-657`), so servers reply uncompressed.

Browsers advertise four content codings, which are three algorithms of very different weight:

| Coding | Algorithm | Reference library | If written in-house |
|---|---|---|---|
| `gzip`, `deflate` | DEFLATE (RFC 1951) in a gzip or zlib wrapper | zlib | about 600 lines; small and well understood |
| `br` | Brotli (RFC 7932) | google/brotli | about 2,000 lines **plus a 122 KB static dictionary** fixed by the RFC |
| `zstd` | Zstandard (RFC 8878) | facebook/zstd | about 2,000 lines |

---

## What the deferral costs

It depends entirely on the path.

**Requests without impersonation: essentially nothing.** The client omits `accept-encoding`, exactly
as today, and servers send identity bodies. The loss is bandwidth.

**Requests with impersonation: it matters, and it affects the impersonation itself.** Every real
browser sends `accept-encoding: gzip, deflate, br, zstd` on every request. That header is part of the
header-layer fingerprint, and it is the cheapest layer for a detector to check - any WAF rule can, with
no TLS introspection. Without decoders there are two choices and both are bad:

1. **Send the header faithfully.** The server picks what it prefers from the list - commonly `br` or
   `zstd` for HTML and JSON - and the client hands back compressed bytes. The request succeeds and the
   body is unusable. HTML and JSON are what one impersonates a browser to fetch.
2. **Send only what can be decoded**, which today is nothing. Bodies are usable, and the client is a
   "Chrome" that does not accept Brotli, which does not exist.

**Partial support does not escape this.** With only `gzip` available, `br` and `zstd` still cannot be
advertised truthfully, because the server chooses among what is advertised. Full header fidelity needs
all three algorithms.

Of the optional layers of the client, this is the one impersonation depends on.

---

## What is done now

**The seam.** `ContentDecoder` is a streaming transform - bytes in, bytes out, incrementally, so a body
is never held twice - registered per session under its content-coding token. Two caps are part of the
interface from the start, because a decoder is a decompression-bomb surface and caps bolted on later
get forgotten: an absolute output limit, and a limit on the ratio of output to input.

**Honest degradation.** The `accept-encoding` actually sent is the profile's list intersected with the
registered decoders. With none registered the header is omitted, and the fidelity report
(design section 6.6) lists the deviation. So the library builds and works with no new dependency, the
gap is visible rather than silent, and each decoder registered later raises fidelity with no other
change.

**Strict mode.** A caller who decodes bodies themselves can ask for the exact browser header and
receive the raw encoded body with its `content-encoding`.

**The interim state, stated plainly:** under impersonation the header layer is exact except for
`accept-encoding`.

---

## The options, for when this is taken up

| Option | New dependencies | Fidelity | Principal cost |
|---|---|---|---|
| A. zlib, brotli and zstd as third-party libraries | three C libraries | full | build scripts per OS, architecture and variant beside `build-openssl-*.sh`; a module each under `projects/make/3rd/`; three more entries in the supply-chain story. Breaks the README's "Boost and OpenSSL" minimalism |
| B. All three in-house, header-only | none | full | roughly 5,000 lines of security-sensitive code to own and fuzz. Brotli's 122 KB dictionary embedded in a header costs compile time and test-module object size |
| C. In-house inflate; `br` and `zstd` only if the application registers them | none | partial by default | `accept-encoding` deviates unless the application supplies decoders |
| D. Seam only, permanently | none | none by default | the application supplies everything |

Two observations which should weigh on the choice.

**Decompressors parsing untrusted network input are a classic vulnerability class.** The reference
libraries are continuously fuzzed. In-house ones would have to be, and the pure byte-in, byte-out
shape of the seam makes a libFuzzer harness straightforward - but it is a standing obligation, not a
one-off task. This is the main argument against B, and a lesser one against C, where only inflate is
at stake and inflate is small.

**Option A has a side benefit on the TLS layer.** With the libraries present, OpenSSL can be
configured with `enable-brotli enable-zlib enable-zstd`, which turns on certificate compression
(RFC 8879) and puts the `compress_certificate` extension into the ClientHello. That closes one of the
gaps between OpenSSL's ClientHello and a browser's (design section 6.3). It also changes the OpenSSL
build on every platform, so it is a decision in its own right (item 6).

Boost.Iostreams is not a fifth option. Its gzip, zlib and zstd filters are thin wrappers which need the
external libraries at Boost build time, so it adds a compiled Boost library *and* the same
dependencies, and it has no Brotli filter at all.

---

## Conditions to revisit

- Impersonation is about to be used against real targets. At that point the `accept-encoding` deviation
  stops being a documented gap and becomes the thing which gets the client detected.
- A consumer wants compressed responses on the ordinary, non-impersonated path, for bandwidth.
- The devenv gains any of the three libraries for an unrelated reason, which makes option A cheaper for
  that coding.
- A decision on certificate compression (item 6), which would bring the libraries in anyway.

---

## Sequencing when it does happen

1. Choose between A, B and C **per coding** - they need not share an answer. Inflate in-house with
   Brotli and Zstandard external is a coherent outcome.
2. For any external library: the build scripts, the makefile module and the supply-chain record, on
   every platform, before any code depends on it. Each behind its own build flag so the library still
   builds without it.
3. For any in-house decoder: the libFuzzer harness first, and golden vectors from the reference
   implementation's own test corpus.
4. Register the decoders in the default session and delete the corresponding deviation from each
   profile's list.
5. Separately, and only if A was chosen: decide on rebuilding OpenSSL with certificate compression,
   re-run the fidelity spike, and update the profiles' grades from what it measures.
