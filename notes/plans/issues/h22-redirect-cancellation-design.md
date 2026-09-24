# H22 — a cancel between redirect hops reports the intermediate response as a success — design

**Date:** 2026-09-24. **Status: first draft, NOT agreed.** Nothing here has been reviewed,
implemented, built or run. No build was performed for it: every claim below is read from the source
on `lazari2`, and §7 lists what that leaves open.

Owed item **16a** on [`astra-remediation-owed-work.md`](astra-remediation-owed-work.md), recorded as
**H22** in [the astra review](../http2-l0-l6-architecture-security-review-2026-09-21.md) and, before
that, as **finding 9** in [`http2-l6-review-record.md`](http2-l6-review-record.md). Pulled forward
out of L6's owed list because it is *a wrong answer handed to the caller* — the class this batch has
just fixed twice (a truncated h1 response reported as a complete 200; an h2 request reported
unretryable after the server answered it).

**Anchors are function names, not line numbers.** The line anchors in both recorded findings are
already stale: H22's evidence cites `ClientSession.h#L1369` and "continuation at 1392–1396", which
today land inside `chkPrepareNextHop( )`, and L6 finding 9 cites `:1255-1258`, which today lands
inside `decodeBody( )`. Both findings are nonetheless about the right code; §1.4 says where they are
imprecise.

---

## 1. What actually happens

### 1.1 The path, read at the source

`SessionRequestTaskT` (in `ClientSession.h`) is the logical request: one caller-visible task that
runs a chain of `HttpClientRequestTaskImpl` hops as a `tasks::WrapperTaskBase` continuation. Two of
its members matter here.

`requestCancel( )` latches first and forwards second:

```
virtual void requestCancel() NOEXCEPT OVERRIDE
{
    m_cancelRequested = true;
    base_type::requestCancel();
}
```

`continuationTask( )` decides, after each hop, whether the chain continues:

```
auto task = base_type::handleContinuationForward();
if( task ) { return task; }

BL_MUTEX_GUARD( base_type::m_lock );

absorbResponse();

if( m_cancelRequested ) { return nullptr; }          // <- the defect

if( m_hop -> exception() ) { ...retry or nullptr... }

if( ! chkPrepareNextHop() ) { return nullptr; }
m_attempts = 0U;
startHop();
return om::copyAs< tasks::Task >( this );
```

The cancel limb returns `nullptr` **without setting an exception anywhere**. `SessionRequestTaskT`
derives from `WrapperTaskBase`, which derives from `ForwarderTaskBaseT` — a class with no state of
its own: its `isFailed( )`, `isFailedOrFailing( )`, `exception( )`, `getState( )` and `name( )` all
forward to `getTargetTask( )`, which is `m_wrappedTask`, which is the hop that just finished. So
when the latched cancel ends the chain after a hop that *succeeded*, the logical request's verdict
is that hop's verdict: **not failed, no exception**.

`RetryableWrapperTaskT` in `TaskBase.h` — which `SessionRequestTaskT`'s own comments name as the
model it copies for the latch and for the direct `m_wrappedTask` assignment — does not have this
hole. Its `continuationTask( )` cancel limb reads:

> *The work task's own error, if it has one, is more informative than a bare cancellation, so it is
> kept; otherwise … the task is completed as cancelled*

and it sets `operation_aborted` marked `errinfo_is_expected` on the wrapped task before returning
`nullptr`. The session's copy of that limb kept the latch and dropped the completion.

### 1.2 The window, and why the hop's own cancel does not close it

The forwarded half of `requestCancel( )` reaches the hop as
`HttpClientRequestTaskT::requestCancel( )`, which marks the TaskBase flag and posts
`EventKind::Cancelled` to the hop's mailbox. The mailbox is FIFO and drained in batches under the
hop's task lock, and the handler that would fail the request, `applyStopped( )`, opens with

```
if( m_isCompleted || m_isCompletionPending ) { return; }
```

So a cancel that reaches the hop after `applyClosed( )` → `answerOnClosed( )` → `failWith( )` /
`completeResponse( )` have set the completion flags is, correctly, a **no-op on the hop**. The hop
keeps its success, and the latch is the only trace the cancel leaves.

The window therefore opens the moment the hop's completion becomes pending, inside the drain, and
closes when `continuationTask( )` reads `m_cancelRequested`. In between, in order:

1. the rest of the batch is applied under the hop's task lock;
2. `applyEvents( )` runs the **deferred phase** off the lock — `releaseConnectionSlot( )` giving the
   stream slot back to the pool (which can dispatch another queued waiter), `releaseConnection( )`
   dropping the task's reference to the connection;
3. `m_isCompleted` is set, `notifyReady( )` runs `onTaskStoppedNothrow( )` and the logging under the
   hop's lock, then releases it and calls `cbReady( )`;
4. `cbReady( )` is `ExecutionQueueImpl::onReadyObserver` → `onReady( task )`, which takes **the
   execution queue's `m_lock`** and only then calls `task -> continuationTask( )`.

Two consequences of step 4 are load-bearing and neither finding states them:

- **A cancel through the queue reaches this window, and the queue lock does not stop it.**
  *Heading corrected 2026-09-24 by the review (§9.2); the body below was right all along and the
  heading said the opposite, which §2, §7 and §8 then repeated.*
  `ExecutionQueueImpl::cancelAll( )` and `flushInternal( ..., cancelExecuting = true )` call
  `requestCancel( )` on each executing task *while holding the queue's `m_lock`*, and `onReady( )`
  holds the same lock across `continuationTask( )`. The two are mutually exclusive **at step 4
  only** — and the window opens at step 1, when the hop's completion becomes pending, with no queue
  lock held anywhere. So if `cancelAll` wins the lock the latch is set and **the defect fires**; if
  `onReady` wins, the next hop has already been created and started and the forwarded cancel reaches
  *it*, which is the correct outcome. **Any future caller's ordinary shutdown — `cancelAll( )`,
  `dispose( )`, `forceFlushNoThrow( )` — can therefore produce the wrong answer.**
- **The window is a real interval, not a single instruction.** It spans a pool `releaseStream( )`
  and everything else in the deferred phase, plus whatever contention the queue lock is under. It is
  not a "two adjacent stores" race.

`absorbResponse( )` is inside the window too: cookies from the 3xx are stored in the jar and the
body is decoded before the latch is read. That is not wrong — the response did arrive — but it means
the cancelled request still mutates session state. The proposed shape keeps that behaviour.

### 1.3 What the caller sees

For a caller that cancelled a chain after its first hop returned `302`:

| read | value |
|---|---|
| `om::qi< tasks::Task >( requestTask ) -> isFailed( )` | `false` |
| `… -> exception( )` | `nullptr` |
| `requestTask -> response( ).status( )` | `302` |
| `requestTask -> response( ).headers( )` | the 3xx's, `location` included |
| `requestTask -> request( )` | the request that produced the 3xx |
| `requestTask -> redirectHops( )` | the number of hops actually followed |

