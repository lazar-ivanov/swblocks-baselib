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

**The 105MB non-x86 ceiling guessed below is too low** — four combos already exceed it, and it was
derived as 1.4x the x86 figure from a single a64 module. Real ratios are ~1.7x for a64 vc143 debug
and ~1.5x for x64. Open question 5 is answered: it had no basis, and these numbers replace it.

One module dominates nine of twelve combos. `utf_baselib_io` is the tightest object in the tree on
every `vc143` combo and every `ccl16` debug combo, which makes it the first candidate for splitting
if any ceiling needs headroom.

### Where the numbers live

**`src/utests/object-sizes.json`** — next to the code it governs, so it appears in the same directory
as the change that moved it and lands in the same review.

```json
{
  "version": 1,
  "limits": {
    "win-x86-*-debug":   { "target_mb": 40, "ceiling_mb": 75 },
    "win-x86-*-release": { "target_mb": 40, "ceiling_mb": 75 },
    "*":                 { "target_mb": 80, "ceiling_mb": 140, "gate": "report" }
  },
  "drift": { "tolerance_pct": 2.0, "tolerance_mb": 1.0 },
  "baselines": {
    "win-x86-vc143-debug": {
      "utf_baselib_messaging": { "UtfBaselibMessagingMain.obj": 71743334 }
    }
  }
}
```

Three things this shape buys:

- **Per-combo limits**, because an a64 object runs ~1.4× its x86 counterpart on `vc143` debug
  (103.24 vs 72.26MB) and the spread across the matrix is wider still — 35.69MB to 125.32MB, a factor
  of 3.5. A single global number would be either far too loose for x86 or spuriously red everywhere
  else.
- **Glob patterns**, so adding a toolchain does not mean adding twelve rows.
- **`"gate": "report"`**, so a combo can be tracked without failing anyone's build — which is how
  every combo starts (§6).

The baseline file is **merged, never overwritten**. A developer who can only build two combos must
not wipe the other ten.

---

## 3. The three checks

| Check | Compares | On breach |
|---|---|---|
| **Ceiling** | object vs `ceiling_mb` | **fail** |
| **Target** | object vs `target_mb` | report, and require a reason in the split ledger |
| **Drift** | object vs its recorded baseline | **fail** — the baseline is out of date |

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
| `make utests-sizes-accept` | Re-record baselines for combos present in `bld/`, merging. |
| `BL_SKIP_SIZE_GATE=1` | Local escape hatch. CI ignores it; document that plainly. |

**`accept` must refuse to record anything over the ceiling.** Otherwise the first person to hit the
wall will accept their way through it, which is precisely the failure mode being designed against.
It should also print each change it is about to make, so accepting is a decision rather than a
reflex.

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

| Phase | Gate behaviour | Exit criterion |
|---|---|---|
| 1 | Report only, every combo. `accept` populates baselines. | Full 12-combo matrix run; baselines committed |
| 2 | **Ceiling fails** on `win-x86-*`; everything else reports | One clean matrix run with no ceiling breach |
| 3 | **Drift fails** on `win-x86-*-debug` | A month of ordinary work without false alarms |
| 4 | Extend drift to remaining combos as their baselines prove stable | — |

Phase 1 is not optional. There is no baseline data for eight of the twelve combos today, and turning
on a gate whose reference data was guessed is how gates get switched off.

---

## 7. Risks

- **Baseline churn in review.** Every test addition touches the JSON. Mitigated by the tolerance
  band, and it is partly the point — the diff *is* the visibility.
- **Compiler upgrades move everything at once.** A new MSVC will shift every object a few percent and
  fail every module simultaneously. Needs a documented "re-baseline after a toolchain bump" step,
  done as its own commit, never mixed with test changes.
- **Merge conflicts on the JSON.** Two developers accepting different modules conflict textually.
  Mitigated by one object per line and stable key ordering; worst case, re-run `accept`.
- **Gating what is cheap to measure rather than what matters.** Object size is a proxy. The thing
  that actually hurt was peak compiler memory, which this cannot see. The `x86` `ccl16` release
  caveat must stay documented next to the gate so it is not mistaken for full cover.
- **False confidence on a partial build.** A developer who builds only x64 gets no x86 verdict. The
  summary should say which combos it did *not* check.

---

## 8. Open questions

1. **Hard-fail non-x86 combos, or report only?** There is no address-space wall there, so the
   argument is compile time and build memory. Recommendation: report only, at least through phase 3.
2. **Tolerance values.** 2% / 1MB is a starting guess, not a measurement. Phase 1 data should set it.
3. **Should `.pdb` and `.exe` be gated?** Recommendation: report only — disk, not address space.
4. **Where does CI enforce?** A gate developers can skip locally needs a CI job that cannot, and that
   job needs the full matrix, which is slow. Worth deciding whether it runs per-PR or nightly.
5. ~~**Does the a64 ceiling of 105MB have any basis?**~~ **Answered 2026-09-14: it did not.** The
   matrix measured every combo; four exceed 105MB and the peak is 125.32MB. The placeholder above is
   now 140MB ceiling / 80MB target for non-x86, which clears the measured peak with margin - but it
   is still a placeholder chosen from one run, not a limit anyone has argued for. Phase 1 should set
   it from a few runs rather than one.
