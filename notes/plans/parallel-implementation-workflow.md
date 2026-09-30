# Parallel implementation across worktrees — the workflow

**Status:** the procedure, 2026-09-27. Written for reuse by any orchestrating agent. It was first
set down for the three change-sets of astra's second review
([`issues/astra-second-review-decisions.md`](issues/astra-second-review-decisions.md)), and is
distilled from the HTTP/2 client's L0–L6 lane runs of 2026-09-17 to 09-25, which paid for most of
its rules.

**What it is for.** Implementing several **already decided** change-sets at once, each in its own
git worktree, each reviewed until an independent reviewer and the orchestrator agree, each gated
before it is called ready. It is not a way to take decisions: a change-set enters this workflow with
its shape decided by the maintainer and recorded. What the work finds on the way is folded into the
open change-set, and anything that needs a decision is put to the maintainer as soon as it is clear,
per the root `AGENTS.md`.

**Binding alongside it:** the root [`AGENTS.md`](../../AGENTS.md) — "Parallel work across worktrees",
the build and repetition rules, "Monitoring A Long Build Or Test Run" — and
[`src/utests/AGENTS.md`](../../src/utests/AGENTS.md). Where this document and those disagree, they
win; fix this one.

---

## 1. Roles

| Role | Who | Does | Never does |
|---|---|---|---|
| **Maintainer** | the human | takes decisions, pushes | — |
| **Orchestrator** | the main session, in the main worktree on the integration branch | plans the lanes, writes the briefs, runs the reviews, merges, gates, keeps the records and the ledger | writes production code; pushes |
| **Lane** | one `opus-lane` agent per change-set, in its own worktree | implements its change-set, validates it at clang debug, commits on its branch, keeps a journal | merges, rebases, pushes, stashes; touches another lane's files or worktree; builds a second toolchain or variant |
| **Reviewer** | an `opus-reviewer` agent | reviews a design note or a checkpoint, independently, and argues its findings with the orchestrator until they agree | edits code; commits; runs builds, unless the delegation says so |

Both agent types run the **Opus model at maximum reasoning effort**. Their definitions are in
Appendix A. *Changed 2026-09-27 by the maintainer: reviews had run on the Fable model
(`fable-reviewer`). Fable finished the one it had in hand, CS-6's I2 design note, and every review
task after it runs on Opus.*

## 2. Setting up

1. **The change-sets.** Group decided work by the files a lane would touch, per `AGENTS.md`'s
   folding rule: several items in one file are one change-set. **Change-sets which touch
   disjoint files run in parallel; overlapping ones are sequenced.** Order them by what should land
   first, and say why.
2. **Agent definitions.** `~/.claude/agents/opus-lane.md` and `~/.claude/agents/opus-reviewer.md`,
   verbatim from Appendix A.
   - **Effort comes only from a definition.** The Agent tool's `model` override does not set it, so a
     `general-purpose` agent with a model override runs at that type's default effort.
   - **A definition written mid-session may not be launchable at once.** Two cases were seen on
     2026-09-27:
     - a new `~/.claude/agents/` directory stayed unlaunchable (*"Agent type … not found"*) until a
       restart;
     - a new file in the existing directory failed a probe immediately after it was written, and
       became available later in the same session. The harness announced it.
     
     Prove each type launches before relying on it, and restart only if it never appears.
   - **Until it appears**, a review may run on `opus-lane`, which has the same model and the same
     effort, with a delegation that makes it a read-only reviewer. Never run one on a type without
     `effort: max`.
   - The repository's `.claude/` is gitignored, which is why the definitions live in the home
     directory and why Appendix A carries them.
3. **Worktrees.** One per lane, as siblings of the main worktree, each on a **fresh branch from the
   integration tip**. **Before reusing a worktree, look at it**: `git status`, and whether its HEAD
   is merged. Uncommitted work found there is never discarded unseen — on 2026-09-27 a lane worktree
   held a 179-line review written three days earlier and never committed, which nothing else in the
   tree mentioned. Rescue it into the records, prove every line arrived, keep a copy of the diff in
   the log directory, and only then clean the worktree.
   **A new worktree also needs the git-ignored `projects/make/ci-init-env.mk`** copied from the main
   worktree, or every `make` stops at `common.mk:8`. A lane worktree made on 2026-09-30 lacked it.
4. **The state directory**, outside the repository, as `AGENTS.md` and the session-machinery rule
   require: the runbook (`RESUME.md`), the ledger (`LEDGER.md`), the briefs, the lane journals, the
   review files, and `logs/`. Nothing in it is ever committed.
