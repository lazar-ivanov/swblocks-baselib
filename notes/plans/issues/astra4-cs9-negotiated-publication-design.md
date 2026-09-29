# CS-9 / U01 — the connection publishes its negotiated value: design note

**Date:** 2026-09-29. **Status:** revision 1, written by lane 1, for review. No production code is
written until this note carries its agreement line.

**The decision.** D1 of [`astra-fourth-review-decisions.md`](astra-fourth-review-decisions.md), taken
by the maintainer on 2026-09-29: shape (a″). `continueAfterConnected( )` writes `m_negotiated` once and
then sets an atomic "published" flag; both getters read the flag first and return a default
`NegotiatedProtocol` - `Unknown`, no identifier - until it is set. The shape is not reopened here; this
note settles its mechanism, as the plan's "What (a″) does, as decided" asks.

**Provenance.** Line numbers are `0f7c7c7`'s, the branch base. VERIFIED means read at the source
named; INFERRED means it follows from verified facts and the rule named.

---

## 1. The premise the mechanism rests on: `m_negotiated` is written at most once per task

Re-verified at the source for this note, since a single publication cannot cover a second write.

- **One assignment in the library.** `m_negotiated` of `ClientConnectionTaskBaseT` is assigned only at
  `ClientConnectionTaskBase.h:656`, in `continueAfterConnected( )`. Every other use reads it: `:630`
  (the factory's argument), `:669` (the trace), `:760` (the getter), `Http2ConnectionTask.h:2755` (the
  fallback test) and `:3111` (the driver's getter). No test writes it: every `m_negotiated` under
  `src/utests` is a test double's own member. VERIFIED (grep of `src/include` and `src/utests`).
- **The cleartext path writes on the same line.** `ClientTlsStreamOps< STREAM, false >` returns
  `withoutAlpn( cleartextProtocol )` (`:273-282`), assigned at `:656`. The plain policy's
  `beginProtocolHandshake( )` calls the continuation synchronously (`TcpBaseTasks.h:562-569`), and
  `isProtocolHandshakeNeeded` is false (`:469-472`), so the retry below is never taken (`:1595`).
  VERIFIED.
- **The establisher's in-place retry is refused once the continuation has run.** The retry
  (`TcpBaseTasks.h:1577-1622`) requires `! hasHandshakeCompletedSuccessfully( )` (`:1596`) and
  `isChannelOpen( )` (`:1594`). VERIFIED.
  - **The gate is the stream wrapper's flag, not `m_isHandshakeCompleted`.** The plan and the brief
    cite `TcpSslBaseTasks.h:659`, which sets `m_isHandshakeCompleted`. The retry reads neither that nor
    anything the task sets: `hasHandshakeCompletedSuccessfully( )` (`TcpSslBaseTasks.h:741-744`) asks
    the `AsioSslStreamWrapper`, whose own handshake handler sets its flag (`AsioSslStreamWrapper.h:320`)
    before it transfers to the task's `onHandshakeCompleted( )` (`TcpSslBaseTasks.h:652`). That handler
    then sets `:659` and `:668` and only then calls the continuation (`:670`). Both flags precede the
    write, so the conclusion stands; the citation was imprecise. VERIFIED.
  - **After the fallback's `detachStream( )` the gate reads false**, because the stream is gone
    (`ClientConnectionTaskBase.h:627`, after the write). The retry is still refused, by
    `isChannelOpen( )`, which reads false for the same reason (`TcpSslBaseTasks.h:204-207`). VERIFIED.
  - So a throw from `continueAfterConnected( )` or `onProtocolNegotiated( )`, before or after the write,
    is never retried; a retry happens only in an attempt whose handshake did not complete, in which the
    continuation, and so the write, never ran. VERIFIED.
- **CS-6's per-endpoint connect loop enters the handshake once.** `onConnectionEstablished( )`
  (`TcpBaseTasks.h:1489-1553`) enters the pre-handshake stage only for an attempt which connected and
  was not cancelled (`:1521-1535`), and begins no further attempt after it; a failed attempt moves to
  the next endpoint without entering the stage (`:1498-1517`). The tunnel stage calls its stored
  continuation once, when the negotiation reports `Done` (`TcpTunnelStage.h:1628`, `:1786-1806`).
  VERIFIED.
- **A completed task rescheduled does not run again.** `TaskBase::scheduleNothrow( )` allows a restart
  (`TaskBase.h:1222-1233`), but the establisher refuses a second run: `m_resolver` is set on the first
  (`TcpBaseTasks.h:862`) and reset only by the retry (`:1607`), and `:846-851` refuses a set one.
  VERIFIED.

**So the write happens at most once per task object.** It is guarded by an assertion at the write
(§3). Had any route written twice, this change-set would have stopped here and gone back to the
maintainer.

## 2. The flag

- **Where:** in the establishment base, beside `m_negotiated`, protected:
  `std::atomic< bool > m_isNegotiatedPublished`, initialized `false` in the constructor's initializer
  list. Never cleared.
- **Type:** `std::atomic< bool >`, the file's idiom (`m_connectionState` is `std::atomic` in the
  driver). `ClientConnectionTaskBase.h` gains `#include <atomic>`.
