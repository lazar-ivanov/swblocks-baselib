# What the astra remediation leaves owed

**Date:** 2026-09-23. **Status:** the consolidated list. Items here are **owed, not abandoned** —
each names what picks it up and, where one exists, the condition that unblocks it.

Written because the owed items had scattered across eight records over four days, and a reader
asking "what is left" had no single place to look. Each entry points at the record that holds the
reasoning; none of it is restated here.

---

## Sequenced, and unblocked by something specific

**ALL THREE LANDED — this section is history, kept for its reasoning.** *Marked 2026-09-24; it had
still read as pending.*

| # | Item | Landed | Where the reasoning lives |
|---|---|---|---|
| 1 | **The uncharged-retire bound** | `24db294`, merged | `pool-uncharged-retire-recursion-record.md` |
| 2 | **H04a**, the driver-pointer publication | `08e18b9`, merged at `4d70076` | `s6r3-design.md` §4.1, §12 |
| 3 | **The h1-over-TLS control case** | `42347a6`, merged at `7e2a1a0`, with the four degradation controls it first shipped without | `initiate-close-teardown-design.md` §13, §17 |

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
| 5a | **`m_wasSocketShutdownForcefully`'s own red** — the one piece of the teardown design's §2.3 a Linux run **provably cannot reach**. Dropping the flag left the task clean in all 35 runs that reached the assertion, because the h1 driver task never performed the handshake (the establisher did), so `isExpectedException( )` takes its `! m_isHandshakeCompleted` arm and `isExpectedSocketException( )` lists `broken_pipe` | **only Windows, or the h2 driver**, which handshakes for itself. Recorded at the point of use in `initiate-close-teardown-design.md` §17 |
| 5b | **A1-cleartext's Windows ordering** — §12.5 called the write's recorded code "redundant and harmless" there; it is redundant and **not** harmless in one ordering, because Windows collapses an orderly close into the reset spellings, so a peer that half-closes and only then aborts would have its FIN-framed message reported as a reset rather than completed | the matrix. **Conservative, not a truncation-as-success.** A one-line narrowing exists and is named in the predicate's comment; whether to take it is the reviewer's and the matrix's |
| 5c | **`utf_baselib_httpclient6`'s x86 debug object.** H22's two cases took it from 39.06 to **42.35 MB on a64 clang debug — 2.4 MB over the 40 MB target**, accepted by the maintainer 2026-09-24 with a recorded reason: every alternative measures worse (`httpclient5` 47.43, `httpclient4` 48.98), and a sibling pays the ~21 MB TU floor again for no change in peak because the weight is the session plus the peer, not the cases. The product fix itself is **+3,584 bytes**; the rest is test weight | **the matrix — but it is NOT the deciding number, and this row first said it was.** *Corrected 2026-09-24 by the H22 implementation review:* the ceiling question is the **family peak**, which is `httpclient4` at 49.0 MB, not `httpclient6` at 42.4 — **if 6 were anywhere near 75 on x86, 4 and 5 would already be over it.** So the choice stands on its own arithmetic and x86 would confirm rather than decide. What is genuinely owed is that **x86 debug is unmeasured and unrecorded in `notes/` for any of modules 4, 5 and 6**, and `x86` `release` is governed by optimizer memory rather than object size, so no figure here speaks to that combination at all |

Item 5's step 1 landed at `2a4ad5f`. Both cases pass either way, so **the reported code settles it**:
`eof` or `asio.ssl.stream:1` means the 2026-09-21 row was that peer's own reset and the Windows
`connection_reset` arm rests on nothing measured; a persisting `10054` against a now-orderly peer
means it was measured after all.

## Defects found during the remediation, recorded and not fixed

**These were sorted by where they were found and not by what they cost, and that is the wrong axis.**
The remediation's scope was astra's 29 findings; everything below was found *by us* while
implementing them, so each was written into the design of the change-set that found it and inherited
that provenance when this list was consolidated. **None of them was ever triaged against an astra
finding.** Where a design says "its own change-set", the deferral was recorded and the successor slot
was never created — that is the gap, and it is what **Status** now makes visible. The column
distinguishes three states that the first version of this table ran together: *unscheduled* (owed a
slot nobody created), *decided* (the reason survives re-reading), and *not a defect*.

