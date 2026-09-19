# Layer L1 under gcc, and utf_baselib_http against the extended SSL stream wrapper

**Measured:** 2026-09-18, after L1 closed at `c1a3af4`, and extended the same evening - see "The one
row which needed re-running" at the end. **Status:** ANSWERED, affirmative on both
counts. No action follows from it; no code was changed.

Layer L1 ran under a round rule of **clang2010 debug, focused modules only**. That was a deliberate
trade for speed and it left two things unmeasured. This record measures them, so that L2 does not
inherit them as assumptions and so that S2.5 does not have to re-derive the toolchain half of D15.

## Gap 1 - the one existing module the extended wrapper is reached from

S1.6 added ~300 lines and a data member to `tasks/AsioSslStreamWrapper.h`, which is on the path of
every TLS task. The only module compiled after it was `utf_baselib_http2`. `utf_baselib_http` is the
module with the real TLS client and server tasks over that wrapper, and it had not been rebuilt.

**Built and run, clang2010 debug, linux-a64: rc 0, zero warnings, first attempt. 37 cases entered
and 37 left, `*** No errors detected`, no `leaked`, no `FATAL`, no `ThreadSanitizer`.** The
extension is confirmed additive against the module which actually drives it.

## Gap 2 - nothing in L1 had been compiled by gcc

Everything is built `-Wall -Wpedantic -Wextra -Werror` and gcc's warning set is not clang's, so a
warning clang does not emit would fail a build nobody had run. That is stated as the residual risk
in `beast-availability-probe-record.md`, and it applied to the whole layer, not only to Beast.

**gcc 15.2.0, release, linux-a64, over every module L1 touched or created: six of six built with
zero warnings and ran clean - 133 of 133 cases.**

| module | build | gcc `-O3` object | cases | run |
|---|---|---:|---:|---|
| `utf_baselib_h2core` | rc 0, 0 warnings | 36.9MB | 5 in, 5 out | `No errors detected`, 0/0/0 |
| `utf_baselib_http2` | rc 0, 0 warnings | 57.4MB | 32 in, 32 out | `No errors detected`, 0/0/0 |
| `utf_baselib_httpclient` | rc 0, 0 warnings | 37.3MB | 7 in, 7 out | `No errors detected`, 0/0/0 |
| `utf_baselib_h2profiles` | rc 0, 0 warnings | 42.2MB | 5 in, 5 out | `No errors detected`, 0/0/0 |
| `utf_baselib2` | rc 0, 0 warnings | 62.1MB | 47 in, 47 out | `No errors detected`, 0/0/0 |
| `utf_baselib2` **re-run, see below** | rc 0, 0 warnings | 62.2MB | **48 in, 48 out** | `No errors detected`, 0/0/0 |
| `utf_baselib_http` | rc 0, 0 warnings | 101.3MB | 37 in, 37 out | `No errors detected`, 0/0/0 |

The three counts in the run column are `leaked` / `FATAL` / `ThreadSanitizer`, all of which print
*after* a green summary line and none of which appeared. Every case count equals that module's
declared `UTF_AUTO_TEST_CASE` count, and every build log was checked for `Compiling` and `Linking`
so that no row is a no-op over an up-to-date object - the gcc tree did not exist beforehand and
every translation unit compiled fresh.

`utf_baselib_h2client` is excluded: it still has no case and exits 200 until S4.1 gives it one.

## The two headers that were the specific concern, and why neither fired

**`core/detail/BeastBoostImports.h` is included by nothing in the repo yet**, so *no* module build
in either toolchain compiles it. A module pass could not have closed this half; only a probe can.
S1.5's own two probe sources were reused verbatim and compiled with the project's own flags exactly
as the makefiles emit them:

| probe | clang2010 debug (S1.5) | gcc1520 release | gcc1520 debug |
|---|---|---|---|
| a TU including only the import header | rc 0, 0 warnings | rc 0, 0 warnings | rc 0, 0 warnings |
| a response parser deriving `basic_parser< false >`, all ten virtuals | rc 0, 0 warnings | rc 0, 0 warnings | rc 0, 0 warnings |

The second is the one that carries weight: including the header instantiates nothing, and
`basic_parser` is abstract, so only a TU which derives from it and overrides all ten virtuals makes
a compiler instantiate Beast's templates - and instantiation is where the two warning sets diverge.

