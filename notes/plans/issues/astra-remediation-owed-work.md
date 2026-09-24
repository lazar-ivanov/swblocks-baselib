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
| 5a | **`m_wasSocketShutdownForcefully`'s own red** — the one piece of the teardown design's §2.3 a Linux run **provably cannot reach**. Dropping the flag left the task clean in all 35 runs that reached the assertion, because the h1 driver task never performed the handshake (the establisher did), so `isExpectedException( )` takes its `! m_isHandshakeCompleted` arm and `isExpectedSocketException( )` lists `broken_pipe` | **only Windows, or the h2 driver**, which handshakes for itself. Recorded at the point of use in `initiate-close-teardown-design.md` §17 |
| 5b | **A1-cleartext's Windows ordering** — §12.5 called the write's recorded code "redundant and harmless" there; it is redundant and **not** harmless in one ordering, because Windows collapses an orderly close into the reset spellings, so a peer that half-closes and only then aborts would have its FIN-framed message reported as a reset rather than completed | the matrix. **Conservative, not a truncation-as-success.** A one-line narrowing exists and is named in the predicate's comment; whether to take it is the reviewer's and the matrix's |

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
| 7 | **h1's write path has no peer-close arm** — h2 has one; which handler notices the peer first still decides whether the task fails | **unscheduled** — §4.4 and §13 both say "its own change-set", and no slot was created | teardown design §4.4, §13 |
| 8 | **A composed TLS read can slip a cancel** the same way a composed write does — pre-existing, neither created nor closed by the teardown fix | **unscheduled** — §13 carries the one-line shape (gate `onPeerClosed( )` on `! isClosing( )`) and says it deserves its own red | teardown design §13 |
| 9 | **The third HPACK hazard** — `encode( )` commits its dynamic-table transaction at queue time, so a dropped block leaves our encoder holding entries the peer never saw | **decided** — the fix is shape **(D)**, which §3.3 rejected for that slice; unreachable today by four properties re-verified at the source on this tip | `s6r3-design.md` §3.3, §3.7; `s6r3-h10-record.md` §5 |
| 10 | **A control frame queued after the SETTINGS ACK now leaves before it** | **not a defect** — nothing in RFC 9113 orders a SETTINGS acknowledgement against a PING acknowledgement. Recorded because it is certain from the code and invisible to the suite, not because it is wrong | `s6r3-h10-record.md` §2 |
| 11 | **A sixth terminal situation** where `onComplete( )` is not truthful: a clean close with no final header block | **deferred with an id** — a terminal callback whatever happened is `onComplete( outcome )`, a `ClientTypes.h` change, deferred as **B4** | `s6r3-design.md` §1.3 |
| 12 | **`TaskBase::scheduleNothrow( )` calls `scheduleTask( )` under the task lock**, so h1's `scheduleRead( )` catch can reach `notifyReady( )` with the task lock held. `m_lock` is a non-recursive `boost::mutex` and `notifyReadyImpl( )` re-acquires it, so this is a **self-deadlock**, not only a breach of `MultiOperationTask.h:62-67` | **scheduled as A4**, driver-local, in `driver-read-write-arms-design.md` §10.2. The **core** question — whether the library itself has a design problem here — is deliberately left open in [`taskbase-schedule-lock-scope-deferral.md`](taskbase-schedule-lock-scope-deferral.md) | teardown design §13 |
| 13 | **h2's `scheduleRead( )` has no accounting guard** — `beginOperation( )` then `async_read_some( )` with no `catch` completing the operation, where h1's read, write and timer each carry one. A throw out of the initiator leaves the count one high and the task unable to take its terminal path | **unscheduled** — pre-existing and narrow | teardown design §13 |

### A blind spot in tier 1, found 2026-09-23

**`check_split.sh --tier1` passed over a manifest with no `notes_cases` entry for a case that has a
recipe.** The capture was taken before `notes.txt` was edited, and **C8 checks that every recipe
resolves to a real case — not that every case's recipe was seen.** So the gap is invisible in that
direction, and a green tier 1 is not evidence that a module's recipes are all recorded.

Caught by the lane re-capturing, not by the gate. It is the same lesson as the rest of this batch:
**a gate saying "clean" is a claim about what it looks at.** Worth a C9, or widening C8, before the
next reader trusts tier 1 for this.

### Found by H01's implementation and its review, 2026-09-23

| # | Item | Status |
|---|---|---|
| 6a | **§13.8's write-first half** — `Ready` is still published on a connection whose write the peer reset, on the interleaving where the write handler runs first and the deferral is therefore never taken. H01's outcome term **prevents a regression on the read-first half rather than closing the defect**: there the verdict was `Draining` by accident before, and would have become a spurious `Ready` had the continuation asked only the flag | **unscheduled.** The fix is the same `&& ! m_writeEndingCode` on `finishStream( )`'s own synchronous `isReusable`. **The tree now carries two verdict computations differing by exactly that term**, which is its own argument for closing it. A modification of an existing core path, so its own gated change-set |
| 6b | **`NetUtils.h:401` still carries the withdrawn "the stack has collapsed" premise** — the claim `windows-peer-close-error-codes-record.md` retracted on 2026-09-23 as self-inflicted by `shutdown_both`. `:348` carries it too and **the record lists that one; it does not list `:401`** | **unscheduled, and the record itself is incomplete.** This is the premise that propagated into a new finding once already |
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
| 13a | **h2's `onWrite( )` has the same write-side hole as h1's** — it asks `isPeerClosed( )`, which is the **read-side** pair, so a peer close reaching the write first as `broken_pipe` goes to `CHK_EC` and fails the task. `H2Driver_PeerHalfClosesWithAWriteInFlightTests` does not see it because its peer holds its end and the read notices first; a peer that RSTs would | **unscheduled**; rides with whatever fixes h1's #7 |
| 13b | **The TLS composed read's hang face** — a partial TLS record and a silent peer, in *both* drivers. The `! isClosing( )` gate proposed for item 8 does not touch it; only the per-operation cancellation that teardown §2.4.3 deferred closes it | **unscheduled**, and larger than item 8 |
| 13c | **Item 13 undercounts.** h2 has **eight** unguarded `beginOperation( )` sites — read, write pump, five timers and the command-drain post — where h1 guards all three of its own | **fixed** by A3, `372a397`; the count was measured, not taken from the design |
| 13d | **The post-negotiation `scheduleRead( )` escapes to the establisher's plain `BL_TASKS_HANDLER_END( )` and then takes a second terminal from the opening write.** Distinct from 13c and **not closed by A3**: closing it needs the inline-completing catch at that one site, which is a self-deadlock at four of the other seven, so it would mean two guard shapes in one driver. h2's `onTaskStoppedNothrow( )` comment at `:2685-2687` already names the route | **unscheduled.** Derived by reading, **not observed** |

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
| 14 | **H11** | closed by the maintainer: leniency is chosen, the justification rewritten, the reopen trigger recorded |
| 15 | **H24, H25** | latent until a content codec ships, and prerequisites of it — now recorded as P2 and P3 in the decoder deferral |
| 16 | **H21, H22** | on L6's owed list, pre-dating astra |

## Not part of this remediation at all

**The embedded decompression work** is parked with its design and plan committed. Decision **E5**
(generated headers in the repo include tree versus the devenv dist) is unmade and blocks its first
layer, and astra's **C01** — the design specifies `inline constexpr`, which is C++17, while baselib
compiles `-std=c++11` — must be resolved before any of it is attempted.
