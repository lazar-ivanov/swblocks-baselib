# Embedded Decompression: Design

**Status:** design, written 2026-09-20. **Nothing built, nothing probed, no source vendored.** Every
number below that is not marked *measured* is an estimate to be replaced by a measurement at the slice
that needs it.

**Origin.** `notes/plans/embedded-compression-design.md` is the research note that proposed this route
— vendored, header-only, private implementations of zlib, Brotli and Zstd. This document is the design
that follows from it, narrowed by four decisions taken on 2026-09-20 and corrected where the research
note's assumptions do not hold in this repository.

**What it feeds.** `notes/plans/issues/http-content-decoders-deferral.md` items 3, 4 and 5 — the
`gzip`/`deflate`, `br` and `zstd` decoders the HTTP client seam was built for and shipped without. This
design is a **fifth option** beside that record's A–D, and §9 states what it does and does not close.

---

## 0. Decision ledger

| # | Decision | Taken |
|---|---|---|
| **E1** | **Decompression now; the facade is shaped so compressors can be added later without reshaping it.** No encoder is vendored. | 2026-09-20 |
| **E2** | **Vendor upstream C and transform it** by a reproducible pipeline, rather than writing decoders in-house. Upstream's correctness and its continuous fuzzing are the asset being bought. | 2026-09-20 |
| **E3** | **Brotli first.** Hardest first: if the 122 KB dictionary or Brotli's structure breaks the approach, that must surface before a pipeline is built around two easier codecs. | 2026-09-20 |
| **E4** | **Certificate compression stays deferred and is not pursued by this route.** See §9; it is not merely unhelped by this design, it is *unsafe to fake*, which is a finding this design contributes. | 2026-09-20 |
| **E5** | **OPEN — where generated headers live.** In the repository's include tree, or in the devenv dist beside `json-spirit`. §4 states the trade; this must be decided before L1. | — |

---

## 1. Requirement

`swblocks-baselib` is header-only. The HTTP client needs `gzip`/`deflate`, `br` and `zstd` content
decoding. The requirement is that a consumer:

- builds and installs no compression library,
- links no `-lz`, `-lbrotlidec`, `-lzstd`, and no archive, `.so`, `.dll` or `.dylib`,
- receives no architecture-specific artifact,
- sees only a `bl::` abstraction, with the upstream libraries as private implementation detail.

"Nobody builds the libraries" cannot mean no machine code is generated. The workable reading, which
this design takes, is that the implementation is **compiled as part of the ordinary C++ compilation of
the translation unit that uses it** — which is exactly what header-only already means here.

---

## 2. Feasibility: the verdict, and what decides it

**Feasible.** Three findings decide it, and only the third was in doubt.

### 2.1 The seam is close to ideal (measured — read at `ContentDecoder.h`)

`httpclient::ContentDecoder` requires exactly three methods:

```cpp
virtual const std::string& contentCoding() const NOEXCEPT = 0;
virtual void write( <bytes>, const decoder_output_callback_t& output ) = 0;
virtual void finish( const decoder_output_callback_t& output ) = 0;
```

Byte-in, byte-out, streaming. **The decompression-bomb caps are enforced outside the decoder** by
`ContentDecoderStreamT`, which counts on the decoder's behalf. So an embedded backend needs to expose
a streaming decode and *nothing else*: no allocator policy, no output limit, no error taxonomy beyond
"the stream is bad". This is the smallest surface any of these libraries could be asked for.

### 2.2 Decoder-only is a different proposition from the research note's

The research note is scoped to compression *and* decompression. E1 drops the encoders, which removes
the bulk of Zstd and a large part of Brotli. Indicative decoder-only sizes, **to be confirmed at the
vendoring slice**:

| Codec | Decoder translation units | Rough scale | Notable |
|---|---|---|---|
| zlib `inflate` | `inflate`, `inffast`, `inftrees`, `adler32`, `crc32`, `zutil` | ~2–3k lines | smallest; no large table beyond CRC |
| Brotli decode | `decode`, `bit_reader`, `huffman`, `state`, `dictionary`, `transform` | ~6k lines | **122 KB static dictionary** |
| Zstd decompress | `zstd_decompress`, `zstd_decompress_block`, `huf_decompress`, `fse_decompress`, `entropy_common`, `zstd_common`, `error_private`, `xxhash` | ~6–8k lines | no dictionary; SIMD paths need care |

