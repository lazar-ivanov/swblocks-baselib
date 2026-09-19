# Boost.Beast is present in the devenv7 dist - the D15 probe, answered

**Probed:** 2026-09-18, by slice S1.5. **Status:** ANSWERED, affirmative. No action follows from it;
this record exists because decision **D15** is taken later, by S2.5, and is taken partly on this.

> **D15 has since been taken: Beast stays.** The three criteria this record leaves open were all
> answered affirmative by S2.5 - see `d15-http1-codec-backend-verdict.md`, which carries the
> coverage probe, the measured object-size delta and the three leniencies the facade closes. The
> residual toolchain risk stated at the bottom of this record was answered negative for gcc by
> `l1-gcc-toolchain-coverage-record.md`; Windows remains argued.

The plan's risk table carries the row *"Beast headers present in the dist | S1.5 | Beast is usable at
all | vendor Beast headers (header-only)"*. **The fallback is not needed.**

## The finding

`<boost/beast/http/basic_parser.hpp>` - the one Beast facility design §5.5 says the HTTP/1.1 codec
would use - is present, at:

```
<DIST_ROOT_DEPS3>/boost/1.90.0/<variant>/include/boost/beast/http/basic_parser.hpp
```

for every one of the four build variants the dist ships: `ub24-a64-clang2010-debug`,
`ub24-a64-clang2010-release`, `ub24-a64-gcc1520-debug`, `ub24-a64-gcc1520-release`. 24355 bytes in
each.

The surrounding tree is complete, not a partial install: **255 files** under `include/boost/beast`,
and the md5 of the whole tree, concatenated in sorted order, is **identical in all four variants**
(`9104ee6456d1ca2e7026fbfe45c4c527`). `boost/beast/http/` carries `fields.hpp`, `message.hpp`,
`parser.hpp`, `serializer.hpp`, `chunk_encode.hpp`, `field.hpp`, `verb.hpp`, `status.hpp`,
`rfc7230.hpp`, `type_traits.hpp`, `error.hpp` and the `detail/` and `impl/` subdirectories - so the
parts §5.5 says are *not* used are there too, and their absence is not what would decide D15.

## What this settles, and what it does not

**Settled.** Availability. Beast does not have to be vendored, there is no dist rebuild, and S2.5
chooses on the other three criteria of §5.5 rather than on whether the headers exist.

**Also shown, by S1.5's own probes** (clang2010 debug, linux-a64, the project's own flags, `-Wall
-Wpedantic -Wextra -Werror`):

- A translation unit including only `core/detail/BeastBoostImports.h` compiles clean, zero warnings.
- A response parser derived from `basic_parser< false >`, implementing all ten virtuals and naming
  **no `boost::beast` name at all** - only the `bl::beast` imports plus the library's own
  `eh::error_code` and `asio::const_buffer` - compiles, links and parses a response end to end:
  status code, the exact field-name casing as it arrived, and the body. So the isolation §5.5 asks
  for is achievable rather than merely intended, and the imported name list is sufficient for the
  usage §5.5 describes.
- The narrow includes pull in **31** Beast headers. That is the compile surface, offered as a data
  point; the criterion that matters is the *object-size delta of the consuming test module*, and
  that is measured by S2.5, not predicted here.

**Not settled - the three criteria D15 actually turns on** (§5.5), all of them S2.5's:

1. that it builds as C++11 on all four devenv7 toolchains (S1.5 demonstrated clang2010 debug on
   linux-a64 only; the others are argued, not measured - see below);
2. the object-size delta against the 40 MB target of `src/utests/AGENTS.md`;
3. that `basic_parser`, used sans-I/O, covers chunked bodies with trailers, read-until-close,
   bodiless responses, interim 1xx responses, and the header and body limits; and that its
   strictness reaches the smuggling defenses of §5.5, with the facade adding what it does not.

## Two facts worth carrying into S2.5

- **`put()` is not eager by default.** It returns as soon as the header is complete and leaves the
  body for a later call. S1.5's probe read `consumed=52` and an empty body until `eager( true )` was
  set, after which it read `consumed=57`, `code=200`, `body='hello'`, `is_done()`. A codec which
  feeds a whole buffer and expects the body callbacks to have run will be wrong about that.
- **Every virtual is declared unconditionally on the primary template.** A response-only parser -
  `basic_parser< false >` - must still override `on_request_impl`, whose signature names
  `beast::http::verb`. That is why `verb` is in the import header despite this library serializing
  its own requests by hand.

## The toolchain coverage this record rests on

Demonstrated: **clang2010, debug, linux-a64**. Argued for the other three (gcc1520 on Linux; msvc and
clang on Windows):

- The import header contains no code - includes and ten `using` declarations. It has no template, no
  function, no class and no macro of its own, so what could differ between toolchains is the Beast
  headers' own portability and whether a name exists.
- **Whether a name exists cannot differ between the dist's toolchains**: the Beast header trees are
  byte-identical across all four variants, measured above. The same text is compiled either way.
- Beast is header-only and is part of Boost's own CI on MSVC, gcc and clang.
- The MSVC-specific hazard this library actually has with Boost headers is the `/analyze` warning
  set, and that is exactly what `BoostIncludeGuardPush.h`/`BoostIncludeGuardPop.h` exist for. The
  import header is wrapped in them, as every other Boost import header here is.

**The residual risk, stated rather than argued away:** gcc's and MSVC's warning sets are not clang's,
and every configuration is `-Werror`. A warning clang does not emit would fail a build that has not
been run. Nothing in this record claims otherwise.