- **Memory order: the default, seq_cst,** for the store and the load.
  - Release and acquire would be enough for the publication itself: the write, then a store-release;
    a load-acquire which reads `true`, then the read.
  - Nothing in `src/include` names a memory order (grep for `memory_order`: none), and the driver's
    `m_connectionState`, which this flag is ordered against (§5), is seq_cst. A first explicit order in
    the library, for no measurable gain, is a new idiom for every later reader to check.
  - The cost is one fenced store per connection's lifetime. The load is on the reader's path, and a
    plain load on x86 either way.

## 3. The publication path, and the two getters

```cpp
void publishNegotiated( SAA_in httpclient::NegotiatedProtocol negotiated )   // protected, in the base
{
    BL_ASSERT( ! m_isNegotiatedPublished.load() );

    m_negotiated = BL_PARAM_FWD( negotiated );

    m_isNegotiatedPublished.store( true );
}
```

- `continueAfterConnected( )` calls it in place of today's assignment, with the same argument - so the
  store comes after the write, and before the trace and `onProtocolNegotiated( )`.
- **Protected, so a test type can reach it**, and nothing else is added to reach it. A deterministic
  case writes the protected member directly to make "written but not published", and calls the helper to
  publish (§7). The production surface grows by one protected function and two protected members; no
  public signature changes.
- **The assertion is `BL_ASSERT`**, the house's, active in debug builds - where the lane's and the
  gate's debug runs execute every route into the write.
- **The base's getter** (`:758`) becomes
  `return m_isNegotiatedPublished.load() ? m_negotiated : m_unsettled;`.
- **The driver's override** (`Http2ConnectionTask.h:3109`) becomes `return base_type::negotiated( );`.
  Today it returns `base_type::m_negotiated` directly; after this it delegates.
- **The strand's own reads stay direct**: `:630`, `:669` and `Http2ConnectionTask.h:2755` follow the
  write in the same handler, on the same thread.

## 4. The default's storage: a per-object const member

`const httpclient::NegotiatedProtocol m_unsettled;` in the base, beside the flag, default constructed.

- **It lives exactly as long as the member it stands in for.** Both getters return a reference, and a
  caller keeping one past the task's life was already broken for `m_negotiated`.
- **No toolchain argument is needed.** A function-local static would rely on thread-safe local statics.
  devenv2's `vc12` lacks them. devenv2 cannot compile this header, though - its OpenSSL is 1.0.2d
  (`projects/make/devenv-detect.mk`) and `:48` refuses anything below 1.1.0 - and devenv3's `vc14` and
  every gcc and clang listed there have them. No makefile disables them (grep for `threadsafe`,
  `threadSafeInit`: none). So a static would be safe; the member is chosen for the next two reasons.
- **No static destruction at exit.** A static `NegotiatedProtocol` holds a `std::string`, which is
  destroyed at exit while a detached thread may still read it. A namespace- or class-scope constant has
  the same exit, plus unordered dynamic initialization for a class template's static member.
- **The cost:** one `NegotiatedProtocol` per connection task - an enum and an empty `std::string`,
  about 40 bytes at 64 bits.

## 5. Why nothing a reader relies on weakens