| # | Item | Status | Where |
|---|---|---|---|
| 6 | **H01's spurious reuse refusal** — `httpclient3` fails ~10% of full-module runs (2/20 with the teardown fix, 3/20 without, so **pre-existing**), and the `tls-h1-control` lane independently measured **~1 in 8** on TLS | **folded into this batch 2026-09-23**, because it degrades the gate every other change-set is judged against. Redesigned in [`h01-reuse-verdict-design.md`](h01-reuse-verdict-design.md) after the recorded shape failed re-derivation; lands **after A2** (§5.3). Its evidence standard is a **scoped exception**, below | teardown design §16.6 |
| 7 | **h1's write path has no peer-close arm** — h2 has one; which handler notices the peer first still decides whether the task fails | **FIXED and merged** as **A2** (`04bcb6f`), shape (ii): the write declines to fail the task and the still-armed read classifies the ending | teardown design §4.4, §13 |
| 8 | **A composed TLS read can slip a cancel** the same way a composed write does — pre-existing, neither created nor closed by the teardown fix | **FIXED and merged** as **A1-tls** faces 1 and 2 (`db30f53`, `5941a50`). Face 1's gate has a red of its own, which a review had concluded nothing in this tree could give it | teardown design §13 |
| 9 | **The third HPACK hazard** — `encode( )` commits its dynamic-table transaction at queue time, so a dropped block leaves our encoder holding entries the peer never saw | **decided** — the fix is shape **(D)**, which §3.3 rejected for that slice; unreachable today by four properties re-verified at the source on this tip | `s6r3-design.md` §3.3, §3.7; `s6r3-h10-record.md` §5 |
| 10 | **A control frame queued after the SETTINGS ACK now leaves before it** | **not a defect** — nothing in RFC 9113 orders a SETTINGS acknowledgement against a PING acknowledgement. Recorded because it is certain from the code and invisible to the suite, not because it is wrong | `s6r3-h10-record.md` §2 |
| 11 | **A sixth terminal situation** where `onComplete( )` is not truthful: a clean close with no final header block | **deferred with an id** — a terminal callback whatever happened is `onComplete( outcome )`, a `ClientTypes.h` change, deferred as **B4** | `s6r3-design.md` §1.3 |
| 12 | **`TaskBase::scheduleNothrow( )` calls `scheduleTask( )` under the task lock**, so h1's `scheduleRead( )` catch can reach `notifyReady( )` with the task lock held. `m_lock` is a non-recursive `boost::mutex` and `notifyReadyImpl( )` re-acquires it, so this is a **self-deadlock**, not only a breach of `MultiOperationTask.h:62-67` | **scheduled as A4**, driver-local, in `driver-read-write-arms-design.md` §10.2. The **core** question — whether the library itself has a design problem here — is deliberately left open in [`taskbase-schedule-lock-scope-deferral.md`](taskbase-schedule-lock-scope-deferral.md) | teardown design §13 |
| 13 | **h2's `scheduleRead( )` has no accounting guard** — `beginOperation( )` then `async_read_some( )` with no `catch` completing the operation, where h1's read, write and timer each carry one. A throw out of the initiator leaves the count one high and the task unable to take its terminal path | **FIXED and merged** as **A3** (`372a397`, `13aac19`, `0a2a2e3`) — eight sites, not one, via the additive `abandonOperation( )` | teardown design §13 |

**The one scoped exception to the negative-control rule, decided by the maintainer 2026-09-23.**
`src/utests/AGENTS.md` requires that a fix be shown red against the unfixed code and green after, and
says in as many words that *"a negative control is the evidence; the run count is not."* **For item 6
only, a rate-based result is accepted instead.** The reason is not convenience: the design's §4
establishes that this defect's window is internal to our own strand scheduling and is reachable by
**no peer-side lever at all**, which the `tls-h1-control` lane demonstrated by trying — a 32 KB
request body moved the rate from 1-in-8 to 2-in-30 and then stopped moving, where a timing race would
have collapsed. A deterministic control remains preferred if one can be built.

