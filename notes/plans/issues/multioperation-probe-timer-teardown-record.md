# The multi-operation stress case is clean under TSan; its probe has a teardown defect

**Origin:** follow-up **F-L0-3** phase B of the HTTP/2 client plan
(`notes/plans/http2-implementation-plan.md` §2), which asks for the multi-operation stress case of
design §3.8 commit 1 to be run under ThreadSanitizer. **Date:** 2026-09-18. **Status:** the
concurrency question is answered - **the accounting is clean**; the teardown defect in the test probe
is OPEN and deliberately not fixed here.

Run against the merged L0 follow-up tip `1d88c3f` (F-L0-1 `da9a444` + F-L0-2 `e2a0830`), so this is
the code that ships: `MultiOperationTaskT` in its `template< typename BASE = TaskBase > : public BASE`
form, and `TestMultiOperationTask.h` in `utf_baselib_tasks2`.

Host capability, the TSan operational notes and the separate `ThreadPoolImpl` race are in
`notes/plans/issues/tsan-baseline-and-threadpool-resize-race-record.md`. That record established that
the repository's TSan baseline is **not** clean, which is why what follows is stated as a delta and
not as an exit code.

---

## 1. The answer: no data races in the multi-operation accounting

`utf_baselib_tasks2`, built `clang2010 debug` with `BL_CLANG_ENABLE_RA_TSAN=1`, run as

    utf-baselib-tasks2 --run_test=Tasks_MultiOperationTaskMultiThreadedTests --log_level=test_suite

The case entered and left, Boost.Test reported `*** No errors detected`, nothing `leaked`.
ThreadSanitizer reported **16 warnings and exit 66**, of which:

| class | count |
|---|---|
| `data race` | **0** |
| `lock-order-inversion` | **0** |
| `heap-use-after-free` | 9 |
| `use of an invalid mutex (e.g. uninitialized or destroyed)` | 6 |
| `unlock of an unlocked mutex (or by a wrong thread)` | 1 |

There is no data-race report, so no report names `m_pendingOperations`, `m_closing`,
`m_closeInitiated`, `m_terminalTaken`, `m_firstError` or `m_firstErrorIsExpected` - the six members at
`MultiOperationTask.h:103`-`:108` that `m_operationsLock` (`:102`) guards. The leaf-lock design those
members sit behind is what design §3.8 wanted checked, and under a 4-thread pool it holds.

`ThreadPoolImpl` frames do appear in the stacks, because the pool owns the `io_service` the probe's
timers are built against, but they produce no race here either.

## 2. How that was distinguished from the 16 reports

The exit code cannot carry this conclusion: 66 is the sanitizer's, and it prints after Boost.Test has
already gone green. Three independent discriminators separate the reports from the accounting, and
each would be enough on its own.

**Nothing but the main thread is implicated.** The log carries 18 thread attributions across the 16
reports, and every one of them reads `by main thread`; the string `thread T` does not occur once.
(The nine `heap-use-after-free` reports carry access lines - `Read of size 8` x7, `Read of size 1`,
`Atomic read of size 1`; the seven mutex-misuse reports carry a stack and no access line.) A data
race requires two threads.

**The single-threaded sibling produces the identical report multiset.**
`Tasks_MultiOperationTaskSingleThreadedTests` runs the same `runMultiOperationSuite` with
`threadsCount = 1`. It reports **the same 16 warnings**; the sorted `SUMMARY` multisets of the two
runs are identical. With one thread the accounting has no concurrency to get wrong, so nothing the
tool reports here depends on the thread count.

**A different case in the same module and harness is clean.**
`TcpPreHandshakeStage_CancelDuringStageTests`, same binary, same instrumentation, same Boost.Test
harness: **0 warnings, exit 0**. So the 16 are specific to the multi-operation probe, not
module-wide noise, and not the sanitizer failing to cope with the harness.

## 3. What the 16 reports are

All of them land in Boost.Asio's deadline-timer teardown - `deadline_timer_service::cancel`,
`timer_queue::cancel_timer`, `epoll_reactor::cancel_timer`, `conditionally_enabled_mutex` and
`posix_mutex::lock`/`unlock` - reached from

    ~basic_deadline_timer -> io_object_impl::~io_object_impl -> deadline_timer_service::destroy -> cancel

The probe holds its timers by value in a vector of owning pointers:

    std::vector< bl::cpp::SafeUniquePtr< bl::asio::deadline_timer > >   m_timers;   // :549
    ...
    m_timers[ i ].reset( new asio::deadline_timer( aioService ) );                  // :689

`aioService` is the thread pool's `io_service`. So each timer holds a reference to a service it does
not own, and the reports say that when the timer is destroyed, that service - or the mutex inside it -
has already been freed. This is a **destruction-order defect**: the timers outlive the asio service
they were constructed against.

The mechanism is **structural and deterministic**, and no exception is involved in it. All line
numbers below are as of the run commit `1d88c3f`; the header has grown since, so they do not match
the current tip.

