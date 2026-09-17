# A Build Gate for Test Module Size

**Status:** design, not implemented. Written 2026-09-13, after `utf_baselib_messaging` had to be
split four ways because it reached a 112.7MB translation unit and made two x86 build combinations
uncompilable.

**Goal.** Make that impossible to repeat *silently*. A developer adding a test case should see how
much room the module has before they add it, see what their change cost after they add it, and be
stopped by the build if it is too much — without having to know any of this history.

**Non-goal.** Stopping test modules from growing at all. They should grow; the tree should just never
again discover a limit by crashing into it.

---

## 1. What is measured, and why per object

**Per object file, not per module.** The constraint that bit is the compiler's ~2GB address space
while producing *one* object. A module with three 30MB objects is fine; a module with one 80MB object
is not. Reporting rolls up per module because that is the unit a developer moves things between, but
the limit applies per object.

Secondary figures worth reporting but **not** gating: the linked `.exe`, the `.pdb`, and per-module
totals. They cost disk and link time, not compile address space.

### What this gate cannot see

State these where developers will read them, or the gate will be trusted for more than it does.

- **Peak compiler memory.** `x86` + `ccl16` + `release` fails on full `-Zi` while producing a
  *smaller* object than its debug build. Object size does not predict it. Only building it does.
- **Compile time.** Correlated with instantiation weight but not measured here.
- **The a64 and x64 story is different.** There is no hard address-space wall on a 64-bit host, so
  limits there are about compile time and build-machine memory, and are advisory by nature.

---

## 2. Where the numbers live

### Measured baseline data, 2026-09-14

The full 12-combo matrix produced real numbers for every combination, replacing the single-combo
extrapolation this plan was originally written on. Peak object per combo, and the module holding it:

| Combo | Peak | Module |
|---|---:|---|
| `win-a64-vc143-release` | **125.32 MB** | `utf_baselib_io` |
| `win-x64-vc143-release` | 118.62 MB | `utf_baselib_io` |
| `win-x64-vc143-debug` | 110.06 MB | `utf_baselib_io` |
| `win-a64-vc143-debug` | 103.24 MB | `utf_baselib_io` |
| `win-x86-vc143-release` | 96.77 MB | `utf_baselib_io` |
| `win-x64-ccl16-debug` | 78.91 MB | `utf_baselib_io` |
| `win-a64-ccl16-debug` | 78.55 MB | `utf_baselib_io` |
| `win-x86-ccl16-debug` | 74.33 MB | `utf_baselib_io` |
| `win-x86-vc143-debug` | **72.26 MB** | `utf_baselib_io` |
| `win-a64-ccl16-release` | 63.11 MB | `utf_baselib_messaging3` |
| `win-x64-ccl16-release` | 62.16 MB | `utf_baselib_messaging3` |
| `win-x86-ccl16-release` | 35.69 MB | `utf_baselib_messaging3` |

**Three things this overturns.**

**The gated combo is the second-smallest in the matrix.** `utf_objsize.py` gates
`^win-x86-.*-debug$` at 72.26MB while `win-a64-vc143-release` sits at 125.32MB entirely unwatched.
That is not wrong — the ~2GB address-space wall is x86-only, and nothing above is a build failure —
but "PASS" has never said anything about eleven of twelve combos, and the largest objects in the tree
live in combos nobody measures.

**Debug is not the worst case, and which variant is worst depends on the toolchain.** For `vc143`,
release is *larger* than debug (96.77 vs 72.26 on x86). For `ccl16` it is dramatically *smaller*
(35.69 vs 74.33). A gate that sampled only debug variants would miss the true peak on every `vc143`
combo, and one that sampled only release would miss it on every `ccl16` one. **Gate both.**

**The 105MB non-x86 ceiling this plan originally guessed was too low** — four combos exceed it. It
was derived as 1.4x the x86 figure from a single a64 module; real ratios are ~1.4x for a64 vc143
debug and ~1.5x for x64, but the *peak* is what matters and that is 125.32MB. The placeholder below
is now 140MB, and it is only a placeholder: Windows non-x86 stays in `report` until stage 2 sets it
from data.

