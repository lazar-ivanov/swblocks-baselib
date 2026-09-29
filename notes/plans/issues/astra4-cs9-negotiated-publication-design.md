# CS-9 / U01 — the connection publishes its negotiated value: design note

**Date:** 2026-09-29. **Status:** revision 4, written by lane 1 — **agreed 2026-09-29** (see the
agreement line at the end). CS-9 is implemented to this note, in the commits which follow this one (§8).

**Revisions.**
- r1 `8f1cec6`.
- r2 `a43477f` carried what the tests on today's code found, with nothing in the mechanism (§1 to §6)
  changed:
  - §7.2: r1's premise for the ThreadSanitizer red was wrong - measured, it reported in 1 run of 54. The
    reader is rebuilt, and its reason is the runtime's slot sharing, read at its source;
  - §7.4: the module is measured, 37.9 MB;
  - §8: the commits so far.
- **r4 (this)** carries review round 2 (`astra4/reviews/cs9-note-r1.md`, "Round 2 - note r3
  (886ac88)", agree with changes): N1 to N3, P3 and text only, taken as the review words them, each checked
  at its source first - N1 and N2 in §7.2, N3 under §7.2's rate table and in §7.5. It adds the agreement
  line. Nothing else changes.
- r3 `886ac88` carried review round 1: `astra4/reviews/cs9-note-r1.md`, agree with changes, and the
  orchestrator's `cs9-note-orchestrator-r1.md`, whose O1 and O2 are the reviewer's R7 and R5.
  - The orchestrator accepted every finding, and refined R9.
  - The review agreed §1 to §6's mechanism as written, and no finding changes it.
  - Each finding was checked at its source before it was taken. Where it was taken:
    - R1: §7.2 corrects its shadow-cell sentence and cites rates per committed reader; §7.5 makes
      §7.2 a red/green pair of 50 runs each;
    - R2: §7.2's verdict is by the report's frames; §7.5 adds a whole-module instrumented run after the
      fix;
    - R3: §7.1;
    - R4, R5 and R6: §6; R5 in §4 and R6 in §3 too;
    - R7: §7.3;
    - R8: §7.3 and §7.4, and §8's D-section;
    - R9, as the orchestrator refined it: §7.5 and §8;
    - R10: §7, before §7.1;
    - R11: §1, §2 and §4.

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
    then sets `:659` and `:666` and only then calls the continuation (`:670`); `:668` sets no flag, it
    updates the untrusted-endpoint record (`AsioSslStreamWrapper.h:820-842`). Both flags precede the
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
- **A task rescheduled after a run which reached the resolve does not run again; one which never
  reached it wrote nothing.** `TaskBase::scheduleNothrow( )` allows a restart (`TaskBase.h:1222-1233`),
  but the establisher refuses a second run: `m_resolver` is set on the first (`TcpBaseTasks.h:862`) and
  reset only by the retry (`:1607`), and `:846-851` refuses a set one. A first run which never reached
  `:862` - cancelled before it started (`TaskBase.h:1243-1253`), or failed before the resolver was made
  - can be followed by one establishment, but it wrote nothing itself. VERIFIED.

**So the write happens at most once per task object.** It is guarded by an assertion at the write
(§3). Had any route written twice, this change-set would have stopped here and gone back to the
maintainer.

## 2. The flag

- **Where:** in the establishment base, beside `m_negotiated`, protected:
  `std::atomic< bool > m_isNegotiatedPublished`, initialized `false` in the constructor's initializer
  list. Never cleared.
- **Type:** `std::atomic< bool >`. It is not this file's idiom - `ClientConnectionTaskBase.h` holds no
  `std::atomic` today - but it is the driver's (`m_connectionState`, `Http2ConnectionTask.h:407-408`)
  and `TaskBase`'s (`m_cancelRequested`, `TaskBase.h:544`). `ClientConnectionTaskBase.h` gains
  `#include <atomic>`.
- **Memory order: the default, seq_cst,** for the store and the load.
  - Release and acquire would be enough for the publication itself: the write, then a store-release;
    a load-acquire which reads `true`, then the read.
  - Nothing in `src/include` names a memory order (grep for `memory_order`: none), and the driver's
    `m_connectionState`, which this flag is ordered against (§5), is seq_cst. A first explicit order in
    the library, for no measurable gain, is a new idiom for every later reader to check.
  - The cost is one store per connection's lifetime, and one load per read. On a64 - this host - seq_cst
    costs nothing over release and acquire: this clang compiles both loads to `LDARB` and both stores to
    `STLRB` at `-O2` (`logs/astra4/cs9/codegen/atomics-a64-READ.txt`; VERIFIED). x86 is not measured:
    this clang has no x86 target.

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
- **`m_negotiated` stays protected, and its comment carries the rule for a derived driver** (R6, §6):
  off the strand it is read only through `negotiated( )`, and a driver which IS this task answers
  `ClientConnection::negotiated( )` by delegating to it, never by returning the member - which is exactly
  what U01's own path did (`Http2ConnectionTask.h:3111`, `return base_type::m_negotiated`).
  - Making it private, with a protected strand-side accessor for `:2755`, was considered and not taken.
    It would need a test-only writer for §7.1, growing the production surface the brief asks to keep
    minimal.
  - Reverses if a derived driver added in L7 or L8 reads the member off the strand.
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
- **A reference taken before the publication names `m_unsettled` for as long as it is held**, and so
  stays `Unknown`. That is new - today a held reference names `m_negotiated` and sees later values,
  racily - and the interface's requirement states it (R5, §6).
- **No toolchain argument is needed.** A function-local static would rely on thread-safe local statics.
  devenv2's `vc12` lacks them. devenv2 cannot compile this header, though - its OpenSSL is 1.0.2d
  (`projects/make/devenv-detect.mk`) and `:48` refuses anything below 1.1.0 - and devenv3's `vc14` and
  every gcc and clang listed there have them. No makefile disables them (grep for `threadsafe`,
  `threadSafeInit`: none). So a static would be safe; the member is chosen for the next two reasons.
- **No static destruction at exit.** A static `NegotiatedProtocol` holds a `std::string`, which is
  destroyed at exit while a detached thread may still read it. A namespace- or class-scope constant has
  the same exit, plus unordered dynamic initialization for a class template's static member.
- **The cost:** one `NegotiatedProtocol` per connection task - a 1-byte enum padded to 8, then an empty
  `std::string`: 32 bytes with libc++, which `clang2010` uses here, and 40 with libstdc++ or MSVC x64.
  INFERRED from the layout (`ClientTypes.h:108-109`) and those libraries' string sizes.

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
| `ClientConnectionTaskBase.h:364-369` | "written once … and read afterwards"; the pool and request task read it off the strand | written once, published by the flag; off the strand read only through `negotiated( )`, and a driver which IS this task delegates to it and never returns the member (R6, text below) |
| `ClientConnectionTaskBase.h:751-756` | `Unknown` until the handshake has completed | `Unknown` until the value is published, then the settled value; the reference names the value current at the call, and a caller which wants a later value asks again (R5) |
| `Http2ConnectionTask.h:196-209` | "WHAT negotiated( ) RESTS ON": the publication order and "READ state( ) FIRST" | the connection publishes the value itself; no reader needs a rule. The last sentence, on `freeStreamSlots( )`, stays with a reason of its own (R4, text below) |
| `Http2ConnectionTask.h:2801-2806` | `Ready` "is what releases" `m_negotiated` | `Ready` follows the value's own publication |
| `Http2ConnectionTask.h:3102-3107` | "valid once state( ) is not Connecting" | safe at any time; delegates to the base; the reference names the value current at the call (R5) |
| `ConnectionPool.h:1121-1128` | testing `isReady` first is what makes the read safe | it is what decides which entries count (§5) |
| `HttpClientRequestTask.h:1898-1901` | read at the END "because that is when it is settled" | a request which fails before its connection's handshake completes reads `Unknown` |
| `ClientConnection.h:357-371` | "Unknown with no identifier until ALPN has resolved"; states no requirement | the requirement on every implementation, line for line (R5, text below) |

**`ClientConnectionTaskBase.h:364-369`** (R6), not line for line - the logic commit changes the file:

```
            /*
             * Written once, on the strand, by publishNegotiated( ), which then sets
             * m_isNegotiatedPublished. Off the strand it is read ONLY through negotiated( ), which
             * reads the flag first: a driver which IS this task answers ClientConnection::negotiated( )
             * by delegating to it, and never returns this member. A factory-built driver is given the
             * value at construction, below - the one moment it is in hand - and holds it const
             */
```

**The driver's class comment keeps its `freeStreamSlots( )` sentence** (R4).
- Its "for the same reason" points at the publication order this change-set removes, so it takes a
  reason of its own:

  > *freeStreamSlots( ) is an atomic because it too is read off the strand: it answers a question about
  > the session, which is strand state a caller may not touch.*

- The field comment at `Http2ConnectionTask.h:403-405`, "What an off-strand caller is allowed to read -
  see the class comment", stays true: the class comment goes on saying what an off-strand caller reads -
  `state( )`, `freeStreamSlots( )`, and `negotiated( )` through the base's publication.

**Left as they are, because they are still true:** `ConnectionPool.h:549-553` (the h2 driver's reads
are atomic loads and a reference, still, and take no lock); `Http1ConnectionTask.h:174-176` and the test
double's `TestClientConnectionTaskBase.h:397` (both `const` members, which a const member keeps safe);
`HttpClientRequestTask.h:868-873`, `:1636-1639` and `:1977-1981`, and `ClientConnection.h:602-608` and
`ClientConnectionTaskBase.h:608-623` (none states the rule or the premise); `TestClientContracts.h:1310`
and `TestHttpClientRequestTask.h:1531`, `:2466` (about `ClientResponse`'s single door and the probe's
refusal). The review's own search found three more, still true and left as well: `ClientTypes.h:69` and
`:100` ("before ALPN has resolved", which is when `Unknown` is still what a getter returns), and
`ConnectionPool.h:1746-1750` (a request submitted before ALPN resolves is bounced retryable on the
fallback), with `ConnectionPool.h:1552` (what `isReady` witnesses).

**`ClientConnection.h:357-371`, line for line** (R5). The block is 15 lines and stays 15.
- It keeps "one query and not two" and what the value fills, and shortens the history of the missing
  identifier to a pointer: `NegotiatedProtocol`'s own note already carries it.
- It adds the requirement on every implementation:
  - safe from any thread at any time;
  - `Unknown` with no identifier until the protocol is settled, then the settled value;
  - **the object a returned reference names is never written afterwards**, so a caller which wants a
    later value asks again. Today a held reference always named `m_negotiated` and saw later values,
    racily. After (a″) one taken before the publication names `m_unsettled` and stays `Unknown`. That is
    safe, and new, and it is the invariant every implementation now keeps. No reader holds one today:
    `ConnectionPool.h:1134` and `HttpClientRequestTask.h:884` read `.protocol( )` at once, and `:1906`
    copies. VERIFIED.
- The text, starting from the reviewer's draft - 15 lines, the widest 100 columns, which is the file's
  own widest:

```
            /**
             * @brief What this connection speaks, and the ALPN identifier which settled it
             *
             * ONE QUERY AND NOT TWO, so that the protocol and the identifier cannot disagree -
             * NegotiatedProtocol's own note says why, and why an empty identifier is a statement
             * rather than a gap. It fills BOTH ClientResponse::protocol() and negotiatedAlpn()
             *
             * A REQUIREMENT ON EVERY IMPLEMENTATION: safe from any thread at any time, with no rule
             * about state( ) first. Unknown with no identifier until the protocol is settled - by
             * ALPN in the TLS handshake, or by configuration for cleartext - and then the settled
             * value. The object a returned reference names is never written afterwards, so a
             * reference taken while Unknown stays Unknown: a caller which wants a later value asks
             * again. A driver built after negotiation holds its value const; the HTTP/2 driver,
             * which IS the establishing task, publishes it (ClientConnectionTaskBase.h)
             */
```

- **The evidence**, as CS-7 showed its own comment edits
  (`logs/astra3/cs7/connectionpool-comment-preprocess-identical.log`):
  - preprocess-identical output for two dependent translation units, before and after: one outside the
    21-module gate (`utf_baselib_httpclient2`) and one inside it;
  - a control which adds one line and must differ;
  - the comment-only checker, `astra4/lane1-chk-comment-diff.sh`, on the commit.

  If it cannot be shown, the gate takes the four modules outside the 21.

## 7. The tests

All five are in one new module (§7.4). `tls_stream_t` is the stranded TLS policy of the shared
`Http2DriverTlsProbe.h`.

**The plan's other control, the HTTP/1.1 driver's constant, needs no new case** (R10).
`Http1ConnectionTask.h` does not change: its value is a `const` member (`:174-179`), returned at
`:2570-2573`. `TestHttp1ConnectionTask.h:180` and `:187` assert it - `Http11`, no identifier - in
`utf_baselib_httpclient3`, which is one of the gate's 21 (`logs/astra4/deps-ClientConnectionTaskBase.txt`).
VERIFIED.

### 7.1 The deterministic getter case — the red for the contract

- Two unstarted test types, each with a method which writes the protected `m_negotiated` - `fromAlpn(
  "h2" )` - and publishes nothing: one derived from `ClientConnectionTaskBaseT< tls_stream_t >`, one from
  `Http2ConnectionTaskT< tls_stream_t >`.
- It asserts that the base's `negotiated( )`, and the driver's through `ClientConnection`, return
  `Unknown` with no identifier. Each half is checked without stopping the case, so a run shows both.
- **Today both return `h2`: a red which is certain, from a pure input** - one thread, no timing.
  Measured at `65cf086` and again at `eba918d`: both halves fail with `Http2 'h2'`
  (`logs/astra4/cs9/red-65cf086-run.log`, `red-8f1cec6-READ.txt`, `red-eba918d-run.log`).
- After the fix, each type also calls `publishNegotiated( )` with the value, and a thread started
  afterwards reads each getter and gets `Http2` and `"h2"`. The thread's start orders the read after
  the publication; what this pins is that a published value is what every reader gets.
- **On the driver, this publish half is the one control which separates (a″) from the withdrawn
  (a′)** (R3).
  - An unstarted driver reads `Connecting` (`Http2ConnectionTask.h:431`).
  - (a′) returned the default while `Connecting` (decision record, D1, `:77-79`).
  - (a″) reads the settled value from the moment it is published, before `Ready` (`:94-96`,
    `:134-136`). Here that is read with the state still `Connecting`.
  - No other case here tells the two apart:
    - the composed cases read after `Ready`, after `Closed`, or with nothing written;
    - the reader of §7.2 loops until the value is settled, so it passes under (a′) too.

    So a later "simplification" to return the default while `Connecting` would pass every other test
    in the module. INFERRED from those VERIFIED lines.

### 7.2 The ThreadSanitizer pair — the red for the race

*Rebuilt in r2: r1's construction reported in 1 run of 54. What r1 said, why it was wrong, and what
replaced it follow; the logs and the runtime's source are under `logs/astra4/cs9/`
(`tsan-red-READ.txt`, `tsan-src/`).*

- The real driver over TLS against `TlsEndingPeer` selecting `h2` (`AwaitTheClient`). The shared probe
  is derived in the module as `NegotiatedSignalProbe`, which signals from `onProtocolNegotiated( )`.
  `continueAfterConnected( )` calls that right after the write, in the same handler - and, with the fix,
  after the publication.
- **A reader thread, ordered after nothing the strand did, reads `negotiated( ).protocol( )` through
  `ClientConnection`.**
  - It is created before the driver is scheduled.
  - It waits until the case lets it go, which the case does, by a relaxed store, as soon as the probe
    signals. A relaxed store and load are not synchronization to the C++ model or to ThreadSanitizer,
    which is the only reason they are relaxed.
  - While it waits, it sleeps in 20 us steps, and at each step it takes a mutex which no other thread
    takes. That orders it after nothing but its own past.
  - Then it reads until the protocol is not `Unknown` - once, today - and signals. The case asserts it
    saw `Http2`.
- **r1's premise, corrected.**
  - **What r1 said.** r1's reader took no lock and read in a loop from before the driver started. It
    reasoned: "At most three threads touch that byte's word ... so ThreadSanitizer's four shadow cells
    still hold the write" - so every run would report.
  - **What was measured.** 1 run in 54 reported, all at `65cf086`. A count showed about 20,000
    concurrent reads in every run, so the reads were not missing.
  - **The shadow cells were enough here, but not for r1's reason** (R1). Shadow values are kept per slot,
    epoch, byte mask and kind, not per thread (`tsan_shadow.h`; the runtime files cited here are archived
    in `logs/astra4/cs9/tsan-src/`, with their checksums).
    - Before the reader reads, this byte's word holds four values (N1):
      - the allocation's 8-byte write (`tsan_mman.cpp:260-276`, then `tsan_rtl_access.cpp:633-639`) and
        the constructor's 1-byte write, both the constructing thread's;
      - the strand's write at `:656`;
      - the strand's own read at `Http2ConnectionTask.h:2755`. It is recorded, because
        `NegotiatedSignalProbe::onProtocolNegotiated( )` signals between the two: a mutex unlock, which
        advances the strand's epoch (`tsan_rtl_mutex.cpp:216-254`). Without that signal, and with trace
        logging off as it is here (`UtfArgsParser.h:540`), the read would fall in the write's epoch and
        `ContainsSameAccess( )` would skip it (`tsan_rtl_access.cpp:172-192`).
    - Four values fit the four cells, so none is evicted before the reader's read. That read meets the
      write while it checks them: `CheckRaces( )` reports before it would evict
      (`tsan_rtl_access.cpp:195-232`).
    - VERIFIED at the runtime's source, the library's and the test's (`TestNegotiatedPublication.h:476-481`,
      the signal before `base_type::onProtocolNegotiated( )`; `TlsTeardownTestUtils.h:111-118`, the
      signal's mutex). What r1 missed is not here.
  - **What r1 missed: the slots.** ThreadSanitizer v3 shares 256 slots among the threads
    (`tsan_defs.h:58`).
    - A slot handed from one thread to another keeps its sid and its epoch (`tsan_rtl.cpp:252-321`), and
      a thread learns that it lost its slot only when it next locks its slot - at a synchronization, or
      when it switches trace parts (`:357-375`, `:964-965`). N2: r3 said "only at its next
      synchronization", which omitted the second; the mechanism stands - reads are recorded under a lost
      sid until the thread notices.
    - When every slot's 14-bit epoch is spent, the whole shadow is reset (`:233-280`).
    - So a reader which synchronizes with nothing goes on recording its reads under a sid which another
      thread may own at a higher epoch. Once that thread's clock reaches the strand, the reads compare as
      ordered before the write (`tsan_rtl_access.cpp:218`). VERIFIED at the runtime's source.
    - That a TLS handshake spends enough to hand a waiting thread's slot over within the window is
      INFERRED (N2), not read: the source shows what spends a slot - 14-bit epochs, one per release
      (`tsan_defs.h:63-66`, `tsan_rtl_mutex.cpp:253-254`) - and the runs show misses.
- **Hence the construction above:** the read follows the write by tens of microseconds, from a slot of
  the reader's own. The rates, per committed reader - each commit's own runs first, then the same
  source's runs before it was committed:

  | Reader | At the commit | The same source, before its commit |
  |---|---|---|
  | `65cf086`: spins through the handshake (r1) | 1 of 54 | - |
  | `b644819`: let go once the opening write has ended, a round trip after the write | 50 of 51 (1 of 1; then 49 of 50, run 12 clean) | 51 of 51 |
  | `4451fd0`: let go by the strand's signal, just after the write - the reader now in the tree | 51 of 51 (1 of 1; then 50 of 50) | 51 of 51 (blob `a493b53`, the committed one) |

  - **How sameness is known** (N3). The "same source" column is corroborated, not recorded: the
    pre-commit batches record only `HEAD` and a modified file, not a hash of what ran
    (`tsan-red2-wt-x50/summary.log`, `tsan-red3-wt-x50/summary.log`). What corroborates it is equal
    binary sizes and identical report-frame offsets:
    - `tsan-red2-wt` and `b644819`: 16,024,656 bytes, with `:371` at `+0x8adaa8` in every report;
    - `tsan-red3-wt` and `4451fd0`: 16,037,352 bytes, with `:409` at `+0x8af49c` in every report.

    From now on every batch's header records `git hash-object` of the sources this change-set changes,
    the green batches included.
  - `b644819`'s window, a round trip, is long enough for a handover or a reset to lose the write's
    record, and that is how run 12's miss is explained - INFERRED (N2), not observed. `4451fd0`'s window
    has not lost it in 102 runs.
  - Every report is the reader's read (`TestNegotiatedPublication.h:409`) against the write at
    `ClientConnectionTaskBase.h:656`, made holding the task lock (M0 in the report is `TaskBase::m_lock`,
    first taken in `scheduleNothrow( )`).
  - The positive control reported in the same tree. The whole module under ThreadSanitizer gives that
    report and no other.
  - `eba918d` changes one comment line of the test, one for one, after these runs. Its code, and every
    line number in it, are `4451fd0`'s.
  - The logs are `logs/astra4/cs9/tsan-red-*`; `tsan-red-READ.txt` reads them.
- **What is certain and what is measured.**
  - The race is certain at the source.
  - What the instrument sees is not certain by construction. The committed reader can still miss in two
    ways (N2):
    - **a global reset** inside the window between the write and the read, which wipes the write's
      record (`tsan_rtl.cpp:233-280`);
    - **the reader landing on the strand's own slot.** When preempted, the reader re-attaches at its
      next private-mutex lock, to the least recently attached slot (`tsan_rtl.cpp:252-321`), and that
      slot can be the one the strand wrote under. A pair with the same sid is not compared at all:
      `CheckRaces( )` skips it (`tsan_rtl_access.cpp:208-214`). The record is not dropped; the
      comparison is skipped.
  - So this red is a measurement - 102 reports in 102 runs of the committed reader, which stands - with
    both ways a miss stays possible recorded, and it is paired with a green of 50 runs (§7.5).
  - The deterministic red for the contract is §7.1's.
- **Why the fixed run cannot report, which is certain by construction.**
  - The reader's first read loads the flag. When it reads `true` it synchronizes with the store which
    follows the write, so its read of the member is ordered after the write.
  - If it read `false` it would read `m_unsettled`, written before its thread was created, and read
    again.
- **`protocol( )` and not a copy of the whole value.** On today's code a torn `std::string` copy can
  crash where a byte read only reports; a report is the verdict. The flag gates the whole object, so
  what a `protocol( )` read shows holds for a copy. The composed cases (§7.3) are what copy the whole
  value, through the request task.
- **The build:** `BL_CLANG_ENABLE_RA_TSAN=1` in an emptied clang debug tree, `TSAN_OPTIONS` set
  explicitly, as CS-7's (`second_deadlock_stack=1` and the suppressions file, which holds one `deadlock:`
  entry and suppresses no race). The positive control is `utf_baselib_basictask` in the same tree, whose
  race at `TestBaselibBasicTask.h:127` must report. The tree is emptied afterwards.
