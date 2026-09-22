# Embedded Decompression: Implementation Plan

**Status:** written 2026-09-20 as a plan with **nothing built, nothing probed and no source vendored**.
Every estimate is an estimate. Every acceptance criterion that names a number requires a measurement,
not an inference.

**Spec.** This plan implements `notes/plans/embedded-decompression-design.md` ("the design"). The
design is the specification; this plan is the work breakdown, the gating order and the verification
method. Decision ids `E1`–`E5` are the design's ledger (its §0). Where a slice says "implements §X",
read that section of the design — the plan does not restate it.

**Before you start any slice, read:** the root `AGENTS.md`, `src/utests/AGENTS.md`, the design in full,
`notes/plans/issues/http-content-decoders-deferral.md`, and
`src/include/baselib/httpclient/ContentDecoder.h`. The library's idioms are learned from the existing
headers, not from this plan.

---

## 0. The shape of this plan, and why it is unusual

**This plan front-loads its own falsification.** Layer L1 exists to try to kill the design, on the
hardest of the three codecs (E3). If Brotli cannot be transformed into an ODR-safe header that
compiles warning-clean on three toolchains and three architectures, and whose object-size cost fits
under a ceiling that consumers are already within 10 MB of, then **the right outcome is to stop and
record why** — not to proceed to Zstd and zlib and discover it three times.

Consequently:

- **L0 is useful on its own.** It ships the facade and the HTTP binding with no vendored code. If
  everything after it is abandoned, L0 still leaves the seam better specified and an application-
  supplied decoder easier to write.
- **G-E1, at the end of L1, is a real go/no-go**, with named criteria (§3.5). It is the only gate in
  this plan whose failure means "abandon the approach" rather than "fix and retry".
- **L2 and L3 are near-mechanical if L1 passes**, because the pipeline is the deliverable of L1 and
  Zstd and zlib are easier inputs to it.

**One codec at a time.** There is no parallelism across L1–L3: they share one generator, and a second
codec entering before the first is proven would obscure which input broke it.

---

## 1. Blocking prerequisite

**E5 must be decided before L1** — whether generated headers live in the repository include tree or in
the devenv dist (design §4). It determines the generator's output path, the makefile work in S1.1, and
whether `projects/make/3rd/` gains a module at all. L0 does not depend on it.

There are no other blocking decisions. E1–E4 are taken.

---

## 2. Layer L0 — the facade and the HTTP binding, with no vendored code

**Goal:** everything in design §3, provable with a decoder written by hand in a dozen lines. No
upstream source, no generator, no flags.

### S0.1 — the generic decompressor concept

**Implements** design §3.1, §3.2.

**Deliverables**
- `src/include/baselib/compression/Decompressor.h` — the sink callback type, the exception contract
  for a malformed stream, and the documented static concept (`formatName`, `write`, `finish`).
- No abstract base class. The concept is documentation plus a compile-time check, not a vtable.

**Acceptance**
- A hand-written `IdentityDecompressorT` in the test tree satisfies the concept and passes bytes
  through unchanged.
- The header pulls in nothing from `httpclient/` — checked by compiling a TU that includes only it.

### S0.2 — the HTTP adapter

**Implements** design §3.2.

**Deliverables**
- `CodecContentDecoderT< DECOMPRESSOR >` binding any conforming codec to `httpclient::ContentDecoder`.
- `contentCoding()` returns the codec's `formatName()`.

**Depends on** S0.1.

**Acceptance**
- `CodecContentDecoderT< IdentityDecompressorT<> >` drives `ContentDecoderStreamT` end to end.
- **The caps still bite.** A case proves that a codec whose output exceeds the ratio or absolute cap is
  stopped by `ContentDecoderStreamT` and not by the codec — i.e. the codec remains cap-ignorant, as
  the seam intends.
- A codec that throws mid-stream surfaces as the seam's malformed-stream exception, not as a codec
  type escaping the abstraction.

### S0.3 — the registration story

**Implements** design §3.4 rule 1.

**Deliverables**
- The documented way an application registers a codec-backed decoder on a session.
- A note at the registration point stating that no codec is registered by default and why.

**Depends on** S0.2.

**Acceptance**
- Registering a decoder changes the session's `accept-encoding` to include its coding, and strict mode
  behaves as `ContentDecoder.h` already specifies.
- `ContentDecoder_NoDecompressorShipsTests` still passes unchanged — L0 ships no decompressor.

**L0 gate.** Focused modules only; no full-suite gate. L0 touches no existing code path except the
registration documentation.

---

## 3. Layer L1 — the pipeline, and Brotli (E3)

**Goal:** one generated header, one codec, and enough measurement to decide whether the approach
survives.

### S1.1 — the vendoring manifest and the generator skeleton

**Implements** design §5.1, §8.

