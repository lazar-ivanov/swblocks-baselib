# H01's spurious reuse refusal — design

**Date:** 2026-09-23. **Status:** first draft, reviewed 2026-09-23 (§10) — **not agreed as first
written**; agreed to implement only in the corrected shape the review left in the body, after A2,
with §8's instrumented measurement as the gate. **Implemented on `h01-fix` @ `749943d`, reviewed
2026-09-23 (§12) — accepted for merge after A2 and A1-cleartext; variant and toolchain coverage is
the orchestrator's release runs.**

Owed item **6** on [`astra-remediation-owed-work.md`](astra-remediation-owed-work.md). The fix shape
recorded in [`initiate-close-teardown-design.md`](initiate-close-teardown-design.md) §16.6 **does not
survive re-derivation**; that is established, not in question here, and §3 says why. This document
replaces it.

Folded into this batch on 2026-09-23 rather than deferred, on the reasoning in §2: the defect
degrades the instrument every remaining change-set is judged against.

Line numbers re-derived on `lazari2` @ `a723cee`.

---

## 1. The defect

`finishStream( )` decides whether the connection may be reused:

```
const bool isReusable =
    isConnectionUsable && ! base_type::isClosing() && ! m_isWriteInFlight;    /* :1300-1301 */
```

and publishes the verdict in one critical section that **also clears the stream handle**
(`:1306-1324`): `m_handle` → `INVALID`, sink moved out, then `m_state` = `Ready` if reusable,
`Draining` otherwise.

`m_isWriteInFlight` is cleared by the write's own completion handler. When the **read** completion
reaches the strand ahead of the **write's** handler — the response having arrived while the write
was, as far as the strand is concerned, still outstanding — `finishStream( )` reads a stale `true`,
publishes `Draining`, and closes a connection with nothing whatever wrong with it.

§16.6's facts were re-verified against the tree by the 2026-09-23 re-derivation and **all hold**: the
mechanism, "pre-existing", "benign", "the safe direction", and that the case is right to be red on
it. Only its fix shape is wrong.

## 2. Why this is folded in rather than deferred

Two independent measurements, from lanes that did not share an approach:

| Measured | Where | Rate |
|---|---|---|
| `utf_baselib_httpclient3`, full-module runs | teardown §16.6 | **2/20 with the teardown fix, 3/20 without** — pre-existing |
| h1 over **TLS**, idle-close observing the `close_notify` | the `tls-h1-control` lane | **~1 in 8**; 4/25 as a session case, 3/25 at the driver |
| `utf_baselib_httpclient3`, full-module runs, **release** | the runbook's `httpclient3-flake.sh`, 2026-09-23 | 2/15 gcc, 1/15 clang — *added by the 2026-09-23 review*; the rate is variant-independent |

So **every release gate carries a 10–15 % false red**, and the gate is what every remaining
change-set is judged against. The TLS lane's traces name the mechanism from the other side: every
failing run has **no `Closing an idle HTTP/1.1 connection` line at all**, and the connection is gone
within 21 ms of the response — the spurious refusal closing it before the idle path is ever reached.

## 3. Why §16.6's recorded shape fails

It defers the verdict when **the write is still in flight, the connection is otherwise reusable, and
we are not closing** — which is *exactly* the conjunction `utf_baselib_httpclient7`'s two write-
barrier cases were built to make true. `TestHttp1DriverWriteBarrier.h:222-227` says so in its own
words: the peer answers 413 from the head alone *"so that `deriveIsReusable( )` has every reason to
call this connection reusable and the ONLY thing standing in its way is the write still in flight."*

The predicate therefore cannot separate the defect from the cases that forbid the fix, and teardown
§8.1 protects those four assertions explicitly (*"Nothing in this case should change"*). Both
readings break them — the verdict-only reading publishes a connection that is reusable **and
immediately submittable** with a write outstanding, re-opening H01's first face; the whole-stream-end
reading hangs both barrier cases at `waitForClosed( )`.

*Precision, 2026-09-23 review.* Both readings above are readings of §16.6's shape, which **waits for
the write** — `onWriteCompleted( )` publishes. The whole-stream-end reading hangs because in the
barrier cases that handler never runs. A deferral of **one strand hop** waits for nothing and cannot
hang, whichever half of `finishStream( )` it carries. So this sentence is a fact about *waiting*, not
about deferring the terminal callback — and §5 as first written took it for the latter, which is
where its split came from. See §5 and §10, finding 1.

**The distinction the predicate misses is not a state. It is time:** in `httpclient3` the write has
physically completed and its handler has merely not been dequeued; in `httpclient7` the write is
genuinely blocked behind 8 MB against a parked peer. At `:1300` the driver sees the same three bits
in both.

## 4. The mechanism, sharpened — and the measurement that decides the shape

**Strand ordering is not violated.** Both completions reach the driver through the socket's
executor, which is the strand the socket was built on (`TcpStrandedStreams.h:143-146`), and the
strand runs them in the order they were *enqueued on it*: whichever completion reaches
`strand_executor_service::enqueue( )` (`strand_executor_service.ipp:96-122`) first runs first.

*Corrected 2026-09-23 review — the paragraph as first written said the write's completion sits in
"the completing thread's private operation queue — asio's continuation path". Read against Boost
1.84 (the tree available here; the dist ships 1.90, not checked), that is not the path this write
takes.* `scheduler::post_immediate_completion( )` (`scheduler.ipp:346-367`) uses the private queue
only when `one_thread_ || is_continuation`; `one_thread_` is false for the pool's `io_service`
(default-constructed on Linux, `ThreadPoolImpl.h:189`; the Windows hint of 0 is still a locking
hint); and `is_continuation` for the driver's `async_write( )` is `write_op`'s hook
(`impl/write.hpp:375-381`), which at the first `async_write_some` defers to the user handler —
`cpp::bind` is `boost::bind` (`CPP.h:251`), for which the only hook is the default
`asio_handler_is_continuation( ... )` returning **false** (`handler_continuation_hook.hpp:46-49`).
The ssl `io_op`'s hook (`ssl/detail/io.hpp:332-337`) defers the same way. So a speculative write
completion is pushed on the **scheduler's public queue with a thread wake**, and it reaches the
strand when a scheduler thread pops it and dispatches it — normally within microseconds. That queue
is FIFO (`task_cleanup`/`work_cleanup`, `scheduler.ipp:73-74`, `:99-102`), so the read's
completion, which arrives later, cannot overtake the write's while both are *queued*; the read wins
only when the thread that has already popped the write's completion **stalls between the pop and
the strand enqueue** — the window §16.6 says a concurrent compile widens. (A write that takes more
than one `async_write_some` step is different: from the second step on `start_ == 0` and the hook
answers true, so the later steps' completions *do* take the private queue. A 32 KB body may well
have taken that path, which is one more reason the experiment below does not speak for the
small-request case.)

**And the race is not in the peer's timing.** The TLS lane established this much by trying to move
it: a 32 KB request body, so the peer could not answer until it had read the whole request, moved
the rate from 3 in 25 to 2 in 30 (`lane2.md:3786-3788`). *Corrected 2026-09-23 review:* that is
what the experiment shows, and it is all it shows. The peer's answer is not the racing party under
**any** candidate mechanism — in every one of them the read handler runs after the answer, and the
question is only where the write's completion is at that moment — so moving the answer cannot
distinguish "enqueue order" from "a thread stall". And 3/25 against 2/30 is not "no further":
Fisher's exact test gives p ≈ 0.65, so those counts cannot tell 12 % from 7 %, let alone from a rate
that did move.

**What §5 actually rests on, stated as the conditional it is.** One hop lands behind the write's
completion **only if that completion is already in the strand's queue when `finishStream( )`
posts** — that is, only if the stall that let the read win ended before the read handler reached
`finishStream( )`, tens of microseconds after it started. A stall of that length is possible; a
stall of *"hundreds of milliseconds"* — §16.6's own account of the widening, which §1 says holds —
ends long after the read handler has finished, the write's completion then reaches the strand
*behind* the continuation, and this fix does nothing for that occurrence. Which band the measured
rate lives in **cannot be settled by reading**; it is what §8.3's count is for, and §9 carries it as
unproven. The draft as first written put this in §9 as a caveat. It is the main question.

## 5. The shape

**One strand hop, deferring only the connection's reuse verdict — never the stream's end.**

`finishStream( )` does two things that this design separates:

1. **Retires the stream's parsing state** — resets the parser (`:1326`), clears the chunk and the
   per-message flags (`:1342-1345`), and clears the request-side state a `submit( )` handed over
   (`:1314-1315`). **Not deferred**, so that the read re-armed at `:1205` can never deliver into a
   message that has ended.
2. **Publishes the connection's reuse verdict, clears the handle, releases the sink, delivers the
   terminal callback, and closes or arms the idle timer** — `:1300-1324` less the parser, then
   `:1347-1364`. **Deferred by one `post` to the strand, as one unit and in today's order**, and
   only when the sole obstacle to reuse is `m_isWriteInFlight` —
   `isConnectionUsable && ! isClosing( ) && m_isWriteInFlight`.

*Corrected 2026-09-23 review.* As first written, item 1 also delivered `onClosed( )` synchronously
and only the verdict moved, on the premise that deferring the terminal callback "is the reading that
hangs the barrier cases". That premise is §16.6's wait-for-the-write shape (§3, *Precision*); one
hop waits for nothing and does not hang. What the split *did* do was invert the contract
`finishStream( )` states at `:1247-1250` — *"THE STATE IS SETTLED BEFORE THE SINK IS TOLD ... If
the verdict were applied after the event, that question would race the answer"* — and every observer
in the tree reads the verdict at `onClosed( )`: the barrier case
(`TestHttp1DriverWriteBarrier.h:279-294`, asserting at `:384`), the keep-alive reuse case
(`TestHttp1ConnectionTask.h:197-221`, `:254-255`), every `runExchange( )` case
(`Http1DriverTestUtils.h:1342-1349`), and the pool's `releaseStream( )` (`ConnectionPool.h:2390`),
which the request task calls from its own `onClosed( )`. §10, finding 1, lists what each of them
would have seen. Moving the callback with the verdict keeps the contract and removes §5.2's second constraint
outright.

**Where the deferral is reachable, so the analysis is bounded.** Of `finishStream( )`'s eight
callers, seven pass `isConnectionUsable == false` (`:601`, `:1029`, `:1124`, `:1143`, `:1152`,
`:1938`) or run before any write is issued (`:671`, the render failure). The deferral can be taken
from **one site only**: `onBytesRead( )`'s complete-message call at `:1042-1046`, with an empty
error code and `isRetryable == false`. Every deferred verdict is therefore the verdict on a
**complete, successful response**, and the request task's `outcomeOnClosed( )`
(`HttpClientRequestTask.h:1183-1197`), which reads `state( )` only on an error, never sees the
window. *(Added 2026-09-23 review.)*

**Why one hop is the whole fix, and why it is not a bounded wait.** If the write's completion is
already enqueued, the hop lands behind it, the handler clears `m_isWriteInFlight`, and the
continuation reads a true `false` — the connection is reused, provided the write *succeeded* (§5.3).
If the write is *genuinely* in flight, as in the barrier cases, nothing is enqueued ahead of us, the
continuation runs on the next strand turn and reads `true`, and the verdict is **today's**: not
reusable, close. One hop distinguishes *finished but not dequeued* from *actually still running*,
which is precisely the distinction §3 says the predicate cannot make. **It never waits for the
write**, which is why the barrier cases keep their timing. *2026-09-23 review:* the barrier-case
half of this paragraph is sound; the other half is §4's conditional — the write's completion is
"already enqueued" only for stalls shorter than the read handler's own run — and is what §8.3
measures rather than what this design establishes.

