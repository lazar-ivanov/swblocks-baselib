# Test Template Instantiation Weight: Deferral Record

This document records why the test module object size policy has two tiers — a 40MB target and a
55MB hard ceiling — rather than the single 40MB limit the sibling record proposed, and the work that
was **not** done: the template instantiation weight of the test suites is high enough that a handful
of modules cannot reach 40MB by any arrangement of files.

**It is a risk acceptance, not an assessment that the concern is absent.** The weight is real, it is
still in the tree, and the 55MB allowance buys headroom rather than removing the cause.

**Origin:** the `utf_baselib_security` pilot of the test module split, 2026-09-12. Measured, not
inferred — see the numbers below.

**Companion:** [x86-clang-cl-host-and-test-module-size-deferral.md](x86-clang-cl-host-and-test-module-size-deferral.md),
whose item 3 this work is part of. Plan and running state:
[../../reviews/major/update_2026/test-module-split-plan.md](../../reviews/major/update_2026/test-module-split-plan.md)
and its ledger.

---

## Decision

**Date:** 2026-09-12
**Status:** **Two-tier size policy applied; instantiation weight reduction deferred.**

| # | Item | Disposition |
|---|---|---|
| 1 | Object size policy for a unit-test TU on x86 debug | **target 40MB, hard ceiling 55MB**, applied 2026-09-12 |
| 2 | Reduce the instantiation weight of the authorization cache test stack | **Deferred** — this record |
| 3 | Survey the other test helper stacks for the same problem | **Partly answered** — `HttpServerHelpers` confirmed to have the same shape, 2026-09-12; messaging, blob transfer, tasks and REST still unmeasured |
| 4 | Retire the 55MB allowance and hold every module at the 40MB target, once item 2 lands | **Deferred** — optional even then |

---

## The limitation

Every test module pays a fixed floor of about **21.4MB** — `UtfMain.h` plus baselib plus the
header-only Boost.Test runner. `utf_baselib_setprio` is 223 lines of test source and still produces a
21.4MB object. The 40MB target therefore allows roughly **19MB** of marginal content per module.

Measured on `ARCH=x86 TOOLCHAIN=vc143 VARIANT=debug`, with one throwaway module per probe:

| Probe | Object | Marginal over the floor |
|---|---:|---:|
| Floor — `UtfMain.h` only (`utf_baselib_setprio`) | 21.4 MB | — |
| **Including** `utests/baselib/HttpServerHelpers.h` | 21.9 MB | 0.5 MB |
| **Including** `utests/baselib/TestAuthorizationCacheImplUtils.h` | 21.9 MB | 0.5 MB |
| **Using** it — `TestAuthorizationCacheImpl.h` alone | 47.3 MB | **25.9 MB** |
| **Using** it — `TestAuthorizationCacheRestImpl.h` alone | 49.4 MB | **28.0 MB** |
| `TestAuthorizationServiceRest.h` alone | 37.7 MB | 16.3 MB |
| **Including** `utests/baselib/UtfBaseLibCommon.h` | 21.5 MB | 0.1 MB |
| **Including** `apps/bl-messaging-http-gateway/MessagingHttpGatewayApp.h` | 22.6 MB | 1.2 MB |
| **Using** it — `TestMessagingApps.h`, **one** test case | 65.4 MB | **44.0 MB** |
| **Instantiating** `TestRestUtils::startBrokerAndRunTests` alone | **60.5 MB** | **39.1 MB** |
| **Instantiating** `TestMessagingUtils::createTestMessagingBackend` alone | 42.3 MB | 20.9 MB |

**Including a template helper costs nothing; instantiating it costs about 26MB.** So any module
containing even one authorization-cache test case exceeds the 40MB target before any sibling header
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

Two tiers were set in `scripts/utests/utf_objsize.py` and `scripts/utests/check_split.sh`:

- **Target 40MB.** What a split aims for. `utf_objsize.py` reports every object above it without
  failing, and each one needs a recorded reason in the split ledger why the target was not reachable.
- **Hard ceiling 55MB.** The gate fails above it.

**Why a target and not just a limit.** Splitting merely to below the limit is how this problem
recurs. The first attempt at `utf_baselib_http` landed at 54.9MB against a 55MB limit — compliant
that day, and red the next time anyone adds an HTTP test case, with the work to be redone. The
February 2026 `-gline-tables-only` workaround bought about seven months for the same reason: it
relieved pressure without leaving room. A target well below the limit is what makes the limit hold
over time rather than only today.

