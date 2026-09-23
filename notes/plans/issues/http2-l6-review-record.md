# HTTP/2 client, Layer L6: review record

**Reviewed:** 2026-09-20, read-only, at tip `6d91d0c`. **Status:** RECORD. Nothing was built and
nothing was run: `httpclient/ClientSession.h` was read whole, both new test headers and both
`Main.cpp` files were read whole, and every production header the session leans on was read at the
places named below - the pool's predicate, waiter counting, `releaseStream`, `refreshEntry`,
`findDispatchable` and `resolveDriver`; the request task's constructor, `applyAcquired`,
`applyClosed`, `answerOnClosed`, `releaseConnectionSlot`, `applyStart` and `effectiveTimeout`; the
driver's `onProtocolNegotiated`, `closeSubmissions`, `onTaskStoppedNothrow`, `chkPublishDraining`
and `submit`; the establishment base's `onProtocolNegotiated` and `continueAfterConnected`;
`WrapperTaskBaseT`, `ForwarderTaskBaseT`, `RetryableWrapperTaskT::continuationTask` and
`ExecutionQueueImpl::onReady`; `RedirectPolicy::evaluate`; the h1 driver's request serialisation
and its ownership note; `Http1Codec::renderName`; `HeaderProfile`; `ClientConnectionConfig` and
`Http2ConnectionConfig` defaults; `ConnectionKey` and `RequestOutcome`. The most important results
are one **High** on the redirect path, which follows an `http` to `https` hop on a cleartext session
and so does the one thing design 5.6 says the session refuses to do (finding 1); the retry fix,
which is right in shape and bounded, and whose control case demonstrates a defect the lane read as
a virtue (finding 4); and a knob which reaches nothing through the session (finding 2), which is the
lane's own standard applied to its own code. Nothing found blocks L7 from starting, but the last
section says what should not be left as it stands.

**The range.** `582d9c5..6d91d0c`, four commits: `19d93c2` (the session and
`utf_baselib_httpclient4`), `3688c18` (the TLS instantiation and `utf_baselib_httpclient5`), the
merge `1265104`, and the manifest refresh `6d91d0c`, which touches `inventory.json` only. The
production diff is one new header, 1759 lines; the design diff is 77 lines in 5.4, 5.6 and 5.8; the
plan diff is 43 lines, the "as landed" record of S6.1.

**What was reviewed against.** Design 5.4 at tip, including the "Landed in S6.1" paragraph; 5.6 at
tip, including the five settlements S6.1 added; 5.8 at tip; 5.7's timeout table; plan section 8, the
S6.1 work order and its "as landed"; my L5 record, both passes - the obligations its section 9
assigned to S6.1 and the ledger its second pass left. `src/utests/AGENTS.md` and
`test-instantiation-weight-deferral.md` for the size question. RFC 6265 sections 4.1.1 and 5.4, RFC
7301 section 3.1, RFC 9110 section 11.7.1 and RFC 9113 sections 3.4 and 8.2.1 from memory, not
re-fetched.

## Verdict per slice

| Slice | Verdict | Basis |
|---|---|---|
| `19d93c2` - the session and `httpclient4` | **Conforms to 5.6 and 5.8 in shape - the pool, the state, the plan snapshot, the pure header functions, the hop chain as a `WrapperTaskBase` continuation and the factory which carries the policy - and to the S6.1 work order on every obligation it names; the dispatched-half retry is bounded and interacts correctly with the pool's half; the two entry points which must enforce "one session speaks one scheme" enforce it once (finding 1); one policy knob is unreachable through the session (finding 2); the request-level total timeout is per attempt rather than per request (finding 3); the idle lifetime reaches one of the two drivers (finding 5); the inter-hop work, including caller code, runs under the execution queue's lock (finding 6).** | `chkPrepareRetry` (`ClientSession.h:1034-1058`) reads the hop's `isRetryable()`/`outcome()`, hands `m_attempts` (incremented at every `startHop`, `:1013`; reset only on a followed redirect, `:1277`) to `chkRequestMayBeReplayed` (`ConnectionPool.h:429-454`, `attempts > maxRetriesPerRequest` refuses), and rewinds the source only after the predicate has accepted it. The bounced rider's outcome is `Failed` (`HttpClientRequestTask.h:1081`, from the error code), so `releaseStream` (`ConnectionPool.h:2190-2260`) does not retire the entry and the retry's fresh `acquire` finds the adopted h1 driver (`refreshEntry`, `:1219-1243`) - verified by walking the path, and consistent with `connectionsCreated == 2` for two `Connection: close` requests (`TestClientSession.h:1745`). The continuation is re-entered by `ExecutionQueueImpl::onReady`'s `continuationIsSelf` branch (`:499-512`) and the wrapper's state, cancel and exception forward to the new hop (`TaskBase.h`, `ForwarderTaskBaseT`). |
| `3688c18` - the TLS instantiation and `httpclient5` | **The type is compiled and run over the TLS policy, and the ALPN-selected GET establishes what it claims; the join case runs the h2-only routing but does not discriminate the narrowing (finding 7).** | `ClientSessionImplT< TcpSslSocketAsyncStrandedBase >` is named at `TestClientSessionTls.h:47` and instantiated at `:118`; the GET asserts `negotiatedAlpn() == "h2"` (`:351`). The join's peer prefers `[ "h2", "http/1.1" ]` (`:389-392`), which selects `h2` from a full offer too, so `connectionsCreated == 2` (`:445`) pins the key split and nothing pins the offer. |
| `1265104` - the merge, design and plan text | **The design amendments are accurate about the code except two sentences: "both halves use it" (5.4) is true of the rule and false of the calls (finding 11), and the 5.6 sentence which says `createRequestTask` refuses a cross-scheme URL "rather than connecting cleartext to a TLS port" is true of `createRequestTask` and not of the session (finding 1). The plan's "as landed" is as the code is, with the size paragraph's "measured" overstated (finding 8).** | Diffs read whole. |
| `6d91d0c` - the manifest | **As claimed in substance: 17 cases added, none removed, no duplicate names, 41 modules. The message's "1022 cases" is a miscount: the `cases` list holds 1024 at tip (1007 at `582d9c5` plus 17), which is the figure the brief carries.** | Counted from the JSON at the four commits; the added set is exactly the 15 `ClientSession_` and 2 `ClientSessionTls_` names; both modules' `notes_cases` match their `notes.txt`. |

The placement and idiom checks of the plan's verification protocol pass: no `BL_DEVENV_VERSION`
test in either new header (grepped); both modules carry `devenv7_only` and the append-convention
note; every new case has a `notes.txt` recipe (15 and 2, name for name); the four
`BL_DEFINE_STATIC_CONST_STRING` sets are per template as the house idiom wants; the QI tables are
the shapes the L4 and L5 records verified compile for a double-`om::Object` base, and here they are
also run. `ClientSession.h` includes `ConnectionPool.h`, which `PreCompiled.h` keeps out for size;
that is consistent, since the session is the consumer the pool exists for.

**No gate is recorded in the range**, as in L5: no runlog change, no gate commit, no message
naming clang release or gcc release. The brief's "15/15 and 2/2 under both release toolchains" is
the orchestrator's validation outside the tree; the lane's own messages claim clang debug only.
Both lane messages also say "no ThreadSanitizer report" for runs which the plan's own "as landed"
says were not ThreadSanitizer builds; the phrase is vacuous and reads as evidence (nit 13(f)).

## Findings

By severity: 1 (High), 2, 3, 4, 5, 6 (Medium), 7 (Medium, evidence), 8 (Low, evidence), 9, 10,
11, 12 (Low), then the nits in 13, the two reversible choices in 14, and the notes in 15.

### 1. High - a followed `http` to `https` redirect on a cleartext session connects cleartext to the TLS port, and the jar then sends the target's `Secure` cookies on that socket

**The invariant and where it is enforced.** Design 5.6, as S6.1 amended it: *"one session speaks
one scheme: `createRequestTask` refuses a URL whose scheme is not the transport's rather than
connecting cleartext to a TLS port."* The refusal is at `ClientSession.h:1604-1614`, and it is the
only one. A session has a second entry point for a URL: the redirect target. `chkPrepareNextHop`
(`:1173-1226`) takes `decision.target` from `RedirectPolicy::evaluate` (`RedirectPolicy.h:379-521`),
which refuses a scheme that is not `http` or `https` (`:452-461`) and refuses an `https` to `http`
downgrade unless allowed (`:469-476`), and follows everything else - including `http` to `https`,
which is the most common redirect on the web. The session then sets `m_next.url( decision.target )`
(`:1206`) and starts a hop whose key `ConnectionKey::fromUri` builds with `scheme = "https"` and
`port = 443`, and whose connection the session's factory builds over the session's own `STREAM` -
the cleartext policy - with the session's own `connectionConfig`. Nothing between `:1206` and the
socket compares the target's scheme with `transportScheme()`.