One module dominates nine of twelve combos. `utf_baselib_io` is the tightest object in the tree on
every `vc143` combo and every `ccl16` debug combo, which makes it the first candidate for splitting
if any ceiling needs headroom.

### Two files, two owners — this is what keeps friction near zero

The first draft of this plan had developers updating a baseline file whenever an object grew. That
does not work, and the reason is worth stating because it is not obvious:

**Baselines are per-combo, but a developer builds one or two combos.** If Alice adds a test and
accepts the new size on `win-x64-vc143-debug`, the recorded baselines for that module on the other
eleven combos are now stale-low. Bob, building `win-a64-ccl16-debug`, then gets a drift failure for a
change he did not make and cannot evaluate. Multiply by every contributor and the gate is off within
a month.

So the data is split by **who writes it and when**:

| | `src/utests/object-size-limits.json` | `src/utests/object-size-baselines.json` |
|---|---|---|
| Holds | ceiling and target per platform pattern | measured size per object per combo |
| Written by | a human, deliberately, rarely | **the matrix operator**, via an explicit target |
| Written when | a limit is argued and changed | after a full matrix run, at milestones |
| Read by | **every build, including local** | the drift check, during a matrix run |
| Enforces | **ceiling — fails any build** | drift — reported when the matrix runs |
| Churn | almost none | once per matrix run |
| Merge conflicts | possible but rare | rare — one writer at a time, by convention |

**The ceiling needs no baseline.** It is a static number, so the check that actually prevents the
112MB catastrophe runs on every build, everywhere, with zero data churn and nothing to update.

**Drift needs a baseline, and there is no CI in this repository to own one.** There are no workflows,
no Jenkinsfile and no pipeline — the `scripts/ci/` directory in the devenv dist is environment
bootstrap, not a build service. An earlier draft of this plan assumed a nightly CI matrix existed.
It does not, and the design must not depend on one appearing.

So the owner is **whoever runs the matrix**. That is a deliberate, occasional act — it took about
seven hours on a two-core box — performed at milestones rather than per commit. Drift is therefore
"what grew since the last milestone", which is exactly the granularity creep needs. If CI ever
arrives, it becomes the operator and nothing else about the design changes.

### The two files

Both sit in `src/utests/`, next to the code they govern.

Build directories are named `$(OS)-$(ARCH)-$(TOOLCHAIN)-$(VARIANT)` (`common.mk:136`,
`platform.mk:7`), and the `OS` component is `win` on devenv7 Windows, `ub12`…`ub24` on Ubuntu,
`rhel5`…`rhel10` on RHEL, and `d156`…`d25` on macOS by Darwin release — so `win-x86-vc143-debug`,
`ub24-x64-gcc-debug`, `d25-a64-clang-release`. Those prefixes are what the patterns match, which is
what makes a per-platform rollout a one-word edit.

**`object-size-limits.json`** — hand-edited, tiny, and the only file a build has to read:

```json
{
  "version": 1,
  "limits": {
    "win-x86-*":  { "target_mb": 40, "ceiling_mb": 75,  "gate": "enforce" },
    "win-*":      { "target_mb": 80, "ceiling_mb": 140, "gate": "report" },
    "*":          { "gate": "off" }
  }
}
```

**Most specific pattern wins**, so `win-x86-*` beats `win-*` beats `*`. Three values for `gate`:
`enforce` fails the build above the ceiling, `report` prints the line and never fails, `off` is
silent. A platform is adopted by changing one word.

**`object-size-baselines.json`** — written only by `make utests-sizes-record`, never hand-edited:

```json
{
  "version": 1,
  "drift": { "tolerance_pct": 2.0, "tolerance_mb": 1.0 },
  "recorded": {
    "win-x86-vc143-debug": {
      "at": { "commit": "aeed9e2", "date": "2026-09-14" },
      "objects": { "utf_baselib_io": { "UtfBaselibIoMain.obj": 75765350 } }
    }
  }
}
```

