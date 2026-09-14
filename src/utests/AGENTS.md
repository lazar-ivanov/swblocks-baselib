# Unit Test Modules

Rules and reference for `src/utests/`. The root [AGENTS.md](../../AGENTS.md) links here; read this
before adding a test case or a test module.

---

## The rule that matters most

**Every test module is a single translation unit, and it must not grow without a bound.**

One `<Module>Main.cpp` defines `UTF_TEST_MODULE`, includes `<utests/baselib/UtfMain.h>`, then
`#include`s every `Test*.h` in its directory. Adding a test case adds to that one TU. Nothing about
the build system caps it.

This has bitten the project once already, and expensively. `utf_baselib_messaging` reached a
**112.7MB** x86 debug object, at which point the 32-bit `clang-cl` host ran out of address space and
died with `0xC000001D` — no diagnostic, just a crash. Two x86 build combinations were unbuildable
until the module was split four ways. See
[the deferral record](../../notes/plans/issues/x86-clang-cl-host-and-test-module-size-deferral.md).

### When you add a test case

1. **Check the module has room.** `scripts/utests/utf_objsize.py` prints every object with its
   headroom. If the module you were going to use is at or near the 40MB target, do not add to it.
2. **Prefer a module comfortably under target.** A case can be added to any module whose fixtures and
   helpers suit it; it does not have to go in the largest one.
3. **Otherwise create a numbered sibling** — see below. This is cheap and is the intended answer, not
   a last resort.

### Size policy

| Tier | Value | Meaning |
|---|---:|---|
| Target | **40 MB** | Aim here. An object above it is reported and needs a recorded reason. |
| Ceiling | **75 MB** | Hard fail on `win-x86-*-debug`. |
| TU floor | **~21 MB** | What an empty test module costs. Subtract it to see your actual content. |

Measured on `x86` `debug`, where the address-space limit is real. The floor is why splitting has a
cost: **every new module pays ~21MB again**, so split when a module is genuinely large, not
reflexively.

### The gate enforces this for you

Every test module prints its headroom as it links, and the build **fails** if an object exceeds the
ceiling on an enforcing platform:

```
Linking utf_baselib_io...
  utf_baselib_io                  72.3 / 75 MB  [##########]  96%  over 40 MB target
```

- `make utests-sizes` prints the whole table, tightest module first, without building.
- Limits live in [object-size-limits.json](object-size-limits.json), per platform pattern.
- **Today only `win-x86-*-debug` enforces.** `win-x86-*-release` reports, everything else is silent —
  see [the rollout plan](../../notes/plans/test-module-size-gate-plan.md). So a module can be over
  budget on a platform you are not building, and you will not hear about it until someone builds it.
- `BL_SKIP_SIZE_GATE=1` silences it locally while experimenting.

**A ceiling breach deletes the linked binary.** The build is configured `.DELETE_ON_ERROR`, so a
failing gate takes the `.exe` with it and you cannot run that module's tests until you are under the
ceiling again — or until you set `BL_SKIP_SIZE_GATE=1` for the run. That is a side effect of where
the check is hooked, not a deliberate punishment.

**Raising a ceiling is not the fix.** Split the module. Changing
[object-size-limits.json](object-size-limits.json) is a deliberate commit of its own, argued in
review, and for `win-x86` it means arguing against a measured toolchain crash.

---

## Creating a new test module

**The build system needs no change.** `projects/make/common.mk` discovers modules by directory
wildcard (`$(wildcard $(SRCDIR)/utests/utf*)`) and globs `*.cpp` inside each. Creating the directory
is sufficient for `make <module>`, `make test_<module>`, `utests`, `testutf`, `install` and `help`.

**Use the numbering scheme.** A module that outgrows itself splits into numbered siblings:
`utf_baselib_messaging` → `utf_baselib_messaging2`, `utf_baselib_messaging3`, and so on. The base
name keeps its original number-free form.

Checklist:

- [ ] `src/utests/utf_baselib_<name><N>/Utf…<N>Main.cpp` — copy an existing one; change
      `#define UTF_TEST_MODULE` and the `#include`s.
