# D15 — the HTTP/1.1 codec stays on Boost.Beast

**Decided:** 2026-09-19, by slice S2.5, on the criteria design §5.5 states. **Status:** DECIDED,
affirmative. This closes the question `notes/plans/issues/beast-availability-probe-record.md` was
opened to feed.

Design §5.5 says the codec *"starts on Beast, to see where it goes"* and names four criteria. Beast
stays if all four hold; otherwise an in-house backend is written behind the same facade and nothing
above the facade changes. **All four hold.** The in-house backend was not written and remains one
sibling `detail/` header away.

## The four criteria

### 1. It builds as C++11 on the devenv7 toolchains against Boost 1.90 — holds, 2 of 4 demonstrated

Demonstrated on **clang2010 debug** (the whole test module, `-Wall -Wpedantic -Wextra -Werror`, zero
warnings) and on **gcc1520 release** (a single TU which derives from `basic_parser< false >` and
instantiates it, same flags, zero warnings). Windows — msvc and clang-cl — remains **argued and not
measured**, exactly as S1.5's record left it; the argument is unchanged and is recorded there.

### 2. The object-size delta is acceptable against the 40 MB target — holds, comfortably

Measured on a **matched pair** of translation units, identical but for the Beast half: both include
`httpclient/PreCompiled.h`, and the second additionally derives from `basic_parser< false >`,
overrides all ten virtuals and calls `put()`/`put_eof()`, so the templates are instantiated rather
than merely declared.

| toolchain / variant | without Beast | with Beast | delta |
|---|---:|---:|---:|
| clang2010 debug | 4,568,232 | 4,852,504 | **+284,272 B (+0.27 MB, +6.2%)** |
| gcc1520 release | 8,941,600 | 9,659,144 | **+717,544 B (+0.68 MB, +8.0%)** |

Both numbers are named with their toolchain deliberately: object sizes in this tree are
toolchain-dependent by about a factor of two and a clang debug figure is the small one
(`l1-gcc-toolchain-coverage-record.md`). Either way this is noise against a 40 MB target.

The consuming test module, `utf_baselib_httpclient`, went **24.0 MB → 25.0 MB** for the whole slice
— both production headers, six cases and their conformance vectors — so Beast's share of that is
about a quarter of it.

### 3. `basic_parser` used sans-I/O covers the codec's cases — holds, probed not read

A probe deriving from `basic_parser< false >` and tracing every callback, fed hand-written
responses:

| case | result |
|---|---|
| chunked with trailers | `on_chunk_header` / `on_chunk_body` per chunk, then **`on_trailer_field_impl` per trailer**, then `on_finish`. Chunk extensions delivered verbatim |
| read-until-close | `need_eof()` true, body delivered, `put_eof()` completes the message |
| 204 No Content | done at the end of the header section, no body callbacks |
| 304 carrying `Content-Length: 100` | bodiless; the length is ignored, which is RFC 9112 §6.3 |
| interim 1xx | parsed as a complete message |
| HEAD response with a length and no body | `skip( true )` completes it |
| header and body limits | `error::header_limit` and `error::body_limit`, the latter on both the identity and the chunked path |

**Two operational facts the codec is shaped by**, both from the probe:

- an interim 1xx consumes **only the interim message** and `is_done()` is then true. `put()` asserts
  `! is_done()` and the class offers no reset, so the facade detects the interim itself, discards
  the parser, builds another and continues on the same buffer.
- `skip( v )` asserts `! got_some()`, so whether the request was a HEAD must be known **before** the
  first byte. It is a constructor argument, not a setter.
- and one for every caller: **`basic_parser` does not buffer.** A `put()` which does not consume
  everything leaves the rest to the caller, who must present it again in front of the next bytes.

### 4. Its strictness reaches §5.5's smuggling defences — holds, with the facade adding three

§5.5 allows this explicitly: *"where Beast is more lenient than this design requires, the facade
adds the check itself — it sees every field as it arrives — and that counts as holding."*

**Beast refuses on its own**, measured: two `Content-Length` whose values differ; the list form
`5, 6`; `Content-Length` with `Transfer-Encoding: chunked` in **either** order; a duplicated
`Transfer-Encoding`; bare-LF line endings; a NUL in a value; a space before a colon; a non-token
field name; `+5`, `0x5` and `-5` as a length; a malformed chunk size; a four-digit status; and a
`Content-Length` arriving in the **trailer** section.

**The facade closes these, because Beast accepts them:**

1. **Obsolete line folding.** Beast silently *unfolds* — `X-A: one\r\n  two` is delivered as
   `X-A: 'one two'` — so a fold is undetectable from the callbacks. The facade scans the raw head
   and refuses a CRLF followed by SP or HTAB. The scan runs **ahead of** the backend, on a copy of
   the scanner, so no byte of a folded message reaches a body callback.
2. **A `Transfer-Encoding` which is not exactly `chunked`.** Beast accepts `gzip`, and accepts
   `chunked, gzip` by silently turning the message into a read-until-close body whose bytes are the
   raw chunk framing. It also accepts `Transfer-Encoding: gzip` **followed by** a `Content-Length`
   while refusing the same two fields in the other order — and an asymmetry of that shape is itself
   the differential. D9 ships no transfer decoder, so the only accepted value is the single token
   `chunked`.
3. **A `Content-Length` which is not `1*DIGIT`.** Beast deliberately accepts the comma list form,
   reading `Content-Length: 5, 5` as 5. RFC 9112 §8.6 gives the field the grammar `1*DIGIT`, so a
   list is invalid framing however consistent it is — and a list is what a proxy produces when it
   joins two separate `Content-Length` fields.

Two further checks the facade adds which are not leniencies of Beast's: the **64 KB header cap**
(Beast's own default is 8 KB, which is smaller than §5.5 requires), and a **forbidden field in the
trailer section** (RFC 9110 §6.5.1 forbids a longer list than the two Beast knows about).

Duplicated `Content-Length` fields with the **same** value are accepted, deliberately: RFC 9112
§6.3 makes only differing values invalid framing, so there is no second reading to pick between.
The case which pins it sits next to the one for differing values.

## What the decision rests on structurally

The choice is reversible because **no Beast type appears in any signature above
`httpclient/detail/Http1CodecBeastImpl.h`**, which a `grep` checks:

- `boost::beast` and `#include <boost/beast/...>` appear in `core/detail/BeastBoostImports.h` and
  nowhere else in `src/`
- `bl::beast` appears in that file and in the codec backend, and nowhere else
- the backend derives from `basic_parser< false >` **privately**, so no inherited member leaks; its
  sink passes text as `( const char*, size )`; and four static predicates taking
  `const eh::error_code&` are what lets the facade classify a refusal without naming Beast's error
  enumeration
- `httpclient/Http1Codec.h` is **not** in `httpclient/PreCompiled.h`, so §5.5's reachability
  property survives: measured with `-M`, `core/BaseIncludes.h`, `core/PreCompiled.h`,
  `http/PreCompiled.h`, `http2/PreCompiled.h` and `httpclient/PreCompiled.h` pull **0** Beast
  headers each, while a TU including the facade pulls **31**

## What this does not settle

- **Windows.** msvc and clang-cl are argued, not measured, for both the import header and the codec.
- **Performance.** Nothing here measures throughput; the criteria §5.5 lists do not include it and
  neither does this record.
- **The request side.** The serializer is in house by design and no criterion above applies to it.
