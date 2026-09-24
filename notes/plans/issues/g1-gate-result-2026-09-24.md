# The G1 whole-suite gate over `9cd211c` — 2026-09-24

**Verdict: CLEAN by the acceptance criterion.** The gate exited 1, which is the only thing it can
ever do; §3 says why, and why the exit code carries no information at all.

Run after the day's tier-1 and tier-3 work merged. This is the first recorded G1 result — previous
runs left evidence directories but nothing a reader could find.

---

## 1. What ran

| | |
|---|---|
| tip gated | `9cd211c` |
| parent | `1bcde00`, fixed |
| combination | `gcc1520` `debug`, one combo, the maintainer's call of 2026-09-17 |
| evidence | `http2-l0-state/evidence/g1/`, report at `report.txt`, 650 lines |
| result | 45 modules, **1062 cases ran, 1073 registered**, 624 differences |

Phases, from an **empty checkpoint** — the previous one was retired the same day because all four of
its artifacts predated the platform stamp *and* its parent and change captures were six days apart,
so it measured the tool rather than the change:

```
phase 1  parent build, fully cold      15:38:45 -> 16:04:31   26 min
phase 2  baseline capture 1            16:04:31 -> 16:23:10   19 min
phase 3  baseline capture 2            16:23:10 -> 16:41:52   19 min
phase 4  derive the unstable list      16:41:52 -> 16:41:53    1 s
phase 5  change build, warm            16:41:53 -> 16:43:12   79 s
phase 6  change capture                16:43:12 -> 17:02:28   19 min
phase 7  compare                       17:02:28               instant
```

**A full capture is 19 minutes and does not vary** — three of them here, 19/19/19, against 16/17/18/19
across the September runs. That is the number to plan with.

## 2. The verdict: six zeros

These are the checks that **never consult the unstable list**, so nothing can be excused into silence:

| check | count |
|---|---|
| `CASE DID NOT PASS` | **0** |
| `MODULE DID NOT REPORT CLEAN` | **0** |
| `MODULE REPORTED FAILURES` | **0** |
| `MODULE EXIT CHANGED` | **0** |
| `NO LONGER RUNS` | **0** |
| `REGISTRATION LOST` | **0** |
| `OUTCOME CHANGED` | **0** |

## 3. Why 624 differences, and why exit 1 means nothing

```
311  NEWLY RUNS
311  REGISTRATION ADDED
  2  ASSERTION COUNT CHANGED
  0  SKIPPED SET CHANGED,  0  OUTCOME CHANGED
---
624
```

**622 of the 624 are the same 311 cases counted twice.** The parent carries **28 modules**; the tip
carries 45. 283 of those cases are in **17 modules `1bcde00` never had** — the whole h2 and
httpclient families — and the rest were added to modules that did exist.

So the gate **cannot exit 0 unless the tree stops growing**, and `--compare` returns 1 on any
non-zero difference count. **The exit code is not a verdict and must never be read as one.** The
method is §4.

**This is the design's real weakness, and it should be said plainly:** a gate whose pass condition is
*"read 650 lines and diff them against last time"* depends on somebody actually doing that. A gate
against a *moving* parent, or one that classified its own structurally-expected differences, would
not. That is a change to how the gate works and was deliberately not made at the end of a batch.

## 4. The method: diff against the previous report

Previous report: `evidence/g1-retired-prestamp-2026-09-18/report.txt`, 626 lines, 07:09 the same day.
**That directory was retired for resumption but kept precisely for this.** The delta was 3 lines gone
and 27 new, and every one is accounted for:

| | |
|---|---|
| **+1 case** | `Http1Driver_PeerResetsAfterACompleteKeepAliveResponseTests`, 6a's case, added since. Explains +2 differences, +1 ran, +1 registered |
| **+22 lines** | the **coverage statement** (`40123ef`), which now names the 17 modules the tier-3 baseline cannot speak about. A feature landing, not a finding |
| **one flake swapped** | `BaseLib_Base64UrlTests` stopped reporting; `BlobTransfer_FilesPackagerInMemoryCancelUploadTests` started |

**The flake swap was checked, not assumed.** Phase 4 derived **10 names** from this run's two baseline
passes; `Base64UrlTests` **is** in that list and `BlobTransfer…` **is not**, which is exactly why one
went quiet and the other spoke. It is the sampling record's own point — two passes classify a case
that agrees with itself as deterministic, and misclassify a rarely-varying case about a third of the
time — and it is why `nondeterministic.json` now has an `observed` key that a refresh cannot drop.

`TlsHandshake_SniOmittedForAddressLiterals (14 → 21)` appears in **both** reports, so it is
pre-existing against a parent now far behind, not a regression.

## 5. What this result does NOT cover

- **gcc1520 debug only.** One combination by design. clang, release and every Windows cell are not
  in it.
- **Tier 3's committed baseline was never consulted** — the gate derives its own from the parent.
  So this says nothing about the `win-x86-vc143-debug` baseline or the platform refusal.
- **Nothing on Windows**, including whether `check_split.sh` renders its three new states there.
- **The two known hangs** — `h2client` and `h2client4`, seen once each in 57 captures — did not
  recur here. One clean run is not evidence they are gone.