**What happens on the wire.** TCP to port 443 succeeds (the TLS server accepts the connection);
`continueAfterConnected` takes the protocol from `cleartextProtocol` (`ClientConnectionTaskBase.h:606-609`),
which is `Http11` for a default `ClientSessionConfig` and `Http2` for the prior-knowledge
session; on `Http11` the h2 task hands the stream to the h1 driver, bounces the rider, the session
retries onto the driver, and the driver writes the serialised request head - in the clear. The head
carries the `Cookie` field `prepareRequest` computed for the **`https` URL** (`:972-973`), and
`CookieJar::cookieHeaderValue` includes `Secure` cookies for an `https` request URI, which the jar
cannot know is being sent over plaintext. Design 5.6 records that an `http` response *can* set a
`Secure` cookie in this jar (the 6265bis rule is not implemented), so the jar can hold one. The
server answers the plaintext with a TLS alert and closes; the request fails as `Failed`, not
retryable, with an obscure parse or reset error. On the `Http2` prior-knowledge session the same
bytes go out as preface plus HPACK-compressed `HEADERS`, which is compressed and not encrypted.

**Severity.** Two defects at once: the functional one - every `http` to `https` redirect on a
cleartext session with redirects enabled fails, after connecting to the right host, with an error
which names nothing about schemes - and the invariant one, which is what `Secure` exists for. The
practical exposure is bounded (the plaintext goes to the intended host's TLS port, which rejects
it), and it needs redirects on, which is off by default; it is High because it is the session's own
stated guarantee, broken at the entry point the design forgot, with the credential the guarantee
protects. No case covers it: `ClientSession_CrossOriginRedirectDropsCredentialsTests` is two `http`
peers.

**Fix.** In `chkPrepareNextHop`, before `:1198`: if `decision.target.scheme() !=` the transport's
scheme, do not follow - either report the 3xx as when the policy is disabled (the caller then sees
the target in the response, which is what the existing client does) or fail the chain with the same
`NotSupportedException` `createRequestTask` throws, naming the target. The plan carries no
transport scheme today; one string in `SessionRequestPlan` (or `templateKey.scheme`, which
`keyFor` currently leaves to the URL) is the whole change. One case: a cleartext peer answering
`302` with `Location: https://127.0.0.1:<port>/`, asserting the chain stops with the 3xx (or the
refusal) and `connectionsCreated == 1`. Design 5.6's sentence should say "at both entry points".

### 2. Medium - `retryIdempotentOnConnectionLoss` is unreachable through the session: the request task never reports `ConnectionUnusable` for a connection that was lost

`chkPrepareRetry` derives `context.isConnectionLost` from `RequestOutcome::ConnectionUnusable ==
m_hop -> outcome()` (`:1039-1040`), which is what `RetryContext`'s own comment says the field means
(`ConnectionPool.h:381-386`). The request task sets `ConnectionUnusable` in exactly one place: the
refused-submit branch for a refusal the connection caused (`HttpClientRequestTask.h:801`; grepped -
`:358`, `:742`, `:787` and `:1081` set `Failed` or `Completed`). That branch has already set
`m_isRetryable = true` (`:783`), so the predicate returns from its `isRetryable` limb before it
consults `isConnectionLost`. A connection which dies **mid-stream** - the case the knob exists for,
design 5.4's "retrying idempotent methods after connection loss is a separate knob" - reaches
`applyClosed`, whose outcome is `Failed` for any error code (`:1081`) and whose `isRetryable` is the
driver's verdict, which for a stream whose `HEADERS` were written is false
(`Http2ConnectionTask.h:1149`, `! isHeadersProduced`). So through the session `isConnectionLost` is
never true on the path where it would matter, and a caller who turns the knob on gets nothing. The
predicate's unit case (`TestConnectionPool.h:1262-1287`) exercises the limb with a hand-built
context, so the rule is right and the feed is dead.

This is the standard the lane set for itself in its own class comment - "a knob which reaches
nothing is the same defect one layer up" - and the pool does not feed the limb either (its queued
half never dispatched, so `isConnectionLost` cannot apply to it). The defect is in S5.1's
`applyClosed`, which derives the outcome from the error code alone: `RequestOutcome::ConnectionUnusable`
is documented as "the connection cannot be used again" (`ClientConnection.h:126-130`), which a
connection that just reset is. The pool does not need the distinction (it retires a dead connection
by observing `Closed`); the session does.

**Fix, in S5.1, additive:** in `applyClosed`, when the closing error is connection-level rather
than a stream error - the driver knows, since `closeAllStreamsUnwrittenRetryable` and `onPeerClosed`
are the connection-level paths and `RST_STREAM` is the stream-level one, but the event carries only
an error code - set `ConnectionUnusable`. The cheapest honest version reads the connection's own
state at that moment: `m_connection -> state()` is `Closed` or `Draining` on every connection-level
route and `Ready` on a stream reset, and the request task still holds `m_connection` at `:1079`.
One case in `utf_baselib_httpclient`: a probe connection which closes the stream with
`connection_reset` and reads `Closed`, asserting `outcome() == ConnectionUnusable`; and one through
the session with the knob on: a peer which drops the connection after the request's `HEADERS`,
asserting a `GET` is retried and a `POST` is not.

### 3. Medium - the request total timeout is per attempt and per hop, so a session request has no deadline

Design 5.7's table: *"Request total, including pool wait | 30 min | request task."* Through the
session that row is not true. Each hop is a fresh `HttpClientRequestTaskImpl` (`:993-999`) whose
`applyStart` arms its own total timer from `effectiveTimeout( m_request.totalTimeout(),
m_config.totalTimeout )` (`HttpClientRequestTask.h:602-607`), and the session passes each hop the
same `m_plan.requestConfig` (`:997`) and the caller's request with its own `totalTimeout()` field
untouched (`ClientTypes.h:476-483`). So every retry and every followed redirect restarts the clock:
four attempts of a hop are up to two hours, and twenty hops (`DEFAULT_MAX_HOPS`,
`RedirectPolicy.h:234`) of four attempts each are forty. A peer which answers each request slowly
enough to stay under the per-hop timer and then redirects can hold a caller for as long as it likes.

**Fix, in the session:** compute a chain deadline once in the constructor from the caller's
`totalTimeout()` or the session default, and in `startHop()` set `m_next.totalTimeout( deadline -
now )` before `prepareRequest`, failing the chain with the request task's own timeout exception when
the remainder is not positive. The per-request override L5 nit 8(a) recorded as unpinned is exactly
the field this needs, so one case pins both: a hop budget that expires across a redirect.

### 4. Medium - the rider is dispatched onto a placeholder which can never speak HTTP/2, the control case shows the consequence and reads it as a virtue, and the bounce hides every establishment failure behind `connection_aborted`

**The mechanism, verified.** The pool dispatches a replayable request onto a `Connecting`
placeholder with no slot out (`ConnectionPool.h:1400-1424`); on a cleartext session configured
`Http11` - which is `ClientSessionConfig()`'s default, and the configuration
`ClientSession_AgainstTheLibraryHttpServerTests` uses - that placeholder is an h2 task whose
`continueAfterConnected` takes `Http11` from the configuration, falls back
(`Http2ConnectionTask.h:2297-2308`, `ClientConnectionTaskBase.h:577-595`), completes, and
`closeSubmissions()` answers the queued `Submit` with `connection_aborted`, retryable
(`:2161-2190`). The session's retry then lands on the adopted driver. Four dispatches for two
requests (`TestClientSession.h:1759`) is that, and the retry is what makes it work; the fix is
right. What the case also shows, and its control (`:1775-1830`) pins as the expected behaviour, is
that on a session which **knows at construction it cannot produce HTTP/2** (`mayProduceHttp2()`,
`:1700-1708`, is what `canCarryBodySource()` already reads) every first request on every
connection spends one attempt of its budget on a dispatch which cannot succeed - and with the
library's own `Connection: close` server that is every request. So `maxRetriesPerRequest = 0`
makes a cleartext HTTP/1.1 session unable to make any request at all, which is what the control
demonstrates: a caller who sets zero because a `POST` must not be retried has switched HTTP/1.1
off. Design 4.6's "retries of one request: 3" is a budget for network faults, and here the pool's
own dispatch rule consumes it.

**The masked cause.** `closeSubmissions()` uses `connection_aborted` unconditionally (`:2185`),
while `onTaskStoppedNothrow` computes a code from `eptrIn` for the open streams one line later
(`:2430-2434`) and never for the queued submissions; `answerOnClosed` turns the code into
`HttpException << errinfo_error_code( connection_aborted )`, "The HTTP request failed"
(`HttpClientRequestTask.h:1110-1125`). So an `ECONNREFUSED`, a resolver failure, an expired
establishment bound and a **TLS certificate verification failure** all reach the caller of a
session's first request as "connection aborted", after `maxRetriesPerRequest + 1` full
establishments - four handshakes against a bad certificate - with the cause available in the h2
task's `exception()` and never chained. The pool's queued half keeps the establishment error as
`lastError` for its waiters (`:1567-1569`, `:1892-1894`); the dispatched half, which is every
first request, does not. The second request to a dead origin therefore gets a better error than the
first.