### 5.1 What the connection must look like inside the window — settled, not assumed

The window must not present the connection as dispatchable. It does not:

```
virtual std::size_t freeStreamSlots() const NOEXCEPT OVERRIDE        /* :1890 */
{
    BL_MUTEX_GUARD( m_stateLock );
    return ( Ready == m_state && INVALID_STREAM_HANDLE == m_handle ) ? 1U : 0U;
}
```

Both conditions are required. **So `m_handle` stays allocated until the continuation publishes the
verdict**, and through the window `submit( )` (`:1772-1778`) refuses and the pool's
`findDispatchable( )` (`ConnectionPool.h:1519-1521`) passes over the entry. The verdict-only
reading's red at `:445-447` (`INVALID == secondHandle`) is closed by construction, and `m_state` is
never given a value it would have to take back.

*Corrected 2026-09-23 review — two claims withdrawn.* **"No new state is needed"** is false on
§5.3's account: the continuation needs the write's *outcome*, which the flag does not carry. And
**"invisible to `ConnectionState` and to every consumer of it"** was true of `state( )`'s value and
false of the readers that combine it with `freeStreamSlots( )`: the pool's Ready arm at
`ConnectionPool.h:1329-1340` feeds `learnPeerLimit( )` (`:1133-1163`), which on a reading of `Ready`,
zero slots and `slotsInUse == 0` stores `peerLimit = 0` (`:1150-1153`). Today that combination
cannot occur — `slotsInUse` reaches zero in `releaseStream( )`, after the handle is cleared — so
under the split it would have been a state no reader was written for. `capacityOf( )` (`:1062-1073`)
happens to read 0 as "unknown", so for h1 the damage was one capacity of one; the point is the class
of reader, not this instance. **With the callback moved into the continuation (§5, corrected), the
window's observable state is exactly the in-flight state** — handle allocated, sink held, `Ready`
unchanged — and no reader can tell the window from the request still running, which is the property
this section's title claims.

### 5.2 Three constraints that fall out, each of which fails a case if missed

1. **The continuation must be an *accounted* handler** — `beginOperation( )` before the post,
   completed after. Otherwise `closeConnection( )` reached from it never reaches `initiateClose( )`
   and `taskEndedUnaided` goes red. `chkArmIdleTimer( )` (`:1443-1463`) is the model: `beginOperation( )`,
   then the initiator in a `try`, with a `catch` completing the operation — *"the same guard the read
   and the write carry, and for the same reason"*. *2026-09-23 review, verified:* `beginClose( )`
   (`MultiOperationTask.h:239-244`) only sets the flags, and `initiateClose( )` runs only from
   `onOperationCompleted( )` (`:371-376`), so an unaccounted continuation that closes wakes nothing
   in the barrier case — read blocked, write blocked, no timer live — and the case reds on its first
   `chkOrFail`. **The continuation must also carry the handle it was posted for** and do nothing but
   complete its operation if `m_handle` no longer matches: a `cancel( )` landing in the window
   (`:1825` → `:1928`) ends the stream itself, and `onTaskStoppedNothrow( )` cannot, because an
   accounted operation keeps the terminal from being taken.
2. ~~**The pool must be re-examined after the verdict lands.**~~ *Dissolved, 2026-09-23 review.* As
   first written: the pool's path — `releaseStream( )` → `examineAll( )` → `findDispatchable( )` —
   dispatches only on `Ready == state && freeStreamSlots( ) > 0`, and with the handle still
   allocated it would see zero slots, so a queued request would sit until the next maintenance tick
   (10 ms, up to 250 ms; `ConnectionPool.h:210-211`), and "the continuation owes the pool a poke".
   That was a consequence of the split, not of the deferral: with the terminal callback inside the
   continuation (§5, corrected), `releaseStream( )` runs after the verdict, as it does today, and on
   the fixed path sees `Ready` and one free slot. No poke is owed and no mechanism is needed.
3. **The idle timer must be armed once, not twice.** `chkArmIdleTimer( )` allocates a fresh
   `deadline_timer` and begins an operation on every call (`:1431-1443`), so two arms cost a timer
   object and an accounting round trip per response. The accounting survives — the destroyed timer's
   wait completes `operation_aborted` — but the two sites must be made exclusive deliberately rather
   than by luck. *2026-09-23 review:* with the handle clear inside the continuation the exclusivity
   is **structural** — the gate at `:1421-1429` returns before allocating while `activeHandle( )` is
   allocated, so the `finishStream( )`-side arm is inert through the window and the continuation's
   is the only one. Say so in the code rather than rely on it silently.

### 5.3 One consequence §16.6 does not mention, and it is a real hazard

**The write's own failure path changes.** `isOurOwnTeardown = ec && isClosing( )` is computed before
the write handler's prolog (`:779`). Today a write that fails *after* the response was read finds
`isClosing( )` true, because `finishStream( )` called `closeConnection( )` synchronously; under the
deferral the close is one hop later, and a failed write whose handler was enqueued between the read
handler and the continuation reaches `BL_TASKS_HANDLER_CHK_EC( ec )` (`:820-823`) with
`isClosing( )` false — the task **fails**, on a connection whose complete response has already been
reported to the sink as a success.

*Corrected 2026-09-23 review — the premise, not the conclusion.* As first written this said the
hazard "is the barrier case's own path, where the peer's RST produces `broken_pipe`". It is not. The
barrier case's write is poisoned by **our own** `initiateClose( )` (`:1556-1563`), which under any
accounted deferral still runs from the continuation's epilog *after* `closeConnection( )`, so
`isClosing( )` is true when that handler runs, exactly as today; and the case's peer sends no RST
until the task has ended (`TestHttp1DriverWriteBarrier.h:305-327`). The path that changes is
**A2's red** (`driver-read-write-arms-design.md` §2.3): a peer that answers from the head and resets
with the upload unread. There the write fails with a *peer's* code while nothing has closed, and
today it is excused only because the synchronous close got there first. So the sequencing conclusion
stands on a different premise: **H01 lands after A2**, whose arm (ii) excuses that code without
asking `isClosing( )`.

**And A2's shape (ii) creates a second hazard this design must close, which neither document
records.** Shape (ii) *"declines to fail the task and does nothing else"* (arms design §10.1): the
handler clears `m_isWriteInFlight` (`:791`), releases the buffers, and closes nothing, leaving the
classification to the read. Under this deferral a continuation that reads the **flag** alone then
finds it clear, `isClosing( )` false, and publishes **`Ready` on a connection the write handler has
just learned is dead** — until the read's own reset arrives one handler later. That is one strand
turn in which `submit( )` accepts. So the continuation's verdict must be computed from the write's
**outcome**, recorded by `onWriteCompleted( )` (success, or the code it excused), and not from the
flag. This is the one piece of new state the change needs, and §5.1's "no new state" is withdrawn
on this account.

## 6. The alternative, and why it is not taken

**A short deadline timer** — wait a bounded time for the write, then decide. It is deterministically
testable, which is a real advantage: a peer that answers from the head, stops reading, then drains
inside the bound is a clean red/green discriminator with no race at all.

**Rejected** on two grounds. It is a *bounded drain*, which `finishStream( )` rejects in principle at
`:1278-1279` *(citation corrected 2026-09-23 review)*. And it delays `onClosed` on the stalled path, which is the path the barrier cases
measure — buying determinism in the test by changing the behaviour under test.

## 7. What must not move

`utf_baselib_httpclient7`'s two write-barrier cases, all four assertions each, per teardown §8.1.

*Corrected 2026-09-23 review.* As first written this said the cases "take **today's** path". They
do not, and cannot: their three bits *are* the deferral predicate (§3), so **both cases take the
deferral on every run**. What is true is narrower — nothing is enqueued ahead of the continuation
(the write is blocked in the reactor, the read is armed and silent, no timer is live), so the
continuation runs on the very next strand turn, reads `m_isWriteInFlight == true`, and publishes
**today's verdict**. The difference between "today's path" and "today's verdict one hop later" is
exactly where the split failed: `runBlockedUpload( )` reads `state( )` (`:293`) the moment
`waitForClosed( )` returns (`:279`), and with the callback ahead of the verdict that read raced the
continuation for a `Ready` the case asserts against (`:384`). With the callback inside the
continuation the read follows the verdict, as today. The remaining assertions hold by the same
order: `freeSlots == 0` (`:386`, handle cleared with `Draining`), `secondHandle` refused
(`:445-447`), `taskEndedUnaided` (the continuation is accounted, §5.2.1, so its epilog reaches
`initiateClose( )`), and `! taskFailed` (the poisoned write finds `isClosing( )` true, §5.3).
**That must still be verified by running them, and by reading the continuation against the four
reads at `:279-312`, not argued.** §16.6's recorded shape died on this kind of reasoning; so did
this design's first draft.

## 8. The red

*Rewritten 2026-09-23 review, folding in the maintainer's decision of the same day (§8.1). As first
written this section offered two candidates — the rate, labelled weaker, and a strand-level no-op
continuation — and said the change does not land if neither can be built.*

The window is *"the write's completion is dispatched but its handler has not run"*, and §4
(corrected) says why no peer-side lever reaches it: every knob a test has — the peer's reading, the
peer's answer, the request size — moves the **write**, and the racing party is the scheduler thread
carrying the write's completion. The TLS lane's 32 KB experiment is evidence of that much.

### 8.1 The evidence standard — the maintainer's decision, 2026-09-23, scoped to this defect

`src/utests/AGENTS.md` holds that *"a negative control is the evidence; the run count is not"*.
**For this defect only, the maintainer accepts a rate-based red as sufficient**, on the reasoning
that the window is internal to our own strand scheduling and no peer-side lever reaches it, so a
control that makes the failure certain may not be buildable from the harness at all. This is a
**scoped exception**, recorded here and in `astra-remediation-owed-work.md`; it is not a relaxation
of the rule, and any other change-set citing it is citing it wrongly. A deterministic control
remains preferred if one is reachable (§8.5), and the rate is accepted only under the standard in
§8.2 and the two conditions in §8.3.

*Reviewer's note on the decision's premise, 2026-09-23.* The decision cites §4 as establishing that
the race is in the enqueue order, "which the 32 KB experiment demonstrated". §4 as corrected
establishes less: the experiment shows the peer's answer is not the racing party — true under every
candidate mechanism — and 3/25 → 2/30 is not a rate that "stopped moving" (p ≈ 0.65). The
conclusion the decision needs — *no peer-side lever* — survives on the corrected §4. The conclusion
it was written with but does not need — *the write's completion is already enqueued* — does not,
and that is why §8.3's count is part of the standard rather than an extra.

### 8.2 The standard

The instrument is **`utf_baselib_httpclient3` run whole**, which is how every before-rate was taken:
2/20 and 3/20 (debug, teardown §16.6), 2/15 gcc release and 1/15 clang release
(`httpclient3-flake.log`, 2026-09-23) — **8 of 70, ≈ 11 % per module run**, variant-independent.
One module run carries **six** exposures — the five `EQUAL( stateAfterResponse, Ready )` reads at
`TestHttp1ConnectionTask.h:363`, `:487`, `:521`, `:573`, `:597` and the reuse case's
`stateAfterFirst` / `slotsAfterFirst` / second `submit( )` at `:197-221` — so the per-exchange rate
is ≈ 2 %, and an isolated `--run_test=` of one case is a **different instrument with a different
rate**. The numbers below are for the module. The TLS instrument is not available: the lane's cases
that showed 1-in-8 were rewritten to issue no write, and the file is on `tls-h1-control`, not on
this tip.

