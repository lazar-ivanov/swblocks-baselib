# `SimpleHttpTask` parses response framing through the global locale

**Found:** 2026-09-19, by the lane fixing the same class of defect in the new HTTP/1.1 codec
(`Http1Codec.h`). **Status:** OPEN — pre-existing, in core code, deliberately not fixed there.
**Not introduced by the HTTP/2 work**, and not reachable from it: the new client does not use
`SimpleHttpTask`.

## What is there

`src/include/baselib/http/SimpleHttpTask.h` parses a response using functions that take
`std::locale()`, so their behaviour depends on a global an embedder may install:

- `:780` — `utils::lexical_cast< std::size_t >( contentLength )` on the response **`Content-Length`**.
  This is the closest analogue of the defect just fixed: a **framing** field parsed through a
  locale-sensitive path. `lexical_cast` runs the value through a stream, and a stream carries a
  `numpunct`, so digit grouping is a locale's to define.
- `:512`, `:745-748`, `:763`, `:787` — `str::to_lower_copy`, `str::trim`, `str::iequals` on header
  names and values.

## Why it was not fixed where it was found

Three reasons, all of which still hold:

1. **It is core code on a path every existing user of `SimpleHttpTask` takes**, so by the rule this
   project already follows it lands as its own change-set, gated on the suite, rather than riding
   along with a slice of a feature.
2. **The lane's brief scoped it to `httpclient/`**, and widening a fix into a legacy client while
   two other lanes were writing in neighbouring files would have been exactly the collision the lane
   structure exists to avoid.
3. **Nothing in the HTTP/2 client reaches it.** `httpclient/Http1Codec.h` is a separate codec; this
   is the older task-based client. So this is a latent defect in existing code, not a gap in new
   work.

## What the fix would look like

The same shape as the one applied to `Http1Codec.h`: an ASCII-only OWS trim and an ASCII-only fold,
plus a digits-only check before converting `Content-Length` rather than handing it to `lexical_cast`.
`http::HeaderList` already exposes `equalsIgnoreCase`, and `Http1Codec.h` now has `isOws`,
`trimOwsCopy`, `toLowerAsciiCopy` and `isDigitsOnly` to copy the shape from.

**Whoever takes this should read the negative control in `Http1Codec_LocaleIndependenceTests`
first.** It shows how to install a hostile `ctype` facet, prove it actually perturbs the functions
under test so the case cannot pass vacuously, and compute inside the window while asserting outside
it. It also records a platform fact worth knowing before writing the test: libc++ short-circuits
`ctype<char>::is()` to false for every non-ASCII octet, so an obs-text spelling is not reachable
through a locale there, while libstdc++ indexes its table for all 256. A comma is reachable on both.

## The related seam, recorded here rather than lost

Six classes now carry a private copy of the same ASCII fold — `HeaderList`, `CookieJar`,
`ContentDecoder`, `HpackEncoder`, `Uri` and `Http1Codec`. Each arrived at it for the same reason and
each is correct, but the duplication is the kind that drifts. The generic home is `HeaderList`'s
public surface or a small ASCII companion beside `str`. Consolidating touches headers that several
slices are writing in, so it belongs to a quiet moment rather than to a layer in flight.

**Addendum, L2 second pass (2026-09-19), and its correction.** The second pass found that `Uri`
carried the ASCII fold for its reg-name host but still folded the scheme (`Uri.h:850`) and an IP
literal (`:735`) through `str::to_lower_copy`, and deferred them here for the same reason as
`SimpleHttpTask`. **That deferral was wrong on its own terms and both sites are now fixed**
(`cd01896`): `Uri.h` is not pre-existing core code. It was added by this work in `b2c0d3e`, neither
`core/PreCompiled.h` nor `core/BaseIncludes.h` names it, and every includer is an `httpclient/`
header or a test from this feature - so reason 1 above, the rule about core paths every existing
user takes, does not reach it. It was the same free fix the sibling classes got, and `Uri` now
carries `toLowerAsciiCopy` under the same name and signature as the others.

The reason they were called fail-safe also needs restating, because it matters to whoever fixes
`SimpleHttpTask`: **both `Uri` sites validate before folding**, so a perturbed locale cannot widen
what is accepted at all - it corrupts the normalized spelling that comes out, and the fail-safe is
downstream, where consumers compare against a known ASCII spelling which a corrupted one fails to
match. `SimpleHttpTask`'s `Content-Length` is **not** of that shape: `lexical_cast` parses and
converts in one step, so there the locale reaches the accept/reject decision itself. It is the more
urgent of the two, not the less.

Two things from `Uri_LocaleIndependenceTests` are worth copying, beyond what the `Http1Codec` control
already shows. Overriding `do_tolower` while keeping `classic_table()` sidesteps the
libc++/libstdc++ divergence entirely - that divergence affects only perturbations spelled through
the mask table, while a `do_tolower` override is a virtual the fold reaches on both. And one
perturbation rule may not reach every site: `'I'` is not a hexadecimal digit, so the Turkish rule
never reaches an IPv6 address, and the facet needs a second rule leaving the six hexadecimal
letters unfolded.
