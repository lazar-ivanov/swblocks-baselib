# Two baseline runs are not enough to classify a rarely-varying case as nondeterministic

**Origin:** the G1 gate (slice **S0.5** of `notes/plans/http2-implementation-plan.md` §2), run on
2026-09-18, gcc1520 debug. **Status:** OPEN - methodological, no code change proposed here.

## What happened

`scripts/utests/utf_runlog.py --compare` reported

    ASSERTION COUNT CHANGED: BaseLib_Base64UrlTests (16390 -> 16396)

for a case in `utf_baselib`, a module no slice of the change-set touches. Both baseline runs had
reported 16390, so the tool had classified the case as **deterministic** and compared it on
assertion count.

It is not deterministic. Running the **unchanged baseline binary** eight times, one case only:

    16384  16384  16390  16396  16396  16390  16396  16366

The value the gate flagged as a change, 16396, is produced by the unchanged binary three times in
eight. Every observed value is a multiple of 6 apart.

## Why the case varies

`BaseLib_Base64UrlTests` (`src/utests/utf_baselib/TestBaselibDefault.h:8358`) draws 1024 random
buffer sizes from `uniform_int_distribution( 0, 1025 )` and its assertions are **conditional on the
size being non-zero**:

- with a non-zero size, each `cbVerifyEncoded` call makes 8 assertions;
- with a zero size, it makes 5 - the `encoded.size()` check and the two `memcmp` checks are skipped.

The callback runs twice per iteration, so **each zero-size draw costs exactly 6 assertions**, which
is the granularity seen above. The number of zero draws over 1024 iterations is roughly Poisson with
mean 1, so the count varies by a few assertions from run to run and two runs agreeing is ordinary.

The generator is not seeded deterministically: `random::seed`
(`src/include/baselib/core/detail/RandomBoostImports.h:70`) seeds from `random_device`, so every
process gets a fresh stream. There is no fixed-seed mode to fall back on.

## The methodological point

`utf_runlog.py` derives its nondeterministic-case list from **two** baseline runs and treats any case
that agrees with itself as deterministic thereafter. That is sound for a case which varies on most
runs - a retry loop, a perf case - and unsound for one whose variation is a **rare event**. The
probability that two runs of this case agree is roughly 30%, so it is misclassified about a third of
the time, and when it is, the next gate reports a false difference.

The cost is not a wrong verdict - the difference is visible and can be investigated, as it was here -
but the investigation is not cheap, and a gate which cries wolf is a gate which stops being read.

## Options, none of them taken here

1. **More baseline runs.** Three or five instead of two, at the cost of a full suite run each. Raises
   the bar without removing the class of error.
2. **Make the case's assertion count fixed.** Hoist the conditional assertions so every iteration
   asserts the same number of times, e.g. assert the zero and non-zero properties separately rather
   than skipping. This is a test change, in a case which is not otherwise implicated.
3. **Seed deterministically under test.** A fixed seed when `BL_IS_UNIT_TEST_BINARY` is defined would
   make this and every other random-data case reproducible, which is a broader change than this
   record's scope and would weaken the coverage random data is there to provide.
4. **Record the case in the tool's own known-unstable list**, so it is always compared on outcome
   only regardless of what the sampling says.

## What was concluded for the G1 gate

The difference was explained, not re-run until green, per design §3.8. The gate's acceptance -
"for every pre-existing case the registered set, the executed set, pass/fail and the assertion counts
of deterministic cases are unchanged, and the new cases pass" - holds: this case is not
deterministic, and its outcome (`passed`) is unchanged.