5. **The build semaphore.** Every `make` and every test binary, in every worktree, runs through one
   machine-wide semaphore that caps concurrent builds and refuses to start below a disk floor. On
   the 2-core, 7 GB host this was written on, that is two slots and 4 GB: **agents think and edit in
   parallel; compiles do not.** Size the slot count to the machine, not to the lane count.
6. **Reserve test module names per lane** before anyone adds a case. Two lanes creating the same
   numbered sibling is a collision no merge resolves cleanly. Check the numbering first — a module
   `utf_…7` may already exist.

## 3. The brief

One work order per lane, in the state directory. It is what a lane restarted cold resumes from, so
it must stand alone:

- the worktree, the branch, and the attribution line for commits;
- the decisions it implements, by reference to the decision record, with the sections to read;
- **the files it owns**, and the files it must not touch;
- which decisions need a **design note first** (§4.1), and what the lane does while it is reviewed;
- the test modules it may use, **including reserved new module names**, and the headroom rule;
- the acceptance list, one checkbox per deliverable, each with its evidence;
- the semaphore command, the log directory, and the journal path;
- when to stop and report instead of improvising.

A template is Appendix B.

## 4. The loop, per change-set

### 4.1 Design notes, for decisions with real design content

A decision whose shape is decided but whose mechanism is not — a new handler, a new cap, a changed
protocol between components — gets a **short design note first**, committed on the lane branch. The
lane writes it and then works on its other decisions while it is reviewed. A decision that is a
one-line predicate or a text correction needs none.

The note is reviewed exactly like a checkpoint (§4.3). **Agreement is a dated line in the note**,
and only then is the decision coded.

### 4.2 Implementation, in the lane

- **Commit after every meaningful step**, staging exact paths. The lane branch is the durable record
  of progress: a lane restarted cold reads `git log --oneline <integration>..<branch>`.
- **One kind of change per commit**: comment-only changes never ride in a logic commit.
- **Characterize before you change**, and show every fix **red against the unfixed code and green
  after**, deterministically — a barrier, a rendezvous, a pure input. A run count is not a control.
- **clang debug only, `-j1`, one focused module at a time, through the semaphore.** Variant and
  toolchain coverage is the gate's.
- **Test text changes tier 1.** The lane runs `utf_inventory.py --compare` and explains every line of
  its report in the journal. It does **not** commit a baseline refresh: parallel refreshes conflict,
  and a conflicted `inventory.json` is resolved only by re-capturing from the integrated tree.
- **Stop and report** when the brief conflicts with the code or the rules, a decided shape turns out
  not to hold, a file outside the lane's ownership needs to change, or a failure has no explanation.
  An agreed shape that no longer holds goes back to the maintainer, not around them.

### 4.3 The checkpoint review

When the lane reports its change-set implemented:

1. **The orchestrator reviews first**, at the source: the diff, the tests, the red and green logs,
   the journal. A report is a claim until its evidence is read.
2. **The reviewer reviews independently** (`opus-reviewer`, maximum effort): the diff against the
   decision record, every claim checked at its source, findings labelled VERIFIED / INFERRED / NOT
   VERIFIED with severities, written to a review file in the state directory.
3. **Back and forth until both agree.** The orchestrator answers each finding — accepted, rejected
   with evidence, or refined — and the reviewer answers the answers, continued through `SendMessage`
   so it keeps its context. Neither side's word settles a point; the source does. A disagreement
   that the source cannot settle is a decision, and goes to the maintainer.
4. **The lane implements the agreed findings**, with evidence, and reports again.
5. **Repeat until the review has no open finding.** Record the rounds and what each changed. A review
   loop that keeps finding things is working, not failing. What it finds beyond the change-set's
   original scope is folded into the change-set, with the lane's ownership widened. Anything that
   needs a decision is put to the maintainer as soon as the decision is clear, before the lane
   reaches it, never held for the end. A decision the implementation later undermines is put again
   (`AGENTS.md`, "Fold what the implementation finds").

### 4.4 Integration

The orchestrator merges the lane branch into the integration branch with a merge commit, in the
main worktree. Lanes never merge. If the change-set touched test text, **re-capture tier 1 once
from the integrated tree**, in a commit of its own, having read the manifest diff line by line —
refreshing to silence a report you cannot explain makes the gate worthless.

### 4.5 The gate

**clang release and gcc debug, `-j1`, through the semaphore, over every affected module**:

- **Affected** means every test module whose object depends on a changed header — read from the
  compiler's own dependency files under `bld/<platform>/utests/*/` — plus every new module. A header
  included everywhere reaches everything: keep a comment-only edit to such a header line for line,
  so that no object changes, or accept a whole-suite gate.
- **Each module is run once and read, not grepped**: it entered its cases, exited 0, printed no
  failure and no `leaked` line. Never read an exit code through a pipe.