- **The verdict is read from the report, not from the exit code** (R2).
  - **A red** is a report whose two accesses are this read (`OffStrandReader::run( )` →
    `NegotiatedProtocol::protocol( )`) and the write under `continueAfterConnected( )`
    (`ClientConnectionTaskBase.h:656`, through `NegotiatedProtocol::operator=`), in either order: at
    `65cf086` the write was the current access and the read the previous one; since `b644819` the
    reverse.
  - **A green** is no report at all.
  - **Any other report is a finding**: it is folded into this change-set and counts as neither.

### 7.3 The composed cases — U01's route, and its controls

- **The harness.** What dispatches a request onto an establishing connection is the pool, under
  `ridePreface` (`ConnectionPool.h:1733-1762`). Composed with the request task and a real driver, that
  pool is the session's harness, and those modules are over the target (`utf_baselib_httpclient5`,
  47.5 MB, and `…10`, 46.7 MB, each at a64). What the pool hands the request is the `Connecting` driver
  itself.
- **So the cases use a pool of one connection written in this module, `OneDriverPool`** (R7).
  - `utf_baselib_httpclient8`'s `OneConnectionPool` (`TestHttp1DriverTlsTruncation.h:130`) is that
    module's own and may not be included here.
  - It was not moved into the shared tree either. It records event strings and has no rendezvous, where
    these cases need a count of acquires, each release's outcome, and a wait for a release. Moving it would
    also change `httpclient8`'s files for no case of its own.
  - So `OneDriverPool` is a new class, not a copy. It answers every `acquire( )` with the driver,
    posted as the contract requires; counts the acquires; records each `releaseStream( )`'s outcome; and
    lets a case wait for the first release.
  - The request task and the driver are the real ones.
