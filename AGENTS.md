# swblocks-baselib Development Guide

## Rules for Modifying This File

- **State each rule exactly once.** Never duplicate rules across sections.
- **Keep this file under 500 lines.** If it grows beyond, condense or move content.
- **Rules and instructions only.** No reference documentation, implementation walkthroughs, or code snippets longer than 10 lines.
- **Technical reference belongs near the code.** Build system details, platform-specific documentation, and implementation internals go in the AGENTS.md closest to the code they describe (e.g., `scripts/devenv7/AGENTS.md`).
- **Use the Edit tool** (never Write) when modifying this file.
- **Every addition must justify its presence.** When adding content, first consider whether something can be removed.

---

## Core Principles

**Default to research and recommendations over implementation.**

Do not jump into implementation or change files unless clearly instructed to make changes. When the user's intent is ambiguous, default to providing information, doing research, and providing recommendations rather than taking action. Only proceed with edits, modifications, or implementations when the user explicitly requests them.

Do not make any assumptions. Use the AskUserQuestion tool to ask as many follow ups as you need to reach clarity.

**Fold in work that was decided and then blocked, the moment it unblocks — do not ask again.** The
decision was already taken and the blocker was the only thing outstanding. This does **not** extend
to work that was merely recommended, or whose shape is still open: that needs a decision and must be
presented as one. Equally, an item that is the consequence of a decision already taken is **closed
against that decision**, recorded where the decision lives, and never carried as pending work.

**When a decision is needed, present it in this shape**, every time:

- **What it is**, in plain English, assuming no context.
- **What happens if it is not done** — the concrete consequence, not the abstraction.
- **Risk, complexity and blast radius.** Blast radius is what the change can *reach*, not how large
  the diff is; a one-line change on a universal path has a larger radius than a thousand-line one in
  a leaf.
- **The undecided part, named explicitly.** It is usually not *whether* but *which shape*.
- **A recommendation, and the condition that would reverse it.**

Where several decisions are presented together, order them by what to do first and say why.

**Sweep the consolidated owed list at the end of every change-set — not when asked.** Re-read it
against what actually merged and report what moved, in either direction. This project has paid for
three separate failures of that list, and only the first is obvious:

- entries **silently lost** when it was consolidated;
- entries **kept long after the work landed**, so a reader is told that finished work is pending;
- and the expensive one — an item **observed repeatedly and never converted into a decision**. One
  gap here was written into five different records, always as an observation. **When a sweep finds
  the same thing recorded twice, that is the signal to decide it, not to record it a third time.**

Reporting a category is not reporting its contents: *dispositioned*, *deferred with reasoning* and
*done* are three different states, and only the last one means nobody can still hit it.

**Batch what the work finds. Do not schedule a lane per finding.**

Good work finds things, and a review loop that keeps finding them is working rather than failing —
but scheduling each one the moment it appears is what turns a bounded batch into an open-ended one.
Closing a single blind spot in the test-inventory tool became **five** change-sets that way, each
with its own design, implementation, review round and merge.

- Collect what a round turns up and bring it as **one decision round**, ordered by what to do first.
- Group by what a lane would actually touch: several findings in one file or one tool are one
  change-set, not several.
- A finding recorded and deferred is not lost — that is what the owed list and the deferral records
  are for, and the sweep above is what stops it being forgotten.
- **The exception is a live defect that hands a caller a wrong answer.** Those are scheduled on
  sight; everything else waits for the batch.

**Always use the project's Python virtual environment.**

When running Python commands, tests, or scripts, ALWAYS use the Python interpreter from the project's `.venv` virtual environment, i.e. `.venv/bin/python` relative to the repository root (or `.venv/bin/pytest` for pytest). On Windows these are `.venv/Scripts/python.exe` and `.venv/Scripts/pip.exe`. If the `.venv` directory does not exist, run `make pytest-install` to create it before proceeding. On Windows that target fails against the devenv7 dist interpreter, which is an embeddable build with no `venv` or `pip` — see `scripts/devenv7/AGENTS.md` for the procedure to provision a full CPython into a scratch directory.