**Fix.** Three parts, the first two additive. (a) `ConnectionPoolPolicy` gains a flag - "ride the
preface", default true - which the session sets false when `! mayProduceHttp2()`, so a transport
that cannot speak h2 never dispatches onto a placeholder; the rider then costs nothing on cleartext
HTTP/1.1 and `maxRetriesPerRequest = 0` means what it says. (b) `closeSubmissions( errorCode )`
takes the code `onTaskStoppedNothrow` already computes, and the request task, when the connection
it was bounced from is a `tasks::Task` which `isFailed()`, attaches that task's `exception()` to
its own as the nested cause - the request task holds `m_connection` at `applyClosed` and
`om::tryQI< tasks::Task >` is how the pool already reads it. (c) The control case's comment should
say what it shows. One case: a session against a listening socket that never accepts (or a closed
port), asserting the failure names the establishment error and `establishmentRetries`; that is also
the case L5 section 9 asked S6.1 for and finding 7 notes is missing.

### 5. Medium - `idleTimeout` reaches the h2 driver only; an idle HTTP/1.1 connection through the session has no lifetime

The factory writes `policy.idleTimeout` into `Http2ConnectionConfig::idleTimeout`
(`ClientSession.h:1547`), whose default is `neg_infin` (`Http2ConnectionTask.h`, the config's
constructor), and the case pins the driver closing itself at 300 ms (`TestClientSession.h:799-844`).
The h1 driver is built by `makeDriverFactory` with `Http1ResponseLimits` and nothing else
(`:1476-1494`), has no idle timer, and says in its own header that "connection pooling, the idle
lifetime and the retry policy: they are the pool's (design 5.4, S5.2)"
(`Http1ConnectionTask.h:98-104`). The pool has no reaper - L5 finding 3 recorded that no code set
the lifetime from the pool, and the fix round wired the driver's timer, not a pool tick. So a
keep-alive HTTP/1.1 connection acquired through the session stays open until the peer closes it
(which the driver's armed idle read notices) or the session is disposed, and design 5.7's
"Connection idle | 5 min | pool" holds for one of the two protocols. Bounded in practice by the
peer's own keep-alive timeout and by `maxConnectionsPerKeyHttp11`; unbounded by anything of ours.
The obligation "idleTimeout wired through the session's connection factory" is pinned as behaviour
for h2 and silently unmet for h1.

**Fix.** The design names the pool as the owner, and the pool is the one place which sees both
protocols: stamp the entry at the release which takes `slotsInUse` to zero, and on the maintenance
tick retire an entry idle past `policy.idleTimeout` - which also needs the tick to run while an
idle `Ready` entry exists, today's `hasWorkToWatch()` does not. The alternative is an idle timer in
the h1 driver, its own change-set. One case: the idle case above run against the library's
`HttpServer` with keep-alive, asserting the connection count drops.

### 6. Medium - the inter-hop work, including the caller's `rewind()` and the registered decoder, runs under the execution queue's lock and the wrapper's lock

`ExecutionQueueImpl::onReady` holds `m_lock` (`ExecutionQueueImpl.h:238`, `:480`) across
`task -> continuationTask()` (`:499`), and `SessionRequestTaskT::continuationTask` takes the
wrapper's `m_lock` (`ClientSession.h:1251`) and then runs `absorbResponse` - `storeCookies` and
`decodeBody`, the latter a registered `ContentDecoder` over a body of up to `DEFAULT_MAX_OUTPUT_BYTES`
(64 MB, `ContentDecoder.h:137`) - `chkPrepareRetry` and `chkPrepareNextHop`, each of which may
call the caller's `BodySource::rewind()` (`:1054`, `:1220`), and `startHop`, which creates the next
request task. While that runs, every `push_back`, `wait` and `pop` on the queue blocks, and a
`requestCancel()` on the wrapper blocks on its lock; a `rewind()` which waits on anything scheduled
on the same queue deadlocks. A throw from any of it is caught by the queue and becomes the task's
failure (`:519-528`), which is the right outcome and the reason a throwing decoder is a failed
request rather than an abort - that part is fine.

`RetryableWrapperTaskT` runs its factory callback in the same place, so this is the house idiom's
property and the session inherits it; the session is the first continuation in the tree heavy
enough for it to matter, and the first to run caller code there. The class comment does not say so,
and `ClientTypes.h`'s `BodySource` contract does not constrain `rewind()`.

**Fix.** Keep `continuationTask()` to the decision and move the work: the decode belongs in the hop
task's deferred phase (it already has one, and a `HttpClientRequestConfig` flag or a decoder
handed to the hop would do it), and the redirect evaluation and cookie store are cheap enough to
stay. At minimum, document at `BodySource::rewind()` and `ContentDecoder` that they run under the
scheduling lock of the queue the request was pushed to and must not block or re-enter it.

### 7. Medium, evidence - what the cases establish versus what is claimed

**The preface rider "holds in composition".** `ClientSession_GetOverHttp2Tests` asserts one
dispatch, one release, one connection, no failure (`:513-518`). A pool which waited for `Ready` and
dispatched at the next tick would produce the same four numbers, so the case does not establish
that the request rode the preface; that is inferred from the pool's rule, which `h2client4` pins
against a stub. What establishes that the rider *was dispatched onto the placeholder* is the
fallback case (four dispatches, `:1759`), where the outcome is a bounce. No case observes `HEADERS`
in the opening write against a real driver - the peer's recorder could (its first read after the
preface), and does not.

**"Dispatch against a driver which really does publish `Ready` before `SETTINGS`".** No case pushes
two requests at once through a session. `ClientSession_ConnectionIsReusedAcrossRequestsTests` runs
two sequential requests, and the second is dispatched after the first's `Completed` release has
called `markPeerLimitKnown` (`ConnectionPool.h:2205-2222`), so the one-slot rule, the settle window,
the band inference and the burst bound - the whole of the L5 fix round's pool logic - are composed
with a real driver only in the regime where none of them has to decide anything. The case does
establish reuse (streams 1 and 3 at the peer). The composition the L5 record said "nobody has run"
is now run, in the sequential regime only.