### 2.3 The constraint the research note misses, and the escape from it

**This repository enforces a hard 75 MB per-object ceiling on test modules** (`src/utests/AGENTS.md`),
after one 112.7 MB module made two x86 build combinations impossible to compile at all.

Measured on 2026-09-20, `gcc1520` debug, a64 — the modules that would consume decoders, and the
pressure already in the tree:

| Object | Size |
|---|---:|
| `utf_baselib_httpclient4` | 65.1 MB |
| `utf_baselib_httpclient5` | 62.0 MB |
| `utf_baselib_httpclient6` | 51.4 MB |
| `utf_baselib_h2client5` | 50.0 MB |
| largest in tree (`utf_baselib_io`) | 77.7 MB |
| `utf_baselib_messaging` | 75.8 MB |

Naively embedding three decoders in headers that `ClientSession.h` reaches would push several modules
through the ceiling. **This is the one finding that could have killed the design.**

**It does not, because of an idiom this library already uses in 153 headers:** the fake template
`template< typename E = void >`. A class template's member bodies are emitted only where the template
is **instantiated**. So:

- a module that *includes* a decoder header pays **parse time**;
- only a module that *instantiates* a decoder pays **object size**.

That turns an unconditional cost into an opt-in one — and it makes two rules non-negotiable (§3.4).

> **Note on the measurements.** The figures above are `a64` `gcc1520` `debug`. The size policy is
> calibrated on `x86` `debug`, which is where it is *enforced*, and this machine cannot build that
> combination. Every object-size acceptance in the plan must therefore be measured on the enforcing
> platform, not inferred from these. Debug→release ratios measured elsewhere in this project ranged
> 1.86–2.67, so do not assume 2×.

---

## 3. The facade

### 3.1 Placement

The facade is generic and does not belong to the HTTP client. Following this project's rule that
generic pieces are placed generically:

```
src/include/baselib/compression/
    Decompressor.h              the concept, the sink type, the exception contract
    Brotli.h                    BrotliDecompressorT
    Zstd.h                      ZstdDecompressorT
    Zlib.h                      InflateDecompressorT, GzipDecompressorT
    detail/
        embedded/
            brotli_<ver>.h      generated — see E5 for where this actually lives
            zstd_<ver>.h
            zlib_<ver>.h
```

The HTTP binding is a single adapter in `httpclient/`, not a second copy of the logic.

### 3.2 No second virtual interface

`httpclient::ContentDecoder` is already the virtual interface, and it is HTTP-flavoured
(`contentCoding()` returns an HTTP token). A generic `bl::compression::Decompressor` **abstract base
would add a second vtable for the same shape**.

Instead the generic layer is a **static concept**, satisfied by each codec class template:

```cpp
// A decompressor satisfies:
//   static const char* formatName() NOEXCEPT      "br", "gzip", "deflate", "zstd"
//   void write( const void* data, std::size_t size, const sink_t& sink )
//   void finish( const sink_t& sink )
```

and one adapter template binds any of them to the HTTP seam:

```cpp
template< typename DECOMPRESSOR, typename E = void >
class CodecContentDecoderT : public httpclient::ContentDecoder { ... };
```

Consequences: no virtual dispatch inside the hot decode loop; each codec is instantiated only where
used; and a non-HTTP consumer uses the codec directly with no HTTP types in scope.

### 3.3 Shaped for compressors later (E1)

Each codec header will later gain a sibling `BrotliCompressorT` with the mirror shape
(`write`/`finish` over a sink). Nothing in the decompressor's shape, naming or placement assumes
decode-only — the word "Decompressor" is in the type name, not in the namespace or the header name, so
adding an encoder needs no move and no rename.

**No encoder is vendored now.** The transformation manifest names only decoder translation units, and
the plan's acceptance criteria assert that no encoder symbol is present.

