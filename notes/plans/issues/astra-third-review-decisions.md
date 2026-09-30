# Astra's third review — the decisions, and how their change-sets land

**Date:** 2026-09-28. **Status: landed on Linux, 2026-09-28.**
- CS-8 is merged at `28f7026`.
- CS-7 is merged at `605b7d9` and gated green on `083898d` (§3).
- Owed: only CS-7's Windows items, C1 and C2. *(Run 2026-09-29 at `edd921b` and settled - see the end of
  §3.)*

The plan was reviewed once by fable (`plan-fable-r1.md`, agree with changes, all taken). **The three
decisions were taken by the maintainer on 2026-09-28, as recommended:**
- **D1 (b′):** freeze at completion;
- **D2:** a mechanical reconciliation clause on `AGENTS.md`'s sweep bullet;
- **D3:** the size policy names its unit.

CS-8 needed no decision.

**Where the evidence lives.** The records of these runs cite `logs/astra2/…` and `logs/astra3/…`.
That is the orchestrator's evidence directory, `http2-l0-state/logs/`, a sibling of the checkout.
It is kept outside the repository on purpose, as session machinery, and the path is relative to
`http2-l0-state/`.

**The review:** [`http2-l0-l6-third-architecture-security-review-2026-09-28.md`](../http2-l0-l6-third-architecture-security-review-2026-09-28.md),
of `93e2d90`.
- It found no new P1, and no regression introduced by CS-1 to CS-6.
- It found two findings: T01 (P2), which is pre-existing, and T02 (P3), which is recurring.

**The plan:** [`http2-implementation-plan.md`](../http2-implementation-plan.md), "Astra's third
review": change-sets CS-7 (T01) and CS-8 (T02).

**Checked at the source by the orchestrator before planning:**
- **T01.** `applyClosed( )` writes both status fields on a close which lands after completion. The
  getters read them with no lock. `TaskBase::notifyReadyImpl( )` runs the ready callback off the task
  lock.
- **T02.** Every stale statement it lists reads as it says.

## 1. The decisions

### D1 — T01: what `isRetryable( )` and `outcome( )` promise a caller after the request completes

- **What it is.**
  - `HttpClientRequestTaskImpl`'s public getters `isRetryable( )` and `outcome( )` read two plain
    fields.
  - The task can complete before the driver delivers the stream's close: when the request's own
    failure wins — a timeout, a cancel, a cap, a sink or source which threw. Its drain then goes on to
    apply that close, and the close writes both fields.
  - A caller that waits for the task and then reads the getters therefore races that write. It is a
    data race in the C++ sense, even when the value stored is the same.
  - The code's comments say the only safe place to read them is the session's `continuationTask( )`,
    which runs inside the drain that completed the hop. Nothing enforces that.
  - The session's own retry decision reads them there, and is safe.
- **What happens if it is not done.** A direct user of the request task who follows the ordinary
  "wait, then inspect" pattern has undefined behaviour. A ThreadSanitizer build of such an
  application reports it. No wrong answer is shown today: the fields matter to a retry decision only
  when the connection's own close decided the failure, and then the close is what completes the task,
  in the same batch.
- **Risk, complexity, blast radius.**
  - **Reach:** every direct user of the request task over both protocols. The change stays inside
    `HttpClientRequestTask.h`: no `TaskBase`, no stream interface, no pool change.
  - **Five sites write the pair.** `applyAcquired( )` writes it four times, but returns early once the
    task is complete or its completion is pending. So `applyClosed( )` is the only writer after
    completion.
  - **The slot release reads `m_outcome` when it runs.** The late close's verdict reaches the pool
    that way.
  - **The comments promise the same batch only.** A close drained in the same batch behind the task's
    own failure still writes the pair. A close in a later batch is documented nowhere.
  - **The shapes:**
    - **(b′) Freeze at completion — I8's principle applied to the last two fields.**
      - `applyClosed( )` writes the pair only while the task has not completed.
      - It hands its verdict to the slot release as a parameter, so the pool still gets it.
      - No lock and no new field. Same-batch behaviour and every documented promise are kept.
      - A late close no longer changes what the getters report after the request's own failure won.
    - **(a) Live and synchronized.**
      - A leaf lock guards the pair, all five write sites take it, and both getters read under it.
      - The comments say the values can change after completion while the stream is cleaned up.
      - It extends the contract to a later batch's close, which no comment promises today. It is a new
        member, two getters and five guarded writes.
    - **(b) A full completion snapshot.** Separate snapshot fields for the getters, and internal
      fields for the late close. That is the same effect as (b′), with more state.
    - **(c) A continuation-only contract**, documented and enforced. It narrows a public API which
      is used directly.
  - **Not an option: dropping the late `Closed`.** Its slot release and cleanup are needed.
  - **No wrong answer today, verified.** The session refuses a replay on `isOwnFailure( )` before it
    reads the pair (`ClientSession.h:1216`, then `:1223-1225`), and a second close is dropped
    (`HttpClientRequestTask.h:1558`).