**The GOAWAY race** (`:666-720`) is run as a race and asserts the pool's verdict - `connectionsCreated
== 2`, `connectionsRetired >= 1` - which is what must hold either way; the `>= 1` and the comment
explaining it are honest. It does not observe which way the race went, and does not need to.

**"`ConnectionUnusable` was never reached from a healthy connection"** is established indirectly
for the paths run: `connectionsCreated == 1` across two requests (reuse) and `== 2` across two
fallback bounces (`:1745`) are only possible if the bounce's outcome was `Failed`. There is no
stat for the outcome itself.

**The obligations.** The reserve is pinned as behaviour and the mechanism is the one L5 asked for:
`chkPublishDraining` at both answers of `applySubmit`, reached because the factory wrote the
reserve (`:1546`). The idle timeout is pinned as behaviour for h2 (finding 5 for h1). The
`BodySource` rule's half one is pinned; half two is pinned as two pure functions
(`keyFor`, `narrowToHttp2`) and the TLS join runs them together, but **the join does not
discriminate the narrowing**: with the peer preferring `[ "h2", "http/1.1" ]`
(`TestClientSessionTls.h:389-392`), a full offer of both also selects `h2`, so deleting
`narrowToHttp2` leaves the case green. What the join pins is the key split. The discriminating case
is one request: a streaming `PUT` alone against a peer preferring `[ "http/1.1", "h2" ]` - with the
narrowing the peer must select `h2`; without it the peer selects `http/1.1`, the h2 task falls back,
and the h1 driver refuses the source. Cheap and decisive, and it is also the only shape which can
show the narrowing on the wire rather than in a config struct.

**What no case runs at all.** An establishment failure of any kind - every case has a live peer.
That is the path L5 section 9 asked S6.1 to pin (`refreshEntry`'s reliance on a cancelled real
connection task's `exception()`), the path finding 4 says masks its cause, and the first thing a
real deployment meets. A concurrent burst: N requests pushed at once against a peer advertising
`SETTINGS_MAX_CONCURRENT_STREAMS` of two, asserting no `REFUSED_STREAM` at the peer and one
connection - the case the L5 pool round was for.

### 8. Low, evidence - the split arithmetic is a prediction from figures measured in other translation units on another toolchain, and with the lane's own figures it does not decide

The module header (`UtfBaselibHttpClient4Main.cpp:39-49`) argues: floor ~21, session with both
drivers "roughly 15", peer 8.6 (from `h2client2`'s header), `HttpServer` 5.8 (from `httpclient3`'s),
hence halves of ~45 and ~42. Three things about that.

The parts sum to 50.4 against a whole of 48.0, which is expected - marginal costs are not additive,
`test-instantiation-weight-deferral.md` says so - and which is also why `src/utests/AGENTS.md`
says *"do not predict a grouping from isolated per-header measurements - they were off by 40x once.
Build the grouping and measure it, or use leave-one-out."* The merge message's "splitting was
measured rather than assumed" is true of the two module sizes and not of the split, which was
computed. The "roughly 15" is the one figure never measured anywhere, and it decides the question:
from the lane's own numbers, `httpclient4` at 48.0 less the floor, the peer and `HttpServer` puts the
session-plus-drivers at about 12.6, which makes the `HttpServer` half of a split about 39 - under
target - and the peer half about 42. The floor figure is x86 MSVC debug (the deferral record's
21.4); the module figures are a64 clang debug; the arithmetic mixes toolchains.

The conclusion may well be right - two modules at about 40 each, 30 MB more in total, for one at 48
under a 75 ceiling is a defensible trade - and the recorded reason satisfies the policy's letter.
But a leave-one-out build (`httpclient4` without the two `HttpServer` cases) is one command and is
what the rule asks for. Nobody has measured either module on the enforcing platform, win-x86
debug, where every other over-target module in the tree has been argued; 48 and 46 clang are the
proxy the project has been using and should be named as one.

### 9. Low - a cancel which lands between hops completes the chain successfully with the intermediate 3xx

`continuationTask` checks `m_cancelRequested` after `absorbResponse()` and returns null
(`:1255-1258`) without setting an exception. If the hop it landed after succeeded - a `302` the
chain was about to follow - the wrapper forwards `exception()` and `isFailed()` to that hop
(`ForwarderTaskBaseT`), so the caller who cancelled sees `isFailed() == false`, `status() == 302`
and `redirectHops()` one short. `RetryableWrapperTaskT`, which the class cites as its model, sets
`operation_aborted` marked expected in exactly this spot (`TaskBase.h:2170-2200`). Mirror it.

### 10. Low - the two halves of the retry each count to the limit, so a request's budget is `(N + 1)^2` establishments per hop, not `N + 1`

The pool's half counts on the waiter (`ConnectionPool.h:746`, `:1567`, `:1591`), and a waiter is
per `acquire`; the session's half counts per hop (`:910`, `:1013`) and every session retry is a
fresh `acquire` with a fresh waiter. So a hop whose every establishment fails three times before
the fourth succeeds, and whose every dispatch is then bounced retryable, spends 16 establishments
before the session gives up - bounded, and design 5.4's "bounded by the retry limit (4.6)" reads
as one budget of three. With the 120 s establishment bound that is 32 minutes, which only finding
3's missing chain deadline would have bounded. Record it as two budgets, or have the session stop
retrying a hop whose failure was an establishment failure the pool already retried - which needs
finding 4(b)'s cause to be visible.

### 11. Low - the pool never calls `chkRequestMayBeReplayed`; "both halves use it" is true of the rule and false of the code

Grepped at tip: the predicate is defined at `ConnectionPool.h:429`, called by the session at
`ClientSession.h:1043` and by the `h2client4` unit case, and by nothing in the pool. The pool's
half is inline - `waiter.request.isReplayable()` for the rider (`:1421`) and `attempts >
maxRetriesPerRequest` for the replay (`:1591`) - and consistent with the predicate on the two limbs
a never-dispatched request needs. The design's *"the rule is one predicate ... and both halves use
it"* (5.4) and the commit's *"the same `chkRequestMayBeReplayed` predicate the pool applies to its
own"* should say the pool applies the same rule inline. Not a defect; the brief asked whether the
predicate is genuinely shared rather than duplicated, and the answer is: defined once, called once,
and the pool's half is a consistent second implementation of two of its three limbs.

### 12. Low - `absorbResponse` decodes a failed hop's partial body, and a decoder which throws there replaces the hop's exception with its own

`absorbResponse` runs before the exception check (`:1253-1260`) and returns early only on
`status() == 0` (`:1068-1076`). A hop which failed after its headers - a body over the cap, a reset
mid-body - has a status, so `storeCookies` (fine) and `decodeBody` both run; a registered decoder
over a truncated coded body may throw, and the queue's `task -> exception( continuationException )`
(`ExecutionQueueImpl.h:519-528`) forwards to the hop and overwrites the network error with the
decoder's. Skip `decodeBody` when `m_hop -> exception()` is set.

### 13. Nits

- **(a)** `mergeCookieValues` dedupes names with `contains`, which is `equalsIgnoreCase`
  (`:487`, `:694-708`); RFC 6265 cookie names are case-sensitive tokens, so a caller's `sid`
  suppresses the jar's `SID`. Compare exactly.
- **(b)** `applyHttp1Casing` (`:627-652`) has no caller (grepped) and duplicates
  `Http1Codec::renderName`'s lookup (`Http1Codec.h:1230-1235`); the h1 driver serialises without any
  profile (`Http1ConnectionTask.h:478-482`), so no casing reaches the wire today by either path.
  That is S7.3's to wire; the session should not ship a second unwired copy. Delete it or route it.
- **(c)** `profile( HeaderProfile )` (`:1678-1681`) writes `m_config.headerProfile` and
  `redirectPolicy()` hands out a mutable reference, both read by `createRequestTask` with no
  synchronisation; the plan-snapshot comment (`:226-231`) implies reconfiguration during flight is
  expected. Say "configure from one thread" or lock.
- **(d)** `m_isDisposed` is a plain bool (`:1409`); two concurrent `dispose()` calls both reach the
  pool's, which is idempotent, so benign.
- **(e)** `narrowToHttp2`'s `cleartextProtocol` half (`:1515`) can never run: on a cleartext session
  `isProtocolNegotiated` is `mayProduceHttp2() && mayProduceHttp11()`, which is `( == Http2 ) && (
  != Http2 )`, so no cleartext key ever carries the marker. Harmless; the comment claims a use.
- **(f)** "No ThreadSanitizer report" in both lane messages, for builds the plan says were not
  ThreadSanitizer builds. Say "not run".
- **(g)** The manifest message says 1022; the list holds 1024.
- **(h)** A streaming upload to a TLS origin without `h2`: the h2-only key's connection offers `h2`
  alone, the peer selects nothing, the task fails, the rider is bounced and retried `N` times, and
  the caller gets `connection_aborted` rather than "this origin cannot carry a streamed body". Same
  root as finding 4(b).
- **(i)** A session with an empty `http2ProfileId` produces the key `"#h2-only"`; fine, and worth
  a line where the marker is defined.
- **(j)** The `ConnectionUnusable` refusal path in S5.1 that the L5 second pass left on the ledger
  (`Failed` for an h1 refusal of a `BodySource`) was closed by `2afe5da` in the L5 gate range
  (`HttpClientRequestTask.h:785-798`), before this range; through the session it is unreachable in
  any case, since no `BodySource` request is dispatched to a key which can produce h1.

### 14. The two reversible choices

**The h2-only marker appended to `ConnectionKey::http2ProfileId`.** Right today, wrong for the next
consumer. Nothing reads the field but the key's ordering and equality (`ClientConnection.h:434-450`;
grepped), so appending a suffix to a string in a frozen type costs nothing now and avoids opening
S2.6. S7.3 ("apply profile across layers") will read `key.http2ProfileId` in the connection factory
to choose the HTTP/2 profile per connection - design 5.4 puts the profile in the key for exactly
that - and `"chrome#h2-only"` will not resolve. Provide `SessionHeaders::http2ProfileIdOf( key )`
now, which strips the suffix, and record in the S7.3 work order that the factory must use it; or
add the field when the frozen type is next opened. The `#` separator is safe for any profile id
which is an identifier.

**A caller's `BodySink` suppresses redirect following.** Consistent with "redirects off" and
documented at the class (`:859-865`) and in 5.6, and not at `ClientSession::createRequestTask`
(`:159-166`), which is where a caller looks. The argument is weaker than stated: `BodySink` carries
no status, but the hop task sees the final status in `onHeaders` before the first `onData`, so it
could buffer a 3xx body and stream everything else - a small S5.1 addition (buffer when the status
is 3xx and a sink is installed, or a `HttpClientRequestConfig` flag) which would let sinks and
redirects coexist. The choice is acceptable as a documented limitation; the better design is one
flag away and should be recorded as the reversal path rather than as impossible.

### 15. The three L2 items

- **One `Cookie` field, merged**: settled, and pinned as a count (`1307`, `1326`) and as a value
  (`sid=fromcaller; theme=dark`, `:1327`) across a same-origin redirect, which is where the two
  sources meet. Nit 13(a) is the one deviation.