- **A red gate is fixed forward in the lane** — reviewed again if the fix is not trivial — and the
  gate re-run over what the fix touched.
- The gate says nothing about platforms it did not run. A change to transport error handling owes
  the **Windows matrix** (`AGENTS.md`), which is handed to the Windows agent through the handoff
  document; other target gaps are recorded as owed, never assumed.

### 4.6 Ready

A change-set is **ready** when its reviews have no open finding, its gate is green, and its records
say so: the decision record marks it done with its commits, the owed list is swept (`AGENTS.md`:
at the end of every change-set, in both directions), and every current-status statement about its
items is reconciled (`AGENTS.md`, the same sweep). **Nothing is pushed** — the maintainer pushes.
Owed platform runs are recorded against it, not silently waited on.

## 5. Running it

- **Monitor the progress, not the liveness, of every agent you launched** (`AGENTS.md`). A running
  process proves nothing.
  - **What progress looks like:**
    - A lane is advancing when its branch gains commits, its journal grows, or its build tree gains
      objects.
    - A reviewer writes its review only at the end, so it is advancing when its transcript grows.
      The transcript is the agent's task output file, a symlink: read it with `stat -L`.
  - **The watch, from the moment an agent starts:**
    - it fires on a stall, 15 minutes without progress;
    - it wakes the orchestrator every 30 minutes for a status line per agent, which goes in the ledger.
  - **On a stall:** look at the agent's artifacts first, then ask it for its status. A stall it does
    not answer goes to the maintainer.
  - **An agent idle by design** comes off the watch until it is given work again.
  - On this host `find` is `bfs`: use `-mmin`, not a relative `-newermt`.
- **Parallelism is bounded by the machine, not the lane count.** Keep every lane doing something
  that does not need a build slot — a design note, a review answer, a journal — while another holds
  one.
- **Disk is a gate of its own.** Measure free space before starting; the semaphore refuses to build
  below its floor, and a stalled lane then looks exactly like a slow one.
- **Evidence goes to the log directory before it is cited** — every red, every green, every probe.
- **A model change mid-run stops the run** until the maintainer confirms it.

## 6. Records

- **One decision record per round**, with each decision as put and as taken; each change-set's
  section gains its commits, review rounds and gate result when it is ready.
- **The consolidated owed list is swept at the end of every change-set**, in both directions, and
  work that landed is marked. What the work found is never added to it: that was folded or decided
  during the run. The list holds only the three kinds `AGENTS.md` allows — what the maintainer
  deferred, what needs another host, and gated core work — each with its reason.
- **The current-status statements are reconciled in the same records commit**, as `AGENTS.md`'s
  sweep requires. Search `notes/plans/` for the items' identifiers, and never work from memory: the
  design's security considerations, the deferral notes and the review records all speak for the
  current state.
- **Corrections are dated and made at the claim**, with the original left legible.

## 7. Restarting

- **A lane restarted cold:** read the runbook, its brief and its journal; then `git status` and
  `git log --oneline <integration>..<branch>` in its worktree. Committed work is done; uncommitted
  work is finished or reset; resume at the first unchecked box of the brief.
- **The orchestrator restarted cold:** read the runbook and the ledger; for each lane, the branch
  shows what is ready and the ledger what has been merged and gated; resume at the first row that
  is not terminal.

## 8. What this procedure was paid for

- **Stubs agreeing with a contract is not components agreeing with each other** — compose real
  components early; a whole layer's defects were found by the first composed case.
- **The instrument can be wrong** — the gate tool was wrong three times in one layer, each defect
  hiding the next. Never let one instrument be the only witness; read the raw artifacts.
- **A right conclusion on a wrong premise** survives plausibility review and dies only when each
  claim is checked at its source — including the orchestrator's own claims to the maintainer.
- **Comments outlive the fix** — "today nothing does X" becomes false when X lands.
- **Work can sit uncommitted in a worktree for days** and appear in no record.
- **An agent type can be defined and still not launchable** in the session that defined it.

---

## Appendix A — the agent definitions, verbatim

`~/.claude/agents/opus-lane.md`:

````markdown
---
name: opus-lane
description: Implementer for one lane of a parallel change-set, in its own git worktree, on the Opus model at maximum reasoning effort. Use when the orchestrator assigns a change-set to a lane worktree.
model: opus
effort: max
---

You implement ONE change-set in ONE lane worktree. The orchestrator's brief names the worktree, the
branch, the decisions you implement, the files you own, the test module names reserved for you, and
the exact acceptance list. Follow it exactly; when it is silent, the rules below apply.

Before anything else:

- Read the repository's `AGENTS.md`, `src/utests/AGENTS.md`, and every `AGENTS.md` nearer the files
  you will touch. They are binding. Read the decision record the brief names in full.
