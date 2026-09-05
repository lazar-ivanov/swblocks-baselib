# F-01…F-18 status — `pr_review_analysis_gpt56sol.md`

**Date:** 2026-09-02
**Review:** [pr_review_analysis_gpt56sol.md](notes/reviews/swblocks-baselib/major/update_2025/v1/pr_review_analysis_gpt56sol.md)
— 18 findings (1 Critical, 9 High, 7 Medium, 1 Low) filed against `lazari2` at tip `dcdb2f4`
**Verified against:** the working tree at `bc50233`, 31 commits later

## Why this document exists

No record tracked `F-01…F-18` as a set. `medium-severity-findings-f11-f17-plan.md` covers only
F-11…F-17; `pr-review-deep-dive-residual-findings.md` and `pr-review-residual-cxx-findings-plan.md`
use the deep-dive review's separate `CXX-` ID space, which shares only F-07 with this review.

That gap had a cost: **three residuals were sitting inside findings recorded as *Fixed***, so they
were invisible to anyone reading the dispositions. Each finding below was re-checked against the
source rather than taken from a commit message.

## Disposition

| ID | Sev | Status | Evidence |
|---|---|---|---|
| F-01 | Critical | **Fixed** | `validate_s3_key_components` / `resolve_download_path` / `lstat_walk_parents`, atomic `mkstemp`+`os.replace`, collision preflight; 38 hostile keys in `TestDownloadPathSafety` |
| F-02 | High | **Deferred** | [supply-chain-verification-deferral.md](scripts/devenv7/docs/supply-chain-verification-deferral.md) |
| F-03 | High | **Fixed** | [s3_manage.py:1167](scripts/s3_manage.py#L1167),[:1179](scripts/s3_manage.py#L1179),[:1231](scripts/s3_manage.py#L1231) — percent-encode + `html.escape(quote=True)`; prefix scheme check at [:1108](scripts/s3_manage.py#L1108) |
| F-04 | High | **Fixed** | all five commands aggregate to an exit code; [s3_manage.py:1727](scripts/s3_manage.py#L1727); `TestExitCodeContract` |
| F-05 | High | **Fixed** | SHA-256 in object metadata, preferred over ETag; candidate chunk sizes now require a full ETag match, never a part-count guess |
| F-06 | High | **Fixed** | [BoostAsioCompat.h:258](src/include/baselib/core/detail/BoostAsioCompat.h#L258) forwards `protocol()` + `flags_value()`, sync and async; `TestBoostAsioCompat.h` |
| F-07 | High | **Fixed** | generation validation (`m_activeWorkGeneration` / `m_lastPublishedGeneration`) + mandatory `NotifyDelivery`; [execution-queue-notification-delivery-breaking-change.md](notes/plans/issues/execution-queue-notification-delivery-breaking-change.md) |
| F-08 | High | **Fixed** | [JsonSecuritySerializationImpl.h:530](src/include/baselib/security/JsonSecuritySerializationImpl.h#L530),[:553](src/include/baselib/security/JsonSecuritySerializationImpl.h#L553) — SPKI then PKCS#1 fallback; `TestPemKeyFormats.h` |
| F-09 | High | **Fixed** | [utf_baselib_jni/Makefile:20-44](projects/make/utests/utf_baselib_jni/Makefile#L20-L44) — OS and devenv-version decisions separated |
| F-10 | High | **Fixed** | `blhash/v2`: domain-separated, length-prefixed records; directory symlinks and junctions rejected |
| F-11 | Medium | **Decided** | sub-items closed individually; canonical format deferred — see below |
| F-12 | Medium | **Fixed** | [JsonUtils.h:166](src/include/baselib/core/JsonUtils.h#L166) — `= delete`d integral overload |
| F-13 | Medium | **Deferred** | [medium-severity-findings-f11-f17-plan.md](notes/plans/issues/medium-severity-findings-f11-f17-plan.md) |
| F-14 | Medium | **Fixed** — *was incomplete, now closed* | see Correction 1 |
| F-15 | Medium | **Fixed** — *was incomplete, now closed* | see Correction 2 |
| F-16 | Medium | **Fixed** | [UuidBoostImports.h:27](src/include/baselib/core/detail/UuidBoostImports.h#L27) — `<boost/version.hpp>` before the first guard |
| F-17 | Medium | **Fixed** | [OSImplPlatformCommon.h:2007](src/include/baselib/core/detail/OSImplPlatformCommon.h#L2007) — `is_directory( sourcePath, ec )` |
| F-18 | Low | **Partly fixed, partly deferred** | items 3/4 fixed (3 completed here — Correction 3), items 1/2 deferred, item 5 rejected |

## Corrections — three findings were recorded as Fixed while still incomplete

These are the substantive output of this review pass. All three are now closed.

### Correction 1 — F-14 left an unconverted instance of its own bug

The original work converted six gates to `BL_DEVENV_IS_LEGACY` and declared F-14 fixed. It worked
from a fixed file list and never searched for other occurrences, so
[msvc-default.mk](projects/make/toolchain/msvc-default.mk) was missed. Its Python gate tested
`devenv7` equality with an `else` branch selecting **Python 2.7** — a future `devenv8` on Windows
would have taken it, which is precisely the failure mode F-14 was filed about. A second gate at
`:428` would have silently dropped `-DBOOST_ASIO_DISABLE_DEPRECATED_MSG`.

Both now use `ifeq (, $(BL_DEVENV_IS_LEGACY))`. The predicate is defined at
[devenv-detect.mk:219](projects/make/devenv-detect.mk#L219), included from
[common.mk:22](projects/make/common.mk#L22), while the toolchain files load at
[common.mk:294-306](projects/make/common.mk#L294-L306) — so it is in scope, verified.

`devenv-detect.mk:204,254,285,290` and `gcc-default.mk:42` remain equality tests deliberately: they
are per-version pins that a future devenv must declare for itself.

### Correction 2 — F-15 never touched `debug_harness.py`, and left a test asserting the old contract

The finding named `scripts/debug_harness.py` alongside the two compiler wrappers. Commit `d55d433`
changed only `README.md`, `cl.py`, `clang-cl.py` and `test_cl_functional.py`.

**A naive port of the `cl.py` fix would have made this file worse.** `cl.py` decodes `latin-1`
because it writes those bytes *back* to the `.d` file — a lossless round-trip where display is
secondary. `debug_harness.py` only prints to a UTF-8 stdout, so an unconditional `latin-1` decode
would mojibake genuine UTF-8 test output, which is the normal case on Linux and macOS. It now tries
UTF-8 first and falls back to `latin-1`; both branches are lossless and neither can raise.

Separately, [test_cl_unit.py](scripts/tests/test_cl_unit.py) `TestByteDecoding` still re-implemented
the *superseded* decode in a helper documented as "Replicate the decode logic from cl.py", and
asserted `'�' in line` — locking in the exact behaviour F-15 removed. This is the defect class
F-04 was filed about, in the test suite. It now asserts the real contract: no `U+FFFD`, and
`line.encode('latin-1') == raw`.

Two tests that claimed to cover non-ASCII UTF-8 were using pure-ASCII literals and could not have
detected a regression; they now use a real non-ASCII character.

### Correction 3 — F-18 item 3 did not establish header self-containment

Item 3 was recorded as fixed after `<algorithm>`/`<cstring>`/`<cwchar>`/`<iosfwd>` were added to
`CPP.h` and `<cstring>` to `Compiler.h`. But six public headers still called `std::mem*` while
relying on a transitive include — compiling, as the finding itself put it, "by coincidence of another
library's include graph". `<cstring>` has been added to each:

`data/DataBlock.h`, `core/detail/OSImplWindows.h`, `messaging/AsyncExecutorWrapperBlocks.h`,
`jni/JavaVirtualMachine.h`, `security/AuthorizationServiceRest.h`, `messaging/MessagingUtils.h`

**Found while verifying:** [JavaVirtualMachine.h](src/include/baselib/jni/JavaVirtualMachine.h) does
not compile standalone even after the fix — `fs::normalize` is not visible without a prior include.
This was confirmed **pre-existing** by compiling the pristine `HEAD` version, which fails identically.
It is a genuine self-containment gap of the same family as F-18 item 3, but it is not a `mem*`
include and was left alone rather than expanding scope silently. `OSImplWindows.h` is Windows-only
and cannot be compile-checked here.

## F-11 needs nothing further

Its sub-items were each closed on their own terms, mostly in
[pr-review-residual-cxx-findings-plan.md](notes/plans/issues/pr-review-residual-cxx-findings-plan.md):

- **`rawUtf8`** — documented as a no-op and json-spirit aligned to raw UTF-8, so the two backends now
  agree rather than diverging silently (CXX-08 / A2).
- **Numeric policy** — json-spirit's `value_to` given the same checked conversions (A3).
- **Nested `canonicalize`** — forwarding through the vector and map macros fixed (A4).
- **Duplicate keys** — specified as backend-defined in
  [json-duplicate-key-contract.md](notes/plans/issues/json-duplicate-key-contract.md) (A1). Tightening
  Boost.JSON was costed at ~4–6 days plus a 10–30% parse cost on every document and rejected; the
  parser differential against non-baselib peers exists either way.

What remains is the canonical wire format, an aspirational deferral. **The question that made it
expensive is now answered: no `getObjectHash` output is persisted, signed, or used as a cache key
outside the process** (confirmed by the author, 2026-09-02). There is nothing to migrate, so the
deferral is correct rather than merely accepted, and the contract stated at
[DataModelObject.h:287-306](src/include/baselib/data/DataModelObject.h#L287-L306) is true today.

This fact is not derivable from the code — in-repo absence of callers does not settle downstream
consumers — which is why it is recorded here. **Keeping it true is the only obligation it creates**;
a release note should say these hashes are process-local.

## Still open

| Item | Where | Note |
|---|---|---|
| F-02 — bootstrap download integrity | [deferral record](scripts/devenv7/docs/supply-chain-verification-deferral.md) | "must be resolved as part of the devenv8 upgrade, not deferred again by default". Steps 1–2 (atomic download, pin PortableGit) are ~1 day and retire most of the risk |
| F-13 — S3 credentials on the command line | [plan record](notes/plans/issues/medium-severity-findings-f11-f17-plan.md) | still `required=True`; `--account-id` still unread. ~1–2 days, own commit |
| F-18 items 1–2 | [deferral record](notes/plans/issues/public-header-hygiene-deferral.md) | `-Woverloaded-virtual` scope; `BOOST_ASIO_DISABLE_STD_CHRONO` ODR hazard |
| `download-sources.ps1` parameter mismatch | now tracked in the [F-02 record](scripts/devenv7/docs/supply-chain-verification-deferral.md) | was an untracked aside; promoted to a deferred item so it stops resurfacing |
| Release notes | — | PEM formats (PKCS#1 → SPKI / PKCS#8), mandatory `NotifyDelivery`, deleted `Manifest`/`Platform` move assignment, process-local JSON hashes |

**Windows devenv7 verification is now closed** (2026-09-03). Correction 1's two `msvc-default.mk`
gates, `OSImplWindows.h`'s `<cstring>` addition and the real `cl.exe` half of F-15 were all verified
on the ARM64 Windows devenv7 host — see
[Bundle D — Windows verification results](pr-review-opus5-residual-findings-plan.md). It corrects
one row of the table under **Verification performed** below: `JavaVirtualMachine.h` **does** compile
standalone on Windows under both `cl.exe` and `clang-cl`, so the `fs::normalize` gap recorded there
is specific to the non-Windows include graph rather than universal.

F-18 item 5 was **rejected as stated and rescoped**: of 97 `git diff --check` failures, 17 are
Markdown hard line breaks, 16 are unshipped `notes/`, 16 are cosmetic EOF blanks. The gate is
`git diff --check <merge-base> -- src`, confirmed clean.

## Verification performed

| Check | Result |
|---|---|
| `pytest scripts/tests/test_debug_harness_unit.py` | 32 passed, 2 skipped |
| `pytest scripts/tests/test_cl_unit.py` | 36 passed |
| `pytest scripts/tests/test_cl_functional.py` | 22 passed |
| Standalone TU compile, 5 non-Windows headers | 4 pass; `JavaVirtualMachine.h` fails on a pre-existing `fs::normalize` gap, confirmed identical on pristine `HEAD` |
| `make -k -j2 utf_baselib_data` / `utf_baselib_messaging` / `utf_baselib_security` | exit 0 each, `-Werror` |
| `utf-baselib-data` | 73 test cases, no errors |
| `utf-baselib-security` | 24 test cases, no errors |
| `make -p` variable comparison, devenv7 | `BL_DEVENV_IS_LEGACY` empty, gate resolves as before — no-op confirmed |
| `git diff --check <merge-base> -- src` | clean |

Builds were run at `-j2`, sequentially, one target at a time — this host has 2 cores. Incremental
rather than clean-tree: `-MMD -MP` dependency tracking rebuilds every consumer of a changed header,
and the standalone TU compiles prove the self-containment property directly, which a full build does
not.

**Not verified here:** anything Windows-only — Correction 1's gates, `OSImplWindows.h`, and F-15
against real `cl.exe` code-page output. **All three were verified on 2026-09-03** on the ARM64
Windows devenv7 host; see
[Bundle D — Windows verification results](pr-review-opus5-residual-findings-plan.md).