**The message callback handed to `SSL_set_msg_callback` is not a function-pointer cast**, so
`-Wcast-function-type` has nothing to bind to in either compiler. In OpenSSL 3.5.4
`SSL_set_msg_callback` is a real function, not a macro (`ssl.h:663`):

    void SSL_set_msg_callback( SSL* ssl,
        void ( *cb ) ( int write_p, int version, int content_type,
                       const void* buf, size_t len, SSL* ssl, void* arg ) );

and `AsioSslStreamWrapperT<...>::onSslMessageCallback` is a static member whose type - after
`SAA_in` expands to nothing off MSVC and the top-level `const` on its parameters is discarded from
the function type, as [dcl.fct]/5 requires - is exactly that parameter type.
`&this_type::onSslMessageCallback` therefore binds with no cast and no conversion. The companion
`SSL_set_msg_callback_arg` *is* a macro over `SSL_ctrl`, which is why the call beside it has no
leading `::` and is cast to `( void )` - but what it passes through `void*` is `this`, an object
pointer, not a function pointer.

Measured as well as argued: `utf_baselib_http2` is the only module which instantiates
`enableClientHelloCapture()`, its `TestAsioSslStreamWrapper.h` also drives `onSslMessageCallback`
directly, and `AsioSslStreamWrapper_ClientHelloCaptureTests` passed under gcc release.

## Why gcc debug was not run over the modules

The modules were built gcc **release** only, and that is a choice with a reason rather than an
omission.

- gcc's warning set at `-O3` is a superset of its set at `-O0`: the optimizer-dependent diagnostics
  (`-Wmaybe-uninitialized`, `-Warray-bounds`, `-Wstringop-*`, `-Wdangling-pointer`,
  `-Wuse-after-free`) fire only with optimization, and the front-end ones fire in both.
- The front-end class is already covered from the other side: clang2010 **debug** built every one of
  these modules during L1, at `-O0`, with the same `-Wall -Wpedantic -Wextra`.
- **No L1 code is debug-only.** Not one of the headers L1 added, and not the S1.6 delta to
  `AsioSslStreamWrapper.h`, contains an `NDEBUG`, `_DEBUG` or `BL_ASSERT` conditional - so there is
  no line a debug build compiles that a release build does not.

So clang debug plus gcc release covers L1's compile surface, and the gcc debug corner is the
redundant one. The Beast import header is the exception, and it was probed in **both** gcc variants
because no module build reaches it in either.

## What this does not settle

**Windows is untouched.** msvc and clang-cl are not measured here and remain argued, exactly as
`beast-availability-probe-record.md` leaves them; the `/analyze` warning set is the hazard that
argument turns on. **OpenSSL 1.1.1w is still owed** - see `openssl-1x-flavor-deferral.md`; every
number here is OpenSSL 3.5.4. And all of it is linux-a64.

One number worth carrying to S2.5, which measures the object-size delta of the consuming test
module: **gcc `-O3` objects here run about twice their clang debug size** - `utf_baselib_http2` is
28.6MB under clang debug and 57.4MB under gcc release, `utf_baselib_h2profiles` 22.3 against 42.2,
`utf_baselib_http` 44.2 against 101.3. Nothing fails: the 40MB target in `src/utests/AGENTS.md` is
calibrated on *debug* objects and the gate is `off` on Linux
(`src/utests/object-size-limits.json`). That file already records the same shape on Windows, where
win-x86-vc143-release measured 96.77MB against a 75MB debug ceiling and built perfectly well. A
release object is simply not the number that target is about.

## The one row which needed re-running

This pass branched from `c1a3af4`, which was **not** the tip by the time it finished: `75e50fc`, the
`Uri::origin()` guard, landed on another lane while this was building. So the `utf_baselib2` row
above compiled a `Uri.h` without the guard - 47 cases rather than 48 - and the guard had been seen by
clang debug only.

The risk was nil, two `BL_CHK_T`s in a header gcc had already compiled. But a coverage record which
quietly does not cover something is worse than one which says so, and this was a three minute build,
so it was re-run rather than annotated:

    utf_baselib2, gcc1520 release, at the tip carrying the guard
      build  rc 0, 0 warnings, 62.2MB
      run    rc 0, 48 entered / 48 left, *** No errors detected, 0 leaked
      includes Uri_OriginRequiresAbsoluteUriTests

**So every line of L1, the post-review guard included, is covered by gcc release.** The lesson is
worth more than the row: a toolchain pass taken against a tip which other lanes are still moving
describes the tree it started from, not the tree that ships. Either take it last, or say which commit
it measured - this record now does the latter.