**Deliverables**
- `scripts/compression/manifest.json` — per codec: upstream repository, pinned tag, content hash, and
  the **explicit list of decoder translation units** (design §2.2). No encoder unit appears.
- `scripts/compression/vendorize.py` — fetch-verify-transform-emit, deterministic, run by a human and
  never by the build.
- Output header carries a generated-file banner: upstream tag, hash, generator version, date, and
  "do not edit".

**Probes**
- Confirm the decoder-only unit list actually links a decoder with no encoder symbol. This is the
  first real contact with upstream's source layout and may change §2.2's table.

**Acceptance**
- Running the generator twice produces byte-identical output.
- The manifest answers "which upstream version are we carrying" in one command (design §8).
- `.venv/bin/python` is used, per the root `AGENTS.md`.

### S1.2 — the transform, on Brotli

**Implements** design §5.2, §5.3, §5.4, §5.5.

**Deliverables**
- Namespacing, `inline` conversion, `constexpr` tables, macro `#undef` block, warning push/pop.
- **A hard failure on any non-`const` file-scope definition** (design §5.3), with each finding resolved
  deliberately and recorded — not silently emitted.

**Depends on** S1.1.

**Probes** — each of these can fail the design, so run them early and report rather than work around:
- Does upstream C compile as C++ at all, after transformation? (implicit `void*` conversions, C99
  designated initializers, `restrict`.)
- Does the 122 KB dictionary survive as `inline constexpr` data?
- Is there mutable file-scope state?

**Acceptance**
- Compiles under `-Wall -Wpedantic -Wextra -Werror` on `gcc1520` **and** `clang2010`.
- The macro-isolation probe passes: a TU defining its own `MIN`, `MAX`, `ERROR` *after* the codec
  header still sees its own definitions.
- Zero mutable file-scope state, or every instance recorded with its resolution.

### S1.3 — `BrotliDecompressorT` and its correctness evidence

**Implements** design §3.1, §6.1, §6.2.

**Deliverables**
- The codec class template wrapping the generated implementation, satisfying S0.1's concept.
- Golden vectors from upstream's own test corpus, committed as test data.
- Compressed fixtures produced by the **real** Brotli library outside this repo, committed.

**Depends on** S1.2, S0.1.

**Acceptance**
- Every golden vector decodes byte-for-byte.
- **A negative control**: a corrupted fixture must fail, and a truncated one must fail at `finish()`
  with the seam's malformed-stream exception. A decoder that accepts corrupt input silently is worse
  than no decoder.
- Streaming is exercised: the same fixture fed one byte at a time produces identical output to a
  single `write()`.

### S1.4 — the ODR and multi-TU proof

**Implements** design §6.4.

**Deliverables**
- Two translation units in one test module, both instantiating `BrotliDecompressorT`, linked together.

**Depends on** S1.3.

**Acceptance**
- Links with no duplicate-symbol error and no ODR diagnostic.
- **This is the proof that the `inline` transform is right** — a `static` transform would also link
  here, so the check that discriminates is that the two TUs share one implementation. Establish that
  by symbol inspection, not by the link succeeding.

### S1.5 — the fuzzing harness

**Implements** design §6.3, §8.

**Deliverables**
- A libFuzzer harness over the byte-in/byte-out surface, with a seed corpus from the golden vectors.
- The instruction that it is re-run on every regeneration.

**Depends on** S1.3.

**Acceptance**
- The harness builds and runs.
- **A positive control**: it finds a deliberately introduced bug in a scratch build. A fuzzer that has
  never found anything is indistinguishable from one that cannot.

### S1.6 — object size and compile time, measured

**Implements** design §2.3, §6.5, §7.

**Deliverables**
- Object-size delta for a module that instantiates the codec, and for one that only includes the
  header, **on `win-x86-*-debug`** — the enforcing platform.
- Compile-time delta for both cases.
- A new numbered test module for the decoder cases (design §6), sized deliberately.

**Depends on** S1.3.

**Acceptance**
- Both numbers recorded in the design's §7, replacing the estimates.
- **No module over the 75 MB ceiling, and none over 40 MB without a recorded reason.**
- The include-only delta is small enough that rule 1 of design §3.4 is a sufficient protection — if it
  is not, that is a finding, and the rule must become stronger rather than the number accepted.

### 3.5 — G-E1, the go/no-go gate

Run after S1.6. **This gate can end the approach.** It passes only if all of:

| # | Criterion |
|---|---|
| 1 | Brotli's decoder compiles as C++ warning-clean on `gcc1520` and `clang2010`, for `a64`, `x64` and `x86` |
| 2 | Every golden vector decodes byte-for-byte, and the negative controls fail as they must |
| 3 | Zero mutable file-scope state, or every instance resolved and recorded |
| 4 | The multi-TU test links and shares one implementation |
| 5 | No test module exceeds the ceiling on the enforcing platform |
| 6 | The include-only compile-time cost is acceptable for a header that consumers may include |
| 7 | The generator is deterministic and the manifest pins tag and hash |

