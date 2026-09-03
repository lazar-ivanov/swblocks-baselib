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

**Always use the project's Python virtual environment.**

When running Python commands, tests, or scripts, ALWAYS use the Python interpreter from the project's `.venv` virtual environment, i.e. `.venv/bin/python` relative to the repository root (or `.venv/bin/pytest` for pytest). On Windows these are `.venv/Scripts/python.exe` and `.venv/Scripts/pip.exe`. If the `.venv` directory does not exist, run `make pytest-install` to create it before proceeding.

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

For detailed build system documentation, see `scripts/devenv7/AGENTS.md`:

- Cross-compilation and ARCH parameter internals
- Host architecture detection algorithm
- devenv version gating pattern
- Windows JNI support (signal handling, JVM loading, troubleshooting)
- OpenSSL and Boost build configuration
- Batch file per-file guidance
- Environment setup script variants
- Archive distribution scripts

---

**Document Version:** 2.1
**Last Updated:** 2026-09-03

**Changelog:**
- v2.1 (2026-09-03): Added build and test rules to Code Change Guidelines
- v2.0 (2026-02-11): Major restructuring — condensed from ~1,700 lines to under 500 lines. Added meta-rules. Moved technical reference to `scripts/devenv7/AGENTS.md`.
- v1.9 (2026-02-07): Added configuration file changes section
- v1.8 (2026-02-06): Added core principles for venv and git
- v1.7 (2026-02-06): Added code review hygiene guidelines
- v1.6 (2026-01-23): Changed host architecture detection
- v1.0-1.5: Initial documentation and incremental additions
