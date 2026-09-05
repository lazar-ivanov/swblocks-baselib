# Fix F-09 — JNI make logic breaks devenv2–6 on Linux and macOS

## Context

`projects/make/utests/utf_baselib_jni/Makefile` picks which `libjsig` to link into the
JNI unit-test binary. JDK 8 (devenv2–6) and JDK 25 (devenv7+) use different layouts:

| JDK | layout |
| --- | --- |
| 8 (devenv2–6) | `$(JAVA_HOME)/jre/lib/<arch>/server/libjsig.so` (Linux), `$(JAVA_HOME)/jre/lib/server/libjsig.dylib` (macOS) |
| 25 (devenv7+) | `$(JAVA_HOME)/lib/server/libjsig.{so,dylib}` + `-Wl,-rpath` |

Commit `a2231b9` ("bootstrap devenv7: fixing utf_baselib_jni for windows") replaced the
layout predicate `ifeq ($(DEVENV_VERSION_TAG),devenv7)` with `ifndef BL_WIN_JNI_DISABLED`,
collapsing two independent decisions — *which JDK layout* and *is Windows JNI available* —
into one flag. `BL_WIN_JNI_DISABLED` is only ever set when `OS` is Windows, so **every
non-Windows devenv2–6 build now takes the devenv7 branch** and links a path that does not
exist. The intended legacy branch is unreachable: its body is guarded by `ifneq (win, ...)`
but is only entered when the Windows-only flag is set.

Confirmed empirically against the local devenv5 dist:

```
make -n utf_baselib_jni \
  DIST_ROOT_DEPS{1,2,3}=/home/lazar/swblocks/old-envs/dist-devenv5-ub20-gcc1110-clang1201-arm \
  OS=ub20 ARCH=a64 TOOLCHAIN=gcc1110
```

emits `.../jdk/open-jdk/8/ub20-a64/lib/server/libjsig.so`, while the dist only contains
`.../jdk/open-jdk/8/ub20-a64/jre/lib/aarch64/server/libjsig.so`. The link fails.

**Outcome:** legacy Linux/macOS JNI builds link again; devenv7 behaviour is byte-identical
to today; the gate becomes future-proof (a hypothetical `devenv8` takes the modern path).

### Secondary finding driving one of the decisions

`BL_WIN_JNI_DISABLED` **disables nothing**. It is written at line 19 and read only at
lines 15 and 25 of the same file — no other reference exists in the repo. On Windows both
branches emit zero `LDADD`, so setting it has no observable effect. It also cannot exclude
the target: this Makefile is included from `projects/make/common.mk:589` → `:482`, *after*
the `BL_JNI_ENABLED` filter at `projects/make/common.mk:285`. What actually keeps Windows
JNI off devenv2–6 is `projects/make/3rd/jdk/common.mk:12` looking for
`$(DIST_ROOT_DEPS3)/jdk/open-jdk/8/win7-<arch>`, which is not published — so
`BL_JNI_ENABLED` is never set and the target is filtered out. The flag is therefore
removed rather than preserved.

## Scope

F-09 only. Explicitly **not** in scope (tracked separately):

- F-14 centralization of the `devenv2..devenv6` predicate into a shared variable — the
  literal `$(filter ...)` set is repeated in `3rd/gradle/latest.mk:3`, `arch-compat.mk:23`,
  `3rd/boost/common.mk:73`; this change keeps using the same literal pattern locally.
- The inverted `jdk/1.8.mk` vs `jdk/common.mk` include at
  `projects/make/utests/utf_baselib_jni/Makefile:1-5` (mirror-image of
  `projects/make/common.mk:159-163`). Pre-existing on master; harmless today only because
  no devenv7 dist ships `oracle-jdk/latest-1.8`.

## Changes

### 1. `projects/make/utests/utf_baselib_jni/Makefile` — replace lines 13–54

Use Edit (not Write). Delete the `BL_WIN_JNI_DISABLED` set-block (lines 13–22) entirely and
replace the two-way flag branch with a single chain that tests OS first, then the explicit
legacy set, then the modern default:

```make
# Configure JNI library linking based on platform and devenv version
#
# Windows JNI support is a devenv7+ capability; for devenv2-6 the JNI targets are excluded
# automatically because the JDK 8 dist publishes no win7-<arch> layout, so BL_JNI_ENABLED
# is never set (see 3rd/jdk/common.mk and the target filter in common.mk)
ifeq (win, $(findstring win, $(OS)))
  # Windows: No libjsig linking needed (uses SEH instead of POSIX signals)
  # Runtime loads jvm.dll via PATH (already configured in jdk/common.mk)
  # JavaVirtualMachine.h handles dynamic loading from bin/server/jvm.dll
else ifneq ($(filter devenv2 devenv3 devenv4 devenv5 devenv6,$(DEVENV_VERSION_TAG)),)
  # devenv2-6: JDK 8 uses the old jre/lib structure
  ifeq (darwin, $(findstring darwin, $(BL_PROP_PLAT)))
    # macOS JDK 8: jre/lib/server with .dylib (no arch subdirectory)
    $(utf_baselib_jni_ARTIFACT): LDADD += $(JAVA_HOME)/jre/lib/server/libjsig.dylib
  else ifeq (x86, $(ARCH))
    $(utf_baselib_jni_ARTIFACT): LDADD += $(JAVA_HOME)/jre/lib/i386/server/libjsig.so
  else ifeq (x64, $(ARCH))
    $(utf_baselib_jni_ARTIFACT): LDADD += $(JAVA_HOME)/jre/lib/amd64/server/libjsig.so
  else ifeq (a64, $(ARCH))
    $(utf_baselib_jni_ARTIFACT): LDADD += $(JAVA_HOME)/jre/lib/aarch64/server/libjsig.so
  endif
else ifeq (darwin, $(findstring darwin, $(BL_PROP_PLAT)))
  # devenv7+ macOS JDK 25: Modern structure with lib/server (no arch subdirectory)
  $(utf_baselib_jni_ARTIFACT): LDADD += $(JAVA_HOME)/lib/server/libjsig.dylib
  $(utf_baselib_jni_ARTIFACT): LDADD += -Wl,-rpath,$(JAVA_HOME)/lib/server
else ifeq (linux, $(findstring linux, $(BL_PROP_PLAT)))
  # devenv7+ Linux JDK 25: Modern structure with lib/server for all architectures
  $(utf_baselib_jni_ARTIFACT): LDADD += $(JAVA_HOME)/lib/server/libjsig.so
  $(utf_baselib_jni_ARTIFACT): LDADD += -Wl,-rpath,$(JAVA_HOME)/lib/server
endif
```