Each combo carries the commit and date it was recorded at, because a partial matrix run merges into
this file rather than replacing it, so entries legitimately come from different points in time. The
drift report needs to be able to say "x64 last recorded four commits ago, a64 forty" rather than
implying they are contemporaneous.

Merged, never overwritten, so recording two combos cannot wipe the other ten.

### How recording actually happens

**Not automatically, and not as a build parameter.** It is a separate target, run deliberately:

```
make utests-sizes-record          # merge whatever is in bld/ into the baselines file
```

**Automatic recording during ordinary builds would recreate the exact bug this design exists to
avoid.** A developer building one combo would silently overwrite that combo's entry while leaving the
other eleven untouched but now inconsistent with it — and worse, would do so without noticing. Making
it an explicit target means recording appears in `git log` as a decision, and cannot happen by
accident.

It is a separate target rather than a flag like `RECORD_SIZES=1` for the same reason: a flag gets
pasted into someone's shell alias and then fires on every build.

**The sequence at a milestone**, and it is the sequence the 2026-09-14 matrix already followed by
hand:

```
1  run the matrix                 build and test all twelve combos
2  make utests-sizes-record       merge the measured sizes, stamped with the commit
3  review the diff                this is the size changelog since the last milestone
4  commit it                      alongside the matrix result
```

Step 3 is the point of the whole exercise. The diff is a per-module statement of what grew since the
last milestone, in a form a reviewer can argue with.

**Between milestones, drift is simply not checked.** Only the ceiling is, and the ceiling is the check
that matters for preventing a hard failure. That is an honest limitation of having no CI, not a gap
to paper over: drift catches slow creep, creep is slow, and milestone granularity is enough for it.

**Why per-platform limits are not optional.** An a64 object runs ~1.4× its x86 counterpart on `vc143`
debug (103.24 vs 72.26MB), and the measured spread is 35.69MB to 125.32MB — a factor of 3.5 on
Windows alone. ELF and Mach-O differ again, and **no Linux or macOS object in this repo has ever been
measured.** A single global number would be far too loose for x86 and spuriously red everywhere else.

---

## 3. The three checks

| Check | Compares | On breach |
|---|---|---|
| **Ceiling** | object vs `ceiling_mb` | **fail** |
| **Target** | object vs `target_mb` | report, and require a reason in the split ledger |
| **Drift** | object vs its last recorded baseline | **reported by a matrix run**, never fails a local build |

**Drift is the anti-creep mechanism and the interesting one.** Ceiling alone does not prevent this
happening again: `utf_baselib_io` can go 48 → 55 → 62 → 71MB over a year of ordinary work and nobody
sees a single step as remarkable. Drift makes each step visible at the moment it happens.

An object fails drift when it exceeds `max(baseline × 1.02, baseline + 1MB)`. The tolerance absorbs
compiler-version jitter and trivial edits; a real test case is well above it.

**Shrinkage warns, it does not fail.** An object well under its baseline means the baseline is stale
and the gate has gone slack, so it should say so — but nobody should have their build broken for
making things smaller.

---

## 4. What a developer sees

This is the part that decides whether the gate is useful or resented.

**During the build**, after each module links:

```
Linking utf_baselib_messaging...
  utf_baselib_messaging      68.4 / 75.0 MB  [########..]  91%  over 40 MB target
```

**At the end of `make utests`**, a summary sorted by headroom, tightest first:

```
Test module object sizes - win-x86-vc143-debug          target 40 MB, ceiling 75 MB

  utf_baselib_io             72.3 MB   96% of ceiling   <-- tightest
  utf_baselib_messaging      68.4 MB   91%
  utf_baselib_tasks          67.7 MB   90%
  ...
  utf_baselib_setprio        21.4 MB   29%

  28 modules, 34 objects, 0 over ceiling, 12 over target
```

**On failure**, a message that says what to do rather than what went wrong:

