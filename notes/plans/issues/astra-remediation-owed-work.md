# What the astra remediation leaves owed

**Date:** 2026-09-23. **Status:** the consolidated list. Items here are **owed, not abandoned** —
each names what picks it up and, where one exists, the condition that unblocks it.

Written because the owed items had scattered across eight records over four days, and a reader
asking "what is left" had no single place to look. Each entry points at the record that holds the
reasoning; none of it is restated here.

---

## Sequenced, and unblocked by something specific

| # | Item | Unblocked by | Where the reasoning lives |
|---|---|---|---|
| 1 | **The uncharged-retire bound** | nothing — in flight | `pool-uncharged-retire-recursion-record.md` |
| 2 | **H04a**, the driver-pointer publication | **item 1 landing** | `s6r3-design.md` §4.1, §12 |
| 3 | **The h1-over-TLS control case** | after S6R.3 | `initiate-close-teardown-design.md` §13 |

**Why H04a waits on the bound, since "deferred" reads as "dropped".** Under today's code a wrong gate
crashes the module with a stack overflow, so the gate cannot be iterated on. Under the bound the
recursion terminates at `maxRetriesPerRequest + 1` and a wrong gate produces a **red assertion**
instead. H04a is an unsynchronised smart-pointer publication and a genuine hazard on a64 — sighted
three times since L5, with a 32-run TSan negative and a recorded reason why that negative is not a
refutation. A tolerable carry for one change-set; not a tolerable carry indefinitely.

**The shape H04a should take** is already specified in §4.1: read the task connection's state **once**
per examine, and make both the poll and the retire decision from that single reading. Both the gate
that crashed and the alternative the design had rejected read it twice.

**And a warning for whoever retries it:** the bound makes H04a safer to attempt and also makes a
wrong gate **quieter**. The crash is what made this defect findable at all. §12's first lesson — read
the gate against the fixture that pins the feature and ask whether it ever opens — is what replaces
the crash as the detector.

## Owed to the Windows matrix, and not closeable here

| # | Item | What settles it |
|---|---|---|
| 4 | **N2's Windows arm** | the matrix; a Linux-only run cannot catch a breach of that rule |
| 5 | **The TLS peer's steps 2 and 3** | run the two retry cases at `--log_level=message` and read the reported code |

Item 5's step 1 landed at `2a4ad5f`. Both cases pass either way, so **the reported code settles it**:
`eof` or `asio.ssl.stream:1` means the 2026-09-21 row was that peer's own reset and the Windows
`connection_reset` arm rests on nothing measured; a persisting `10054` against a now-orderly peer
means it was measured after all.

## Defects found during the remediation, recorded and not fixed

| # | Item | Where |
|---|---|---|
| 6 | **H01's spurious reuse refusal** — `httpclient3` fails ~10% of full-module runs; measured 2/20 with the teardown fix and 3/20 without, so **pre-existing** | teardown design §16.6 |
| 7 | **h1's write path has no peer-close arm** — h2 has one; which handler notices the peer first still decides whether the task fails | teardown design §13 |
| 8 | **A composed TLS read can slip a cancel** the same way a composed write does — pre-existing, neither created nor closed by the teardown fix | teardown design §13 |
| 9 | **The third HPACK hazard** — `encode( )` commits its dynamic-table transaction at queue time, so a dropped block leaves our encoder holding entries the peer never saw | `s6r3-design.md` §3.7 |
| 10 | **A control frame queued after the SETTINGS ACK now leaves before it** — legal, and invisible to the suite | `s6r3-h10-record.md` |
| 11 | **A sixth terminal situation** where `onComplete( )` is not truthful: a clean close with no final header block | `s6r3-design.md` §1.3, as implemented |

## Astra findings deliberately not taken

| # | Item | Why |
|---|---|---|
| 12 | **H11** | closed by the maintainer: leniency is chosen, the justification rewritten, the reopen trigger recorded |
| 13 | **H24, H25** | latent until a content codec ships, and prerequisites of it — now recorded as P2 and P3 in the decoder deferral |
| 14 | **H21, H22** | on L6's owed list, pre-dating astra |

## Not part of this remediation at all

**The embedded decompression work** is parked with its design and plan committed. Decision **E5**
(generated headers in the repo include tree versus the devenv dist) is unmade and blocks its first
layer, and astra's **C01** — the design specifies `inline constexpr`, which is C++17, while baselib
compiles `-std=c++11` — must be resolved before any of it is attempted.
