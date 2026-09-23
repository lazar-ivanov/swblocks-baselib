# The pool re-establishes without bound for an entry that retires before it was ever usable

**Status:** record, 2026-09-23. Found when lane 3's H04a implementation crashed
`utf_baselib_h2client4` (`s6r3-design.md` §4.1); verified at the source on `lazari2` @ `9583c31`,
every function below read from its signature to its end. Nothing implemented; the bound is
specified, not written. Its change-set is sequenced **before** H04a's re-gate (`s6r3-design.md` §6)
because it is what turns a wrong gate on the driver poll into a failed request instead of a stack
overflow.

## The hazard

`examineKey( )` charges the waiters of a key — `++attempts`, the counter `maxRetriesPerRequest`
bounds (`ConnectionPool.h:1626-1651`, `:1669`) — only when `refreshEntry( )` returned true for some
entry in that pass (`:1592-1595`). `refreshEntry( )` returns true on two routes: the establishment
bound expired (`:1363-1399`) and the attempt's task `Completed` with nothing to show (`:1401-1434`).
Its third retiring route — the connection reads `Draining` or `Closed` (`:1342-1350`) — returns
false, by design: *"a clean close is not a failure"* (the class comment, `:631-633`), which is right
for a connection that carried requests and was closed by its peer or its idle timer.

It is wrong for a connection that never became usable. Such an entry is retired uncharged; holding
no slot, it is forgotten and erased in the same pass (`:1597-1609`, with `m_totalConnections`
decremented at `:1603`, so `maxTotalConnections` never counts it); `hasLiveConnection` is false and
nothing was charged; `findDispatchable( )` finds nothing; `canStartConnection( )` (`:1507-1525`)
counts live entries only and says yes; a placeholder is inserted and `actions.starts` gets it
(`:1710-1712`).

**Synchronously, that is a recursion.** `startConnection( )` fills the new entry under the lock and
calls `examineAll( )` (`:1923-1938`), then `runActions( )` (`:1942`), which calls
`startConnection( )` for every start the examine produced (`:1860`). If the new entry *also* reads
`Draining` or `Closed` at that first examine, the chain is `startConnection( ) → runActions( ) →
startConnection( ) → …` on the thread that called `acquire( )`, `releaseStream( )` or the maintenance
tick, with nothing to end it: attempts are never charged and the connection total nets to zero per
level. The stack overflows and the process dies with SIGSEGV. That is the backtrace lane 3 recorded.

**Asynchronously, it is an unbounded reconnect loop, and that half is live today.** An h2 origin
that answers every connection with an immediate GOAWAY — a server at its connection limit — is
`Ready` for one round trip and `Draining` before the pool's next examine, so `isReady`, the pool's
own record of having seen `Ready` and written only at `:1333`, is never set; the entry retires
uncharged and the pool opens the next connection. A queued waiter continues until its own deadline —
thirty minutes by default (`:182`) — at one connection per round trip. (The one request that rides
the preface is bounded by the session's own attempt counter when it bounces; the requests queued
behind it are bounded by nothing.) The retry budget of design 4.6 does not apply, because nothing
spends it.

## Why it is latent as a crash, and what removes the latency

No real attempt is born `Draining` or `Closed`: the h2 task is `Connecting` from construction
(`Http2ConnectionTask.h:431`) and the h1 driver `Ready` (`Http1ConnectionTask.h:238-239`). So the
synchronous form needs a connection whose first reading, in the very examine that creates its entry,
is already terminal — which today only a stub produces (`TestConnectionPool.h:801`), and which any
gate on the driver poll that opens later than the task's own `Closed` produces for the real ALPN
fallback (`s6r3-design.md` §4.1). The second is what makes this worth a bound rather than a note: the
pool's response to a wrong reading of its own is a crash, not a failed request, and the wrong reading
was one agreed correction away.

## The bound

In `refreshEntry( )`'s `Draining`/`Closed` arm (`:1342-1350`), an entry that retires **without ever
having been usable** is a failed attempt and is charged as one:

    else if( ConnectionState::Draining == state || ConnectionState::Closed == state )
    {
        if( ! entry -> isRetired )
        {
            entry -> isRetired = true;

            ++m_stats.connectionsRetired.lvalue();

            if( ! entry -> isReady && ! entry -> isPeerLimitKnown )
            {
                hasFailed = true;

                const auto eptr = entry -> attempt.task ?
                    entry -> attempt.task -> exception() : std::exception_ptr();

                failure = eptr ? eptr : makeException< UnexpectedException >( /* as :1424-1433 */ );
            }
        }
    }

