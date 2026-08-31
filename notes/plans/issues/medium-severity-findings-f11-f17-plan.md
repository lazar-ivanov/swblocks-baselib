# Fix F-12, F-14, F-15, F-16, F-17 — the low-risk medium-severity findings

## Context

`notes/reviews/swblocks-baselib/major/update_2025/v1/pr_review_analysis_gpt56sol.md` lists seven
medium-severity findings (F-11 – F-17) against `lazari2`. The critical and high findings F-01 and
F-03 – F-10 were already addressed by earlier commits on this branch.

This change closes the five medium findings that are cheap, mechanically reviewable and very low
risk, and explicitly defers the two that need a design decision or a wide-blast-radius change.

Each finding was verified against the actual source rather than taken from the review at face
value. Three of the review's claims turned out to be inaccurate — F-12's stated failure mode is
not reproducible, F-11 is partly already handled, and F-16 is **more** severe than described.

**Outcome:** five findings closed; devenv7 build behaviour is byte-identical for every currently
supported toolchain; `BOOST_UUID_DISABLE_ALIGNMENT` is no longer include-order dependent; the
non-throwing `fs::copy` overload honours its contract; Windows dependency files preserve the exact
bytes the compiler emitted.

| ID | Finding | Effort | Risk | Verdict |
|---|---|---|---|---|
| F-17 | `fs::copy(src, dst, ec)` can throw | ~10 lines, 1 file | Very low | **Fixed** |
| F-16 | UUID guards depend on include order | 1 include, 1 file | Very low | **Fixed** |
| F-14 | Exact `devenv7` equality checks | 6 make edits | Very low | **Fixed** |
| F-12 | `saveToStream` option contract | ~17 lines, 1 file | Very low | **Fixed** (reduced scope) |
| F-15 | Compiler wrapper encoding | 2 scripts + docs + tests | Low | **Fixed** |
| F-13 | S3 credentials on command line | 5 call sites + argparse + 35 doc examples | Low–moderate | **Deferred** |
| F-11 | JSON wire/hash compatibility | Canonical format + custom serializer + golden vectors | High | **Deferred** |

Nothing is committed by this work; the changes are left in the working tree for review. The five
findings touch completely disjoint file sets, so the intended one-commit-per-finding split is
derivable from paths alone — see **Staging** at the end.

---

## F-17 — the error-code `copy` overload throws instead of reporting through `ec`

**File:** `src/include/baselib/core/detail/OSImplPlatformCommon.h`

The non-throwing overload opened with `is_directory( sourcePath )`, which binds to the *throwing*
`bl::fs::is_directory( const path& )` at line 1727. An inaccessible source therefore unwound out of
an API whose entire contract is "report via `ec`".

The fix uses the error-code overload that already exists at line 1741 — no new helper was needed —
and returns early when `ec` is set:

```cpp
if( is_directory( sourcePath, ec ) )
{
    detail::bfs::create_directory( targetPath, sourcePath, ec );
    return;
}

if( ec )
{
    return;
}
```

The throwing `copy( source, target )` overload above it is untouched. Repo-wide, the only `fs::copy`
caller is `FsUtils.h:921`, which uses the throwing overload, so the `ec` overload has no in-repo
callers and the blast radius is zero.

### Correction — the first test design did not exercise the bug

The obvious test — copy from a **missing** source — does not work. Boost.Filesystem treats
`ENOENT`/`ENOTDIR` as `file_not_found`, which is *not* an error, so `is_directory` never throws for
a merely missing path. A test written that way passes against the unfixed header (confirmed by
reverting the header and rebuilding: the test still passed).

The real trigger is an **inaccessible** source. `FsUtils_TestCopyErrorCodeOverload` in
`src/utests/utf_baselib/TestBaselibDefault.h` therefore:

1. creates `restricted/source.txt` inside the test's `TmpDir`,
2. sets `restricted` to `bl::fs::perms::no_perms`,
3. probes whether the throwing `is_directory` actually raises on this platform,
4. if so, calls the `ec` overload and records whether it threw,
5. **restores the permissions before asserting**, so `TmpDir` cleanup always succeeds,
6. asserts the `ec` overload did not throw, that `ec` is set, and that no target was created.

Step 3 keeps the test meaningful on POSIX and harmless on Windows, where the permission change is
not enforced. A second block asserts the overload still succeeds and leaves `ec` clear for a valid
directory source.

