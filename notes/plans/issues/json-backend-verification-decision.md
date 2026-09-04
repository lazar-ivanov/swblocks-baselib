# JSON Backend Verification: Decision Record

This document records the decision that the dual-backend build check stays **manual and prescribed**
rather than automated, so that the absence of a build target for it is read as a decision and not
re-filed as a gap.

**Finding:** §1.1 — "the two JSON backends are never built in the same environment",
`notes/reviews/major/update_2025/v1/pr_review_analysis_opus5_deep_dive.md`. Tracked as R-18 in
`notes/plans/issues/pr-review-opus5-residual-findings-plan.md`.

---

## Decision

**Date:** 2026-09-03
**Status:** Manual verification, deliberately — no build target and no CI job

| # | Item | Disposition |
|---|---|---|
| 1 | A `make` target that builds and runs both JSON backends | **Not added** — the manual command is the contract |
| 2 | A CI job enforcing it | **Not added** — the repository has no CI to add a job to |

**This is a deliberate choice, not an oversight.** The position was already stated in
`notes/plans/issues/json-duplicate-key-contract.md`: *"'Supported' here means buildable on demand
and documented. It does **not** mean continuously verified."* This record makes that a decision with
a rationale attached.

---

## The prescribed check

When a change touches either JSON backend adapter, the data-model macros, or `JsonUtils.h`:

```bash
make -k -j1 utf_baselib_data VARIANT=release BL_USE_JSON_SPIRIT=1
```

Then run the resulting `utf-baselib-data` binary.

**Use release, not debug.** json-spirit debug is measured at up to 146x slower than Boost.JSON
release on medium documents (`notes/performance/json-library-performance-comparison.md`); a debug
run against the larger test documents looks like a hang.

**Cite the command in the commit message** when it was required, so the record of it having been run
lives with the change rather than in someone's shell history.

### Correction to the command this inherits

`notes/plans/issues/pr-review-residual-cxx-findings-plan.md:461` prescribes
`make -k -j4 BL_USE_JSON_SPIRIT=1`. **Do not use that form.** It is an all-targets parallel build,
which contradicts `AGENTS.md`:

- *"Never do full parallel builds of the entire repo unless explicitly requested."* (`AGENTS.md:67`)
- *"When more than one test module (or the entire repo) must be built, do NOT parallelize the
  build — always use `-j1`."* (`AGENTS.md:70`)

The dual-backend check does not need a repository build. It needs `utf_baselib_data`, which is where
the JSON abstraction coverage lives, plus `utf_baselib_messaging` if the change reaches the data
model's messaging consumers. The focused form above is both correct under the build rules and
faster.

---

## Why this is not automated

**There is no CI in this repository.** Adding a job means introducing continuous integration, which
is an organizational decision about infrastructure, ownership and cost — disproportionate to closing
one verification gap, and not a choice this finding should force by the back door.

**A `make` target without CI buys little.** It would shorten the command but not make it run. The
thing that makes a check reliable is something that executes it without being asked, and that is
precisely the part not being added.

**The exposure is bounded and has been measured.** Shared production code sits inside the
intersection of the two backend APIs today. Repository-wide greps for the Boost.JSON-only surface —
`.kind()`, `is_number()`, `is_primitive()`, `is_structured()`, `if_contains(`, and any direct
`boost::json::` qualification — return **zero hits outside the two implementation headers**
(`BoostJsonImpl.h`, `JsonSpiritImpl.h`). The migration was done carefully; what is missing is
enforcement against future drift, not a present defect.

**json-spirit is a supported configuration, and that is why the check exists at all.**
`CONTRIBUTING.md` lists JSON Spirit 4.08 among the devenv7 dependencies, the headers are installed
into the devenv7 distribution by `scripts/devenv7/linux/install-json-spirit-linux.sh` and its macOS
counterpart, and devenv2–6 select it automatically. It is not legacy debt.

---

## What limits the exposure

- The intersection property above is verified, not assumed.
- The two backends' known divergences are individually recorded rather than latent: duplicate keys
  in `json-duplicate-key-contract.md`; `rawUtf8` and numeric policy closed by aligning the backends
  (`pr-review-residual-cxx-findings-plan.md`, CXX-08) - note that the `rawUtf8` alignment itself
  regressed control character escaping on json-spirit (review finding H-3's sibling H-2 in
  `pr_review_analysis_fable51.md`), which is exactly the kind of same-backend-invisible defect this
  check exists for; fixed by `escapeControlCharacters()` in `JsonSpiritImpl.h` and asserted on both
  backends by `JsonSerializeEscapesControlCharacters`; number and string formatting deferred with
  F-11; parse depth documented in `JsonUtils.h`.
- The prerequisites for running the check are already present in every devenv7 distribution, so the
  barrier is remembering to run it, not being able to.

---

## Conditions to revisit

- **CI is introduced for any other reason.** At that point adding this job is nearly free and it
  should be added immediately — the argument above is entirely about the cost of introducing CI, not
  about the value of the check.
- A backend divergence reaches a release.
- The json-spirit backend gains a second in-repository consumer, or Windows devenv7 gains a
  json-spirit installer (there is none today, so the check is Linux and macOS only).
- Any change makes the two backends' public surfaces diverge further, since the intersection
  property is what currently makes manual verification sufficient.
