# The x86 clang-cl Host Swap and Oversized Test Translation Units: Deferral Record

This document records why the x86 `ccl16` build no longer uses the 32-bit clang-cl host, what was
changed on 2026-09-10 to get x86 building again, and the two things that were **not** fixed: the test
modules are single translation units large enough to exhaust a 32-bit compiler, and the debug info
trimmed in February 2026 to work around an earlier instance of the same problem is still trimmed.

**It is a risk acceptance, not an assessment that the concern is absent.** The underlying problem —
test translation units that grow without a bound — is real, is still in the tree, and the applied fix
buys headroom rather than removing the cause.

**Origin:** the full 12-combo Windows build/test matrix run on 2026-09-10 (`ARCH` a64/x64/x86 ×
`TOOLCHAIN` vc143/ccl16 × `VARIANT` debug/release). Both x86 `ccl16` combos failed to build; the other
ten combos passed. Not from a code review.

**Platform:** Windows, `ARCH=x86`, `TOOLCHAIN=ccl16` only. `vc143` on x86 is unaffected — MSVC's
`cl.exe` for an x86 target is selected by `MSVCHOSTARCHTAG` and is already a 64-bit host binary.

---

## Decision

**Date:** 2026-09-10
**Status:** **Applied fix recorded; two follow-ups deferred.** The follow-ups are real work items, not
risk acceptances that should be closed unread — but neither blocks anything today.

| # | Item | Disposition |
|---|---|---|
| 1 | x86 `ccl16` selects a 64-bit clang-cl host with an explicit `--target=i686-pc-windows-msvc` | **Applied** 2026-09-10 |
| 2 | `PassThroughOptionParser` dropped the value of a joined `--opt=value` | **Applied** 2026-09-10 — prerequisite for item 1 |
| 3 | Split the oversized test translation units so a 32-bit compiler can build them again | **Deferred** — this record |
| 4 | Restore full `-Zi` debug info for x86 `ccl16` release once item 3 lands | **Deferred** — blocked on item 3 |
| 5 | Restore the 32-bit clang-cl host for x86 targets once item 3 lands | **Deferred** — optional even then; see "Conditions to revisit" |

---

## The limitation

`clang-cl` 16.0.5 crashes building `src/utests/utf_baselib_messaging/UtfBaselibMessagingMain.cpp` for
an x86 target when it runs as the 32-bit host binary:

```
1. <eof> parser at end of file
2. Code generation
Exception Code: 0xC000001D
clang-cl: error: clang frontend command failed due to signal (use -v to see invocation)
Target: i686-pc-windows-msvc
```

This is a crash **inside the compiler**, not a diagnostic about our code. It reproduced identically in
`debug` and `release`, and again standalone with nothing else running on the machine — so it is
deterministic for that translation unit, not a symptom of the matrix's parallel load.

**Diagnosis.** `$(MSVC)/VC/Tools/Llvm/bin/clang-cl.exe` is `PE32, Intel i386` — a 32-bit process,
limited to roughly 2GB of address space. That translation unit produces a **110MB object**. The
frame below the fault is `ucrtbase.dll`, and `0xC000001D` (`STATUS_ILLEGAL_INSTRUCTION`) is what the
CRT's fail-fast path surfaces as on this host, so the crash has the shape of an `abort()` out of
memory rather than of bad code generation.

Note this diagnosis is **inferred from the object size, the faulting frame and the history below**;
the compiler's memory use was not directly instrumented. It is consistent with everything observed,
but if someone reopens this, measuring peak working set of the 32-bit host on that TU is the cheap
way to confirm it outright.