- **What `OneDriverPool` does not exercise, and where that is covered.**
  - **Not exercised here:** the pool's own decision to let a request ride (`ConnectionPool.h:1733-1762`),
    and its release accounting.
  - **The decision to ride:** `utf_baselib_h2client4`, `TestConnectionPoolRetiredEntry.h:141` and
    `:227` - a replayable request rides the preface, and an unreplayable one does not; and
    `utf_baselib_httpclient4`, `TestClientSession.h:2055` - the session's rider across the fallback.
  - **The release accounting:** `TestConnectionPool.h:1186` and `:1966`, in `utf_baselib_h2client4`.
  - VERIFIED: each case read at its line.
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
- **Measured: 37.9 MB at a64 clang debug**, 16.9 MB over the empty module's floor (39,731,232 bytes at
  `65cf086`, 39,771,872 at `eba918d`; `utf_objsize.py` in `logs/astra4/cs9/red-*-build.log`).
- **That is under the target at a64 and over it where the policy measures** (R8).
  `src/utests/AGENTS.md` sets the 40 MB target on x86 debug, and by the ratio win-x86 debug has shown
  over a64 clang debug (1.11 to 1.17) this module is about 42 to 44 MB there - INFERRED, not measured.
  So the reason is recorded in `Main.cpp`, as the policy asks, and as `utf_baselib_h2client9` and `…10`
  record theirs.
