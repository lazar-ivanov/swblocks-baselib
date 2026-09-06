# Process Spawn Descriptor Sweep Cost: Deferral Record

This document records a performance issue observed while fixing the review findings O-3 and O-4
(the UNIX `createProcess` child section) and the decision **not** to change it in that round. It
is a risk acceptance with a named trigger, not an assessment that the cost is acceptable
everywhere.

**Origin:** not a review finding. Observed on 2026-09-05 while verifying O-3/O-4
(`notes/reviews/major/update_2026/whole-library-cxx-review-fable51-decisions.md`, O-3 and O-4
"Decided" entries) on the Linux devenv7 host.

**Location:** `src/include/baselib/core/detail/OSImplUNIX.h`, `execChildProcessNothrow`, the
close-on-exec sweep:

```
for( int fd = info.maxFd; --fd > STDERR_FILENO; )
{
    tryMakeFileDescriptorPrivate( fd );
}
```

`info.maxFd` is `::getdtablesize()` (the soft `RLIMIT_NOFILE`), captured by the parent before the
fork. The sweep pre-dates the O-3/O-4 change; the rewrite kept it as is, only moving it into the
async-signal-safe child helper.

---

## Decision

**Date:** 2026-09-05
**Status:** item 1 **fixed on Linux on 2026-09-06** (plan `process-spawn-fd-sweep-perf-plan.md`,
option 1); item 2 (Darwin) remains **deferred**

| # | Item | Disposition |
|---|---|---|
| 1 | One `fcntl` call per possible descriptor on every spawn (a second one only for the few that are open), bounded by the soft `RLIMIT_NOFILE`, not by the number of open descriptors | **Fixed 2026-09-06** on Linux 5.11+: `close_range( 3, ~0U, CLOSE_RANGE_CLOEXEC )` through a raw `syscall()` in `tryMarkAllCloseOnExecNothrow`; the loop remains the run-time fallback on kernels that return `ENOSYS` (older than 5.9) or `EINVAL` (5.9 and 5.10). Tests `BaseLib_OSCreateProcessDescriptorLimitTests` (timing under a raised soft limit; failed before the fix) and `BaseLib_OSCreateProcessDescriptorHygieneTests` (no descriptor leaks, including one opened concurrently) in `utf_baselib` |
| 2 | Same cost on Darwin, where the same loop runs | **Deferred**: Darwin is compiled out of the new call and keeps the loop, byte for byte. The path when a Darwin deployment raises the descriptor limit is option 2 below (parent-side enumeration with `proc_pidinfo`), with its documented race; the default macOS soft limit of 256 makes the loop cost microseconds today |

## Outcome (2026-09-06, Linux)

Measured on the same host and binary configuration as the table below (gcc1520 debug), soft
limit 1048576:

| Measurement | before | after |
|---|---|---|
| Average spawn (`BaseLib_OSCreateProcessDescriptorLimitTests`, 5 spawns of `true`) | about 180 ms | 2 ms |
| `BaseLib_OSCreateProcessTests` (4 spawns) | 0.75 s | 0.22 s |
| `BaseLib_OSCreateProcessExecFailureWhileLoggingTests` (42 spawns, 4 threads logging) | 15.9 s | 1.1 s |
| `strace -c` over `BaseLib_OSCreateProcessTests`: `fcntl` calls | 4,194,309 | 8 (the comm-pipe check, two per spawn) |
| `strace -c` over the same: `close_range` calls | 0 | 4 (one per spawn) |

Descriptor hygiene is unchanged (`BaseLib_OSCreateProcessDescriptorHygieneTests` passes before
and after: a child started with a redirect sees only descriptors 0, 1, 2 and its own directory
handle, with ten non-close-on-exec descriptors held by the parent and another thread opening
and closing descriptors throughout the spawn).

---

## What was measured

On the Linux devenv7 host (Ubuntu 24, arm64, 2 cores) the soft descriptor limit is 1048576, so
every spawn performs about 1.05 million `fcntl` system calls in the child before `execvp`
(`strace -c` over the four-spawn `BaseLib_OSCreateProcessTests`: 4,194,309 `fcntl` calls,
4,194,283 of them failing with `EBADF` on closed descriptors).
Measured with the `utf_baselib` process tests:

| Measurement (same binary, soft limit set with `ulimit -n`) | at 1024 | at 1048576 |
|---|---|---|
| `BaseLib_OSCreateProcessTests` (4 spawns) | 0.02 s | 0.75 s |
| `BaseLib_OSCreateProcessDetachedReleaseTests` (2 to 3 spawns) | 0.12 s | 0.39 s |
| `BaseLib_OSCreateProcessExecFailureWhileLoggingTests` (42 spawns, 4 threads logging) | 4.3 s | 15.9 s |
| Cost per spawn attributable to the sweep | under 1 ms | 130 to 280 ms |

(`BaseLib_OSCreateProcessRedirectedTests` takes about 20 s at either limit; that time is spent
in the test's own waits, not in the sweep. An earlier version of this record attributed it to
the sweep; corrected 2026-09-05 after measuring at both limits.)

