# JSON Duplicate Object Keys: Contract Record

This document records the library's contract for a JSON document which contains the same object
member name more than once, why that contract was chosen rather than the alternative of rejecting
such documents, and what a consumer who needs the rejecting behaviour should do.

**Finding:** CXX-01 — "Boost.JSON accepts duplicate object keys; json-spirit rejects them" (High),
`notes/plans/issues/pr-review-deep-dive-residual-findings.md:49-68`

**Analysis and decision:** `notes/plans/issues/pr-review-residual-cxx-findings-plan.md`

---

## The contract

**Duplicate object member names are not a supported input shape, and the behaviour for a document
which contains them is backend-defined.**

| Backend | Selected by | Behaviour |
|---|---|---|
| Boost.JSON | default on devenv7+ | The last of the equal members is kept, the earlier ones are discarded |
| json-spirit | `BL_USE_JSON_SPIRIT=1`, and automatically on devenv2-6 | The document is rejected; `bl::UserMessageException` is thrown during parsing |

The contract is stated on `bl::json::readFromString` in `src/include/baselib/core/JsonUtils.h`, with
pointers to it at both Boost.JSON parse entry points (`BoostJsonImpl.h`, one-shot and streaming) and
on `WrapperConfig::add` in `JsonSpiritImpl.h`.

No code behaviour was changed. This is a documentation-only disposition.

---

## Why the parsers differ, and why that is not a defect in either

RFC 8259 section 4 says the names within an object **SHOULD** be unique — not MUST — and states that
when they are not, "the behavior of software that receives such an object is unpredictable". The
specification deliberately leaves the handling to the implementation, which is why parsers disagree
and why neither behaviour can be called non-conformant.

Boost.JSON documents its choice explicitly in its parser reference: *"If there are object elements
with duplicate keys; that is, if multiple elements in an object have keys that compare equal, only
the last equivalent element will be inserted."* That is the same choice JavaScript's `JSON.parse`,
Python's `json` and Go's `encoding/json` make, and RFC 7515 section 4 (JWS) and RFC 7519 section 4
(JWT) explicitly permit it — a JOSE recipient may either reject duplicates *or* use a parser which
returns the lexically last member.

json-spirit's rejection is not a designed policy either; it is a consequence of this repository's
`WrapperConfig::add` using `std::map::emplace` and treating a failed insertion as an error. Note
that it throws `bl::UserMessageException`, not `bl::JsonException` like every other parse error path
in that file, which is a further sign it was not designed as a parser policy.

---

## Why the two backends were not made to agree

Three options were considered.

| Option | Cost | Why not taken |
|---|---|---|
| Tighten Boost.JSON to reject duplicates | ~4-6 days, plus a 10-30% parse cost on **every** document | `boost::json::parse_options` has no duplicate-key setting and the standing request for an I-JSON mode ([boostorg/json#127](https://github.com/boostorg/json/issues/127)) is unresolved. Detection cannot be done after parsing — the DOM has already discarded the earlier member — so it requires a `basic_parser` handler which tracks the member names seen per object. That is a permanent cost on the hot path of the library's default backend, paid on every document to defend against an input shape which is already invalid |
| Relax json-spirit to last-value-wins | ~0.5 day, no perf cost | Would remove a check which currently exists on the legacy builds. Cheap, but it buys uniformity by deleting a defence rather than by adding one |
| Document the difference, change nothing | ~2 hours | **Taken** |

The deciding consideration is that neither uniform behaviour would remove the real risk. The risk
worth naming is a **parser differential**: a document with duplicate member names read one way by
this library and another way by a peer built against a different parser, in a different language.
That risk exists against every non-baselib peer regardless of which behaviour the two in-repo
backends share, so paying a permanent parse cost to make them agree with each other buys very
little.

---

## What a consumer who needs rejection should do

Two options, in order of preference:

1. **Reject duplicate member names before the document reaches this library**, at the trust boundary
   where the document arrives, alongside whatever other schema validation is applied there. This is
   the option to choose when the requirement is security relevant, because it also covers the
   differential against non-baselib peers, which the backend choice does not.

2. **Build against the json-spirit backend**, which rejects such documents during parsing. That
   backend remains supported and is documented in `CONTRIBUTING.md`
   (`make -k -j4 BL_USE_JSON_SPIRIT=1`); the headers are installed by the devenv7 setup scripts.

   Note the caveat below before relying on this.

---

## Caveat on the json-spirit backend

"Supported" here means buildable on demand and documented. It does **not** mean continuously
verified: no build or test target in this repository sets `BL_USE_JSON_SPIRIT`, and there is no CI.
Its behaviour also differs from the default backend in ways beyond duplicate keys, and it is
substantially slower — see `notes/performance/json-library-performance-comparison.md`, which
measures it at 1.1x to 5.5x slower than Boost.JSON in release builds and far worse in debug.

A consumer who selects it for the duplicate-key behaviour is taking on the rest of those
differences as well, which is why rejecting at the trust boundary is the recommended option.

---

## Related

- `rawUtf8` and the numeric conversion policy, the other two halves of the JSON compatibility
  surface, were closed by making the two backends agree — see CXX-08 in
  `notes/plans/issues/pr-review-residual-cxx-findings-plan.md`. Duplicate keys are the one part
  where agreement was judged not to be worth its cost.
- The parser resource limits sub-finding of CXX-01 (no shared limit on input size, depth, member
  count or string length) is untouched by this record. Boost.JSON applies its own default maximum
  nesting depth of 32; json-spirit applies none.
