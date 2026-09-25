# The http and h2 client modules were measured for tier 3 and refused

**Measured:** 2026-09-24, branch `tier3-client-baseline` off `lazari2` at `6b4fa34`, worktree
`swblocks-baselib-lane1`, `ub24-a64-clang2010-debug`. **Status:** CLOSED — tier 3 now says it does
not cover them instead of appearing to.

The gap was real. `notes/reviews/major/update_2026/baseline/runlog.json` covers 17 of the tree's 45
test binaries and none of the http or h2 client ones, so the tier that exists to catch *a case which
still registers and still passes while silently doing less work* had nothing to say about the
modules this batch spent a week changing. H21 hit it: it moved per-case assertion counts in
`utf_baselib_httpclient4` and found them unjudgeable.

The answer to "extend the baseline" is **no**, on three independent grounds, each sufficient.

## 1. The blind spot is exactly what it was claimed to be

Demonstrated on the shipped baseline with the shipped tool, before changing anything. One assertion
removed from one case of `evidence/g1/change.json`, compared against `runlog.json`:

| case doctored | module | tier 3 says |
|---|---|---|
| `BaseLib_HttpServerPerfTest` 130 → 129 | `utf_baselib_http`, **covered** | `ASSERTION COUNT CHANGED`, 727 differences |
| `RedirectPolicy_MethodRewriteMatrixTests` 152 → 151 | `utf_baselib_httpclient`, **uncovered** | **726 differences — byte-identical to the undoctored capture** |

Tier 3 is not *blind* on these modules, which matters for reading a report: `CASE DID NOT PASS`,
`MODULE DID NOT REPORT CLEAN` and `MODULE REPORTED FAILURES` are absolute, read from the after side
alone, so an outright failure in an uncovered module is still caught. What is lost is the
differential half — registered set, executed set, skips, and per-case assertion counts. That half is
the whole of what tier 3 is for.

## 2. Five of the seventeen modules cannot carry a baseline

57 captures of an unchanged tree, each a full `utf_runlog.py --run` over the 17 modules (303 cases),
through `build-slot.sh`, some runs idle and some concurrent with another lane's compile.

| module | what varies | runs |
|---|---|---|
| `utf_baselib_httpclient4` | `ClientSession_AgainstTheLibraryHttpServerTests` aborts | 4 of 57 |
| `utf_baselib_httpclient4` | `ClientSession_SinkIsToldCompleteOnceAcrossTheFallbackRetryTests` aborts | 1 of 57 |
| `utf_baselib_httpclient5` | `Http1DriverTls_IdleCloseSendsCloseNotifyTests` aborts | 1 of 57 |
| `utf_baselib_h2client6` | `H2Driver_AnsweredRequestSurvivesAPeerEndingOnTheWriteTests` aborts | 1 of 57 |
| `utf_baselib_h2client4` | **hangs** — 0 of 20 cases ran, killed at the 600 s per-module timeout | 1 of 57 |
| `utf_baselib_h2client` | **hangs** — 0 of 12 cases ran | 1 of 57 |
| `utf_baselib_h2client` | `TcpStrandedStreams_PlainFullDuplexTests` 53 ↔ 58 assertions, passing | 1 of 57 |

Then the gate's own shape, simulated exactly as `g1-gate.sh` runs it — the nondeterministic list
derived from captures 1 and 2, capture 1 then compared against each of the 55 later ones:

**9 of 55 red. 16.4%, on a tree that did not change.**

The remaining twelve modules showed nothing in 57 runs. `utf_baselib_httpclient3` is among them, and
that is **not** evidence its known refusal flake is gone: the ~10% figure is the pre-H01 rate and the
post-H01 rate is ~1%, which over 57 runs has an expected count near 0.6. Zero reds there is the
expected outcome, not a result.

## 3. The nondeterministic mechanism does not stretch to any of it

`g1-gate.sh` captures the parent twice so `utf_runlog.py` can derive a list of cases to compare on
outcome alone. That list absorbs **exactly one** shape of variation: a case which *passes in both
captures* with different assertion counts. Read `compare( )`:

- `OUTCOME CHANGED` is appended **before** the `if name in unstable: continue` guard, so a listed
  case still reds the moment it flips `passed → aborted`.
- `CASE DID NOT PASS`, `MODULE DID NOT REPORT CLEAN`, `MODULE REPORTED FAILURES`,
  `MODULE EXIT CHANGED` and `NO LONGER RUNS` never consult the list at all — by design, and the
  comments in the file say why.

So four of the six failure modes above are outside it by construction, and the hangs are the worst
of them: a hung module contributes 12 or 20 `NO LONGER RUNS` lines at once.

**And the derivation is itself a lottery.** From captures 1 and 2 of this tree it produced **0
cases** — at these rates neither capture happened to catch anything. Worse, an abort *also* moves
the assertion count (10 → 7, 6 → 4, 4 → 3, 3 → 6), so the only way a flaky case enters the list is
that one of the two baseline captures caught its abort — and in that same capture the outcome check
reds anyway. The mechanism cannot help here even when it fires.