**Never commit to git without explicit permission.**

Do NOT attempt to commit changes to the git repository using `git add`, `git commit`, or `git push` unless the user explicitly asks you to do so. Changes should remain uncommitted until the user reviews and decides to commit them.

---

## Code Change Guidelines

### Incremental, Intentional Changes Only

**ALL file modifications must be incremental, intentional, and independently reviewable.** Never rewrite entire files or mix unrelated changes.

### File Modification Tools

1. **Edit Tool** — For ALL modifications to existing files (modifying, adding, fixing, refactoring). Shows exact before/after diffs that are reviewable.
2. **Write Tool** — ONLY for creating NEW files that don't exist. **NEVER** use to modify or append to existing files.

### Never Mix Changes

Each PR/commit should contain ONE type of change:

- **Logic changes** (functional): Bug fixes, new features, refactoring, algorithm changes
- **Style changes** (non-functional): Formatting, naming, comments/docstrings, import order

If you're making a logic change, do NOT touch style. If you're making a style change, do NOT touch logic.

### Git Diff Review Checklist

Before making ANY file change, verify:

- [ ] Will the git diff show ONLY the changes I intend to make?
- [ ] Could I explain every line in the diff to a reviewer?
- [ ] Are there any unintended changes (style, formatting, unrelated fixes)?
- [ ] Used Edit tool (not Write) for existing files?
- [ ] No style changes mixed with logic changes?
- [ ] Diff is reviewable (not 1000+ lines)?

**Golden Rule**: Every line in `git diff` should be intentional and explainable. If you can't explain why a line changed, you've made a mistake.

### Building and Testing Changes

**Never do full parallel builds of the entire repo unless explicitly requested.**

- Validate changes with focused builds of the individually affected test modules only.
- When more than one test module (or the entire repo) must be built, **do NOT parallelize the build** — always use `-j1`.
- Tests may be parallelized, up to **5 test modules running concurrently**.

**Build and test the relevant mix of supported toolchains and variants.**

- Linux: `gcc` and `clang`. Windows: `msvc` and `clang`. Select with `TOOLCHAIN=`.
- Both `VARIANT=debug` and `VARIANT=release`.

**Parallel work across worktrees.** When work is split across lane worktrees with one orchestrating
agent merging into the main worktree, the toolchain and variant mix above is **divided between lane
and orchestrator**, never repeated in both:

- A lane worktree validates with **clang debug only**, and builds and runs **only the focused test
  modules its slice affects, one module at a time**. A lane never builds the repo, never compiles two
  modules concurrently, and never builds a second toolchain or variant.
- The orchestrator validates with **clang release and gcc debug**, in the main worktree, after it
  merges a lane's commit. That is where variant and toolchain coverage is earned. Lanes give clang
  debug, so the three together cover both toolchains and both variants — and **gcc release is the
  cell nobody covers**, which is the price of the split and is deliberate.
- These limits are what keep the machine viable. With every lane confined to one focused module at a
  time, no more than about two test modules are ever compiling at once across all worktrees.

**Repetition, not breadth, is what makes a batch slow. Default to 50 runs.**

Focused scope is cheap and is already the rule above; what costs hours is running the same focused
module hundreds of times. One change-set here spent about **700 module runs** and an earlier one
**1370**, because a rate was asked for by reflex rather than because a rate was the question.

- **50 runs** is the default for a regression check, a must-not-move set, or a red/green pair. It
  catches anything real.
- **600 runs** only where **the rate itself is the acceptance criterion** — where the change is
  accepted or rejected on a measured frequency and no deterministic control exists. That has been
  true exactly once.
- A **deterministic** red needs neither: show it red and green once each, and say why it is certain.
- Say which of the three a run count is, and why, whenever one is reported.

### Test Module Size

**Each test module is a single translation unit and must not grow without a bound.** One oversized
module (112.7MB) once made two x86 build combinations impossible to compile at all.