**If 1, 2, 3 or 4 fails, the approach is wrong** and the outcome is a record saying so, with the
evidence — which redirects the deferral record to options A or C rather than leaving the question open.

**If 5 or 6 fails, the approach is right but the placement is wrong** — revisit design §3.4, possibly
making codecs opt-in per translation unit rather than per build.

**A whole-suite gate is required** before L1 merges, per the project's rule that changes to existing
core paths gate separately: L0's adapter touches the HTTP client's decoding path.

---

## 4. Layer L2 — Zstd

Runs only if G-E1 passed. Mechanically parallel to L1, with the pipeline already built.

| Slice | Content |
|---|---|
| **S2.1** | Manifest entry, decoder-only unit list, generation |
| **S2.2** | `ZstdDecompressorT`, golden vectors, negative controls, streaming |
| **S2.3** | Multi-TU proof, fuzzing harness with positive control |
| **S2.4** | Object size and compile time on the enforcing platform |

**New risk not present in Brotli:** Zstd's decoder carries more architecture-conditional SIMD than
Brotli's (design §5.6). S2.1 must establish that no selected path depends on a define the consumer's
build will not set, and S2.2 must show identical output across `a64`, `x64` and `x86`.

**Acceptance**: as L1's, minus the dictionary question, plus the SIMD-parity check.

---

## 5. Layer L3 — zlib (`inflate`, and `gzip` framing)

Runs only if G-E1 passed. Expected to be the easiest input, and it is deliberately last: it is the one
that would have succeeded regardless, so it proves least.

| Slice | Content |
|---|---|
| **S3.1** | Manifest entry, `inflate` unit list, generation |
| **S3.2** | `InflateDecompressorT` (raw deflate) and `GzipDecompressorT` (gzip framing over it) |
| **S3.3** | Multi-TU proof, fuzzing harness with positive control |
| **S3.4** | Object size and compile time |

**Note on two codings from one codec.** HTTP's `deflate` and `gzip` differ only in framing, and
real-world `deflate` responses are inconsistent about which framing they use. S3.2 must decide and
record whether `GzipDecompressorT` tolerates a raw-deflate body, as browsers do — a fidelity question,
not a correctness one.

---

## 6. Layer L4 — adoption

Runs only when at least one codec has passed its layer. Each codec can be adopted independently.

### S4.1 — registration and `accept-encoding`

**Deliverables**
- The decoders registered in whatever way design §3.4 rule 1 permits — explicitly, by the application.
- The per-profile `accept-encoding` deviation removed **only for codings actually registered**.

**Acceptance**
- `ContentDecoder_NoDecompressorShipsTests` revisited deliberately, not silently broken (design §6).
- The deferral record updated: items 3, 4 and 5 closed for the codings that shipped, item 6 left open
  with design §9's finding recorded — that faking `compress_certificate` is unsafe, so option A is its
  only solution.

### S4.2 — end to end, against a real server

**Deliverables**
- A case that fetches a compressed body over the real HTTP client and decodes it.

**Acceptance**
- Composed with the real driver and the real session, not a stub — this project's own rule, and the
  one that found every defect of consequence in the HTTP/2 work.

---

## 7. Schedule and gating order

```
L0  (facade + adapter + registration)       — independent, useful alone
      |
      |  E5 decided
      v
L1  (pipeline + Brotli)  ──► G-E1  ──► stop, or continue
      |
      +──► L2 (Zstd)        independent of L3
      +──► L3 (zlib)        independent of L2
              |
              v
            L4 (adoption, per codec)
```

L2 and L3 are parallel with each other once G-E1 passes. Nothing else is parallel.

---

## 8. What this plan does not cover

- **Compressors.** E1 — the facade is shaped for them (design §3.3); no slice builds one.
- **Certificate compression.** E4 and design §9 — not reachable by this route.
- **Registering decoders by default.** Design §3.4 rule 1 says no; revisit when a codec ships.
- **The `accept-encoding` fidelity gap** in full: it closes only for codings actually registered.

---

## 9. Standing obligations this creates

These outlive the plan and belong to whoever owns the library afterwards.

1. **Re-run the fuzzers on every regeneration** (design §8).
2. **Watch upstream advisories for three codecs**, and answer "are we affected" from the manifest.
3. **A CVE is a regeneration, not a version bump** — transform, verify, fuzz, re-measure, review a
   large generated diff.
4. **Keep the generated tree out of ordinary review** and regenerate rather than hand-patch. A
   hand-edited generated header is the failure mode that makes the next CVE unanswerable.
