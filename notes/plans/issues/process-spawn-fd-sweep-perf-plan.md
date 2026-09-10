# Process Spawn Descriptor Sweep: Investigation and Fix Plan

## Context

Every `os::createProcess` on UNIX forks, and the child marks every descriptor from 3 up to the
soft `RLIMIT_NOFILE` close-on-exec with an `fcntl` call each (a second one for the open ones) before `execvp`
(`src/include/baselib/core/detail/OSImplUNIX.h`, `execChildProcessNothrow`, the loop
`for( int fd = info.maxFd; --fd > STDERR_FILENO; ) tryMakeFileDescriptorPrivate( fd );` with
`info.maxFd = ::getdtablesize()`). The loop is the library's only guarantee that descriptors
owned by third-party code in the same process (JVM, OpenSSL, Boost Asio sockets created without
`SOCK_CLOEXEC`, the library's own `pipe()` ends) do not leak into the new program. It pre-dates
the 2026-09-05 O-3/O-4 rewrite, which kept it unchanged and recorded its cost in
`process-spawn-fd-sweep-perf-deferral.md`.

This plan is the investigation that record asks for: what a fix entails, what it buys, what it
risks, and a recommendation. Nothing here is implemented yet.

**Already decided, not re-opened:** the sweep itself stays (option 3 below, dropping it in
favour of `O_CLOEXEC` at creation, was rejected in the deferral record because it cannot cover
third-party descriptors). The child must remain async-signal-safe (O-4).

## Summary

| Recommendation | Risk | Blast radius | Cost of testing | Complexity | Cost of implementation |
|---|---|---|---|---|---|
| Fix now: `close_range( 3, ~0U, CLOSE_RANGE_CLOEXEC )` through a raw `syscall()` in the child, falling back to today's loop on `ENOSYS`/`EINVAL`; Darwin keeps the loop | Low (one system call whose failure path is the current code) | every UNIX `createProcess` caller (in-tree: `tasks/utils/Pinger.h`, the tests, `UtfMain.h`; downstream unknown) | `utf_baselib` 4 units + 2 new cases (timing under a raised soft limit; descriptor hygiene under a redirect) + `strace` count | Low | 2 h (about 30 lines plus tests) |

## What was measured (2026-09-05, Linux devenv7 host, arm64, 2 cores, gcc1520 debug)

| Test (same binary, soft limit set with `ulimit -n`) | at 1024 | at 1048576 |
|---|---|---|
| `BaseLib_OSCreateProcessTests` (4 spawns) | 0.02 s | 0.75 s |
| `BaseLib_OSCreateProcessDetachedReleaseTests` (2 to 3 spawns) | 0.12 s | 0.39 s |
| `BaseLib_OSCreateProcessExecFailureWhileLoggingTests` (42 spawns, 4 threads logging) | 4.3 s | 15.9 s |

Per spawn the sweep costs 130 to 280 ms at the 1M limit (the upper end under CPU contention)
and under a millisecond at 1024. The cost is one system call per possible descriptor, so it is
proportional to the soft limit, not to the number of open descriptors. `strace` on the four-spawn
test confirms the call volume (see "Evidence" at the end).

## Why it should be fixed (benefits)

1. **Deployments commonly run at the large limit.** Docker's default `nofile` is 1048576 for
   both soft and hard, systemd units frequently set `LimitNOFILE=1048576`, and this developer
   host has it. In those environments every spawn pays 130 to 280 ms of pure system-call
   overhead before the new program starts. The library was written and tested where the soft
   limit was 1024, so the cost was invisible.
2. **The parent is blocked for that time whenever it waits or redirects.** `WaitToFinish`,
   `createRedirectedProcessAndWait` and every redirect callback sit behind the exec, so a
   thread-pool thread that runs an external tool loses the sweep time on every call. In-tree
   this is `Pinger` and the tests; downstream tooling that shells out (packaging, hashing,
   certificate tools) pays it on each invocation.
3. **CPU burn, not just latency.** A million system calls per spawn is 0.15 to 0.3 core-seconds
   each; a host that spawns a few times per second spends a core on `fcntl`.
4. **Detached daemons start late.** A detached child's exec is delayed by the same amount.
5. **Test-suite time.** The process tests in `utf_baselib` are dominated by the sweep at the
   1M limit (the 42-spawn logging stress drops from 16 s to 4 s), and any future spawn-heavy
   test inherits the cost.
6. **The fix cannot weaken hygiene.** `CLOSE_RANGE_CLOEXEC` does in one call exactly what the
   loop does; where the kernel lacks it, the loop remains. No descriptor that is hidden today
   becomes visible.

What it does **not** buy: correctness. The sweep is correct; this is latency and CPU only,
which is why it was deferred rather than folded into O-3/O-4.

## Options investigated

| # | Option | Cost per spawn | Hygiene | Signal-safe in the child | Portability | Verdict |
|---|---|---|---|---|---|---|
| 1 | `close_range( 3, ~0U, CLOSE_RANGE_CLOEXEC )` via `syscall( __NR_close_range, ... )`; fall back to the loop on failure | one system call | identical to today (kernel marks every open descriptor in the range) | yes (a bare system call) | Linux 5.11+ for the flag (5.9/5.10 return `EINVAL`, older `ENOSYS`); Darwin has no equivalent | **Recommended** |
| 2 | Parent enumerates its open descriptors before the fork (`/proc/self/fd` on Linux, `proc_pidinfo( PROC_PIDLISTFDS )` on Darwin) into a fixed buffer in `ChildExecInfo`; the child marks only those | proportional to open descriptors | **racy**: a descriptor opened by another thread between the enumeration and the fork is missed and leaks into the new program; today it is caught | yes (the child only reads the buffer) | both | acceptable only as the Darwin path, and only if Darwin cost ever matters; not now |
| 2b | Variant of 2: parent computes the highest open descriptor number and the child sweeps `[3, highest]` | proportional to the highest number in use | same race for numbers above the snapshot | yes | both | same verdict as 2 |
| 3 | `O_CLOEXEC` / `SOCK_CLOEXEC` at creation everywhere and no sweep | zero | **broken** for third-party descriptors (JVM, OpenSSL, Asio) | n/a | both | rejected (deferral record) |
| 4 | Leave as is; document `ulimit -n` / `LimitNOFILE` guidance | none | unchanged | n/a | both | the status quo; loses benefits 1 to 5 |

**Why option 1 is safe to prefer.** Its failure path is the current loop, so the worst case on
any kernel is today's behaviour. `close_range` has had the same syscall number (436) on every
Linux architecture since it was added, so a local `#define` covers Ubuntu 20.04's older kernel
headers (its glibc 2.31 has no wrapper either; 2.34+ does, and both `g++` and `clang++` predefine
`_GNU_SOURCE`, so on Ubuntu 24 the `<unistd.h>` prototype is visible, but the raw `syscall()`
form is used regardless so one code path serves every supported Linux).

**Darwin.** The default soft limit on macOS is 256 (10240 at most in common configurations), so
the loop costs microseconds there. The plan leaves Darwin on the loop; option 2 is the recorded
path if a Darwin deployment ever raises the limit.

## Platform matrix

The selection between the new call and the existing loop is made **at run time, in the child,
per spawn**, never at build time. One binary therefore behaves correctly on every kernel it may
run on, which matters for a build made on Ubuntu 24 that runs in a container on an older host
kernel.

| Platform | What runs | Behaviour change | Cost per spawn |
|---|---|---|---|
| Linux, kernel 5.11 or newer | `close_range( 3, ~0U, CLOSE_RANGE_CLOEXEC )` succeeds; the loop is skipped | faster only; every descriptor the loop hid is hidden | one system call |
| Linux, kernel 5.9 or 5.10 | the call exists but not the flag: `EINVAL`; the loop runs | none (today's code) | one failed call plus today's loop |
| Linux, kernel older than 5.9 | no such call: `ENOSYS`; the loop runs | none (today's code) | one failed call plus today's loop |
| Darwin | compiled out (`#ifdef __linux__`); the loop runs | none | today's loop (soft limit is 256 by default, so microseconds) |
| Windows | not applicable: `CreateProcessW`, no fork, no sweep | none | n/a |

Notes:

- The probe on an old kernel costs one failed system call, microseconds against the loop's
  hundreds of milliseconds, so the result is not cached. Caching would have to happen in the
  parent anyway (the child must not touch shared state), and it would buy nothing measurable.
- The raw `syscall()` form with a locally defined number (436, the same on every Linux
  architecture since the call was added) and flag (`1U << 2`) is used deliberately: the glibc
  wrapper exists only from 2.34 and would tie the build to newer headers, whereas the raw form
  compiles against Ubuntu 20.04's headers and degrades at run time exactly as the table says.
- Darwin is unchanged by this work. Its improvement, if a Darwin deployment ever raises the
  descriptor limit, is option 2 (parent-side enumeration with `proc_pidinfo`), which stays
  recorded as item 2 of `process-spawn-fd-sweep-perf-deferral.md` together with its known race
  (a descriptor opened by another thread between the enumeration and the fork is missed).

## What the fix entails

**Location:** `src/include/baselib/core/detail/OSImplUNIX.h`, `execChildProcessNothrow` and one
new private static helper next to it. About 30 lines.

1. A helper `tryMarkAllCloseOnExecNothrow( int fromFd ) NOEXCEPT`:
   - `#ifdef __linux__`: define `BL_NR_close_range` as `__NR_close_range` when the headers have
     it, else `436`; define `BL_CLOSE_RANGE_CLOEXEC` as `CLOSE_RANGE_CLOEXEC` when present, else
     `( 1U << 2 )`; call `::syscall( BL_NR_close_range, fromFd, ~0U, BL_CLOSE_RANGE_CLOEXEC )`;
     return `true` on 0. On `-1` with `ENOSYS` (no syscall) or `EINVAL` (syscall without the
     flag, kernels 5.9 and 5.10) return `false`. Any other error also returns `false` (the loop
     is always a correct fallback).
   - Otherwise (Darwin) return `false`.
2. In `execChildProcessNothrow`, replace the loop with: if the helper returns `false`, run the
   existing loop. Keep the explicit `tryMakeFileDescriptorPrivate( info.fdComm )` check after it:
   the comm pipe must be close-on-exec for the parent's "EOF means success" protocol, and the
   check costs nothing.
3. `info.maxFd` stays (the fallback needs it).
4. No change to the parent, to `ChildExecInfo`, to the O-3 `/dev/null` handling, or to the
   redirect logic: the standard descriptors 0 to 2 are excluded from the range exactly as they
   are excluded from the loop.
5. Comment in the code naming the kernel versions and the fallback, so the next reader does not
   remove the loop.

Design intent preserved: the sweep's purpose ("hide every descriptor from the new program") is
kept; the O-4 constraint (only async-signal-safe calls in the child) is kept, since `syscall()`
is a bare trap.

## Test plan

**Existing regression (must stay green):** `BaseLib_OSCreateProcessTests`,
`BaseLib_OSCreateProcessRedirectedTests`, `BaseLib_OSTryAwaitTerminationTests`,
`BaseLib_OSTerminateProcessTree`, the three O-3/O-4 cases
(`BaseLib_OSCreateProcessDetachedStdioTests`, `BaseLib_OSCreateProcessDetachedReleaseTests`,
`BaseLib_OSCreateProcessExecFailureWhileLoggingTests`), on `gcc1520` and `clang2010`, debug and
release. These already exercise every branch of the child helper except the new call.

**New case 1, timing under a raised limit (proves the fix; fails before it).** In the test,
`getrlimit( RLIMIT_NOFILE )`; if the hard limit is below 65536 skip with a message (the fix is
then unobservable on that host); otherwise `setrlimit` the soft limit to the hard limit for the
duration of the case (restored on scope exit), spawn `true` five times with `WaitToFinish`, and
require the average spawn under 20 ms. Before the fix this measures 130 ms or more at a 1M limit;
after it, a few milliseconds. `getdtablesize()` follows the soft limit, so the child sees the
raised value.

**New case 2, hygiene unchanged (must pass before and after).** Open ten descriptors on
`/dev/null` without `O_CLOEXEC` and hold them; start a thread that opens one more immediately
before the spawn; spawn `bash -c 'ls /proc/self/fd | wc -l'` with `RedirectStdout` and read the
count. Expected 4 (0, 1, 2 and the directory descriptor `ls` opens). This pins that the new call
hides everything the loop hid, including a descriptor created concurrently.

**Fallback path.** The fallback is today's loop, already covered by every existing case. No
test hook is proposed: forcing `ENOSYS` would need a production knob or a test-only static, and
the loop's behaviour is unchanged by this work. If a hook is wanted anyway, a
`BL_IS_UNIT_TEST_BINARY`-guarded static flag read by the parent before the fork is the cheapest
form.

**Evidence outside the test binary.** `strace -f -c -e trace=fcntl` on
`BaseLib_OSCreateProcessTests`: millions of `fcntl` calls before, tens after (see "Evidence").

**Not testable here:** Darwin (unchanged path) and Ubuntu 20.04 kernels (fallback path); both
run the existing loop, which is the code being kept.

## Risk assessment

| Aspect | Assessment |
|---|---|
| Functional risk | Low. The new call is a superset of the loop's effect on the same descriptor range; its failure returns to the loop. The one protocol-critical descriptor (the comm pipe) keeps its explicit check. |
| Portability risk | Low. Raw syscall number and flag are defined locally; `ENOSYS`/`EINVAL` handled; Darwin excluded at compile time. |
| Hygiene regression | None by construction; pinned by new case 2. |
| Signal-safety (O-4) | Preserved: `syscall()` only. |
| Behaviour change visible to callers | Faster spawns only. Exit codes, exceptions, redirect semantics and detached semantics unchanged. |
| Blast radius | Every UNIX `createProcess`; in-tree `Pinger`, tests, `UtfMain`; downstream callers benefit without change. |
| Complexity | Low: one helper, one `if`, two new tests. |
| Cost of implementation | About 2 hours including the tests and the four-unit verification. |
| Cost of testing | Four `utf_baselib` units plus two new cases; one `strace` run. |

## Recommendation

Implement option 1 now. It is small, its failure mode is the status quo, it is verifiable on
this host with a test that fails before and passes after, and the benefit lands in exactly the
environments (containers, systemd services) where the library runs unattended. Fold O-5 (the
next item in the same function) in afterwards, not together, so each diff stays reviewable.

## Decisions requested (recommendation is the default)

1. **Proceed now** versus leave deferred until a downstream latency report. Default: now.
2. **Fallback on kernels without `CLOSE_RANGE_CLOEXEC`:** keep today's loop (default; correct,
   slow only on old kernels with large limits) versus option 2b (faster, documented race).
3. **Timing test gating:** skip when the hard limit is below 65536 (default) versus always run
   with whatever limit is available (weaker assertion).
4. **Darwin:** leave on the loop (default) versus implement option 2 for parity now.

## Outcome (2026-09-06)

All four decisions were taken with their defaults: proceed now; today's loop stays the fallback
on kernels without `CLOSE_RANGE_CLOEXEC`; the timing test skips when the hard limit is below
65536; Darwin stays on the loop (deferral record item 2).

**Implemented** in `src/include/baselib/core/detail/OSImplUNIX.h`: `tryMarkAllCloseOnExecNothrow`
(raw `syscall( 436 or __NR_close_range, from, ~0U, CLOSE_RANGE_CLOEXEC or 1U << 2 )` under
`#ifdef __linux__`, returning `false` on any failure and on other platforms) and, in
`execChildProcessNothrow`, the loop kept as the fallback when the helper returns `false`; the
explicit comm-pipe `fcntl` check is unchanged. No change to the parent, to `ChildExecInfo` or to
the O-3 descriptor handling.

**Tests added** to `utf_baselib` (`TestBaselibDefault.h`, inside the UNIX-only block):
`BaseLib_OSCreateProcessDescriptorLimitTests` (raises the soft limit to the hard limit, five
spawns of `true`, requires under 20 ms each; **failed before the fix**: 5 spawns in 1.37 s) and
`BaseLib_OSCreateProcessDescriptorHygieneTests` (ten non-close-on-exec descriptors held plus a
thread opening and closing descriptors during the spawn; the redirected child reports exactly
4 descriptors; passes before and after).

**Measured after the fix** (gcc1520 debug, soft limit 1048576): average spawn 2 ms (was about
180 ms); `BaseLib_OSCreateProcessTests` 0.22 s (was 0.75 s); the 42-spawn logging stress 1.1 s
(was 15.9 s); `strace -c` over `BaseLib_OSCreateProcessTests` shows 4 `close_range` calls and 8
`fcntl` calls (the comm-pipe check) against 4,194,309 `fcntl` calls before:

```
% time     seconds  usecs/call     calls    errors syscall
------ ----------- ----------- --------- --------- ----------------
 60.67    0.000526          14        36           clone3
 21.11    0.000183          45         4           clone
 17.65    0.000153           3        46        42 execve
  0.46    0.000004           0         8           fcntl
  0.12    0.000001           0         4           close_range
------ ----------- ----------- --------- --------- ----------------
100.00    0.000867           8        98        42 total
```

**Verification matrix:** `utf_baselib` built with `make -k -j1` for `gcc1520` and `clang2010`,
debug and release (4 units, no errors, no warnings); on each binary the two new cases and the
seven existing process cases (`BaseLib_OSCreateProcessTests`, the three O-3/O-4 cases,
`BaseLib_OSCreateProcessRedirectedTests`, `BaseLib_OSTryAwaitTerminationTests`,
`BaseLib_OSTerminateProcessTree`) pass; the timing case reports an average spawn of 0 to 2 ms
at the 1048576 limit on every variant; no leftover processes after any run. `git diff --check`
clean. Nothing committed.

## Evidence (before the fix)

`strace -f -c -e trace=fcntl,execve,clone,clone3,fork,vfork` over `BaseLib_OSCreateProcessTests`
(four spawns, gcc1520 debug, soft limit 1048576):

```
% time     seconds  usecs/call     calls    errors syscall
------ ----------- ----------- --------- --------- ----------------
 99.99   22.211533           5   4194309   4194283 fcntl
  0.01    0.001418          30        46        42 execve
  0.00    0.000669         167         4           clone
  0.00    0.000000           0        36           clone3
------ ----------- ----------- --------- --------- ----------------
100.00   22.213620           5   4194395   4194325 total
```

Four spawns, 4,194,309 `fcntl` calls: 1,048,576 per spawn, one `F_GETFD` per possible
descriptor, of which 4,194,283 fail with `EBADF` because the descriptor is closed (the `F_SETFD`
follows only for the few that are open). `fcntl` is 99.99 % of the traced system-call time. The
46 `execve` entries are the test binary's own exec plus `bash` resolving `doesnotexistproc`
through `PATH` (42 failing attempts), not the sweep. After the fix the same trace should show a
handful of `fcntl` calls per spawn (the comm-pipe check and the fallback path when the kernel
lacks `close_range`).
