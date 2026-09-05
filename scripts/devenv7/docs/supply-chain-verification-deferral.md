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

## Deferred defects in the devenv7 deployment scripts (2026-09-04 review)

**Status:** Deferred to the same trigger as the rest of this record — the devenv8 upgrade, or the
next provisioning run on the affected platform, whichever comes first.

**Source:** M-11 … M-17 in `notes/reviews/major/update_2025/v1/pr_review_analysis_fable51.md`;
assessed in `notes/plans/issues/pr-review-fable51-merge-gate-items4-8-plan.md`.

### Why none of these is being fixed now

Every item below is a change to a bootstrap script whose only validation is a full provisioning
run per operating system × architecture. The development checkout this was reviewed from has no
Windows or macOS host and there is no CI, so each fix would ship blind. That is the same
cost argument given under [Why this is deferred](#why-this-is-deferred), and for the Windows
source-download items the same reasoning as the `download-sources.ps1` defect above: a blind edit
replaces a call which fails loudly and immediately with whatever else is wrong further down an
unexercised path. The decision was taken explicitly by the maintainer on 2026-09-04: deployment
script defects found by review are recorded here, not fixed from a checkout that cannot run them.

Each entry gives the location, the symptom, the rule it violates, and the one-line fix, so that
whoever runs the next provisioning can apply and validate them together. None of the entries
shifts a line number cited elsewhere in this record, because no script has been edited.

| # | Finding | Location | Symptom | Rule / precedent | Fix when picked up |
|---|---|---|---|---|---|
| 1 | M-11 residual | macOS `build-boost-macos.sh:117`, `build-openssl-macos.sh:328`, `install-openjdk-macos.sh:104`, `install-gradle-macos.sh:99`, `install-json-spirit-macos.sh:97` | `curl -L` without `--fail` exits 0 on an HTTP 4xx/5xx, so `set -e` does not fire and the HTML error page is saved under the archive name; the `[ ! -f "${ARCHIVE}" ]` gate then treats it as a cache hit forever. This is a different class from the truncated-download case described under "What is not reduced": the file is complete, well formed, and wrong | `set -e` cannot catch it | `curl -fL`; fold into scope step 1 (atomic download). Linux `wget` already exits non-zero on HTTP errors |
| 2 | M-12 (1) | `windows/build-openssl-windows.bat:463-468` | `^` line continuation inside a double-quoted `-Command "…"` string (with the quote flag on, `cmd` treats `^` literally and ends the command at the line break, so PowerShell receives `& { ^` and the next two lines run as `cmd` commands); and `PS_SCRIPT_DIR` is `set` at `:463` and read as `%PS_SCRIPT_DIR%` at `:466` inside the same `if (` block, so it expands empty at parse time | root `AGENTS.md`, "Line Continuation in Set Commands" and "Delayed Expansion Inside Control Structures"; `scripts/devenv7/AGENTS.md` names this file under the `!VAR!` rule | single-line `-Command`, `!PS_SCRIPT_DIR!` |
| 3 | M-12 (2) | `windows/build-boost-windows.bat:296-298` | `SCRIPT_DIR` is `set` and read as `%SCRIPT_DIR%` inside the same `if not exist (` block, so `Import-Module '…internal\common.ps1'` resolves relative to the current directory | same delayed-expansion rule | `!SCRIPT_DIR!` |
| 4 | M-12 (3) | `windows/internal/download-sources.ps1:151, 267` | `Copy-DirectoryWithProgress -SourcePath … -DestinationPath …`, but the function (`common.ps1:212-221`) declares `-Source` / `-Destination`; the six other callers (`download-tools.ps1:708, 776, 1042, 1110`, `vs-detector.ps1:341, 361`) use the right names | same family, and the same two functions, as the `-OutputPath` typo recorded above | rename the parameters |
| 5 | M-12 (4) | `windows/build-openssl-windows.bat:465-468` | imports only `download-sources.ps1`, never `common.ps1`, so `Copy-DirectoryWithProgress` (not exported; `common.ps1:568-581`) is unresolved on that path even after #4; `build-boost-windows.bat:298` imports both | — | import `common.ps1` first, as the Boost script does |
| 6 | M-13 | `windows/build-openssl-windows.bat:673-675` | `wmic cpu get NumberOfLogicalProcessors` — `wmic` is deprecated and absent by default on Windows 11 24H2+ and Server 2025; when missing the `for /f` body never runs, `PARALLEL_JOBS` is empty and `jom -j` at `:686` fails. New ARM64 Windows 11 hosts, the primary target, are exactly the machines affected | `NUMBER_OF_PROCESSORS` is a standard environment variable, used nowhere in `scripts/devenv7` yet | `set /a "PARALLEL_JOBS=%NUMBER_OF_PROCESSORS% * 4"` and `HARNESS_JOBS=… * 2`; the variables are set outside any block, so `%VAR%` is correct there |
| 7 | M-14 | `docker/ubuntu/Dockerfile:2, 40`; `docker/rhel/build-notes.txt:12, 18` | `FROM ubuntu:latest` no longer resolves to 24.04. The Linux build scripts derive `OS_TAG` from `VERSION_ID` (`linux/build-gcc-linux.sh:82-84` and four siblings), producing e.g. `dist-devenv7-ub26-…`, which `projects/make/platform.mk:278-293` rejects with `Unsupported Ubuntu Version`; `unminimize` at `:40` is also gone after 24.04. The `ubi9/ubi:latest` and `ubi10/ubi:latest` recipes float the same way (and `platform.mk` maps only `rhel5..rhel8`, so those recipes are ahead of the makefiles regardless) | reproducible builds | `FROM --platform=linux/amd64 ubuntu:24.04@sha256:<digest>` (digest from `docker manifest inspect`; refresh manually); pinned `ubi9/ubi:9.x` tags |
| 8 | M-15 | `macos/build-boost-macos.sh:271` | `architecture=arm` is unconditional while `:49-54` sets `ARCH_FLAGS` per `uname -m`; with `--layout=tagged` an Intel Mac produces `libboost_*-mt-s-a64.a` containing x86_64 code, which `projects/make/3rd/boost/common.mk:38` (`-x64`) cannot link. Fails loudly, at consumer link time | `windows/build-boost-windows.bat:144-156` derives `BOOST_ARCHITECTURE` per architecture | set `BOOST_ARCHITECTURE=arm` / `x86` beside `ARCH_FLAGS` and use it at `:271` |
| 9 | M-16 | `windows/build-openssl-windows.bat:743-749, 760, 827, 850` | a failing `jom test` sets `TESTS_FAILED=1`, prints a warning, and the script still installs, verifies, copies to the dist, archives, and exits 0. Linux and macOS abort under `set -e` (`linux/build-openssl-linux.sh:554`, `macos/build-openssl-macos.sh:417`, "before installing"). No record says warn-only was intended; whether the Windows suite passes today is unknown from this checkout, so this is a policy decision for the provisioning run: if the suite passes, `exit /b 1` on failure and keep `-skip-tests` plus the cross-execution auto-skip; if it does not, understand the failing test first | platform parity | `exit /b 1` at `:745` |
| 10 | M-17 (a) | `windows/internal/common.ps1:548` | `Start-Process -FilePath "tar" -ArgumentList @("-xf", $ArchivePath, "-C", $DestinationPath)` passes the elements unquoted, so a `%USERPROFILE%` containing a space (`C:\Users\First Last`) breaks every non-`.7z.exe` extraction. `download-tools.ps1:888` uses the opposite convention — elements pre-wrapped in literal quotes — which Windows PowerShell 5.1 can double up; the two cannot both be right, and only a Windows host can tell which | — | settle one quoting convention for `Start-Process -ArgumentList` and apply it to both sites |
| 11 | M-17 (b) | `windows/build-openssl-windows.bat:596-597, 620-621` | `--prefix=%OPENSSL_ROOT_PATH%\out` and `--openssldir=…` are unquoted; the default `DIST_ROOT` (`:74`) is under `%USERPROFILE%`, so a profile path with a space splits into extra `Configure` arguments and the failure lands inside `log_bootstrap.log` | the Unix scripts quote (`linux/build-openssl-linux.sh:542-544`) | `--prefix="%OPENSSL_ROOT_PATH%\out"`, likewise `--openssldir` |

The Low and Informational findings of the same review which fall in these scripts (L-23 … L-29, and
the script halves of L-30, I-16 and I-17; assessed in
`notes/plans/issues/pr-review-fable51-residual-findings-status.md`) are recorded here for the same
reason and under the same trigger:

| # | Finding | Location | Symptom | Rule / precedent | Fix when picked up |
|---|---|---|---|---|---|
| 12 | L-23 | `windows/build-env-all-windows.bat:278` | `DIST_ROOT` is set unconditionally, silently discarding the parsed `-dist-root` option | `build-msvc-toolchain.bat:343-346` guards the same assignment with `DIST_ROOT_PROVIDED` | the same guard |
| 13 | L-24 | `macos/build-boost-macos.sh:180, 215` | a sed script is written with `cat >` to the predictable path `/tmp/jamfile_patch.sed` and then executed with `sed -f` (CWE-377): a local user can pre-create the path as a symlink or race its contents to inject Jam code | the JSON-Spirit installers already use `mktemp` + `trap` | `mktemp` + `trap` |
| 14 | L-25 | `docker/ubuntu/docker-install.sh:2`; `rosetta/*/rosetta-binfmt.service`; `rosetta/*/register-rosetta.sh` | `qemu-user-static` + `binfmt-support` register a handler for the same x86-64 ELF magic as Rosetta and the unit orders only after `network.target`, so which interpreter wins after a reboot is registration-order dependent; `build-gcc-linux.sh:122` only checks that the `rosetta` entry exists; `register-rosetta.sh` waits for the Parallels share with no `TimeoutStartSec=` | reproducibility | order the unit after `binfmt-support` (or do not install qemu on a Rosetta host); add a timeout; check which handler is active, not merely registered |
| 15 | L-26 | every `curl -L`, `wget` and `Invoke-WebRequest` download site (the tables above) | HTTPS to HTTP redirects are followed; `aka.ms` and the Adoptium `latest` endpoint are redirectors | supply-chain scope step 1 | `--proto '=https' --proto-redir '=https'` (curl), `--https-only` (wget), and the PowerShell equivalent |
| 16 | L-27 | `linux/check-prerequisites.sh:56` | accepts `rhel|rocky|almalinux|centos`, while every build and install script accepts only `ID` = `ubuntu` or `rhel`, so a Rocky host passes the check and fails at the first build step; `ID_LIKE` is consulted nowhere | consistency | consult `ID_LIKE` in the build scripts as well, or narrow the check |
| 17 | L-28 | `linux/build-boost-linux.sh:342` | Boost is built without `-D_FILE_OFFSET_BITS=64` while the consumers compile with it (`gcc-default.mk`), and `address-model=64` is unconditional; harmless on 64-bit, an `off_t` / `struct stat` ABI mismatch on the x86 path the scripts nominally support | ABI parity | pass the define; derive `address-model` from the architecture |
| 18 | L-29 | `macos/*.sh` | macOS 14 is mapped to `d24`, while `projects/make/platform.mk:180-187` maps Darwin 23 to `d22` / devenv6 only, so a devenv7 dist built on macOS 14 is unusable on the machine which built it | a dist must be consumable where it was built | align the OS tag map with `platform.mk` |
| 19 | L-30 (installer half) | `linux/install-gradle-linux.sh:105-108`, `macos/install-gradle-macos.sh:74-77` versus `windows/internal/download-tools.ps1:737` | the Unix installers create `gradle/latest/default` while the Windows one creates `gradle/<version>/default`; `projects/make/3rd/gradle/latest.mk` now tolerates both by selecting on the presence of the launcher | one layout per tool | pick one layout for all three platforms |
| 20 | I-16 (OpenSSL half) | `linux/build-openssl-linux.sh:515-525` | the clang build of OpenSSL is configured without `-fvisibility=hidden`, unlike the gcc build, so the export surface of anything linking it differs by toolchain | toolchain parity | add the flag to the clang `CFLAGS` |
| 21 | I-17 (script docs) | `windows/build-msvc-toolchain.bat:59-60, 88-89`; `windows/internal/toolchain-setup.ps1:488-490` | the help text still advertises the pre-downgrade Perl default and a cache directory the script no longer uses; the generated `ci-init-env.mk` hard-codes the `/c/Users/` prefix (only `$(USERNAME)` is parameterised), which breaks profiles on another drive or under a domain directory | docs match code; the Unix generators use `${HOME}` | correct the help text; derive the prefix from `%USERPROFILE%` |

### Scope when picked up

1. Apply items 2-6, 9, 10, 11, 12 and 21 on a Windows devenv7 host and run one Boost + OpenSSL
   provisioning for each target architecture; this is the same run the `download-sources.ps1`
   defect above is waiting for, so do them together.
2. Apply items 1, 8, 13, 18 and 19 on a macOS host (item 8 needs an Intel Mac, or at least an
   inspection of the produced library names) and run one Boost provisioning.
3. Apply items 7, 14, 16, 17, 19 and 20 on Linux and rebuild the Ubuntu image; confirm
   `build-env-all.sh` produces a `ub24` dist.
4. Apply item 15 on all three platforms as part of scope step 1 of the checksum work above.
5. Update the line-number tables in this record afterwards.

---

## References

- Finding F-02: `notes/reviews/swblocks-baselib/major/update_2025/v1/pr_review_analysis_gpt56sol.md`
- Download helper with unused verification: `scripts/devenv7/windows/internal/common.ps1`
- Windows tool downloads: `scripts/devenv7/windows/internal/download-tools.ps1`
- Linux downloads: `scripts/devenv7/linux/build-*.sh`, `scripts/devenv7/linux/install-*.sh`
- macOS downloads: `scripts/devenv7/macos/build-*.sh`, `scripts/devenv7/macos/install-*.sh`
- Dist archiving: `scripts/devenv7/{linux,macos,windows}/archive-dists-*`