**This is scoped to item 6 and is not a relaxation of the rule.** Any other change-set citing it is
citing it wrongly. The standard the rate must meet, and the residual risk it leaves, are in
`h01-reuse-verdict-design.md` §8 — in particular that the standard distinguishes *eliminated* from
*residual ≥ ~1 %*, so a fix that took 10 % to 0.3 % would likely pass as eliminated, and §4's own
caveat names exactly the mechanism that would produce such a partial result.

### Found by 13a's reading pass, 2026-09-24 — and this one is live

| # | Item | Status |
|---|---|---|
| 13e | **h2 tells the caller "do not retry" for a request the server answered.** In the one window where h2's write arm can fire — no read armed — the peer ends, the **write** handler runs first, `onPeerClosed( )` → `closeAllStreamsUnwrittenRetryable( )` empties `m_streams`/`m_handles` and reports every live stream `connection_aborted`, **non-retryable**. The read handler then arrives carrying the peer's **actual response** (Linux does not purge the receive queue on RST) and feeds it into a session with no streams, so `sinkOf( )` returns nullptr and the answer is dropped | **LIVE TODAY** on `connection_reset`. This is h2's counterpart of the h1 misreport §12.5 closed, by a different mechanism — **a wrong answer to the caller, the worst class on this list.** Fixing item 13a's predicate *alone* **widens** it to `broken_pipe` as well |

**It is closed by the shape, not by the predicate.** h2's `onPeerClosed( )` takes no argument and is
code-free, so the write side contributes **no information** by calling it — whatever it would decide,
the read decides identically one strand turn later. The only thing the call changes is *when*, and
earlier is strictly worse, because it empties the stream table before the answer arrives. So the
write declining and doing nothing closes 13a and 13e together.

### A blind spot in tier 1, found 2026-09-23

**`check_split.sh --tier1` passed over a manifest with no `notes_cases` entry for a case that has a
recipe.** The capture was taken before `notes.txt` was edited, and **C8 checks that every recipe
resolves to a real case — not that every case's recipe was seen.** So the gap is invisible in that
direction, and a green tier 1 is not evidence that a module's recipes are all recorded.

Caught by the lane re-capturing, not by the gate. It is the same lesson as the rest of this batch:
**a gate saying "clean" is a claim about what it looks at.**

**CLOSED 2026-09-24 by C9** on `tier1-c8` — and the rule is not the obvious one. *"Every case must
have a recipe"* is **wrong by a wide margin**: 481 of 1075 cases have none, across 27 of the 46
modules, because `notes.txt` is a curated list of the hard-to-reproduce cases almost everywhere. A
universal rule would have fired on 481 legitimate cases. **But the narrower rule was already written
in the tree** — fifteen `notes.txt` files open with *"each slice appends the recipes for the cases it
lands here"*. *Corrected 2026-09-24: this said those fifteen are **exactly** the modules at 100 %
coverage. They are not — **18** modules are complete and **15** declare it, the undeclared three
being `h2profiles` (14/14), `messaging4` (1/1) and `setprio` (1/1). The property that holds, and the
one C9 actually rests on, is **`declared ⊆ complete`**; the step-1 measurement said 18 all along.*
The convention existed
in the team's own words and the tool had never read it. C9 is therefore **opt-in**, keyed on a claim
a module makes about itself, in three halves: intrinsic, no-loss, and no-withdrawal.

### And the second blind spot is larger than the first — found 2026-09-24, NOT fixed

| # | Item | Status |
|---|---|---|
| C-2 | **`check_against( )` never reads `manifest['modules']` at all.** The entire per-module half of the manifest is captured on every run and **never compared**, so every differential claim tier 1 makes is about `cases` and `members` only. C8's one-wayness was one instance of a structural gap | **CLOSED 2026-09-24** on `tier1-modules`, four commits, one invariant each: **C10** (the `#include` list), **C7's differential half** (data-file content, and a file newly unreferenced), **C6's ADDED direction**, and the refresh docs. Tier 1 is now C1–C10 |

Measured on a tree copy, all four **PASS** tier 1 today: a data file's content changed; **all five**
copies of a shared data file changed identically (ruling out C7 as accidental cover); an `#include`
added to a header holding live cases; a helper member invented.

