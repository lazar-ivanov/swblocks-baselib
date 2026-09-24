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
(`src/include/baselib/http/SimpleHttpTask.h:612-657`), and in practice servers reply uncompressed.

**Corrected 2026-09-22 (astra H29).** That sentence used to end *"so servers reply uncompressed"*,
and as a statement about the protocol it is false. RFC 9110 section 12.5.3, the first of the rules a
server tests acceptability by: *"If no Accept-Encoding header field is in the request, any content
coding is considered acceptable by the user agent."* It is an **empty** field value, not an absent
one, which says the opposite: *"An Accept-Encoding header field with a field value that is empty
implies that the user agent does not want any content coding in response."* So omitting the field
permits every coding rather than forbidding all of them, and a conforming server may answer it with
`content-encoding: br`. Sending none when none was asked for is a convention servers keep, not an
obligation the protocol places on them - which is why "in practice" is the strongest the sentence
can be made.

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
as today, and in practice servers answer with an uncoded body. The loss is bandwidth.

**Corrected 2026-09-22 (astra H29), the second of the two sentences.** This one used to read *"and
servers send identity bodies"*, which promises what section 12.5.3 does not - see the correction
above. **The code is already right, and nothing here is a behaviour defect.** A server which does
answer an absent `accept-encoding` with a coding meets
`SessionRequestTaskT::decodeBody( )` (`ClientSession.h`): it finds no decoder registered for that
coding and returns, so the body and its `content-encoding` reach the caller exactly as they
arrived. Nothing is decoded wrongly, nothing is stripped, and the header which says the body is
coded is still on it. What the two sentences overstated was the *guarantee*, not the behaviour.

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

## Prerequisites before a decoder ships

Three defects are harmless while the registry is empty and become live the moment it is not. They are
recorded **here** rather than in a ledger because `ContentDecoder.h`'s file note sends every codec
author to this document by name, and because none of them is the codec's own work. **They are a gate,
not a deferral:** shipping a codec without P1 puts unbounded CPU under a queue-wide mutex.

### P1. The decode runs under the execution queue's scheduling lock, uncancellable and undeadlined

`ExecutionQueueImpl::onReady( )` holds the queue's `m_lock` across `task -> continuationTask( )`, and
`SessionRequestTaskT::continuationTask( )` takes the session wrapper's lock and then runs
`absorbResponse( )` — `storeCookies( )` and `decodeBody( )`. So a decode of up to
`DecoderLimits::DEFAULT_MAX_OUTPUT_BYTES` (64 MB) holds a **queue-wide** mutex: every `push_back( )`,
`wait( )` and `pop( )` on the queue the request was pushed to blocks for its duration, as does a
`requestCancel( )` on the wrapper. And nothing interrupts it — the completed hop has already
cancelled its timers, `chkRemainingBudget( )` is evaluated synchronously **inside** `startHop( )`,
which runs *after* the decode, and the wrapper's cancel flag is tested after it too. Those are two
separate facts: **uncancellable** and **undeadlined**.

**The fix is structural and is not a limit.** Move the response transformation into the hop task's
deferred phase, which already exists and already runs off both locks. It is testable when it lands —
a deliberately slow decoder with other completions queued behind it — and that case belongs to the
change that makes it pass.

*L6 finding 6; astra H09. S6R.3 took the documented minimum now — the contract written at
`BodySource::rewind( )` in `ClientTypes.h` and at `ContentDecoder` — because that closes the half
which is live today: `rewind( )` is **caller code** running in that place with a contract that said
nothing about it. The decode half is latent, and measurably so: `decodeBody( )` returns at once
unless a decoder is registered for the response's coding, and every `registerDecoder( )` call in the
tree is under `src/utests/`.*

### P2. `Content-Encoding` is read as one token, not as a list — astra H24

`decodeBody( )` looks the **whole field value** up in the registry (`decoders.hasDecoder( *coding )`),
so a value naming more than one coding matches nothing and the body is handed back intact with its
header — which is the documented behaviour for a coding we cannot decode, and is why this is latent
rather than silent. It stops being latent as soon as a decoder exists: the layers of a multi-coding
value have to be recognised and peeled in reverse order, and today a single successful decode deletes
both `content-encoding` and `content-length` wholesale. The full entry, including the verification
record's own correction that H24 is *not* silent, is in `astra-review-verification-record.md`.

### P3. Decoding runs on failed and on bodyless responses — astra H25

`absorbResponse( )` returns early only on `status( ) == 0`, and `continuationTask( )` calls it
**before** it examines `m_hop -> exception( )`. So a hop that failed *after* its headers — a body
over the cap, a reset mid-body — still reaches `decodeBody( )`, and a decoder that throws on the
truncated coded body has its exception forwarded by the queue over the network error that actually
happened. The bodyless half is its sibling: a 204, a 304 or a response to HEAD which carries a
`content-encoding` reaches `createStream( )` and `finish( )` with nothing to decode, which the
`ContentDecoder` contract makes a truncation throw. The failed half is L6 finding 12; the full entry
is in `astra-review-verification-record.md`.

---

## The decision on P2 and P3, taken 2026-09-24

**Both stay deferred here, and this is a decision rather than an omission.** They came up in the
sweep of everything astra's review left open, were considered on their own merits, and the
disposition is unchanged: **P2 (astra H24) and P3 (astra H25) are prerequisites of the decoder
programme, not of the HTTP client layers.**

**Why deferring costs nothing today.** Both are **latent by the same property**: this client
registers no decoder, so `decodeBody( )` matches nothing, the body is handed back intact with its
header, and that is the documented behaviour for a coding we cannot decode. Neither can produce a
wrong answer while that holds.

**What reverses it — one condition, and it is the same for both.** The moment **any** content codec
is registered, both stop being latent, and they become **the first two items of that work** rather
than new findings discovered during it. P2's multi-coding layers have to be peeled in reverse order;
P3's early return has to move above the exception check. Whoever unparks the decoder programme owns
them on day one.

**Note the parked state is not only this deferral's.** The embedded decompression design and plan are
committed and parked on two unmade decisions of their own — **E5** (generated headers in the repo
include tree versus the devenv dist) and astra's **C01** (the design specifies `inline constexpr`,
which is C++17, while baselib compiles `-std=c++11`). So there are three gates between here and a
registered codec, and P2/P3 sit behind all of them.

---

## Sequencing when it does happen

0. Close **P1** above, and P2 and P3 for any coding being registered. P1 is a change to the
   continuation protocol `RetryableWrapperTaskT` shares, so it is a core-path change-set of its own
   and gates on the whole suite; it does not ride with a codec.
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
