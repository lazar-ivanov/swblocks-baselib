# HTTP/2 client, Layer L5: review record

**Reviewed:** 2026-09-19, read-only, at tip `7a6f6ba`. **Status:** RECORD. Nothing was built and
nothing was run: the two new production headers, every production hunk in the range, the engine,
driver, base-task and `TaskBase` code they lean on, the specifications and the three new test
files were read; no external reference was needed. The most important result is about the seam
the orchestrator reconciled at merge: **the reconciliation states the opposite of what the pool
does** (finding 1), and the consequence - a stream slot leaked, and a connection kept alive and
never forgotten, on every refused `submit()` - is a composition defect no lane's module could have
seen. Two further defects belong to S5.1 (finding 2), and three of the pool's stated mechanisms are
not what the code does (findings 3, 4, 5). Nothing found blocks S6.1 from starting once findings 1
and 2 are scheduled, but S6.1 is the first consumer of every one of them. Notes for later work
orders are in section 9.

**The range.** `cc7db3b..7a6f6ba`, nine commits: S5.1 as `c08ce0d` (the contract change-set) and
`0c27dc3` (the request task) merged by `ab05659`; S5.2 as `1019ef1` (the reserve wiring),
`eef5faa` (the pool) and `3275ecc` (a TSan fix in the stub) merged by `cda1c36`; the
reconciliation `60a38b2`, which touches only the plan; the manifest refresh `7a6f6ba`, which
touches only `inventory.json`. Both lanes branched from `cc7db3b`, the L4 fix round's tip.

**What was reviewed against.** Design 4.3, 4.5, 4.6, 5.2, 5.3, 5.4, 5.7 at tip - that is, after
S5.1's amendments to 4.5 and 5.3 and S5.2's to 5.4 and 5.7, whose diff over the range was read
whole (96 insertions, three deletions, those four sections and nothing else); plan section 7, the
two work orders, their "as landed" records and the seam paragraph, whose diff was likewise read
whole; `body-source-readiness-deferral.md` in full; the L4 record's section 8 notes for S5.1 and
S5.2 and its second pass's unresolved ledger item. RFC 9113 sections 5.1.2, 6.5.2 and 6.9 from
the text held in the L3 pass, not re-fetched.

## Verdict per slice