**The `#include` one is the sharpest.** `utf_inventory.py`'s own docstring claims C2+C3+C4 establish
*"the text of every test, **and the compilation context that text sees**, is unchanged"*. Includes
are part of that context, they are captured per file, and **no check reads them** — the tool's
central claim is stated broader than it holds.

Per invariant: C1 is bidirectional; C2/C3/C4 are symmetric equalities; C5 has no converse;
**C6's no-loss half is one-way exactly as C8 was**; **C7 is intrinsic only and never differenced**,
and its ref→file direction leaves an orphan data file unreported.

#### Review of `tier1-c8` (`16b33f9`) and `tier1-modules` (`ae97a50`), 2026-09-24

Every number below was re-measured, not read from the lane's logs: `--compare` on the current tree
PASSes (46 modules, 1075 cases, 550 members, 182 include lists, 32 data hashes, 4 accepted orphans),
the selftest is red against the tool at `lazari2` on exactly the twelve new mutations and green
against `tier1-modules`, both on the frozen baseline and on a fresh capture. The arming claim holds:
`36ec522`, the tool's first commit, already captured `files[*].includes` and `data_files`, and the
baseline refreshed at `6767d8f` carries 182 of 182 include lists and all 32 hashes. The guards fail
rather than warn, and the selftest exercises both.

**Verdict: accept `tier1-c8`, with one wording correction. Do not accept `tier1-modules` as it
stands.** C7's differential half and C6's ADDED direction are right and were measured silent on
real relocations; **C10 reds on every legitimate relocation**, which is the one thing the lane itself
said this gate cannot afford. Since the four commits are chained, the branch lands after C10 is
narrowed and re-proven; the control that proves it is named below and is already in hand.

**1. C10 fires on the split itself, through the source module's `Main.cpp`.** Every module's
`Main.cpp` is a roster of `#include "Test*.h"` lines, and a relocation *must* edit it. That file is
present on both sides, so C10 judges it. Measured three ways on tree copies, each green at
`16b33f9` and red at `tier1-modules` with the only C10 line on that roster: `TestTasks8.h` moved
whole to a new `utf_baselib_tasks9`, recipes with it (*case 1 of "Reducing a module"*) — one line,
`removed "TestTasks8.h"`; a 143-line helper block cut verbatim from `TestTasks.h` into a sibling
header included from the same `Main.cpp` (*step A of case 2*) — one line, `added "TestTasksCut.h"`;
and **the one real split in history replayed**, `f992e2f`'s pre- and post-trees from `git archive`
— `16b33f9` PASS, `tier1-modules` three lines, one of them the roster. None of the lane's controls
was a relocation: the "file removed" selftest covers the header that vanished, not the roster line
that named it. The scope exclusion is therefore not over-broad but *not broad enough*, and the
principled extension of the lane's own rule is one clause: **a quoted include naming a file this
relocation added to or removed from the same module is not judged, exactly as that file is not.**
That keeps what matters — an `#include <…>` added to `Main.cpp` still fires, and a *reorder* of the
surviving roster still fires as REORDERED, which is worth keeping because include order there is
registration order is run order, and `src/utests/AGENTS.md` records a cold-start contract that run
order can silently neuter. The `f992e2f` replay is the live control for the fix: it should report
exactly the two `C7 NEWLY UNREFERENCED` lines and nothing else. Not implemented here.