**Verified:** reverting only `OSImplPlatformCommon.h` and rebuilding makes the test fail with
`critical check ( ! copyWithErrorCodeThrows ) has failed`; restoring it makes it pass.

---

## F-16 — `BOOST_VERSION` tested before `<boost/version.hpp>` is included

**File:** `src/include/baselib/core/detail/UuidBoostImports.h`

`#if BOOST_VERSION >= 108900` guarded `BOOST_UUID_DISABLE_ALIGNMENT`, but the first Boost header in
the file was included *after* that guard. With `BOOST_VERSION` undefined the preprocessor evaluates
it as `0` and the guard is false.

The fix adds `#include <boost/version.hpp>` immediately after `BoostIncludeGuardPush.h` and before
the first version guard. The later guards already sat after `<boost/uuid/uuid.hpp>` and were
incidentally fine; they are now correct by construction too.

### Correction — this is worse than the review described

The review called it an ABI hazard. It is that, but the immediate symptom is a **hard compile
error**: with `BOOST_VERSION == 0` the `#if BOOST_VERSION < 108100` branch at the bottom of the
header also fires, defining a `std::hash< bl::uuid_t >` that collides with the one Boost 1.90
provides natively.

Confirmed empirically with a translation unit containing only
`#include <baselib/core/detail/UuidBoostImports.h>`:

| | result |
| --- | --- |
| before the fix | `error: redefinition of 'struct std::hash<boost::uuids::uuid>'` |
| after the fix | compiles; `BOOST_UUID_DISABLE_ALIGNMENT` defined, `alignof( bl::uuid_t ) == 1` |

This is reachable in principle because `src/include/baselib/core/TlsState.h:20` includes
`UuidBoostImports.h` as its **very first** include. In practice `TlsState.h` is not currently
standalone-compilable for an unrelated reason (`CPP.h` has an incomplete-type problem with
`boost::function<void() noexcept>`), which belongs to F-18's header self-containment family and was
deliberately left alone. Before the fix the first error from that header was the UUID redefinition;
after the fix only the pre-existing `CPP.h` error remains.

Because this changes preprocessor state, it must be validated on a clean tree, never incrementally.

---

## F-14 — exact `devenv7` equality checks contradict the stated devenv7+ policy

**Files:** `projects/make/devenv-detect.mk`, `projects/make/common.mk`,
`projects/make/3rd/boost/common.mk`, `projects/make/3rd/jdk/common.mk`,
`projects/make/utests/utf_baselib_jni/Makefile`

Gates written as `ifeq ($(DEVENV_VERSION_TAG),devenv7)` mean a future `devenv8` silently falls into
the legacy branch: JDK 8 paths, Python 2, `boost_system` linked (no longer a separate lib in Boost
1.89+), and Gradle's removed `--build-file`.

Legacy is now expressed as an explicit finite set, defined once in `devenv-detect.mk` after
`DEVENV_VERSION_TAG` is resolved and validated:

```make
BL_DEVENV_IS_LEGACY := $(filter devenv2 devenv3 devenv4 devenv5 devenv6,$(DEVENV_VERSION_TAG))
```

This is an existing repo convention, not a new one — the same `$(filter devenv2 … devenv6, …)`
pattern was already used in `3rd/boost/common.mk:73`, `3rd/gradle/latest.mk:3`, `arch-compat.mk:23`
and the JNI Makefile's own line 24.

Six gates were flipped to test `BL_DEVENV_IS_LEGACY`:

| File | Gate |
| --- | --- |
| `3rd/boost/common.mk:6` | BOOSTDIR variant suffix |
| `3rd/boost/common.mk:25` | `BOOST_BIND_GLOBAL_PLACEHOLDERS` |
| `3rd/boost/common.mk:44` | skip `boost_system` |
| `3rd/jdk/common.mk:5` | JDK 25 layout vs JDK 8 layout |
| `common.mk:111` | Python selection |
| `utests/utf_baselib_jni/Makefile:59` | Gradle `-p` vs `--build-file` |

The version-pinning blocks in `devenv-detect.mk` (`BL_DEVENV_BOOST_VERSION`,
`BL_DEVENV_OPENSSL_VERSION`, `-DBL_DEVENV_VERSION=7`, the `win7` → `win` rename) were deliberately
**not** converted. Those are correctly per-version; a future `devenv8` must declare its own values.