`runMultiOperationProbe` (`:769`) creates the probe at `:779`, creates the thread pool **as its own
local** at `:781` - `const auto tpLocal = om::lockDisposable( ThreadPoolImpl::createInstance ... )` -
and ends with `return taskImpl;` at `:808`. So on every return the local `ObjPtrDisposable< ThreadPool >`
disposes the pool, which destroys the `io_context` and with it the `deadline_timer_service` the timers
were built against, and the probe is handed back **alive, with `m_timers` still populated**. Each
caller then holds it in a `const auto taskImpl` until its own scope ends and destroys it there. That
destruction is the read, and it is the same in all nine `heap-use-after-free` reports:
`deadline_timer_service::cancel` / `timer_queue::cancel_timer` <- `~basic_deadline_timer` <- the
`~vector` of `m_timers` <- `~MultiOperationProbeT` (`:532`) <- `~ObjPtr< ... >` <-
`runMultiOperationSuite` at a scenario's closing brace. **No exception frame appears on the read side
of any report.**

The free side splits 5/4, and the split is not a second mechanism:

- **Five** (log lines 189, 307, 524, 644, 963) carry a 41-frame free stack that names the freed block
  outright: `operator delete` <- `execution_context::allocator_impl::deallocate` <-
  `service_registry::destroy_allocated< deadline_timer_service >` <- `service_registry::destroy_services`
  <- `~execution_context` <- `~io_context` <- `ThreadPoolImplT::disposeInternal` <- `dispose` <-
  `~ObjPtrDisposable< ThreadPool >` <- **`runMultiOperationProbe`** <- `runMultiOperationSuite:924`.
  What was freed is the `deadline_timer_service` itself, and what freed it is the helper's return.
- **Four** (log lines 49, 118, 1483, 1552) carry a 20-frame stack whose frame `#0` is the `free`
  interceptor - not `operator delete` - called from `__cxa_end_catch`. Frame `#0` alone shows this is
  a *different* deallocation, not the same one rendered differently. These four belong to the two
  scenarios that end with `UTF_REQUIRE_THROW_MESSAGE( ... )` immediately before the closing brace
  (`:898`-`:902` and `:987`-`:991`), while the scenario behind the five long stacks (`:924`-`:930`)
  contains no throw at all. So what `__cxa_end_catch` frees is the caught exception object, allocated
  over the service's already-freed memory and freed again before the probe is destroyed; ThreadSanitizer
  prints the *last* free recorded for a granule, so that later free masks the service's own. The reads
  are at the same two fields of the same object either way. These stacks are also missing the
  `runMultiOperationSuite` frame between `__cxa_end_catch` and `test_method:1087`, so they are
  truncated, and inconsistent with a real free at that point in a `-O0` build.

The single-threaded control run (`phaseB-control-singlethreaded-tsan.log`) reproduces the split
exactly - the same three scenarios, the same 2/5/2 distribution, the same 20/41 frame counts, the same
frame `#0` in each, and the same two addresses for the four short-stack reports. The split is
deterministic and independent of the thread count.

The reports fall in three batches (log lines 49-118, 189-1374 and 1483-1552 of 1628) at three scenario
scope ends - `:903`, `:930` and `:992` - not a single end-of-process teardown. All seven mutex-misuse
reports sit in the middle batch, so `:930` accounts for 12 of the 16. Seven of the suite's eight
scenarios call the helper and so all seven carry the defect; only three report it, because
ThreadSanitizer can call the read a use-after-free only while the freed region is still unallocated at
that moment, and in the other four it has been handed to a live allocation in between. That is a
property of when the tool can see the defect, not of whether it is there.

## 4. Why it was not fixed

F-L0-3 is a validation lane; its brief forbids changing production or test source. That is the whole
reason, and not the size of the fix: with the mechanism pinned, the fix is small and mechanical. The
pool has to stay alive as long as the probe, and exactly one place decides otherwise -
`runMultiOperationProbe` owns the pool as a local (`:781`) and returns only the probe (`:808`). Either
hand the pool back alongside the probe or create it in `runMultiOperationSuite` and pass it in: one
helper signature and its seven call sites, with the probe itself untouched. It should still be made by
someone who can re-run the whole `utf_baselib_tasks2` module behind it.

It is worth saying plainly that this is a **test-harness defect, not a library one**. Nothing here
implicates `MultiOperationTask`, `TaskBase` or `ThreadPool`. But it is a genuine use-after-free, not
cosmetic, and it is the reason a clean accounting still exits 66.

## 5. What this means

- Design §3.8 commit 1's stress row is **satisfied**: the multi-operation accounting has been run
  under ThreadSanitizer, on a 4-thread pool, and is free of data races.
- Anyone re-running this module under TSan will see 16 warnings and should not re-derive the above.
  The number to watch is the **`data race` count, which must stay 0**; a change in the other 16 is a
  change in the probe's teardown, and a `data race` appearing at all is a real regression.
- These reports must **not** go into `projects/make/toolchain/tsan-suppressions.txt`. That file is
  for patterns that cannot occur in practice; this one occurs, and it is fixable in the test.