**2. The three-halves judgement on C9 stands; "exactly" does not.** Intrinsic covers the declared
modules including a *new* case, no-loss is the only coverage the 31 undeclared modules have,
no-withdrawal is what keeps the intrinsic half armed — none is redundant, and the only overlap is
cosmetic (a recipe deleted in a declared module prints two lines). But *"those fifteen are exactly
the modules at 100 % coverage"* — written in the paragraph above, in `utf_inventory.py:128-129` and
in `16b33f9`'s message — is false, and the lane's own step-1 log says so (*"modules exhaustively
indexed: 18"*). Re-counted independently: **18 modules with cases are complete, 15 declare it**;
`utf_baselib_h2profiles` (14/14), `utf_baselib_messaging4` (1/1) and `utf_baselib_setprio` (1/1)
are complete and undeclared. What makes the rule defensible is **declared ⊆ complete**, which holds,
not equality. `h2profiles` is the one that matters: an h2 module living by the convention without
saying so, where a new case landing unindexed is uncaught. Declaring it is a one-line `notes.txt`
edit under `src/`, owed separately. `AGENTS.md`'s *"Fifteen modules say this today"* is correct as
written.

**3. The orphan rule's premise is wrong for three of its four files, and the rule is still right.**
*"This tree carries such files today and always has"*: `git grep` at `f992e2f^` shows
`utf_baselib_messaging` referencing both `async_rpc_response.json` and
`async_rpc_response_with_exception.json`; that split moved the referencing headers out and left the
copies, and gave `utf_baselib_messaging3` a copy of the second it never names. Only
`utf_baselib_rest/data/async_rpc_response.json` is 2017-era. So three of the four "legitimate"
orphans are **exactly the defect the rule exists to catch, eleven days old, grandfathered.** The
differential shape is still correct — a rule red on the untouched tree is still wrong — but the
replay says two more things: the rule *works* (it names both messaging leftovers), and it **misses
`messaging3`'s, because a module absent from the baseline is skipped entirely** (`was is None →
continue`), a scope the comment does not state. Owed: a `src/` commit deleting the three leftovers,
so the accepted set is honest; and judging a *new* module intrinsically for orphans — nothing to
grandfather there, so no false positive is possible, and it would have caught the third.

**4. The narrowed docstring is still one notch broader than the checks.** *"C10 adds the remaining
part of the compilation context"* — but text at file scope outside any column-0 namespace block is
hashed by nothing: `struct ManifestFixture` (`utf_baselib_loader/TestManifest.h:28`, the fixture of
three `UTF_FIXTURE_TEST_CASE`s), nine column-0 `static` helpers such as
`utf_baselib2/TestTimeZoneData.h:28`, the `BL_IID_DECLARE`s, `UTF_GLOBAL_FIXTURE( LoaderInit )` —
fifteen files carry such lines. Measured: a member injected into `ManifestFixture` and an edit to
`logTestName( )` both PASS tier 1 at `tier1-modules`. Pre-existing, not introduced here, but the
rewritten docstring asserts a completeness it does not have. Minimum: say so in the docstring. The
closing shape is to treat file scope as an implicit block and run `split_members( )` over its residue,
so a fixture that moves with its cases stays a move; whether the member precondition holds at file
scope is unmeasured and is the lane's to decide.

**5. The selftest's orphan silence control is complete and vacuous.** It runs
`check_against( baseline, baseline )`, which is empty for *any* differential definition, so the
copied `unreferenced( )` feeds only the printed count and nothing asserts on it — the control is
subsumed by the *"unmutated baseline is clean both ways"* line above it. The second reason for the
copy (running against the older tool) is real. Minimum: assert the count is nonzero, since that is
the premise that forced the differential shape; a real silence control mutates the *after* side of a
module that carries a baseline orphan and asserts the old orphan is still not reported.

**6. Smaller findings.** C6's identity is text *with leading indentation* (`normalize( )` strips
trailing whitespace only), so a hoist across nesting depth re-indents and reads as LOST + ADDED per
member — the "hoist reads as a move" comment holds for same-depth hoists; a verbatim move measured
clean both for the 143-line block and for a header carrying eleven data files and a helper block, so
this is refresh-workflow noise, not a false positive. All fifteen declarations sit on `#` lines, as
`NOTES_INDEX_RE` requires; the phrase form is a substring match, so a quoting mention would opt a
module *in* — a visible red, not a silent off, and `notes-index: complete` is the robust spelling.
The no-withdrawal half is not in force until the baseline is refreshed, which the tool says on every
run; that refresh is the companion commit owed the moment the branch lands. C5 needs no converse:
it is a uniqueness property of one set, and "at least once" is C1.

**7. The docs describe less than the tool does, and more than C10 should.** The tiers table says
*"helper members neither lost nor duplicated"* — C6's ADDED direction, the whole of `1c7003e`, is not
in it; it says *"no case loses one"* and omits C9's intrinsic and no-withdrawal halves, which only the
checklist carries; and *"every file keeps the `#include` list it had"* overstates C10 twice — files
present on both sides, and, once fixed, minus the roster lines of files the relocation added or
removed. The refresh paragraph is right and was the missing piece; the checklist's C9 item is
accurate. *"Tier 1 is a relocation gate"* is true again only after finding 1 is fixed.

**Could not settle here:** whether the roster exclusion should also silence REORDERED on `Main.cpp`
(argued no, above); the file-scope residue shape; and whether the three split-leftover orphans are
deleted before or after the branch lands — before is cleaner, because the replay control then
reports nothing at all.

### Found by H01's implementation and its review, 2026-09-23

| # | Item | Status |
|---|---|---|
| 6a | **§13.8's write-first half** — `Ready` is still published on a connection whose write the peer reset, on the interleaving where the write handler runs first and the deferral is therefore never taken. H01's outcome term **prevents a regression on the read-first half rather than closing the defect**: there the verdict was `Draining` by accident before, and would have become a spurious `Ready` had the continuation asked only the flag | **unscheduled.** The fix is the same `&& ! m_writeEndingCode` on `finishStream( )`'s own synchronous `isReusable`. **The tree now carries two verdict computations differing by exactly that term**, which is its own argument for closing it. A modification of an existing core path, so its own gated change-set |
| 6b | **`NetUtils.h:401` still carries the withdrawn "the stack has collapsed" premise** — the claim `windows-peer-close-error-codes-record.md` retracted on 2026-09-23 as self-inflicted by `shutdown_both`. `:348` carries it too and **the record lists that one; it does not list `:401`** | **FIXED** (`58aa106`). Both sites now say what is known: the spellings are admitted so a retry is not refused, and whether a clean close ever arrives as one is **not measured** |
| 6c | **`h01-coverage`'s seam is unusable and must not be reused** — line for line the pre-rendezvous hold and release, so it hangs at H01's own rate. Superseded by `TestHttp1DriverStrandSeam.h` on `h01-fix`. **Nothing from that branch should merge**; its 1370-run measurement stands because it ran on `httpclient3`, where there is no seam | recorded; no action beyond not reusing it |
| 6d | **H01's handle-identity guard is implemented and unexercised**, and it decides something the design did not spell out: a `cancel( )` landing in the one-hop window now wins over a completed response, and the sink gets the cancel's code | **owed a case**, and a deterministic recipe exists — a sink calling `cancel( handle )` from inside `onHeaders( )` puts `onCancelStream( )` ahead of the continuation by strand FIFO |

### Found by A2's implementation, 2026-09-23 — and this one is measured

| # | Item | Status |
|---|---|---|
| 7a | **A truncated close-delimited response is reported to the caller as a complete, successful 200.** Measured: a close-delimited body (no `Content-Length`) cut short by a peer RST yields a 16-octet body reported `closed:ok`, no error — **7/8 without A2's arm, 8/8 with it**, so **pre-existing**; A2 does not create it | **SCHEDULED** as **face 3 of A1-cleartext**, `driver-read-write-arms-design.md` §12.5, with its red already in hand (8/8 red today) |

**The mechanism, established by a standalone raw-socket probe and confirmed in the driver.**
`sk_stream_error( )` takes the pending socket error with an **exchange**, so whichever syscall reaches
the reset first consumes `ECONNRESET` and the other gets the leftovers: `send( )` first → the read
sees a plain **`eof`**; `recv( )` first → the write sees **`EPIPE`**. **That is deterministic, not
timing**, and it is why item 7's predicate has to admit both spellings.

On the `send( )`-first ordering the read is handed `eof`, `isCleanEndOfStream( eof )` is true,
`parseEof( )` declares the close-delimited message complete, and the truncation is invisible — while
**the write side held the only evidence that the ending was a reset.**

**A2 removes nothing the caller ever saw** — *corrected 2026-09-23 by A2's implementation review,
which read the request task rather than reasoning from the driver.* `answerOnClosed( )` fails the
request only on the stream's own `event.errorCode`, and the connection task's failure is **chained,
never substituted**. So before A2 the caller already received the truncated body as a complete 200
while the task failed unread. This entry previously said A2 removed an "accidental signal"; that was
wrong — the signal never reached the answer.

**The fix, and why the obvious shape is not enough.** The write's code is the only evidence the
ending was a reset, so the write must record it and the read must consult it — but that alone
**fails**, because the read's `eof` handler can run *before* the write's handler, and a record not
yet written cannot be consulted. §12.5's step 3 supplies the missing half: when the read observes an
end of stream with a live parser **and a write in flight**, it defers delivery to the write's
handler, which `initiateClose( )` guarantees will run. Both are on the strand, so the order the
dispatcher chose stops mattering.

**The rule that makes it decidable, derived from the kernel and verified independently:**
`tcp_reset( )` writes `ECONNRESET` only from `ESTABLISHED` and `EPIPE` from `CLOSE_WAIT`, and
`tcp_fin( )` sets `SOCK_DONE`. So **a write that completed `connection_reset` proves the ending was
a reset with no FIN before it; a write that completed `broken_pipe` is not evidence against the
read's own code.** `net::isPeerClosedErrorCode( )` already asks exactly that of a write's code — no
new comparison is needed, which is what AGENTS.md's networking rule wants.

| 8a | **A1's recorded gate no longer reaches the defect it was specified for.** Today `isClosing( )` is *true* in that read handler because the write's `CHK_EC` set it, so the `! isClosing( )` gate would have closed the close-delimited misreport **by accident**. With A2 landed the write sets nothing, `isClosing( )` is false, and the recorded gate misses it | **A1 must be re-specified before it is implemented** |

### Found by the 2026-09-23 design review of items 7, 8 and 13

| # | Item | Status |
|---|---|---|
| 13a | **h2's `onWrite( )` has the same write-side hole as h1's** — it asks `isPeerClosed( )`, which is the **read-side** pair, so a peer close reaching the write first as `broken_pipe` goes to `CHK_EC` and fails the task. `H2Driver_PeerHalfClosesWithAWriteInFlightTests` does not see it because its peer holds its end and the read notices first; a peer that RSTs would | **FIXED and merged** with **13e** (`bc1f13a`, `09e22d8`) — the shape, not only the predicate |
| 13b | **The TLS composed read's hang face** — a partial TLS record and a silent peer, in *both* drivers | **CLOSED 2026-09-24 — it is a consequence of a decision already taken**, not owed work. `initiate-close-teardown-design.md` §2.4.3 rejected per-operation cancellation on two grounds that survive — it changes how every write is issued, and it introduces a second cancellation mechanism — and that section already records that a `terminal` emit *"would close §13's TLS-read item as well as this defect"*. So this is the cost of that rejection, written where the decision lives. **Revisit only if §2.4.3 is revisited** |
| 13c | **Item 13 undercounts.** h2 has **eight** unguarded `beginOperation( )` sites — read, write pump, five timers and the command-drain post — where h1 guards all three of its own | **fixed** by A3, `372a397`; the count was measured, not taken from the design |
| 13d | **The post-negotiation `scheduleRead( )` escapes to the establisher's plain `BL_TASKS_HANDLER_END( )` and then takes a second terminal from the opening write** | **CLOSED 2026-09-24 — folded into [`taskbase-schedule-lock-scope-deferral.md`](taskbase-schedule-lock-scope-deferral.md) §6.1.** It is an instance of that section's general statement, its fix is that section's to make (the additive *"fail while operations are pending"* primitive), and it changes nothing an L0–L6 caller can observe: bounded to redundant work and possibly a second log line by three verified properties, with the pool seeing one completion. **Not owed work; part of a deferred question** |

**Items 12 and 13 were dropped when this list was first consolidated.** Both are in teardown §13 and
neither reached the first version of this table. They are added here rather than quietly, because a
consolidated list that silently loses entries is worse than the eight scattered records it replaced,
and because this is the second time this feature has lost something by summarising it.

**Owed comment corrections, from §16.9.** `httpclient7/notes.txt`'s stale "known red" block is
**done** — rewritten rather than deleted, per §16.8.1. `TestHttp2DriverWriteBarrier.h` still
contradicts itself on §8.3's wrong lever in four places (`:59-60`, `:82-84`, `:639-641`, and the two
failure messages at `:655` and `:708`) while `BIG_WRITE_THRESHOLD`'s own comment is correct — a
comment pass, still **owed**, and it matters because those are the words a failing run prints.