- **The undecided part.** Which contract the getters carry: (b′), (a), (b) or (c).
- **Recommendation: (b′), freeze at completion.**
  - It is the smallest change: a guard and a parameter.
  - It keeps every promise the comments make.
  - It is the principle CS-4's I8 already applied to every other field a caller reads.
  - No caller reads the late verdict through the getters, and the pool keeps getting it.
  - An earlier draft recommended (a), on three premises the plan's fable review showed wrong. Its
    "new lock edge" was not new: queue-then-task is the documented order (`TaskBase.h:50-60`). It did
    not keep the documented meaning, but extended it. And it was not the smallest shape.
  - **Reverses to (a)** if the maintainer wants a direct caller to learn, after its own failure won,
    what the connection did afterwards. **To (c)** if direct use of these getters is to be
    withdrawn.
- **Decided by the maintainer, 2026-09-28: (b′), freeze at completion.** It lands in CS-7.

### D2 — the recurring status drift: reconcile the current-status summaries as part of every change-set's records

- **What it is.** T02 is the second time a review has found current-status summaries describing
  landed work as pending: R09 on 2026-09-26, and now T02.
  - The change-set records mark the owed-list rows done.
  - But the design's security considerations and the deferral notes, which a reader takes as the
    current state, are not part of any records step, so they lag.
  - `AGENTS.md` says an item recorded twice is a decision waiting, not a note to write again.
- **What happens if it is not done.** Each review spends effort reconciling the same summaries, and
  a reader is told fixed behaviour is still open — here, that the client still completes a truncated
  TLS body and keeps an unbounded HTTP/1.1 backlog.
- **Risk, complexity, blast radius.** A text rule in the workflow's records step (§6). It adds a
  few minutes to each change-set's records and no code.
- **Evidence that the owed-list sweep alone does not catch it.** The L6 record's status table was
  written by the bucket C sweep on 2026-09-27, and was stale the same day (T02's second list).
- **The undecided part.** Whether to add the rule, and where: as one clause on `AGENTS.md`'s existing
  sweep bullet, or in the workflow's §6 alone.
- **Recommendation: make it mechanical, and put it as one clause on `AGENTS.md`'s sweep bullet.**
  - That bullet already owns this failure mode, but scopes it to the owed list.
  - The clause: *"and every current-status statement about the change-set's items, found by
    searching `notes/plans/` for their identifiers — never from memory. Each hit says what landed and
    where, or is dated history and says so."*
  - The workflow's §6 and §4.6 then refer to it.
  - **Reverses to §6 alone** if `AGENTS.md` is to stay untouched, accepting that a session outside
    the parallel workflow can reintroduce the drift.
- **Decided by the maintainer, 2026-09-28: the clause on `AGENTS.md`'s sweep bullet.** It is
  `AGENTS.md` v2.17. The workflow's §4.6 and §6 refer to it.

### D3 — the size policy names its unit (from CS-7's checkpoint review, O1)

- **What it is.** `src/utests/AGENTS.md` sets a 40 MB target and a 75 MB ceiling without naming the
  unit.
  - The tools count 2^20 bytes: `utf_objsize.py:80`, and the build's size gate.
  - Some records quote 10^6 bytes: the Windows handoff's a64 columns (A4, B14) and
    `utf_baselib_httpclient13`'s `Main.cpp`.
  - It was noticed twice: CS-4's review (F2, "in the house's unit") and CS-7's (F3, O1).
- **What happens if it is not done.** The records keep mixing units. The handoff's x86 estimates
  run about 4.9% high, and a module can read under the target in one record and over it in another.
- **Risk, complexity, blast radius.** One sentence in a rules file. Enforcement does not change,
  because the tools already count 2^20.
- **The undecided part.** Whether the rules file names the unit, or each record states its own.
- **Recommendation:** one sentence in the size policy. It reverses to per-record units if the rules
  file is to stay untouched.
- **Decided by the maintainer, 2026-09-28, as recommended:** *"MB here means 2^20 bytes, as
  `utf_objsize.py` and the build's size gate count them; record sizes in that unit."* It lands in
  CS-7, with the corrected tables.

### T02 itself needs no decision

T02 is a text correction to decisions already taken, so it is folded (`AGENTS.md`): CS-8 corrects
each statement to say what landed, at which commit, and what remains, and keeps dated decisions as
history.

## 2. The change-sets

| Change-set | Finding | Lane | Landed |
|---|---|---|---|
| **CS-7** | T01 | lane 1, `astra3-cs7` | merged at `605b7d9`, gated green |
| **CS-8** | T02, and where the run's evidence lives | lane 2, `astra3-cs8` | merged at `28f7026`; text only, so no gate |

The plan gives each one's files, tests and gate.

## 3. As they land

### CS-8 — T02, the current-status summaries — merged 2026-09-28

**Merged at `28f7026`** from `astra3-cs8`. It is text only: 21 Markdown files under `notes/plans/`, no
gate, and tier 1 unaffected, passing after the merge.

**What it reconciled:**
- **The design:** its security summary (R01, R02, R03, the decoders), and its account of the connect
  loop. That loop is CS-6's D3, owed-list row I16, not asio's ranged connect.
- **The decoder deferral**, the L6 record's status table (rows 12, ThreadSanitizer, E1 to E4), the
  body-sink deferral's E3, the plan's B6 line, and the TaskBase deferral's link 2.
- **The second review's record:** its head, and where the run's evidence lives. The same pointer is
  in five more records which cite `logs/`.
- **Eight older records** whose "nothing implemented" status lines are kept, marked as the status at
  design time, with what landed.
- **The owed list's R09 row.**

**Every cited hash was verified,** to exist and to be an ancestor of its merge. `ConnectionPool.h`'s
stale comment, found by the search, went to CS-7.

**The drift recurred after R09's sweep; the sweep did not miss it.** The lane's `git log -S` showed
each stale statement was written at or after R09's sweep and was true when written. Each went stale as
CS-1 to CS-6 and B6 landed, and no change-set's records reached it. That is the gap D2 now closes.

**Reviewed by an Opus reviewer in two rounds.** Round 1 was READY WITH CHANGES. Its Medium finding was
the R09 row, which predated D2's decision; the lane merged `lazari2` first, then folded every finding.
Round 2 was READY, and its two optional nits were taken.

### CS-7 — T01, the status pair frozen at completion — merged and gated 2026-09-28

**Merged at `605b7d9`** from `astra3-cs7`, to D1 (b′). Tier 1 was re-captured once (`083898d`), with 4
lines, identical to the lane's report: two cases added, and one C6 pair for the one namespace member
they changed.

**The commits:**
- **The fix** — `980295e`. `applyClosed( )` writes `m_isRetryable` and `m_outcome` only while the task
  has not completed, and hands its verdict to `releaseConnectionSlot( )` as a parameter. The pool still
  gets the late verdict, and the late close still releases its slot. A close in the batch of the
  task's own failure still sets the pair.
- **The reds:**
  - `7a664cf`, the contract case, red in 4 of 4 sub-runs on the old code;
  - `634fd4a`, the same-batch case, whose control, the harmonized guard, is red in both sub-runs;
  - the ThreadSanitizer pair, red in 50 of 50 runs, with its positive control, and 0 after.

  ThreadSanitizer does not report every such race: the pair shares one 8-byte word with six other
  flags, ThreadSanitizer keeps four shadow values per word and evicts one when they are full, and one
  report retires the word's reads (LLVM 20.1.0 `tsan_rtl_access.cpp:156-158`, `:186-188`,
  `:227-230`). So the case makes four requests, and every run reports. *(2026-09-29: the measurement
  stands, 50 of 50 and 0 of 50; the explanation is incomplete. The runtime also shares 256 thread
  slots, and a thread which synchronizes with nothing can lose the comparison. Astra's fourth review's
  CS-9 found it; see `src/utests/AGENTS.md`, "A ThreadSanitizer red is a measurement".)*
