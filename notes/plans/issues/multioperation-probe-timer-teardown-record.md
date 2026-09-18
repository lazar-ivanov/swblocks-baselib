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

The reports are spread through the run (log lines 49 to 1552 of 1628), in batches rather than all at
the end, which matches one batch per scenario of `runMultiOperationSuite` as each scenario's probe
goes out of scope - not a single end-of-process teardown.

The freeing stack in each case is `free` from `__cxa_end_catch`, inside the test case's own frame
(`TestMultiOperationTask.h:1087`). The probable mechanism is therefore that destroying the caught
exception at the end of one of the throwing scenarios releases the last reference keeping the pool -
and with it the `io_service` - alive, and the timers are destroyed after that. **This is stated as the
probable mechanism, not as a proven one**: what is proven is the ordering (timers destroyed against a
freed service), not which reference was the last one.

## 4. Why it was not fixed

F-L0-3 is a validation lane; its brief forbids changing production or test source. Beyond that, the
fix belongs to whoever owns the probe: the ordering wants the timers destroyed (or at least
`m_timers.clear()`ed) while the pool is still alive, which is a change to the probe's scope
structure, not a one-line edit, and it should be made by someone who can re-run the whole
`utf_baselib_tasks2` module behind it.

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