**Why it is safe:** `devenv7` is currently the only non-legacy value `DEVENV_VERSION_TAG` can hold
(`devenv-detect.mk` maps every supported toolchain and errors on anything else), so for every build
configuration that exists today the flipped conditions evaluate identically.

**Verified:** `BOOSTDIR`, `JDK_VERSION`, `JDK_BASE_PATH`, `PYTHON`, `GRADLE_ARGS` and `LDLIBS`
resolve byte-identically before and after, for devenv7 (default), devenv6 (`TOOLCHAIN=clang1500`),
devenv5 (`TOOLCHAIN=gcc1110`) and devenv2 (`TOOLCHAIN=gcc492`). The only delta in `make -p` output
is the new `BL_DEVENV_IS_LEGACY` variable itself, which is empty for devenv7 and `devenv6`/`devenv2`
respectively for the legacy toolchains.

---

## F-12 — `saveToStream` option contract (reduced scope)

**File:** `src/include/baselib/core/JsonUtils.h`

### Correction — the review's stated failure mode is not reproducible

The review claims `saveToStream(v, s, OutputOptions::raw_utf8)` still compiles and silently means
"pretty print". It does not: `json::OutputOptions` was **removed entirely** in this branch —
`grep -rn OutputOptions src/ scripts/` returns nothing — so that exact call is already a hard
compile error, which is the desired outcome.

The residual risk is narrower but real. The third parameter went from `const unsigned int options`
on `master` to `const bool prettyPrint`, so external code passing a bare integer or a raw
`json_spirit::Output_options` value (`raw_utf8 == 2`) still compiles and now silently requests
pretty printing.

A deleted overload closes it, so any non-`bool` third argument fails to compile:

```cpp
template
<
    typename STREAM
>
inline void saveToStream(
    SAA_in          const json::value&                      val,
    SAA_inout       STREAM&                                 output,
    SAA_in          const unsigned int                      options
    ) = delete;
```

Compile-time only, zero runtime behaviour change. Both in-repo callers pass a literal `bool` —
`Manifest.h:437` (`true`) and `TestJsonAbstraction.h:503` (`false`).

---

## F-15 — compiler wrappers lose non-UTF-8 bytes in dependency paths

**Files:** `scripts/cl.py`, `scripts/clang-cl.py`, `scripts/README.md`,
`scripts/tests/test_cl_functional.py`

`raw_line.decode('utf-8', errors='replace')` turned any non-UTF-8 byte into U+FFFD, and those
mangled strings went straight into the `.d` dependency file — so `make` got edges pointing at files
that do not exist and could reuse stale objects.

This was cheaper than the review implied: `scripts/README.md` already documented the current state
as a deliberate partial fix ("Option B") and spelled out the complete fix ("Option C") with working
code. The remaining work was to apply it.

1. Decode with `latin-1`, which maps bytes 0–255 bijectively and therefore round-trips losslessly.
2. Write the `.d` file in binary mode, encoding each line back through a helper.
3. Reuse the `_reconfigure_stdio()` pattern from `debug_harness.py:101-117` for the `print` path.
   **This also fixes a latent bug the review missed:** the wrappers never reconfigured stdio, so
   echoing a decoded non-ASCII line to an ASCII stdout under MSYS2/make could raise
   `UnicodeEncodeError` even with `errors='replace'`.
4. `scripts/README.md` updated — Option C is now the current fix, Option B is retained as history.

**Why it is safe:** for pure-ASCII compiler output `latin-1` and `utf-8` produce byte-identical
results, so every current build on every platform is unaffected. Behaviour diverges only on the
non-ASCII paths that were broken.

### Correction — two deviations from the README's Option C

**The existing tests could not stay unchanged.** Two `TestCompilerWrapperEncoding` cases called
`depfile.read_text()`, which now hits the preserved `0xe9` byte instead of a U+FFFD and raises
`UnicodeDecodeError`. Their reads were switched to `read_bytes()` with byte literals; the tests'
intent is unchanged, only the read method. A new `test_non_utf8_dependency_path_round_trips` asserts
the original bytes survive verbatim and that no U+FFFD appears.

