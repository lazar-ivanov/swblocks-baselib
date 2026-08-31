# Public-Header Warning Scope and Diff Hygiene: Deferral Record

This document records a deliberate decision **not** to change two compatibility constructs in the
public headers in this cycle, and the conditions under which that decision must be revisited. It
also records why one part of the originating finding was rejected rather than deferred.

**Finding:** F-18 — "Public-header warning scope and diff hygiene regressions" (Low),
`notes/reviews/swblocks-baselib/major/update_2025/v1/pr_review_analysis_gpt56sol.md:269-278`

---

## Decision

**Date:** 2026-08-30
**Status:** Partially addressed; two items deferred to the next major cleanup / toolchain upgrade

F-18 bundles four unrelated items under a single Low-severity label. They do not share a risk
profile, so they were not given a single disposition:

| # | Item | Disposition |
|---|---|---|
| 1 | Global `-Woverloaded-virtual` suppression in `Compiler.h` | **Deferred** |
| 2 | `BOOST_ASIO_DISABLE_STD_CHRONO` defined from `OSBoostImports.h` | **Deferred** |
| 3 | `mem*` / `fill_n` in public headers without `<cstring>` / `<algorithm>` | **Fixed** |
| 4 | Trailing whitespace in `StringUtils.h` and `BoostAsioCompat.h` | **Fixed** |
| 5 | "Make `git diff --check master...lazari2` clean before merge" | **Rejected as stated; rescoped** |

**Items 1 and 2 are a risk acceptance, not an assessment that the risk is absent.**

---

## Item 1 — Global `-Woverloaded-virtual` suppression

**Location:** `src/include/baselib/core/Compiler.h:36-38`

```
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 15
#pragma GCC diagnostic ignored "-Woverloaded-virtual"
#endif
```

The pragma is at file scope with no `push` / `pop`. `Compiler.h` is included transitively almost
everywhere, so on GCC 15+ the warning is suppressed in essentially every translation unit, not just
around the `BL_VARIADIC_CREATE_INSTANCE` construct the accompanying comment describes.
`-Woverloaded-virtual` catches real bugs — an override that silently hides a base overload.

### Why this is deferred

The build compiles with `-Wall -Wpedantic -Wextra` and, unless `GCC_DISABLE_STRICT=1`, `-Werror`
(`projects/make/toolchain/gcc-default.mk:271` and `:314`). Narrowing or removing the suppression
therefore does not produce warnings to triage — it produces **hard build failures**, across a
surface of 175 `BL_DECLARE_OBJECT_IMPL*` declaration sites (183 textual occurrences less 8 macro definitions). The number of genuine hits is unknown until a full
GCC 15 build is run with the pragma removed, and the work to fix each one is unbounded at the point
of the decision. The benefit is entirely prospective: catching a future accidental hiding bug.

### What limits the exposure

The pragma is gated on `defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 15`. Clang and MSVC
consumers are unaffected, as are all GCC versions below 15. The blast radius is real but narrower
than "all downstream translation units."

### Review consensus

Four of the six review passes flagged this independently, and all four rated it advisory / low:

- `pr_review_analysis_opus5.md:356` (M-7)
- `pr_review_analysis_opus46.md:258` (CC-1)
- `pr_review_analysis_grok46.md:380` (CC-1)
- `pr_review_analysis_gpt56sol_deep_dive.md:545`

No reviewer identified an actual hidden-overload defect caused by the suppression.

### Conditions to revisit

- The next time the GCC baseline moves (devenv8 or a GCC 16 toolchain).
- Before then, the cheap measurement step is: build once with the pragma removed under
  `GCC_DISABLE_STRICT=1` so warnings do not fail the build, and count the real hits. That number
  turns this from an unbounded task into a scoped one. Do the measurement before committing to the
  narrowing.
- The preferred end state is `_Pragma` push / ignored / pop inside the `BL_VARIADIC_CREATE_INSTANCE`
  macro body, so the suppression travels with the construct that needs it. This must be validated on
  a real GCC 15 build — whether the diagnostic is attributed to the macro expansion site or to class
  completion determines whether the scoped form actually suppresses it.

---

## Item 2 — `BOOST_ASIO_DISABLE_STD_CHRONO` defined from a public header

**Location:** `src/include/baselib/core/detail/OSBoostImports.h:29`

The macro is defined unconditionally before `<boost/thread.hpp>` and `<boost/asio.hpp>`, with no
`#undef` and no guard.

### The hazard is ODR, not just API availability