- **Before: ≥ 100 module runs on the unfixed tree.** Expected ≈ 10 reds. **Three or fewer** means
  the baseline is not established (P ≈ 0.8 % at 10 %) and no comparison can be drawn. The existing
  20-run samples each show zero reds about 12 % of the time and are too small to be the before-side
  on their own; the four samples together, 8 of 70, are not — but they were not taken on the build
  the after-side will use, which is the point of the next bullet.
- **After: ≥ 300 module runs, zero reds.** By the rule of three the 95 % upper bound on a residual
  per-run rate is ≈ 1 %. 100 is not enough: a surviving 2 % goes unseen about 13 % of the time at
  n = 100.
- **Same host, same build, same variant, and conditions at least as adversarial as the
  before-side.** The before-rates were taken during ordinary lane activity, and
  `src/utests/AGENTS.md` records a flake that survived 240 idle runs and reproduced under a parallel
  build. Quiet after-runs against busy before-runs are a false comparison; §8.3's count is how the
  after-side proves it was not quiet.

### 8.3 What makes the rate sound — two conditions, both required

**The observable must be read after the verdict.** Every red above reads `state( )` the moment
`onClosed( )` returns. Under the split as first drafted that read landed *before* the continuation,
so it returned `Ready` on the fixed path and on the "fix did not apply" path alike — the
discriminator would have been **green by window**, and 300 clean runs would have measured the order
of two thread wakes, not the fix. With the callback inside the continuation (§5, corrected) the read
follows the verdict and the assertion means what it says. The lane confirms this order in the code
before the after-side is run.

**The mechanism must be counted, not inferred.** Two trace lines, at the level the existing
*"Closing an idle HTTP/1.1 connection"* line uses (`:1501-1507`) and the TLS lane's traces captured:
one in `finishStream( )` when the deferral is taken, one in the continuation stating whether it
found the write completed or still in flight. Over the after-side's 300 runs they turn the rate into
a coverage measurement. The number of deferrals taken is how often the race occurred — it should
track the before-rate, since the fix changes what follows the race and not its frequency, and a
count near zero says the runs were quiet and voids the comparison. The split of continuation
outcomes is the fix's coverage, **measured rather than bounded**. This is what distinguishes
*eliminated* from *reduced* at a resolution 300 runs cannot reach on reds alone, and it is what
decides §4's open conditional. Both lines stay in the tree; they cost nothing and they are the
record. *2026-09-23 implementation review:* that level is `Logging::trace( )` and the utests default
to `LL_DEBUG`, so the lines show only with `-- --bl-logging-level=6` — and the `--` is not optional,
since Boost.Test refuses an argument of its own it does not recognise. It cost the lane a batch that
looked 5 of 5 red (§12.6).

### 8.4 The residual risk, stated plainly

300 clean runs distinguish "eliminated" from "residual ≥ ~1 %" and nothing finer: a fix that took
11 % to 0.3 % passes 300 clean runs **about two times in five** (0.997³⁰⁰ ≈ 0.41); at 0.5 % about
one in five; at 1 % about one in twenty. §4's corrected conditional is exactly the shape that would
produce such a partial result — the hop helps only where the write's completion reached the strand
before the read handler reached `finishStream( )`, and §16.6's own account of the widening puts much
of the population outside that band. Without §8.3's count a partial fix would pass as a whole one;
with it, the residual is a number in the log.

**If the count shows low coverage** — deferrals taken, most resolved "still in flight" — this shape
has been refuted by measurement, the rate red stays in force as the gate's known flake, and the
alternatives are reopened: §6's bounded wait, or a verdict that waits for the write's *outcome*
without waiting for the write — a third connection-side state, "verdict pending", visible to nothing
but the two handlers — which this review did not design.

### 8.5 A deterministic control, if it is reachable

The window cannot be steered from the peer, and it cannot be steered from the strand either: the
test does not hold the strand, and FIFO defeats every ordering trick that goes through the
scheduler's queue. **The one seam that can produce the window on demand is `STREAM`.** The driver
is a template over its stream policy; a test-only policy whose `async_write_some( )` completes the
socket write and then *holds* the completion, releasing it onto the socket's executor only after the
read's completion has been enqueued, makes "read first, write's handler already queued behind it"
certain — red before, green after — and the same policy releasing it *after* the continuation would
have run demonstrates the fix's limit on demand. Whether `establishDriver( )`'s establisher and
factory can instantiate the driver over such a policy is a question of the harness's types, not of
the design, and is the lane's first hour. If it is reachable it is preferred over the rate, per
§8.1, and §8.3's count is then confirmation rather than the evidence.

## 9. What this design does not establish

*Rewritten 2026-09-23 review.*

- **Which band of stall lengths the measured rate lives in** (§4, corrected). Not provable by
  reading; §8.3's count decides it, and a low count refutes the shape (§8.4). As first written this
  list carried it as a caveat — "where it does not hold, this fix silently does nothing and the rate
  drops rather than reaching zero" — and that sentence stands, promoted.
- Whether the `STREAM`-seam control of §8.5 is reachable from the harness. Preferred, not blocking,
  per §8.1.
- ~~How the continuation pokes the pool.~~ Dissolved by the corrected §5; see §5.2.2.
- Whether §5.3's write-failure path is fully covered by A2's arm, or needs its own — and the
  continuation's use of the write's recorded outcome (§5.3, second hazard), which must be read
  against A2's landed shape. Determined by reading once A2 has landed.
- The asio reading in §4 is against Boost 1.84; the dist ships 1.90. The three facts it rests on —
  the private-queue condition, `write_op`'s hook, the default hook — are old and were not expected to
  move, but were not checked in 1.90.
- The per-exchange rate in §8.2 is derived from the module's rate and its six exposures, not
  measured per case.

## 10. Design review, 2026-09-23

**Reviewer: Claude Fable 5.1, on `lazari2` @ `a723cee`, read against the source at that tip.** Read
whole, from signature to end: h1's `onStartRequest( )`, `onWriteCompleted( )`, `scheduleRead( )`,
`deliverTrailers( )`, `onBytesRead( )`, `onPeerClosed( )`, `onReadCompleted( )`,
`activeHandle( )`, `tryGetActiveStream( )`, `finishStream( )`, `closeConnection( )`,
`chkArmIdleTimer( )`, `cancelIdleTimer( )`, `onIdleDeadline( )`, `initiateClose( )`,
`scheduleTask( )`, `onTaskStoppedNothrow( )`, `submit( )`, `cancel( )`, `freeStreamSlots( )`,
`state( )`, `onCancelStream( )`, `postToStreamExecutor( )` and the class comment;
`MultiOperationTask.h`'s accounting — `beginOperation( )`, `beginClose( )`,
`takeTerminalNoLock( )`, `applyDecision( )`, `onOperationCompleted( )`; the pool's
`releaseStream( )`, `findDispatchable( )`, the Ready arm of the entry refresh, `learnPeerLimit( )`,
`markPeerLimitKnown( )`, `capacityOf( )` and the maintenance interval; the request task's
`outcomeOnClosed( )` and `applyClosed( )`; `TcpStrandedStreams.h`'s `createSocket( )`;
`ThreadPoolImpl.h`'s `io_service` construction; `TestHttp1DriverWriteBarrier.h` whole;
`TestHttp1ConnectionTask.h`'s reuse case and every `stateAfterResponse` read;
`Http1DriverTestUtils.h`'s `RecordingSinkImpl::onClosed( )` / `waitForClosed( )`,
`establishDriver( )` and `runExchange( )`; teardown §2.2, §8.1, §13, §14 (head) and §16.6; the
arms design §2 and §10.1; the owed-work record with its uncommitted edits; both modules'
`notes.txt`; the runbook's `teardown-validate.sh`, `httpclient3-flake.sh` and its log; the TLS
lane's record (`lane2.md:3765-3830`); and, in Boost **1.84** — the tree available here; the dist
ships 1.90, which is not — `scheduler.ipp`'s `post_immediate_completion( )`, `task_cleanup`,
`work_cleanup` and the `one_thread_` rule, `concurrency_hint.hpp`,
`reactive_socket_service_base.hpp`'s `async_send( )` / `start_op( )`, `epoll_reactor.ipp`'s
`start_op( )` speculative path and `perform_io( )`, `impl/write.hpp`'s continuation hook,
`ssl/detail/io.hpp`'s hook and completion block, `strand_executor_service.{hpp,ipp}`'s
`enqueue( )`, `push_waiting_to_ready( )`, `do_execute( )` and the invoker, `impl/io_context.hpp`'s
inline-dispatch rule, and `handler_continuation_hook.hpp`. Nothing was built or run. The
maintainer's decision on §8 arrived mid-review and is folded in at §8.1.

**Verdict: not agreed as first written; agreed to implement in the corrected shape now in the
body, after A2, with §8's instrumented measurement as the gate — and not agreed that it fixes the
defect, because that is what the measurement decides.** The idea survives: one hop, never a wait
for the write, an accounted continuation, the handle kept allocated through the window. Two things
did not. The split of §5 — callback now, verdict later — inverted `finishStream( )`'s own contract
and broke every observer in the tree, including the red this design proposed to be measured by; and
§4's mechanism is not the path the driver's write takes, so the load-bearing claim that the write's
completion is "already enqueued" is a conditional whose truth is unknown. Both are the failure
classes the brief named: a premise carried over from the refuted shape, and a comment-grade claim
about asio that the sources do not bear. Each correction is written in place and dated; this
section says what was found and why.

**1. §5's split failed the same way §16.6 did — a predicate true of the barrier cases, and no way
to tell them from the defect at the moment the verdict is observed.** The draft kept `onClosed( )`
synchronous and deferred the verdict, on the premise that deferring the callback "hangs the barrier
cases". That premise is true of §16.6's shape, which waits for the write, and false of one hop,
which waits for nothing (§3, *Precision*). The split's real effect was to move the verdict *after*
the event every observer rendezvouses on, against `:1247-1250`. Verified consequences, each at the
source:

- `Http1Driver_WriteInFlightRefusesReuseTests` takes the deferral on every run — §3 says its bits
  are the predicate — and reads `state( )` at `:293` the moment `waitForClosed( )` returns at
  `:279`; with the verdict one hop behind the callback that read races the continuation and can
  return `Ready`, which `:384` asserts against. A spurious red on the case §7 promised not to move.
- `Http1Driver_RequestResponseAndKeepAliveReuseTests` reads `freeStreamSlots( )` at `:202` and
  submits again at `:214` right after `:197`; in the window the slot count is 0 (`:1890-1900`) and
  the second `submit( )` is refused (`:1772-1778`), against `:254-255` and `:218-221`. The fixed
  path, on the runs it applies to, would have turned today's flake into a different red.
- Every `runExchange( )` red (`Http1DriverTestUtils.h:1342-1349`) reads `state( )` before the
  continuation on either path — `Ready`, unchanged from the request — so the draft's §8.1
  discriminator (now §8.3) was **green by window**: 300 clean after-runs would have measured the
  order of two thread wakes, not the fix.