**That tuple is byte-for-byte the legitimate "the policy did not follow this redirect" outcome** —
redirects disabled (the default), the hop limit reached, a cross-scheme target refused by
`chkPrepareNextHop( )`, a non-replayable request, or a `BodySink` installed. Every one of those hands
the 3xx back unfailed on purpose, and `ClientRequestTask::response( )` is documented as "the response
of the last hop". So a caller cannot tell, from the task, a chain that stopped because the policy
said so from a chain that stopped because its own cancel truncated it. That indistinguishability —
not the bare fact that `isFailed( )` is `false` — is the severity of this finding.

### 1.4 Where the two recorded findings are imprecise

Both are **right in conclusion**. Three details do not survive reading:

- **"`redirectHops( )` one short" (L6 finding 9) is wrong as stated.** `m_hops` is incremented inside
  `chkPrepareNextHop( )`, which never runs on this path, so the count is an accurate count of
  redirects *followed*. Nothing about it is short; it is `isFailed( )` that lies. The number becomes
  a real question only under the fix — see §3.2.
- **"a successful intermediate 3xx rather than the expected cancellation" (H22) understates it.**
  The caller does not merely get the wrong *kind* of answer, it gets one that is identical to a
  documented, correct answer.
- **H22's correction — "validate a controlled pause between finalizing the 3xx hop and creating the
  next one" — asks for a sleep.** A pause is not a rendezvous. §5 gives a construction with no pause
  in it.

