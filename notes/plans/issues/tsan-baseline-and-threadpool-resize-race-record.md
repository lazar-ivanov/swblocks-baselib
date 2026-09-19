# ThreadSanitizer on this host: it works, and the baseline is not clean

**Origin:** follow-up **F-L0-3** of the HTTP/2 client plan
(`notes/plans/http2-implementation-plan.md` §2, "L0 follow-ups"), which asks for the multi-operation
stress case of design §3.8 commit 1 to be run under ThreadSanitizer. Before that case could be run,
the question was whether TSan works on this machine at all - nothing here had ever been built with
it. **Date:** 2026-09-18. **Status:** the environment question is answered; the race in
`ThreadPoolImpl` (§3) is **FIXED** - see §8. The race in the test code (§4) is still open.

Host: `ub24-a64-dev-d2-rosetta`, Linux 6.8.0-139, aarch64, 2 cores.
Toolchain: devenv7, clang2010, `VARIANT=debug`, `BL_CLANG_ENABLE_RA_TSAN=1`.

---

## 1. The environment answer: TSan works, fully

Both halves of the risk are closed.

**The toolchain ships a TSan runtime.** `libclang_rt.tsan.a`, `libclang_rt.tsan.so` and
`libclang_rt.tsan_cxx.a` are present under
`$(DIST_ROOT_DEPS3)/toolchain-clang/20.1.0/ub24-a64-clang2010-release/lib/clang/20/lib/aarch64-unknown-linux-gnu/`.

**The runtime starts on this kernel.** This mattered more than it looks: TSan supports aarch64 only
at 39-, 42- and 48-bit VMA, and aborts at startup with `unsupported VMA range` otherwise. This host
has a 48-bit user VA (stack top `0xfffff4948000`), so it is in range. A standalone two-thread probe
compiled with `-fsanitize=thread` ran clean at exit 0 and, with a planted race, produced a full
report and exit **66**.

**A real module builds and runs instrumented.**
`make -k -j1 utf_baselib_basictask TOOLCHAIN=clang2010 VARIANT=debug BL_CLANG_ENABLE_RA_TSAN=1`
completed at rc 0 with `-fsanitize=thread -fno-omit-frame-pointer` on the compile and
`-fsanitize=thread` on the link. The binary ran all five of its cases to completion, Boost.Test
reported `*** No errors detected`, and no object leak was reported.

So the stress case of design §3.8 **can** be run under TSan here. What follows is why its result will
need care.

## 2. What a passing module reports

`utf_baselib_basictask` passes an ordinary build. Under TSan it reports races on **every** run:

| run | warnings | exit |
|---|---|---|
| 1 | 4 | 66 |
| 2 | 3 | 66 |
| 3 | 3 | 66 |

Two distinct defects account for all of them. The count varies between 3 and 4, and which side of the
pair TSan names in the `SUMMARY` moves between `ThreadPoolImpl.h:325` and `ThreadPoolImpl.h:456`,
which is ordinary for a race - the tool reports whichever access it observed second.

**The baseline of this repository under TSan is therefore not clean.** Any future TSan result has to
be read as a delta against a known baseline for the module in question, not as pass/fail on the exit
code. Exit 66 is the sanitizer's, not the test framework's: Boost.Test itself passed all three runs.

## 3. The production defect: `m_threads` is read outside `m_lock`

Three of the four reports in run 1, and both production-side reports in runs 2 and 3, are one defect.

`ThreadPoolImplT` guards its thread vector with a mutex:

    std::vector< cpp::SafeUniquePtr< os::thread > >     m_threads;    // :62
    mutable os::mutex                                   m_lock;       // :65

`createThreads` takes that lock and then **reallocates and appends** to the vector:

    os::mutex_unique_lock guard( m_lock );          // :319
    const auto currentSize = m_threads.size();      // :321  (under the lock)
    ...
    m_threads.reserve( threadCount );               // :325
    m_threads.push_back( ... );                     // :329

