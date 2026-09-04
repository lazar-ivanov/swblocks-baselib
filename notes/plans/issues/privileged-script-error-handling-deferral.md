# Privileged Provisioning Scripts — Shell Error Handling: Deferral Record

This document records a deliberate decision **not** to add `set -e` / `set -u` / `set -o pipefail`
to the nine devenv7 rosetta and docker provisioning scripts in this cycle, and the conditions under
which that decision must be revisited. It is a risk acceptance, not an assessment that the risk is
absent.

**Finding:** M-12 — "`set -e` missing in the privileged install scripts; no `pipefail` anywhere",
`notes/reviews/major/update_2025/v1/pr_review_analysis_opus5.md` (Medium). Tracked as R-13 in
`notes/plans/issues/pr-review-opus5-residual-findings-plan.md`.

---

## Decision

**Date:** 2026-09-03
**Status:** Deferred to the next refresh of the deployment scripts and their design

| # | Item | Disposition |
|---|---|---|
| 1 | `set -e` / `set -u` in the nine rosetta and docker scripts | **Deferred** |
| 2 | `set -o pipefail` across all `scripts/` shell scripts | **Deferred** |

**This is a risk acceptance, not an assessment that the risk is absent.**

---

## What is not protected

Nine scripts, 125 lines in total, set neither `set -e` nor `set -u`. `set -o pipefail` appears
**nowhere** under `scripts/`.

| Script | Lines |
|---|---:|
| `scripts/devenv7/rosetta/rhel/rosetta-install.sh` | 28 |
| `scripts/devenv7/rosetta/rhel/register-rosetta.sh` | 19 |
| `scripts/devenv7/rosetta/ubuntu/rosetta-install.sh` | 10 |
| `scripts/devenv7/rosetta/ubuntu/register-rosetta.sh` | 15 |
| `scripts/devenv7/docker/rhel/docker-install.sh` | 24 |
| `scripts/devenv7/docker/rhel/launch.sh` | 10 |
| `scripts/devenv7/docker/ubuntu/docker-install.sh` | 8 |
| `scripts/devenv7/docker/ubuntu/launch.sh` | 9 |
| `scripts/devenv7/docker/ubuntu/build-image.sh` | 2 |

These are the scripts that install system packages, register binfmt handlers, modify systemd units
and add users to the `docker` group. A failed step is followed by the next step running against a
half-configured system.

The inconsistency is real and worth naming: **all 19 devenv7 *build* scripts already set `-e` and
`-u` correctly.** These nine are an inconsistency rather than a considered exception — which is why
the finding is legitimate and why this record exists rather than a simple rejection.

---

## Why this is deferred

**The change cannot be validated from a development checkout.** Verifying `set -euo pipefail` on
privileged provisioning scripts means actually running them, as root, on RHEL and Ubuntu hosts with
Parallels Rosetta shares and Docker present. Nothing in this repository's test suite reaches them,
and a syntax check (`bash -n`) cannot find the steps that matter.

**And blind adoption would break at least two scripts.** Reading the nine turned up concrete cases
where a non-zero exit is currently expected and ignored:

- **`docker/rhel/docker-install.sh:2`** — `sudo dnf remove -y docker docker-client …` lists eight
  packages that are *not installed on a clean machine*. `dnf remove` reports "No packages marked for
  removal" and exits non-zero in that case, so `set -e` would abort the Docker installer at its
  first step on exactly the machine it is meant to provision.
- **`rosetta/rhel/rosetta-install.sh:20`** and **`rosetta/ubuntu/rosetta-install.sh:9`** — both end
  with `systemctl status rosetta-binfmt.service --no-pager`, which exits non-zero when the unit
  failed to start. Under `set -e` the script would abort *there*, skipping the diagnostic block
  immediately below it:

  ```bash
  if [ -f /proc/sys/fs/binfmt_misc/rosetta ]; then
      echo "SUCCESS: Rosetta is registered in the kernel."
      cat /proc/sys/fs/binfmt_misc/rosetta
  else
      echo "ERROR: Rosetta registration failed."
  fi
  ```

  So `set -e` would suppress the script's own failure diagnostic precisely in the failure case it
  was written to report. That is a worse outcome than the status quo, not a better one.

This is the general shape of the risk: `set -e` on a script that has lived without it converts a
silent half-configuration into a **loud half-configuration at an arbitrary point**, which is only an
improvement once each affected step has been triaged. Triage needs the scripts run.

`set -o pipefail` carries the same problem in miniature — `register-rosetta.sh` writes through
`echo … | sudo tee /proc/sys/fs/binfmt_misc/register`, and whether a failure of `tee` on a kernel
interface should abort the script is a decision that needs a real binfmt_misc mount to make.

---

## What limits the exposure

- **These are developer and CI provisioning scripts, not library code.** Nothing shipped by
  swblocks-baselib depends on them, and no unit test invokes them.
- **They are small enough to audit at a glance.** Nine scripts, 125 lines, five of them under 16
  lines. A reader can hold the whole surface in view — which is not true of the 19 build scripts
  that *do* set `-e`.
- **They already use the `set -e` idiom where it matters.**
  `docker/ubuntu/docker-install.sh:5` is `sudo docker buildx rm rosetta-builder || true`, so the
  authors were already marking expected failures explicitly.
- **They are run interactively, by a developer, once per machine.** The operator sees the output. A
  half-configured result is visible immediately in a way it would not be inside an unattended
  pipeline.

---

## Conditions to revisit

- **The next refresh of the deployment scripts and their design.** This is the expected trigger:
  the work belongs with whoever is already running these scripts on real hosts and can validate the
  change as they go.
- Any of the nine growing beyond roughly a page, at which point "auditable at a glance" stops being
  true.
- Any defect report that traces back to a half-configured provisioning run.
- Any move to run these unattended — from CI, from an image build, or from a fleet tool — since the
  interactive-operator mitigation disappears at that point.

## Scope when picked up

1. Run each of the nine on both RHEL and Ubuntu **before** changing anything, and record which steps
   exit non-zero in normal operation.
2. Add `set -euo pipefail`, then mark the steps found in step 1 with `|| true` — or restructure them
   so the expected failure is expressed rather than ignored.
3. Move the two rosetta diagnostic blocks *above* the `systemctl status` call, or guard that call,
   so the script's own error reporting survives.
4. Add `set -o pipefail` to the 19 build scripts, which already carry `-e` and `-u` and are the
   lower-risk half of this work.

**Related deferred script defects.** Eleven further defects in the devenv7 build and install
scripts (download failure handling, Windows batch expansion and quoting, `wmic`, floating Docker base
images, macOS Boost architecture tagging, non-fatal Windows OpenSSL tests) are recorded, with the
same "record, do not fix blind" rationale, under "Deferred defects in the devenv7 deployment
scripts" in `scripts/devenv7/docs/supply-chain-verification-deferral.md`. Whoever picks up either
record should read both.