**The README snippet is not Python 2 safe.** It encodes the target with a bare
`.encode('latin-1')`, but the target comes from `argv` rather than the compiler byte stream, and on
Python 2 a native `str` would be implicitly ASCII-decoded first, raising `UnicodeDecodeError` —
which the snippet's `except UnicodeEncodeError` would not catch. The helper therefore returns
`bytes` unchanged and falls back to the filesystem encoding on any `UnicodeError`.

---

## Deferred

### F-13 — S3 credentials required on the command line

The security point is valid: `--access-key` / `--secret-key` are `required=True` at
`scripts/s3_manage.py:767-772`, so secrets land in process listings and CI echoes, and
`--account-id` is required but never read anywhere in the implementation.

Not risky to fix, but not small: five separate `boto3.client(...)` constructions (lines 798, 917,
998, 1306, 1490) want a shared factory, `--account-id` cannot simply be removed without breaking
existing CI invocations (it must be demoted to accepted-and-ignored), and `scripts/s3_manage.md`
carries 35 `--access-key` occurrences in worked examples. This belongs in its own commit and review.

### F-11 — JSON backend migration changes persisted hashes and wire semantics

Two of the review's sub-claims are weaker than stated:

- **Key ordering is already handled.** `canonicalizeValue()` in `BoostJsonImpl.h:281-340`
  recursively sorts object keys, and everything in the library that hashes goes through
  `getObjectHashCanonical`. Non-canonical `getObjectHash` has no in-repo caller outside
  `TestDataModelDefault.h`.
- **Duplicate-key rejection is retained** on the json-spirit side (`JsonSpiritImpl.h:337-355`); the
  gap is that Boost.JSON has no equivalent, so the two backends disagree.

What genuinely remains is not cheap. `rawUtf8` is accepted and ignored under Boost.JSON
(`BL_UNUSED( rawUtf8 )` at `BoostJsonImpl.h:455`) — json-spirit escapes non-ASCII to `\uXXXX` when
it is false, Boost.JSON always emits raw UTF-8 — and number/string formatting is not guaranteed
byte-compatible between the two serializers. Closing either properly means writing a custom
serializer plus cross-backend golden vectors, and deciding whether the exported wire format may
change at the devenv6 → devenv7 boundary at all. That is a design decision, not a cleanup.

---

## Verification performed

All on devenv7 / ub24-a64-clang2010, from the repo root.

| Check | Result |
| --- | --- |
| `rm -rf ./bld && make -k -j4` | exit 0 |
| `make -k -j4 VARIANT=release` | exit 0 |
| `utf_baselib` | 145 test cases, no errors |
| `utf_baselib_data` | 66 test cases, no errors |
| `utf_baselib_messaging` | 27 test cases, no errors |
| `.venv/bin/pytest scripts/tests/test_cl_unit.py scripts/tests/test_cl_functional.py` | 58 passed |
| F-14 no-op proof (`make -p` var comparison, 4 toolchains) | identical |
| F-17 negative control (revert header, rebuild) | test fails as expected |
| F-16 negative control (standalone TU compile) | redefinition error before, clean after |
| `git diff --check` | clean |

**Not verified here:** the Windows half of F-15. The `test_cl_functional.py` cases exercise it
through mock compilers on Linux, but real `cl.exe` code-page output is not reproducible in this
environment. A Windows devenv7 build should be run before merge.

---

## Staging

No file appears in two rows, so `git add` of a row's paths yields exactly that finding's diff.

| # | Commit | `git add` paths |
|---|---|---|
| 1 | F-17 — error-code `fs::copy` overload must not throw | `src/include/baselib/core/detail/OSImplPlatformCommon.h`, `src/utests/utf_baselib/TestBaselibDefault.h` |
| 2 | F-12 — reject the legacy integral `saveToStream` options argument | `src/include/baselib/core/JsonUtils.h` |
| 3 | F-14 — express legacy devenv behaviour as an explicit version set | `projects/make/` |
| 4 | F-15 — preserve compiler output bytes in generated dependency files | `scripts/cl.py`, `scripts/clang-cl.py`, `scripts/README.md`, `scripts/tests/test_cl_functional.py` |
| 5 | F-16 — make the UUID Boost-version guards self-contained | `src/include/baselib/core/detail/UuidBoostImports.h` |

Each change is logic-only per `AGENTS.md` — no formatting, comment or naming cleanups ride along.