Why 55MB for the limit:

- The 32-bit `clang-cl` host died on a **110MB** translation unit. 55MB is a factor of two below it,
  so the headroom is large rather than marginal.
- It is reachable by moving whole files, which is the low-risk operation the split plan is built on
  and which the verification tooling can prove faithful.
- It is where the pilot's measurements say some modules must land: `utf_baselib_security3` cannot
  reach the target at all, for the reasons above.

Modules currently between the target and the ceiling, each with its reason:

| Module | Object | Why the target is not reachable |
|---|---:|---|
| `utf_baselib_security3` | 52.4 MB | authorization cache instantiation, ~26MB shared across its headers |
| `utf_baselib_http` | 54.9 MB | `HttpServerHelpers` server instantiation, ~30MB shared across its headers |
| `utf_baselib_data` | 49.8 MB | not yet attempted |

### `utf_baselib_rest` cannot be split into compliance at all

`TestRestUtils::startBrokerAndRunTests` instantiated on its own, in a module containing nothing but
one trivial assertion, produces a **60.5MB** object. That is **above the 55MB ceiling before a single
real test case is added**, and `utf_baselib_rest` has 88 references to that helper across its 13
server cases.

So no arrangement of that module's files reaches the ceiling — not a two way split, not a thirteen
way one. Its header split was **not attempted**, because it cannot succeed and would produce two
non-compliant modules plus a large diff to review.

This does **not** generalise to the other large modules, which was the working assumption until it
was measured:

| Module | Stack it drives | Floor | Verdict |
|---|---|---:|---|
| `utf_baselib_rest` | `TestRestUtils` (88 refs) | **60.5 MB** | **blocked** — floor exceeds the ceiling |
| `utf_baselib_messaging` | `TestMessagingUtils` (97 refs) | 42.3 MB | splittable, 12.7MB of headroom per module |
| `utf_baselib_io` | neither, but drives `BrokerFacade` and the TCP block transfer stack directly | ~30 MB shared | **blocked** — measured, see below |

The difference is `BrokerFacade::execute` plus the REST bridge, which `startBrokerAndRunTests` pulls
and `createTestMessagingBackend` does not: 39.1MB against 20.9MB.

### `utf_baselib_rest` cannot comply at 75MB either, and this is the strongest measurement in the record

The 60.5MB figure above is what `startBrokerAndRunTests` costs on its own. The module's cases share
considerably more than that. Splitting the header six cases against nine, a near even cut by case
count, gave:

| | Object | Cases |
|---|---:|---:|
| `utf_baselib_rest` before | 78.7 MB | 15 |
| `utf_baselib_rest` after | 76.8 MB | 6 |
| `utf_baselib_rest2` | 77.5 MB | 9 |

76.8 + 77.5 against 78.7 means roughly **75.6MB is shared** — very nearly the whole module. Each
half is within 2MB of the original while holding under half the cases, so **every possible partition
of this module lands near 76MB**, including a fifteen way one. The split was reverted.

`utf_baselib_rest` is therefore the one module which cannot be brought under the 75MB ceiling by any
means available to this work. It is over by 3.7MB and no arrangement of files reduces it. Closing it
out requires item 2 of this record and nothing else.

### `utf_baselib_messaging` cannot comply either, and the shed rate is the proof

Cases were removed from the module in two stages and the object measured each time:

| Cases in the header | Module object | Shed |
|---:|---:|---|
| 38 | 112.7 MB | — |
| 31 | 100.6 MB | 12.1 MB for 7 cases |
| 22 | **95.6 MB** | **5.0 MB for 9 more** |

Removing 16 of 38 cases bought 17MB. Reaching 75MB needs 20.6MB more at roughly 0.55MB per case,
which is about 37 further cases when only 22 remain. **The shared instantiation is around 90MB and
the cases themselves are nearly free**, so no partition of this module reaches the ceiling.

The attempt also ran into the coupling the structural analysis had predicted:
`IO_MessagingProxyBackendTests` is welded to `exceptionThrowHook2` and `exceptionThrowHook3` in the
first anonymous namespace, so that cut needs a helper hoist as well - more work in service of a
target which is unreachable regardless. Reverted.

The step A header split is kept: it is inert, and it leaves the file cut 31/7 for when the weight is
addressed.

### `utf_baselib_io` is blocked for the same reason, measured the expensive way

