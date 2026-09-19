# The ASCII fold and whitespace trim go in `core/StringUtils.h`, as `str::ascii`

**Decided:** 2026-09-19, by the maintainer. **Status:** DECIDED, **not yet implemented.** It is its
own change-set, gated on the whole suite, scheduled **after L4**. Nothing is blocked on it.

Companion to `notes/plans/issues/http2-error-raiser-consolidation-record.md`, which settles the same
kind of question inside `http2/`. This one is the seam recorded in
`simplehttptask-locale-dependent-framing-record.md`.

## What is duplicated, and how far it has already drifted

Three helper families. **Two of the three have already drifted in name**, which is why this is not a
hypothetical tidy-up:

| Family | Copies | Names |
|---|---|---|
| Char fold `char -> char` | 4 | `toLowerAsciiChar` (`http/HeaderList.h:223`) vs `toLowerAscii` (`httpclient/CookieJar.h:228`, `httpclient/ContentDecoder.h:445`, `core/Uri.h:213`) — **drifted** |
| String fold `string -> string` | 4 | `toLowerAsciiCopy` (`httpclient/Http1Codec.h:1211`, `CookieJar.h:233`, `http2/HpackEncoder.h:308`, `core/Uri.h:238`) — consistent |
| Whitespace trim | 2 | `trimOwsCopy` (`Http1Codec.h:856`) vs `trimOws` (`CookieJar.h:270`) — **drifted** |

`Http1Codec.h` also carries `isOws:833` and `isDigitsOnly:765`.

Each copy arrived for the same reason - `str::to_lower_copy` is `boost::to_lower_copy` and folds
through `std::locale()`, a process-wide global an embedder may replace - and each is correct.

## The decision

**`core/StringUtils.h`, in a nested `str::ascii` namespace.**

1. **The two folds become `str::ascii::to_lower_copy`**, char and string overloads. Eight copies
   collapse to two functions.
2. **The whitespace pair goes to core too, named for what it does rather than for the RFC that
   wanted it**: `str::ascii::is_ws` and `str::ascii::trim_ws_copy`. They are pure "strip SP and
   HTAB"; the only thing HTTP about them is the name. An HTTP caller may keep a one-line `isOws`
   alias where the RFC vocabulary is worth having at the call site, documenting that RFC 9110's OWS
   is exactly SP and HTAB.
3. **`isDigitsOnly` stays put.** One copy, nothing to consolidate.

## Why `core/StringUtils.h`

- **`StringUtils.h:55` is literally where `str::to_lower_copy` is re-exported from boost** - the
  exact function all six classes had to stop using. The ASCII alternative belongs beside the
  locale-bound one it replaces, so anyone reaching for one sees the other. That adjacency is the
  point: this whole defect class was "someone reached for the obvious one and got the locale".
- **Zero new include edges.** All six classes already use `str::`, so all six already reach it.
- It is `core/`, so every layer above can use it.

**The nested namespace is deliberate.** `str::ascii::to_lower_copy( s )` differs from
`str::to_lower_copy( s )` by exactly one qualifier, so the call site *shows* which was chosen and a
reviewer knows it is locale-independent without opening anything.

## Two homes that were considered and rejected

**`http::HeaderList`'s public surface** - the seam note's original suggestion. **Ruled out by
layering, twice over.** `core/Uri.h` carries two of these folds, so a fold living in `http/` would
make `core/` depend on `http/`, which is backwards and which nothing in `core/` does today. And
`httpclient/CookieJar.h` depends on **`core/` only** - its includes are `Uri.h`, `TimeUtils.h`,
`OS.h`, `BaseIncludes.h` - so it would gain an `http/` edge it does not have.

**A new `core/AsciiUtils.h`.** It would avoid growing a 1391-line header that every translation unit
compiles, but nothing includes it, so all six would need a new include, and it loses the adjacency
to `to_lower_copy` that makes the right choice visible.

## The cost, stated honestly

`StringUtils.h` is core and reaches **every translation unit**, so by this project's rule this is a
core change and **lands as its own change-set gated on the whole suite** - unlike the `Globals.h`
consolidation, which is `http2/`-only.

What makes that cheap rather than risky: the core half is **purely additive** (new names, nothing
existing touched) and the six-class half is **purely subtractive with identical behaviour**, so the
gate should show no pre-existing case moving at all. Roughly +40 lines in core, -60 across six
headers.

## Two things for whoever implements it

- **The copies do not agree on idiom.** `Uri::toLowerAscii` compares `char` directly while
  `HpackEncoder::toLowerAsciiCopy` casts to `unsigned char` first. Both are correct for ASCII today -
  a negative `char` fails `>= 'A'` either way - but **the `unsigned char` form is the one to keep**,
  since it stays correct if anyone later compares against a value above 127.
- **Grep for tests asserting on the helpers by name** before starting. Several of these have
  locale-independence cases with hostile `ctype` facets pointed at them
  (`Http1Codec_LocaleIndependenceTests`, `Uri_LocaleIndependenceTests`); those cases must end up
  pointed at the shared function, not deleted with the private copy.

## Timing

Same window as the error raiser: **after L4**, when no lane is writing in these files. The seam was
recorded rather than fixed in the first place because closing it mid-layer would have conflicted
with lanes writing in neighbouring code for no gain.