```
FAIL  utf_baselib_io / UtfBaselibIoMain.obj
      78.2 MB exceeds the 75.0 MB ceiling for win-x86-vc143-debug

      This translation unit is too large for the 32-bit x86 toolchain, which has
      roughly 2GB of address space. Move some test headers into a numbered sibling:

          src/utests/utf_baselib_io3/UtfBaselibIo3Main.cpp

      See src/utests/AGENTS.md for the checklist. Raising the ceiling is not the fix -
      it is what this gate exists to prevent.
```

**On drift**, the message is different, because the action is different:

```
FAIL  utf_baselib_data / UtfBaselibDataMain.obj
      52.1 MB, baseline 49.8 MB (+4.6%, tolerance 2.0%)

      If this growth is intended, record it:

          make utests-sizes-accept

      That updates src/utests/object-sizes.json. Commit it with your change so the
      size increase is visible in review.
```

### Targets

| Target | Does |
|---|---|
| `make utests-sizes` | Print the table for whatever is in `bld/`. No build. |
| `make utests-sizes-record` | Merge measured sizes into the baselines file. Milestones only. |
| `BL_SKIP_SIZE_GATE=1` | Local escape hatch for experiments. Never set it in a matrix run. |

There is deliberately **no developer-facing accept target.** Nothing a developer does updates a
recorded size in the course of ordinary work: the ceiling never consults recorded sizes, and the
baselines file is written only by the explicit `utests-sizes-record` target at a milestone.

### What this costs a developer, concretely

**Adding a test to a module with room** — the overwhelmingly common case. One extra line per module as
it links, plus the summary. Nothing to update, nothing to commit, no JSON in the diff.

```
Linking utf_baselib_utils...
  utf_baselib_utils           3.2 / 75.0 MB  [..........]   4%
```

**Adding a test to a module near the ceiling.** The same line, reading as a warning, visible long
before it becomes a failure:

```
  utf_baselib_io             73.4 / 75.0 MB  [#########.]  98%  over 40 MB target
```

**Pushing a module over the ceiling.** The build fails and says to create a numbered sibling. Still no
data file to touch. This is the only case that stops anyone, and it is the case the gate exists for.

**A legitimate large increase that genuinely has to land.** Someone raises the ceiling for that
pattern in `object-size-limits.json` — a one-line change, its own commit, argued in review. That is
the intended friction: raising a ceiling should be a decision, not a reflex. An accept command that
silently records whatever it finds is how this class of gate dies, which is why there isn't one.

**What a milestone matrix catches that a developer cannot.** Running all twelve combos records every
one and reports drift since the last milestone. A change that grew something unexpectedly on a combo
its author never builds surfaces there, in the matrix operator's commit, not theirs.

---

## 5. Build integration

The hook is `projects/make/common.mk`, in the `TEMPLATE` define that generates per-module rules. It
already echoes `Linking $(1)...`; the check goes immediately after the link recipe, so it fires when
a module is actually relinked and is silent otherwise.

```make
  $$($(1)_ARTIFACT): $$($(1)_OBJECTS)
	@echo "Linking $(1)..."
	@mkdir -p $$(@D)
	$$(LINK.$$(TOOLCHAIN))
	@$$(SIZE_GATE) --module $(1) --bld $$(BLDDIR) --platform $$(BL_PLAT_TAG)
```

**Cost.** One Python start per relinked module. On a clean full build that is ~30 invocations; on the
incremental builds developers actually run, it is one or two. Acceptance criterion: **under 3 seconds
added to a clean `utests` build**. If it misses that, move the per-module line to `awk` over `stat`
output and keep Python only for the summary and the accept path — at the cost of the limit logic
living in two places, which is why it is not the first choice.

**Failing fast matters more than failing tidily.** Checking at link time names the offending module
while its output is still on screen, rather than at the end of a long build.

---

## 6. Rollout, so nobody's build breaks on a flag day

**Rollout is per platform, and the unit of adoption is one word in `object-size-limits.json`.** A
platform moves `off` → `report` → `enforce` on its own schedule, and a platform that is not ready
simply is not listed. Nobody on an un-adopted platform sees anything change.