- The pool's `releaseStream( )` (`:2390`), called from the request task's `onClosed( )`, examined
  the entry with the handle still allocated: `findDispatchable( )` found no slot (the draft's
  "poke"), and the Ready arm at `:1329-1340` fed `learnPeerLimit( )` a reading — `Ready`, 0 slots,
  `slotsInUse == 0` — that stores `peerLimit = 0` at `:1150-1153`, a combination no reader was
  written for. Harmless for h1 by the accident that `capacityOf( )` reads 0 as unknown.

The correction moves the callback into the continuation with the verdict, in today's order. All
four consequences disappear, §5.2's poke dissolves, and the window's observable state becomes
exactly the in-flight state. The barrier cases' four assertions and `taskEndedUnaided` /
`! taskFailed` are re-derived in §7 under the corrected shape; they must still be run.

**2. §4's mechanism is not the driver's, and its experiment cannot carry the conclusion drawn from
it.** The write's speculative completion is pushed on the scheduler's public queue with a thread
wake, not the completing thread's private queue: `post_immediate_completion( )` takes the private
path only for `one_thread_ || is_continuation`, `one_thread_` is false for the pool's `io_service`,
and `is_continuation` is false for a `boost::bind` handler at `write_op`'s first step (Boost 1.84,
cited in §4). Under that path FIFO forbids the read's completion overtaking the write's while both
are queued; the read wins only when the thread that popped the write's completion stalls before the
strand enqueue, and the hop lands behind the write's completion only if that stall ended within the
read handler's own run. §16.6's "hundreds of milliseconds" is outside that band. The 32 KB
experiment moved the peer's answer, which is not the racing party under any mechanism, and
3/25 → 2/30 is p ≈ 0.65. **The fix's coverage is therefore unknown**, §9's caveat is the main case,
and §8.3's count is what settles it. The draft's model is internally consistent and would hold on
the continuation path — which a multi-step write does take from its second step on, so it may even
be what the 32 KB run exercised.

**3. §5.3 — right conclusion, wrong premise, and a second hazard.** The barrier case's poisoned
write is poisoned by our own `initiateClose( )`, which runs from the continuation's epilog after
`closeConnection( )`; `isClosing( )` is true for it under any accounted deferral, and the case's
peer holds its end until the task has ended (`:305-327`). The path that changes is A2's red — a peer
that resets with the upload unread — so **after A2** stands. But A2's chosen shape (ii) clears the
flag and closes nothing, so a continuation reading the flag would publish `Ready` on a connection
its write handler just learned was dead, for one strand turn. The continuation must use the write's
recorded outcome — new state, and the withdrawal of §5.1's "no new state".

**4. Verified and standing:** the mechanism and every fact §1 carries over from §16.6; §2's rates
(`lane2.md:3773-3777`, `:3786-3788`; the release probe adds 2/15 and 1/15); `freeStreamSlots( )`'s
two conditions (`:1890-1900`); §5.2.1's accounting claim (`MultiOperationTask.h:239-244`,
`:371-376`) — with the handle guard added for a `cancel( )` in the window; §5.2.3's cost, now
structural through the gate at `:1421-1429`; §6's rejection of the bounded wait, with its citation
moved to `:1278-1279`; the maintenance interval (`ConnectionPool.h:210-211`); and the reachability
fact added to §5 — the deferral is takeable from `:1042-1046` and nowhere else, which is what keeps
`outcomeOnClosed( )` out of the window.

**5. §8, rewritten around the maintainer's decision, with four corrections to its numbers and one
to its premise.** The instrument is the module, not an isolated case — the module carries six
exposures and its rate is ≈ 11 % per run over four samples in both variants, so "≥ 100 runs, expect
~10 reds" holds for the module and not for `--run_test=`. "Three or fewer" is the numeric floor for
a baseline not established. A 0.3 % residual passes 300 clean runs about two times in five, not
"likely". The TLS instrument does not exist on this tip. And the decision's stated premise — that
§4 established the enqueue-order mechanism — is narrowed to what §4 can carry; the decision itself
does not depend on the narrowed part. Two conditions are added without which the rate measures
nothing: the observable read after the verdict (finding 1), and the two-line count that turns the
rate into a coverage measurement and also proves the after-runs were not quiet (§8.3). The
`STREAM`-seam control is offered as the one deterministic lever the draft did not consider (§8.5);
whether the harness can instantiate the driver over a test policy is the lane's to settle, and it is
preferred if reachable.

**Precisions applied in place, none changing a decision:** the barrier comment's lines
(`:222-227`, §3); the drain sentence (`:1278-1279`, §6); `:445-447` (§5.1); the release probe's
row (§2); the status line.

**What this review does not claim.** Nothing was compiled or run. The asio reading is against 1.84,
not the 1.90 the dist ships; the three facts it uses are old and stable but were not checked there.
The FIFO argument in finding 2 is derived from the sources read, not observed. The per-exchange rate
is derived, not measured. Whether the `STREAM`-seam control is reachable was not attempted. And the
corrected shape was re-derived against the four reads in `runBlockedUpload( )` and the reuse case by
reading; §7 says it must still be run.

**Agreement.** With the in-place corrections taken as part of the design — the callback with the
verdict, the write's outcome recorded and used, the handle guard, the module as the instrument, the
count in §8.3 — the reviewer agrees this may be implemented **after A2**, as a change whose
acceptance is decided by §8.3's coverage count and §8.2's standard together, and records that a low
count is a refutation of the shape and not a partial success.

---

## 11. Coverage, measured 2026-09-23 — the condition of §10 is met

**94.5 %**, 290 of 307 occurrences. Wilson 95 % CI **[91.3 %, 96.5 %]**. 1370 whole-module runs of
`utf_baselib_httpclient3`, clang debug, five batches from quiet to continuous compile load. Probe on
`h01-coverage` (`0c2feca`, `757aefd`), driver diff **additive only**, every block marked *"remove
with the measurement"*.

**Residual per-run red after the fix: 1.24 %, CI [0.78 %, 1.98 %]**, against 18.3 % measured across
these runs. A **~15×** reduction, which is what H01 was folded into this batch to achieve.

### 11.1 Why the number can be trusted

Three properties, each established rather than assumed, and together they are what make this a
measurement instead of a statistic:

1. **The probe counts H01 and nothing else, 1:1.** 307 deferrals, 307 failed assertions, and in every
   one of the five batches *every red run took a deferral and every deferral run was red*. All six
   exposures §8.2 names fire. **So the denominator of the coverage fraction is exactly the gate's
   false red** — not a proxy for it.
2. **There is no observe-only bias, and it is provable rather than hoped.** Coverage is decided by a
   single comparison — was the write's completion enqueued on the strand before `finishStream( )`'s
   post — and that post sits at the same point in the probe as it would in the fix. Nothing after it
   can change the order.
