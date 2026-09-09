# The Single-Threaded `ObjectImpl` Configuration: Keep Decision

This document records the keep-or-delete decision for the `isMultiThreaded = false`
configuration of the object model - `bl::RefCountedBase< false >`,
`bl::om::ObjectImpl< T, ..., false >` and `bl::om::detail::ServerLifetimeTrackerT< false >` - which
has **no instantiation anywhere in the repository**.

**Origin:** `notes/reviews/major/update_2026/whole-library-cxx-test-enhancement-plan.md`, task T360, which notes this is *"dead configuration with
zero consumers and should be preceded by a keep-or-delete decision on the single-threaded
configuration - if it is to be deleted, this task disappears"*.

---

## Decision

**Date:** 2026-09-08
**Status:** **Kept**, and now type-checked and exercised by
`ObjModel_SingleThreadedObjectImplTests` in `src/utests/utf_baselib/TestObjModel.h`

| # | Item | Disposition |
|---|---|---|
| 1 | `RefCountedBase< isMultiThreaded >`'s `std::conditional` on the counter type (`RefCountedBase.h:42-45`) | **Kept** |
| 2 | `ObjectImpl`'s defaulted `LifetimeTracker = detail::ServerLifetimeTrackerT< isMultiThreaded >` (`ObjModel.h:1120`) | **Kept** |
| 3 | `ServerLifetimeTrackerT< false >`'s separate counter, invisible to `om::outstandingObjectRefs()` (`ObjModel.h:1007-1010`) | **Kept and documented by the test** |

---

## Why keep

**The parameter is part of the public template signature.** `om::ObjectImpl`'s third template
parameter is public API of a shipped header. Removing the `false` specialisation means changing that
signature, which is a breaking change for any downstream consumer that passes it - and the three
in-repository `ObjectImpl` typedefs pass `true` **explicitly** (`ObjModel.h:1481`, `:1595`,
`loader/Resolver.h:246`, `core/AppInitDone.h:98`), which is what a codebase looks like when the
parameter is understood to be meaningful rather than vestigial.

**The real defect was that it was never compiled, and that is now fixed.** The hazard T360 identifies
is not that the configuration exists but that no translation unit instantiated it, so a change to the
`std::conditional`, to the defaulted tracker parameter, or to the tracker's counter type could break
only the `false` specialisation and still ship green in every toolchain and variant. One test case
closes that: it instantiates both `ObjectImpl< T, false, false >` and `ObjectImpl< T, true, false >`
(the `SharedPtrImpl< true >` plus non-atomic-counter combination) and round-trips them through
`addRef` / `release`, `om::copy`, `om::tryQI` and `getSharedPtr`.

**Deleting it would be a larger change than keeping it.** `RefCountedBase`, `ObjectImpl` and
`ServerLifetimeTrackerT` are all parameterised on the same flag; unwinding it touches three headers
and the public signature, for no behavioural gain.

---

## What the test also documents

`om::outstandingObjectRefs()` reports **only** `ServerLifetimeTrackerT< true >::outstandingRefs()`.
Single-threaded objects are counted in a separate static that no global leak check ever reads. That
is deliberate, and the case asserts it directly - creating a single-threaded object leaves
`om::outstandingObjectRefs()` unchanged while
`ServerLifetimeTrackerT< false >::outstandingRefs()` increments by one.

Anyone who later makes the single-threaded configuration a real consumer must decide whether that
blind spot is acceptable, and this record plus that assertion is where they will find the question
already posed.

---

## Conditions to revisit

- A real consumer for the single-threaded configuration appears. At that point the tracker's
  invisibility to `om::outstandingObjectRefs()` stops being a documented curiosity and becomes a
  leak-detection gap that has to be closed.
- The object model's public template signatures are broken for another reason. Removing the flag
  then costs nothing extra, and `ObjModel_SingleThreadedObjectImplTests` is deleted with it.