- **The flag precedes every publication a reader can observe after the write.** It is stored before
  `onProtocolNegotiated( )`, which publishes `Ready` (`Http2ConnectionTask.h:2808`) in the same handler;
  every `Draining` and `Closed` site other than the terminal is a strand handler reached only after
  `onProtocolNegotiated( )` built the session (the one-off review's per-site list, plan-fable-r1 F2);
  the fallback's terminal follows the write in the same handler; and the terminal runs under the task
  lock, which the handshake handler holds across the write. So a reader which observes any state other
  than `Connecting` from a task which wrote its value also observes the flag. INFERRED from those
  VERIFIED sites and the seq_cst load and store of `m_connectionState` and of the flag.
- **A task whose establishment failed before the write never sets it,** and its getter returns
  `m_unsettled` - `Unknown` with no identifier, which is what `m_negotiated` still holds. So the value a
  reader gets after observing a non-`Connecting` state is the member's final value, exactly as today.
- **The pool's H04b test stays, for a different reason** (`ConnectionPool.h:1121-1136`). Reading
  `negotiated( )` is safe without it now. What `entry -> isReady` still decides is which entries
  count: `isReady` is set only once the pool has observed `Ready` (`:1508-1510`). Without it, an h2 task
  which fell back reports `Http11` from its handshake onwards, never publishes `Ready`, and would raise
  the key's limit to `maxConnectionsPerKeyHttp11` (`canStartConnection( )`, `:1767-1785`) before the
  pool has adopted and observed the HTTP/1.1 driver which replaces it (`:1455-1510`). VERIFIED.
- **The request task's readers need no rule.** `completeResponse( )` (`HttpClientRequestTask.h:1906`)
  and `isRequestUnsuitableForConnection( )` (`:884`) read a value which is safe at any time: `Unknown`
  before the handshake completes, and the settled value from then on, even before `Ready`.

## 6. The comments

The search the brief asks for - "READ state", "state( ) first", "must not look", "publication order",
"valid once state", "read afterwards", "when it is settled", "rule its class comment", and every comment
naming `negotiated( )` in `src/include` and `src/utests` - finds these, each corrected in a comment-only
commit of its own after the logic commit:

| Where | What it says now | Correction |
|---|---|---|
| `ClientConnectionTaskBase.h:364-369` | "written once … and read afterwards"; the pool and request task read it off the strand | written once, published by the flag; the getter is safe from any thread |
| `ClientConnectionTaskBase.h:751-756` | `Unknown` until the handshake has completed | `Unknown` until the value is published, then the settled value |
| `Http2ConnectionTask.h:196-209` | "WHAT negotiated( ) RESTS ON": the publication order and "READ state( ) FIRST" | the connection publishes the value itself; no reader needs a rule |
| `Http2ConnectionTask.h:2801-2806` | `Ready` "is what releases" `m_negotiated` | `Ready` follows the value's own publication |
| `Http2ConnectionTask.h:3102-3107` | "valid once state( ) is not Connecting" | safe at any time; delegates to the base |
| `ConnectionPool.h:1121-1128` | testing `isReady` first is what makes the read safe | it is what decides which entries count (§5) |
| `HttpClientRequestTask.h:1898-1901` | read at the END "because that is when it is settled" | a request which fails before its connection's handshake completes reads `Unknown` |
| `ClientConnection.h:357-371` | "Unknown with no identifier until ALPN has resolved"; states no requirement | the requirement on every implementation, line for line |

**Left as they are, because they are still true:** `ConnectionPool.h:549-553` (the h2 driver's reads
are atomic loads and a reference, still, and take no lock); `Http1ConnectionTask.h:174-176` and the test
double's `TestClientConnectionTaskBase.h:397` (both `const` members, which a const member keeps safe);
`HttpClientRequestTask.h:868-873`, `:1636-1639` and `:1977-1981`, and `ClientConnection.h:602-608` and
`ClientConnectionTaskBase.h:608-623` (none states the rule or the premise); `TestClientContracts.h:1310`
and `TestHttpClientRequestTask.h:1531`, `:2466` (about `ClientResponse`'s single door and the probe's
refusal).

**`ClientConnection.h:357-371`, line for line.** The block is 15 lines and stays 15. It keeps "one
query and not two" and what the value fills, shortens the history of the missing identifier to a
pointer - `NegotiatedProtocol`'s own note already carries it - and adds the requirement: safe from any
thread at any time; `Unknown` with no identifier until the protocol is settled, by ALPN in the handshake
or by configuration for cleartext; never changed after it is settled. The evidence is
preprocess-identical output for two dependent translation units, before and after, as CS-7 showed its
own comment edits (`logs/astra3/cs7/connectionpool-comment-preprocess-identical.log`): one outside the
21-module gate (`utf_baselib_httpclient2`) and one inside it, plus a control which adds a line and must
differ. If it cannot be shown, the gate takes the four modules outside the 21.

## 7. The tests

All five are in one new module (§7.4). `tls_stream_t` is the stranded TLS policy of the shared
`Http2DriverTlsProbe.h`.

### 7.1 The deterministic getter case — the red for the contract

- Two unstarted test types, each with a method which writes the protected `m_negotiated` - `fromAlpn(
  "h2" )` - and publishes nothing: one derived from `ClientConnectionTaskBaseT< tls_stream_t >`, one from
  `Http2ConnectionTaskT< tls_stream_t >`.
- It asserts that the base's `negotiated( )`, and the driver's through `ClientConnection`, return
  `Unknown` with no identifier. Each half is checked without stopping the case, so a run shows both.
- **Today both return `h2`: a red which is certain, from a pure input** - one thread, no timing.
- After the fix, each type also calls `publishNegotiated( )` with the value, and a thread started
  afterwards reads each getter and gets `Http2` and `"h2"`. The thread's start orders the read after
  the publication; what this pins is that a published value is what every reader gets.

### 7.2 The ThreadSanitizer pair — the red for the race

- The real driver (`Http2DriverProbe`) over TLS against `TlsEndingPeer` selecting `h2`
  (`AwaitTheClient`).
- **A reader thread takes no lock and reads `negotiated( ).protocol( )` through `ClientConnection`.**
  It reads once and signals that it has started; the case schedules the driver only after that signal.
  Then the reader loops until it reads a protocol other than `Unknown`, records it, and signals again.
  The loop's only other operation is a load of an atomic "abandon" flag, which the case stores only if
  that signal never comes - so on a passing run the load reads its initial value and synchronizes with
  nothing. The case asserts that the reader saw `Http2`.
- **Why today's run reports, every run.** The read which returns `Http2` read the byte the strand wrote
  at `:656`, so it follows that write in real time. Nothing orders the two: the reader's last
  synchronization is its first signal, before the driver was scheduled. At most three threads touch that
  byte's word - the constructing thread, the strand's and the reader - so ThreadSanitizer's four shadow
  cells still hold the write. INFERRED from ThreadSanitizer's shadow model, and to be shown by the red.
- **Why the fixed run cannot report.** The reader reads `m_unsettled`, written before its thread was
  created, until its load of the flag returns `true`; that load synchronizes with the store which
  follows the write.
- **`protocol( )` and not a copy of the whole value.** On today's code a torn `std::string` copy can
  crash where a byte read only reports; a report is the verdict. The flag gates the whole object, so
  what a `protocol( )` read shows holds for a copy. The composed cases (§7.3) are what copy the whole
  value, through the request task.
- **The build:** `BL_CLANG_ENABLE_RA_TSAN=1` in an emptied clang debug tree, `TSAN_OPTIONS` set
  explicitly, as CS-7's (`second_deadlock_stack=1` and the suppressions file). The positive control is
  `utf_baselib_basictask` in the same tree, whose race at `TestBaselibBasicTask.h:127` must report. The
  verdict is read from `WARNING: ThreadSanitizer` lines, not from the exit code, and the tree is emptied
  afterwards.

### 7.3 The composed cases — U01's route, and its controls

- **The harness.** What dispatches a request onto an establishing connection is the pool, under
  `ridePreface` (`ConnectionPool.h:1733-1762`). Composed with the request task and a real driver, that
  pool is the session's harness, and those modules are over the target (`utf_baselib_httpclient5` and
  `…10`, 47.5 MB). What the pool hands the request is the `Connecting` driver itself. So the cases use
  a one-connection test pool, as `utf_baselib_httpclient8` does: it answers every `acquire( )` with the
  driver, posted as the contract requires, and records `releaseStream( )`. The request task and the
  driver are the real ones.