Unlike `rest`, this one was not predictable from a helper probe: `utf_baselib_io` drives neither
`TestRestUtils` nor `TestMessagingUtils`. It was measured by performing the split and looking.

| | Object | Cases |
|---|---:|---:|
| `utf_baselib_io` before | 77.4 MB | 32 |
| `utf_baselib_io` after moving the messaging client island | 72.3 MB | 27 |
| `utf_baselib_io2` | **56.9 MB** | **5** |

Five cases need **56.9MB standing alone** but removed only **5.1MB** from their parent — roughly
**30MB of shared TCP and messaging machinery** which either side instantiates in full. Both halves
end over the ceiling and the aggregate grows from 77MB to 129MB, so the split was **reverted**.

The header split (step A) was **kept**: it is verified inert, it turns a 7,442 line header into
6,166 + 1,348, and it leaves the module pre-split for when the weight is addressed.

**Above the ceiling, not merely the target:**

| Module | Object | Why nothing can be done by moving files |
|---|---:|---|
| `utf_baselib_rest` | 78.7 MB | its helper's floor alone is 60.5 MB; **not attempted** |
| `utf_baselib_rest` | 78.7 MB | ~75.6 MB shared; every partition lands near 76 MB |
| `utf_baselib_messaging` | 112.7 MB | ~90 MB shared; removing 16 of 38 cases bought 17 MB |
| `utf_baselib_apps2` | 65.4 MB | one module, one header, **one test case**. `MessagingApps_HttpGatewayTlsValidationTests` instantiates the whole messaging HTTP gateway application: 44MB for 165 lines of test source. There is no split available at any granularity short of deleting the case |

`utf_baselib_http` is the one to watch: it sits **0.1MB** under the ceiling, so it is the first module
that will go red when anyone adds an HTTP test case. That is the concrete cost of leaving item 2
undone, and it arrived on the very first module of the fan-out.

**What it does not do.** It does not make the translation units smaller, and it leaves the growth
unbounded in the same way the companion record describes. A module which acquires a second heavy
helper stack will cross 55MB as easily as it crossed 40MB, and nothing warns.

---

## What limits the exposure while this is open

- `utf_objsize.py` gates every build against the ceiling and reports everything above the target, so
  growth is now **detected** even though it is not prevented. That is new; before this work nothing
  measured object size at all.
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
3. **Narrowing includes is measured to be worthless and must not be attempted.** Every include
   probed so far is within 1.2MB of the bare floor: `HttpServerHelpers.h` 0.5MB,
   `TestAuthorizationCacheImplUtils.h` 0.5MB, `UtfBaseLibCommon.h` **0.1MB**,
   `MessagingHttpGatewayApp.h` 1.2MB. The cost appears on instantiation, never on inclusion.

   This specifically retires the "adjunct lever" proposed in §1.1 of the split plan, which
   hypothesised that `UtfBaseLibCommon.h` — an umbrella pulling the whole messaging, http, tasks and
   data stack, included by 20 test headers — was driving the object sizes. It is not. It costs
   0.1MB. That hypothesis is refuted and should not be revisited.

### Step 3 — survey the rest

**`HttpServerHelpers.h` has already been confirmed to have the same shape**, during the
`utf_baselib_http` split on 2026-09-12. Including it costs 0.5MB; standing up a server through it
costs roughly **30MB** of instantiation which every server case in the module shares. The
consequences were identical to the authorization cache:

- `utf_baselib_http` is 54.9MB with three server-test headers and **cannot reach the 40MB target** by
  moving files.
- Splitting `TestClientHttpTasks.h` — 12 cases — into a fourth module moved `utf_baselib_http` by
  **2.5MB** and produced a **51.1MB** object, leaving two modules near the ceiling instead of one.
  Reverted.

So this is not a peculiarity of the authorization cache. It is the shape of the test helper stacks
generally, and it is the reason the 40MB target needs the 55MB allowance at all.

Still **unmeasured**: `TestMessagingUtils.h`, `TestBlobTransferUtils.h`, `TestTaskUtils.h` and
`TestRestUtils.h`. The modules that use them — messaging at 112.7MB, rest at 78.7MB, io at 77.4MB —
are the largest in the tree, and on the evidence so far they should be expected to behave the same
way. **Measure before planning their splits**, because the number of modules a split needs cannot be
predicted when most of the weight is shared.

### Step 4 — revisit the ceiling

If the weight comes down materially, holding every module at the 40MB target becomes possible and the 55MB allowance can be retired. It is
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