### 3.4 The two rules that keep the size cost opt-in

1. **Nothing in `Baselib.h`, `ClientSession.h` or any default registration may reach a codec header.**
   A consumer registers decoders explicitly. A test module that does not decode pays nothing.
2. **Each codec is behind its own build flag**, following the `BL_USE_JSON_SPIRIT` convention already
   in `projects/make/3rd/` — tested by *value*, not definedness, defaulting off:
   `BL_USE_EMBEDDED_BROTLI`, `BL_USE_EMBEDDED_ZSTD`, `BL_USE_EMBEDDED_ZLIB`.

Rule 1 is a behavioural change to the seam as shipped: the client will *not* silently gain decoders.
That is deliberate, and §7 states what it costs.

---

## 4. Where the generated headers live — E5, OPEN

Two placements, and the requirement does not settle it by itself.

**(a) In the repository's include tree.** `src/include/baselib/compression/detail/embedded/`. This is
what the requirement literally asks for: a consumer who has baselib has the decoders, with no dist and
no install. Cost: roughly 15k lines of generated third-party-derived code enters the repository, its
diffs, its greps and its review surface — in a repository whose README advertises Boost and OpenSSL
minimalism.

**(b) In the devenv dist,** beside `json-spirit`, reached by an `INCLUDE +=` in a
`projects/make/3rd/` module. This is the **existing precedent**: json-spirit is header-only, lives at
`<dist>/json-spirit/4.08/source`, is consumed by include path, and is gated by `BL_USE_JSON_SPIRIT`
defaulting off. Cost: it **does not satisfy the requirement** — a consumer now needs the dist.

**Recommendation: (a), with the generated tree clearly marked and excluded from ordinary review.** The
requirement is the whole point of the exercise; (b) reproduces the dependency the exercise exists to
remove. But (a) is a repository-shape decision, not a technical one, and it is the maintainer's.

---

## 5. The transformation

### 5.1 Model

```
upstream C source (pinned tag + hash)
        |
        |  scripts/compression/vendorize.py  — deterministic, reproducible, checked in
        v
one generated header per codec, namespaced, inline, warning-isolated
```

The generator is **checked in and run by a human**, not part of the build. Its output is committed.
The build never invokes it; the build only compiles the generated header.

### 5.2 What the transform must do

| Step | Why |
|---|---|
| Concatenate the decoder TUs in dependency order, inlining upstream's internal `#include`s | one header, no include graph |
| Wrap everything in `namespace bl::compression::detail::<codec>_<ver>` | isolation from every other symbol in the program |
| File-scope functions → `inline` (**not** `static`) | Strategy A of the research note: one logical implementation, ODR-safe, no per-TU duplication |
| File-scope `const` tables → `inline constexpr` where they are genuinely constant | deduplicated, and provably read-only |
| Prefix and `#undef` every upstream macro at the end | macros do not respect namespaces; §5.4 |
| Wrap in a warning push/pop with this project's `-Werror` in mind | upstream C does not compile warning-clean as C++ under `-Wall -Wpedantic -Wextra -Werror` |
| Assert **zero mutable file-scope state** | §5.3 |

`static` is explicitly rejected as the mechanism: it gives every translation unit a private copy of the
code, the tables and any state, which against a 75 MB ceiling is the opposite of what is needed. The
research note's Strategy B is a spike technique only.

### 5.3 State and thread safety — a gate, not a note

Each codec must be shown to have **no mutable file-scope state**, so that an `inline` transformation is
ODR-correct and two threads decoding on different streams cannot interfere.

The decoders are expected to be clean here — they are designed around a caller-owned state struct — but
"expected" is not "established". The transform **fails loudly** on any non-`const` file-scope
definition rather than emitting it, and each such find is resolved deliberately (made `const`, or
moved into the per-instance state) and recorded.

Lazily-initialised tables, if any exist, are the dangerous case: they are mutable file-scope state that
looks const. They must be either precomputed into `constexpr` data by the generator or moved into the
instance.

### 5.4 Macro and warning isolation