- Work ONLY inside your worktree, with absolute paths. Never touch another lane's worktree or the
  main worktree, and never touch files another lane owns.

Commits and branches:

- Commit on your lane branch only, after every meaningful step, staging exact paths (never
  `git add -A`). Never merge, rebase, push, stash, reset shared history or switch branches.
- One kind of change per commit: comment-only changes never ride in a logic commit.
- End every commit message with the attribution line the brief gives you.

Building and testing:

- clang debug only (`TOOLCHAIN=clang2010 VARIANT=debug`), `-j1`, only the focused modules your
  change affects, one module at a time, and every `make` and every test binary run through the
  machine's build semaphore named in the brief. Never build the repo, a second toolchain or variant.
- Characterize before you change where behaviour can be observed first. A fix is shown red against
  the unfixed code and green after, deterministically - a barrier, a rendezvous or a pure input -
  never by a run count or a sleep.
- A test run is green only when it entered its cases, exited 0, printed no failure, and printed no
  `leaked` line. Read the run's own output; never read an exit code through a pipe.
- Before adding a case, check the module's headroom (`src/utests/AGENTS.md`); use only the module
  names the brief reserves for you. A test-text change needs tier 1 run and its report explained;
  the orchestrator re-captures the baseline after integration.
- Evidence you cite goes to the log directory the brief names before you cite it.

How to work:

- Check every claim at its source; read the whole function before asserting anything about it. A
  right conclusion on a wrong premise is a defect - name the premise.
- Keep a short journal in the file the brief names: what you did, what you measured, what surprised
  you, what is still open. It is how a restarted lane resumes.
- Stop and report, rather than improvise, when: the brief conflicts with the code or the rules, a
  decision's shape turns out not to hold, you would need to touch a file you do not own, or a build
  or test fails for a reason you cannot explain.

Finish with a concise report: commits (hash and subject), what each proves and how it was checked,
anything left open, and anything the orchestrator must decide.
````

`~/.claude/agents/opus-reviewer.md` — it replaced `fable-reviewer.md`, whose body it keeps; only the
model, the name and the fold rule's line differ:

````markdown
---
name: opus-reviewer
description: Independent reviewer of designs, plans and code on the Opus model at maximum reasoning effort. Use for every review task in the parallel implementation workflow - design notes and checkpoint reviews.
model: opus
effort: max
---

You are an independent technical reviewer. The person who delegates to you says what to review,
what you may change, and where your output goes; follow those boundaries exactly. When the
delegation does not say, you are read-only: do not edit, build, run tests, or run `git add`,
`git commit`, `git push` or `git stash`.

How to review:

- Read the project's `AGENTS.md` first, and any `AGENTS.md` nearer the files under review.
- Check every claim at its source. Read the whole unit - function, section, file - before asserting
  anything about it. A document's summary of a fact is not the fact.
- Challenge the author's claims as hard as anyone else's. A right conclusion drawn from a wrong
  premise is still a defect: name the premise.
- Label everything VERIFIED (read or measured at the source you name), INFERRED (follows from verified
  facts and named rules, but not checked), or NOT VERIFIED.
- Keep findings (defects in a claim, with evidence) separate from proposals (your judgement).
- When you correct a claim, quote what it said and say why it is wrong.
- Where wording should change, write the wording itself, keyed to file and line.
- Give a severity to each finding and a reversing condition to each recommendation.
- What you find that belongs in the change-set under review is folded into it, not deferred: say so
  (`AGENTS.md`, "Fold what the implementation finds").

Finish with a concise summary - verdicts and top findings in severity order - and the paths of any
files you wrote.
````

## Appendix B — a brief, as a template

```markdown
# <CS-n> — <what it changes>

Worktree <path>, branch <name> from <integration tip>. Attribution line: <line>.
Semaphore: <command>. Logs: <dir>. Journal: <path>.

## Decisions
<Dn> — <one line>, decided <date>: <decision record, section>. Design note first: yes / no.

## Files
Owned: <paths>; new test files in the modules below, one `#include` line each in their `…Main.cpp`,
and one `notes.txt` recipe line per case added — C9 requires it in a module whose index declares
itself complete. Not to be touched: <paths owned by other lanes>.

## Tests
Modules: <existing, if the measured headroom allows> or <reserved new names>. Headroom rule:
src/utests/AGENTS.md. Every fix red before, green after, deterministically.

## Acceptance
- [ ] <deliverable> — evidence: <what proves it>
- [ ] tier 1 run and every line of its report explained in the journal
- [ ] the focused modules green at clang debug, logs in <dir>

## Stop and report when
the brief conflicts with the code; a decided shape does not hold; a file outside "Owned" must
change; a failure has no explanation.
```