**It is the same problem that was already worked around once.** Commit `50070ef` (2026-02-01,
"bootstrap devenv7: reduce debug info avoid out of memory issues on ARCH=x86 TOOLCHAIN=ccl16
VARIANT=release") added `-gline-tables-only` for exactly this combination, with the comment "Use
minimal debug info for x86 clang-cl release builds to reduce memory consumption". That bought about
seven months. By 2026-09-10 the release variant crashed **even with the trimmed debug info**, and the
debug variant — which still uses full `-Zi` — crashed too.

### Why the translation units are this large

Every test module except `utf_baselib` is built as a **single translation unit**: one
`<Module>Main.cpp` that defines `UTF_TEST_MODULE`, includes `<utests/baselib/UtfMain.h>`, and then
`#include`s every test-case header in the module. Adding a test case adds to the same TU; nothing
caps its growth.

Object sizes, `ARCH=x86 VARIANT=debug` (`vc143` / `ccl16`), largest first:

| Module | vc143 | ccl16 |
|---|---:|---:|
| `utf_baselib_messaging` | 112.7 MB | 110.3 MB |
| `utf_baselib_rest` | 78.7 MB | 78.1 MB |
| `utf_baselib_io` | 77.4 MB | 79.4 MB |
| `utf_baselib_apps` | 77.2 MB | 77.0 MB |
| `utf_baselib_tasks` | 66.7 MB | 67.9 MB |
| `utf_baselib_security` | 65.0 MB | 63.9 MB |
| `utf_baselib_blobtransfer` | 56.2 MB | 57.4 MB |
| `utf_baselib_http` | 55.8 MB | 56.5 MB |
| `utf_baselib` (`UtfBaselibMain.obj`) | 55.3 MB | — |

**Nine modules are at or above 55MB.** `utf_baselib_messaging` is merely the first to cross the line,
not an outlier — the next largest is within a factor of 1.4 of it. Treating this as "the messaging
module is too big" would be the wrong reading.

Size does not track source line count. `utf_baselib_messaging` includes only **3** test headers
(12,419 lines) and produces the largest object; `utf_baselib` includes **19** (23,302 lines) and
produces half as much. The weight is template instantiation, not text, so splitting must be judged by
object size, not by line counts.

---

## The fix applied on 2026-09-10

Three edits, in two files plus the compiler wrappers.

1. **`projects/make/toolchain/msvc-default.mk`, `CLANG_CL_DIR`** — for `ARCH=x86`, select a 64-bit
   clang-cl host matching the build host (`Llvm/ARM64/bin` on an ARM64 host, `Llvm/x64/bin` on an x64
   host), falling back to the 32-bit `Llvm/bin` only when the build host is itself x86 and no 64-bit
   host can run. Previously x86 hardcoded `Llvm/bin`.

2. **`projects/make/toolchain/msvc-default.mk`, `CXXFLAGS`** — add
   `--target=i686-pc-windows-msvc` for `ARCH=x86` under `BL_USE_CLANG_CL`. A 64-bit host otherwise
   defaults to its own triple. This is a no-op when the 32-bit host is used, which already defaults to
   exactly that triple.

3. **`projects/make/toolchain/msvc-default-x86.mk`** — use `$(CLANG_CL_DIR)` in the three
   `BL_USE_CLANG_CL` `PATH` branches instead of hardcoding `$(MSVC)/VC/Tools/Llvm/bin`. This is
   load-bearing: that file is included **after** `msvc-default.mk` and prepends to `PATH`, and
   `scripts/clang-cl.py` resolves the compiler with `shutil.which`, so without it the 32-bit directory
   still wins regardless of `CLANG_CL_DIR`.

**Prerequisite fix.** Adding a joined `--opt=value` argument broke every x86 clang-cl compile, because
`PassThroughOptionParser` in `scripts/clang-cl.py` (and its twin in `scripts/cl.py`) mishandled it:
`optparse` splits `--opt=value`, pushes `value` back onto `rargs`, and only then fails to match the
option, so the value was delivered a second time as a bare argument and clang-cl read it as an input
file (`error: i686-pc-windows-msvc: 'linker' input unused`). Both wrappers now drop the pushed-back
value. This was a **pre-existing latent bug**; no joined long option had ever reached these wrappers.

**Verification.** The ARM64 host compiled the failing TU in 1m02s where the 32-bit host crashed at
1m58s, and `llvm-objdump -f` confirms the output is `architecture: i386` — a genuine 32-bit object,
not a host-architecture object. Both x86 `ccl16` combos then built clean and `x86-ccl16-release`
passed its full suite (17/17). The linker is unaffected: `ccl16` links with MSVC's `link`, not
`lld-link`, so `CLANG_CL_DIR` only selects the compiler.

### What the fix does not do

It gives the compiler a 64-bit address space so the existing translation units fit. It does not make
them smaller, and it does not restore the debug info trimmed in February. **The growth is unbounded
and nothing warns when a module gets bigger** — the next symptom will be slower builds and higher peak
memory, and eventually a 64-bit host under memory pressure, rather than a clean error.

---

## What limits the exposure while this is open

- **All 12 matrix combos build**, and x86 `ccl16` passes its tests, so nothing is blocked today.
- A 64-bit host has orders of magnitude more address space than the ~2GB that was being exhausted;
  the headroom gained is large, not marginal.
- The 64-bit host is also **faster** — 1m02s against 1m58s on the measured TU — because on an ARM64
  host it runs natively instead of under x86 emulation. The change is not a pure workaround; it is
  the better configuration on its own merits.
- Both 64-bit hosts (`ARM64` and `x64`) are present in all three devenv7 dist layouts
  (`hostarch-a64`, `hostarch-x64`, `hostarch-x86`), so the selection cannot fail to resolve.
- `vc143` is untouched on every architecture, and `a64`/`x64` `ccl16` are untouched.

---

## Why splitting the modules is deferred rather than done now

- **It is broad, mechanical and risky in aggregate.** Nine modules are in the size band. Splitting a
  test module means adding TUs, deciding where Boost.Test's runner and `UTF_TEST_MODULE` live, and
  checking that fixtures and helpers shared between the split halves still link exactly once. Done
  across nine modules in one change it is a large diff with a quiet failure mode: a test case that
  silently stops being registered still shows a green run.
- **It was not what the matrix run was for.** The run existed to validate the 12 combos; the host swap
  restores that. Splitting is a separate piece of work that deserves its own change and its own
  review.
- **Nothing is blocked.** With a 64-bit host there is no functional pressure, only the latent growth
  problem.
- **The right split is not obvious without measurement.** Because object size tracks instantiation
  weight rather than line count, splitting by "half the headers each" may not halve the object. The
  work should start from measurements, not from a guess.

---

## The work, when it is picked up

### Step 1 — split the oversized test modules

Target the modules by **object size**, not line count, starting with `utf_baselib_messaging`.

The mechanism is already in the tree: Boost.Test supports a single-runner TU plus additional TUs that
only contribute test cases. `utf_baselib` is already a two-TU module (`UtfBaselibMain.cpp` and
`TestPublicHeaderInstantiation.cpp`), though its second TU is a separate concern rather than a size
split — so treat it as evidence the build system copes with multiple TUs per test module, not as a
worked example of the split itself.

Constraints to respect:

1. Exactly one TU may define `UTF_TEST_MODULE` / pull in the Boost.Test runner; the others must not.
2. Test case names must stay unique across the split, and every case that ran before must still run.
   **Compare the case list before and after** — `--list_content` on the built binary, or the
   `Entering test case` lines in the utf log — and diff them. A case that stops being registered is
   the failure mode to guard against, and it looks like success.
3. Helpers shared between the halves must not become duplicate symbols; move them behind a header with
   the usual inline/`static` discipline rather than copying.
4. `projects/make/common.mk` derives targets from `$(wildcard $(SRCDIR)/utests/utf*)` and builds every
   `.cpp` in the module directory, so adding a TU needs no makefile change.

Aim for a ceiling — 40MB per object on x86 debug is a reasonable first target, which is comfortably
under what the 32-bit host managed historically and leaves room to grow.

### Step 2 — restore full debug info for x86 ccl16 release

`projects/make/toolchain/msvc-default.mk` currently reads, for `BL_USE_CLANG_CL` + `ARCH=x86` +
`VARIANT=release`, `CXXFLAGS += -gline-tables-only`, with every other combination getting `-Zi`.
Once the objects are under control, delete that special case so the whole matrix uses `-Zi`.

This matters beyond tidiness: `-gline-tables-only` means x86 `ccl16` release has **line tables but no
variable or type information**, so a crash dump from that configuration cannot be inspected the way
the other eleven can. `scripts/debug_harness.py` runs `cdb` on a dump when a test crashes, and on this
one configuration it has materially less to work with.

Restore it and rebuild `ARCH=x86 TOOLCHAIN=ccl16 VARIANT=release`; if peak memory or object size is
still uncomfortable, the split is not finished.

### Step 3 — optionally restore the 32-bit host

Only meaningful if there is a reason to want it (see below). To test whether it would work again,
override the directory without editing the makefiles:

```
make -k -j1 utests ARCH=x86 TOOLCHAIN=ccl16 VARIANT=debug \
  CLANG_CL_DIR='<dist>/toolchain-msvc/vc143/BuildTools/VC/Tools/Llvm/bin'
```

`--target=i686-pc-windows-msvc` is harmless there — it is that host's own default triple — so the
override alone is a complete test. If it builds, item 5 can be closed; the makefile change to make it
permanent is to revert the `CLANG_CL_DIR` block to the single hardcoded `Llvm/bin` line.

---

## Conditions to revisit

- **Any test module's x86 debug object approaches ~100MB again.** That is the band in which the
  32-bit host died, and it is the signal that the 64-bit headroom is being consumed too. Worth a
  periodic `find bld/win-x86-*/utests -name '*.obj' -printf '%s %p\n' | sort -rn | head` after a
  matrix run.
- **Compile time or build-machine memory becomes a complaint.** These TUs are the largest single
  compilations in the repository and they are on the critical path of every full build; the split
  pays for itself in wall-clock long before it pays for itself in address space.
- **A crash needs diagnosing in x86 ccl16 release.** That is the configuration with the trimmed debug
  info, and the moment it matters is the moment step 2 stops being cosmetic.
- **The build must run on a 32-bit x86 host.** The fallback still selects `Llvm/bin` there, which will
  crash on these TUs exactly as before — the fix helps only hosts that have a 64-bit clang-cl. This is
  the one scenario in which the split is not optional. Related prior art:
  `scripts/devenv7/AGENTS.md` already documents a separate x86-32-bit-host defect (the Boost
  `clang-win.jam` `i686` patch), so that host is a configuration the project does try to support.
- **A newer LLVM lands in the dist.** Worth re-testing step 3 opportunistically; a later clang may use
  less memory, though it is at least as likely to use more.

---

## Records to update when it lands

- This document: replace the Decision table dispositions with the outcome, in the shape the sibling
  records use.
- `projects/make/toolchain/msvc-default.mk`: the comment block above `CLANG_CL_DIR` for `ARCH=x86`
  and the `-gline-tables-only` comment both reference this limitation and should be updated or removed
  together with the code.
- `scripts/devenv7/AGENTS.md`: if the 32-bit host is restored, or if the split changes how test
  modules are laid out, the toolchain notes there should say so.