Upstream C uses short macro names (`MIN`, `MAX`, `ERROR`, `BROTLI_*`, `ZSTD_*`, `HUF_*`). A header that
leaks those into a consumer's translation unit is unacceptable in a header-only library. The generator
emits a trailing `#undef` block covering every macro it defined, and the plan asserts this by compiling
a probe TU that defines its own `MIN`/`MAX`/`ERROR` *after* the codec header and checks they survive.

### 5.5 Allocation

Upstream decoders take allocator callbacks. The embedded form binds them to this library's ordinary
allocation, so that a decode failure raises the same way as any other allocation failure here rather
than returning a C error code that the facade would have to re-interpret.

### 5.6 SIMD and architecture

Upstream decoders carry `#ifdef`-selected SIMD paths. The transform keeps them; it does not flatten to
a portable path, because the portable path is the slow one and correctness is unaffected. What it must
do is ensure no path depends on a build-system define that the consumer's build will not set. The
architecture matrix here is `a64`, `x64`, `x86` — all three must compile and produce identical output
on the golden vectors.

---

## 6. Verification

Five tiers. The first two gate the vendoring; the rest gate adoption.

1. **Golden vectors from upstream's own test corpus.** Decode them, compare byte-for-byte. This is the
   evidence that the transform preserved behaviour, and it is the only evidence that does.
2. **Round-trip against a reference encoder.** Not available in-repo — compress externally with the
   real library, commit the compressed fixture, decode here. Fixtures are test data, not code.
3. **libFuzzer harness per codec.** The byte-in/byte-out shape makes this straightforward, and §8
   makes it a standing obligation rather than a one-off.
4. **Multi-translation-unit test.** Two TUs in one module both instantiating the same codec, linked
   together — proves the `inline` transformation is ODR-correct and did not silently rely on internal
   linkage.
5. **Object-size measurement on the enforcing platform** (`win-x86-*-debug`), before adoption. §2.3.

**Test module placement.** The decoder cases do **not** go into `utf_baselib_httpclient4/5/6`, which
are at 65.1, 62.0 and 51.4 MB. They go into a new numbered sibling, sized deliberately. The existing
`ContentDecoder_NoDecompressorShipsTests` in `utf_baselib_httpclient` records today's behaviour and
must be revisited, not silently broken, when a decoder ships.

---

## 7. What this costs

| Cost | Detail |
|---|---|
| **Parse time, everywhere the header is included** | The reason for rule 1 of §3.4. Unmeasured; measure at L1. |
| **Object size, where instantiated** | Against a 75 MB ceiling with consumers already at 50–65 MB. Measure on x86 debug before adoption. |
| **~15k lines of generated code in the repository** | If E5 resolves to (a). Reviewable only as "regenerate and diff", not line by line. |
| **A standing fuzzing obligation** | §8. |
| **CVE response is harder than a version bump** | §8. |
| **Decoders are not registered by default** | Rule 1 of §3.4 means the client gains no decoder until a consumer asks. `accept-encoding` continues to deviate from a browser's unless the application registers, which is the deferral record's option-C shape, not its option-A shape. |

---

## 8. Security

**Decompressors parsing untrusted network input are a classic vulnerability class**, and the deferral
record already says so. Two consequences this design must own rather than inherit:

**Fuzzing is a standing obligation.** Upstream fuzzes continuously; a vendored *fork* does not inherit
that. The harness of §6.3 must exist before a codec is registered anywhere, and must be run on every
regeneration.

**A CVE costs more here than a version bump.** With an external library, a CVE is a rebuild at a new
tag. Here it is: fetch the new upstream, re-run the transform, re-verify against golden vectors,
re-run the fuzzer, re-measure object size, and review a large generated diff. The manifest must
therefore pin **upstream tag and content hash** so that "which version are we carrying, and is it
affected" is answerable in one command — that question being the first one asked when an advisory
lands.

---

## 9. What this design forecloses: certificate compression (E4)

The deferral record's item 6 notes that with real libraries present, OpenSSL can be built with
`enable-brotli enable-zlib enable-zstd`, giving RFC 8879 certificate compression and the
`compress_certificate` extension in the ClientHello.