| Stage | Limits file says | What it proves before the next stage |
|---|---|---|
| 0 | `"win-x86-*": enforce`, everything else `off` | The mechanism works where the data is real and the constraint is real. x86 is the only place a ceiling breach is a hard build failure rather than a policy. |
| 1 | add `"win-*": report` | Collects a64 and x64 Windows data through ordinary builds. Peaks are known (up to 125.32MB) but the limits are not yet argued. |
| 2 | `"win-*": enforce` | A month with no false failures on Windows, ceilings set from stage 1 data rather than extrapolation. |
| 3 | add `"ub24-*": report`, then `enforce` | **No Linux object has ever been measured here.** ELF differs from COFF; the numbers must be collected before any limit is credible. |
| 4 | add `"d25-*"` (macOS), same two steps | Mach-O differs again, and macOS is the least exercised platform in this repo. |
| 5 | add `rhel*`, older `ub*`/`d*` as they matter | Only where the matrix is actually run. |

**Start with `win-x86` specifically, not Windows generally.** It is the only configuration with a
hard physical limit — roughly 2GB of address space in the 32-bit toolchain — so the ceiling there is a
fact rather than a policy, and the one number already calibrated against a real failure. Everywhere
else the ceiling is a judgement about compile time and build memory, and judgements need data first.

**Never skip `report` on a new platform.** A gate whose reference numbers were guessed will fire
spuriously, and a gate that fires spuriously gets switched off — permanently, along with the
platform that was working. The whole point of `report` is that the first numbers come from the
platform itself.

**The rollout order should follow wherever the matrix is actually run.** There is no CI here, so a
platform reaches `report` only when someone builds and records it, and `enforce` only when that has
happened enough times to argue a limit. Linux and macOS are therefore blocked on somebody running
the suite there at all, not on anything in this design.

---

## 7. Risks

- ~~**Baseline churn in review.**~~ **Resolved by the two-file split.** Developers never touch either
  file in ordinary work: the ceiling needs no recorded size, and the baselines file is touched only
  by a deliberate milestone recording.
- **Compiler upgrades move everything at once.** A new MSVC will shift every object a few percent and
  trip drift on every module simultaneously. Because drift is only evaluated during a matrix run this
  produces one noisy report rather than breaking everyone's build, and the recovery is a single
  re-record commit. A toolchain bump large enough to threaten a *ceiling* is a different matter and
  should be caught in the bump's own validation.
- ~~**Merge conflicts on the JSON.**~~ **Structurally impossible now.** The baseline file has a
  single writer at a time - the matrix operator. The limits file is hand-edited rarely and is a few
  lines long.
- **Gating what is cheap to measure rather than what matters.** Object size is a proxy. The thing
  that actually hurt was peak compiler memory, which this cannot see. The `x86` `ccl16` release
  caveat must stay documented next to the gate so it is not mistaken for full cover.
- **False confidence on a partial build.** A developer who builds only x64 gets no x86 verdict. The
  summary should say which combos it did *not* check.

---

## 8. Open questions

1. ~~**Hard-fail non-x86 combos, or report only?**~~ **Settled by the staged rollout.** Report until
   a platform's own numbers justify a ceiling, then enforce. The `gate` field makes it per-platform.
2. **Tolerance values.** 2% / 1MB is a starting guess, not a measurement. Phase 1 data should set it.
3. **Should `.pdb` and `.exe` be gated?** Recommendation: report only — disk, not address space.
4. **Should this repository have CI at all?** It has none today, which is why drift is milestone
   scoped rather than nightly. If CI arrives it becomes the matrix operator and nothing else here
   changes - but a cheap per-PR job running one combo for the ceiling only would be worth more than
   the full matrix, and would make the ceiling enforceable on contributions rather than only on
   whoever builds locally.
5. ~~**Does the a64 ceiling of 105MB have any basis?**~~ **Answered 2026-09-14: it did not.** The
   matrix measured every combo; four exceed 105MB and the peak is 125.32MB. The placeholder above is
   now 140MB ceiling / 80MB target for non-x86, which clears the measured peak with margin - but it
   is still a placeholder chosen from one run, not a limit anyone has argued for. Phase 1 should set
   it from a few runs rather than one.
