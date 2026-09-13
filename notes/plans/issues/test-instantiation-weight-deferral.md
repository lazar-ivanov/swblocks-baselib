# Test Template Instantiation Weight: Deferral Record

This document records why the test module object ceiling was set to 55MB rather than the 40MB the
sibling record proposed, and the work that was **not** done: the template instantiation weight of the
test suites is high enough that a handful of modules cannot reach 40MB by any arrangement of files.

**It is a risk acceptance, not an assessment that the concern is absent.** The weight is real, it is
still in the tree, and 55MB buys headroom rather than removing the cause.

**Origin:** the `utf_baselib_security` pilot of the test module split, 2026-09-12. Measured, not
inferred — see the numbers below.

**Companion:** [x86-clang-cl-host-and-test-module-size-deferral.md](x86-clang-cl-host-and-test-module-size-deferral.md),
whose item 3 this work is part of. Plan and running state:
[../../reviews/major/update_2026/test-module-split-plan.md](../../reviews/major/update_2026/test-module-split-plan.md)
and its ledger.

---

## Decision

**Date:** 2026-09-12
**Status:** **Ceiling raised to 55MB; instantiation weight reduction deferred.**

| # | Item | Disposition |
|---|---|---|
| 1 | Object ceiling for a unit-test TU on x86 debug | **55MB**, applied 2026-09-12 |
| 2 | Reduce the instantiation weight of the authorization cache test stack | **Deferred** — this record |
| 3 | Survey the other test helper stacks for the same problem | **Deferred** — blocked on item 2's approach |
| 4 | Revisit the 40MB ceiling once item 2 lands | **Deferred** — optional even then |

---

## The limitation

Every test module pays a fixed floor of about **21.4MB** — `UtfMain.h` plus baselib plus the
header-only Boost.Test runner. `utf_baselib_setprio` is 223 lines of test source and still produces a
21.4MB object. A 40MB ceiling therefore allows roughly **19MB** of marginal content per module.

Measured on `ARCH=x86 TOOLCHAIN=vc143 VARIANT=debug`, with one throwaway module per probe:

| Probe | Object | Marginal over the floor |
|---|---:|---:|
| Floor — `UtfMain.h` only (`utf_baselib_setprio`) | 21.4 MB | — |
| **Including** `utests/baselib/HttpServerHelpers.h` | 21.9 MB | 0.5 MB |
| **Including** `utests/baselib/TestAuthorizationCacheImplUtils.h` | 21.9 MB | 0.5 MB |
| **Using** it — `TestAuthorizationCacheImpl.h` alone | 47.3 MB | **25.9 MB** |
| **Using** it — `TestAuthorizationCacheRestImpl.h` alone | 49.4 MB | **28.0 MB** |
| `TestAuthorizationServiceRest.h` alone | 37.7 MB | 16.3 MB |

**Including a template helper costs nothing; instantiating it costs about 26MB.** So any module
containing even one authorization-cache test case exceeds a 40MB ceiling before any sibling header
joins it. That is not a splitting problem and no rearrangement of files addresses it.

### Marginal costs are not additive

`TestAuthorizationCacheImpl.h` and `TestAuthorizationCacheRestImpl.h` are 47.3MB and 49.4MB alone,
but **52.0MB together** — they force the same instantiations, so roughly 25MB is shared. This was
established the expensive way: splitting `TestAuthorizationServiceRest.h` (37.7MB standalone) out of
`utf_baselib_security3` moved that module by **0.4MB** while adding a 37.7MB object. That fourth
module was reverted.

The practical consequence is recorded in the split plan: a grouping cannot be bin-packed from
per-header measurements taken in isolation. Only a leave-one-out delta, or building the proposed
grouping itself, predicts the result.

---

## What was done instead

The ceiling was set to **55MB** in `scripts/utests/utf_objsize.py` and `scripts/utests/check_split.sh`.

Why that number:

- The 32-bit `clang-cl` host died on a **110MB** translation unit. 55MB is a factor of two below it,
  so the headroom is large rather than marginal.
