# Bootstrap Download Integrity Verification: Deferral Record

This document records a deliberate decision **not** to add integrity verification to the devenv7
bootstrap scripts in this cycle, and the conditions under which that decision must be revisited.
For operational guidance (what commands to run), see the parent [AGENTS.md](../AGENTS.md).

---

## Decision

**Date:** 2026-08-30
**Finding:** F-02 — "Bootstrap scripts execute downloaded toolchains without integrity verification"
(High), `notes/reviews/swblocks-baselib/major/update_2025/v1/pr_review_analysis_gpt56sol.md`
**Status:** Deferred to the next major devenv upgrade (devenv8)

The devenv7 bootstrap scripts download compilers, libraries and tools over HTTPS and use them
without verifying a pinned checksum or publisher signature. This is a real gap. It is being carried
forward rather than fixed now because the remediation is broad and cannot be validated cheaply — see
[Why this is deferred](#why-this-is-deferred).

**This is a risk acceptance, not an assessment that the risk is absent.**

---

## What is not verified

Every artifact below is downloaded over HTTPS and then extracted or executed with no checksum or
signature check.

### Linux and macOS

No shell script under `scripts/devenv7/linux` or `scripts/devenv7/macos` performs any checksum
verification. There are 12 download sites, each a single `wget`/`curl` line:

| Script | Line | Artifact | Upstream |
|---|---|---|---|
| `linux/build-gcc-linux.sh` | 222 | GCC source | `ftp.gnu.org` |
| `linux/build-clang-linux.sh` | 234 | LLVM/Clang source | `github.com/llvm/llvm-project` |
| `linux/build-boost-linux.sh` | 199 | Boost source | `archives.boost.io` |
| `linux/build-openssl-linux.sh` | 443 | OpenSSL source | `www.openssl.org` |
| `linux/install-openjdk-linux.sh` | 140 | OpenJDK binary | `api.adoptium.net` |
| `linux/install-gradle-linux.sh` | 132 | Gradle binary | `services.gradle.org` |
| `linux/install-json-spirit-linux.sh` | 120 | JSON Spirit headers | `github.com/lazar-ivanov/swblocks-assets` |
| `macos/build-boost-macos.sh` | 117 | Boost source | `archives.boost.io` |
| `macos/build-openssl-macos.sh` | 328 | OpenSSL source | `www.openssl.org` |
| `macos/install-openjdk-macos.sh` | 104 | OpenJDK binary | `api.adoptium.net` |
| `macos/install-gradle-macos.sh` | 99 | Gradle binary | `services.gradle.org` |
| `macos/install-json-spirit-macos.sh` | 97 | JSON Spirit headers | `github.com/lazar-ivanov/swblocks-assets` |

### Windows

`windows/internal/download-tools.ps1` has 12 `Invoke-WebDownload` calls (lines 234, 292, 368, 517,
575, 680, 754, 822, 871, 959, 1020, 1088) covering the tools declared in `$script:ToolConfigs`: Git,
Python, MSYS2, Strawberry Perl, JSON Spirit, OpenJDK, Gradle, Boost, OpenSSL, Jom, 7-Zip and NASM.
**None passes `-ExpectedHash`.**

### The highest-severity single item

`Expand-ToolArchive` in `windows/internal/common.ps1` (lines 516-567) runs downloaded `.7z.exe`
self-extracting archives through `Start-Process`. **PortableGit is the only artifact in the tree
that is executed rather than extracted** — everything else goes through `tar`. If only one artifact
is ever pinned, it should be this one.

---

## Do not mistake `ExpectedHash` for protection

`Invoke-WebDownload` in `windows/internal/common.ps1` (lines 271-377) implements checksum
verification **completely**: it verifies after download, verifies on a cache hit, re-downloads on
cache mismatch, deletes partial files on failure, and retries transient failures three times.

The parameter defaults to `$null`, and verification is skipped when it is not supplied. No caller
supplies it. A reader who sees the parameter and concludes that Windows downloads are verified will
be wrong.

The one useful consequence: because verification is opt-in per call, hashes can be added one tool at
a time with **no code change and no flag day**. Unpinned tools keep behaving exactly as they do now.

---

## Risk accepted, and its limits

### What reduces exposure

The bootstrap scripts are not part of the normal developer workflow. `linux/build-env-all.sh` and
`macos/build-env-all-macos.sh` orchestrate a full environment build, and `archive-dists-*.sh` then
package the result as `tar.gz`/zip archives for distribution. The intended model is that a small
number of people provision a dist on a build machine and everyone else consumes the archive.

This narrows *how often* and *on how many machines* an unverified download happens.

> **Assumption to confirm at review time:** that no developer or CI job invokes the
> `install-*`/`build-*` scripts directly. If any does, this mitigation is weaker than stated here.

### What is not reduced

- **Impact is undiminished.** A poisoned dist is archived and redistributed to every consumer.
- **A compromised compiler contaminates everything it builds**, silently and durably, including
  artifacts published from that dist.
- **Cache entries are trusted on existence alone.** Every shell script gates on
  `if [ ! -f "${ARCHIVE}" ]`, and the Windows Git download passes `-SkipIfExists` with no hash. A
  download interrupted by a reset connection, a full disk or Ctrl-C can leave a truncated file that
  the next run accepts as valid. The scripts use `set -e`, which aborts the current run but does not
  remove the partial file.
- **Threats in scope of the original finding remain open:** CDN, upstream, mirror, DNS or TLS
  trust-chain compromise, and a malicious or compromised proxy.

---

## Why this is deferred

- **Roughly 30 artifact × architecture combinations** need a pinned digest across three operating
  systems and three architectures.
- **A hash cannot be produced without downloading the artifact on each target architecture**, so
  populating the list is not desk work.
- **Validation is the expensive part.** A wrong or stale hash does not degrade gracefully — it hard
  fails the bootstrap, on a machine that may be someone's only route to a working environment.
  Meaningful validation needs a provisioning run per OS × architecture.
- **Signature verification would widen it further**, adding key distribution and rotation.

---

## Known blocker: the OpenJDK URLs cannot be pinned as written

`linux/install-openjdk-linux.sh:110` and `macos/install-openjdk-macos.sh:75` resolve the JDK through
Adoptium's **`latest`** endpoint:

```
https://api.adoptium.net/v3/binary/latest/${JDK_VERSION}/ga/<os>/${JDK_ARCH}/jdk/hotspot/normal/eclipse
```

The bytes change with every JDK patch release, so no digest can be pinned against it. Windows uses
`https://aka.ms/download-jdk`, a redirector — and a different vendor (Microsoft Build of OpenJDK
versus Eclipse Temurin on Linux/macOS).

**Pinning the JDK URLs to a specific release is a prerequisite for any verification work, and is
worth doing on its own for build reproducibility.**

---

## Scope of work when this is picked up

Ordered by risk retired per unit of cost. Each step is independently shippable; there is no flag day.

1. **Atomic download** — download to `${ARCHIVE}.tmp` and rename into place only on success.
   Requires no digests at all and removes the partial-cache class entirely. Two lines per site.
2. **Pin PortableGit** — three digests (arm64/x64/x86) and one `-ExpectedHash` argument at
   `download-tools.ps1:234`. Closes the only arbitrary-code-execution path.
3. **Pin the compilers** — GCC and LLVM/Clang next, because a compromised compiler contaminates
   every artifact the dist produces.
4. **Pin the JDK URLs to a specific release** (see the blocker above), then digest them.
5. **Populate `ExpectedHash` for the remaining Windows tools**, adding a `Hashes` field to
   `$script:ToolConfigs`. No new code paths.
6. **Add a shared `verify_sha256` helper for Linux/macOS**, called after download and on cache hit,
   treating an empty expected value as skip-with-warning. 12 identical call sites.

**Bootstrapping the digest list:** most upstreams publish checksums beside the artifact (Gradle
`.sha256`, OpenSSL `.sha256`, GNU `sha512.sum`, Boost's published digests, and a `checksum` field in
the Adoptium asset API). Fetching a digest over the same channel as the artifact provides **no**
protection against an attacker controlling that channel, so it must not be done at install time.
It is acceptable for generating the list **once**, to be reviewed and committed — after which the
committed values are the independent, reviewable source of truth.

Explicitly out of scope: GPG/publisher-signature verification, and any rewrite of the download layer.

---

## Trigger for revisiting

**This must be resolved as part of the devenv8 upgrade, not deferred again by default.**

The pointer in the parent [AGENTS.md](../AGENTS.md) lives in *Tool Versions and Compatibility* —
the section a devenv upgrade has to edit anyway to bump tool versions — so it is encountered at the
point the work becomes relevant.

Revisit sooner if any of these change:

- A developer or CI workflow starts invoking the `install-*`/`build-*` scripts directly.
- Dist archives begin being distributed outside the current trusted group.
- Any upstream in the tables above suffers a publicised compromise.

---

## Deferred defect — `download-sources.ps1` passes a parameter that does not exist

**Status:** Deferred to the same trigger as the rest of this record — the devenv8 upgrade.

`windows/internal/download-sources.ps1` calls `Invoke-WebDownload -Url ... -DestinationPath ...` at
lines 96 and 212, but the function's parameter is `-OutputPath`. With `Set-StrictMode -Version
Latest` and `$ErrorActionPreference = "Stop"`, these calls throw at runtime.

**The inference matters more than the typo:** a mandatory parameter that has never been spelled
correctly means the Windows Boost/OpenSSL *source* download path has never been exercised. The
Windows source builds either take a different route or have not been run since this script was
written. Whoever picks this up should establish which, before assuming the rename is the whole fix.

### Why it is not being fixed now

- It is a deployment script that **cannot be tested from a development checkout**. Validating it
  needs a Windows devenv7 machine performing a real Boost/OpenSSL source provisioning run.
- The rename is one word, but it *exposes* an unexercised code path rather than repairing a working
  one. Shipping it means shipping whatever else is wrong further down that path, unvalidated —
  strictly worse than a call that fails loudly and immediately.

### Why it is recorded here rather than elsewhere

This defect was found during the F-02 analysis and was originally written up at the end of this
document as an aside saying it "should be tracked and fixed separately". No separate tracking record
was ever created, so it resurfaced unchanged in the next review. It is promoted to a first-class
deferred item here so that it is encountered by whoever next works in these scripts, which is the
same person the checksum items above are addressed to.

### Scope when picked up

1. Rename both call sites to `-OutputPath`.
2. Run a Windows Boost/OpenSSL source provisioning end to end and fix what it uncovers.
3. Fold the two sites into the atomic-download work in step 1 of the scope list above — they are
   downloads like any other, and will want the same `.tmp`-and-rename treatment.

---

## References

- Finding F-02: `notes/reviews/swblocks-baselib/major/update_2025/v1/pr_review_analysis_gpt56sol.md`
- Download helper with unused verification: `scripts/devenv7/windows/internal/common.ps1`
- Windows tool downloads: `scripts/devenv7/windows/internal/download-tools.ps1`
- Linux downloads: `scripts/devenv7/linux/build-*.sh`, `scripts/devenv7/linux/install-*.sh`
- macOS downloads: `scripts/devenv7/macos/build-*.sh`, `scripts/devenv7/macos/install-*.sh`
- Dist archiving: `scripts/devenv7/{linux,macos,windows}/archive-dists-*`