- **Comments** — `98ad98d` and `3774a97`. The pair is frozen at completion, and reading it after
  completion is safe on any thread.
- **Folded as found:**
  - `0372820`, `ConnectionPool.h`'s comment, line for line and preprocess-identical;
  - `053dac2`, D3's sentence in `src/utests/AGENTS.md`;
  - `47b41d7`, the size records in that unit. `utf_baselib_httpclient` is 35.1 MB (2^20), about 39.6
    to 41 MB on x86: inferred, and at the target, with its reason in its `Main.cpp`;
  - `c326663` and `cc780dd`, the Windows handoff's C1 and C2, and unit notes on its A4 and B14 tables.
    On those tables: B14's upper estimates run about 4.9% high. A4's estimates, and B14's lower ends at
    1.11, are unaffected, because `…7`'s 10^6 a64 figure cancels the unit. This corrects D3's rationale
    above, which is kept as the decision was put.

**The gate: green, 14 of 14, on `083898d`.** It ran clang release and gcc debug over the seven modules
whose objects include `HttpClientRequestTask.h`: `utf_baselib_httpclient`, `…4`, `…5`, `…6`, `…8`, `…9`
and `…10`, as the compiler's `-MM` found them.
- It was checked independently of the gate's summary. Cases entered equal cases left, each run
  printed "No errors detected", and none printed a failure or `leaked`.
- Every module's case count matches the tier-1 manifest.
- `ConnectionPool.h`'s other includers are unaffected: the comment fold leaves their preprocessed
  translation units byte-identical.

**Reviewed by an Opus reviewer in two rounds.** Round 1 was READY WITH CHANGES, with no High or Medium
finding and no decision on the code. Its O1 became D3. Round 2 was READY, and the reviewer confirmed
all three of the lane's corrections to its own round-1 wording.

**Owed:** the Windows matrix's C1 and C2, from a pushed tip containing `605b7d9`. *(Run 2026-09-29 at
`edd921b`, which contains it: C1 - both of T01's cases pass, within `utf_baselib_httpclient` 78 of 78 on
x86 and x64; C2 - `utf_baselib_httpclient` measures 39.1 MB at `win-x86-vc143-debug` and 39.8 at
`ccl16`, against the 39.6 to 41 inferred. The owed list's W10.)*