- It is reachable by moving whole files, which is the low-risk operation the split plan is built on
  and which the verification tooling can prove faithful.
- It brings `utf_baselib_data` (49.8MB) into compliance with no work at all, and reduces
  `utf_baselib` (55.3MB), `utf_baselib_http` (55.8MB) and `utf_baselib_blobtransfer` (56.3MB) to
  moving a single small header each.
- 40MB was a round number chosen before any of this had been measured.

**What it does not do.** It does not make the translation units smaller, and it leaves the growth
unbounded in the same way the companion record describes. A module which acquires a second heavy
helper stack will cross 55MB as easily as it crossed 40MB, and nothing warns.

---

## What limits the exposure while this is open

- `utf_objsize.py` gates every build against the ceiling, so growth is now **detected** even though it
  is not prevented. That is new; before this work nothing measured object size at all.
- The x86 build works on a 64-bit clang-cl host, so nothing is blocked today.
- The pilot reduced `utf_baselib_security` from 65.0MB to a 52.4MB maximum, and the same treatment is
  planned for the nine remaining modules, so the trend is downward.

---

## The work, when it is picked up

### Step 1 — find where the instantiation weight actually is

Start with `utests/baselib/TestAuthorizationCacheImplUtils.h` and the `AuthorizationCache` stack it
drives. The probe method is in the split plan: one throwaway module per candidate, built at
`ARCH=x86 TOOLCHAIN=vc143 VARIANT=debug`, object size minus the 21.4MB floor. Note that the cost
appears on **use**, not on include, so a probe must odr-use the entity to measure it.

`-d1reportTime` on MSVC, or `-ftime-trace` under clang-cl, will attribute it per template rather than
per header and is worth the setup cost before changing anything.

### Step 2 — reduce it

Candidates, cheapest first:

1. **Explicit instantiation.** Move the common instantiations into one translation unit per module
   and declare them `extern template` in the test helper, so each test header stops re-instantiating
   the same specialisations.
2. **Reduce template depth.** The cache stack is templated on a policy which has few real
   instantiations in tests; a non-template base holding the bulk of the logic would collapse most of
   it.
3. **Narrow the helper's own includes** — but note this was measured and is *not* where the weight
   is: including the helper costs 0.5MB. Do not start here.

### Step 3 — survey the rest

The authorization cache is the stack the pilot happened to land on. `TestMessagingUtils.h`,
`TestBlobTransferUtils.h`, `TestTaskUtils.h` and `TestRestUtils.h` are the other shared helpers with
plausible weight, and the modules that use them — messaging at 112.7MB, rest at 78.7MB, io at 77.4MB
— are the largest in the tree. Whether they have the same shape is **unmeasured**.

### Step 4 — revisit the ceiling

If the weight comes down materially, lowering the ceiling back toward 40MB becomes possible. It is
worth doing only if the growth trend justifies it; the companion record's real requirement is that no
TU exhausts a 32-bit compiler, which 55MB already satisfies with a wide margin.

---

## Conditions to revisit

- **Any test module's x86 debug object approaches 80MB.** That is the midpoint between the new
  ceiling and the band in which the 32-bit host died, and it is the signal that 55MB is being consumed
  rather than merely occupied.
- **A module cannot be brought under 55MB by moving files.** The pilot showed `utf_baselib_security3`
  could not reach 40MB; if the same happens at 55MB for messaging, rest or io, this item stops being
  optional.
- **Compile time or build-machine memory becomes a complaint.** Instantiation weight is compile time
  as well as object size, and these are the largest single compilations in the repository.
- **The 32-bit clang-cl host must be restored.** Item 5 of the companion record; the lower the
  objects, the more comfortable that becomes.

---

## Records to update when it lands

- This document: replace the Decision table dispositions with the outcome.
- `scripts/utests/utf_objsize.py`: the comment block above `DEFAULT_CEILING_MB` explains the 55MB
  choice and should be updated together with any change to it.
- The split plan and its ledger, which carry the probe table this record summarises.
