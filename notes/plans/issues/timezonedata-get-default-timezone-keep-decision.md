# `TimeZoneData::getDefaultTimeZone()`: Keep Decision

This document records the keep-or-delete decision for
`bl::time::TimeZoneDataT::getDefaultTimeZone()` in
[`src/include/baselib/core/TimeZoneData.h`](../../../src/include/baselib/core/TimeZoneData.h), which
has **no in-repository callers**.

**Origin:** `notes/reviews/major/update_2026/whole-library-cxx-test-enhancement-plan.md`, task T361, which is *"gated on a keep-or-delete decision
for `getDefaultTimeZone()`, which has zero in-repo callers - the same decision T268 raises"*.

---

## Decision

**Date:** 2026-09-08
**Status:** **Kept**; the `TZ` environment-variable leg of the POSIX branch is characterised by
`TestGetDefaultTimeZonePosixTzEnvVar` in `src/utests/utf_baselib/TestTimeZoneData.h`

| # | Leg | Disposition |
|---|---|---|
| 1 | Windows branch (registry / Windows-to-Olson mapping) | **Kept**, not covered here |
| 2 | POSIX: the `TZ` environment variable | **Kept and covered** - valid Olson, valid abbreviation, and the invalid-value warning |
| 3 | POSIX: `/etc/timezone` | **Kept**, deliberately **not** covered |
| 4 | POSIX: `/etc/sysconfig/clock` | **Kept**, deliberately **not** covered |
| 5 | POSIX: the `localtime_r` abbreviation fallback | **Kept**, deliberately **not** covered |

---

## Why keep

**It is the only machine-time-zone discovery the library offers, and it is public API.** The rest of
`TimeZoneData` answers questions *about* a named zone - `validateTimeZone`, `getTimeZoneOffset`,
`getTimeZoneDataFields`. `getDefaultTimeZone()` is the only entry point that answers "which zone is
this machine in", which is what any consumer formatting local timestamps for a user needs. Its
absence of an in-repository caller reflects what this repository happens to build, not whether the
function has a purpose.

**Its per-platform logic is not reconstructible cheaply.** The POSIX branch alone encodes an ordered
four-step discovery - `TZ`, then `/etc/timezone` (Debian/Ubuntu), then a quote-split parse of
`ZONE="..."` in `/etc/sysconfig/clock` (RedHat), then a `localtime_r` abbreviation - each gated on
`validateTimeZone` and each logging a distinct warning on rejection. Deleting it discards that and
guarantees the next consumer re-derives it, worse.

**The one genuinely risky leg is now pinned.** The `TZ` leg returns an operator-supplied string
verbatim if - and only if - `validateTimeZone` accepts it. Dropping that gate would make an arbitrary
string flow onward as a time-zone name and every downstream `getTimeZoneOffset` would throw. The new
case asserts both halves: a valid Olson name and a valid abbreviation are returned unchanged, and an
invalid value produces the warning naming the offending value rather than being returned.

---

## Why three legs are not covered

`/etc/timezone` and `/etc/sysconfig/clock` are **absolute paths**. A unit test must not create,
modify or delete them, and cannot make their absence deterministic on a machine where they exist. The
`localtime_r` fallback depends on the host's zone database and on glibc's caching of `TZ` (`tzset` is
not re-run by `localtime_r`), so its result is not a function of anything the test controls.

The invalid-`TZ` case therefore asserts **only** the warning: what the fall-through ultimately
returns depends on those three legs, and on a machine where none yields a valid zone the call throws
`"Cannot determine local time zone"`, which the test tolerates explicitly.

---

## Conditions to revisit

- A consumer for `getDefaultTimeZone()` appears in the repository. The three uncovered legs then
  need coverage, which means a seam - an injectable path root or a file-reader callback - rather than
  a test that touches `/etc`.
- The Windows branch is changed. It has no coverage at all and T361 does not add any.
