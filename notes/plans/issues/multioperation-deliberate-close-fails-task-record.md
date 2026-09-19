# A deliberate `beginClose()` with operations in flight completes the task FAILED

**Found:** 2026-09-19, by the S3.2 lane, under ThreadSanitizer — which was slow enough to flip a
race the ordinary build never lost. **Status:** **CLOSED 2026-09-19.** Decided by the maintainer as fix
shape 1 below, implemented as its own change-set, and merged at `f4d51a2`. What it resolved and how
is at the end of this record. **Not a data race.** It is a contract gap in
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

**CHOSEN: 1, with the refinement below.** It needs a flag distinct from `m_closing`, because
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


## The decision, 2026-09-19

Shape **1**, with both refinements: a flag **distinct from `m_closing`** (which both doors already
set, so it cannot tell a deliberate close from a failing one), and excusing
`asio::error::operation_aborted` **specifically** rather than every error arriving after a deliberate
close — a genuine I/O failure while closing still reports.

```cpp
bool m_closingDeliberate = false;          // distinct from m_closing

void beginClose() NOEXCEPT
{
    BL_MUTEX_GUARD( m_operationsLock );
    m_closing = true;
    m_closingDeliberate = true;
}

// in onOperationCompleted, under m_operationsLock:
const bool isSelfInflictedAbort = m_closingDeliberate && isOperationAborted( eptr );

if( eptr && ! m_firstError && ! isSelfInflictedAbort )
{
    m_firstError = eptr;
    ...
}
```

`m_closingDeliberate` resets in `scheduleNothrow` with the rest of the per-run accounting.

**Three things the implementation must settle, none of them decided by the shape.** What
`isOperationAborted` tests, given the error arrives as an `std::exception_ptr` and not a code — the
existing `isExpectedSslErrorCode` and the S0.1 classifier are the precedent to follow rather than
invent against. What `cancelTask()`, the *external* cancel, should report, since it also produces
`operation_aborted` but for a reason the caller asked for. And a case for the quadrant S0.1 never
covered — deliberate close with operations outstanding — which is what would have caught this.


## How it was closed (2026-09-19)

Merged at `f4d51a2`, its own change-set touching only `tasks/MultiOperationTask.h` and
`utf_baselib_tasks2/TestMultiOperationTask.h`, as the rule for landed gated core requires.

**The guard as it landed**, with the term that does the work highlighted:

```cpp
const bool isSelfInflictedAbort =
    m_closingDeliberate && ! base_type::isCanceled() && isOperationAborted( eptr );
```

**The three things the shape left open, and how they were answered.**

1. **`isOperationAborted` follows precedent rather than inventing a mechanism**:
   `eh::errorCodeFromExceptionPtr( eptr ) == asio::error::operation_aborted`. That helper is
   pre-existing and `NOEXCEPT`, already used on a task-completion `eptr` in
   `messaging/MessagingClientImpl.h`, and already unit-tested for all three forms. An exception
   carrying **no** code yields an empty code, which never equals `operation_aborted` - so a genuine
   failure without a code is still recorded, which is the wanted answer. The rethrow is not free, so
   the call sits last behind the cheap tests and is never reached on the success path.
2. **`cancelTask()` still reports failed, deliberately**, and the `! isCanceled()` term is what
   makes that hold: without it, a cancel landing on a task which had already begun closing
   deliberately would silently flip from failed to succeeded - exactly the silent change this record
   warned against. Verified rather than assumed that there is no window in which an abort arrives
   while the flag is invisible: `requestCancelInternal()` sets the flag **before** calling
   `cancelTask()`, and all three internal call sites of the latch are `requestCancel()` overrides.
   `isCanceled()` is an atomic read taking no lock, so the leaf-lock rule is intact.
3. **The uncovered quadrant is covered.** Two cases inside `runMultiOperationSuite()`, so they run
   under both the 1-thread and 4-thread configurations and add no new case name: a deliberate close
   with two operations outstanding asserting the task does **not** fail, and the same shape plus
   `requestCancel()` asserting it **does** fail with `operation_aborted` - the pin for answer 2.

**The negative control.** With the new cases in place and the header reverted to pristine, the
module exits 201 with exactly two failures, both the new case, in both runs. The header was then
restored and the working diff verified byte-identical.

## One thing this closed elsewhere

The S3.2 probe's `chkToClose()` carried a comment saying a deliberate `beginClose()` from the read
handler "fails on its own clean close". That was the finding, and it stopped being true here. The
probe still takes the close decision only in the timer handler - that is still where it knows both
halves are done - but the restriction is now **sufficient rather than necessary**, and the comment
says so rather than citing a reason that no longer holds (`0a87721`).

## What this record cannot tell you

**Tier 3 cannot speak to this change at all.** `notes/reviews/major/update_2026/baseline/runlog.json`
dates from 2026-09-17 and carries no `utf_baselib_tasks2` key, nor any h2 module - it predates every
case involved here. That is pre-existing staleness rather than anything this change introduced, and
the absolute counts recorded above are what a refresh would capture.