- **What it pays for:** the HTTP/2 driver over the stranded TLS policy, as r1 expected. The request
  task, which r1 expected to cost more, is the small part: `utf_baselib_h2client10` carries the driver
  and no request task, and is 37.6 MB.
- **A split cannot lower it**, since every case instantiates the driver.
- **The D-section of `windows-matrix-handoff.md`** gives the a64 size and the x86 estimate. It also asks
  whether the module builds at x86 `ccl16` release, which object size does not govern
  (`src/utests/AGENTS.md`, "The x86 release caveat").
- `utf_baselib_httpclient14` stays reserved and unused.

### 7.5 Runs

**Which tree** (R9, as the orchestrator refined it).
- **Every run which validates the change-set is taken on the tree after the comment commits** (§8), so
  that what it validates is what merges. The comment edits outside `ClientConnection.h` move lines, and
  with them the `__LINE__` values `BL_CHK_T` and `BL_NOEXCEPT_END` bake into the code after them: a run
  on the tree before them is not a run on what merges.
- **Before the comments, only what is needed to see the fix work:** one ordinary build and run of the
  module after the logic commit, and again after the publish half is added.
- **Every run's header records `git hash-object` of the sources this change-set changes** (N3), so that
  what ran is recorded and not only corroborated.

**What runs, on that tree:**
- **§7.1 is deterministic.** It is shown red once, which is done (`65cf086`, and again at `eba918d`),
  and green once, by construction.