Two readers of the same vector do **not** take the lock:

    virtual std::size_t size() const NOEXCEPT OVERRIDE
    {
        return m_threads.size();                    // :440  - no lock
    }

    virtual std::size_t resize( SAA_in const std::size_t threadCount ) OVERRIDE
    {
        BL_CHK( true, m_shuttingDown, ... );        // atomic; guards nothing else
        const auto currentSize = m_threads.size();  // :456  - no lock
        ...
            createThreads( limitThreadCount( threadCount ) );   // :482
        return size();                              // :485  - no lock
    }

The `BL_CHK` before the read tests `m_shuttingDown`, which is a `std::atomic< bool >`; it does not
synchronize `m_threads`.

**The members the two stacks touch are the vector's own `__begin_` and `__end_` pointers.** TSan
reports the racing addresses as `0xfffff2202af0` and `0xfffff2202af8` inside the
216-byte heap block at `0xfffff2202ae0` that is the pool object - adjacent 8-byte slots, which is
exactly the `__begin_`/`__end_` pair that `vector::size()` subtracts. The writer stacks are
`__swap_out_circular_buffer` (from `reserve`, `:325`) and `_ConstructTransaction::~_ConstructTransaction`
(from `push_back`, `:329`); the reader stack is `vector::size()` from `resize` at `:456`.

The asymmetry is explicit in the report. Every write carries `(mutexes: write M0)`, and M0 is the
pool's `m_lock` - TSan names its creation site as `ThreadPoolImpl.h:319`. The reads carry no mutex at
all. This is not a lock-order artifact like the one in
`notes/plans/issues/async-executor-tsan-inversion-record.md`, and it is not a false positive from
pooled objects: it is one side of a shared vector taking the lock and the other side not.

### Why this is not benign

`std::vector::size()` is `__end_ - __begin_`. `reserve` writes both pointers non-atomically. A reader
that lands between the two writes computes a difference over a mismatched pair, so `size()` can
return a value that was never the size of anything - including a very large one, since the
subtraction is unsigned and an updated `__end_` against a stale `__begin_` underflows.

Both unlocked readers then act on that value:

- `resize` decides whether to grow from it (`:456`), so a garbage `currentSize` makes it either skip
  a growth that was asked for, or ask `createThreads` for a size it has already reached;
- `resize` **returns** it (`:485`), and `size()` is public API.

`dispose()` widens the exposure rather than narrowing it. It swaps the vector out under the lock
(`m_threads.swap( threads )`, `:391`) and its own comment at `:370` says the flip must be under the
lock "so it can't race with a concurrent `resize()` pushing new threads into `m_threads`". That
reasoning holds for the writer and was never extended to the readers, so `size()` concurrent with
`dispose()` reads a vector whose pointers are being swapped.

### How it was reached

`Tasks_ThreadPoolResizeAndDisposeTests` (`src/utests/utf_baselib_basictask/TestBaselibBasicTask.h:43`)
starts two threads that call `resize( 8U )` and `resize( 12U )` on one pool (`:69`, `:76`) and joins
them. That is a deliberate concurrency test - its own comment says two concurrent callers growing the
pool must both return - so the race is on the path the test was written to exercise, not an exotic
one. The case asserts `UTF_REQUIRE_EQUAL( 12U, tp -> size() )` at `:84`, which is the value at risk,
though that particular call is after the joins and therefore ordered.

The code is original (`ThreadPoolImpl.h` last changed in `cb431f0`, before that `Initial commit`);
the test was added by the P2 test-coverage work (`ca1218b`). Neither is HTTP/2 work.

## 4. The other report is in the test, not the library

The fourth report is a 1-byte write to `called` at
`src/utests/utf_baselib_basictask/TestBaselibBasicTask.h:127`, from two different pool threads, each
holding a *different* task lock (M0 and M1, both created in `TaskBase::scheduleNothrow`). `Location
is stack of main thread`.

`Tasks_BasicTests` binds the same `cb` lambda into four tasks (`:155`-`:158`) and pushes them onto
the queue together. `called` is a plain `bool` captured by reference, written by whichever pool
threads run those tasks, with no lock in common - and never read afterwards. So the race is real but
the variable is dead: the effect is nil, the report is not.