F-18 describes the consequence as "downstream Boost.Asio API availability becomes include-order
dependent." That understates it. `BOOST_ASIO_DISABLE_STD_CHRONO` changes the definitions of
Boost.Asio's waitable-timer types. A translation unit that includes `<boost/asio.hpp>` **before** any
baselib header gets different definitions than one that includes baselib first. Two such TUs linked
into the same binary are an ODR violation, which manifests as silent misbehaviour rather than a
diagnostic.

Within baselib itself the ordering is consistent, because everything reaches Boost.Asio through
`OSBoostImports.h`. The exposure is to **external consumers** who mix their own direct Boost.Asio
includes with baselib includes across translation units.

### Why this is deferred

Every candidate fix trades one hazard for another:

- **Move the define to `projects/make/common.mk` as `CPPFLAGS`** (suggested in
  `pr_review_analysis_opus5.md:124`). This makes the setting uniform for anything built by the
  project's own makefiles, and **breaks external consumers**, who compile against baselib headers
  with their own build flags and would no longer receive the define at all. That is precisely the
  case the header define exists to cover. It converts a conditional hazard into an unconditional one
  for third-party builds.
- **Remove the define.** Requires a verified GCC 15 / Boost 1.90 build to establish that it is not
  load-bearing. It was introduced alongside the Boost 1.89+ compatibility work and its necessity has
  not been re-tested.
- **Add a compile-time `#error` guard** that fires when `<boost/asio.hpp>` was already included
  without the macro (detectable via `BOOST_ASIO_DETAIL_CONFIG_HPP`). This was considered and
  deliberately not taken this cycle: it cannot change generated code and would convert a silent ODR
  mismatch into a diagnostic, but it is a behaviour change shipped to downstream builds without a
  verified build matrix behind it — it would break consumers whose builds currently appear to work.
  This remains the cheapest future mitigation and should be the first thing tried when this is
  revisited.

### Related

`UuidBoostImports.h` has the same class of problem — a compatibility macro whose effect depends on
include order — and is tracked separately as F-16 (Medium), where the consequence reaches binary
wire layout. F-16 should be fixed on its own merits; it is not covered by this deferral.

### Conditions to revisit

- The next Boost major upgrade.
- Any observed Boost.Asio timer type or symbol mismatch at link time.
- Any report from an external consumer of asio behaviour differing by translation unit.

---

## Item 5 — Why the `git diff --check` requirement was rescoped

F-18 and the review's verification list (`pr_review_analysis_gpt56sol.md:346`) require a clean
`git diff --check master...lazari2`. That requirement is incorrect as stated. Measured breakdown of
the 97 reported failures:

| Count | Location | Assessment |
|---|---|---|
| 17 | `CONTRIBUTING.DEVENV3.md`, `CONTRIBUTING.DEVENV4.md` | **Not defects.** These are two-space line endings — Markdown hard line breaks. `git diff --check` does not understand Markdown. "Fixing" them changes rendered output. |
| 16 | `notes/` | Local working notes, not shipped and not part of the library. |
| 16 | `scripts/devenv7/` (docker, rosetta, install scripts) | Cosmetic `new blank line at EOF`. Harmless; left for a future cleanup pass. |
| 8 | `src/include/baselib/core/` | Real. Fixed — see Item 4. |

**The merge gate should be `git diff --check <merge-base> -- src`, not repo-wide.** That is clean as
of this record.

---

## What was fixed

**Item 3 — header self-containment.** `CPP.h`'s `char_traits< unsigned char >` specialization uses
`std::fill_n`, `std::memmove`, `std::memcpy`, `std::memcmp` and `std::memchr`, and names
`std::mbstate_t`, `std::streamoff` and `std::streampos`; `Compiler.h`'s `safeMemsetImpl` uses
`std::memset`. Neither header declared those dependencies. Added `<algorithm>`, `<cstring>`,
`<cwchar>` and `<iosfwd>` to `CPP.h` and `<cstring>` to `Compiler.h`.

This was preventive, not a repair of a live breakage: both headers compiled before the change. An
include trace under Clang 20 showed `<cstring>` arriving at nesting depth 16 and `<cwchar>` /
`<iosfwd>` at depth 17, entirely through a Boost → libc++ chain rooted at `BaseDefs.h`. The headers
compiled by coincidence of another library's include graph. Note that both affected regions are
toolchain-gated — the `CPP.h` specialization on `__clang__ >= 20`, `safeMemsetImpl` on
`__GNUC__ >= 15` — so only the devenv7 toolchains compile them at all.

**Item 4 — trailing whitespace.** Removed from `StringUtils.h` (6 lines) and `BoostAsioCompat.h`
(2 lines). The complete set of trailing-whitespace lines in both files was exactly the set this
branch introduced, so no pre-existing content was touched.