| Slice | Verdict | Basis |
|---|---|---|
| `c08ce0d` - the contract change-set | **Conforms; the argument for the event over an amendment holds; the driver's half of one-ask-one-answer is enforced by construction and pinned by nothing; the empty answer is unpinned.** | Option 2 tested in finding 6: with `onData` response-side, `consumed()` the other way and `freeStreamSlots()` connection-level, a bounded hand-over had no pacing signal, so the only *paced* upload possible without a contract change was none - the lane's conclusion is right and its "not an implementation" is one word too strong (a first chunk and a stall is an implementation, a useless one). Additive holds for the diff (50/0) and not for the ABI, which is the point of calling it a change-set: a pure virtual on a frozen interface grows every implementer, and the three named are all test code (`RecordingSinkT`, `StubStreamEventSinkT`, the h1 suite's `RecordingSink` - grepped, no fourth). The driver's `isBodyWantedOutstanding` (`Http2ConnectionTask.h:326`) is set before the call (`:922`) and cleared by any `applyProvideBody` (`:760`), and `raiseBodyWanted` refuses while it is set (`:897-902`); no case holds a pull unanswered and asserts no second one arrives. `applyProvideBody`'s empty-answer return (`:779-796`) and the re-raise it defers to `pumpAllBodies` have no case. |
| `0c27dc3` - S5.1, the request task | **Conforms in shape - one deque, one drain, the flag cleared at the end, three phases, both body modes, the three timers - with two defects: two user-reachable calls run in the apply phase under the task lock, where a throw is a process abort (finding 2), and the two paths which hand a connection back without a stream release nothing (finding 1).** | The mailbox argument (`HttpClientRequestTask.h:132-139`) is right and the driver's is the right counter-example; `post()` reads the pool under the mailbox lock so `requestCancel()` before `scheduleTask()` queues rather than drops (`:348-365`); `scheduleTask` is a post (`:1369-1391`), pinned by the thread-id assertion. Backpressure credits the sink's return value (`:791-830`), pinned. A body over the cap resets and does not credit (`:745-771`), pinned. `completeResponse` reads one `negotiated()` (`:1072-1075`), pinned. Nine cases, each with a recipe. |
| `1019ef1` - the reserve wiring | **Additive holds; the wiring is real and pinned; the wiring is also the only part of the mechanism which exists (finding 3).** | `Session.h` 18/0: a field defaulting to zero and one constructor line (`:534`), and `setDrainingReserve( 0 )` is what the registry already holds (`StreamStateMachine.h:881`, a `ScalarTypeIniter`), so no existing session changes. `H2Session_DrainingReserveFromLimitsTests` pins the path and the refusal. |
| `eef5faa`, `3275ecc` - S5.2, the pool | **Conforms to 5.4 in the placeholder, the FIFO, the replayable rider, GOAWAY draining, the fallback preference and disposal; the leaf-lock shape is kept everywhere a callback or a queue is involved; the count which limits dispatch is the pool's own, as the design says and the plan denies (finding 1); the two numbers are chosen, pinned as constants, and argued wrongly (findings 3 and 4); the peer's limit is latched at a moment which precedes the peer's `SETTINGS` (finding 5); the tick is sound and terminates, with one omission (finding 7).** | Every entry point is take-lock, decide into `Actions`, drop, act (`acquire` `:1784-1834`, `releaseStream` `:1836-1892`, `onMaintenance` `:1571-1634`, `startConnection` `:1438-1513`); the placeholder is in the map before the lock drops (`:1113-1133`, `:1288-1296`); exactly one rider and only if replayable (`:1057-1080`); FIFO by a `break` on the first unservable waiter (`:1298-1304`). The TSan fix is a real race and a real fix, and running it beside `utf_baselib_basictask`'s known race is the control the L4 record asked for. |
| `60a38b2` - the seam reconciliation | **Does not conform: its central factual claim is false of the code it reconciles, of the S5.2 commit message, of design 5.4 as S5.2 amended it, and of `H2Pool_SlotLimitingTests`.** | Finding 1. |
| `7a6f6ba` - the manifest | **As claimed: 22 cases added, none removed.** | The diff's `"name"` lines: 22 additions (nine `HttpClientRequestTask_`, eleven `H2Pool_`, one `H2Session_`, one `H2Driver_`), zero deletions; the two other additions are `utest` namespace entries. 22 recipes across the four `notes.txt` files, name for name. |

The placement and idiom checks of the plan's verification protocol pass: no `BL_DEVENV_VERSION`
test in either new header (grepped); `httpclient/ConnectionPool.h` is kept out of
`httpclient/PreCompiled.h` with a reason written in (`PreCompiled.h:42-50`, size rather than
dependency, which is a fair reason); `utf_baselib_h2client4` carries its `devenv7_only` marker and
the append-convention note; no new statics, so no `BL_DEFINE_STATIC_MEMBER` owed. One nit against
design 2.2: `http2/Session.h:120-121` now names `httpclient::ConnectionPoolPolicy` in a comment,
which is not a dependency but is the engine's header pointing up at a type it must not know; the
sentence should name the pool and not the type.

## Findings

By severity: 1 (High, composition), 2 (Medium), 3 (Medium), 4 (Medium), 5 (Medium), 6 (Low,
evidence), 7 (Low), then the nits in 8 and the notes in 9.

### 1. High - the seam: the pool does hold a per-connection outstanding counter, S5.1 never gives it back on the two paths the reconciliation names, and the record says the opposite of the code

**What the pool does.** `Entry::slotsInUse` (`ConnectionPool.h:559-566`) is "slots the pool has
handed out and not yet had back"; it is incremented at every dispatch (`:1278`) and decremented in
exactly one place, `releaseStream` (`:1856-1859`; every touch point grepped: `:566`, `:909`,
`:1050`, `:1057`, `:1175`, `:1278`, `:1856-1858`, `:1942`). `findDispatchable` dispatches only
while `slotsInUse < capacityOf( entry )` (`:1049-1052`) and reads `freeStreamSlots()` "for its
zero" - the header says so at `:1042-1047`, the S5.2 commit message says so, design 5.4 now says
so at `:903-906` ("the count which limits dispatch is the **pool's own**, not
`freeStreamSlots()`"), and `H2Pool_SlotLimitingTests` (`TestConnectionPool.h:1044-1114`) pins it
with a stub whose `freeStreamSlots()` "says two for ever" while the pool holds the third request
back. A retired entry is forgotten only when `slotsInUse` reaches zero (`:1175-1187`).

**What the reconciliation says.** Plan `:1359-1366` and `60a38b2`: *"That leaks nothing, because
the pool holds no per-connection outstanding counter: it reads slot availability from the driver's
own `freeStreamSlots()`, and a refused submit never opened a stream, so that count is already
right."* Every clause of that sentence after "because" is false of the tree it was merged into.

**What S5.1 does on the two paths.** On a refused `submit()` (`HttpClientRequestTask.h:628-663`)
the task sets `ConnectionUnusable`, fails, and calls `releaseConnection` (`:660`), which drops the
reference and nothing else; the comment at `:636-640` says "what the pool has to undo is its own
acquire bookkeeping, which is the pool's and is reached through the answer it gets from
`outcome()`" - but the pool never reads `outcome()` except as the third argument of
`releaseStream`, which is not called. On the completion-pending path (`:582-593` - the total timer
fired while the request waited in the pool, and the pool then answered with a connection) the code
calls `releaseConnectionSlot`, whose comment says "the slot is handed straight back rather than
leaked", and `releaseConnectionSlot` returns without calling the pool when `m_handle` is
`INVALID_STREAM_HANDLE` (`:1268-1271`) - which on that path it always is. So both paths leak, and
the second leaks against its own stated intent.

**Consequences, in the composition nobody has run.** Per refused submit: `slotsInUse` stays one
higher for the life of the entry; the entry is never retired (retirement on `ConnectionUnusable`
lives inside `releaseStream`, `:1863-1871`) and, if retired by observation of `Closed`, never
forgotten (`:1175`), so `m_byConnection` holds the entry and the entry holds the connection task
alive for the life of the pool; and with `maxConnectionsPerKey` of one for h2, every refusal is a
permanent unit of capacity lost on the only connection of that key. Two refusals are ordinary:
the h2 driver's `submit()` returns `INVALID_STREAM_HANDLE` once `closeSubmissions()` has run
(`Http2ConnectionTask.h:2442-2445`), i.e. for any dispatch which raced the connection's end; the
h1 driver's returns it for **every request carrying a `BodySource`** and for a second request
while one is in flight (`Http1ConnectionTask.h:1297-1314`). The BodySource case is worse than a
leak: the request task calls the connection unusable when the request was unsuitable, so if it
*did* release, a healthy HTTP/1.1 connection would be retired for a request no HTTP/1.1 connection
can ever take. The timer race is narrow but real: the task's total timer is armed in `applyStart`
before the pool's waiter deadline is computed in `acquire` (`ConnectionPool.h:1803-1809`), so it
fires first, and a tick between the two which finds a slot free dispatches a connection to a
request which has already failed.

**The fix is in S5.1 and in the plan, not the pool.** The pool's contract comment says
`releaseStream` "gives the stream slot back" (`ClientConnection.h:528-530`), and the pool's
accounting ignores the handle (it is logged, `:1873-1883`, and nothing else) - so the reconciliation
that is true is: *every answered `acquire` is paired with exactly one `releaseStream`, stream or no
stream.* In `applyAcquired`, both paths call `pool -> releaseStream( connection,
INVALID_STREAM_HANDLE, outcome )` directly, deferred; `releaseConnectionSlot`'s handle guard
goes, or becomes a guard on the connection alone. The outcome on the refused path should be
`Failed` for a refusal the request caused (the h1 `BodySource` case, which S6.1 should not even
dispatch - section 9) and `ConnectionUnusable` only for a refusal the connection caused; the
request task cannot tell them apart today, which is L4 nit 7(e) grown up: the contract has one
refusal value for two reasons. Plan `:1359-1366` should be rewritten to say what the pool counts
and what the pairing rule is; design 5.4 `:903-906` is already right. One case each in
`utf_baselib_httpclient`: a refused submit followed by an assertion on `ProbePool::releases()`,
and a total timeout followed by a late pool answer with the same assertion - the second is
`HttpClientRequestTask_TotalTimeoutCoversThePoolWaitTests` with the probe pool answering late
instead of never.

### 2. Medium - S5.1: two calls the class comment puts in the deferred phase run in the apply phase, and a throw from either is a process abort; a throw from the deferred phase fails the request without resetting the stream

`HttpClientRequestTask.h:141-148` says *"nothing is called out while the task lock is held ...
every call which leaves - `submit()`, `consumed()`, `provideBody()`, `cancel()`, `releaseStream()`,
the caller's `BodySink` and `BodySource`, and `notifyReady()` itself - is in the middle phase"*,
and `:436-443` says the caller's source and sink are "guarded on their own" so that "a caller's
sink throwing must not escape into the NOEXCEPT drain". Two of the named calls are not in the
middle phase: `m_connection -> submit( ... )` at `:623-626` and `source -> read( *block )` at
`:854` both run inside `applyEvent`, which runs under `BL_MUTEX_GUARD( base_type::m_lock )` at
`:427`. Nothing in the apply phase catches; `onDrain` wraps `applyEvents` in
`BL_NOEXCEPT_BEGIN/END` (`:383-416`), and `BL_NOEXCEPT_END` is `catch( std::exception& ) {
BL_RIP_MSG( ... ) }` (`CPP.h:104-109`), which ends in `bl::os::fastAbort()` (`:88`).

So a `BodySource` whose `read()` throws - a file-backed source whose read fails, which is the
"always ready" source the deferral record says every present source is - takes the process down.
The lock half is the smaller problem: the read runs on the drain's own thread, so it serializes
only against readers of this task's `getState()`/`exception()`, and a source which blocks in
`read()` (synchronous disk I/O) blocks those; no deadlock unless the source reaches for the task.
`submit()` under the lock is the same shape for the h2 driver's `ArgumentException` and for
anything `toSessionRequest` can throw; it is also a lock-order edge the header does not record
(request-task lock, then the driver's mailbox or the h1 driver's state lock) - harmless today
since no driver reaches a request task's lock, and worth writing down for the same reason the pool
comment in finding 7 is.

**The deferred half.** The deferred loop does guard (`:447-460`), and a throw there reaches
`failWith( deferredException, false )` at `:470-473` - which cancels the timers, fills the response
and marks completion, and does **not** call `cancelStream`. Compare `applyStopped` (`:1011-1052`),
which cancels the stream and then fails. So a `BodySink::onData` which throws, or a
`connection -> provideBody` which throws, completes the caller's task with the stream still open on
the driver, no `RST_STREAM` sent, no timer left running, and `m_connection` still held: an upload
the peer is waiting on, or a download whose window is never credited again, stays open until the
*peer* gives up. Bounded by the peer's patience, not ours.

**Fix.** Move `source -> read()` into the deferred lambda at `:882-887` (the block, the handle and
the connection are already captured; the read produces the payload there and the `provideBody`
follows it, which also keeps one read per pull), and move `submit()` into a deferred action whose
result is posted back as an event - or, cheaper, keep it in phase one and wrap it, since a throwing
`submit()` is a failed request and not a dead process. In `applyEvents`, `cancelStream( deferred2 )`
before `failWith` on the deferred-exception path, with the second deferred list run after the
lock. Rewrite `:141-148` and `:436-443` to describe what runs where. One case with a source whose
`read()` throws, asserting the task fails and the trace shows `cancel:42`.

### 3. Medium - the draining reserve: the number is reachable, the mechanism it is argued from is not, and no code carries the number to the session

**What the wiring does.** `SessionLimits::drainingReserve` (`Session.h:127`) reaches
`StreamRegistry::setDrainingReserve` (`:534`); `isDraining()` is `explicit || remaining <=
reserve` (`StreamStateMachine.h:1104-1107`); `canOpenLocalStream()` is `! isDraining() &&
remaining != 0` (`:1124-1127`); `Session::submitRequest` refuses on it (`Session.h:920-926`); and
the driver's `canOpenStream()` consults `m_session -> isDraining()` (`Http2ConnectionTask.h:558-566`),
so `applySubmit` answers with `failSubmission( connection_aborted, retryable )` (`:570-583`).
That is the whole of what a non-zero reserve changes: the point at which submissions start
bouncing moves 1024 streams earlier.

**What the argument needs, and what the driver does.** `ConnectionPoolPolicy.h:130-140`: *"nothing
publishes Draining on identifier exhaustion - only a GOAWAY, a connection error and the close
paths do ... The reserve is what makes the pool see it coming, by turning identifier exhaustion
into the Draining state the pool already knows how to retire a connection for."* The first
sentence is true (`publishState( Draining )` is called from `onGoAwayReceived` `:1348`,
`onConnectionErrorEvent` `:1380`, `onPeerClosed` `:1487` and `closeGracefully` `:2089`, and nowhere
from a registry that has begun draining); the second does not follow from it. Nothing turns
`isDraining()` into `ConnectionState::Draining`: `publishFreeStreamSlots` is `limit -
m_streams.size()` while the state is `Ready` and consults neither `isDraining()` nor
`canOpenStream()` (`:2219-2240`), so a connection past the reserve still reads `Ready` with slots
free, exactly as the comment says an exhausted one does. The pool then dispatches, every submit
bounces `connection_aborted` retryable, the request task closes with outcome `Failed` (its
`applyClosed` derives the outcome from the error code alone, `HttpClientRequestTask.h:907`) and
releases the slot, the pool does not retire the entry, and the next waiter goes to the same
connection - the "stream of retryable bounces" the reserve was chosen to prevent, and with the
dispatched-half retry not existing (finding 5) every one of those requests simply fails. The
connection ends when its idle timer fires, which needs `m_streams.empty()` and an idle timeout
which is off by default (`Http2ConnectionConfig`, `:130`) and which no code sets from the pool.

**The third gap.** `ConnectionPoolPolicy::drainingReserve` is read by nothing: `drainingReserve`
appears in `Session.h`, `StreamStateMachine.h` and `ConnectionPool.h` only (grepped), and no
factory maps the policy to an `Http2ConnectionConfig`. "The pool configures the driver" is S6.1's
to write, which is fine; the plan's "as landed" (`:1381-1391`) should say that the number travels
no further than the policy struct today.

**On the number.** 1024 is sufficient: what must still have identifiers when `isDraining()` first
fires is what the pool has committed but the strand has not opened, which is at most
`capacityOf( entry )` and so at most 256, and a bounce consumes no identifier, so nothing accrues
after the trip. "Three further generations of slot reuse" (`:123-124`) describes no mechanism -
there is no path by which more than one capacity's worth is ever committed-but-unopened - and is
harmless decoration. The cost side ("under one millionth") is right. Practically the whole
finding is unreachable at 1.07 billion streams per connection; it matters because design 5.4
`:913-921` now describes a retirement the driver does not perform.

**Fix, in the driver, and by the house rule its own small change-set gated on `h2client2`:** after
`submitRequest` succeeds in `applySubmit`, `if( m_session -> isDraining() ) publishState(
ConnectionState::Draining )`, and let `onStreamClosedEvent`'s existing `m_streams.empty() &&
Draining` branch (`:1336-1342`) take the graceful close - which is what the argument at
`ConnectionPool.h:137-140` already assumes exists. `publishFreeStreamSlots` should also store zero
when `! canOpenStream()`, which makes the pool's zero read true one tick earlier. One case in
`h2core` or `h2client2` with a reserve of "everything but two", two submits, and an assertion that
`state()` reads `Draining` after the second.

### 4. Medium - the establishment bound: 120 s is a defensible number argued from the wrong arithmetic, and it lies below the one delay the design measured

Design 5.7 `:1154-1159` and `ConnectionPool.h:463-468`: *"120 seconds, because the legitimate
worst case is two attempts of the 60 s per-attempt deadline plus two resolve-and-connect legs, and
the per-attempt deadline fires first on every path where it applies at all - so this bound
truncates nothing which would have succeeded."* Two things are wrong with the sentence and neither
is the number.

First, its own arithmetic: two 60 s deadlines plus two resolve-and-connect legs is more than 120 s
by the two legs, so the bound truncates the stated legitimate worst case by construction.

Second, "truncates nothing which would have succeeded": the per-attempt deadline does not apply
to resolve-and-connect - that is the front-end gap the same section records at `:1129-1136`, with
the figure of **134 s per black-holed address** on this host. The case that gap exists for is an
origin whose first-returned address drops SYNs and whose second answers: a dual-stack host with a
dead AAAA is the everyday shape, and `getaddrinfo` returns the v6 address first. Without the pool's
bound that request succeeds at about 134 s plus a handshake; with it, the placeholder is retired at
120 s, the task cancelled, and the queued waiters retried on a fresh placeholder which does the
same thing - three times (`maxRetriesPerRequest`), so about eight minutes of deterministic failure
for an origin that was reachable. The rider, if there was one, fails on the first expiry with
nothing to retry it (finding 5).

Whether 120 s is *right* is a policy question the design can settle either way - a browser would
never wait 134 s for one address, and would have connected over v4 in under a second, because it
bounds each address (Happy Eyeballs) rather than the whole establishment. What the record must not
do is claim the bound is free. **Fix:** argue the number against 134 and not against 2 x 60 -
either raise it above one SYN timeout plus one attempt (about 200 s) until the establisher bounds
each endpoint, or keep 120 s and say in 5.7 that a black-holed first address is a hard failure
through the pool until then; and carry the per-endpoint connect bound (L4 finding 1's front end,
still open) as the real fix, in the establisher, where `async_connect` over the resolver range is
what makes one address cost the whole timeout. The pinned mechanism (`H2Pool_EstablishmentBoundTests`
at 300 ms, task cancelled, timeout exception through `lastError`) is right for what it pins; note
that it pins the *attempts* path, not the waiter-deadline branch at `:1235-1245`, which no case
reaches.

### 5. Medium - dispatch capacity is latched from a `Ready` which the driver publishes before the peer's `SETTINGS`, and nothing between the pool and the wire enforces the peer's limit; the dispatched half of the retry which would absorb the consequence exists nowhere

**The latch.** `refreshEntry` sets `peerLimit` from `freeStreamSlots()` whenever it sees `Ready`
with `slotsInUse == 0` (`ConnectionPool.h:905-923`); `capacityOf` is `min( 256, peerLimit or 100 )`
(`:830-837`). The driver publishes `Ready` in `onProtocolNegotiated`, before the opening write is
issued (`Http2ConnectionTask.h:2303-2318`), and `publishFreeStreamSlots` reports `100 - used` until
the peer's `SETTINGS_MAX_CONCURRENT_STREAMS` has arrived (`:2229-2233`, `Session.h:2506-2508`).
So any examine which runs between `Ready` and the peer's `SETTINGS` - the first dispatch of a
request which waited for `Ready`, or any tick in that round trip - latches 100, and the latch is
refreshed only at a later `Ready` moment with zero slots out, which a connection under steady
load never has.

**What bounds the burst.** `examineKey` dispatches in a loop while `slotsInUse < capacityOf` and
`freeStreamSlots() > 0` (`:1231-1286`, `:1049-1052`); the second is re-read per iteration but
cannot move, because the submits are commands in a mailbox the strand has not drained. So one
examine can dispatch up to `capacityOf - slotsInUse` at once - up to 100 with the pre-`SETTINGS`
latch - against a peer whose limit is lower. `Session::submitRequest` checks the registry's
draining state and nothing about the peer's concurrency limit (`Session.h:906-960`), so every one
of them opens a stream and its `HEADERS` go out; the excess draws `RST_STREAM( REFUSED_STREAM )`
or `PROTOCOL_ERROR` from a conforming peer (RFC 9113 5.1.2). The driver maps the first to
`connection_refused`, retryable; the second is not retryable.

**Where the retry would be, and is not.** Design 5.4 `:940-951`, as S5.2 amended it: a dispatched
request "comes back through `releaseStream` and a fresh `acquire`, and its attempts are counted by
the request task, which has per-request state by construction". `HttpClientRequestTask.h` calls
`acquire` exactly once (`:556`), holds no attempt counter, and exposes `isRetryable()` and
`outcome()` "for the caller" (`:1518-1534`). So today the pool replays the queued half and *nobody*
replays the dispatched half: a `REFUSED_STREAM`, a GOAWAY above the stream's id, an ALPN bounce of
the preface rider, an establishment timeout of the rider's connection - every one fails the
request while the requests queued behind it are retried. That asymmetry is not stated anywhere;
the design sentence reads as if the counter existed.

**Reachability.** RFC 9113 6.5.2 recommends a limit no smaller than 100 and assuming 100 before
`SETTINGS` is what browsers do, so the pre-`SETTINGS` burst against a peer below 100 is within the
design and within practice, if not within what a strict peer forgives. The sticky latch is the
part which is not: a peer which said 16 keeps being sent bursts sized to 100 for as long as the
connection stays busy, and each burst above 16 loses requests.

**Fix.** (a) Whose half the dispatched retry is must be written down - the plan's S6.1 work order
is the natural owner, with the request task exposing what it does today - and design 5.4
`:946-947` must stop saying the request task counts. (b) Bound the burst: the cheap version keeps
a per-entry budget read once per examine from `freeStreamSlots()` and decremented per dispatch in
the loop, which bounds a single examine to what the driver last reported; the honest version is the
engine queueing submissions beyond the peer's limit and opening them as streams close, which is
what nghttp2 does and which would make the pool's count advisory rather than load-bearing - a core
change, its own change-set, gated on `h2core`. (c) Until `SETTINGS` have been seen the pool should
not assume 100 at `Ready`: the driver cannot say so through the frozen contract, but it can report
`freeStreamSlots()` as one until `peerLimitsConcurrentStreams()` is true, which is design 5.1's
own rule ("exactly one rides the preface, because until the peer's `SETTINGS` arrive nothing
else is known") applied at the layer which knows. That one is additive to the driver and worth
doing first.

### 6. Low, evidence - S5.1's control pins that the pull exists and comes first; it does not pin the bound the contract is named for

The case (`TestHttp2ConnectionTask.h:666-803`) never calls `provideBody()` itself, so a driver
without the pull moves no bytes and the stream never ends - that is a real control, and the
strongest thing in the case. `firstWanted < firstResponse` (`:801`) is a real ordering property
and it is deterministic as the comment says: `applySubmit` ends in `pumpBody`, which ends in
`raiseBodyWanted` (`Http2ConnectionTask.h:636`, `:867`), on the strand, before any read.

The arithmetic is weaker than its comment. The sink hands over at most what was asked, so
`wantedTotal >= upload.size()` (`:799`) is implied by the body-equality assertion two lines above
it and discriminates nothing. `wantedLargest < upload.size()` (`:798`) and `wantedCount > 1` are
implied by `bodyBytesWanted` being bounded by the peer's `SETTINGS_MAX_FRAME_SIZE`
(`Session.h:1039-1046`), 16384 by default (`Globals.h:200`): no ask can exceed 16 KB against a
200 KB body, whatever the driver does between asks. What none of the three can see is the
property the event is documented for - "at most one un-placed chunk per stream". A driver which
raised the pull while `pendingUpload` was non-empty, or which raised a second pull before the first
was answered, would pass every assertion in the case, because the sink answers each ask in full
and the peer receives everything either way. That bound lives in `raiseBodyWanted`'s four
conditions and the `isBodyWantedOutstanding` flag and was verified here by reading only.

**The discriminating case is cheap:** a sink which records the first pull and does not answer it
for 200 ms, an assertion that exactly one `wanted` record exists at that point, then the answer
and the rest of the case. That pins the driver's half of one-ask-one-answer, which is the half
the request-task suite cannot (its probe delivers each pull only after the previous answer,
`TestHttpClientRequestTask.h:1000-1006`, so it pins the task's half). A second, for the request
task: a source which yields nothing without finishing, asserting `body:0:more` in the trace - the
"legal nothing yet" answer has no case on either side.

### 7. Low - the maintenance tick is sound and terminates; the header's lock claim is broader than the code; one omission costs a quarter second; one branch tests the wrong task; two reads are unsynchronised

**Sound and terminating.** `m_isMaintenanceArmed` is true from the flip in `examineAll`
(`ConnectionPool.h:1341-1344`) through the handler's whole run, and only the flipping thread or
the handler itself calls `armMaintenance()`, so `expires_from_now` and `async_wait` are never
issued on a timer with a wait pending (which would cancel it and, by `:1575-1583`, leave the flag
true and nothing re-arming). The handler re-arms only while `hasWorkToWatch()` (`:1636-1655`): a
waiter, or an entry neither ready nor retired. A placeholder which never becomes ready is bounded
by `establishBy` when the bound is enabled; a waiter with no deadline and no connection to start
ticks at 250 ms until something changes, which is the price of polling and is fine. The backoff
doubles on an empty `Actions` and resets on a non-empty one (`:1604-1617`), and during a tick
`examineAll` forces `actions.armMaintenance` false (`:1345-1354`), so the emptiness test sees only
answers, cancels, schedules and starts, as intended.

**The lock claim.** `:424-425`: "Nothing is called while it is held: not a waiter's callback, not
the connection factory, not the execution queue, not a connection, not a timer." Under the lock,
`refreshEntry`, `findDispatchable` and `effectiveMaxConnectionsPerKey` call
`connection -> state()`, `freeStreamSlots()` and `negotiated()`, `attempt.driver()`,
`task -> getState()` and `task -> exception()`. For the h2 driver the first three are atomic
loads and a reference; for the h1 driver `state()` and `freeStreamSlots()` take `m_stateLock`
(`Http1ConnectionTask.h:1426-1441`); `TaskBase::exception()` takes the task's own lock
(`TaskBase.h:1129-1135`). So the pool lock is not a leaf in the sense written: the order is pool
lock, then a driver's state lock or a connection task's lock. It is safe because no driver and no
connection task ever reaches the pool - the request task does, from its deferred phase with
nothing held - and that fact, not "nothing is called", is what should be in the comment, since
it is the fact a future driver could break.

**The omission.** `m_maintenanceIntervalMs` is set to the minimum in the constructor (`:696-697`)
and thereafter only inside `onMaintenance`. When the tick stops (`:1623-1628`) it keeps its last
value, up to 250 ms, and the next `examineAll` which arms from idle arms at that value. So after
any pool has once idled with something to watch, the first tick of the next establishment fires up
to 250 ms late, and the first non-rider dispatch after `Ready` waits for it. One line at the flip
in `examineAll`: reset to the minimum when arming from idle.

**The wrong task.** `refreshEntry`'s last branch (`:979-1012`) retires the entry as a failed
attempt when `attempt.task -> getState()` is `Completed` and `isReady` is false. Once the ALPN
fallback has built a driver the original task *is* completed, successfully, so the branch would
retire every fallback connection on discovery unless the driver already reads `Ready` at that
moment. It does - `Http1ConnectionTaskT::m_state` is `Ready` from construction
(`Http1ConnectionTask.h:192-193`) - so the code is correct today by a property of another header
which the comment at `:984-989` ("it produced a driver, which the refresh above would have picked
up, so reaching here means it did not") does not state and no case pins: the fallback case's stubs
report `Ready` from construction too. Test `driverConnection` (or the driver task's state) in
that branch, not `attempt.task`.

**The unsynchronised reads.** `effectiveMaxConnectionsPerKey` evaluates `connection ->
negotiated().protocol()` before `entry -> isReady` in its `&&` chain (`:817-821`), so for a
`Connecting` h2 task - which is the entry's `taskConnection` from `startConnection` on,
`:1493-1495` - it reads `m_negotiated` while the strand may be writing it in
`continueAfterConnected`. The h2 driver documents that `m_negotiated` is published by the `Ready`
store and must be read after it (`Http2ConnectionTask.h:2303-2309`); the pool reads it before.
Only the enum half is read, so the harm is formal, but a TSan run of the real composition would
report it. `attempt.driver()` reads `ClientConnectionTaskBase::m_connection` (`:669-672`, written
on the strand at `:581`) with no acquire before it; the same object is safely published behind the
task's `Closed` state, which the pool loads only afterwards (`:903`). Reorder both: test `isReady`
first, and read the driver only after `taskConnection -> state()` reads `Closed`. Neither is
reachable by the h2client4 stubs, whose `negotiated()` is a const member and whose `driverOf`
returns an immutable copy - which is what "the stub does not establish" means concretely, section 8.

### 8. Nits - small inaccuracies and unpinned corners, not defects

- **(a)** Per-request timer overrides are unpinned: `effectiveTimeout` is used for both timers
  (`HttpClientRequestTask.h:541-545`, `:667-674`) but every case sets the config, none the request.
  The bug the S5.1 merge says it caught ("the total timer reading the session default while the
  code below it read the request's override") is therefore fixed and unpinned.
- **(b)** `applyBodyWanted` guards on the source, the connection and the stream (`:839-842`) and
  not on completion, so a pull which arrives after a timeout or a cancel, before the driver has
  applied the reset, reads the caller's `BodySource` for a request that has already been answered.
  Add `m_isCompleted || m_isCompletionPending`.
- **(c)** The block allocated per pull is `wanted` bytes (`:852`), which is the peer's
  `SETTINGS_MAX_FRAME_SIZE` at most (up to 16 MB, `Session.h:1039-1046`) - a peer-chosen allocation
  per stream, bounded, and worth a cap at the request task since a source with a natural chunk
  fills what it is given.
- **(d)** The h1 suite's `RecordingSink` records `wanted:` so that a case can assert it never
  happens (`TestHttp1ConnectionTask.h:164-183`); no case does (grepped: the only `wanted` in the
  file is the recorder). One `UTF_REQUIRE( ! has( "wanted" ) )` in the existing upload-refusal
  case pins the h1 half of the contract for free.
- **(e)** `BodySink` has the readiness gap `BodySource` has, one layer over: a sink which returns
  zero is re-offered only when the next `onData` arrives (`:791-830`), nothing is credited so the
  window closes, and after the advertised window's worth of blocks nothing arrives to re-offer on.
  `ClientTypes.h:312` says "the remainder is offered again" without saying when. It belongs in
  `body-source-readiness-deferral.md` as the sibling it is.
- **(f)** S5.1's three commits say "no ThreadSanitizer report" for four modules and name no
  control; S5.2's names `utf_baselib_basictask`'s known race and its exit code. The L4 second pass
  set the standard, and only one lane met it.
- **(g)** `ClientConnection.h:335-342` says `freeStreamSlots()` is "zero for a connection which is
  Connecting, Draining or Closed"; after finding 3, a connection past its reserve is none of the
  three and reports slots it cannot use. The sentence is right and the driver is what should
  change.
- **(h)** `Http1ConnectionTask.h:1404-1412`, the h1 note S5.1 added, is right and well placed.
- **(i)** The S5.2 merge says the request which rides the preface is "the first of them"; the
  code takes the first *replayable* waiter which reaches the head of the FIFO (`:1231-1305`), and
  a non-replayable one at the head blocks the rider behind it - which is the FIFO rule at `:1298-1302`
  and correct, and slightly different from the sentence.

## What the stub does and does not establish

`utf_baselib_h2client4` instantiates no session, driver, peer or stream policy; its
`StubConnectionTaskT` is a `SimpleTaskBase` which is also a `ClientConnection`, with `om::Object`
as a base twice and the two-entry QI table (`TestConnectionPool.h:286-311`), which is the shape of
`Http2ConnectionTaskT` (`Http2ConnectionTask.h`'s own `BL_QITBL`, verified in L4). What that
establishes: `om::tryQI< ClientConnection >( task )` resolves through a double `om::Object` base
at compile time and at run time for a type of that shape, `self_ref_t` over the `Task` half
compiles, and the pool's bookkeeping is right against the contract's *documented* state
semantics - `Connecting` then `Ready` with slots, `Draining` monotone, `Closed`, a task which
completes or is cancelled. All eleven cases are about that bookkeeping and they pin it.

What it does not establish, each item being where a finding above lives: that the real driver's
`Ready` precedes the peer's `SETTINGS` and what the pool latches at that moment (finding 5); that
a refused `submit()` is answered by no `releaseStream` (finding 1 - the stub's `submit` never
refuses, `:409-420`); that a fallback driver is `Ready` on discovery (finding 7, third part - the
stub's driver is constructed `Ready` because the case says so, `:1457`); that the pool's reads of
`negotiated()` and `connection()` are ordered against the strand's writes (finding 7, fourth part -
the stub's are immutable); that `requestCancel()` on a real connection task in the middle of a
resolve or a handshake ends in `Task::Completed` with `exception()` set, which `refreshEntry`
relies on (`:979-1012` - the stub completes through a posted callback); that the policy reaches a
driver at all (finding 3 - the stub's factory `BL_UNUSED`s it, `:764-765`); that the h1 driver's
state lock under the pool lock is deadlock-free (finding 7, second part - the stub has no lock).
The three real components - pool, request task, driver - have not been composed in any module in
the tree: S5.1 ran against a probe pool and a probe connection, S5.2 against stub connections,
and the driver against a recording sink. Finding 1 is a composition defect of exactly the kind
that layout cannot see, and S6.1's first end-to-end case will be the first thing to run it.

## 9. Notes for later slices

- **S6.1, before anything else:** finding 1's pairing rule, then finding 2's two phase-one calls;
  neither can wait for a real request to find them. The dispatched-half retry (finding 5c) is
  either S6.1's or nobody's, and the plan should say which. A `BodySource` request must never be
  dispatched to an HTTP/1.1 connection: the pool cannot tell (it sees a key, not a protocol, until
  the connection is `Ready`), the h1 driver refuses it synchronously, and the request task calls
  the connection unusable for it - the session should fail such a request with `NotSupportedException`
  when the connection it was given reports `Http11`, before submitting. The factory S6.1 writes is
  where `ConnectionPoolPolicy::idleTimeout` and `drainingReserve` become an `Http2ConnectionConfig`;
  until it exists, both are constants which travel nowhere. `refreshEntry`'s reliance on
  `exception()` of a cancelled real connection task (section 8) should be pinned by S6.1's first
  establishment-timeout case against a real black hole, or a listening socket that never accepts.
- **The driver (its own change-set, gated on `h2client2`):** finding 3's `Draining` on
  `isDraining()`; finding 5c's `freeStreamSlots()` of one until `SETTINGS`; finding 7's two
  reorders are the pool's, not the driver's.
- **The engine (its own change-set, gated on `h2core`):** finding 5b's queueing of submissions
  beyond the peer's limit, if the pool's count is not to remain load-bearing.
- **The establisher (L4 finding 1's front end, still open):** a per-endpoint connect bound, which
  is what makes finding 4's number a policy rather than a hazard.
- **`body-source-readiness-deferral.md`:** nit 8(e), the sink side.
- **Design 5.4 and 5.7:** `:946-947` ("counted by the request task") is not true of the tree;
  `:913-921` describes a retirement the driver does not perform; `:1154-1159` argues 120 s from
  arithmetic which contradicts itself. **Plan `:1359-1366`:** replace with the pairing rule.

## What was verified versus inferred

Verified by reading the code against the design: every row of the verdict table's basis column;
the whole of `HttpClientRequestTask.h` and `ConnectionPool.h`; every hunk of the range in
`ClientConnection.h`, `Http1ConnectionTask.h`, `Session.h`, `PreCompiled.h` and
`Http2ConnectionTask.h`, and around the last of those `canOpenStream`, `applySubmit`,
`applyProvideBody`, `pumpBody`, `raiseBodyWanted`, `pumpAllBodies`, `failSubmission`,
`closeStream`, `onStreamClosedEvent`, `onGoAwayReceived`, `onPeerClosed`, `closeGracefully`,
`closeSubmissions`, `publishState`, `publishFreeStreamSlots`, `submit`, the `Ready` publication
and the initial `Connecting`; `Session::submitRequest`, `bodyBytesWanted` and the `SETTINGS`
switch; the registry's `remainingLocalStreams`, `isDraining`, `setDrainingReserve` and
`canOpenLocalStream`; `TaskBase::getState` (atomic) and `exception()` (locked);
`ClientConnectionTaskBase::connection()` and the one write of `m_connection`; the h1 driver's
initial state, `submit` and `freeStreamSlots`; `BL_NOEXCEPT_END` through `BL_RIP_MSG` to
`fastAbort`; `ClientTypes.h` in full. Verified by grep: `slotsInUse` has one decrement;
`releaseStream` has one caller in `src/include/`; `acquire` is called once by the request task;
`drainingReserve` is read by nothing outside the three headers named; `publishState( Draining )`
has four callers and none is a registry condition; `ClientStreamEventSink` has three test
implementers and one production one; no `BL_DEVENV_VERSION` in the new headers; every new case
has a recipe; no `wanted` assertion in the h1 suite. Verified by diffing: the additive claims
(`Session.h` 18/0, `ClientConnection.h` 50/0, `Http1ConnectionTask.h` 7/0, `PreCompiled.h` 8/1
comment-only, `Http2ConnectionTask.h` 109/5 with the five deletions in `pumpBody`'s doc block);
the manifest's 22 additions and zero deletions; that no runlog, gate or baseline file changed in
the range. Verified by reading the tests: what every case named above asserts, `ProbePool`'s
dropped callback, `RecordingSinkT::onBodyWanted`'s "at most what was asked", the stub task's
never-refusing `submit`, the fallback stub's constructed-`Ready` driver.

Inferred or recalled: that `getaddrinfo` returns a v6 address first on a dual-stack host and that
a dead AAAA is the everyday shape of finding 4 (RFC 6724 ordering, from memory); that a conforming
peer answers an over-limit `HEADERS` with `REFUSED_STREAM` or `PROTOCOL_ERROR` (RFC 9113 5.1.2,
from the text held in L3); the peer-limit figures behind "a peer below 100" (nginx 128, HAProxy
100, Go 250, from memory, illustrative); that TSan would report the two unsynchronised reads in
finding 7 (a claim about the tool, not an observation); that `H2Driver_UploadIsPulledFromTheSinkTests`'s
asks are bounded by 16384 (from `bodyBytesWanted` and the default, not from a trace - the lane
reports the case green, which is consistent); that the seam's timer race is narrow (from the
arming order, not measured).

## What was not checked

- **No build and no test run.** Every "clean under -Werror", the 53 and 11 case counts, the
  object sizes (30.71 MB `httpclient`, 26.68 MB `h2client4`, 76.83 MB gcc release), the two TSan
  runs and their control, and the 22-case manifest's runtime are as reported.
- **No gate is recorded in the range.** The L4 round ended in `b57727b gate: L4 passes`; this
  range has no gate commit, no runlog change and no message naming clang release or gcc release.
  The orchestrator's validation may exist outside the tree; it is not in it.
- **The h1 driver's `finishStream`** was not re-read for whether it holds `m_stateLock` across
  the sink call; the L4 record's "state settled before `onClosed`" was relied on for the
  lock-order argument in finding 7.
- **`ExecutionQueueImpl::forceFlushNoThrow` from a destructor during teardown** (`ConnectionPool.h:1768-1773`)
  was not traced; the design's disposal paragraph asserts it is safe and the destructor path has
  no case.
- **`TaskBase::getThreadPool( eq )`** and whether the request task's drain really lands on
  `ThreadPoolId::GeneralPurpose` for a queue built on another pool; design 5.2 says GP, the code
  says the queue's.
- **`Http2TestServer.h`'s `bodyOf`, `requireStreamClosedAtPeer` and `awaitStreamClosed`** were
  relied on as L4 left them, not re-read.
- **`DataBlock::get`/`copy` capacity semantics** beyond what `StubBodySource::read` shows
  (`TestClientContracts.h:98-119`, which bounds itself by `capacity() - size()`).
- **Windows**, and the **1.1.1w flavor**, as before; **a64**, on which finding 7's unsynchronised
  reads stop being formal.

## Second pass: the fix round `7a6f6ba..4399b0c`, tip `4399b0c` (2026-09-19)

**Verdict: the five findings and the late one are carried out as described; the High is closed on
every path an `acquire` can be answered on, not only the two that leaked; three of the five
departures are right, one is right in its conclusion and wrong in its reasoning, and one rests on
a sentence about the driver which is false on the two routes it names; nothing new in the
production code beyond one Low and one misstatement, and the flake shape lives in every case of
the pool suite on its failure path.** Read in full: the three production diffs
(`HttpClientRequestTask.h` 174/57, `ConnectionPool.h` 388/52, `Http2ConnectionTask.h` 43/1), the
three test diffs and the eight new cases, the design and plan diffs, every commit message in the
range, and the tip code around every function the round touched, with `TaskBase::requestCancelInternal`,
`ExecutionQueueImpl::forceFlushNoThrow` and `StreamRegistry::openLocalStream` read for the
questions below. Nothing was built and nothing was run; the module results (15/15, 56/56, 12/12,
3/3 under both release toolchains) are as reported, and the manifest holds 1005 cases (counted from
the `cases` lists at tip), the eight new names and nothing removed. **No TSan run is recorded for
the round** in any of its commit messages, against a pool which gained a settle timestamp, a
known-limit flag and a cancel flag, and a request task whose deferred phase now runs the body read.

**Finding 1 (High) - the pairing rule, walked on every path.** The pool answers an `acquire` in
exactly five shapes and a slot travels with exactly one of them: a dispatch
(`answerWaiter( actions, waiter, entry -> current(), nullptr )`, `ConnectionPool.h:1569-1577`,
after `++slotsInUse`), where `findDispatchable` has already required `current()` non-null
(`:1327-1331`); a waiter deadline (`:1525-1535`), the attempts bound (`:1537-1562`), disposal
(`:2012-2021`) and an `acquire` after disposal, all with a null connection and an exception. The
request task's `applyAcquired` (`HttpClientRequestTask.h:626-760`) has five branches and the
release is present on every one which can have a slot: completion pending (`:631-644`) releases
if the connection is non-null; exception-or-null (`:646-660`) releases nothing, which is right
since no slot came; a throwing `submit()` (`:681-707`) and a refused one (`:709-745`) both
release the slot and drop the connection; a successful one records the handle and releases at
`applyClosed`, whose `m_isStreamClosed` guard makes it once. `releaseConnectionSlot` is guarded
on the connection alone (`:1386-1389`) and `releaseStream` decrements for the connection it is
handed, both of an entry's pointers being registered (`:1806` for the task, `:1188` for a
fallback driver) and both erased only at zero (`:1135-1150`). So the rule "every answered acquire,
exactly one release, stream or no stream" holds by construction on the task side and by lookup on
the pool side, and it depends on one property of the pool which is true and unstated: an answer
carries a connection if and only if it carries a slot. The task's branch order tests the exception
first, so a pool which ever answered with both would leak silently - a robustness nit, one
reordering, not a defect in this pool. The pin is what the first pass asked for:
`HttpClientRequestTask_RefusedSubmitReturnsTheSlotToTheRealPoolTests` (`TestHttpClientRequestTask.h:1513-1578`)
composes `ConnectionPoolImpl`, a real request task and a refusing probe through the fallback shape
of `connectionFactoryFor` (`:731-757`, a `SimpleTaskImpl` as the attempt and the probe through the
accessor), and asserts the pool's own `slotsInUse( ) == 0`, one dispatched, one released, one
retired, `connectionCount( ) == 0`; its "no rendezvous needed" comment is correct, since the
release is a phase-two action of the drain which notifies in phase three.
`HttpClientRequestTask_LatePoolAnswerStillReturnsTheSlotTests` (`:1580-1645`) pins the other path
with a held answer (`holdTheAnswer`/`answerNow`, `:544-570`), asserting `0:failed` and no submit.
Both are the right assertions.

**One consequence the round left standing, and it is now sharper than before.** The h1 driver's
refusal of a `BodySource` request still produces `ConnectionUnusable`, which the merge defers as
"a contract change". For *this* refusal it is not: the request task holds the request (its
`bodySource()`) and the connection (`negotiated().protocol()`), both on the frozen contract, so
"an `Http11` connection refused a `BodySource` request" is distinguishable today, and should be
`Failed`, not retryable, connection kept. What makes it matter is the late finding: a retired entry
at zero slots is now *cancelled* (`forgetConnection`, `:1135-1150`), so a healthy HTTP/1.1
connection is killed for every streaming upload a caller sends its way, where before the fix round
it merely leaked a slot. The plan's S6.1 work order carries the factory and the dispatched-half
retry as obligations and does not carry this one (grepped: `BodySource` appears in the plan only
in S2.6, S5.1 and the seam paragraph). **Unresolved, ledger.**

**Departure 1 - finding 5: the "one until known" rule is the right fix and its three ways are
sound; (a)'s claim holds on two conditions, one of them unenforced; the stored limit has one
conservative wrinkle.** `capacityOf` returns `UNCONFIRMED_MAX_CONCURRENT_STREAMS` (one) until
`isPeerLimitKnown` (`:974-985`), and the settle window is checked in the same `learnPeerLimit`
(`:1014-1056`) which takes the reading, one reading per examine (`:1215`).

*(a) The band.* The pre-`SETTINGS` driver reports `100 - m_streams.size()`
(`Http2ConnectionTask.h:2254-2275`, the assumed limit at `:261`), and `m_streams.size() <=
slotsInUse` at every instant because a stream is erased from `m_streams` before its sink's
`onClosed` is even posted (`closeStream`, `:1093-1098`), and the pool's decrement comes after that
post, a drain and a `releaseStream`; the slots for requests which never opened a stream only widen
the band, which is the safe direction. So a reading outside `[ 100 - slotsInUse, 100 ]` cannot be
the assumption's - verified. The two conditions: the driver's `ASSUMED_MAX_CONCURRENT_STREAMS` and
the pool's must be the same number, which they are (`:261` and `ConnectionPool.h:104`) and which
nothing enforces - the pool's comment says it "HAS to be", and a `static_assert` is impossible
across two headers the driver does not include, so it should at least be a test in
`H2Pool_PolicyDefaultsTests`; and a zero reading while the state still reads `Ready` - possible
in the window between an external cancel and `onTaskStoppedNothrow`'s `Closed`, since
`publishFreeStreamSlots` now stores zero on `! canOpenStream()` (`:2265`) - is taken as "outside
the band" and latches `peerLimit = slotsInUse`; harmless, because that connection reads `Closed`
on the next examine and is retired.

*(b) A completed response.* RFC 9113 section 3.4 makes the server's `SETTINGS` the first frame it
sends (recalled, not re-fetched) and the engine applies frames in order, so `Completed` at
`releaseStream` (`:2170-2183`) does prove the peer has spoken. Right, and the only proof
available for a peer at exactly the assumed number.

*(c) The settle window.* Armed at the first `Ready` reading (`:1030-1035`), one-shot, default
1 s (`:203`); on expiry the reading is taken as the peer's (`:1037-1040`), which is the pre-fix
behaviour, so the window's worst case is the old behaviour on a path whose `SETTINGS` take longer
than a second to arrive - a heuristic, and honestly recorded as one. The cost on the peer at
exactly 100 is one stream for at most a second plus a tick, and less than that whenever the first
response returns sooner, which is the ordinary case. **5(c) - the driver reporting one slot until
`peerLimitsConcurrentStreams()` - is still not done**, and the design says in as many words that it
would remove the window's cost; it is the additive driver change the first pass asked for first.
Unresolved, but owned.

*The burst.* `peerLimit` is lowered to `slots + slotsInUse` whenever that is lower (`:1049-1054`),
so a single examine dispatches at most the slots the driver last reported, and a later examine
against the same stale reading finds `slotsInUse` already at the bound. Verified by walking the
loop; that closes the burst finding 5 named.

*The wrinkle.* At `slotsInUse == 0` the reading is stored as `peerLimit` unconditionally
(`:1042-1047`), including when it is the pre-`SETTINGS` assumption; thereafter it is only lowered.
A peer allowing 250 whose first idle reading was the assumed 100 is therefore capped at 100 (or 101
with the rider out) until its next idle moment, which under steady load never comes -
under-dispatch, never over-dispatch, and bounded by `maxStreamsPerConnection` in any case. Low.
Fix: when `isPeerLimitKnown` transitions in `learnPeerLimit`, take that reading fresh
(`slots + slotsInUse`, or `slots` at zero) rather than the minimum. The three cases are consistent
with the code as read: the stub's `freeStreamSlots` never moves on `submit`, which the band
arithmetic does not need, and `H2Pool_AssumedLimitIsNotDispatchedAgainstTests`'s "two free with
one out" is precisely a reading below `100 - 1`.

**Departure 2 - finding 4: the truncation is now recorded as a hard failure, the arithmetic
checks, and the per-endpoint bound is rightly the only real fix.** Design 5.7 `:1194-1213` and
`ConnectionPool.h:520-548` say what the first pass asked them to say: the old argument
contradicted itself, the per-attempt deadline does not cover resolve-and-connect, a dual-stack
host with a stale `AAAA` is failed at 120 s on each of three attempts, and that is a policy
choice. The figures check: one dead address is 134 + 60 = about 200 s, two about 330, three about
470; three retries of 200 s are ten minutes. `H2Pool_PolicyDefaultsTests` pins
`establishmentTimeout < 134`, which pins the admission itself. Naming the establisher's
per-endpoint connect bound as the only fix is right: the quantity is addresses times SYN timeout,
and only something applied per address changes it. Two things to add when that fix is scheduled:
`DEFAULT_HANDSHAKE_RETRY_COUNT` of 1 re-walks the *same* address list, so the retry buys nothing
against a black hole and doubles the bill; and the cheapest shape of a per-endpoint bound on Linux
is `TCP_SYNCNT` on the socket before `async_connect`, which caps the SYN schedule without an Asio
timer (recalled, not verified).

**Departure 3 - the late finding: all four routes are covered, the double cancel is idempotent
three times over, and the sentence which justifies cancelling a `Draining` driver is false on both
`Draining` routes.** The four retirements - `Draining`/`Closed` observed (`:1217-1224`), the bound
expired (`:1238-1272`, with its own `chkCancelEntry`), the attempt task ended (`:1274-1301`), and
`ConnectionUnusable` reported (`:2184-2191`) - all set `isRetired`, and every one is followed in
the same call by `examineKey`'s `isRetired && 0U == slotsInUse` check (`:1470-1482`), which is
the single door to `forgetConnection`; the factory-failure retirement (`:1783-1787`) reaches it too
with no task to cancel. Idempotence: `isCancelRequested` on the entry (`:1081-1086`),
`chkCancelTask`'s `Completed` skip (`:1058-1067`), and `TaskBase::requestCancelInternal`'s own
`m_cancelRequested` and `Running` guards (`TaskBase.h:1032-1041`) - the pool's flag is the tidy
one, the other two would have made a second cancel harmless anyway. Disposal covers a fallback
driver the pool scheduled although it collects and cancels only `attempt.task` (`:2033-2035`, `:2070-2072`): the queue flush
runs with `cancelExecuting` true (`ExecutionQueueImpl.h:1420-1437`).

*The sentence.* `:1124-1130`: *"A real driver which is Draining with nothing in flight is not on
its way out either: the GOAWAY drain takes itself to Closed through chkFinishClose() when its last
stream ends, so a driver still reading Draining here is one staying up."* On both `Draining`
routes the opposite holds. `onStreamClosedEvent` (`Http2ConnectionTask.h:1320-1343`) posts the
sink's `onClosed` and then, in the same strand handler, calls `closeGracefully()` when
`m_streams.empty()` and the state is `Draining` - so by the time that `onClosed` has crossed the
mailbox, been drained, and reached `releaseStream`, the driver has already queued its GOAWAY, armed
its drain deadline and published `Draining` from `closeGracefully` (`:2080-2095`). A driver
reading `Draining` at `forgetConnection` on the GOAWAY-received route and on the reserve route is
one *in the middle of closing*, and the pool's `requestCancel()` races its GOAWAY write. The
consequence is what L4's second pass established for the drain deadline, which takes the same
`requestCancelInternal` path: the write's abort ends the task as a cancel marked expected instead
of a success, and the GOAWAY reaches the peer only if the write had already left the socket
buffer, which for a nine-byte frame it ordinarily has. Classification and a SHOULD; not
correctness. The driver which really does stay up is narrower than the comment says: `Draining`
published from the *refused* branch of `applySubmit` with an empty stream table (a session
draining from birth, a misconfiguration), and the `ConnectionUnusable` route on a healthy h1
connection, which is the misclassification above. So the cancel is right as an invariant - "a
connection the pool forgets is one nothing else will stop" is true, and the establishment bound
and the h1 case need it - and its comment should say that on the `Draining` routes it usually
finds a close already in progress and cuts it short. Two pins encode that: `H2Pool_GoAwayDrainingTests`
now asserts `waitForCancel()` on the GOAWAY route, which with a real driver is the race; and
`H2Driver_DrainingReserveIsPublishedToThePoolTests` (`TestHttp2ConnectionTask.h:1482-1610`)
asserts the graceful GOAWAY with error 0 (`:1593`) for the driver alone, which composed with the
pool is not guaranteed. Neither is wrong; together they describe a race as two certainties. Low,
comment and design 5.4's GOAWAY paragraph.

**Departure 4 - finding 2's `submit()`: the guard is the right shape; the reasoning that it could
not be deferred is wrong, and the placement is a choice rather than a necessity.** The class
comment (`HttpClientRequestTask.h:150-157`) and the merge argue that a deferred `submit()` would
let a sink event overtake the handle. Sink events cannot overtake the drain: they append to the
mailbox and are applied by a *later* batch, and phase two of the current batch runs to completion
before that batch's phase one - the mailbox flag, not the task lock, is what serializes phases,
and phase-two state is drain-owned by the class's own account (`offerToSink` already reads
`m_handle` in phase two, `:913`). A `submit()` in phase two which records `m_handle` there is
therefore ordered before every event the connection can deliver. The hazard that is real is the
one the comment does not name: two events in the *same* batch, `[ Acquired, Expired ]`, where
phase one applies the expiry with no handle, defers no reset, and phase two then opens a stream
for a request already failed - reset only when its response completes. That is avoidable with one
check of `m_isCompletionPending` inside the deferred submit, so deferring is possible and not
"wrong"; it is merely not simpler. The chosen placement costs a documented lock-order edge and
`toSessionRequest` under the request-task lock, and buys same-batch ordering for free. Keep it;
the guard (`:681-707`, `Failed`, not retryable, slot and connection released) is the right shape
in either placement; the comment's "BY NECESSITY" should become "by choice, for same-batch
ordering". Nit.

**Departure 5 - the identifier wrap: the refutation is right.** `openLocalStream`
(`StreamStateMachine.h:1137-1167`) tests `canOpenLocalStream()` before touching the counter, and
`canOpenLocalStream()` requires `remainingLocalStreams() != 0`, which requires
`m_nextLocalStreamId <= MAX_STREAM_ID` (`:1083-1091`). So the advance `streamId + 2U` runs only
from a value at most `2^31 - 1` and produces at most `2^31 + 1`, representable in the `uint32`
(`:877`) and rejected by the `> MAX_STREAM_ID` test from then on; the subtraction in
`remainingLocalStreams` is guarded by the same test. Both roles check: the client's last id is
`2^31 - 1` itself, the server's `2^31 - 2`, and the value after each is above the maximum. And
`Session::submitRequest` asks `canOpenLocalStream()` again before calling in (`Session.h:920-926`),
so the guard is doubled. The new case's reserve arithmetic is consistent with this: `(MAX - 1) / 2`
leaves `2^30` in hand at birth, one above the reserve, so exactly one stream opens before
`isDraining()` - as the case asserts. Withdrawn rightly.

**The flake shape, and where else it lives.** The shape is a posted answer holding a raw pointer
to an `Answers` on the case's stack, via `acquireInto( ..., &answers, ... )`
(`TestConnectionPool.h:904-923`), and it is in every one of the suite's thirteen acquiring cases
by construction. On the success path it is live only where a case returns with an answer still in
flight, and after this round that is nowhere: each case's final `waitFor( N )` equals the number of
answers it caused, or the case ends with no waiter queued (checked case by case against the
acquire counts). On the **failure path it is live in every case which has a waiter queued at the
failing assertion**: a `UTF_REQUIRE` unwinds through `PoolGuard::~PoolGuard`, whose `dispose()`
posts an aborted answer per waiter (`:2012-2021`), and `answers` is destroyed a few frames later -
so the guard, written to stop "a second failure which hides the first", produces exactly one
whenever it has waiters to answer. `H2Pool_ConcurrentAcquireAndReleaseTests` has the same shape
through `[ &pool, &answers, &released ]` (`:1686`, `:1695`). The sibling shape is the factory:
`cpp::ref( factory )` hands the pool a reference to a stack `StubFactory`, `startConnection`
calls it outside the lock from `runActions`, and `disposeInternal` does not join a `runActions`
already in flight on a tick - narrow, and the same class of hazard. The other two suites are
clean: `ProbePool` posts a refcounted connection and a `shared_ptr` callback holding the task's
own reference, and the `[ & ]` lambdas in both are synchronous scopes under `runTask`, `withPeer`
and `runDriver`. **Fix, structural and two lines:** `Answers` and `StubFactory` held by
`std::shared_ptr` and captured by value, which removes the shape from both paths at once;
the three `waitFor( 5U )` rendezvous would then be belt and braces rather than load-bearing.

**New or unresolved.**

- *Unresolved, ledger:* the h1 `BodySource` refusal's outcome - distinguishable on the frozen
  contract, now fatal to a healthy connection, and on no work order.
- *Unresolved, owned:* 5(c), the driver reporting one slot until `peerLimitsConcurrentStreams()`,
  which is what makes the settle window unnecessary.
- *Unresolved, evidence:* no TSan run recorded for the round; the driver-plus-pool composition
  still exercised in no module, so the two `Draining`-route pins describe a race as two
  certainties.
- *New, Low:* the `Draining`-routes sentence at `ConnectionPool.h:1124-1130` (departure 3).
- *New, Low:* `peerLimit` stored from a pre-`SETTINGS` idle reading and only ever lowered
  (departure 1, the wrinkle).
- *Nits:* the equal-constants assumption unpinned; the answer's connection-and-exception branch
  order; "BY NECESSITY" in the request task's class comment; `TCP_SYNCNT` and the
  same-list handshake retry for the per-endpoint bound's work order.

**What else in this round rests on evidence that thin.** The eight new cases pin what they say;
what remains unpinned is the composition: no case runs the real pool over the real driver, so the
forget-cancel racing a graceful close, the settle window against a real `SETTINGS` round trip, and
the band inference against a driver whose `m_streams` really lags `slotsInUse` are all verified
here by reading. The lane's negative controls (five degradations shown red for finding 2, the
reverted hunks for finding 3) are as reported. The manifest is committed; the runtime baseline is
not, as before.

**Verified versus inferred in this pass.** Verified by reading: every diff named above and the
tip code around each; the five answer shapes and the five `applyAcquired` branches; both
registrations and the single erasure of an entry's pointers; the erase-before-`onClosed` order
in `closeStream` that makes the band inference sound; `chkPublishDraining`'s two call sites and
`publishFreeStreamSlots`'s new predicate; the four retirement routes and the one door; the three
idempotence guards; the flush's `cancelExecuting`; `openLocalStream`'s guard order and the
counter's type; `offerToSink`'s phase-two read of `m_handle`; the acquire-versus-wait counts of
all thirteen pool cases; the absence of `BodySource` from the S6.1 work order and of "sanitizer"
from the round's messages; the manifest's 1005 by counting. Inferred or recalled: RFC 9113
section 3.4's ordering of the server preface; that a nine-byte GOAWAY already handed to
`async_write` ordinarily leaves the socket buffer before a cancel lands (a claim about socket
buffers, as in L4); `TCP_SYNCNT`'s semantics and the same-list handshake retry's cost; that the
L4 drain-deadline analysis transfers to an external `requestCancel()`, which follows the same
`requestCancelInternal` path.

**Not checked in this pass.** No build and no test run. `TcpConnectionEstablisherConnector::cancelTask`
was not re-read for this round; the drain-deadline reading from the L4 second pass was relied
on. The h1 driver's `Draining` publication on a non-reusable response and whether it closes
itself afterwards, which decides whether the `Draining`-route cancel finds a closing h1 driver
too. `SimpleTaskImpl`'s completion timing in `connectionFactoryFor`, assumed immediate. Windows,
the 1.1.1w flavor, and a64, as before.