## 4. The baseline is a Windows artifact and cannot be extended from a Linux host

`runlog.json` was captured over `win-x86-vc143-debug` (`91d5c2c`, "Two independent full passes over
win-x86-vc143-debug, 17 modules"). Compared against a Linux `gcc1520 debug` capture of the same 17
modules it reports **105 differences, 37 of them `ASSERTION COUNT CHANGED`**, and they are plainly
the platform rather than the tree:

    BaseLib_OSJunctionsTests                      18 -> 0
    BaseLib_OSRegistryValueTest                    8 -> 0
    BaseLib_OSCreateProcessArgvQuotingWindowsTests 81 -> 0
    BaseLib_LfnPrefixesTests                      35 -> 14
    Tasks_ScanDirectoryTaskTests                 411 -> 15

Appending Linux records for the client modules would make the file wrong on **both** platforms. This
also means tier 3 is already unusable on Linux against this baseline, which is a larger finding than
the one this work was sent to close and is **not** fixed here.

> **Closed 2026-09-24, in a later change-set — both halves.** The paragraph above stands as the
> finding; it no longer describes the state. The baseline carries `__platform__` and `--compare`
> **refuses** across a mismatch at exit 3, which `check_split.sh` renders as SKIP-with-reason, so
> tier 3 on Linux is no longer unusable — it declines to answer, loudly, instead of answering
> wrongly. An **unstamped** baseline is refused the same way, since the only unstamped baseline in
> existence is also cross-platform. Merged at `05f503f`.
>
> Two of the numbers above were also classified and are **not** what this section cited them for:
> of the 251 differences only **43 are platform**, the remaining 208 being the baseline's age — 86
> cases relocated by splits that landed the same day *after* `91d5c2c`, plus 18 added since, doubled
> across two signals. The separate 813 figure is 741+36+36 with **zero** platform content, because
> none of those six modules is among the baseline's 17; it reds identically on Windows. The
> conclusion held and the premises did not, which is why they are corrected here rather than left
> to be cited again.
>
> The 37-assertion-count figure in `src/utests/AGENTS.md` is the original measurement, attributed
> and unchanged; only its tense was corrected, at `402d4f6`.

## What was done instead

- `utf_runlog.py --compare` now ends every comparison, PASS or FAIL, with a coverage statement
  naming the modules the baseline does not cover and stating what was therefore not compared. New
  `--uncovered PATH` supplies a per-module reason, and a reason naming a module which *is* covered
  is reported as stale rather than left to rot.
- `check_split.sh` passes `baseline/uncovered.json` when it exists, and its tier 3 summary line now
  reads *runtime equivalence, baseline modules only*.
- `baseline/uncovered.json` carries the seventeen reasons above.

## What would reverse this

- **For the five unstable modules:** each flake fixed or quarantined. They are separate defects, not
  a tier 3 problem — `ClientSession_AgainstTheLibraryHttpServerTests` is already recorded as a
  pre-existing `connectionsCreated` miscount, and the two hangs are new here and unexplained.
- **For the other twelve:** a decision on whether the tier 3 baseline becomes per-platform. They
  were stable across 57 runs and are otherwise ready.

## What this could not settle

- **The two hangs have no stack.** `ptrace_scope` refused `gdb -p`; the 37 threads were all in
  `futex_wait_queue` bar one in `ep_poll`, so it is a deadlock and not a spin. `utf_runlog.py`
  discards `TimeoutExpired.output`, so the capture keeps no partial log and the hanging case is not
  named. Reproducing either hang with the module's output on a file is the next step.
- **Windows was not run**, as always from this host, so the rates above are `a64 clang debug` only.

## 2026-09-25: the Windows refresh, and why `utf_baselib_http2` is covered now

The baseline was refreshed on `win-x86-vc143-debug` over **28 modules**, by the maintainer's decision
(`astra-remediation-owed-work.md` W9). `utf_baselib_http2` came in with it, and **not** by this
record's reversal condition: it is a numbered sibling of `utf_baselib_http`, made by the module split,
and it holds 19 cases the split moved out of `utf_baselib_http` - cases the old capture was still
checking, tree-wide, and which a refresh leaving it out would have dropped. It was stable in the 57
Linux runs above and in three `win-x86-vc143-debug` runs.

**The other sixteen stay refused.** Their reasons in `uncovered.json` were reworded: "this host cannot
produce Windows records" was written on Linux and reads as false on the Windows host that runs tier
3, so each now says it was stable in 57 Linux runs and is not yet captured on Windows, admission
being one module at a time and deferred. `utf_baselib_httpclient5`'s now says its 1-of-57 abort
matches the timing defect fixed at `1184eb4`, which the Linux capture kept no message to prove.