With a conventional interactive soft limit of 1024 the same sweep costs well under a millisecond,
which is why the cost was never noticed: it only appears under the large soft limits that
systemd services, containers (Docker's default `nofile` is 1048576) and some developer shells set.
Investigation plan: `process-spawn-fd-sweep-perf-plan.md`, next to this file.

## Why it was deferred on 2026-09-05 (assessment kept as written; option 1 was applied on 2026-09-06)

- **It is a cold path.** `createProcess` is used by `Pinger`, the tests and downstream callers
  that run external tools; nothing in the library spawns in a hot loop.
- **The fix is not trivial inside the child.** After `fork()` in a threaded process only
  async-signal-safe calls are allowed (the O-4 fix exists precisely to enforce that), so the
  usual "iterate `/proc/self/fd`" replacement cannot run in the child: `opendir`/`readdir`
  allocate. The candidates are:
  1. `close_range( 3, ~0U, CLOSE_RANGE_CLOEXEC )` (Linux 5.11+, glibc 2.34+; a single system
     call, async-signal-safe), with the current loop as the fallback when the call is missing
     (`ENOSYS`) or on Darwin.
  2. The parent enumerates its open descriptors before the fork (`/proc/self/fd` on Linux,
     `proc_pidinfo( PROC_PIDLISTFDS )` on Darwin) into a fixed buffer in `ChildExecInfo`, and the
     child marks only those. Correct on both platforms, but racy against descriptors opened by
     other threads between the enumeration and the fork; those would be inherited by the new
     program (today they are caught by the sweep). Acceptable only with a `close_range` first
     choice on Linux.
  3. Marking every descriptor the library creates `O_CLOEXEC` at creation and dropping the sweep.
     Rejected: descriptors created by third-party code in the same process (JVM, OpenSSL, Boost
     Asio without `SOCK_CLOEXEC`) would leak into every child, which is the daemon-hygiene
     property the sweep exists to guarantee.
- **Mixing it into O-3/O-4 would have widened a change that was already the core spawn path.**

## What limits the exposure

- Spawn latency only; no descriptor leak and no functional change.
- Proportional to the soft limit, which a deployment controls (`LimitNOFILE=` for a service,
  `ulimit -n` for a shell), and which is commonly 1024 for interactive use.
- The parent thread is blocked for the duration only when `WaitToFinish` or a redirect callback
  is used; a detached spawn returns as soon as the child has passed the exec (the comm pipe
  closes on a successful exec), which is after the sweep in both cases.

## Fix applied on Linux (2026-09-06) and the remaining Darwin path

Option 1 as applied: in `execChildProcessNothrow`, `tryMarkAllCloseOnExecNothrow` issues
`syscall( __NR_close_range or 436, 3, ~0U, CLOSE_RANGE_CLOEXEC or 1U << 2 )` (raw syscall so
the build does not depend on glibc 2.34 headers; constants defined locally when the headers
lack them) and the previous loop runs only when that returns false (`ENOSYS`, `EINVAL`, or
Darwin, where the call is compiled out). The explicit `fcntl` check on the comm pipe
descriptor is kept, since it must stay close-on-exec for the parent's "EOF means success"
protocol. Details, platform matrix and measurements: `process-spawn-fd-sweep-perf-plan.md`.

Still open (item 2): on Darwin the loop is unchanged. If a Darwin deployment ever raises the
descriptor limit, the path is option 2 (the parent enumerates its descriptors with
`proc_pidinfo( PROC_PIDLISTFDS )` before the fork into a fixed buffer in `ChildExecInfo`, the
child marks only those), accepting or closing its race against descriptors opened by another
thread between the enumeration and the fork.

## How it was validated (Linux) and how to validate the Darwin path

- Linux, done: `BaseLib_OSCreateProcessDescriptorLimitTests` (soft limit raised to the hard
  limit; average spawn under 20 ms; failed before the fix) and
  `BaseLib_OSCreateProcessDescriptorHygieneTests` (redirected child sees exactly 0, 1, 2 and its
  own directory handle with ten non-close-on-exec descriptors held and another thread opening
  descriptors during the spawn); the O-3/O-4 cases and the existing process cases green on
  `gcc1520` and `clang2010`, debug and release; `strace -c` shows 4 `close_range` and 8 `fcntl`
  calls for four spawns. (An earlier version of this section expected
  `BaseLib_OSCreateProcessRedirectedTests` to speed up; its 20 s are the test's own waits and
  did not change.)
- Darwin, when item 2 is picked up: the same two test cases on the macOS devenv (the timing case
  needs a raised hard limit to be meaningful there), plus the existing process cases.

## Conditions to revisit (item 2 and the fallback)

- A Darwin deployment raises the descriptor limit, or a spawn-heavy Darwin user reports spawn
  latency: implement option 2 for Darwin.
- A Linux deployment on a kernel older than 5.11 (fallback loop) with a large `LimitNOFILE`
  reports spawn latency: option 2b (parent computes the highest open descriptor, the child
  sweeps only up to it) is the faster fallback, with its documented race.
- Every supported Linux baseline reaches glibc 2.34+: the raw `syscall()` form can become the
  `close_range()` wrapper (cosmetic).
- Any further work on `execChildProcessNothrow` (O-5 is the next item in that function): keep
  the helper-then-fallback shape.

## References

- O-3/O-4 decision entries:
  `notes/reviews/major/update_2026/whole-library-cxx-review-fable51-decisions.md`
- Related pending item in the same function: O-5 (`PR_SET_PDEATHSIG` thread binding, `SIGPIPE`)