- Every test module prints its headroom as it links, and `make utests-sizes` shows the whole table.
- Add to a module comfortably under the **40MB target**; never to one already at or near it.
- Otherwise create a numbered sibling module (`utf_baselib_messaging2`, `3`, …). This needs no
  makefile change and is the intended answer, not a last resort.
- **75MB per object is a hard ceiling and fails the build** on the platforms where it is enforced.
  Split the module; do not raise the ceiling.

Read `src/utests/AGENTS.md` before adding or splitting a test module — it carries the full rules,
the new-module checklist, and the verification tiers.

### Networking Error Codes Are Not Portable

**Never compare an asio transport error code by hand.** "The peer went away" is one event that
arrives as four different codes depending on platform, I/O model and what the connection was doing.
On Windows a peer close can arrive as `connection_reset` or as `connection_aborted` where POSIX
reports an orderly end of stream — both measured. `connection_aborted` is NOT a plain FIN on a
pending read (Asio maps that to `eof`), so do not reason from the POSIX meaning of either code.

- Ask `net::isPeerClosedErrorCode()` — *is the conversation over?* — or
  `net::isOrderlyPeerCloseErrorCode()` — *did it end cleanly, so is a retry worth it?* They differ
  only on a reset on POSIX, and picking the wrong one reintroduces a real defect.
- If neither fits, add a third predicate **in `core/NetUtils.h`** with its reasoning; do not
  open-code the comparison at the call site.
- `operation_aborted` belongs to neither: it is our own close or an external cancel.
- TLS truncation has its own spelling — ask `STREAM::isStreamTruncationError()` as well.

**A Linux-only run cannot catch a breach of this**, and the abort case is a race, so a Windows run
may need dozens of iterations. Run the Windows matrix for any change to transport error handling,
and suspect this first when a network test fails intermittently there. This has cost the project
three separate defects; see
`notes/plans/issues/windows-peer-close-error-codes-record.md`.

### Monitoring A Long Build Or Test Run

**A running process is not a progressing process. Never report the first as if it were the second.**

Checking `pgrep` and reporting "still running" is not a status. Check that the work is *advancing*:
the build or run log's size and mtime moving against the wall clock, the checkpoint or phase files
appearing, the test-module binary under execution changing. This is the whole check, and it costs
one command.

Two traps that make a stuck job look busy:

1. **`pgrep -f PATTERN` matches the watcher's own command line.** A shell invoked as `bash -c`
   carries the entire script text as its argv, so any pattern drawn from that script — including the
   `pgrep` line itself — matches the watching process and the guard never fires. Write the pattern so
   it cannot match its own literal: `pgrep -f "[g]1-gate.sh"`, not `pgrep -f "g1-gate.sh"`.
2. **A log written only at the end of a phase never moves during it.** Log freshness is the wrong
   progress signal for a phase that buffers or writes on completion; pick a signal that actually
   changes while that phase runs.

Prefer a watcher that reports on a stall — alive but not advancing — over one that only reports
completion. A job that hangs sends no completion notification, which is exactly when a watcher is
needed and exactly when a completion-only watcher is silent.

**Evidence goes to the log directory before it is cited, not after it is challenged.** A measurement
quoted from a session scratchpad cannot be checked by the next reader, and a reviewer is right to
disbelieve one. Write the artifact where it will outlive the session, then cite it. A disagreement
about what was measured is settled in one exchange when the file is durable and the tool's own source
can be pointed at; otherwise it costs a round trip and someone is wrongly corrected.

---

## Configuration File Changes

**Core Principle: If you're uncertain about the impact or correctness of a change, research first. If you're certain, proceed.**

Research official documentation when uncertain about syntax rules, tool behavior, impact of changes, or whether a feature exists.

**Mandatory Rules:**

1. **Never add features not requested** - No "nice to have" additions
2. **Minimal changes only** - Only what's explicitly needed
3. **Single-pass correctness** - If you need multiple fixes, you failed to verify adequately
4. **When in doubt, verify** - Better to take 2 minutes confirming than creating a problem

