# Test Module Split: Ledger

**This file is the resume point.** A session with no memory of any earlier session reads
[test-module-split-plan.md](test-module-split-plan.md), then this file, then continues.

Companion to the plan; see its §9 for the rules this ledger operates under. In short:

1. Every handback is **one commit that updates the code and its ledger row together**, so `git log`
   is the recovery record and this file can never drift from the tree.
2. **Every commit leaves the tree green.** Step A and Step B are each atomic — a module is never
   left half-split. Recovery from a dead session is `git checkout -- src/utests/<module>*` and redo
   that step, never a partial merge.
3. **Verify before trusting.** Run `scripts/utests/check_split.sh` and reconcile against this file
   before taking the next row. A row marked `in-progress` by a session that is no longer running is
   suspect: re-verify, then reset it to `todo` or promote it to `done`.

---

## Cold-start protocol

```
1  read  notes/reviews/major/update_2026/test-module-split-plan.md
2  read  notes/reviews/major/update_2026/test-module-split-ledger.md      (this file)
3  run   scripts/utests/check_split.sh                 does the tree match the ledger?
4  run   scripts/utests/utf_objsize.py                 which modules are already under ceiling?
5  take  the first `todo` row in plan order; commit code + ledger together
```

**Interpreter note.** The tooling is stdlib-only and `check_split.sh` resolves an interpreter on its
own. On this Windows box the repo `.venv` is **broken** — it points at a scratch directory from an
old session that no longer exists — so the script falls back to the devenv7 dist interpreter at
`~/swblocks/dist-devenv7-*/python/*/default/python.exe`. Set `UTF_PYTHON` to override.

---

## Status

Legend: `todo` · `in-progress` · `done` · `blocked` · `n/a`

### Step 0 — baseline, tooling and measurement

| # | Item | Status | Commit | Notes |
|---|---|---|---|---|
| 0.0 | Plan + ledger committed under `update_2026/` | **done** | _pending_ | this file and the plan beside it |
| 0.1 | `utf_inventory.py` + invariants C1–C7 | **done** | _pending_ | 772 cases, 115 helper blocks, 19 modules |
| 0.1a | `selftest_inventory.py` — prove each invariant fires | **done** | _pending_ | 11 corruptions, all caught |
| 0.2 | `utf_objsize.py` + ceiling gate | **done** | _pending_ | gates `win-x86-*-debug` only |
| 0.3 | `utf_runlog.py` + runtime comparator | **done** | _pending_ | `--list_content`, Entering/Leaving, SKIPPED, assertion counts |
| 0.4 | `check_split.sh` — the single gate | **done** | _pending_ | tiers skip cleanly when inputs are absent |
| 0.5 | Baseline: source inventory | **done** | _pending_ | `baseline/inventory.json` |
| 0.6 | Baseline: object sizes | **done** | _pending_ | `baseline/objsize.json` — captured **before** any tree deletion |
| 0.7 | Confirm `--report_level=detailed` emits per-case assertion counts | **done** | — | verified on `utf_baselib_utils`: 51 / 22 / 9 |
| 0.8 | Baseline: runtime pass 1 | **todo** | — | `baseline/runlog-pass1.json` |
| 0.9 | Baseline: runtime pass 2 | **todo** | — | second pass, for the non-determinism list |
| 0.10 | Derive `baseline/nondeterministic.json` | **todo** | — | cases whose assertion count disagrees between passes |
| 0.11 | Promote pass 1 to `baseline/runlog.json` | **todo** | — | the file tier 3 compares against |
| 0.12 | Per-header object-weight probe | **todo** | — | **the expensive one** — see below |
| 0.13 | Probe `utf_baselib_apps` with `UtfBaseLibCommon.h` narrowed | **todo** | — | prices the adjunct lever, plan §1.1 |

### Step 1 — pilot

| Module | Step | Status | Commit | Obj before | Obj after | Gate | Notes |
|---|---|---|---|---|---|---|---|
| `utf_baselib_security` | A | **n/a** | — | — | — | — | headers already fine-grained; pure Step B |
| `utf_baselib_security` | B | **todo** | — | 65.0 | — | — | 3-way split, zero data duplication |

### Step 2 — fan out

Order is the plan's §10 order: easiest first to bank the recipe, hardest last.

