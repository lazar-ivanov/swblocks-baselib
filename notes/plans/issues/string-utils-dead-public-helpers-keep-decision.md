# Dead Public Helpers in `StringUtils.h`: Keep Decision

This document records the keep-or-delete decision for four public helpers in
[`src/include/baselib/core/StringUtils.h`](../../../src/include/baselib/core/StringUtils.h) which
have **no in-repository callers**, so that their retention is read as a decision and not re-filed as
dead code.

**Origin:** `CPP_TEST_ENHANCEMENT_PLAN.md`, task T355, which is *"gated on a keep-or-delete decision
for the four helpers ... take that decision before implementing and characterise only what
survives"*.

---

## Decision

**Date:** 2026-09-08
**Status:** All four **kept**; all four characterised by
`BaseLib_StringUtilsDeadHelpersTests` in `src/utests/utf_baselib/TestBaselibDefault10.h`

| # | Helper | Location | Disposition |
|---|---|---|---|
| 1 | `str::toBool` | `StringUtils.h:838-856` | **Kept** |
| 2 | `str::unquoteString` | `StringUtils.h:1274-1285` | **Kept** |
| 3 | `str::setToString` (`std::set` and `std::unordered_set` overloads) | `StringUtils.h:1128`, `:1144` | **Kept** |
| 4 | `str::formatTime` | `StringUtils.h:965-995` | **Kept** |

---

## Why keep

**These are public library API, not internal helpers.** `swblocks-baselib` is a library; the absence
of an *in-repository* caller says nothing about the absence of a downstream one. Every one of the
four lives in the `bl::str` namespace of a shipped public header, unqualified by any `detail`. There
is no deprecation mechanism in the codebase and no release in which their removal could be announced,
so deleting them is a silent breaking change for a consumer this repository cannot see.

**The cost of keeping is now zero-risk rather than unknown.** The argument for deleting untested,
uncalled code is that nobody knows what it does, so nobody can safely change anything near it. That
argument is answered by characterising it instead: after T355 all four have their current behaviour
pinned, including `toBool`'s rejection set and its user-friendly flag, `unquoteString`'s exact
one-quote-each-end rule, `setToString`'s default separator/header/footer rendering, and
`formatTime`'s 64-byte buffer ceiling.

**One of them carries a contract worth having a test on regardless.** `str::toBool` is the only
`BL_THROW_USER_FRIENDLY` site in the header and the only production-shaped consumer of the
`SafeInputStringStream` exception-mask contract - that mask covers `badbit` only, so `is.fail()` is
reachable. Had the mask ever been widened to include `failbit`, `toBool` would throw
`std::ios_base::failure` from the extraction instead of its own `ArgumentException`, and nothing
would have caught it. The characterisation test now would.

---

## Conditions to revisit

- A deprecation mechanism (an attribute, a `BL_DEPRECATED` macro, a release-notes process) is
  introduced. At that point marking these deprecated is nearly free and is the right first step.
- A survey of downstream consumers establishes that none of the four is used. Deletion is then a
  scoped, defensible change - and `BaseLib_StringUtilsDeadHelpersTests` is deleted with it.
- `str::formatTime`'s fixed 64-byte buffer becomes a problem for a real caller. The characterisation
  test pins the overflow as an `UnexpectedException`; growing the buffer would be a behaviour change
  and that test is the single place the expectation would flip.