**Before proposing a configuration change, ask:**

- Am I certain this syntax is correct? (If no → research)
- Is this change actually needed? (If no → don't add it)
- Have I seen this exact pattern work before? (If no and uncertain → verify)

---

## Windows Batch File Rules

### Special Character Escaping

When writing or modifying Windows batch files (`.bat`), special characters **MUST** be escaped with `^` when used literally inside control structures (`if`, `for`, etc.):

- `(` and `)` → `^(` and `^)`
- `<` and `>` → `^<` and `^>`
- `|` → `^|`
- `&` → `^&`
- `%` → `%%` (in batch files)

```batch
REM WRONG:
if condition (
    echo Using compiler (version 16)
)

REM CORRECT:
if condition (
    echo Using compiler ^(version 16^)
)
```

### Line Continuation in Set Commands

Using `^` for line continuation in batch files **breaks quoted string context** in `set` commands. Lines after `^` are interpreted as separate commands, not part of the `set` statement.

```batch
REM WRONG - subsequent lines become separate commands:
set "VAR=%VAR% value1 ^
value2 ^
value3"

REM CORRECT - keep on single line or use multiple set commands:
set "VAR=%VAR% value1 value2 value3"
REM or:
set "VAR=%VAR% value1"
set "VAR=%VAR% value2"
set "VAR=%VAR% value3"
```

### Delayed Expansion Inside Control Structures

**MANDATORY RULE:** When modifying ANY Windows batch file, you MUST use delayed expansion syntax (`!VAR!`) for ALL variable references inside control structures (`if`, `for`). Failure causes silent, hard-to-debug bugs where variables become empty strings with no error messages.

**Why:** When batch parses a control structure, ALL `%VAR%` expansions are evaluated at **parse time** (before the block executes). Variables set inside the block appear **empty** when referenced with `%VAR%`.

Enable delayed expansion near the beginning of the script:

```batch
setlocal enabledelayedexpansion
```

**Decision Matrix:**

| Context | Variable Type | Correct Syntax | Example |
|---------|---------------|----------------|---------|
| Inside `if`/`for` | Set BEFORE block, never changes | `%VAR%` | `%USERPROFILE%`, `%SCRIPT_DIR%` |
| Inside `if`/`for` | Set INSIDE same block | `!VAR!` | `!TARGETS_SPACED!`, `!DIST_FOLDER_NAME!` |
| Inside `if`/`for` | String substitution | `!VAR:old=new!` | `!TARGET_ARCHS:,= !` |
| Inside `for` loop | Changes each iteration | `!VAR!` | `for %%A in (...) do echo !CURR!` |
| Outside control structures | Any variable | `%VAR%` | `%DIST_ROOT%`, `%HOST_ARCH%` |

**When in doubt, use `!VAR!` — it always works inside control structures.**

**Representative example (real bug from this codebase):**

```batch
REM WRONG - produces empty string:
if not "!SKIP_TOOLCHAIN!"=="1" (
    set "TARGETS_SPACED=%TARGET_ARCHS:,= %"          &REM parse-time = empty
    call script.bat -targets %TARGETS_SPACED%         &REM passes empty string
)

REM CORRECT - uses delayed expansion:
if not "!SKIP_TOOLCHAIN!"=="1" (
    set "TARGETS_SPACED=!TARGET_ARCHS:,= !"          &REM execution-time expansion
    call script.bat -targets !TARGETS_SPACED!         &REM passes correct value
)
```

This bug was completely silent — no errors, no warnings, just empty strings passed to commands. It was only discovered when the script produced wrong results.

---

## Build Commands

```bash
# Build debug variant (all targets)
make -k -j4

# Build release variant
make -k -j4 VARIANT=release

# Build specific target
make -k -j1 utf_baselib_jni

# Build specific target, release variant
make -k -j1 utf_baselib_jni VARIANT=release
```

**Cleaning build artifacts** (`make clean` does not work in this project):

```bash
# Linux/macOS
rm -rf ./bld

# Windows
rd /s /q .\bld
```

**Cross-compilation (Windows devenv7+):** Use `ARCH=` parameter (e.g., `make -k -j4 ARCH=x64`). Supported values: `a64` (ARM64), `x64`, `x86`. See `scripts/devenv7/AGENTS.md` for details.

---

## Development Environment Setup

The makefiles automatically configure all compiler paths (PATH, INCLUDE, LIB, LIBPATH) based on the `ARCH` parameter. **Setup scripts are optional** and only needed for:

- Adding MSYS2 tools (make, bash, etc.) to PATH
- Interactive use of compiler tools (cl.exe, link.exe)
- Running test executables manually

See `scripts/devenv7/AGENTS.md` for detailed setup instructions, script variants, and PATH configuration.

---

## Technical Reference

For unit test module rules and layout, see `src/utests/AGENTS.md`:

- Test module size policy and why it exists
- Creating and splitting test modules, and the numbering scheme
- `data/`, `notes.txt` and cross-module include constraints
- The three verification tiers and when each is required
- The x86 release caveat, where object size is not the limit

For detailed build system documentation, see `scripts/devenv7/AGENTS.md`:

- Building the full matrix on Linux, for both a64 and x64, and the scripts that drive it
- Cross-compilation and ARCH parameter internals
- Host architecture detection algorithm
- devenv version gating pattern
- Windows JNI support (signal handling, JVM loading, troubleshooting)
- OpenSSL and Boost build configuration
- Batch file per-file guidance
- Environment setup script variants
- Archive distribution scripts
- Running the Python test suite on Windows
- Linux x64 testing under Rosetta in Docker containers
- ARM64 SVE capability reporting on virtualized aarch64 hosts

---

**Document Version:** 2.13
**Last Updated:** 2026-09-24

**Changelog:**
- v2.13 (2026-09-24): 50 runs is the default and 600 only where the rate itself is the acceptance criterion; and batch what the work finds into one decision round instead of a lane per finding
- v2.12 (2026-09-24): Sweep the owed list every change-set — an item observed twice is a decision waiting, not a note to write again; and evidence goes to the log directory before it is cited
- v2.11 (2026-09-24): Added when to fold work in without asking, when a decision must be presented instead, and the shape to present it in
- v2.10 (2026-09-24): Orchestrator validation is clang release and gcc **debug**, not gcc release — gcc debug is where assertions and the debug standard library actually fire, and the lanes' clang debug already covers that variant on one toolchain only
- v2.9 (2026-09-22): Added Networking Error Codes Are Not Portable — ask net::, never compare transport codes by hand
- v2.8 (2026-09-19): Added Monitoring A Long Build Or Test Run — check progress, not liveness
- v2.7 (2026-09-17): Added the parallel-work-across-worktrees split of the toolchain and variant mix
- v2.6 (2026-09-14): Noted that the test module size ceiling is now enforced by the build
- v2.5 (2026-09-13): Added the Test Module Size rule and referenced src/utests/AGENTS.md
- v2.4 (2026-09-11): Referenced the ARM64 SVE capability reporting guidance under Technical Reference
- v2.3 (2026-09-11): Referenced the Rosetta container testing guidance under Technical Reference
- v2.2 (2026-09-03): Noted the Windows Python provisioning procedure under the venv principle
- v2.1 (2026-09-03): Added build and test rules to Code Change Guidelines
- v2.0 (2026-02-11): Major restructuring — condensed from ~1,700 lines to under 500 lines. Added meta-rules. Moved technical reference to `scripts/devenv7/AGENTS.md`.
- v1.9 (2026-02-07): Added configuration file changes section
- v1.8 (2026-02-06): Added core principles for venv and git
- v1.7 (2026-02-06): Added code review hygiene guidelines
- v1.6 (2026-01-23): Changed host architecture detection
- v1.0-1.5: Initial documentation and incremental additions