- **`Proxy-Authorization` dropped by the session**: settled by construction (`isIgnoredCallerHeader`,
  `:758-763`, applied to both the caller's headers and the profile's defaults) and pinned at the
  peer (`:1615-1627`). Silent, and documented as such; the design's "no-op by construction" holds.
- **`isHttpApi`**: restated, correctly - there was nothing to settle; the jar's parameter stays
  `true` from an HTTP client whatever the session's type. What the type parameterisation settles is
  the *Secure-cookie* surface the plan folded into item (3): one session, one jar, one scheme, so an
  `http` response cannot overwrite a `Secure` cookie an `https` session would send. Two things
  reopen it: finding 1, today; and L8's "process-default session" (design 5.8), which must be one
  session per scheme - and then either shares a jar across the two, reopening the surface, or does
  not, and non-`Secure` cookies set over `https` are not sent over `http` to the same host, which
  browsers do send. Design 5.6 should say which before S8.1.

## What is not safe to leave as it stands

- **Finding 1.** One check in `chkPrepareNextHop`. Until it lands, redirects should not be enabled
  on a cleartext session, and design 5.6's sentence is not true.
- **Finding 3.** A session request has no deadline. One field per hop.
- **Finding 2.** A documented policy knob does nothing through the only consumer it has.
- **Finding 4(b).** Every first request to a dead or misconfigured origin reports "connection
  aborted" after four establishments, and a certificate failure is among them. Not a correctness
  defect; the first thing an operator will ask about.
- **Finding 5.** An idle HTTP/1.1 connection is nobody's to close.

Findings 6, 7 and 8 are evidence and shape, not correctness; they decide how much the green runs
prove, which for the concurrent regime and the establishment-failure path is nothing yet.

## What an L7 would trip over first

1. `key.http2ProfileId` with the marker on it, the moment S7.3's factory selects a profile per
   connection (finding 14). The factory is static and captures values (`:1521-1534`); S7.3 needs it
   to capture a profile registry as well, and `tlsProfileId` (`:106`) has the same shape - a key
   field the factory cannot yet act on.
2. Two unwired header-casing paths (nit 13(b)); S7.3 must pick one and delete the other.
3. `accept-encoding` omitted when no decoder is registered (`:385-387`, pinned at `:1394-1422`) is
   a fingerprint deviation under impersonation which S7.4's fidelity report has to report; the
   session records nothing about it today.
