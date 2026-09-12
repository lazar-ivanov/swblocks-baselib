# Linux x64 Testing Under Rosetta: Technical Deep Dive

This document contains detailed technical analysis of the two environment properties that make otherwise correct tests fail when the x64 toolchain runs under Rosetta emulation inside a Linux container on Apple Silicon. For operational guidance (what to do about them), see the parent [AGENTS.md](../AGENTS.md).

The container is launched by `docker/ubuntu/launch.sh`; Rosetta itself is registered with the host kernel by the scripts under `rosetta/ubuntu/` and `rosetta/rhel/`. Both distributions are affected identically, because the cause is the kernel's binfmt_misc handler rather than anything in the image.

---

## Descriptor Accounting: Rosetta Adds Four

### The mechanism

When the kernel executes an x86_64 binary on an ARM64 host, the binfmt_misc entry registered for Rosetta causes the interpreter to be invoked instead. The handler passes both the target binary and the interpreter itself to the new process as already-open file descriptors, and those descriptors are not close-on-exec — so they survive into the next `exec` as well.

A child spawned as `bash -c 'ls /proc/self/fd'` therefore goes through two execs (`bash`, then `ls`), each contributing a pair:

```
0 -> /dev/null            4 -> /media/psf/RosettaLinux/rosetta
1 -> pipe:[...]           5 -> /usr/bin/ls
2 -> pipe:[...]           6 -> /media/psf/RosettaLinux/rosetta
3 -> /usr/bin/bash        7 -> /proc/N/fd        (the directory 'ls' itself opened)
```

Eight entries, where a native Linux run of the same command shows four (`0`, `1`, `2`, and the directory descriptor). The offset is constant and deterministic: it depends only on how many execs the probe performs, not on load, timing, or what the parent had open.

### Distinguishing a real leak from the offset

The offset is additive and each genuinely leaked parent descriptor contributes exactly one more entry. That gives a cheap discriminator — hold known descriptors open across the spawn and see whether the count moves:

```bash
# baseline in this environment
docker exec <container> bash -c 'bash -c "ls /proc/self/fd | wc -l"'          # 8

# two extra non-CLOEXEC descriptors held by the parent
docker exec <container> bash -c 'exec 8</dev/null; exec 9</dev/null; \
    bash -c "ls /proc/self/fd | wc -l"'                                        # 10
```

A count equal to the baseline means nothing leaked, regardless of what the baseline happens to be on that machine. This is why `BaseLib_OSCreateProcessDescriptorHygieneTests` in `src/utests/utf_baselib/TestBaselibDefault.h` measures a baseline before opening its own descriptors rather than comparing against a hard-coded constant: the property under test is the difference, not the absolute value. A test written against the native constant of `4` reports a descriptor leak here that does not exist.

---

## Orphan Reaping Depends on PID 1

### The mechanism

When a process dies, its children are reparented to PID 1, which is expected to `wait()` on them. Until something does, a terminated child remains a zombie — and a zombie still has a PID, so `kill(pid, 0)` continues to return `0` rather than failing with `ESRCH`.

`launch.sh` runs an interactive `bash` as PID 1, which reaps reparented children as part of its normal job-control handling. A container started with any other entry process generally does not. The common case is a detached container created so it can be driven non-interactively:

```bash
docker run -d ... ubuntu-dev sleep infinity      # PID 1 is 'sleep' — never reaps
```

`sleep` never calls `wait()`, so orphaned zombies accumulate for the life of the container.

### How it presents

Tests that kill a process group and then confirm the grandchild is gone will hang on the confirmation and fail on timeout. `BaseLib_OSTerminateProcessGroupTests` in `src/utests/utf_baselib/TestBaselibDefault4.h` polls `kill(pid, 0)` for `ESRCH`; against an unreaped zombie that poll never succeeds, and the test fails at its `waitForProcessToDisappear` check. The signal delivery and the process-group teardown were both correct — only the reaping was missing.

Confirming the diagnosis takes a few seconds:

```bash
docker exec <container> bash -c '
  bash -c "sleep 300 >/dev/null 2>&1 & echo \$!" > /tmp/p.txt
  P=$(head -1 /tmp/p.txt); kill -9 $P; sleep 1
  ps -o pid,ppid,stat,comm -p $P'
```

A `Z` in the `STAT` column with `PPID` of 1 is the failure mode. Adding `--init` to `docker run` installs a real init (`tini`) as PID 1 and restores the reaping behaviour that the interactive shell provides.

---

## Performance and Memory Characteristics

Measured on a 2-core, 8 GB VM with Rosetta emulation, building all test modules from scratch:

| Operation | Approximate cost |
|---|---|
| One toolchain/variant cell, `make -k -j1 utests` | ~4 hours |
| Full clang/gcc x debug/release matrix | most of a day |
| Full test run, `make -k -j5 testutf` | ~1 hour |

Each test module is essentially one large translation unit compiled against a header-only library, so the build is dominated by ~19 long single-file compiles rather than by many short ones. That makes the wall-clock cost fairly insensitive to caching and largely proportional to the number of cells built.

Memory is the binding constraint on parallelism. Under gcc at `-O2`, `cc1plus` on the largest test translation units (`UtfBaselibMessagingMain.cpp` in particular) peaks near 3.7 GB resident. Two such compiles in parallel will exhaust an 8 GB VM and invite the OOM killer, which surfaces as an opaque `Killed` partway through the build. Keeping the build at `-j1` avoids this; test execution is far lighter and parallelises safely.