- **§7.2 is a red/green pair which is not deterministic** (R1). It takes the default, 50 runs each way.
  - **The red is done:** 50 of 50 in `4451fd0`'s batch, plus the same blob's 50 of 50 and the two single
    runs (§7.2's table). The tree was emptied before them, and the positive control was built and run in
    it.
  - **The green is 50 runs after the fix, with no report in any**, in a tree emptied again, with the
    positive control built and reporting in it.
  - **The green weighs only as much as the red's rate.** Against r1's reader, which reported in 1 run of
    54, fifty clean runs would have shown nothing. The committed reader missed in none of its 102 runs,
    so fifty clean runs in a row would need a miss rate the red never showed.
  - **Then the whole module once, in the same instrumented tree** (R2). The composed cases are what copy
    the whole value through the request task. Any report there is judged as §7.2 says: a finding, not a
    red or a green.
- **§7.3 green once**, then **50 runs of the module's five cases**: the default regression count, since
  no rate is the criterion here.
- **Then the existing modules, one at a time:** `utf_baselib_h2client`, `…2`, `…3` and
  `utf_baselib_httpclient`.
- **Then tier 1**, with every line of its report explained in the lane's journal. At `eba918d` it
  reports six lines, all additions: the five cases (C1) and the module's helper namespace (C6)
  (`logs/astra4/cs9/tier1-eba918d.log`). The orchestrator re-captures the baseline after integration.

## 8. Commits

**Landed on the branch before the agreement:**

1. `8f1cec6` this note, r1.
2. `65cf086` tests: the new module - §7.1's red half, r1's reader, §7.3's characterizations.
3. `b644819` and `4451fd0` tests: the reader rebuilt twice (§7.2).
4. `eba918d` comments: a count in the reader's comment corrected.
5. `a43477f` this note, r2.
6. `886ac88` this note, r3.
7. This revision, r4, with the agreement line.

**To come, in order:**

1. *(Done in r4: the agreement line.)*
2. **Logic:** `ClientConnectionTaskBase.h` (flag, default, helper, getter, `<atomic>`) and
   `Http2ConnectionTask.h` (the getter delegates). Run only to see the fix work (§7.5): the module
   once, with §7.1 green.
3. **Tests:** §7.1's publish half, and the module once, to see it green.
4. **Comments, one commit per file, per §6.**
   - Each commit is checked comment-only (`astra4/lane1-chk-comment-diff.sh`).
   - `ClientConnection.h`'s is shown preprocess-identical, with its control.
5. **On the tree after 4, every validating run of §7.5, in its order:**
   - §7.1 green;
   - §7.2's 50 green and the whole-module instrumented run, with the positive control;
   - §7.3 green, and the module's 50 runs;
   - the four existing modules;
   - tier 1.
6. **The D-section of `windows-matrix-handoff.md`:**
   - the deterministic cases run on Windows; ThreadSanitizer does not;
   - the module's a64 size and its x86 estimate;
   - whether it builds at x86 `ccl16` release (R8).

**Agreement: 2026-09-29.**
- **Reviewed:** two rounds by an Opus reviewer (`astra4/reviews/cs9-note-r1.md`, rounds 1 and 2), with the
  orchestrator's own review of r1 (`cs9-note-orchestrator-r1.md`).
- **Agreed by the reviewer** on revision 3 (`886ac88`), with changes N1 to N3 (P3, text only), taken in
  this revision as the review words them.
- **Agreed by the orchestrator**, who checks this revision against the review.
- **The mechanism, §1 to §6, is r1's.** The rounds changed §7 and the text only.

## 9. What the checkpoint review corrected — 2026-09-29, lane 1

*Added after the agreement, for CS-9's checkpoint review (`astra4/reviews/cs9-checkpoint-r1.md`, C2, which
carries the orchestrator's O1). Nothing here changes a decided shape; each item corrects a figure of §7.4
with what was measured at the source. MB is 2^20 bytes, as `src/utests/AGENTS.md` counts it.*

- **The module closed at 38.0 MB**, not §7.4's 37.9 MB: 39,802,696 bytes at `e55a92d`
  (`logs/astra4/cs9/green-e55a92d-build.log`), 17.0 MB over the empty module's floor.
- **On x86 that is about 43 to 44 MB**, by row 5c's ratio in one unit, 1.13 to 1.17
  (`windows-matrix-handoff.md`, B14). §7.4's lower bound, 1.11, divides an x86 figure by a 10^6-byte
  one. Still inferred, not measured.
- **`utf_baselib_h2client10`'s 37.6 MB is a 10^6-byte figure.** In this unit it is 35.9 MB, 37,607,912
  bytes at `d13859c` (`logs/astra2/cs6/tip-d13859c/summary.log:6`). So the request task, the pool of one
  connection and the five cases cost about 2.1 MB. The conclusion stands: the driver is the weight, and a
  split cannot lower it. The review's 36.1 MB is 37,876,496 bytes from a build of `fff158c` with
  uncommitted changes (`logs/astra2/cs6/dl31/a2-h2client10-build.log:42`).
- The same corrections are in `UtfBaselibH2Client11Main.cpp` and in the handoff's D2.