- **The driver** is `Http2DriverProbe`, derived in the module to signal once `submit( )` has returned a
  handle, and once `beginPreHandshakeStage( )` has returned - the handshake begun.
- **"Rides the preface" is certain, not raced.** The request task is pushed first, and the driver only
  after its `submit( )` signal. A submit to a driver which has not started is queued: `postCommand( )`
  appends while `m_isStrandReady` is false. The pool can do the same: `startConnection( )` hands out an
  attempt it has only just scheduled.
- **(a) A cancel during a held handshake reports `Unknown`.**
  - The peer is a listener which never accepts, as `utf_baselib_h2client10` holds a handshake: the
    kernel completes the connect, and nothing answers the ClientHello.
  - Once the handshake has begun, the case asserts `Connecting`, cancels the request and waits for it.
  - It asserts: the request failed with its cancel (`operation_aborted`, `isOwnFailure( )`); its
    response reports `Unknown` and an empty ALPN.
  - Then it cancels the driver. The driver's terminal bounces the queued submit, and the request's late
    close releases the slot. The case waits for the release and asserts: exactly one release, and the
    cancel and the response unchanged.
- **(b) A cancel after the handshake reports `h2`.** The peer is `TlsEndingPeer` selecting `h2`. After
  the driver's opening write is over (`waitForQuiet( )`) the case asserts `Ready`, cancels the request
  and waits for it. It asserts `Http2` and `"h2"`, the cancel intact, and one release, from the stream's
  close.
