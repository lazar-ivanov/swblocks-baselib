# The x86 module sizes after the HTTP/2 pull, and why `utf_baselib_io` should not be split again

**Measured:** 2026-09-21, from a full 12-combo Windows build-and-test matrix at commit `ba71298`
(3 architectures x {vc143, ccl16} x {debug, release}, one combo at a time, `-j1` builds and `-j4`
tests). **Status:** analysis only, nothing implemented. The recommendation below is a proposal and
its predicted numbers are NOT measured - see "What this cannot tell you".

## The verdict

**No module violates the ceiling on either enforcing combination.** Nothing has to be split to make
the tree legal.

| Combo | Gate | Over 75 MB ceiling | Over 40 MB target | Peak object |
|---|---|---:|---:|---|
| `win-x86-vc143-debug` | **enforce** | **0** | 24 | `utf_baselib_io` 72.27 |
| `win-x86-ccl16-debug` | **enforce** | **0** | 24 | `utf_baselib_io` **74.34** |
| `win-x86-vc143-release` | report | 10 | 31 | `utf_baselib_io` 96.79 |
| `win-x86-ccl16-release` | report | 0 | 0 | `utf_baselib_messaging3` 35.70 |

The ten on `x86-vc143-release` are **not** violations. That combo is `gate: report` precisely
because the 75 MB number was calibrated against a 32-bit clang-cl crash on a *debug* object, and it
builds perfectly well - see `src/utests/object-size-limits.json`. `utf_baselib_io` measures
**34.24 MB to 96.79 MB** across the four x86 combos, a 2.8x spread on one module, which is the
concrete reason a single ceiling cannot govern all four.

## The one thing that needs attention

`utf_baselib_io` sits at **74.34 / 75 MB on `win-x86-ccl16-debug` - 0.66 MB, or 0.9% of headroom**,
on a combination where the gate **fails the build**. The build is configured `.DELETE_ON_ERROR`, so
the next test case added to that module does not merely warn: it takes `utf-baselib-io.exe` with it
and that module cannot be run until it is under the ceiling again.

It is the tightest object in the tree on 8 of the 12 combos measured.

**The pull is not the cause.** The 14 modules the HTTP/2 and HTTP-client work added are all well
clear - the largest, `utf_baselib_httpclient4`, is 55.81 MB (74%). Every module above it predates
the pull, and `utf_baselib_io` is unchanged by it: 72.27 MB here against 72.26 MB recorded on
2026-09-14.

Modules at or above 50 MB on the two enforcing combos:

| Module | vc143 debug | ccl16 debug |
|---|---:|---:|
| `utf_baselib_io` | 72.27 | **74.34** |
| `utf_baselib_messaging3` | 69.95 | 67.92 |
| `utf_baselib_messaging` | 68.43 | 68.76 |
| `utf_baselib_tasks` | 67.67 | 68.76 |
| `utf_baselib_apps2` | 65.43 | 65.03 |
| `utf_baselib_messaging4` | 60.45 | 59.97 |
| `utf_baselib_rest` | 60.45 | 59.52 |
| `utf_baselib_messaging2` | 57.30 | 57.12 |
| `utf_baselib_io2` | 56.94 | 57.10 |
| `utf_baselib_httpclient4` | 55.81 | 57.69 |
| `utf_baselib_blobtransfer2` | 54.44 | 55.74 |
| `utf_baselib_http` | 54.93 | 55.67 |
| `utf_baselib_httpclient5` | 54.13 | 55.02 |
| `utf_baselib_security3` | 52.39 | 53.47 |
| `utf_baselib_blobtransfer` | 52.11 | 52.87 |

Note `ccl16` is the tighter toolchain for `io` and for most of the top of this table, while the
2026-09-14 matrix recorded `vc143` as the tighter one at 72.26 vs 74.33. The two have effectively
swapped rank at the top; whichever is tighter, `io` is the module in front.

## Why splitting `utf_baselib_io` again is the wrong answer

**`utf_baselib_io2` has a byte-identical include list to `utf_baselib_io`.** Both pull the whole
messaging and block-transfer stack - `BrokerFacade.h`, `BrokerDispatchingBackendProcessing.h`,
`TcpBlockTransferClient.h`, `TcpBlockServerMessageDispatcher.h`, `MessagingClientFactory.h` and the
rest. That is the ledger's "shared helper stack" case in its purest form, and it prices the split:

- The first split took `io` from **77.4** to **72.3 + 56.9**.
- Sum 129.2 against 77.4, so **51.8 MB was duplicated, not divided**.
- That 51.8 is about the 21 MB TU floor plus ~31 MB of shared instantiation.
- `utf_baselib_io2` carries **56.94 MB for 5 cases and 1,354 lines** - nearly all of it that stack.

So a third sibling pays ~52 MB of baseline before it holds a single test case. `io` is 72.27, which
leaves roughly 20 MB of genuinely divisible content; dividing it evenly lands near **62 + 62** and
buys about **10 MB** of headroom on the peak, at the cost of a whole extra module.

**That is a trade this tree has already made, measured and reverted, twice:** `utf_baselib_http3`
(2.5 MB gained for a 51.1 MB module) and `utf_baselib_security4` (0.4 MB gained for 37.7 MB). See
`notes/reviews/major/update_2026/test-module-split-ledger.md`.

## What to do instead, in order of preference

1. **Move the shared `io` / `io2` instantiation out of line.** This is the technique that resolved
   `utf_baselib_rest` (78.7 -> 48.8 / 63.4, commit `a00f41b`) *without* splitting it, and the
   ledger's condition for it paying - several modules each instantiating the same thing - is
   exactly met here by two modules with identical includes. `messaging`, `messaging3`, `messaging4`
   and `rest` already carry an out-of-line `Impl` translation unit; `io` is still single-TU and has
   never had the treatment. This is the only option that *reduces* weight rather than relocating it.

2. **Decide to do nothing, and record it where it will be seen.** The tree is legal. But 0.66 MB is
   not room to grow, and someone will add an I/O test case without knowing. `src/utests/AGENTS.md`
   already tells contributors to check `make utests-sizes` first; a note naming `utf_baselib_io`
   specifically would be cheaper than the build failure.

3. **Split `io` a second time.** About 10 MB for a whole extra module, against two recorded
   precedents that argue it does not pay.

The underlying lever - reducing instantiation weight inside baselib itself - is already tracked in
`notes/plans/issues/test-instantiation-weight-deferral.md` and is what would actually move the
target rather than the ceiling.

## What this cannot tell you

**Every size above is measured; the ~62/62 prediction is not.** The ledger records isolated
per-header probes being wrong by a factor of 40, and requires a proposed grouping to be built and
measured - by leave-one-out, not in isolation - before it is believed. Treat that figure as an
order of magnitude and nothing more.

The matrix also says nothing about `x86-ccl16-release`, which is bounded by peak memory in the
*optimizer* and not by object size; its objects are the smallest in the tree (35.70 MB peak, zero
over the 40 MB target) while being the slowest combination to build. Object size is a proxy for
compiler address space, and that proxy is calibrated only for debug.