- [ ] **`git mv` the headers** that move, so history follows them.
- [ ] **`data/` cannot be shared between modules.** `TestUtils::resolveDataFilePath` resolves
      `<exe-dir>/<exe-stem>-data/`, so a module referencing a data file needs **its own copy**. Group
      data-consuming headers to avoid duplication where you can.
- [ ] **Move the `notes.txt` `--run_test=` recipes** with their cases. A recipe must name a case in
      its own module.
- [ ] Run `scripts/utests/check_split.sh`.

### Things that are not obvious

- **Never `#include` a test header across module directories.** Each module owns its headers. No
  invariant catches a cross-include; it silently duplicates cases into two binaries.
- **Never copy a helper into two headers of the same module.** That is an ODR violation; invariant C6
  catches it.
- **Anonymous namespaces have internal linkage.** A helper in one cannot be shared across a TU
  boundary without being moved into a named namespace first.
- **`using namespace bl;` at module scope is load-bearing** in some modules. Omitting it is a compile
  error, not a silent failure.
- **Watch for cold-start contracts.** `MessagingUtils_TokenTypeConcurrencyTests` deliberately
  exercises a process-global cache while it is still cold; any earlier case in the same process that
  warms it neuters the test **and it still passes**. Grep a header for such comments before splitting
  it.

---

## Verifying a change to the test tree

`scripts/utests/check_split.sh` runs the tiers below. Run it before handing back any change that
moves, adds or removes test cases.

| Tier | Tool | Checks |
|---|---|---|
| 1 | `utf_inventory.py --compare` | C1–C8: no case lost, added or edited; guard and namespace stacks unchanged; no duplicate names; helper members neither lost nor duplicated; data files present; `notes.txt` recipes resolve |
| 2 | `utf_objsize.py --ceiling 75` | No object over the ceiling |
| 3 | `utf_runlog.py --compare` | Registered set, executed set, pass/fail, skips, and **per-case assertion counts** |

`selftest_inventory.py` proves tier 1 actually fails when it should; run it if you change the
extractor.

**Tier 3 is the one that catches a case which still registers and still passes while silently doing
less work.** Do not skip it for a change that moves cases between modules.

---

## Reducing a module that is already too large

In order of preference.

1. **Move whole headers to a numbered sibling.** Cheapest and safest; only files move.
2. **Split a header, then move it** — as two separate commits. Step A cuts a block verbatim into a
   sibling header included from the *same* `Main.cpp`: the preprocessed TU is unchanged, so the
   object size must not move (this is a strong, mechanical gate — a real split measured +0.003%).
   Step B then moves the whole file, which is case 1.
3. **Move heavy helper bodies out of line.** For a `T< E = void >` helper in
   `src/utests/include/utests/baselib/`, move big member bodies into a sibling `…Impl.cpp` with an
   explicit instantiation, and give each consuming module a thin forwarding `.cpp` that
   `#include`s it. Keep the fake template — that idiom is what keeps baselib header-only.

**Know when (3) pays.** It *relocates* weight, it does not reduce it. It pays only when several
modules would each otherwise instantiate the same thing: it worked for `TestMessagingUtils` across
four modules, and would do nothing for a helper only one module uses.

**Splitting cannot fix everything.** A single case cannot straddle two executables, so one heavy
case sets a hard floor. `utf_baselib_messaging3` is 68.35MB of *one* inline helper,
`messageProcessingRoundTrip`. Reducing that needs
[instantiation weight work inside baselib](../../notes/plans/issues/test-instantiation-weight-deferral.md),
not more splitting.

**Do not predict a grouping from isolated per-header measurements** — they were off by 40× once.
Build the grouping and measure it, or use leave-one-out. What decides splittability is how much
every case instantiates *in common*, measured as `before − (a + b)`.

---

## The x86 release caveat

**Object size does not govern `x86` + `ccl16` + `release`.** That path is limited by peak memory in
the *optimizer*, and it is the one combination that still cannot afford full `-Zi` — see
`BL_MINIMAL_DEBUG_INFO` in `projects/make/toolchain/msvc-default.mk`. Its release object is
*smaller* than its debug object, so passing the 75MB ceiling tells you nothing about it. The only
check is whether that combination builds.

x86 targets are built by the x86-hosted tools on every host, so those ~2GB limits are real on every
machine, not just 32-bit ones.