This one is a test-code defect and a much smaller thing than §3. It is recorded so that a future
reader of a TSan run on this module knows which of the reports is which.

## 5. Why nothing was fixed

F-L0-3 is a validation lane. Its brief forbids changing production or test source, and AGENTS.md
gates modifications of existing core code paths as their own tested change-set. `ThreadPool` is under
essentially everything in the library, so taking `m_lock` in `size()` and in `resize`'s read is a
change whose blast radius is the whole suite - the same class as the four commits of design §3.8,
which the author accepted only on the condition that they are gated.

The fix is not obviously a one-liner either, which is a second reason not to improvise it here:

- `m_lock` is already `mutable`, so guarding `size()` compiles - but `size()` is declared `NOEXCEPT`,
  and acquiring a mutex can throw, so a guard there converts a lock failure into `std::terminate`.
  Inside this header `size()`'s only caller is `resize` at `:485`, which holds no lock, so there is
  no self-deadlock locally; but `size()` is public virtual API on `ThreadPool`, so the audit of who
  calls it and under what is library-wide, not local to the file.
- Making `resize`'s read (`:456`) consistent with the growth decision at `:482` really wants the
  check and the growth under **one** acquisition, which means restructuring `createThreads`'s own
  locking rather than adding a second guard around it.

That is a design decision about `ThreadPoolImpl`'s locking contract, and it belongs to whoever owns
that change-set.

## 6. What this means for the HTTP/2 work

- The stress case **can** be run under TSan here; F-L0-3 phase B is unblocked by this record.
- Its result must be stated as a delta. A race reported inside the multi-operation accounting is a
  finding; a race reported in `ThreadPoolImpl::resize` is this record and is not.
- The suppressions file (`projects/make/toolchain/tsan-suppressions.txt`) is **not** the place to
  silence §3. Its own header says an entry may only be added for a pattern that cannot occur in
  practice, and this one occurs.

## 7. Operational notes for running TSan here

Small things, each of which cost time to find:

- **`TSAN_OPTIONS` is exported by make, not baked into the binary.** `clang-analysis.mk:249` sets
  `second_deadlock_stack=1:suppressions=$(abspath projects/make/toolchain/tsan-suppressions.txt)`,
  and that reaches a binary launched *by make*. A test binary run by hand - which is how the modules
  are run here - gets none of it, and the suppression is silently not applied. Set it explicitly.
- **The sanitizer is not part of the build path.** `PLAT = $(OS)-$(ARCH)-$(TOOLCHAIN)-$(VARIANT)`
  (`platform.mk:7`), so an instrumented build and an ordinary one share
  `bld/ub24-a64-clang2010-debug` and will silently mix objects. Build TSan in a tree with no other
  build in it, or remove `bld/` first.
- **The strip does not cost the stacks.** The link rule strips the binary and attaches a
  `.dbg` via `--add-gnu-debuglink`; TSan's symbolizer follows it, and every frame above resolved to
  file and line.
- Exit code under a report is **66**, and it comes from the sanitizer after Boost.Test has already
  printed `*** No errors detected`. Checking a TSan run therefore needs the same discipline the
  leak check needed: grep for `ThreadSanitizer`, not just the exit code and not just `error`.

## 8. The production defect is closed

Fixed on 2026-09-18 by `dc74102` (the characterization cases) and `31f9463` (the change), as the
gated core change-set §5 asked for.

**The change is two reads.** `size()` takes `BL_MUTEX_GUARD( m_lock )` before returning
`m_threads.size()`, and `resize()`'s own read at `:456` goes through the now-locked `size()` instead
of reading the vector directly. `return size()` at `:485` needed nothing once `size()` locks.

**§5's two reasons for thinking this was not a one-liner did not survive contact with the code.**