- **(c) The fallback reports `Http11`.** The peer selects `http/1.1`. The factory registers an HTTP/1.1
  creator which builds a stub connection and keeps the stream, so the task takes the production
  fallback: it hands the stream over and completes. Its terminal bounces the queued submit, retryable.
  The case asserts: the request failed, retryable; it reports `Http11` and `"http/1.1"`; one release.
- **Green on both sides of the fix:** in (a) no write happens at all, and (b) and (c) read after the
  write, ordered by the quiet signal and by the request's mailbox. They are characterizations, committed
  before the fix. (b) is the control which separates the fix from one that blanks every failed
  response. The race itself is §7.2's.

### 7.4 The module

- `utf_baselib_h2client11`, new and reserved for this lane: `Main.cpp`, `notes.txt` with a recipe per
  case, and the `devenv7_only` marker - the stranded policies need Boost 1.72.
- **What it pays for:** the HTTP/2 driver over the stranded TLS policy, and the request task. The
  driver alone costs `utf_baselib_h2client10` 37.6 MB at a64 clang debug. So this module is expected at
  or above the 40 MB target.
- **A split cannot lower it.** Case (a) alone needs both the driver and the request task, and the
  other four cases need the driver. So the reason is recorded in `Main.cpp`, as `utf_baselib_h2client9`
  and `…10` record theirs, and the D-section of `windows-matrix-handoff.md` gives the a64 size and the
  x86 estimate.
- **The measurement is committed with the tests, before the review closes,** and this section is then
  updated with it. `utf_baselib_httpclient14` stays reserved and unused.

### 7.5 Runs

- 50 runs of the five cases after the fix, at clang debug. This is the default regression count; no
  rate is the criterion here.
- The reds are deterministic, and are shown red once and green once: §7.1 by construction, §7.2 by the
  argument above.
- Then `utf_baselib_h2client`, `…2`, `…3` and `utf_baselib_httpclient`, one at a time; and tier 1, with
  its report explained in the lane's journal.

## 8. Commits, in order

1. This note.
2. Tests: the new module - §7.1's red half, §7.2's case, §7.3's characterizations - with the measured
   size. Evidence: §7.1 red, the rest green, §7.2 red under ThreadSanitizer, with the positive control.
3. The note's dated agreement line, once the orchestrator sends it.
4. Logic: `ClientConnectionTaskBase.h` (flag, default, helper, getter, `<atomic>`) and
   `Http2ConnectionTask.h` (the getter delegates). Evidence: §7.1 green, §7.3 green, §7.2 green.
5. Tests: §7.1's publish half.
6. Comments, one commit per file, per §6.
7. The D-section of `windows-matrix-handoff.md`: the deterministic cases run on Windows; ThreadSanitizer
   does not.
