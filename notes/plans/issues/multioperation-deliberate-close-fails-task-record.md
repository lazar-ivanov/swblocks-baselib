# A deliberate `beginClose()` with operations in flight completes the task FAILED

**Found:** 2026-09-19, by the S3.2 lane, under ThreadSanitizer — which was slow enough to flip a
race the ordinary build never lost. **Status:** OPEN, and it is a **decision for the maintainer**,
not an implementation detail. **Not a data race.** It is a contract gap in
`src/include/baselib/tasks/MultiOperationTask.h`, which is landed, gated core, so a fix is its own
tested change-set (AGENTS.md).

**Nothing in L3 is blocked by it.** It blocks **S4.1**, which is L4.

## The defect

`beginClose()` sets `m_closing = true` and records **no error**. `applyDecision` then calls
`initiateClose()`, which cancels the task's sockets and timers. Each cancelled operation's handler
arrives at `onOperationCompleted` carrying `asio::error::operation_aborted`, and there:

```cpp
if( eptr && ! m_firstError )
{
    m_firstError = eptr;              // <- the self-inflicted abort becomes THE error
    m_firstErrorIsExpected = isExpectedException;
    m_closing = true;
}
```

`m_firstError` is null, because the close was deliberate and nothing failed. So the first
`operation_aborted` becomes the task's error and reaches
`base_type::notifyReady( firstError, ... )`. **The task completes `isFailed()` on its own clean
close.**

`m_firstErrorIsExpected` does not save it: that flag only carries the expected/unexpected
classification alongside the error, it does not suppress the error.

## The wrong assumption is written into the file twice

Both comments are correct *when a genuine error came first*, and both are false on a deliberate
close:

- On `onOperationCompleted`: *"Errors arriving after it, including the operation_aborted of
  everything initiateClose() cancelled, are accounted for but not reported."* True only if there is
  an "it" — an error that arrived first.
- Inside `applyDecision`: *"A failure to cancel must not cost the task its terminal path - and **it
  is already failing when this runs**, so the original error is the interesting one."* The
  emphasised clause is exactly what a deliberate close violates.

So this is not an oversight in one line; the close path was written on the assumption that closing
implies failing.

## Why the existing coverage does not reach it

`Tasks_MultiOperationTaskCloseWhenAllSucceedTests`
(`src/utests/utf_baselib_tasks2/TestMultiOperationTask.h:638`) calls `beginClose()` only once every
operation has already succeeded, so **nothing is in flight to cancel**. Every other case in that
file enters closing *through* an error — which is the case the mix-in's comments describe. The
uncovered quadrant is precisely "deliberate close, operations outstanding".

## Why it matters, concretely

Design §5.1's connection task closes deliberately as a matter of course — on GOAWAY, on the idle
deadline, when the last stream finishes — and it does so with a read and timers outstanding. Under
today's mix-in **every such shutdown reports failed with `operation_aborted`**, and `ConnectionPool`
(S5.2) would count a clean shutdown as a failed connection, which feeds the retry and health logic
of D6.

## Three shapes a fix could take

1. **`beginClose()` marks the run as deliberately closing, and `onOperationCompleted` then declines
   to record an `operation_aborted` that arrives after that mark.** Narrow and behaviour-preserving:
   a task that closes through an error is unaffected, because its first error is already recorded.
2. **The task is expected to filter it**, and the mix-in's doc comment says so explicitly and says
   how.
3. **`initiateClose()` records which operations it cancelled and excuses exactly those.**

**Recommended: 1, with one refinement.** It needs a flag distinct from `m_closing`, because
`m_closing` is already set by *both* doors — an error and `beginClose()` — and the fix has to tell
them apart. And it should excuse `asio::error::operation_aborted` **specifically**, not every error
arriving after a deliberate close: an abort during a deliberate close is self-inflicted and means
nothing, whereas a genuine I/O failure while closing is still worth reporting.

2 is the weakest: the accounting is the one thing the mix-in exists to own, and pushing it to every
task is how it gets got wrong once per task. 3 is the most precise and the most machinery — the
accounting is deliberately a *count*, not a set of operation identities, and 3 would change that.

Whoever implements it should also decide what `cancelTask()` — the external cancel — should report,
since that path produces `operation_aborted` too but for a reason the caller asked for.

## What was done instead, meanwhile

The S3.2 probe takes the close decision **only in the timer handler**, where the timer is the
operation completing and nothing else is in flight. That is a workaround in a test probe, not a fix,
and it is documented at `chkToClose()` so that a later reader does not "simplify" it away.