### Item 6, re-derived 2026-09-23 — the recorded shape is wrong

§16.6's *facts* were re-verified against the tree and all hold: the mechanism, "pre-existing",
"benign", "the safe direction", and that the case is right to be red on it. **Its fix shape does not
hold**, and the reason is that it was derived from `httpclient3` alone.

The shape defers the reuse verdict when the write is still in flight, the connection is otherwise
reusable, and we are not closing. **Those three conjuncts are exactly what
`utf_baselib_httpclient7`'s two barrier cases were built to make true** —
`TestHttp1DriverWriteBarrier.h:216-225` says so in its own words — so the predicate cannot separate
the defect from the cases that forbid the fix, and teardown §8.1 protects those four assertions
explicitly. Both readings break them: leaving the state un-downgraded publishes a connection as
reusable *and immediately submittable* with a write outstanding, re-opening H01's first face; ending
the whole stream hangs both barrier cases at `waitForClosed( )`.

**The only separator is time**, which makes this a design decision and not an implementation detail —
one strand `post` (free, but only fixes the window when the handler is already queued, and the
continuation must be an *accounted* handler or `taskEndedUnaided` goes red), or a short deadline
timer (deterministically testable, but a bounded drain, which `finishStream( )` rejects in
principle). Three further gaps the re-derivation surfaced and §16.6 does not mention: the write's own
failure path would have to widen §4's freshly-gated arm; `chkArmIdleTimer( )` is not idempotent-free;
and three synchronous readers of the verdict sit inside `onClosed`, one of which dispatches a queued
request.