| # | Module | Step | Status | Commit | Obj before | Obj after | Gate | Notes |
|---|---|---|---|---|---|---|---|---|
| 1 | `utf_baselib_apps` | B | **todo** | — | 77.2 | — | — | isolate the gateway header |
| 2 | `utf_baselib_data` | B | **todo** | — | 49.8 | — | — | `serialized_object.json` follows `TestDataModelDefault.h` |
| 3 | `utf_baselib_http` | B | **todo** | — | 55.8 | — | — | split on the lock; 3 TLS headers are lock-free |
| 4 | `utf_baselib` | B | **todo** | — | 55.3 | — | — | 2 sticky clusters; carry `using namespace bl;` |
| 5 | `utf_baselib_rest` | A | **todo** | — | 78.7 | — | — | easiest file in the tree |
| 5 | `utf_baselib_rest` | B | **todo** | — | — | — | — | |
| 6 | `utf_baselib_blobtransfer` | A | **todo** | — | 56.3 | — | — | cut at line 292 |
| 6 | `utf_baselib_blobtransfer` | B | **todo** | — | — | — | — | |
| 7 | `utf_baselib_messaging` | A | **todo** | — | 112.7 | — | — | cut 4576–6914, zero fixups; pin the cold-cache case |
| 7 | `utf_baselib_messaging` | B | **todo** | — | — | — | — | |
| 8 | `utf_baselib_io` | A | **todo** | — | 77.4 | — | — | hardest cuts; 3493–3844 first |
| 8 | `utf_baselib_io` | B | **todo** | — | — | — | — | |
| 9 | `utf_baselib_tasks` | hoist | **todo** | — | 67.7 | — | — | **prerequisite** — `TestTasks.h` fixtures to the shared tree |
| 9 | `utf_baselib_tasks` | B | **todo** | — | — | — | — | only after the hoist |

### Step 3 — close the deferral

| # | Item | Status | Commit | Notes |
|---|---|---|---|---|
| 3.1 | Every x86 debug object under 40 MB | **todo** | — | `utf_objsize.py --ceiling 40` exits 0 |
| 3.2 | Restore `-Zi` for x86 ccl16 release | **todo** | — | deferral item 4 |
| 3.3 | Retest the 32-bit clang-cl host | **todo** | — | deferral item 5 |
| 3.4 | Update deferral record + `msvc-default.mk` + devenv7 AGENTS.md | **todo** | — | |
| 3.5 | Full 12-combo Windows matrix | **todo** | — | |

---

## Measured baseline

Captured 2026-09-12 from the build trees then on disk, **before** any of them is deleted. These are
the numbers every later comparison is made against.

**Source inventory** — 772 test cases, 115 helper blocks, 19 module directories, all case names
globally unique. Note 772 is the count in *source*; a single platform builds fewer (the x86 Windows
run executes 741) because some cases sit inside platform guards. Comparisons are always
before-vs-after on the same platform, so this is not a discrepancy.

**Object sizes, x86 debug** — 20 objects over the 40 MB ceiling across the two toolchains:

| Module | vc143 | ccl16 |
|---|---:|---:|
| `utf_baselib_messaging` | 112.7 | 110.3 |
| `utf_baselib_rest` | 78.7 | 78.1 |
| `utf_baselib_io` | 77.4 | 79.4 |
| `utf_baselib_apps` | 77.2 | 77.0 |
| `utf_baselib_tasks` | 67.7 | 67.9 |
| `utf_baselib_security` | 65.0 | 63.9 |
| `utf_baselib_blobtransfer` | 56.3 | 57.4 |
| `utf_baselib_http` | 55.8 | 56.5 |
| `utf_baselib` | 55.3 | 55.9 |
| `utf_baselib_data` | 49.8 | 47.0 |

The ~21 MB per-TU floor is visible in `utf_baselib_setprio` at 21.4 MB for 223 lines of source.

---

## Notes for whoever picks this up

- **C3 deliberately ignores include guards.** A first cut counted them, and 77 of the 113 "guarded"
  cases turned out to be sitting under an `#ifndef __UTEST_*_H_` include guard rather than any real
  conditional. That would have fired C3 every time a case moved into a new header written without
  one — a false alarm that erodes trust in the gate faster than any missed defect. `is_include_guard`
  in `utf_inventory.py` recognises the `#ifndef FOO` / `#define FOO` shape and drops that frame.
  **Do not "fix" this back.** After the filter, C3 covers 37 cases under genuine conditionals:
  28 platform guards, 6 library-version guards, 1 devenv-version guard.
- **Verified preconditions.** Zero cases sit inside any namespace, confirming that no anonymous
  namespace wraps a test case; 156 cases carry a doc comment that C2 now hashes; and the column-0
  namespace detection reproduces the hand analysis exactly (`TestIO.h` 4 blocks, `TestMessagingDefault.h`
  5 including both `utest` blocks, `TestRestDefault.h` 1, `TestBlobTransferFilesystem.h` 7).
- **`utf_baselib_plugin` is a shared library, not a test executable.** It has no `Main.cpp`, reports
  0 cases, and `utf_runlog.py` skips it because no matching `.exe` exists. That is correct, not a
  gap.
- **The runtime baseline must be captured twice.** A single pass cannot distinguish a real
  regression from a case whose assertion count naturally varies. Until `nondeterministic.json`
  exists, tier 3 will report false differences on retry-loop and perf cases.
- **Item 0.12 is the expensive one.** It builds one throwaway module per test header — roughly 50
  compiles at 1–4 minutes each. It is embarrassingly parallel across lanes and its output is what
  decides each module's actual split, so it gates Step 2 but not Step 1.
- **Do not delete `bld/win-x86-vc143-debug` or `bld/win-x86-ccl16-debug` until 0.8–0.11 are done.**
  They are the only built trees, and the runtime baseline needs their binaries.