One further thing neither finding says, and it narrows the fix: **the failure limb is already
correct.** A cancel latched after a hop that *failed* returns `nullptr` with the hop's own exception
intact, which is exactly what `RetryableWrapperTaskT` says it wants ("the work task's own error … is
more informative than a bare cancellation"). Only the success limb is defective.

## 2. Is it reachable

**Yes. There is no live caller today — but the execution queue's own cancellation API reaches it.**

> *Corrected 2026-09-24 (§9.2).* This opening said there was *"no way to hit it from the execution
> queue's own cancellation API"*. There is: the queue lock serialises only step 4 of §1.2's window,
> which opens at step 1. **`cancelAll( )`, `dispose( )` and `forceFlushNoThrow( )` all reach it**, so
> a future caller's ordinary **shutdown** produces the wrong answer. This makes §4(a)'s rejection
> stronger and §4(d) weaker; the shape in §3 is unaffected, because it does not care who latched.

- Nothing in `src/` outside the unit tests calls `ClientSession::createRequestTask( )`. The L8
  compatibility facade is future work. So the defect is **latent**: it can be reached only by code
  that does not exist yet, or by a test.
- The only cancellation paths are `tasks::Task::requestCancel( )` on the task itself and the queue's
  `cancelAll( )` / `flush( … cancelExecuting )` / `cancelTask( )`. As §1.2 establishes, **the queue
  routes cannot hit the window on the same queue** — the queue lock serialises them against
  `onReady( )`. A caller holding the task and calling `requestCancel( )` directly *can*, from any
  thread, and that is the shape the existing tests already use (`TestHttp2DriverAccounting.h` has a
  comment explaining why it uses the task's own `requestCancel( )` rather than the queue's).
- When it is hit, it is hit on an ordinary interval, not a pathological one: the window contains a
  pool `releaseStream( )` and a queue-lock acquisition. No measurement of the interval was taken —
  see §7.

So: a narrow window, no shipped caller, and a wrong answer when it does fire. The absence of a
caller is the argument for fixing it **now** rather than for deferring it — there is nothing to be
compatible with, the edit is small, and the next thing built on this layer is the facade that will
have the cancel button on it.

## 3. The shape

### 3.1 The boundary

The design owes an answer to one question: **when is the logical request finished, such that a
cancel arriving afterwards is a no-op?**

The answer this design takes: **the logical request is finished when the chain has decided it has no
more work to do.** Concretely — when `chkPrepareNextHop( )` says no next hop follows, or when the
failure limb says no retry follows. Before that point the chain owes the caller more work, so a
latched cancel must end it as *aborted*. After it, the answer exists and the cancel lost the race,
so the answer stands.

The consequence worth naming: a cancel that lands in the same window after the **final** hop — the
200 the caller actually wanted, or a 3xx the policy refuses — still reports success. That is
deliberate, it is what H22 itself allows ("cancellation after final logical completion may remain a
no-op"), and it is also what every other cancellable API in this tree does with a cancel that
arrives after completion.

### 3.2 The edit

In `SessionRequestTaskT::continuationTask( )`, after `absorbResponse( )`:

- **Failure limb — unchanged in effect.** `m_cancelRequested` continues to short-circuit before
  `chkPrepareRetry( )`, and the hop's own exception is what the caller gets. This is the branch the
  library's own idiom already blesses, and leaving it alone keeps every existing failed-hop case
  exactly where it is.
- **Success limb — the cancel check moves below the decision.** `chkPrepareNextHop( )` runs first.
  If it returns `false` the chain is finished and the hop's success is returned as today, cancelled
  or not. If it returns `true` **and** the latch is set, the chain is ended as aborted instead of
  started.
- **`m_hops` moves.** It is incremented today as the last statement of `chkPrepareNextHop( )`.
  Moving that increment to `continuationTask( )`, immediately before the `startHop( )` it already
  precedes, keeps `redirectHops( )` exact for the aborted case and is behaviour-neutral everywhere
  else, because `startHop( )` follows the increment unconditionally on every other path.

Sketch, for shape only:

```
if( m_hop -> exception() )        { ...as today... }

if( ! chkPrepareNextHop() )       { return nullptr; }       // the hop IS the answer

if( m_cancelRequested )           { failChainAsCancelled(); return nullptr; }

m_hops     = m_hops.value() + 1U;                            // moved out of chkPrepareNextHop( )
m_attempts = 0U;
startHop();
return om::copyAs< tasks::Task >( this );
```

Running `chkPrepareNextHop( )` and then abandoning its work has two side effects, and both are
accepted rather than overlooked. It mutates `m_next` (url, method, headers, body) — invisible to the
caller, which reads `request( )`, i.e. `m_request`, the request that actually went out. And on a
307/308 with a streaming source it calls `m_next.bodySource( ) -> rewind( )` on a request that will
now never be sent — a rewind of the caller's own object, harmless by that interface's contract, and
reachable only by a cancel inside this window on a redirect that preserves a streaming body. The
alternative is splitting `chkPrepareNextHop( )` into a pure `evaluate` and an `apply`; §4 says why
that is not taken.

### 3.3 What the cancellation is spelled as

The wrapper has no exception storage of its own (§1.1), so the exception must be set on `m_hop`,
exactly as `RetryableWrapperTaskT` sets it on `m_wrappedTask`. Two properties are required and one
is a choice:

- **Required:** `errinfo_is_expected( true )`, so `TaskBase::notifyReadyImpl( )` does not dump a
  deliberate cancel as a failure — the same reason the hop's own cancel and the chain's timeout are
  marked expected.
- **Required:** an `operation_aborted` error code, so a caller can discriminate it.
- **Choice:** the *shape* should be the one this layer already produces for a cancel —
  `UnexpectedException` carrying `errinfo_error_code( operation_aborted )` with a message containing
  "was cancelled", which is what `HttpClientRequestTaskT::applyStopped( )` builds and what
  `HttpClientRequestTask_CancelResetsTheStreamTests` already asserts on. Mirroring
  `RetryableWrapperTaskT`'s `SystemException::create( asio::error::operation_aborted, … )` instead
  would hand the caller two different exception types for one event depending on which side of a hop
  boundary the cancel landed, which is the discrimination problem this finding is about, one level
  down.

The message must not carry the URL, or must route it through `redactedUrl( )` — astra H20's rule,
which `chkRemainingBudget( )` already follows.

`m_response` is **left as it is**: the 3xx stays readable. With `isFailed( )` true and an explicit
`operation_aborted` the answer is no longer wrong, and a failed chain already leaves the failing
hop's partial response readable, so this is the consistent choice rather than a new one.

### 3.4 Lock order

The new call sets an exception on `m_hop` while holding the wrapper's `m_lock` — order
`wrapper lock → hop task lock`. That order already exists (`requestCancel( )` forwards under the
wrapper lock) and the reverse does not: `notifyReadyImpl( )` releases the hop's lock before calling
`cbReady( )`, so the path that reaches the wrapper lock from a hop does not hold the hop's lock when
it gets there. `RetryableWrapperTaskT` performs the identical call in the identical place.
`TaskBase::exception( eptr )` is an unconditional set under the task lock, valid on a task in
`PendingCompletion` — which is the state every hop is in when `continuationTask( )` runs, because
`setCompletedState( )` is called by `onReady( )` only *after* `continuationTask( )` returns.

## 4. Alternatives considered and rejected

**(a) Mirror `RetryableWrapperTaskT` literally: on a latched cancel, if the wrapped task has no
exception, fail it with `operation_aborted`.** This is the correction L6 finding 9 proposed
("Mirror it"), it is ~5 lines, and it needs no reordering. Rejected because it cannot tell §3.1's two
cases apart: it would also fail a chain whose **final** response had already arrived. A caller that
runs one request with redirects off — the default — and cancels on shutdown would have a complete,
correct 200 replaced by `operation_aborted`. `RetryableWrapperTaskT` accepts that trade because its
wrapper cannot know whether more attempts were coming; `SessionRequestTaskT` *can* know, by asking
`chkPrepareNextHop( )`, so accepting the same trade would be choosing to discard information the
class has in hand. It is also the more disruptive change to existing behaviour, which §6 counts.

**(b) Split `chkPrepareNextHop( )` into a pure `evaluateNextHop( )` and an `applyNextHop( decision )`
so the cancel check sits between them.** Cleaner in principle and removes §3.2's two accepted side
effects. Rejected for this change-set: it restructures a function that carries four separate
refusals (sink, policy, scheme, replayability), each with its own recorded reasoning, and the
benefit is a `rewind( )` that does not happen on a request nobody will send. If the split is wanted
it is its own change, judged on its own merits, not smuggled in behind a cancellation fix.

**(c) Add a "was this chain interrupted" flag to `ClientRequestTask` and leave the task successful.**
Rejected: it puts the burden of discovering the truth on the caller, who must know to ask. The task
framework already has one way to say "this did not complete" and every other layer here uses it.

**(d) Do nothing and record it.** Genuinely on the table, because the defect has no live caller
(§2). Rejected on the ground the maintainer pulled it forward on: the reasoning is fresh, the edit
is under twenty lines, the red is deterministic (§5), and the alternative is that the first code
ever written against this API is written against the wrong contract.

## 5. The red

**The window can be made certain without a sleep, a poll or a run count**, by intercepting the
decision point rather than racing it. `src/utests/AGENTS.md`'s rule — *"A negative control is the
evidence; the run count is not"* — is satisfiable here.

### 5.1 The rendezvous

`ExecutionQueueImpl::scheduleTaskInternal( )` binds `onReadyObserver` to **the task the queue holds**
(`om::ObjPtrCopyable< Task >( taskInfo.getTask( ) )`), so `onReady( )` — and therefore
`continuationTask( )` — is called on the task the test pushed, not on whatever is wrapped inside it.
A test can therefore push its own `tasks::WrapperTaskBase` around the real session task:

```
class RedirectCancelProbe : public tasks::WrapperTaskBase        // test-local
{
    virtual auto continuationTask() -> om::ObjPtr< tasks::Task > OVERRIDE
    {
        if( ! m_armed.exchange( false ) )
        {
            m_session -> requestCancel();       // the real public cancel, once, on the first hop
        }
        return base_type::continuationTask();   // -> the session task's continuationTask( )
    }
};
```

At the top of the probe's `continuationTask( )` three facts hold **by construction, not by timing**:

1. **The hop has finished.** The probe is only called from `onReady( )`, which is only called from
   the hop's `cbReady( )`, which `notifyReadyImpl( )` invokes after publishing the hop's completion.
2. **The session task has not yet decided.** Its `continuationTask( )` is reached only through the
   probe's `base_type::continuationTask( )` → `handleContinuationForward( )`, on the next line.
3. **No lock stands in the way.** The session task's `m_lock` is free at this instant, so the real
   `requestCancel( )` runs to completion: it sets the latch and forwards to the hop, where
   `applyStopped( )`'s `m_isCompleted` guard discards it — which is the very no-op the live race
   depends on, exercised rather than assumed.

This is the house idiom, not a new one: `TestTcpPreHandshakeStage.h`'s `ConnectOrderProbe` overrides
a product virtual and marks the task cancelled mid-operation for exactly this reason, with a comment
saying it is "what makes 'the cancel landed before the connect handler completed' deterministic".

### 5.2 The cases

Against the real `Http2TestPeer` already used by the session tests, with a `/start` → `302
/final` script and `redirectPolicy( ).isEnabled( true )`:

- **`…_CancelBetweenHopsIsReportedAsCancelled`** — probe armed on the first hop.
  `isFailed( ) == true`; the exception's message contains `was cancelled`; `response( ).status( )`
  is still readable as `302`; the peer never received `/final`. **Against unfixed code this fails on
  the first assertion** — `isFailed( )` is `false` — which is the negative control, and it fails for
  the stated reason rather than by hanging or by timing out.
- **`…_CancelAfterTheFinalHopIsANoOp`** — probe armed on the *second* hop (the `200`).
  `isFailed( ) == false`, `status( ) == 200`, body `/final`, `redirectHops( ) == 1`. This one is
  **green before and after** and is the guard against §4(a)'s over-reach: it is what makes the
  boundary of §3.1 a tested property instead of a paragraph.
- A third case is worth having for `redirectHops( )` under the fix (`0`, not `1`, when the cancel
  aborts before the second hop) because §3.2 moves that increment.

### 5.3 What the red does not cover, stated plainly

The probe makes the **decision** deterministic; it does not reproduce the **concurrency**. A cancel
arriving on another thread part-way through `absorbResponse( )` — mid-`storeCookies( )`, mid-decode —
reaches the same latch and the same check and so is the same case by construction, but the probe
does not execute that interleaving. No construction found during this design makes *that* variant
deterministic: every test-reachable hook inside the window (`decoders`, `cookieJar`) runs under the
wrapper's `m_lock`, where a cancelling thread blocks between setting the latch and returning, with
nothing observable in between to rendezvous on. That is a limitation of the coverage, not of the
evidence for this defect: the latch is a single `std::atomic< bool >` and the check reads it under a
lock, so the set of observable states at the check is exactly two.

## 6. Blast radius

**Touched:** `SessionRequestTaskT::continuationTask( )` and `chkPrepareNextHop( )`'s last statement,
in `src/include/baselib/httpclient/ClientSession.h`. One header, one class, ~15–20 lines. No
interface change: `ClientRequestTask` and `ClientSession` are untouched, and no caller outside the
tests exists (§2).

**What could move.** A behaviour change confined to one conjunction — *latch set* **and** *hop
succeeded* **and** *a next hop would have followed*. Every other path through `continuationTask( )`
keeps its outcome:

| path | today | after |
|---|---|---|
| no cancel | unchanged | unchanged |
| cancel latched, hop failed | hop's exception | hop's exception |
| cancel latched, hop succeeded, no next hop | hop's success | hop's success |
| cancel latched, hop succeeded, next hop due | **hop's success** | `operation_aborted`, expected |

**Existing cases at risk:** none found. `TestClientSession.h`, `TestClientSessionTls.h`,
`TestClientSessionTlsHttp1.h` and `TestClientSessionIdle.h` contain no session-request cancellation
case at all — the only `cancel` occurrences are in prose. The hop-level
`HttpClientRequestTask_CancelResetsTheStreamTests` is a level below and is untouched. The redirect
cases assert `redirectHops( )` only after completion, where the moved increment is invisible.

**Gating.** This modifies an existing core code path, so by the project's rule it lands as **its own
change-set, gated on the whole suite**, not folded into a feature. The suites that must run are the
session and pool families — `utf_baselib_httpclient`, `4`, `5`, `6`, `7` and the `h2client` family —
because `SessionRequestTaskT` sits under all of them.

**Where the new cases go.** Not `utf_baselib_httpclient4`: L6 recorded it at **48.0 MB**, already
over the 40 MB target, and `src/utests/AGENTS.md` forbids adding to a module at or near it.
`utf_baselib_httpclient6` holds one 377-line header and its own session helpers, and is the natural
home; a numbered sibling is the fallback. The module must be measured at implementation time rather
than assumed — the 48.0 figure is a64 clang debug, and the gate that matters is win-x86 debug.

## 7. What this design does not establish

1. **The width of the window was not measured.** §1.2's interval is read from the code. No build was
   run for this document, so there is no number for how often a cancel issued during a redirect
   chain lands inside it. If the fix is judged on cost rather than correctness, that number is
   missing.
2. **That any future caller will cancel a session request at all.** Today nothing outside the tests
   creates one (§2), so the defect's practical weight rests on a prediction about the L8 facade.
3. **That the probe of §5.1 compiles and composes as described.** It is derived by reading
   `ExecutionQueueImpl::onReady( )`, `scheduleTaskInternal( )` and `WrapperTaskBaseT`; a nested
   wrapper over `SessionRequestTaskT` has never been built in this tree. If it does not compose, §5
   loses its determinism and the value of the fix should be re-judged, not the test weakened.
4. **The exception shape is a proposal, not a settled contract.** Nothing asserts on a session-level
   cancellation today, so §3.3 is choosing a spelling, and a reviewer may reasonably prefer
   `RetryableWrapperTaskT`'s `SystemException`.
5. **Whether `m_response` should survive an abort.** §3.3 argues for keeping it; that is a judgement
   about what a caller wants from a cancelled chain, and it has not been checked against any caller,
   because there is none.
6. **Whether the same hole exists elsewhere in this layer.** The pool's own waiter and retry paths on
   cancellation were not read for this document. `RetryableWrapperTaskT` was checked and is correct;
   nothing else was.
7. **The `bodySource( ) -> rewind( )` on an abandoned 307/308 (§3.2)** is argued harmless from the
   interface's contract, not from any implementation of it.
8. **Nothing here has been reviewed.** Three findings in this batch were right in conclusion and
   wrong in premise; §1.4 corrects three such details in H22 itself, and this document is subject to
   the same failure mode.
9. **The reachability narrowing itself — added 2026-09-24 by the review (§9.7), and it was FALSE.**
   This list had eight items and omitted the one claim that decides how reachable the defect is: that
   the queue's cancellation API could not reach it. It was read from the lock and never checked
   against the window's own start. **Item 8 predicted exactly this, and this is the instance.** The
   claim is now corrected at §1.2, §2 and §8; the shape in §3 never depended on it.

## 8. Verdict

**Worth fixing, and worth fixing in this batch.** Not because it is likely — it is latent and has no
live caller — but because of what it costs to leave:

> **Corrected 2026-09-24 by the review (§9.2).** This paragraph said *"the queue's own cancellation
> API cannot reach it"*. **That is false**, and §1.2's own body said so — *"if `cancelAll` wins the
> lock the latch is set and the defect fires"* — under a heading claiming the opposite, which §2 and
> §8 then repeated. The queue lock serialises `flushInternal( cancelExecuting )` against
> `continuationTask( )` at **step 4 only**; the window opens at `m_isCompletionPending` in steps 1–3
> with **no queue lock held**. So `cancelAll( )`, `dispose( )` and `forceFlushNoThrow( )` all reach
> it: **any future caller's ordinary shutdown can produce the wrong answer.** This makes the fix more
> necessary, not less — it strengthens §4(a)'s rejection and weakens §4(d). The taken shape is
> unaffected, because it does not care who latched.

- the wrong answer it produces is **indistinguishable from a correct one** (§1.3), which is the
  property that makes this class of defect expensive rather than merely present;
- the fix is one conjunction in one function, under twenty lines, with no interface change and no
  existing case at risk (§6);
- the red is **deterministic** (§5), which is the condition this project attaches to a fix, and the
  same construction buys a guard against the fix over-reaching;
- and the layer is pre-caller, so the contract can still be set correctly for free. After L8 it
  cannot.

The honest counter-argument is §7.1 and §7.2 together: an unmeasured window on an API nobody calls.
If this design is not taken, the right disposition is **not** to leave it on a list a third time but
to record the boundary decision of §3.1 in `SessionRequestTaskT`'s own comment — that a cancel
latched between hops deliberately reports the intermediate response — so that the next reader finds
a choice rather than an oversight.

---

## 9. Review — 2026-09-24

Read against `lazari2` at the source: `ClientSession.h`, `TaskBase.h`, `ExecutionQueueImpl.h`,
`HttpClientRequestTask.h`, `RedirectPolicy.h`, `Algorithms.h`, the six test headers that touch a
session task, and both recorded findings. No build.

**Verdict: may go to implementation, once the three premise corrections below are folded into the
document.** None of them changes the boundary of §3.1, the shape of §3.2, the spelling of §3.3 or
the rejection of §4(a). One of them changes §2 and §8 materially, and in the direction of making
the fix more necessary, not less.

### 9.1 Verified and holding

- §1.1 is verbatim. `ForwarderTaskBaseT` has no state; `isCanceled( )` is a `TaskBase` member, not
  on the `Task` interface, and the forwarder does not forward it — so **§1.3's tuple really is
  everything a caller can see, and it is identical to every policy refusal.** The severity
  sharpening stands.
- §1.2 steps 1–4 are the code: `applyEvents( )` applies under the lock, runs the deferred list off
  it, sets `m_isCompleted` under it again, and `notifyReadyImpl( )` leaves the hop in
  `PendingCompletion` and releases before `cbReady( )`; `onReady( )` takes the queue's `m_lock`,
  then `continuationTask( )`, then `setCompletedState( )` only when the continuation is not self.
- §1.4(1) stands: the increment is the last statement of `chkPrepareNextHop( )` and the cancel limb
  returns before it. The interface says `redirectHops( )` is "how many redirects were followed",
  and an aborted one was not. L6's "one short" is ambiguous rather than wrong if it meant "one
  short of the hop the chain was about to follow"; keep the correction, say the ambiguity.
- §2's "no live caller": `createRequestTask( )` is defined and commented in `ClientSession.h` and
  called nowhere else under `src/` outside the tests. The `TestHttp2DriverAccounting.h` comment is
  as cited.
- §3.2: `RedirectPolicy::evaluate( )` is `const` and documented as "a pure decision … holds no
  state and does not count hops", so the two accepted side effects (`m_next`, `rewind( )`) are the
  complete list. The increment precedes `startHop( )` on every path today, including the one where
  `chkRemainingBudget( )` throws, so the move is neutral there too; all seven `redirectHops( )`
  assertions in the tree read after completion.
- §3.3: `applyStopped( )` builds exactly `UnexpectedException` marked expected, carrying
  `errinfo_error_code( operation_aborted )`, message "The HTTP request was cancelled";
  `CancelResetsTheStreamTests` asserts on `was cancelled`. §3.4's order (wrapper lock → hop lock)
  exists in `requestCancel( )` and the reverse does not.
- §4(a)'s rejection is right — and stronger than written, see 9.2.
- §5.1: `ConnectOrderProbe` is the idiom claimed. The probe's `requestCancel( )` under the queue lock
  does not re-enter the queue: the hop's `requestCancel( )` marks and posts to its mailbox, nothing
  else.
- §6: none of the six files that touch a session task calls `requestCancel( )`, `cancelAll( )`,
  `flush( … cancelExecuting )` or `forceFlushNoThrow( )` on one; every runner is
  `push_back → wait( task )` before `flushAndDiscardReady( )`, and `session -> dispose( )` disposes
  the pool, not a queue. **No existing case has a chain in flight at a cancelling teardown.** The
  three failure-expecting cases (`:1420`, `:2216`, `:2402`) prove `runSessionTask( )` tolerates a
  failed task, so the red needs no new runner. `utf_baselib_httpclient6` is one 377-line header,
  already includes `Http2TestServer.h`, and is `devenv7_only` like the other three.

### 9.2 Refuted: "a cancel through the queue can never land in this window" (§1.2, §2, §8)

The queue's lock serialises `flushInternal( cancelExecuting )` against `continuationTask( )` —
**step 4 only.** The window, by §1.2's own definition, opens at `m_isCompletionPending` inside the
drain — steps 1 to 3 — where no queue lock is held. `cancelAll( )`, `dispose( )` and
`forceFlushNoThrow( )` all reach `flushInternal( … cancelExecuting = true )`, which calls
`requestCancel( )` on every *executing* task; the session task is executing until `onReady( )`
completes it. So: the hop's completion goes pending, `cancelAll( )` takes the queue lock, latches
the session and forwards to the hop, where `applyStopped( )`'s guard discards it; `cancelAll( )`
releases (or waits on the condition variable); `onReady( )` acquires, `continuationTask( )` reads
the latch — the defect fires. §1.2's own body says exactly this ("If `cancelAll` wins the lock the
latch is set and the defect fires") one sentence under a heading that says the opposite. What the
queue lock rules out is a cancel interleaving *inside* `continuationTask( )`, which the wrapper's
own `m_lock` already rules out.

Consequences, none of them for the shape:

- §2 and §8 must say the ordinary **shutdown** of any future caller can produce the wrong answer,
  not only a direct `requestCancel( )` from another thread. The defect is still latent (no caller),
  but it is not narrow in the way claimed.
- §4(a)'s example — "cancels on shutdown … a complete, correct 200 replaced by `operation_aborted`"
  — is real and consistent only under this correction; the rejection is right and stronger.
- §4(d)'s "genuinely on the table" is weaker than the design thinks.
- §6's last table row includes a shutdown `cancelAll( )` during a chain, and
  `flush( cancelExecuting = true, nothrowIfFailed = false )` would surface it as a throw. That is the
  intended answer and is worth one sentence.

### 9.3 Wrong premise, right conclusion: §3.3's "required" `errinfo_is_expected`

`notifyReadyImpl( )` has already run for the hop when `continuationTask( )` sets the exception, and
`m_notifyCalled` stops it running again; the dump decision was taken then, from the hop's own
`isExpected` argument. Nothing in `ExecutionQueueImpl` or the test helpers reads the tag afterward.
Keep the mark — it is the shape `applyStopped( )` produces, `RetryableWrapperTaskT` does the same,
and `CmdLineAppBase` and `BackendProcessingBase` read it — but the stated reason is not the reason.

### 9.4 §1.4's third correction misreads H22

H22 says "validate a controlled pause between finalizing the 3xx hop and creating the next one".
That does not say sleep. A *controlled* pause is a control point, and the probe of §5.1 is one — the
design's construction **satisfies** H22's correction as written rather than replacing it. Overturning
a recorded claim needs the claim quoted; this one was paraphrased into something it does not say.

### 9.5 §5.1's sketch drops the probe out of the chain after the first hop

`base_type::continuationTask( )` on a `WrapperTaskBase` resolves to
`ForwarderTaskBaseT::continuationTask( )`, which returns whatever the session returns — the
**session** task, not the probe. `onReady( )` then fails `om::areEqual( continuation, task )`,
pushes the session as a new queue entry and completes the probe. So `eq -> wait( probe )` returns
after hop 1, and **case 2 — armed on the second hop — never fires and is green for the wrong reason**,
a guard that guards nothing. Case 1 still works. The spelling that keeps the probe in the chain is
`base_type::handleContinuationForward( )` — what `SessionRequestTaskT` and `RetryableWrapperTaskT`
themselves do — which swaps `m_wrappedTask` and returns `this`, so `onReady( )`'s `continuationIsSelf`
branch re-schedules the probe and the next hop's `cbReady` is bound to it again. Two smaller things
in the same sketch: the arming predicate is inverted (`! m_armed.exchange( false )` cancels on the
un-armed call), and case 2 needs a hop counter, not a bool. §7.3 stays, sharpened: "composes" must
include "stays in the chain across hops", and case 2 should record that its override ran on hop 2.

Also: the forwarded cancel is discarded **asynchronously** — posted to the mailbox, drained on the
pool — not at the probe's call site. The outcome is the same, but "exercised rather than assumed" is
a little strong; nothing in the test observes the discard.

### 9.6 Smaller corrections

- Preamble: L6's `:1255-1258` today lands in the tail of `absorbResponse( )` (`storeCookies( );
  decodeBody( );`), not "inside `decodeBody( )`".
- §5.2 case 1: only the `isFailed( )` assertion discriminates. "The peer never received `/final`" is
  green on both sides — unfixed code also returns `nullptr` on the latch — so it must not be read
  as part of the control.
- §2 names `cancelTask( )` among the queue's routes; that is the `TaskBase` virtual, not a queue
  method. The queue's `cancel( task, … )` never reaches an executing task, as the
  `TestHttp2DriverAccounting.h` comment records.

### 9.7 The ninth thing §7 does not establish

**The reachability narrowing itself.** §7 lists eight open items and omits the one claim that
decides how reachable the defect is — that the queue's cancellation API cannot reach it — which was
read from the lock and never checked against the window's own start. It is false (9.2). Add it as
item 9 and resolve it in the same edit.

### 9.8 On the alternative disposition

Agreed: if the fix is not taken, a comment in `SessionRequestTaskT`, not a third list entry. But the
comment must record the boundary truthfully, including that the queue's own `cancelAll( )` can latch
after a hop completed — a comment repeating §2's "unreachable from the queue" would be a comment
outliving a claim that was never true. With 9.2 in hand the case for taking the fix is stronger than
§8 states, and the honest counter-argument shrinks to §7.2 alone.

### 9.9 Not settled by reading

1. That a test-local `WrapperTaskBase` over `SessionRequestTaskT` compiles and, with
   `handleContinuationForward( )`, stays in the chain — never built here (§7.3).
2. Which `wait( task )` semantics let a failed task past `flushAndDiscardReady( )` — verified by the
   three existing cases, not by reading `wait( )`.
3. Whether the L8 facade will shut down through `cancelAll( )`/`dispose( )` — §7.2, now with the
   queue route in scope.
4. `utf_baselib_httpclient6`'s size after the additions — §6 already says measure.

---

## 10. Implementation review — 2026-09-24

Reviewed: branch `h22-cancel-between-hops`, `101be15` (the fix) and `21b0c37` (the manifest), two
commits off `lazari2` @ `3430966`. Read whole: `SessionRequestTaskT` after the fix and
`continuationTask( )` / `chkPrepareNextHop( )` before it (`git show 3430966:`), the new case file,
the module note, `ForwarderTaskBaseT` / `WrapperTaskBaseT` / `RetryableWrapperTaskT` /
`TaskBase::notifyReadyImpl( )` / `scheduleNothrow( )`, `ExecutionQueueImpl::onReady( )` /
`waitInternal( )` / `flushInternal( )`, `HttpClientRequestTaskT::applyStopped( )` /
`requestCancel( )`. Every run log under `http2-l0-state/logs/lane3-h22/` read, not the table. Sizes
re-read from the objects on disk with `scripts/utests/utf_objsize.py`, read-only. No build.

**Verdict: accept both commits.** The boundary is the one §3.1 chose and it is implemented on every
path; the red is deterministic and fails on the discriminating read; the blast radius is the three
modules the lane names and they are green; the size decision is right and its recorded reason
suffices, with one clause to correct. Nothing found blocks the merge. The items below are ordered
hardest first, then the judgements asked for, then what this review corrects in §9.

### 10.1 The boundary — verified on every path

The four-way table of (hop exception, latch) against the pre-fix text at `3430966`:

| hop exception | latch | before | after |
|---|---|---|---|
| set | set | `nullptr`, hop's exception (the latch read came first) | `nullptr`, hop's exception (the latch read is now inside the limb) |
| set | clear | `chkPrepareRetry( )` → retry or `nullptr` | identical |
| none | clear | `chkPrepareNextHop( )` → next hop or `nullptr` | identical, increment moved out of the predicate |
| none | set | **`nullptr`, no exception** — the defect | `chkPrepareNextHop( )` first: `false` → the hop is the answer; `true` → `failChainAsCancelled( )` |

The failure limb is unchanged **in effect**: the only textual change is that its early return moved
from above `if( m_hop -> exception() )` to the first statement inside it, and every path inside the
limb returns.

The `m_hops` move is neutral everywhere else, and this was checked against the three readers of
the value rather than asserted: `RedirectPolicy::evaluate( )` receives `m_hops.value( )` as
`hopsSoFar` before the increment in both versions (the increment was the predicate's *last*
statement); `chkRemainingBudget( )`'s message reads `m_hops` from inside `startHop( )`, which the
increment precedes in both versions, including the path where that check throws; and the retry limb
calls `startHop( )` without an increment in both. `redirectHops( )` is "how many redirects were
followed" and a decided-then-abandoned one now reads 0, which the interface doc supports.

One corollary the design accepted implicitly and did not name: on the (none, set) path the
predicate's own throw sites — `m_next.url( )` allocation, `bodySource( ) -> rewind( )` on a 307/308
— are now reachable where the pre-fix latch returned first. A throw there leaves `continuationTask( )`
through `onReady( )`'s `task -> exception( continuationException )`, i.e. still a *failed* chain,
with the throw in place of `operation_aborted`. The polarity is right and a `rewind( )` that throws
on a source `chkRequestMayBeReplayed( )` already vetted is the source's defect. Accepted, recorded.

### 10.2 §9.5 confirmed by experiment — and the lane's "one degree better" is right

`probe-counterfactual-run.log`, built with `base_type::continuationTask( )` on purpose: case 2
fails at `TestClientSessionCancel.h(497)`, `EQUAL( probe -> hopsObserved(), 2U )`, lhs 1 — the
probe dropped out of the chain after hop 1 exactly as §9.5 derived. Agreed that this is better than
§9.5 stated: §9.5 described the *sketch*, which had no counter, as silent, and prescribed the
counter; asserted first in case 2, the counter makes the wrong spelling loud.

What the same log does **not** show: the 53.8 ms for case 1 against 2.5 ms is not evidence about
the spelling. By reading, case 1 is identical under both spellings — each calls the session's
`continuationTask( )` once under the probe's lock and each returns `nullptr` when it does. The log
line under it — *"waiting for 1 outstanding connections to shutdown"*, then *"closed"* ~52 ms
later — is the peer's shutdown poll finding the client's connection still open at teardown, the
same ~54 ms that shows on every TLS case in `hc5-run.log`. A teardown artefact; the evidence is
case 2's counter and nothing else.

### 10.3 `failChainAsCancelled( )` without a guard — the invariant holds

It is reached only after `if( m_hop -> exception() )`, every path of which returns, so the hop had
no exception at that read. Between the read and the set, under the wrapper's lock, nothing can put
one there: `notifyReadyImpl( )` has already run for this hop — that is how `continuationTask( )`
came to be called — and `m_notifyCalled` refuses a second; the forwarded cancel only marks and
posts to the mailbox, and `applyStopped( )` returns on `m_isCompleted || m_isCompletionPending`;
`onReady( )`'s own `task -> exception( … )` is the same thread, after return. `RetryableWrapperTaskT`
carries its guard because its cancel limb sits *above* its exception check and serves a failed and
a succeeded wrapped task alike; here the limbs are split, so the guard would guard nothing. "An
invariant of the placement" is the right description. Optional, not required:
`BL_ASSERT( ! m_hop -> exception() )` at the top of the helper (`BOOST_ASSERT`, debug only) would
make the placement self-checking against a future move of the call.

### 10.4 The red — read from the logs

`red-run.log`: `TestClientSessionCancel.h(409): fatal error … critical check ( task -> isFailed() )
has failed`, 1.9 ms, no hang, no timeout; case 2 green in the same run. `green-run.log`,
`final-hc6-run.log`, `final2-hc6-run.log`: 3 of 3. Two precisions the table cannot carry:

- Because `UTF_REQUIRE` is fatal, **none of case 1's later assertions ran against the unfixed
  code**. The header's "green on both sides" for `redirectHops( ) == 0`, `finalRequests == 0` and
  `hopsObserved( ) == 1` is established by reading — the pre-fix latch returned before
  `chkPrepareNextHop( )`, so the increment inside it never ran — and the reading is correct. It is
  by reading, not by run.
- The source state of the red build is not in the log; what is in the log is a failure only the
  unfixed code produces.

### 10.5 Blast radius — the enumeration is complete

`#include <baselib/httpclient/ClientSession.h>` occurs in exactly five files under `src/` —
`httpclient4/TestClientSession.h`, `httpclient5/TestClientSessionTls.h`,
`httpclient5/TestClientSessionTlsHttp1.h`, `httpclient6/TestClientSessionIdle.h`,
`httpclient6/TestClientSessionCancel.h` — and in no umbrella: `httpclient/PreCompiled.h` does not
pull it in, and `ContentDecoder.h` names it in a comment only. `SessionRequestTaskT` is a template
reached only through `ClientSessionImplT`, so no other translation unit can instantiate it. The
seven pre-existing `redirectHops( )` assertions are all in `httpclient4/TestClientSession.h`
(`:609`, `:1203`, `:1304`, `:1312`, `:1428`, `:1524`, `:1665`), as is
`ClientSession_RequestBudgetIsChainedAcrossHopsTests` (`:1358`). `hc4-run.log`: *Running 20 test
cases … No errors detected*; `hc5-run.log`: 8; `hc6`: 3.

This corrects **§6 of this document**: its gating list named `utf_baselib_httpclient`, `7` and the
`h2client` family as suites that must run "because `SessionRequestTaskT` sits under all of them".
It does not; they do not compile the class. The project's whole-suite gate for a core change
applies as a rule, but a regression from this edit cannot appear there.

### 10.6 The module size — judged

Figures, re-read from the lane's objects with the project's own tool: `httpclient6` **42.4 MB**
(21.4 above the ~21 MB floor), `httpclient5` 47.4, `httpclient4` 49.0, a64 clang debug.

**The recorded reason suffices, and the module is the right one.** The reason is not "it is only
2.4 over"; it is that no home under target exists for a session-level case that needs a peer: the
module's own measured grouping put the floor plus the session with both drivers at 39.0 before any
peer, and the peer's marginal cost inside such a TU is 3.3, so any module holding these cases sits
at about 42 or above. Given that, `httpclient6` is the smallest of the three that can host them,
adds nothing to the two largest, keeps the family's peak at `httpclient4`'s 49.0, and pays no
second floor. `src/utests/AGENTS.md`'s "needs a recorded reason" is met by the note as written.

Two corrections to how the decision is *described*:

1. **The old note's prediction was right in its conclusion and wrong in its magnitude.** It said a
   case instantiating a peer "would not fit" — and it does not fit: 42.4 is over target. What was
   wrong was the 8.6 it converted from `httpclient4`, against a measured 3.3. The commit message's
   "the note's prediction was wrong" overturns a claim that held; the replacement note in
   `UtfBaselibHttpClient6Main.cpp` is more careful but still reads as a rebuke. One clause fixes it:
   *the magnitude was converted and wrong; the conclusion, over target, was right; the reason for
   going over anyway is that nothing can be under.* This is the batch's recurring failure — a
   conclusion judged by its premise — in the other direction.
2. **"A sibling would measure about the same" is derived, not measured.** It is derived from this
   module's own grouping and its own marginal, which is the right way to derive it, but
   `src/utests/AGENTS.md` says build the grouping. One clang-debug build of a sibling holding only
   `TestClientSessionCancel.h` costs about twenty seconds on this host and would turn the argument
   into a number. Worth doing if the orchestrator wants a number; not a condition of acceptance.

**x86 debug**, the governing measurement, is unmeasured here and unrecorded anywhere in `notes/`
for any of `httpclient4`/`5`/`6`. It is not the deciding number for *this* choice: the ceiling
question is the family's peak, and that is `httpclient4`, not `6`; if `6` were near 75 on x86,
`4` and `5` would already be over it. The Windows matrix round is where the figure comes from.

### 10.7 The stale creation figures — judged

Verified at the creation commit `0af7934`: `httpclient4`'s note then said 48.0 and `httpclient5`'s
said 46.0, so the figures in *"WHY THIS MODULE EXISTS"* were true when written. The **tense** is
what has outlived them: "`httpclient4` **is** 48.0 MB … `httpclient5` **is** 46.0", in the present,
two paragraphs above a paragraph this commit rewrote, while the objects measure 49.0 and 47.4.
Keeping the numbers is right — they are the reason the module exists; keeping "is" is the
comment-outlives-its-fix pattern. Recommend *"was 48.0 … and 46.0 when this module was created"*:
a wording change inside lines already in this commit's neighbourhood, no line-count change, and
`Main.cpp` carries no `__LINE__`-anchored case.

### 10.8 The other claims checked

- **9.9.1 settled.** The probe compiles and stays in the chain: `hopsObserved( ) == 2` is green in
  three runs, and the counterfactual shows the assertion catching the other spelling.
- **9.9.4 settled.** `isExpectedOperationAborted( )` reads the code and the mark, the message is
  asserted, the type is `UnexpectedException`; the shape is a tested contract. One residual
  difference from `applyStopped( )`'s shape, recorded so nobody "fixes" it: the hop builds its
  exception through `createException<>( )`, which calls `enhanceException( )` and attaches
  `errinfo_task_info`; `failChainAsCancelled( )` cannot — `enhanceException( )` is `protected` on
  the hop — so the between-hops cancel carries `BL_EXCEPTION`'s function, file, line and time and
  no task info. Type, code, mark and message are the same; a log reader sees no task info.
  Acceptable.
- **§9.7's ninth item** is in `continuationTask( )`'s comment (the `cancelAll( )`, `dispose( )`,
  `forceFlushNoThrow( )` sentence), as §9.8 asked. **This document's body still carries the refuted
  claim**: §1.2's bold bullet and §2's bold opening say the queue cannot reach the window, and §7
  has eight items; only §8 carries a correction blockquote (added in `3430966`, with §9). A reader
  of §2 alone is misled. Recommend the same blockquote at §1.2's bullet and §2's opening and the
  ninth item in §7. Not done here: this review was asked for as an appended section.
- **The concurrency limitation in the test header** (lines 54–59) says what is true, as qualified.
  `storeCookies( )` and `decodeBody( )` run inside `absorbResponse( )` under `base_type::m_lock`; a
  cancelling thread sets the latch lock-free and then blocks in the forward on that lock; the latch
  is not readable from a hook (the session is not a `TaskBase`, `m_cancelRequested` is protected).
  The deferred phase and the pool's `releaseStream( )` are in the window and are not black-box
  reachable, which the header's "a black-box test can reach" covers. The probe exercises the late
  end of the window — completion published, decision not taken; a cancel in §1.2's steps 1–3
  reaches the same latch and the same read.
- **"Discarded asynchronously, on the pool"** — the probe's comment now says this correctly; §9.5's
  "exercised rather than assumed" objection is met.
- **The manifest** (`21b0c37`): the two case entries carry the right lines (374, 462), the
  `sessioncancel` block at 71, the `utest` namespace at 69, the include list matches the header, the
  two `notes_cases` match `notes.txt`. Mechanical and consistent; the tier-1 gate's own run is the
  lane's claim.

### 10.9 Comments the lane did not touch, checked for the same failure

- `chkRemainingBudget( )`: *"No case reaches this throw"* — still true; nothing under `src/utests/`
  asserts on "budget was spent". The budget case pins the chaining, not the throw.
- `m_cancelRequested`'s member comment and `startHop( )`'s "assigned directly" comment — still true.
- `RedirectPolicy::evaluate( )` "does not count hops" — still true, and now the predicate does not
  either; the two agree.
- `Http2ConnectionTask.h:3198`, `TestHttp2ConnectionTask.h:1792`, `:1922` — describe
  `chkPrepareNextHop( )` dropping the body and touching the headers; that part did not move.
- `httpclient4`'s note, *"SPLITTING WAS CONSIDERED AND MEASURED"*: its 8.6 and 5.8 are "from the
  figures h2client2 and httpclient3 recorded" — converted from other modules, the same conversion
  the old `httpclient6` note is faulted for. The lane's 3.3 is the first *measured* marginal for
  the peer inside a session TU. It does not overturn that note's conclusion — a peer half at
  ~21 + 15 + 3.3 lands at about 39–40, at target rather than under it, and the HttpServer half is
  still ~42, still +21 in total — but the next person to cut `httpclient4` should measure rather
  than convert. Not this change's business.
- `astra-remediation-owed-work.md` row **16a** and L6 **finding 9** ("Mirror it"): the fix
  deliberately does not mirror, and §4(a) and the code comment say why. The ledger row needs its
  disposition line at merge; a one-line pointer at the L6 finding would stop the next reader
  reopening "mirror it". Orchestrator's call.

### 10.10 A pre-existing third spelling, outside this change-set

A cancel that lands after the latch read and before the new hop's `scheduleNothrow( )` marks the
hop first, and `TaskBase::scheduleNothrow( )` then throws `BL_THROW_EC( operation_aborted )` — a
`SystemException`, marked expected — which the failure limb hands to the caller as the hop's own
error. `HttpClientRequestTaskT` does not override `scheduleEvenIfAlreadyCanceled( )` (only
`TimerTaskBaseT` does). Unchanged by the fix, same window before and after; §3.3's "one event, one
type" holds at the hop boundary this change is about and not at the pre-start boundary. Record; do
not fix here.

### 10.11 What this review corrects in §9

- **§9.6's "only the `isFailed( )` assertion discriminates"** was loose. The message read and the
  code-and-mark read also fail against the unfixed code — there is no exception at all — so what
  discriminates is one *fact*, the verdict, expressed by four reads. The test header's
  "`isFailed( )` and the three reads that follow from it" says it correctly.
- **§9.1's `httpclient6` line** ("already includes `Http2TestServer.h`") was about the include and
  not the instantiation, as the lane says; it did not check the module's own note, which forbade
  what §6 directed. The lane read the note, measured, and recorded — which is the right response.

### 10.12 Not settled by this review

1. The x86 debug figure for `httpclient6` — and for `4` and `5`, which decide the ceiling question.
2. The sibling's size — derived at ~42, not built.
3. gcc debug and clang release — the orchestrator's cells, after merge.
4. The red build's source state — inferred from the failure it produced.