3. **The two populations are physically distinct.** For a covered occurrence the write handler ran a
   median 51 µs after the post (p10 33, p90 98 — the read handler's own remaining run) and the
   continuation 4 µs behind it. The uncovered 5.5 % are still off-strand **eight immediate hops,
   150–170 µs, later**. That is a different thing, not a tail of the same thing.

**Load moves the rate hard — 6.7 % to 37 % across windows — and does not visibly move the coverage.**

### 11.2 §7's prediction is now measured rather than argued

§7 required that the barrier cases be verified by running and not by reasoning, because §16.6's
shape died on exactly that kind of argument. **Measured:** a whole `utf_baselib_httpclient7` run
takes exactly four deferrals — the two barrier cases and the seam's two — and the barrier ones
resolve *still in flight* through all eight hops, **every run**. One hop leaves their verdict exactly
as today.

### 11.3 Corrections this measurement makes to the design

- **§9's asio item is closed, in Boost 1.90** — what the dist actually ships, not only the 1.84 the
  review could read. But **§4's sentence is misleading and should be made exact**: it reads as though
  `write_op`'s hook itself defers, while the hook is `start_ == 0 ? true : is_continuation( handler_ )`
  and **`start_` is 1 at the first `async_write_some`**, because `start_write_op` invokes
  `operator( )( ec, 0, 1 )`. A reader checking the claim sees `start_ == 0 ? true` first and concludes
  the opposite of the truth. The conclusion stands; the sentence does not.
- **§8.3 does not need the fix in the driver.** An observe-only deferral is a strictly *better*
  instrument: behaviour is unchanged, so the occurrences counted are the natural ones and no
  before/after build comparison is needed. This weakens how load-bearing §8.2's "≥300 after-runs,
  zero reds" has to be — the maintainer's scoped statistical exception is no longer the thing
  carrying the evidence.
- **§8.5's seam is reachable, with two corrections that are load-bearing for whoever builds it.** The
  policy does **not** gain an `async_write_some( )`; it hides `getStream( )` and returns a wrapper
  stream holding the socket, and **`stream_t`/`stream_ref` must not be redefined**. And
  `onWriteCompleted( )` is **not** interceptable by subclassing the driver — it is non-virtual and
  bound by `cpp::bind`, so the stream is the only lever. *Third correction, 2026-09-23 implementation
  review, and it is the one that decides whether the module can be run whole:* the release must be a
  **rendezvous** — the read's delivery parked until the write's completion has been captured —
  because which of the two reaches the strand first is H01's own race one layer down, and a seam
  that releases from a read which arrived first captures the write with nobody left to release it
  and hangs the module at H01's own rate (§12.3). `h01-coverage`'s seam has this defect; `h01-fix`'s
  does not.
- **§8.2's derived ~2 % per exchange measures 3.7 %**, and the six exposures are unequal within a
  factor of two.

### 11.4 What the measurement does not establish

- **The fix is not built.** §7's four barrier assertions *under the fix*, §5.3's write-outcome hazard
  and A2's interaction remain settled by reading only.
- The 17 uncovered occurrences are counted as uncovered even where one may have been a **legitimate**
  refusal of a genuinely blocked write, which would make 94.5 % conservative. The two could not be
  separated.
- Second-order: the fix changes the *number* of occurrences — a reused connection changes the rest of
  the run — not their coverage.
- Linux a64 clang debug, one host. **TLS was not exercised**, though the `tls-h1-control` lane
  measured the same defect there at ~1 in 8.

**Verdict: build it.**

---

## 12. Implementation review, 2026-09-23 — `h01-fix` @ `749943d`, the third shape

**Reviewer: Claude Fable 5.1, reading the lane worktree at `749943d`** — three commits on
`a1-cleartext` @ `6632469`: `8c967d2` (A1-cleartext's merge conditions, §12.7), `ef26bed` (H01),
`749943d` (manifest). Line numbers below are at `749943d`. Read whole: the H01 diff and, in the
landed file, the class comment, the member block, `onStartRequest( )`, `onWriteCompleted( )`,
`scheduleRead( )`, `onBytesRead( )`, `onPeerClosed( )`, `onReadCompleted( )`, `activeHandle( )`,
`tryGetActiveStream( )`, `finishStream( )`, `publishStreamEnd( )`, `deferStreamEnd( )`,
`onStreamEndDeferred( )`, `closeConnection( )`, `chkArmIdleTimer( )`, `onIdleDeadline( )`,
`initiateClose( )`, `scheduleTask( )`, `onTaskStoppedNothrow( )`, `submit( )`, `cancel( )`,
`freeStreamSlots( )`, `onCancelStream( )`; `TaskBase.h`'s handler macros and `CPP.h`'s
`BL_NOEXCEPT_*`; `MultiOperationTask.h`'s accounting; `TestHttp1DriverStrandSeam.h` whole and the
same file on `h01-coverage`; `TestHttp1DriverWriteBarrier.h`'s exchange and both cases; the reuse
case and every `Ready` read in `TestHttp1ConnectionTask.h`; `Http1DriverTestUtils.h`'s
`plain_stream_t`, `chkOrFail( )`, `waitForClosed( )` and `release( )`; `NetUtils.h`'s five
predicates and the arms design §13; the lane's five run scripts and its journal
(`lane3.md:3767-3950`); and **every log in `logs/lane3-h01/`, re-tabulated by script rather than
read off the journal**. Nothing was built or run.

**Verdict: agree that the implementation should be accepted.** The third shape survives what
refuted the first two, and it survives the strongest claim made for it — that the residual is the
uncovered band and nothing else — which holds run by run in the logs (§12.1). No condition in code;
what is owed is precisions and cheap runs (§12.9). The hang was the seam's and is fixed by
construction (§12.3); lane2's `h01-coverage` seam carries the same defect and is unusable as it
stands. `8c967d2` landed as §13 specified (§12.7).

### 12.1 The numbers, re-derived from the logs

The four rate logs are per-run lines from `lane3-h01-rate.sh`, three counters per run taken from the
two trace lines. Re-tabulated:

| log | runs | red | deferrals | completed | still in flight | red ∧ inflight=0 | green ∧ inflight>0 | red ∧ inflight>1 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `before-hc3-quiet` | 200 | 20 | 0 | 0 | 0 | — | — | — |
| `before-hc3-load` | 200 | 116 | 0 | 0 | 0 | — | — | — |
| `after-hc3-quiet` | 300 | 3 | 46 | 43 | 3 | 0 | 0 | 0 |
| `after-hc3-load` | 300 | 13 | 214 | 201 | 13 | 0 | 0 | 0 |

260 deferrals, 244 behind the write — **93.8 %**; 16 reds, 16 *still in flight*, each red carrying
exactly one and no green run carrying any. **The 1:1 claim holds in the logs**; the table in the
commit message is the log, not a summary of it. The after-load batch ran under a compile: `load.log`'s
last loader session is 16 rebuilds of `utf_baselib_httpclient4`, 22:11:12 to 22:18:24, against a run
from 22:11:27 to 22:18:17. (The loader log is truncated per session, so the two earlier load batches
are covered by the journal only.)

Two precisions, neither touching the conclusion:

- **"Every red is `EQUAL( stateAfterResponse, Ready )`" is not what the logs show for 22 of the 152
  reds** (2 + 14 before, 0 + 6 after): those have an empty assertion capture. The script's pattern
  matches only `in "X": critical …`, and `chkOrFail( )` fails through `UTF_FAIL( )`
  (`Http1DriverTestUtils.h:80-89`), whose line reads `fatal error: in "X": <message>` — no match.
  The one H01 exposure that reds through `chkOrFail( )` is the keep-alive reuse case's second
  `submit( )` (`TestHttp1ConnectionTask.h:219-222`), so those 22 are §8.2's sixth exposure —
  inferred by elimination, since the run outputs were deleted. The named reds fall on three cases
  (`ChunkedTrailersAndBodiless` 59, `ReuseVerdictInputs` 47, `InterimResponsesPrecedeTheFinalBlock`
  24). For the next rate script: match `in "X":` and keep the rest, or the reuse case is invisible.
- **§11's 1.24 % was predicted at an 18.3 % occurrence rate**; the quiet batch occurs at 10 % and
  measures 1.0 %, the load batch at 58 % and measures 4.3 %. What transfers is the coverage
  (93.8 % [90.2, 96.2] against 94.5 % [91.3, 96.5]) and the identity of the residual; the per-run
  residual scales with the occurrence rate, as it must. The journal says this correctly.

`utf_baselib_httpclient7`: `after-hc7-module2.log` and `after-hc7-module-load.log`, 12 + 12 runs,
every one `rc=0 deferrals=5 completed=2 inflight=3`. The first batch (`after-hc7-module.log`, 10
runs, pre-rendezvous seam) is 9 green and one `rc=143` at `completed=1 inflight=4` — the hang.
Object size after the 800-line header: `UtfBaselibHttpClient7Main.o` 33,912,304 octets, 32.3 MiB,
under the 40 MB target.

### 12.2 The driver, read for what could go wrong

**The shape is §5 as corrected.** `isReusable` (`:1474-1475`) is untouched; `isVerdictDeferred =
isConnectionUsable && ! isClosing( ) && m_isWriteInFlight` (`:1510-1511`); the message's own state
retires synchronously — `m_request` / `m_startPending` under the lock (`:1526-1527`), the parser
(`:1530`), the write's storage and record under `! m_isWriteInFlight` as before (`:1546-1552`), the
chunk and flags (`:1554-1557`); and the ending — handle, sink, verdict, `onClosed( )`, close-or-arm
— is one function, `publishStreamEnd( )` (`:1587-1635`), called inline or from the continuation. The
handle stays allocated through the window, so `submit( )` (`:2188`) and `freeStreamSlots( )`
(`:2323`) see the in-flight state — §5.1's property, verified.

**Accounted, in both directions.** `deferStreamEnd( )` is `chkArmIdleTimer( )`'s shape:
`beginOperation( )`, the post inside a `try` whose `catch` completes the operation with the
exception and returns. A post that throws fails the task and `onTaskStoppedNothrow( )` (`:2123`)
delivers `onClosed( )` from the sink still held — the one path the continuation was posted to serve
is served by the safety net instead. The continuation's `break` on the handle guard (`:1733-1736`)
leaves the handler macro's `do { } while( false )` with no exception, so
`BL_TASKS_HANDLER_END_MULTIOP( )` accounts the operation on that path too. `closeConnection( )` from
the continuation is therefore `beginClose( )` plus an epilog that reaches `initiateClose( )`
(`MultiOperationTask.h:371-376`) — what §5.2.1 required and what `taskEndedUnaided` measures, 24 of
24.

**The write's outcome, not the flag** (`:1761-1764`): `isWriteEnded && ! m_writeEndingCode &&
! isClosing( )`. The record is written raw before the write handler's prolog (`:851`), cleared by
`finishStream( )` only under `! m_isWriteInFlight` (`:1551`) — the branch the deferral path never
takes — and cleared by the continuation once consulted (`:1788`). So it is there exactly when the
deferral needs it and cannot leak into the next message on a connection published Ready. The third
seam case is the negative control for this one term: `negctl-writeoutcome.log`, 10 of 10 red at
`:796` with the term removed, the other two cases 0 of 10 either way. **§5.3's second hazard is
real and closed.** `isClosing( )` re-asked is not redundant: a write handler that failed with a
code neither arm excuses has already recorded the task's first error and set `m_closing`.

**§5.3's first hazard is real in the narrow sense and is the majority ordering's own behaviour.**
Before this change a write failing inside the window with an unexcused code was excused by
`isOurOwnTeardown` (`:801`) because the synchronous close got there first — by accident. After it,
the covered path fails the task exactly as the write-first ordering always has; the sink still gets
`onClosed( )` with the response's empty code from the continuation (`isClosing( )` true → Draining →
callback → `closeConnection( )`), and the task completes with the write's error. The two orderings
now agree. On cleartext a write that physically completed before the response arrived carries
success or a peer-close code — A2's arm — so the path is reachable, if at all, under TLS. Recorded;
not a defect.

**The reordering on the non-deferred path is unobservable — verified reader by reader.** Between
the two locked blocks (`:1513-1528`, `:1598-1612`) the handle and sink are still held and `m_state`
unchanged, where before they were already cleared and published. Off-strand readers: `submit( )`
refuses on an allocated handle either way; `cancel( )` (`:2248`) now finds the handle and posts
`onCancelStream( )`, which runs after `publishStreamEnd( )` and returns on its guard (`:2361`) — a
no-op where before it returned at once; `freeStreamSlots( )` reports 0 for the extra span where
before it could report 1 *before* `onClosed( )` was delivered — a narrowing of a pre-existing
micro-window; `state( )` sees the in-flight state a few hundred nanoseconds longer. On-strand
readers cannot interleave. `onTaskStoppedNothrow( )` runs only at a zero pending count, and every
`finishStream( )` caller but `onStartRequest( )` is inside an accounted handler; `onStartRequest( )`'s
two callers pass `isConnectionUsable == false` or run before a write. **Agree: unobservable.**

**The read is re-armed on the deferral path** (`:1377`) — the lane's finding 1, verified and
accepted. On the covered path it is the connection's life-long idle read, as today after Ready; on
the uncovered path it is an extra registered operation the continuation's `initiateClose( )` reaps
with `operation_aborted`, excused as self-inflicted under `m_closingDeliberate`
(`MultiOperationTask.h:355-370`) — `! taskFailed` holds in every barrier run. Inside the window it
reads into a message with no parser: unsolicited data throws as on an idle reused connection, and an
`eof` takes `onPeerClosed( )`'s `! m_parser` return (`:1231`) straight to `closeConnection( )`,
after which the continuation finds `isClosing( )` true and publishes Draining — the verdict today's
path publishes on that peer. Consistent in every branch this reviewer could construct.

**The `BL_LOG` move is real and the new placement is the only right one.** Inside the `try`, a
throw from logging after a successful post would have completed the pending operation in the
`catch`, and the continuation would later have run on a task that could already have taken its
terminal — the double completion `chkArmIdleTimer( )`'s guard exists to prevent. Outside the `try`
(`:1695-1702`) a throw reaches `BL_NOEXCEPT_END( )`, which is `BL_RIP_MSG( )` (`CPP.h:104-108`): the
contract every `NOEXCEPT` member of this class already has, and nothing double-counted. Before the
`beginOperation( )` would have been worse — a throw there leaves the stream unended with a re-armed
read and no continuation, a hang.

### 12.3 The hang, judged — the seam's, fixed by construction; and `h01-coverage`

**The mechanism is confirmed at the source, not accepted from the story.** The pre-rendezvous seam —
which `h01-coverage` still carries, its `onWriteSomeCompleted( )` / `onReadSomeCompleted( )` being
line for line the ones lane3 replaced — arms two flags: hold the next write's completion, release it
from inside the read's. Both wrapper lambdas are handlers on a socket whose executor is the strand
(`plain_stream_t` is `TcpSocketAsyncStrandedBase`, `Http1DriverTestUtils.h:766`; a handler with no
associated executor runs on the I/O object's), so they serialise — and *which runs first* is the
race under test one layer down: the write's completion was posted to the scheduler first and the
read's overtakes it only on §4's stall. Read first: the release consumed its arming with nothing
held; the driver's read handler took the deferral and re-armed the read; the write's completion was
then captured with no release left; the continuation found the write still in flight, published
Draining, and `initiateClose( )` reaped the re-armed read and found nothing of a write whose socket
step had already completed; one accounted operation stayed pending forever and `takeTerminalNoLock( )`
was never true. Every thread parked, the reactor idle — the 37 + 1 of `hang-run4-stacks.txt`. **The
hung run's own counters say the same**: `completed=1 inflight=4` where every healthy run is 2 / 3.

**Reproduced as claimed**: `repro-seam-underload.log`, 40 runs of the two early-release cases, four
`rc=124` (early 1, reset 3), each with one *still in flight* line, no *completed* and no *Leaving
test case*; the other 36 the plain green shape.

**The rendezvous cannot strand a completion — checked on every ordering.** `deliverRead( )` (`:344`)
parks the read's delivery only while both flags are still set, i.e. only while the write's completion
has not yet reached `onWriteSomeCompleted( )` (`:366`); that function's `exchange( )` is what clears
`holdNextWrite`, and in the same call it takes and posts any parked read. Both run on the strand, so
there is no interleaving between the test-and-park and the capture-and-post (the lock is belt and
braces). Write first: held, read delivered directly, released early — the original path. Read first:
parked, posted by the capture; the delivery runs `onReadSomeCompleted( )` (`:393`), which posts the
held completion, and *then* the driver's read handler, whose deferral lands behind it — the window
on demand, on both orderings. The held completion always comes: the peer answers only after reading
the whole request, so the socket write has completed before any response exists; nothing in the
driver cancels or shuts down before its read handler runs, and a completed socket step is outside
both `cancel( )` and `shutdown( )` anyway. The late variant's chain holds an executor and no task;
the held completion holds the driver's handler and so the task, which cannot take its terminal with
the write pending — so the release always runs inside the case, never after it, and `armSeam( )`'s
clearing of `held` / `pendingRead` never discards a live completion.

**The positive control proves what it claims**: `repro-seam-fixed-underload.log`, 75 runs, all
`rc=0`, *SEAM parking the read* in 11 — early 2, reset 3, late 6 — each with the branch its case
predicts and its *Leaving* line. 11 of 75 is 14.7 %, which is the per-exchange occurrence rate under
compile load (58 % per module run over six exposures ≈ 13.5 %): the seam now *measures* H01's
occurrence directly, exchange by exchange, a by-product worth having.

**The driver is not at fault, and the late case is the proof.** In the hung configuration the
driver did everything a driver can — closed, shut the send side down, cancelled — against a
completion the harness held with no release path, which no production scheduler does. The late case
holds a completion for forty hops *with* a release path: the driver publishes today's verdict,
closes, and ends cleanly when the completion is finally delivered — 25 of 25 under load, 6 on the
inverted ordering.

**`h01-coverage`'s seam: treat as unusable.** It hangs at H01's own per-exchange rate on its
early-release case, it is superseded by `TestHttp1DriverStrandSeam.h` on `h01-fix` in every respect,
and nothing from that branch should merge. The branch remains the record of §11's measurement, which
is unaffected — §11's 1370 runs were of `utf_baselib_httpclient3`, where there is no seam; lane2's 12
whole `httpclient7` runs (`lane2.md:3954`) completed by luck at that rate, and §11.2's "every run" is
true of the runs that completed. Any future run of that branch's module must be bounded with
`timeout`, as lane3's scripts now are.

### 12.4 The seam as evidence — one gap in the record, closed by reading

- **Red-before was measured with the pre-rendezvous header.** `before-seam.log` and
  `before-hc7-module.log` fail at `TestHttp1DriverStrandSeam.h(611)`; the committed header's
  assertion is at `:712` (`negctl-writeoutcome.log` cites `:796` for the third case, which is the
  committed file). So no run exists of the *committed* seam against the unfixed driver. By
  construction it is red on both orderings — write first: held, released early, `finishStream( )`
  reached with the flag true, Draining published synchronously; read first: parked, delivered behind
  the capture, released early, the same — and the rendezvous changes nothing on the write-first path
  the old header measured 10 of 10 on. **Owed, cheap, not a condition:** ten runs of the three seam
  cases with `ef26bed`'s header over `8c967d2`'s driver, so the record matches the artefact.
- **The third case's "green against the tree before the deferral too"** (`:776`, and `notes.txt`)
  is by reading, not by a log — the case did not exist when the before-runs were taken (three cases
  first appear in `after-seam.log`, 21:29). The reading is right: the stale flag publishes Draining,
  then the write handler's reset meets `isOurOwnTeardown`. The same owed run settles it.
- The late case asserting `Ready != state` rather than `Draining` is correct: `onTaskStoppedNothrow( )`
  sets `Closed` microseconds after `onClosed( )` on that path, since the task ends as soon as the
  held completion is delivered.

### 12.5 The five items, verified

1. **§13.8, both halves.** *Write-handler-first*: the flag is already clear at `finishStream( )`,
   `isVerdictDeferred` is false, the synchronous verdict publishes Ready and clears the record at
   `:1551` — untouched, as the lane says; the continuation cannot run there. *Read-first*: before,
   Draining from the stale flag; after, without the outcome term, the continuation reads a clear flag
   and publishes Ready — §13.8 regressed onto the H01 ordering; with the term, Draining. **Verified
   at the source, and the negative control of §12.2 is this exact interleaving.** So the change-set
   is neutral on the write-first half and protective on the read-first half: net-positive on a defect
   it does not close. Closing the other half is one term on `:1474-1475` and a red on A2's harness
   with a keep-alive `Content-Length` answer — its own gated change-set, agreed, and **recommended as
   the next one**: the tree now carries two verdict computations that differ by exactly that term, an
   asymmetry `publishStreamEnd( )`'s comment documents and a future "simplification" could resolve
   the wrong way.
2. **The reordering** — §12.2, verified, unobservable.
3. **§7 under the change** — `deferrals=5 completed=2 inflight=3` in 24 of 24 runs. The five are
   the three seam cases and the two barrier cases; the seam cases' branches are pinned per case in
   `after-seam.log` (early: completed; late: still in flight; reset: completed — 10 of 10 each), so
   the arithmetic leaves both barrier cases *still in flight* on every run. §11.2's observe-only
   prediction holds with the fix in. The four assertions and both bounds hold in every run; the
   reads at `:293-295` follow `waitForClosed( )` and the callback follows the verdict inside
   `publishStreamEnd( )`, which is what §7 required.
4. **The `BL_LOG` move** — §12.2, real, correct.
5. **The handle-identity guard is unexercised.** Acceptable at the merge and owed a case: four lines
   whose every premise this reviewer verified — `cancel( )` finds the handle and posts behind the
   continuation; the continuation reads without taking; `publishStreamEnd( )` is the only other
   writer and is on the strand. What the guard *decides*, which the design did not spell out: a
   `cancel( )` landing in the one-hop window now wins over a completed response — the sink gets the
   cancel's code from `onCancelStream( )`'s `finishStream( )`, where before the change the cancel
   found the handle already cleared and the sink had already been told success. The caller asked for
   it, every event before `onClosed( )` was delivered, and the connection closes either way;
   acceptable, and it is what §5.2.1 specified. **The case is cheap on the seam and deterministic:**
   a sink that calls `cancel( handle )` from inside `onHeaders( )` on the early-release exchange
   posts `onCancelStream( )` while the read handler is still running, so it sits on the strand ahead
   of the continuation `finishStream( )` posts moments later, by FIFO — it ends the stream with the
   cancel's code and the continuation takes the guard.

### 12.6 The lane's four findings against the design

1. **Accepted.** The re-armed read is real (§12.2) and harmless, and §7's re-derivation should have
   named the extra operation the barrier cases now carry through `initiateClose( )`. Recorded here
   rather than rewritten into §7.
2. **Accepted.** §5 item 1 lists the request-side clear (`:1314-1315` at `a723cee`) as synchronous
   and item 2's range `:1300-1324` includes it; the implementation resolves it as item 1, which is
   right — nothing reads `m_request` or `m_startPending` in the window, since `submit( )` refuses
   while the handle is allocated.
3. **Accepted, and it cost a batch.** Noted in §8.3 in place. The batch that "looked 5 of 5 red" is
   not among the saved logs, so the Boost.Test 1.90 refusal is the lane's report; the argument is
   real (`UtfArgsParser.h:318`, `:376-378`).
4. **Accepted.** Noted in §11.3 in place as the third, and deciding, correction.

### 12.7 `8c967d2`, checked against the arms design §13

- **§13.3's condition: landed verbatim**, nine lines for nine (`NetUtils.h:501-509`), the hunk
  `-13/+13`, so the file's line count is unchanged as the condition required.
- **§13.4.1: the predicate refuses `eof`** (`:521`, `isPeerClosedErrorCode( ec ) &&
  ! isCleanEndOfStreamErrorCode( ec )`), and the four-line paragraph at `:496-499` says why, in
  four lines.
- **§13.4.2: R1 asserts the transport's own code** (`TestHttp1DriverWritePeerClose.h:919`,
  `net::isPeerClosedErrorCode( result.errorCode )`), with a five-line comment stating the contract.
  That header's lines shift by five below `:908`; nothing measured cites them.
- **§13.7's two words**: the *"with OR WITHOUT"* sentence at `:120-124` now reads *"8 times in 8 WITH
  … and 7 times in 8 without"* — more than two words, and the substance §13.7 asked for.
- **Kept separate: honoured.** `8c967d2` carries nothing of H01; `ef26bed` nothing of these.
- **Measured only together.** The 24 `httpclient7` runs its message cites are H01's 24, on the tree
  with both commits — fine for a predicate narrowing that changes no measured path, a strengthened
  assertion and two comments, and said here so the message is not read as an isolated run.
- **Two neighbours still state the withdrawn premise**, outside this commit's specified scope:
  `isOrderlyPeerCloseErrorCode( )`'s *"the stack has collapsed the clean close into it"* (`:348`,
  which the record lists as owed) and `isCleanEndOfStreamErrorCode( )`'s *"the stack has collapsed a
  clean close into them"* (`:401`, which it does not). The second is the trap §13.3 named — the next
  lane to read `:401` inherits it. Owed; a same-line-count edit each.

### 12.8 Comments checked — touched and untouched

**Born in `ef26bed`, every claim verified**: `finishStream( )`'s H01 block (*"11-18 %"* is §11's range
across windows; *"94.5 %"* is §11's number cited, this lane's own being 93.8 %); `publishStreamEnd( )`'s,
`deferStreamEnd( )`'s and `onStreamEndDeferred( )`'s. One is true but explains a guard the structure
no longer needs: *"THE IDLE TIMER IS ARMED HERE AND NOWHERE ELSE … structural rather than lucky"*
above the continuation's `publishStreamEnd( )` call — §5.2.3's exclusivity is now by construction,
one `publishStreamEnd( )` per ending, and the gate at `:1858` is not what provides it. Harmless. One
sentence in `publishStreamEnd( )`'s comment overstates — *"that difference is the whole of this
change"*: the hop is the change, the difference its consequence.

**Untouched and now narrower than they read** — the class the brief asked for; each owed a clause,
none false enough to block:

- `finishStream( )`'s *AND IT DOES NOT HANG* paragraph, *"the epilog of the very handler that got
  here … The handler that trips the barrier is the one that frees it"*: on the deferral path the read
  handler trips it and the continuation frees it. The mechanism — an accounted handler's epilog
  reaching `initiateClose( )` — is unchanged; the identity is not.
- The class comment's *"THE OPERATIONS IN FLIGHT. At most three: a read … a write … and the idle
  timer"* (`:88-95`): there is a fourth accounted operation, the deferred ending, and it coexists with
  the read and a still-running write in the barrier cases. "Three at once" still holds; the list does
  not.
- `m_writeEndingCode`'s member comment, *"consulted by onPeerClosed( ), released with the rest of
  this message's state in finishStream( )"* (`:233-240`): consulted also by `onStreamEndDeferred( )`,
  the load-bearing consumer §12.2 verified, and released there on the deferred path.
- `m_isWriteInFlight`'s *"what the storage of a write and the reuse of this connection both depend
  on"* (`:218-227`): §13.7 counted three dependants; the deferral decision is a fourth.
- `onTaskStoppedNothrow( )`'s *"never through finishStream( )"* — §13.7's note stands, unchanged.

**In the tests**: `TestHttp1DriverWriteBarrier.h:392-393` (*"the epilog of the handler that got there
reaches initiateClose( )"*) is generic enough to stay true of the continuation; `:429-430`
(*"finishStream( ) clears the handle and publishes Ready"*) is true through `publishStreamEnd( )`.
`notes.txt`'s three H01 recipes are accurate, with §12.4's *"green before"* precision.

### 12.9 Agreement, what is owed, and what this review could not settle

**Agreed; H01 on `h01-fix` @ `749943d` is accepted for merge in the order A2, A1-cleartext (with
`8c967d2`), H01.** No condition in code. Two record edits go with the merge: this design's status
line, §8.3 and §11.3, done here; and the owed-work record's item 6, the orchestrator's. Variant and
toolchain coverage — clang release and gcc release of `utf_baselib_httpclient3` and
`utf_baselib_httpclient7` — is the orchestrator's release runs, per the rule; the lane's clang debug
is the lane's share.

**Owed, recorded so it is not rediscovered:** the red-before run of the committed seam header
(§12.4); the `cancel( )`-in-the-window case (§12.5.5); §13.8's write-first half as its own gated
change-set (§12.5.1); the five clauses of §12.8; `NetUtils.h:348` and `:401` (§12.7); the
rate-script pattern (§12.1); and, from the lane's own list, the one `Logging::trace( )` line in
`onReadCompleted( )`'s hand-over arm that would make A1-cleartext's deferral visible — six lines, no
behaviour, A1's.

**Not settled by reading:** TLS — the change compiles over `TcpSslSocketAsyncStrandedBase` (the
explicit instantiation at `TestHttp1ConnectionTask.h:96` is in every `httpclient3` build) but was
never exercised over it, and the seam is cleartext-only; the `tls-h1-control` lane's ~1 in 8 is the
number to re-take once this merges. Windows, and release in both toolchains — the orchestrator's.
Whether the driver header that produced the 600 after-runs (`after-hc3-build.log`, 22:05) is
byte-identical to the committed one (22:21): the negative control edited and restored one term in
between (`negctl-build.log`, `restore-build.log`, 22:19), `after-seam-final.log` is 8 of 8 × 3 on the
restored tree, and nothing else is journaled — assumed identical, not shown. The Boost.Test 1.90
argument refusal (§12.6.3) — the lane's report, no log. The two before-side load batches' loader
coverage — the journal's, `load.log` being per-session. And the 22 empty captures are the reuse case
by elimination, not by a saved output.

---

## 13. Implementation review, 2026-09-24 — `sync-verdict` @ `6e97b86`, owed item 6a

**Reviewer: Claude Fable 5.1, reading the lane worktree at `6e97b86`** — two commits on `lazari2` @
`50dd9ca`: `edb6d96` (the fix), `6e97b86` (manifest). Line numbers are at `6e97b86`. Read whole: the
diff of all four files; in the landed header the member block, `onStartRequest( )`'s write initiation
and its catch, `onWriteCompleted( )`, `armRead( )` and the tail of `onReadCompleted( )`,
`onPeerClosed( )`, `deriveIsReusable( )`, `finishStream( )`, `publishStreamEnd( )`,
`deferStreamEnd( )`, `onStreamEndDeferred( )`, `closeConnection( )`, `submit( )` and `state( )`;
`NetUtils.h`'s five predicates; `TestHttp1DriverWritePeerClose.h`'s sink, arrangement and the new
case; `notes.txt`; `Http1DriverTestUtils.h`'s `waitForRecords( )`; the source `#include` graph and the
`*.d` union of the main worktree's tree; the lane's two scripts and its journal; the owed-work
record's 6a, the arms design's §12.5, §12.6, §13.6 and §13.8; and **every log in
`logs/lane3-syncverdict/`, re-tabulated from the 60 per-run files rather than off the rate
summaries**. Tier 1 and tier 2 were re-run read-only from the main worktree's venv. Nothing was
built; no test was run.

**Verdict: agree that the implementation should be accepted.** The term is the right one at every
code the write can carry (§13.1), it is asked on the one line where the evidence still exists
(§13.2), it changes exactly one verdict on one path (§13.3), the two computations are now one
question and the textual residue is benign and named (§13.4), the red is deterministic and its
load-bearing half — that it measures the synchronous verdict — is verified in the logs two ways
(§13.5), the second rendezvous is sound and sits on the risk rather than beside it (§13.6), the
narrowing of the gate is accepted because the reach was derived and not read (§13.8). One comment
carries a wrong premise under a right conclusion (§13.9) — owed a same-line-count rewording, not a
condition.

### 13.1 The predicate, checked at every code the write can carry

`m_writeEndingCode` is written raw at `:857` by every completion of the write, ahead of the handler
prolog; `m_isWriteInFlight` clears at `:869`; and the task fails on the write's code only when
neither `isOurOwnTeardown` (`:807`) nor `isPeerClosedOnWrite` (`:843`) excuses it (`:928`). So at a
synchronous verdict with the flag clear, the record is:

- **empty** — a clean write. The term passes. The only code on which a connection may be handed on.
- **`connection_reset`, `broken_pipe`, `eof`, or the policy's truncation** — A2's arm: the task does
  not fail, the flag clears, the record stays. The term refuses, and must: a write that completed on
  any of these handed the stream fewer octets than the request has, so the peer holds a partial
  request and a second one put behind it is read as this one's body — `finishStream( )`'s own mirror
  sentence, with the arrow the write side's way.
- **`operation_aborted`** — our own cancel or `initiateClose( )`'s. `! isClosing( )` already refuses;
  the term is redundant and harmless.
- **anything else** — `CHK_EC( )` fails the task, `m_closing` is set, `! isClosing( )` refuses;
  redundant and harmless.

So the term decides exactly on A2's arm and is inert everywhere else. **The lane's rejection of
`net::isPeerResetOnWriteErrorCode( )` is right, and it is decisive on a reachable shape**, not only
a tidier question: that predicate refuses `broken_pipe` (`NetUtils.h:519-521`), and a peer that
half-closes with our upload unread after answering in full — FIN first, then our send into
`CLOSE_WAIT` — completes the write `EPIPE` while the read completes the message on the octets ahead
of the FIN. Under the predicate that connection would be published Ready for one strand turn and
`submit( )` accepts in one strand turn (`:2359-2360`); under the term it is Draining. The asymmetry
the predicate encodes — a reset is proof, a broken pipe is not — is the right one for
`onPeerClosed( )`'s question and the wrong one for reuse, exactly as the lane says. And the term is
code-agnostic, which is what makes it safe on the platform nobody has measured: the Windows spelling
`NetUtils.h:508` leaves open cannot reach it.

### 13.2 Placement, verified

`isReusable` at `:1617-1619`; the record cleared at `:1695` under `if( ! m_isWriteInFlight )`
(`:1690`); the bool carried into `publishStreamEnd( )` at `:1709`. The only branch on which
`isReusable` can be true is the branch that clears the record, so a term asked after the state block
would read an empty code on precisely the path that needs it. The deferred path is untouched: the
clear is skipped for it, the continuation reads at `:1912` and clears at `:1936`, as §12.2 verified.
At `50dd9ca` the two lines are `:1586` and `:1663` — the 77 of the commit message.

### 13.3 Blast radius — the eight call sites, confirmed

`:629` false (closing at start), **`:699` true** (render failure), `:1169` false (parse error),
**`:1182` `deriveIsReusable( )`** (complete message), `:1304`, `:1323`, `:1332` false (the three
endings of `onPeerClosed( )`), `:2524` false (cancel). Two sites can pass true, as the lane counted.
At `:699` the record is empty by construction, not by luck: `submit( )` accepts only on `Ready` with
no handle (`:2359-2360`), and every publication of `Ready` — `:1757`, reached synchronously under
`! m_isWriteInFlight` or from the continuation under `isWriteEnded` — clears the record in the same
call (`:1695`, `:1936`). The write-initiation catch (`:765`) clears the flag and fails the task, so
it cannot leave a record either. And `isReusable` is not consulted at all when `isVerdictDeferred`
(`:1703-1710`). **The one behavioural change is the complete-message site after A2's arm has run:
Ready becomes Draining.** Nothing else moves.

### 13.4 The two computations, and the trade

Synchronous `:1617-1619`, deferred `:1909-1912`: the same four questions, the deferred one without
`isConnectionUsable`. That term is a conjunct of `isVerdictDeferred` (`:1654-1655`), the only
condition under which `deferStreamEnd( )` is called (`:1703-1705`), whose post is the only path to
`onStreamEndDeferred( )` — true by construction, and a *stable* fact, being a property of the
completed response (the parser's verdict and `m_requestSaidClose`) that nothing in the window can
change. Plumbing it through would add a parameter that is always true to two signatures and a bind
on a universal path — a dead parameter that invites "when is it false?" and answers "never". The
lane recorded the omission in both comments instead. **Judged: the right trade.** The hazard §12.5.1
named — a future simplification resolving the asymmetry the wrong way — was about the *outcome*
term, which is now at both sites. The residue cannot be resolved silently the wrong way: adding
`isConnectionUsable` to the deferred verdict is a no-op, and removing it from the synchronous one
publishes Ready on every failed stream and turns four `Ready != state` assertions red in one file
alone. Benign, and loud if touched.

### 13.5 The evidence, re-derived — and the half that makes it evidence

From the 60 per-run logs, not the two summaries: `unfixed-{1..15}` 15 red, `fixed-{1..15}` 15
green, `negctl-{1..15}` 15 red, `final-{1..15}` 15 green — an A-B-A-B on the same binary path, the
last pair on the committed text: the header's mtime `08:58:36` (the restore after the negative
control) precedes `UtfBaselibHttpClient7Main.o`'s `08:59:00`, and the tree is clean at HEAD, so the
object the `final` and `hc7-final-module` runs used is the committed text. The gap §12.9 recorded
for H01 — "assumed identical, not shown" — does not recur for `httpclient7`. All 30 reds carry one
message, the new case's `chkOrFail( )` at `Http1DriverTestUtils.h:87`, with the same events and
peer records: `headers:200:final|data:8|data:8|data:10|closed:ok` and
`rcvbuf:set|head:…|chunk-two:sent|gate:held|chunk-three:sent|linger:set|reset|state:read` — the
response complete and reported `closed:ok`, the reset after the third chunk, the verdict read after
the reset.

**`deferrals=0`, verified two ways.** A `grep` over all 60 per-run logs for either trace line —
`Deferring the reuse verdict` (`deferStreamEnd( )`, `:1826`) and `The deferred reuse verdict`
(`onStreamEndDeferred( )`, `:1914`; the script counts this one) — finds none. And the instrument
prints at that level: H01's `after-seam.log` and `after-seam-final.log` carry both lines under the
same `-- --bl-logging-level=6`, and every per-run log here shows TRACE output, so the level was in
force. Therefore `isVerdictDeferred` was false in all 60 runs; on the unfixed runs Ready was
published, which requires `! isClosing( )` and so the flag clear — the write handler ran first; on
the fixed runs, with `isConnectionUsable` true (200, `part-onepart-twopart-three`, `closed:ok`
asserted) and the task not failed (asserted), the Draining is the record's. **These runs are
evidence about the synchronous verdict and nothing else, as the lane says.** The whole-module
negative control (`hc7-negctl-module.log`, 14 cases) fails the new case and only it, so no existing
`httpclient7` case depended on the term's absence.

### 13.6 The second rendezvous, judged

**Mechanism, at the source.** `publishStreamEnd( )` sets `m_state` under `m_stateLock` at `:1757`,
releases the lock, then calls `sink -> onClosed( )` at `:1766`. The gated sink (`:416-433`) delegates
to the inner sink first — which satisfies `waitForClosed( )` — and only then blocks in `m_endGate`
on `peer.waitForRecords( 8 )`, bounded by `WAIT_TIMEOUT_IN_MILLISECONDS = 30000`
(`Http1DriverTestUtils.h:72`). The test thread wakes, reads `state( )` (`:775`, `m_stateLock` only,
which the publishing thread no longer holds — no deadlock), records `state:read` (`:779`), and the
gate opens. The hold is on the strand, so the three writers of `m_state` — `:1757`, `:1968`,
`:2298` — cannot run until the sink returns; and the read is re-armed only after `onBytesRead( )`
returns (`:1484-1486`), i.e. after the gate. That last fact is why the gate is needed: on the
unfixed tree the re-armed read meets the reset's residue and ends the connection within one strand
turn, and a `state( )` read racing it can return `Draining` — a green run against the defect, the
one failure mode this case must not have.

**Sound, and it masks nothing**, on three grounds. It reads the verdict at the event the production
reader reads it — `releaseStream( )` runs inside the request task's `onClosed( )` — so the control
is on the risk: it measures the value `submit( )` would consult (`:2359`) at that moment. It delays
only `publishStreamEnd( )`'s epilog, on a strand with nothing else pending — the write handler has
run, the read is not yet re-armed, no timer is live — so there is no concurrent activity whose order
the delay could hide. And its failure mode is a slow run and not a wrong answer: the state is read
*before* the record is written, so the bound can only elapse if the test thread does not run for
30 s between `waitForClosed( )` and `state( )`; and if `onClosed( )` never comes, `waitForClosed( )`
reports it. The two earlier cases are untouched — `m_endGate` is empty unless `attachEndGate( )` is
called (`:690`), their record count stays 5, and the manifest shows their `body_sha` unchanged. The
sink still calls nothing on the connection; the read is from the test thread, so L3 holds.

### 13.7 §13.8's open question — what 15/15 settles

§13.8 had no case; the write-first ordering was "argued from the kernel and R1's measurement". 15 of
15 with a real RST (`SO_LINGER( on, 0 )`, no shutdown), no seam and an unmodified driver settles
**reachability**: the ordering exists with a real transport, and on it the defect fires every time.
The arrangement holds the strand on chunk two for 50 ms after the reset
(`STRAND_HELD_AFTER_RESET_IN_MILLISECONDS`, `:220`) — a slow consumer, which is a legitimate sink and
not an injection — and the source says why the ordering then follows: with the read not re-armed
until `onBytesRead( )` returns, the write op is the only op registered during the hold, the reset
can reach nothing else, and its handler is on the strand before the hold releases. What 15/15 does
not measure is the production *frequency* of the ordering against a fast sink — that needs the RST
and the completing octets in one reactor wake, or a lagging read handler — and it does not need to:
a deterministic red on a reachable ordering is what a one-term fix is judged by. The other ordering
is H01's, closed by the outcome term with its own negative control. **Settled.**

### 13.8 Coverage — the narrowing accepted, and on what condition

**The reach was derived, not read, twice over.** From source: `Http1ConnectionTask.h` is
`#include`d by `ClientSession.h:22` and by four test headers (`Http1DriverTestUtils.h:20`,
`TestHttp1ConnectionTask.h:20`, `TestClientSessionTlsHttp1.h:23`, `TestHttp1DriverTlsCancelClose.h:22`);
the mentions in `HttpClientRequestTask.h:656,964` and `Http2ConnectionTask.h:1891` are comments;
`ClientSession.h` is included only by `httpclient4`, `5` and `6` test headers. That reaches
`utf_baselib_httpclient{3,4,5,6,7}` and nothing else. From the compiler: the `*.d` union of the main
worktree's tree — 46 of 46 test modules and the four apps — names the same five. baselib is
header-only, so a module that does not compile the header links a byte-identical binary, and running
the other 41 measures nothing about this change. The whole-suite gate exists for reach that reading
can miss; here the reach comes from the compiler's own dependency output, and the lane said plainly
what it did not run. **Accepted.** The condition is the obvious one: the argument is as good as the
include graph, and the next `#include` of `ClientSession.h` or the driver header widens the set.
What the rule still owes and this narrowing does not discharge: clang release and gcc debug of the
five modules, the orchestrator's.

**The five, re-tabulated.** `httpclient7` 3 + 5 whole-module runs green, 14 cases, the last five on
the committed text. `httpclient5` 3 + 2 green, 8 cases, rebuilt at HEAD at 09:01
(`hc5-head-driver.log` says three runs; two logs were saved). `httpclient6` 3 green, 3 cases.
`httpclient4` 25 runs, 2 red: run 2 `SinkIsToldCompleteOnceAcrossTheFallbackRetryTests`
`dispatched == 2U` — arms §13.6, red on the *base* binary once in 30 before any of this; run 6
`AgainstTheLibraryHttpServerTests` `connectionsCreated == 2U` — arms §12.6, 3/30 both sides and
6/90 against 7/90 across H04a, the pool's adoption window. Both pre-existing, both at rates
consistent with one in 25. Their objects (08:45, 08:52) and `httpclient3`'s (08:25) predate the
final header text; see the last paragraph.

**`httpclient3`, the H01 instrument:** 3/300 quiet, 4/300 under the lane's own single-module builds
of `httpclient4/5/6` (08:45-08:52, a lighter load than H01's sixteen rebuilds), 7/600 = 1.17 %,
Wilson [0.57 %, 2.39 %] — re-derived. The quiet figure is H01's 3/300 exactly. The reds:
`RequestResponseAndKeepAliveReuse` ×2 (the second `submit( )` refused — §12.1's sixth exposure,
captured empty by the script's pattern exactly as §12.1 predicted; run 62 carries it beside
`ReuseVerdictInputs`, so 7 red runs are 8 failing cases), `ReuseVerdictInputs` ×2,
`ChunkedTrailersAndBodiless` ×3, `InterimResponsesPrecedeTheFinalBlock` ×1 — every one a
`stateAfterResponse == Ready` failing, H01's residual set. **No new signature.** One precision: the
`hc3` runs were taken without `--bl-logging-level=6` (`lane3-sv-hc3.sh`), so no red carries the
`still in flight` line §12.1 used to identify the residual run by run. Here the identity is by
elimination: for the new term to publish Draining the write must end with a non-empty code without
failing the task — A2's arm, a peer that closed during the write — and every red case expected
Ready, so its peer answered keep-alive and held the connection, and its small write ended clean.
Tight, and by reading; a level-6 batch is the cheap way to make it by log.

**Tiers.** Tier 1 re-run here, read-only, at `6e97b86` from the main worktree's venv: **PASS, all
invariants** — the lane's three tier-1 logs are all pre-refresh FAILs (C1, C6 ×2; C9 in the first,
before the recipe), and no post-refresh log exists, so the commit message's "PASSes after this" was
a claim until this run. Tier 2, `utf_objsize.py` read here: `httpclient7` 32.5 MB (34,117,616
octets), marginal 11.5 under the 40 MB target; the family peak `httpclient4` 49.0 MB, untouched.
Tier 3: no baseline for the httpclient family (`tier3-family.log`: 0 baseline modules), so it cannot
speak; intrinsically all five `registered == ran`, 0 skipped, exit 0 — the lane's finding, recorded.

### 13.9 Comments checked — one wrong premise under a right conclusion

**Born in `edb6d96`, verified:** the member comment's three readers (`:1289`, `:1619`, `:1912`) and
two releases (`:1695` after `:1619`; `:1936` after `:1912`); `finishStream( )`'s account of A2's arm
(`:928`); "released below"; "the same four terms"; `publishStreamEnd( )`'s rewritten paragraph. In
the test: "cleared it 77 lines later" (`:1586` → `:1663` at `50dd9ca`); "most of an 8MB upload never
sent" (`BLOCKED_BODY_SIZE` 8 MiB against a 2048-octet receive buffer); `notes.txt`'s "printed none in
any of those 30 runs" (60, counting both pairs).

**The wrong premise.** The `NON-EMPTY RATHER THAN` paragraph — and the commit message and the
journal after it — says *"admitting broken_pipe would be WRONG, because EPIPE proves a FIN came
first"*. It does not. `NetUtils.h`'s fifth predicate, verified at the kernel source by the arms
design's §13.3, says a write's `broken_pipe` is consistent with three histories — a FIN first; the
read having taken the reset itself, after which the exchange leaves the send `EPIPE`; or our own
`shutdown_send` — and "says nothing at all against the read's own code". The conclusion the sentence
supports is right: for `onPeerClosed( )`'s question `broken_pipe` must not be taken as proof of a
reset, because in every one of those histories the read's own code is the truth. The premise stated
is one history of three. A comment, no behaviour; **owed a same-line-count rewording** — the
paragraph is seven lines, and the `BL_LOG` / `BL_THROW` sites below it bake `__LINE__` in, so the
count must hold to keep the measurement on the committed object valid.

**Two minor imprecisions, neither owed more than a word:** "the branch that makes this verdict
reachable" — precisely, the branch that lets it be *true*; the verdict is used, false, on the others.
And "aborted … is a request this peer will not finish reading" — for `operation_aborted` the reason
is ours, not the peer's; the answer is the same. **Untouched and now off by one:** the H01 paragraph
below the predicate, *"THE SAME THREE BITS ABOVE"* (`:1622-1623`) — there are four; the three H01 is
about are still there and the sentence stays true of them. Add to §12.8's list.

### 13.10 Agreement, what is owed, and what this review could not settle

**Agreed; `sync-verdict` @ `6e97b86` is accepted for merge onto `lazari2`.** No condition in code.
The base has moved to `6b4fa34`; the journal names `inventory.json` as the file likeliest to
conflict. Record edits at the merge, the orchestrator's: the owed-work record's item 6a, which still
reads "unscheduled" and becomes FIXED against this section; this design's status line; and
§12.5.1's "recommended as the next one", now done.

**Owed, none a condition:** the `EPIPE` rewording (§13.9), same line count; a level-6 batch of
`httpclient3` so the residual's identity is by log (§13.8); the "three bits" word (§13.9); clang
release and gcc debug of the five modules, per the rule; and the tier 3 baseline for the httpclient
family, a whole-tree run and not a lane's, without which no differential assertion-count evidence
exists for any module the change reaches.

**Not settled by reading.** TLS: the term compiles over the TLS policy (`httpclient5` green at HEAD)
but the case is cleartext; a write into a reset TLS connection surfaces the transport's code or the
policy's truncation, and the term refuses either, so the answer is the same on every spelling —
argued, not measured. Windows: the matrix's, with the point in §13.1's last sentence in the term's
favour. Whether the header text the `httpclient3`, `4` and `6` binaries were built from (08:25,
08:45, 08:52) is byte-identical to the committed one (08:58:36): the journal says the first rate pair
predates "the comment correction", so at least comments moved between 08:23 and 08:58; the term was
in place from 08:23 (`build-fixed-hc7.log`, `fixed-{1..15}` green), and comment-only edits shift
`__LINE__` literals and nothing else — assumed, as §12.9 assumed for H01; `httpclient7` and
`httpclient5` are on the committed text and are not assumed. The production frequency of the
write-first ordering (§13.7). And the third `hc5-head` run, whose log was not saved.
