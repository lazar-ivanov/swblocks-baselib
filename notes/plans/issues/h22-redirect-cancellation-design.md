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

- **A cancel through the queue can never land in this window on the same queue.**
  `ExecutionQueueImpl::cancelAll( )` and `flushInternal( ..., cancelExecuting = true )` call
  `requestCancel( )` on each executing task *while holding the queue's `m_lock`*, and `onReady( )`
  holds the same lock across `continuationTask( )`. The two are mutually exclusive. If `cancelAll`
  wins the lock the latch is set and the defect fires; if `onReady` wins, the next hop has already
  been created and started and the forwarded cancel reaches *it*, which is the correct outcome.
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

**Yes, but there is no live caller today, and there is no way to hit it from the execution queue's
own cancellation API.**

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