## Astra findings deliberately not taken

| # | Item | Why |
|---|---|---|
| 13f | **H19's actual remedy** — moving the TLS specialization behind a TLS-specific header, so a plain client does not reach OpenSSL at all | **owed, and it was missing from this list until 2026-09-24.** H19 itself is discharged as *recorded and accepted*: the `OPENSSL_VERSION_NUMBER` guard, the comment explaining why it is keyed on the capability rather than `BL_DEVENV_VERSION`, and the consequence written into `httpclient/PreCompiled.h`. But astra's remedy is a real piece of work recorded only in the verification record — **not in the one place a reader is told to look for what is left** |
| 14 | **H11** | closed by the maintainer: leniency is chosen, the justification rewritten, the reopen trigger recorded. **Landed in S6R.4** (`38ed037`), which the plan's slice table did not list |
| 15 | **H24, H25** | latent until a content codec ships, and prerequisites of it — now recorded as P2 and P3 in the decoder deferral |
| 16 | **H21** — successful HTTP/1 selection consumes a retry, so `maxRetriesPerRequest = 0` makes the first request to every HTTP/1.1 origin fail | **already deferred, properly** — it is L6's **finding 4a**, recorded in [`http2-l6-review-record.md`](http2-l6-review-record.md) with its cost, both candidate shapes and what each does to the control case, and its handover condition **met**: `ConnectionPool.h`'s `maxRetriesPerRequest` comment opens with *"AND ZERO SWITCHES OFF HTTP/1.1 THROUGH A SESSION RATHER THAN MERELY TIGHTENING THE BUDGET"*. Needs no new document; this row exists so a reader of one list finds the other |
| 16a | **H22** — cancellation between redirect hops can report the **intermediate** response as a success | on L6's owed list as finding 9. **A wrong answer to the caller**, so the same class as 7a and 13e |

## Not part of this remediation at all

**The embedded decompression work** is parked with its design and plan committed. Decision **E5**
(generated headers in the repo include tree versus the devenv dist) is unmade and blocks its first
layer, and astra's **C01** — the design specifies `inline constexpr`, which is C++17, while baselib
compiles `-std=c++11` — must be resolved before any of it is attempted.