**Embedded header-only decoders do not give you that, and cannot be made to.** Verified on
2026-09-20 against the dist's OpenSSL 3.5.4 headers: `SSL_CTX_compress_certs`,
`SSL_CTX_set1_cert_comp_preference` and `SSL_OP_NO_TX_CERTIFICATE_COMPRESSION` all exist, but they
drive OpenSSL's own internal COMP framework, wired to the libraries **at OpenSSL's build time**. There
is no hook to register an external decompressor. This build is configured `no-shared` and nothing
else, so none of it is compiled in.

**Two findings that make this a smaller loss than it looks, and one that makes it a real one.**

- Certificate compression is **one of three** missing extensions. Design §6.3 records that what stands
  between OpenSSL and Chrome's JA4 is "ALPS, certificate compression and ECH-GREASE". Adding one of
  three does not produce a match; Chrome's grade stays `Ja4Candidate` either way. JA3 cannot match
  regardless, for reasons unrelated to compression (extension order, point formats).
- Firefox and Safari are graded `Approximate` for unrelated reasons entirely.
- **But it cannot be faked, and that is this design's own finding.** Design §6.3 proposes closing JA4
  gaps with `SSL_CTX_add_custom_ext`, since JA4 hashes extension *type codes* only — sound for GREASE
  and an inert ALPS placeholder. It is **not** sound for `compress_certificate`: advertising it invites
  the server to send a `CompressedCertificate`, and a client that cannot decompress one fails the
  handshake. That is the same trap the design already names for a genuine ALPS advertisement.

**So certificate compression has exactly one real solution — the deferral's option A — and this design
is not it.** The two are independent rather than competing: this closes content decoding at the HTTP
layer and costs nothing that exists today. If impersonation against real targets later makes the
ClientHello gap decisive, option A remains available and would supersede the embedded decoders for
that purpose only.

---

## 10. Alternatives not taken

| Option | Why not |
|---|---|
| **A — external libraries** (deferral record) | Build scripts per OS × architecture × variant, three `projects/make/3rd/` modules, three supply-chain entries, and it breaks the README's Boost-and-OpenSSL minimalism. Retains one decisive advantage: certificate compression (§9). |
| **B — in-house decoders** | ~5,000 lines of security-sensitive parsing owned here, against untrusted input, with no upstream fuzzing to inherit. E2 rejects this: upstream's correctness is the asset. |
| **C — in-house inflate only** | Coherent and cheap, but leaves `br` and `zstd` to the application and does not answer the requirement. |
| **D — seam only, permanently** | Status quo. |
| **Boost.Iostreams** | Its filters wrap the same external libraries at Boost build time, adds a compiled Boost library, and has no Brotli filter at all. |

---

## 11. Open questions

1. **E5 — repository or dist** (§4). Blocks L1.
2. **Does Brotli's 122 KB dictionary survive as `inline constexpr` data** without dominating object
   size or compile time? This is the single largest technical risk, and E3 exists to answer it first.
3. **Does upstream C compile as C++ under `-Wall -Wpedantic -Wextra -Werror`** after transformation,
   on `gcc1520`, `clang2010` and `msvc`, for `a64`, `x64` and `x86`? Unknown until attempted.
4. **What is the real per-codec object-size delta on `win-x86-*-debug`?** Everything in §2.3 is a64.
5. **Is there mutable file-scope state** in any of the three decoders (§5.3)?
6. **Does the client register decoders by default once they exist?** Rule 1 of §3.4 says no; that
   leaves `accept-encoding` deviating. Revisit when a decoder ships.

---

## 12. References

- `notes/plans/embedded-compression-design.md` — the research note this follows
- `notes/plans/issues/http-content-decoders-deferral.md` — the deferral this feeds, options A–D
- `notes/plans/http2-design.md` §6.3 — the ClientHello fidelity analysis §9 relies on
- `src/include/baselib/httpclient/ContentDecoder.h` — the seam
- `src/utests/AGENTS.md` — the object-size policy
- `projects/make/3rd/json-spirit/` — the header-only third-party precedent