4. The unsynchronised `profile()` setter (nit 13(c)) if profiles are applied at run time.
5. For S8.1: the process-default session is two sessions (finding 15's last item), and the facade's
   `isSecureMode` maps to which of the two.

## What was verified versus inferred

Verified by reading the code against the design: every row of the verdict table; the whole of
`ClientSession.h`, both test headers, both `Main.cpp` files, both `notes.txt`; the retry path from
the driver's `closeSubmissions` through the request task's `applyClosed` and `answerOnClosed`, the
pool's `releaseStream` `Failed` branch and `refreshEntry`'s adoption of the driver, to the session's
`chkPrepareRetry` and `startHop`, and the queue's `continuationIsSelf` re-entry; the single place
`ConnectionUnusable` is set and the four places the outcome is set otherwise; the redirect policy's
scheme handling; the factory's two policy writes and the h2 config's `neg_infin` default; the h1
driver's factory call, its serialiser call and its ownership note; `http1CaseMap`'s consumers; the
per-hop arming of the total timer; the queue lock around `continuationTask()`; the wrapper's lock
and the direct assignment which avoids the double take. Verified by grep: `chkRequestMayBeReplayed`'s
callers; `http2ProfileId`'s readers; `applyHttp1Casing`'s callers (none); `ConnectionUnusable`'s
assignments; `BL_DEVENV_VERSION` absent from the new headers; every recipe resolves. Verified by
counting: the manifest's 1024 and the 17-name delta at each of the four commits. Verified by diffing:
the design and plan hunks, read whole.

Inferred or recalled: that a TLS server closes on plaintext with an alert (finding 1's wire
description - the write happens either way; what the server does with it is recalled); RFC 6265's
case-sensitive cookie names, RFC 7301's "select only from what was offered", RFC 9113's server
`SETTINGS` first, RFC 9110's hop-by-hop `Proxy-Authorization`, all from memory; that a peer
preferring `http/1.1` would make the join case discriminate (from the base's `negotiatedProtocol`
and the driver's fallback, not from a run); that MSVC x86 debug objects would differ from clang a64
(a claim about toolchains, from the deferral record's figures); that the session-plus-drivers
figure is about 12.6 (arithmetic on the lane's numbers, which finding 8 says is not a measurement
either); that 4 x 120 s and 16 x 120 s are the bounds (from the constants, not measured).

## What was not checked

- **No build and no test run.** The 15/15, 2/2, 106 and 19 assertion counts, the sizes, the
  "-Werror clean" claims and both release toolchains are as reported.
- **The L5 gate range** `4399b0c..582d9c5` (`79ba6f0`, `2afe5da`, `7bcce7a`, `df90fe8`, `70d0b03`,
  `582d9c5`) landed after my L5 second pass and was read only where S6.1 leans on it
  (`isRequestUnsuitableForConnection`, `chkPublishDraining`, `markPeerLimitKnown`); it was not
  reviewed as a change-set.
- **`Http2TestServer`'s TLS half** and `CryptoBase::setAlpnServerPreference` were relied on as L4
  left them.
- **`net::Uri::resolve` and `origin()`** on the redirect targets; `RedirectPolicy::evaluate`'s
  header-list overload; `CookieJar::cookiesForRequest`'s `Secure` filtering, relied on from its
  signature and design 5.6 rather than re-read.
- **`ContentDecoderRegistry::createStream`'s** cap behaviour under a throw, and `DecoderStream`'s
  `finish()`.
- **`HttpClientRequestConfig::isArmed`** and what `neg_infin` resolves to at the hop, relied on
  from L5.
- **Whether `eq -> wait( task )` on a wrapper whose wrapped task is swapped** has any window in
  which the queue's `wait` sees the old task complete - the cases are green, which says it does not
  on this path; not traced.
- **Windows, the 1.1.1w flavor and a64 under ThreadSanitizer**, as before; the TSan run the lane
  records as owed is the first thing able to report L5 finding 7's two unsynchronised reads, and
  `resolveDriver` (`ConnectionPool.h`, reads `attempt.driver()` under the pool lock whenever
  `driverConnection` is unset, with no state test before it) is the read it would report first.

## Second pass: the fix round `6d91d0c..b43ec06`, tip `b43ec06` (2026-09-20)

**Verdict: the five "not safe to leave" items are carried out and each is pinned by a case which
was run red against the unfixed code; of the three departures from my prescriptions, two were
right and my prescriptions were wrong - one of them would have destroyed the ALPN fallback - and
the third is right on its own terms; the lock argument behind the chained cause holds; the idle
split is sound and `initiateClose()` runs where the h1 driver says it does. One thing is new and
it is in the fix for finding 2: the h2 driver answers its sinks BEFORE it publishes `Closed` on two
routes, so the gate's state read is a race there, and `retryIdempotentOnConnectionLoss` is
nondeterministic on the write-error path rather than dead (new finding 16, Medium, two one-line
reorders). Finding 8 is withdrawn: `httpclient6` measured what the arithmetic could not. Nothing
here blocks a maintainer handover; two things block calling the client usable under load, and they
are the same two the first pass named.** Read whole: every production diff in the range
(`ClientSession.h` 176/-, `HttpClientRequestTask.h` 130/-, `Http1ConnectionTask.h` 219/-,
`ConnectionPool.h` 15/-, `Http2ConnectionTask.h` 9/-), every test diff, the new module, the design
and plan diffs, all nine commit messages; and, around them, `TaskBase::notifyReadyImpl`,
`exception()`, `requestCancel`/`requestCancelInternal` and the handler macros;
`MultiOperationTaskT`'s contract, `onOperationCompleted`, `applyDecision` and `initiateClose`;
the h2 driver's `onRead`, `onWrite`, `onPeerClosed`, `isPeerClosed`, `onPingDeadline`,
`onDrainDeadline`, `onSettingsDeadline`, `closeStream`, `closeAllStreamsUnwrittenRetryable`,
`onTaskStoppedNothrow`, `chkArmIdleTimer` and `cancelTask`; the h1 driver's `finishStream`,
`closeConnection`, `cancelTask`, `postToStreamExecutor` and request-start path;
`HttpClientRequestConfig`'s defaults and the pool's waiter deadline. Nothing was built and
nothing was run; the module results (59/59, 6/6, 17/17, 2/2, 1/1, 13/13 under both release
toolchains) are as reported. The manifest holds 1030 (counted): the six new names, none removed,
one body edited (`ClientSessionTls_StreamingUploadTakesAnHttp2OnlyConnectionTests`, the flake fix),
42 modules.

### The five fixes, walked

**Finding 1 (High) - `69e2f5c`.** `SessionRequestPlan::transportScheme` (`ClientSession.h:275`),
set from `transportScheme()` in `createRequestTask` (`:1799`), compared in `chkPrepareNextHop`
against `decision.target.scheme()` after `shouldFollow()` and before any state moves
(`:1315-1336`); on a mismatch the hop is not taken and the chain ends with the 3xx. `net::Uri`
lower-cases the scheme at parse (`Uri.h:56`, `:882`) and `resolve` produces a parsed target, so
the comparison is exact on both sides. The case (`TestClientSession.h:1052-1168`) is the right
shape: a second peer whose recorder must stay empty, a `Secure` cookie set on the 3xx, the jar
asked afterwards what it would have sent to the `https` target (`sid=secret`) and to the `http`
one (nothing), `connectionsCreated == 1`; the commit records the unfixed run at 200 - the cleartext
session really fetched the `https` URL. **Closed.**

**Finding 3 - `69e2f5c`.** `m_deadline` computed once (`deadlineFor`, `:980-1000`) from the
request's `totalTimeout()` or `HttpClientRequestConfig::totalTimeout`, whose default is 30 minutes
(`HttpClientRequestTask.h:35-40`; checked, since a `neg_infin` default there would have meant no
chain deadline by default); `chkRemainingBudget()` (`:1008-1031`) asked first in `startHop()` and
the remainder stamped on the request which goes out (`:1095-1109`). Two properties fall out which
the lane did not claim: the pool's waiter deadline reads the same `request.totalTimeout()`
(`ConnectionPool.h:2173`), so the pool wait of every later hop is bounded by the chain remainder
too; and the first pass's finding 10 (the `(N + 1)^2` establishment budget) is now bounded in time
by the request's own budget, which is the practical fix - downgraded to a note. The case
(`:1261-1358`) discriminates by arithmetic (`HOP_DELAY < BUDGET < 2 x HOP_DELAY`) and asserts
`redirectHops() == 1` so a slow machine fails loudly rather than vacuously - good. The
exhausted-budget throw is a guard no case reaches, recorded as such; `deadlineFor` uses the wall
clock, as the hop's `deadline_timer` does, so a clock step moves both together (nit). **Closed.**

**Finding 2 - `8539a7c`, departure 1.** `outcomeOnClosed()` (`HttpClientRequestTask.h:1098-1112`):
`Completed` on no error; `Failed` if `event.isRetryable` or no connection; else `Failed` when the
connection reads `Ready` and `ConnectionUnusable` otherwise. See "the three departures" below for
the gate and finding 16 for what it rests on. **Closed as specified; one hole in the drivers.**

**Finding 4b - `8539a7c`, departure 2.** `connectionFailureCause()` (`:1152-1206`) reads the
connection task's `exception()` and `answerOnClosed` chains it as `errinfo_nested_exception_ptr`
(`:1224-1247`), the request's own `HttpException` with the error code staying the answer. See
below for the lock argument, which holds. Pinned twice: at the request task with a connection that
is a real task failing from `onExecute` and answering the sink from `onTaskStoppedNothrow`
(`TestHttpClientRequestTask.h:496-720`, `:2337-2458`), and at the driver against a dead port
(`TestHttp2ConnectionTask.h:1613-1723`), which is also the establishment-failure case the first
pass said no module ran. **Closed.**

**Finding 5 - `0af7934`.** The h1 driver takes `idleTimeout` as a constructor parameter
(`Http1ConnectionTask.h:238-239`, default `neg_infin`), arms a timer on the stream's executor at
the end of a keep-alive response (`:1132`) and, posted, at `scheduleTask()` for a driver the pool
has no request for (`:1345-1362`), cancels it when a request starts (`:572`) and in
`initiateClose()` (`:1304`), and closes gracefully when it fires (`:1259-1282`); the session's
driver factory carries the value (`ClientSession.h:1623-1650`). The case observes the client's FIN
at a keep-alive peer of its own (`TestClientSessionIdle.h`), in a new module because both session
modules are over target. See "finding 5's split" below. **Closed.**

### The three departures, checked hardest

**Departure 1 - finding 2's gate. My prescription was wrong and would have broken the fallback.**
I wrote "read `m_connection -> state()` at close"; taken literally that reports the bounced rider's
h2 placeholder `ConnectionUnusable`, `releaseStream` retires the entry (`ConnectionPool.h:2226-2233`),
and the entry is the one holding the adopted h1 driver - `m_byConnection` is written from both
pointers (re-verified: the task at `startConnection`, the driver at `refreshEntry:1233`). My own
first-pass verdict table had walked exactly that path and said the bounce's `Failed` outcome is
what keeps the fallback alive; I then prescribed the thing that would have changed it. The lane's
gate - ask the connection only when the close does not already prove the request unprocessed -
is the right one, and it is cheap for the reason the lane gives: `chkRequestMayBeReplayed` returns
from its `isRetryable` limb without consulting `isConnectionLost`, and a dead connection is retired
by the pool's own observation on the retry's `acquire` (`examineKey` refreshes before it
dispatches).

*Is the gate sufficient, and does it leave a genuinely-lost connection reported as merely `Failed`?*
Two kinds do, and they differ. (a) By design: every lost connection whose stream was never
written - the rider on an establishment failure, a stream still in the header-block queue at a
peer close (`closeAllStreamsUnwrittenRetryable`, `Http2ConnectionTask.h:1149`), a GOAWAY above the
stream's id - is retryable and so `Failed`; harmless for the session (replayed on the retryable
limb) and for the pool (retired by observation). (b) By a race the gate did not create but now
depends on: the outcome reads `state()` from the request task's drain thread, so it is right only
where the driver publishes before it answers. **The h1 driver does** - `finishStream` settles
`m_state` under `m_stateLock` before `sink -> onClosed` and says that order is its contract with
the pool (`Http1ConnectionTask.h:1076-1120`), and its task-stopped path stores `Closed` before
answering (`:1441`, `:1451`). **The h2 driver does on five routes and not on two** - finding 16.
So the gate is sufficient as a gate; the sentence the lane wrote to justify the read, "every
connection-level route to a closed stream publishes `Draining` or `Closed` with it", is what does
not hold, and the unit case cannot see it because its probe publishes before it answers by
construction (`TestHttpClientRequestTask.h:349-362`, "which is the order a driver produces too" -
asserted, not verified against the drivers).

**Departure 2 - finding 4b's cause. My prescription was wrong as written; the lane's read is
deterministic.** I wrote "carry the code `onTaskStoppedNothrow` already computes"; that code is
`operation_aborted` whenever `eptrIn` is set (`Http2ConnectionTask.h:2430-2432`), which is less
informative than the `connection_aborted` already there - the lane is right. What I meant was the
establishment's own error, which is an exception, and chaining the exception is the only way to
carry it through a contract that has one error code. *The lock argument.* `notifyReadyImpl` takes
`m_lock` (`TaskBase.h:536`), calls `onTaskStoppedNothrow` inside it (`:604`) - which is where the
h2 driver's `closeSubmissions()` answers the rider - and stores the exception inside the same
critical section, after the call (`if( ! m_exception ) setExceptionInternal( eptr )`, `:737-740`),
releasing only after `m_notifyCalled = true` (`:745`); `cbReady()` runs after the release
(`:748`). `TaskBase::exception()` takes the same `m_lock` (`:1129-1140`). `onTaskStoppedNothrow` has
exactly one non-chaining caller in `src/include/`, that line (grepped: 16 hits, 15 are
`base_type::` chains). So a request task whose drain reaches `connectionFailureCause()` after the
bounce blocks on the lock and reads the stored cause; it cannot read the gap. **Verified.** Two
things worth writing down beside it: on the other routes `closeSubmissions()` is called from
(`onPeerClosed`, `onPingDeadline`, `onDrainDeadline`, the graceful close) the handler holds the
task lock too (`BL_TASKS_HANDLER_BEGIN` is `BL_MUTEX_GUARD( m_lock )`, `:119-120`) but the task
has not stopped, so the read returns null and no cause is chained - correct, since those failures
carry their own code; and the lock order request-task `m_lock` then connection-task `m_lock` closes
no cycle because the only thing a driver takes on a request task is its mailbox lock through
`post()` (re-verified in L5) and the pool's edge is pool lock then connection-task lock, from a
thread holding no request task lock. No deadlock; a drain can stall behind a long strand handler
(a large `feed()`), which is latency only.

**Departure 3 - finding 1's refusal.** Reporting the 3xx rather than failing is the policy's own
principle applied where it applies most - a `Location` is entirely server-controlled - and it
matches what the client does with every other refusal and with redirects off. *Does refusal leave
any path to plaintext on 443?* Walked: the caller's URL (`createRequestTask`, `:1604-1614`); the
server's URL (`chkPrepareNextHop`, `:1315-1336`, placed before `m_next` is touched); a relative
`Location` resolves to the request's own scheme; a `Location` with an upper-case scheme is
lower-cased by `Uri::parse`; a plan built by anything but `createRequestTask` has an empty
`transportScheme`, which refuses every hop (the safe direction); the key's scheme is the URL's, so
it always equals the transport's after these two checks; a TLS session's `https` to `http` hop is
refused by the policy or, with the downgrade allowed, by the session. No path found. Two nits fall
out: `RedirectPolicy::allowHttpsToHttpDowngrade` reaches nothing through a session (the session
refuses any scheme change regardless) and should say so at the knob; and the refusal reason
(`RedirectDecision::refusal`) is not surfaced on `ClientRequestTask`, so a caller cannot tell "not
followed: disabled" from "not followed: scheme" without re-deriving it - the existing client's
`errinfo_http_redirect_url` is the precedent for surfacing the target, and the reason should ride
with it. Defence in depth, third nit: `SessionRequestTaskT`'s constructor takes any plan and any
request; one `BL_CHK_T` on the scheme there would make the invariant the type's rather than
`createRequestTask`'s.

### Finding 5's split, and `initiateClose()`

The split is argued correctly: `forgetConnection()`'s only lever is `requestCancel()`, design 5.4
names the idle lifetime as the graceful path and the cancel as the abrupt one, and a pool reaper
would have to keep the maintenance tick alive for every idle `Ready` entry, which inverts
`hasWorkToWatch()`. The pool keeps the value, each driver the timer; design 5.7's row and the h1
header now say so, and the h2 config comment is corrected to match.

*The cancellation points, and the thread `initiateClose()` runs on.* The mix-in states that
`initiateClose()` and the terminal are invoked ONLY from `onOperationCompleted()`, which the
handler epilog places outside the task lock (`MultiOperationTask.h:62-68`, `:280-293`; the macro
scoping confirms it, `TaskBase.h:107-118`, `:195-260`), and `onOperationCompleted()`'s only
callers are those epilogs - which for this driver run on the stream's executor: the read, the
write and now the timer. The external cancel does not break that: the h1 `cancelTask()` override
POSTS the socket shutdown to the executor (`Http1ConnectionTask.h:1375-1396`), the pending read
then fails there, and its epilog is what calls `initiateClose()`. So every touch of `m_idleTimer` -
the `reset( new ... )` in `chkArmIdleTimer()`, the `cancel()` in `cancelIdleTimer()` from the
request start and from `initiateClose()`, the handler - is serialised by the executor under a
stranded policy, which is the only policy this driver is claimed correct over (`:71-81`). The
arm-after-close hazard the mix-in exists to prevent ("an operation which is never woken is a task
which never reaches its terminal path") is closed by the same serialisation: `chkArmIdleTimer`'s
`isClosing()` test and its `beginOperation()` cannot interleave with `initiateClose()` on one
executor. The accounting balances: every `beginOperation()` (`:1214`) is matched by the handler's
`BL_TASKS_HANDLER_END_MULTIOP` whether it fired, was cancelled, or its timer object was destroyed
by a re-arm (asio cancels on destruction and still invokes the handler). **Verified.** The h2
driver relies on a *different* serialisation for the same pattern - its `cancelTask()` calls
`cancelTimers()` on the cancelling thread, under the task lock `requestCancel` holds
(`TaskBase.h:1237`, `:1026-1060`; `Http2ConnectionTask.h:2404-2409`), and its arm runs in handler
bodies which hold the same lock - so the two drivers are each correct for a reason the other does
not share; a maintainer copying one idiom into the other would break it. One nit: the posted arm
from `scheduleTask()` can land after a response has already re-armed, and then replaces a pending
timer with a new one - balanced but wasteful; `chkArmIdleTimer` should return when a timer is
already pending.

### Finding 4a, not fixed - do I still agree?

Yes, with one condition. With 4b landed the cost of the rider on a transport that cannot speak h2
is one wasted dispatch, one bounce and one retry per connection, which the library's own
`Connection: close` server pays on every request; and `maxRetriesPerRequest = 0` still makes a
cleartext HTTP/1.1 session unable to make any request. What 4b does *not* help there: the bounce's
nested cause is null, because the placeholder completed successfully, so a caller who sets zero
gets "The HTTP request failed: connection aborted" with nothing to explain it. That is not "not
safe", since the default is three and a caller who needs zero can set one; it IS a knob whose value
zero disables a protocol, and nothing at the knob says so (grepped: the design records the control
case, the pool policy comment at `ConnectionPool.h:239` says nothing). Condition for handover: one
sentence at `maxRetriesPerRequest` naming the consequence, and 4a as the first item of owed work
with the two-sided shape the lane correctly insists on (pool flag plus session setting it).

**Added 2026-09-22 (astra R4): whoever takes 4a must decide what happens to the control case, and
it will not tell them.** `ClientSession_FallbackRiderNeedsTheDispatchedRetryTests`
(`utf_baselib_httpclient4/TestClientSession.h`) is the negative control for the case above it. It
sets `poolPolicy.maxRetriesPerRequest = 0`, sends one GET over cleartext to the library's own
`HttpServer`, and asserts `task -> isFailed()`, `status() == 0`, `dispatched == 1` and
`released == 1`. It exists to pin the very defect 4a removes, so 4a's landing is exactly when it
stops meaning what it says.

**What happens to it depends on 4a's shape, and both shapes are live.** Read at the source: it
builds a **default** `ClientSessionConfig`, so `connectionConfig.cleartextProtocol` is `Http11`
while `alpnOffer` still names `h2`.

- **Per key or per connection** - astra's own wording for H21, *"avoid dispatching a rider when the
  selected protocol is already known to be h1"*. A cleartext key under `cleartextProtocol = Http11`
  is exactly that, so no rider is dispatched, the request succeeds with zero retries, and the case
  **fails loudly** on `isFailed()`. It must then be inverted: assert success, one dispatch, one
  release.
- **Per session**, which is the shape this finding's own wording implies and which
  `ConnectionPoolPolicy` being pool-wide makes natural - *"a session turns it off when it cannot
  produce HTTP/2 at all"*. A default session still can, over TLS. The flag stays on, the rider
  still rides the cleartext placeholder, and the case **keeps passing untouched** - which is the
  trap, because a green control then reads as evidence that 4a bites on this path when 4a has not
  touched it.

Either way the case has to be revisited deliberately. Pick the shape first, then say in the same
change-set what this case asserts afterwards, and if the answer is "nothing changed" say why. (The
first half of this finding's handover condition - the sentence at the knob - has since landed;
`ConnectionPool.h` now carries both it and the two-budget arithmetic of finding 10.)

### The evidence findings (7, 8) - blocking, or owed?

- **Finding 8 is withdrawn, in the lane's favour, by measurement.** `utf_baselib_httpclient6` is
  39.0 MB clang debug with one case and no HTTP/2 peer or `HttpServer` in it: the session with both
  drivers costs about 18 MB over the floor in a TU with nothing else. Then neither half of a split
  of `httpclient4` along the peer-versus-`HttpServer` seam lands under 40 (about 47 and 45 by the
  same figures), which is what the lane concluded and what its arithmetic could not show. This is
  the leave-one-out the rule asks for, done by accident; the module's own header draws the right
  lesson ("measure before adding one; do not convert from another module's figure"). What follows
  for a maintainer: every session-level module starts at about 39 MB, so there is room for about one
  peer per module and the enforcing platform (win-x86 debug) has measured none of them.
- **The concurrent-request case is the one I would not ship without**, and it is the same answer as
  the first pass: the pool's capacity logic against a real driver has been run only sequentially.
  Not blocking for handover with this record; blocking for calling an HTTP/2 client usable, since
  concurrency is the regime it exists for. Recipe as before: N requests at once, peer limit two,
  no `REFUSED_STREAM`, one connection.
- **The narrowing discrimination** (one streaming `PUT` against a peer preferring
  `[ "http/1.1", "h2" ]`) is owed and small; not blocking.
- **The establishment-failure path** is now run at the driver (the dead-port case), which is what
  4b needed. Through the pool and the session it is still not run; the pool's completed-task
  retirement of a real cancelled connection task (L5 section 9's obligation) is still pinned by
  reading only. Owed. One nit on the new case: the dead port is an ephemeral port just released,
  which another process can take between the two calls; improbable on a test host, and a listening
  socket that never accepts would be the deterministic shape.

### 16. Medium, new - the h2 driver answers its sinks before it publishes `Closed` on two routes, so the outcome the session's knob feeds on is a race there

Finding 2's fix reads `m_connection -> state()` on the request task's drain thread at the moment
the stream's `onClosed` is applied. The h2 driver publishes before it answers on `onPeerClosed`
(`Draining` at `Http2ConnectionTask.h:1492`, streams at `:1494`), on a GOAWAY, on a protocol error
(`onConnectionErrorEvent`, `:1385`, the streams already answered by the engine's events), on the
graceful close, and on the drain deadline - which answers first (`:2058-2061`) but runs from a
state already `Draining`. It answers **before** it publishes on two routes: `onPingDeadline`, which
runs `closeAllStreams` and `closeSubmissions` (`:1832-1836`) and only then `publishState( Closed )`
(`:1839`), from a state that is `Ready`; and `onTaskStoppedNothrow`, which runs `closeSubmissions`
and `closeAllStreamsUnwrittenRetryable` (`:2428-2434`) and then `publishState( Closed )` (`:2437`),
from whatever state was last published. That terminal route is what every write error takes
(`onWrite` is `BL_TASKS_HANDLER_BEGIN_CHK_EC`, `:1578`), what every read error other than EOF,
reset or TLS truncation takes (`onRead`, `:1446-1452`; `isPeerClosed`, `:1429-1435`), and what an
external cancel of a `Ready` connection takes - and on all of those the last published state is
`Ready`. A peer which resets during an upload is noticed on the write side as `EPIPE` or
`ECONNRESET` as often as on the read side, so this is the ordinary shape of "connection lost while
sending". Between the sink's `onClosed` (a post to the request task's mailbox and a thread-pool
schedule) and the strand's `publishState( Closed )` a few instructions later, the drain may or may
not have read `state()`: `ConnectionUnusable` or `Failed`, and with it `isConnectionLost` true or
false. The pool is indifferent (it observes `Closed` either way); the knob is not. So
`retryIdempotentOnConnectionLoss` is no longer dead; it is nondeterministic on the write-error and
keepalive-timeout routes. Not a data race - `state()` is an atomic load - and not something
ThreadSanitizer would report; a stress run of the knob would show it as a coin toss.

**Fix, in the driver, two lines:** move `publishState( ConnectionState::Closed )` above the
answers in `onPingDeadline` and in `onTaskStoppedNothrow`; `publishState` is monotone
(`:2200-2215`, the L4 record) and nothing in `closeStream` reads the state, so publishing first
costs nothing and makes the lane's sentence true. **Pin it at the driver, not the request task:**
`RecordingSink` already holds the connection (`setConnection`, `Http2DriverTestUtils.h:164`), so
recording `connection -> state()` inside `onClosed` (`:312-334`) and asserting `Closed` there - in
the new dead-port case, where today it would read `Connecting`, and in a write-failure case - pins
the ordering the gate rests on. The request-task case's probe should say that its
publish-before-answer is the driver's *contract*, and that case is what enforces it.

### What is unresolved from the first pass

- **6** (inter-hop work under the queue lock and the wrapper lock, including caller code): not
  addressed, not on the not-safe list; owed, with the documentation line at `BodySource::rewind()`
  and `ContentDecoder` as the minimum.
- **9** (a cancel between hops completes as success with the 3xx): not addressed; small; owed.
- **10**: mitigated in time by finding 3's chain deadline; the two budgets remain two budgets;
  a note now rather than a finding.
- **11**: design 5.4 still says "both halves use it" (`http2-design.md:970`); the pool does not
  call the predicate. Text, unresolved.
- **12** (decode on a failed hop): not addressed; owed.
- **13** nits: (f) is moot for this round (no lane message claims a ThreadSanitizer run); the rest
  stand, plus the four new nits above (`allowHttpsToHttpDowngrade` dead through a session, the
  refusal reason not surfaced, the constructor's missing check, the double arm) and one on process:
  `e6246fb` is the one merge in the feature with no message.
- **14, 15**: unchanged; the L8 process-default-session question stands.
- **Owed and unchanged**: 5(c) at the driver; ThreadSanitizer on any module (the `resolveDriver`
  read is still the one it would report first); OpenSSL 1.1.1w; Windows; a64.

### What I got wrong in the first pass

Three things, stated so a maintainer weighs the rest accordingly. The finding 2 prescription, taken
literally, would have retired the fallback entry - the lane caught it. The finding 4b prescription
named the wrong artefact (a code which is `operation_aborted` on every establishment failure) - the
lane caught that too. And the TLS join case's `bodyOf( 1U )` read had no happens-before against the
peer's `appendBody` and lost the race one run in eight; I read that assertion, wrote "fine", and
did not see it - the lane found it by running, which is the third time in this feature that running
found what reading did not.

### What I would not ship

- **Finding 16**, before the knob is documented as working: two reorders and one recorded state.
- **The concurrent-request case**, before the client is described as an HTTP/2 client rather than
  a serial one. The L5 pool round's capacity logic is composed with a real driver only in the
  regime where it never decides anything.
- **A ThreadSanitizer run of `httpclient4` or `httpclient5`** before any a64 deployment: the two
  unsynchronised reads L5 finding 7 named have now been composed in four modules and observed by
  none, and the `resolveDriver` read is under the pool lock on every fallback.
- **`maxRetriesPerRequest = 0` without the sentence at the knob** (4a's condition).

Everything else in the two ledgers is owed work a maintainer can schedule, and the record says
which.

### Verified versus inferred in this pass

Verified by reading: every diff named above and the tip code around it; the single non-chaining
caller of `onTaskStoppedNothrow` and its position inside `notifyReadyImpl`'s critical section
relative to the exception store and the callback; `exception()`'s lock; the handler macros'
scoping of the epilog outside the task lock; the mix-in's invocation of `initiateClose()` from
`onOperationCompleted()` only, and the h1 `cancelTask()` post; the order of publish versus answer
on every h2 route listed in finding 16 and on both h1 routes; the `m_byConnection` writes from both
pointers; `Uri::parse`'s scheme fold; `HttpClientRequestConfig::totalTimeout`'s 30-minute default
and the pool's waiter deadline reading the request's own; the manifest's 1030 by counting and the
one edited body by hash. Verified by grep: `onTaskStoppedNothrow` callers; `requestTimeout`
readers; the absence of any sentence about `maxRetriesPerRequest = 0` at the knob; "both halves use
it" still present. Inferred or recalled: that asio invokes a cancelled wait's handler with
`operation_aborted` on timer destruction (asio's documented behaviour, from memory); that a peer
reset during an upload surfaces on the write side as often as the read side (from how a RST is
delivered to a socket with both pending, from memory); that a stress run of the knob would show
finding 16 as nondeterministic (a claim about timing, not observed); the 47 and 45 figures in the
withdrawal of finding 8 (arithmetic on the lane's measured 39.0, 8.6 and 5.8, subject to the
non-additivity the deferral record describes - the direction is what is asserted, not the
numbers).

### Not checked in this pass

No build and no test run. `Http2TestRecorder::waitForRecordsOf` and the keep-alive peer's read
semantics under a client FIN (relied on from the case's own description). `closeGracefully`'s use
of the state in the h2 driver, relied on from L4. The `ClientConnectionTaskBase` `onTaskStoppedNothrow`
chain (`:648`) for whether it alters `eptrIn`, which would change only the cause's identity, not
its presence. Windows, 1.1.1w and a64, as before.

### For a maintainer picking this up cold

The client is `httpclient/ClientSession.h` over `ConnectionPool.h`, `HttpClientRequestTask.h`,
`http2/Http2ConnectionTask.h` and `httpclient/Http1ConnectionTask.h`, and the design is
`notes/plans/http2-design.md` with the plan beside it; the six review records under
`notes/plans/issues/http2-l*-review-record.md` are the argued history and each ends with a ledger.
Five things to know before changing anything. (1) One session speaks one scheme, enforced at two
entry points; if you add a third way a URL reaches a session, add the check. (2) The retry is two
halves with one rule: the pool replays what it still holds, the session replays what was
dispatched, and `chkRequestMayBeReplayed` is the rule the session calls and the pool applies inline.
(3) A request through a session is a chain with one deadline; every hop task arms only what is
left. (4) The pool never closes a connection gracefully - it cancels - so the idle lifetime is the
drivers', and the two drivers serialise their timers differently (executor for h1, task lock for
h2); do not copy one idiom into the other. (5) The outcome a request task reports reads the
connection's state at the close, so a driver must publish before it answers its sinks; the h2
driver does not yet on two routes (finding 16), and that is the first thing to fix. Then the owed
list, in the order I would take it: finding 16; the concurrent-request case; a ThreadSanitizer run
of `httpclient4`; 4a with its sentence at the knob; the narrowing case; findings 6, 9, 12; 5(c) at
the driver; OpenSSL 1.1.1w. Every session-level test module costs about 39 MB before its first
case, and the size gate that matters has never seen one.