- *"`size()` is `NOEXCEPT` and acquiring a mutex can throw."* True but not an obstacle:
  `ExecutionQueueImpl` already has five `NOEXCEPT OVERRIDE` getters which take
  `BL_MUTEX_GUARD( m_lock )` directly, including a `size()` of exactly this shape
  (`ExecutionQueueImpl.h:1219`). The fix follows them, and does **not** wrap the body in
  `BL_NOEXCEPT_BEGIN`/`BL_NOEXCEPT_END`: `os::mutex` is `std::mutex` (`OSBoostImports.h:95`), whose
  `lock()` only fails when the process state is already broken, and a bare `NOEXCEPT` escape and a
  `BL_RIP_MSG` both abort after printing `what()`. The value-returning form of that macro also needs
  a result variable outside the block, which would turn a two-line getter into six.
- *"Making `resize`'s read consistent with the growth decision really wants the check and the growth
  under one acquisition."* It does not, and it must not: `m_lock` is not recursive and
  `createThreads` acquires it itself, so `resize` must not hold the lock across that call.
  `createThreads` already re-reads the size under the lock (`:321`) and makes the authoritative
  decision there, so `resize`'s read is advisory - it only picks the branch and the message logged in
  it. `size()` has no other caller inside the class, and while `m_lock` is held the class makes no
  outbound call which can re-enter it, so locking the getter cannot self-deadlock.

**One correction to the reasoning, which does not change the fix.** "The pool only ever grows" is
true of `createThreads`, the only writer which appends, but not of the vector: `disposeInternal`
swaps it out (`:391`), taking it to empty. So a concurrent `dispose()` can make `resize`'s advisory
read stale *high*, not only low. It is harmless in both directions - stale low makes `createThreads`
re-decide correctly under the lock, and stale high only skips a growth while the pool is being
disposed, which is the wanted outcome - but the safe-direction argument rests on `createThreads`
re-deciding under the lock, not on monotonic growth.

**`aioService()` is deliberately still unlocked.** It reads `m_ioservice`, which `disposeInternal`
resets under the lock (`:433`), so it is racy in the same family. A guard there would not fix it: the
function returns a reference to the pointee, so the lock would be released before the caller touched
the object. What protects that call is the disposable lock, and changing it is a different design
decision. TSan does not report it on this module's baseline.

### The measured delta, `utf_baselib_basictask`, `BL_CLANG_ENABLE_RA_TSAN=1`

| tree | runs | `data race` total | of which `ThreadPoolImpl` | of which test code (§4) |
|---|---:|---|---:|---:|
| unmodified (§2's baseline, re-measured) | 3 | 3, 3, 3 | **2, 2, 2** | 1 each |
| with the characterization cases, before the fix | 3 | 6, 6, 8 | **5, 5, 7** | 1 each |
| after the fix | 5 | 1, 1, 1, 1, 1 | **0** | 1 each |

The counts rise in the middle row because `Tasks_ThreadPoolConcurrentSizeReadTests` is the first case
to call `size()` concurrently with growth, so `ThreadPoolImpl.h:440` starts appearing in the
`SUMMARY` lines alongside `:325` and `:456`. After the fix none of the three appears as an access in
any report; `ThreadPoolImpl` frames remain only as intermediate stack frames of §4's report.

Boost.Test passed on every run above except one: **the characterization case caught the defect for
real**, failing 1 instrumented run of 3 on `EQUAL( 0U, outOfRange.load() )` - `size()` returned a
value outside `[ 2, 12 ]`, which is §3's "a value that was never a size" observed rather than argued.
The same binary built without the sanitizer passed 5 runs of 5 before the fix, so the instrumented
`-O0` build is what widens the window; the defect is the same one either way.

### What §6 now means

Its guidance inverts for this module: a race reported in `ThreadPoolImpl::size` or
`ThreadPoolImpl::resize` **is** a finding from now on - it would be a regression of `31f9463`, not
this record. §4's report at `TestBaselibBasicTask.h:127` is what remains of the baseline, and it is
still the reason a TSan result on `utf_baselib_basictask` must be read as a delta rather than as
pass/fail on the exit code.