**"Ever usable" has two witnesses, and both are needed.** `isReady` is the pool having *seen* `Ready`
(`:1333`). `isPeerLimitKnown` is set by `markPeerLimitKnown( )` (`:1089-1098`) from exactly two
places: a reading of `freeStreamSlots( )` no silent driver could publish (`learnPeerLimit( )`,
`:1147`), which presupposes `Ready` was seen, and a request that **completed** on the connection
(`releaseStream( )`'s `Completed` arm, `:2384`). The second is the one that matters here. A
connection that served its preface rider and went `Draining` inside one tick was usable — it answered
a request — and must not be charged for the requests still queued behind it, or an origin that closes
after one request would fail a queue after three connections instead of serving it one request per
connection. With the second witness that origin is served exactly as today. What is charged is a
connection the pool never saw `Ready` **and** which completed nothing: the immediate GOAWAY, the
born-dead stub, and the fallback under a wrong gate.

`exception( )` takes the task lock (`TaskBase.h:1135`), which `notifyReadyImpl( )` holds from before
`Closed` is published (`:604` → `Http2ConnectionTask.h:2700`) to after the exception is stored
(`setExceptionInternal( )`, a few lines above the `:726` store), so a pool that reads `Closed`
inside that window blocks for its
remainder and then reads the task's own exception rather than the generic one — the diagnostics of
the `Completed` arm, on the arm that used to lose them.

**What it does not change.** A connection seen `Ready`, or one that completed a request, still
retires uncharged on `Draining`/`Closed` — the class comment's rule is kept for the connections it
was written for. The establishment-bound and `Completed` arms are untouched, and the `Completed` arm
cannot be reached for an entry this arm charged: it is retired, and forgotten as soon as it holds no
slot.

**Under the bound the recursion terminates at `maxRetriesPerRequest + 1` levels** — charged once per
level, the waiters fail at the `:1669` test with the last error, no placeholder is inserted, and the
frames return. That is a failed request, which is what H04a's agreed gate would have produced instead
of a crash, and what `H2Pool_FallbackDriverIsPreferredTests` would then have reported as a red
assertion (`records[ 0 ].exception` non-null, `TestConnectionPool.h:1543`). The asynchronous loop
terminates the same way, three connections in.

## Test

`H2Pool_FailedEstablishmentRetriesQueuedRequestsTests` (`TestConnectionPool.h:1309-1360`) has the
shape: two unreplayable waiters, `maxRetriesPerRequest = 2`, an attempt that fails at once, three
factory calls, both waiters failed. The new case is the same with the failure moved from the task to
the connection: `initialState = Closed`, no `failWith`, no fallback. Assert three factory calls, two
failed answers, `failures == 2`, `establishmentRetries == 3`, `dispatched == 0`. **Red before: the
process dies of SIGSEGV entering the case** — the loudest red a case can show, and the reason the
case goes in with the bound rather than with H04a. A sibling with `initialState = Draining` pins the
other state of the same arm. The control that pins the second witness: `initialState = Ready`, one
replayable request dispatched and given back with `RequestOutcome::Completed`, then
`setState( Draining )`, then a second request — served by a second connection, with
`establishmentRetries == 0`.

Subject to the size rule in `src/utests/AGENTS.md`: `utf_baselib_h2client4`, or a numbered sibling if
it is near the target.

### Corrected by the implementing lane, 2026-09-23 — two of the three cases do not work as written

The bound is unchanged; what follows is the test section only, and both corrections were settled by
a run rather than argued. The paragraph above is kept as written, because the reason it was wrong is
the instructive part.

1. **The counts assume two waiters queued behind one placeholder, and this shape cannot produce
   that.** `acquire( )` calls `examineAll( )` under the lock and `runActions( )` after it, both
   synchronously, and an entry born terminal is retired inside `startConnection( )`'s own examine —
   so the whole bounded chain runs to its end, and the waiter is answered, **before the first
   `acquire( )` returns**. A second `acquire( )` therefore starts its own chain: two waiters give
   six factory calls and `establishmentRetries == 6`, not three. The sibling this section borrowed
   the shape from gets three because its failure is asynchronous —
   `StubControl::onScheduled( )` POSTS the failure
   (`TestConnectionPool.h:155`), which leaves the placeholder standing long enough for the second
   waiter to queue behind it.

   The case landed with **one** unreplayable waiter, where the count of factory calls is the bound
   itself: `maxRetriesPerRequest + 1` levels, `failures == 1`, `establishmentRetries == 3`,
   `dispatched == 0`. Both states of the arm are driven in one case, `Closed` and `Draining`.

2. **The control as written pins nothing.** With `initialState = Ready` the pool records `isReady`
   at the first examine, so the FIRST witness already spares the retire and the case is green with
   or without the second. To reach the retire with `isReady` false and `isPeerLimitKnown` true the
   connection must stay `Connecting` throughout and the request must ride the preface — which is
   also the only shape in which the two witnesses disagree in production. The control landed that
   way, and was shown red against a gate carrying `! isReady` alone: the second request to a
   one-request-per-connection origin is answered with an exception. That is the second witness
   earned rather than asserted.

3. **A third case landed that this section did not ask for**, because the section's only case needs
   a stub to reach the arm and the live half does not: a connection which is `Connecting` when the
   pool looks and `Draining` when it looks next has never been seen `Ready`, which is the
   GOAWAY-at-birth origin exactly. Its red is an assertion rather than a crash — unfixed, the waiter
   is never answered at all.

The SIGSEGV red is as this section predicted, recorded with a `gdb` backtrace of
`startConnection( ) :1942 -> runActions( ) :1860` repeating to the guard page.

## Its own change-set, before H04a's

A core-path change to the pool's retry accounting; it gates on the whole suite. It is sequenced
before the H04a re-gate so that the gate lands on a pool where a wrong reading fails a request
instead of the process — the property this record exists to buy — and because the live half, the
reconnect loop, does not wait on H04a at all.