Points to preserve deliberately:

- **No `-Wl,-rpath` in the legacy arm** — master did not add one; do not introduce it.
- **`OS` for the Windows test, `BL_PROP_PLAT` for linux/darwin** — matches the existing
  code and `3rd/jdk/common.mk:33-38`. `findstring win` covers both `win` and `win7`.
- **No `else` fallback in either arm** — an unrecognized `ARCH`/platform links nothing,
  exactly as on master.
- Leave lines 1–11 (jdk/gradle includes) and lines 56 onward (Gradle args) untouched.

The only intentional behaviour change beyond restoring master is the legacy **darwin**
sub-branch: master emitted `jre/lib/<arch>/server/libjsig.so` there, a path no macOS JDK 8
has ever shipped, so that configuration could only fail to link. It now emits the real
JDK 8 macOS path. This cannot regress a working build and does not touch devenv7.

### 2. `scripts/devenv7/AGENTS.md:396` — update the version-gating sentence

The current text documents `BL_WIN_JNI_DISABLED`, which no longer exists. Replace it with
the actual mechanism: the libjsig path is chosen from the explicit devenv2–6 set (JDK 8
`jre/lib`) versus devenv7+ (JDK 25 `lib/server`); Windows JNI is devenv7+ only because no
`jdk/open-jdk/8/win7-<arch>` dist is published, so `BL_JNI_ENABLED` stays unset and the
JNI targets are filtered out. Keep it to one or two lines — this is a doc correction that
ships in the same commit as the code it describes.

## Risk

Low. For devenv2–6 the emitted link line returns to master's, and for devenv7 it is
unchanged.

| devenv | OS | today | after |
| --- | --- | --- | --- |
| 2–6 | Linux (x86/x64/a64) | `lib/server/libjsig.so` → **link fails** | `jre/lib/{i386,amd64,aarch64}/server/libjsig.so` = master |
| 2–6 | macOS | `lib/server/libjsig.dylib` → fails | `jre/lib/server/libjsig.dylib` (real JDK 8 layout) |
| 2–6 | Windows | no LDADD | no LDADD, unchanged |
| 7 | Linux | `lib/server/libjsig.so` + rpath | unchanged |
| 7 | macOS | `lib/server/libjsig.dylib` + rpath | unchanged |
| 7 | Windows | no LDADD | no LDADD, unchanged |
| 8+ | any | modern path (filter misses) | unchanged |

Arch coverage is unaffected: devenv7 x86 has no JDK 25, so `BL_JNI_ENABLED` is never set
and the target is filtered out before this code is parsed. `else ifeq` chaining is already
used in this file and in `3rd/jdk/common.mk`, so no new GNU Make version requirement.

## Verification

All commands are dry runs unless stated; `make -n` executes no recipes.

**1. Legacy regression (reproduces the bug, must change):**

```bash
D=/home/lazar/swblocks/old-envs/dist-devenv5-ub20-gcc1110-clang1201-arm
make -n utf_baselib_jni DIST_ROOT_DEPS1=$D DIST_ROOT_DEPS2=$D DIST_ROOT_DEPS3=$D \
  OS=ub20 ARCH=a64 TOOLCHAIN=gcc1110 2>&1 | grep -o '[^ ]*libjsig[^ ]*'
```

- before: `$D/jdk/open-jdk/8/ub20-a64/lib/server/libjsig.so` (does not exist)
- after: `$D/jdk/open-jdk/8/ub20-a64/jre/lib/aarch64/server/libjsig.so` (exists — confirm
  with `ls`), and no `-rpath` entry

**2. devenv7 zero-delta (must not change):** capture the full link line before and after
the edit against the default dist root and diff them.

```bash
make -n utf_baselib_jni 2>&1 | grep 'Linking\|libjsig' > /tmp/.../devenv7-before.txt
# apply edit, re-run into devenv7-after.txt, then diff
```

Expect an empty diff, with `.../openjdk/25/a64/lib/server/libjsig.so` plus its `-rpath`
present in both.

**3. Real devenv7 build and test** (this is the native environment):

```bash
make -k -j4 utf_baselib_jni
make test_utf_baselib_jni
```

**4. Diff review:** `git diff` should touch exactly two files, with no formatting or
unrelated changes, and `git diff --check`.

**5. Cross-platform sign-off, where hardware is available** — a devenv5/6 Linux build and,
if any legacy macOS dist actually carries `jdk/open-jdk/8/d<ver>-<arch>`, a legacy macOS
build to exercise the new darwin sub-branch. If no such dist exists, record that the legacy
macOS arm is unexercised; it is unreachable in that case because `BL_JNI_ENABLED` is never
set.
