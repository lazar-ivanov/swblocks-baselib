# S6R.1 — design for the nine contained fixes

**Status:** design, 2026-09-22. **Nothing implemented.** This is the artifact that must be agreed
before code is written, per the review loop. **Agreed 2026-09-22 — see §11a for by whom, on what
text, and what the agreement does not claim.** **Implementation reviewed 2026-09-22 — see §11b;
"nothing implemented" above is the status at design time and is left as written.**

**Scope:** the nine findings grouped as R1 in `astra-review-verification-record.md` — H02, H03a,
H13, H14, H17, H26, H04b, H27, H28. Chosen because they touch **disjoint functions**, are each
deterministically testable, and between them remove a silent duplicate POST, a shutdown segfault,
two protocol violations, a wedged stream window and an invalid CONNECT authority.

**What this design is not.** It is not a restatement of the findings — read the verification record
for those. It is the *intended change* for each, the reason that shape was chosen over the
alternatives, and what could go wrong.

**Provenance.** H02's replay chain, H26's rendering and H27's comparison
were read at the source by the orchestrator. The remainder come from the verification lanes' reports
and are marked where the author has not personally re-read them — a reviewer should weigh those
harder.

---

## 1. H02 — mark the request as possibly-sent when the write starts, not when it completes

**Defect.** `m_requestBytesWritten` is set only in `onWriteCompleted` when `bytesTransferred != 0`.
`onPeerClosed` passes its negation as `isRetryable`. Bytes can be on the wire, and acted on, long
before that handler runs. `chkRequestMayBeReplayed` then returns `true` from the `isRetryable` limb
**before** reaching `isIdempotentMethod`, so a POST is replayed without the idempotency gate the
design put there. *(Both links read by the author.)*

**Change.** Set the flag **immediately before** `async_write` is issued in `onStartRequest` — not
after. Keep the existing assignment in `onWriteCompleted` — harmless and now redundant. **Rename the
member to say what it now means**: `m_requestMayHaveBeenSent`.

**Why before and not after.** The initiating call can throw; its `catch` completes the operation and
fails the task, which reaches the other reader of this flag. A throwing initiator is precisely a
case that cannot be proven unwritten, so the flag must already be set. The h2 precedent marks before
the write goes out for the same reason. Zero cost, strictly more conservative.

**Why this shape.** The HTTP/2 driver already decides the identical question this way —
`isHeadersProduced` is set at hand-off (`Http2ConnectionTask.h:737`) and consumed at `:1192`, with a
comment saying a block handed to a failed write "is not provably unwritten". Copying a rule this
library already states beats inventing one, and it is what design 5.4/D6 describe.

**What could go wrong, and why it does not.** The two h1 paths that legitimately claim "provably
unwritten" — the `isClosing()` check and the render failure — both `return` **before** `async_write`
is issued, so neither regresses. The change is strictly more conservative: it can only turn a
would-be retry into a non-retry, never the reverse.

**Deliberately not done here.** The exact answer ("zero bytes escaped, so retry is safe") needs the
write-completion barrier of H01, which is S6R.2. This fix is the conservative approximation and
S6R.2 will revisit these lines.

**Test — corrected; the earlier draft named a fixture that does not exist.** There is no fake stream
policy in the tree, and building one against the `TcpBaseTasks` contract is not cheap.

Use the strand instead — but **both posts must be issued from a handler already running on the
strand**, which an earlier draft left unsaid and which is the whole of the determinism. (The
unqualified wording was the reviewer's own, from round 1; the reviewer corrected it in round 2.)

Issue `submit( )` and the probe post from inside a strand handler. Strand FIFO then orders
`[ onStartRequest, probe ]` before either runs, and asio's rule that a completion handler is never
invoked from inside the initiating call puts `onWriteCompleted` after both. From the probe, a
subclass calls the protected `onPeerClosed( )`: the parser exists, `parseEof` fails, and
`finishStream` carries the flag's negation out to the sink's `onClosed`.

**Why the unqualified version does not work.** If the probe is posted from the test thread, it races
the strand: should `onStartRequest` finish and release before the post lands, the write completion —
already queued, and on loopback complete in microseconds — can be dispatched first. The flag is then
already set, and the "red" run is green. Ordering by thread scheduling is not ordering.

Red before (flag false → `isRetryable == true`), green after.

## 2. H03a — flush with wait, and never reset the queue outside construction and destruction

*(Retitled. The original heading — "hold the execution queue in the action batch" — named the first
draft's shape, which the body below rejects. A title naming a rejected fix is a false premise on the
page for anyone reading by headings.)*

**Defect.** `runActions` dereferences the member `m_eqConnections` outside the pool lock;
`disposeInternal` flushes and `reset()`s that same non-atomic pointer, also outside the lock. A batch
suspended between collection and execution can then null-deref or use-after-free.

**Change — ONE LINE: flush with wait, and never `reset( )` the member in `disposeInternal`.**

Replace `forceFlushNoThrow( false ); m_eqConnections.reset( );` with
`forceFlushNoThrow( true /* wait */ )`. Nothing else: no field on `Actions`, no move-out under the
lock, no null-check in `runActions`.

**Two earlier drafts of this section were wrong, and the second was worse than it looked.** The
first changed only the reader, leaving the member's `reset( )` unordered against a batch's copy. The
second added a move-out under the lock — mechanically correct, but the reviewer traced the residual
to its end in round 2 (the author then verified both links at the source) and **it still crashes,
later and elsewhere**: `dispose( )` does `m_observerThis -> disconnect( );
m_observerThis.reset( );`, a post-dispose push binds that now-null proxy into the ready callback,
and `onReadyObserver` dereferences it **unguarded** (`observerThis -> tryAcquireRef< >( )`) when the
orphan task completes. The `if( ! m_observerThis )` guard below it is inside `onReady`, reached only
*after* that dereference. So the second shape moved the segfault out of shutdown and into the
orphan's completion. Verified at the source by the author.

**Why the one-line shape is strictly stronger.** `forceFlushNoThrow( true )` and `dispose( )` pass
**flag-for-flag identical** arguments to `flushInternal` — `wait`, `discardPending`,
`nothrowIfFailed`, `discardReady`, `cancelExecuting` all true — so the join `dispose( )` performs
today happens at the same point, outside the lock and after the cancels, **while the observer proxy
stays connected**. The member is then written only in the constructor and destroyed by its own
`~ObjPtrDisposable` at pool destruction, which is already the path a never-disposed pool takes. With
no write to the member outside construction, **there is no reader race left to fix — by construction,
not by protocol.**

**One precision on "already the path".** That is true of the *timing*, not of the frame. Today a
never-disposed pool disposes the queue from inside `disposeInternal` in the destructor **body**,
with every member alive. Under shape (A) a disposed-then-destroyed pool disposes it during **member**
destruction, after the members declared later — the maintenance timer and its flags, the connection
count, the disposed flag and the stats — are already gone, while the lock and the two maps are still
alive. Benign, because no connection task references the pool; stated so the claim stays exact.

**What it trades.** The queue object lingers until the pool is destroyed (bytes), and its `dispose( )`
moves to the destructor — a path that already exists and is already exercised.

**Residual — two faces, and an earlier draft of this paragraph stated only one.**

1. *A push landing after the flush returns.* The orphan task runs to its own idle lifetime rather
   than being admitted or refused; the pool destructor joins it if it is still alive.
2. *A push landing **during** the flush's wait.* `flushInternal`'s wait is a condition-variable
   predicate on `! hasPendingOrExecuting( )` which **releases the queue lock**, and the
   `cancelExecuting` sweep has already run once before it. Such a push is therefore accepted,
   scheduled, and not cancelled — so **`dispose( )` blocks until that orphan completes on its own**.
   For an attempt task the sweep already cancelled, that is immediate; for a driver scheduled from
   the fallback path, which the sweep never touches, it is the driver's idle lifetime —
   `ConnectionPoolPolicy::idleTimeout`, **300 s by default**, and unbounded if a caller disables it
   or builds the pool with a factory that does not pass it on.

**Face 2 is pre-existing and this change does not introduce it.** Today's `reset( )` → `dispose( )`
→ `flushInternal( wait = true )` releases the lock inside the same predicate wait, and the observer
proxy is nulled only after that flush returns, so a push landing in today's wait produces exactly
the same delay. What shape (A) removes are the **crash** faces: today's null member and the
use-after-free of a copy racing its `reset( )`, and — had it shipped — the second draft's null-proxy
completion. (An earlier wording counted "two crash faces, the null member and the second draft's
null-proxy completion", which listed a face of a rejected draft as if it were in the tree and left
out the use-after-free that is.) The delay is **not** closed by S6R.3, and this sentence used to say
it was — *"The delay is closed by S6R.3's admission protocol, with H04a."* Corrected 2026-09-22 by
the S6R.3 design review: S6R.3 §4.2 finds that H04a (an acquire on the writer's publishing store)
and this residual (an admission gate with a drain) share a site and nothing else, and that H04a does
not shorten the delay at all. What S6R.3 identifies instead is a one-line reduction of the delay's
*duration* — `disposeInternal( )` sweeping `entry -> driverConnection` as well as `attempt.task`,
so a driver pushed during the wait is already cancel-requested and completes at once — which belongs
to whichever change-set owns the pool's disposal. The in-tree comment at
`ConnectionPool.h:2146-2152` carries the same sentence and is H04a's change-set to amend.

So the honest claim is: **this buys the crash; in the narrower sub-window a pre-existing shutdown
delay bounded by the idle lifetime remains.**

**Test.** No deterministic pin exists. **A TSan run over the pool module with a racing `dispose( )`
is REQUIRED** — the build supports it (`BL_CLANG_ENABLE_RA_TSAN=1`) and the recipe is in the
timer-cancel record §5. Under this shape the run should be clean **by construction**, which is what
it corroborates; detection is interleaving-dependent, so a clean run is **not** the proof — the
construction argument is.

## 3. H13 — subtract the PRIORITY fields from the first fragment's budget

**Defect.** `queueHeaderBlock` gives the first fragment the whole of `m_peerMaxFrameSize`, then
`serializeHeaders` adds five octets of PRIORITY on top, so the frame exceeds the peer's limit by up
to five. *(Lane-reported; author has not re-read the arithmetic.)*

**Change.** Reduce the first fragment's budget by `PRIORITY_FIELDS_SIZE` when the **`priority`
parameter**'s `isSet` is true — key it off the parameter, not off "the profile". Two callers pass
different things: the request path passes the profile's priority, the plain header path passes none.
Test what the serializer tests. No underflow is possible: `m_peerMaxFrameSize` can never be below
16384, which the SETTINGS validation already enforces.

**Note on the review's wording.** The overshoot is *up to* five, not exactly five — one to four when
the block falls just below the limit. The fix is the same either way.

**Test.** Extend the existing fragment-boundary case with a priority profile and assert every
produced HEADERS length is `<= peerMaxFrameSize()`.

## 4. H14 — flush the stream window after crediting padding

**Defect.** Padding is charged to the stream window and immediately marked consumed, with no flush.
The only flush site is application-driven, and a padding-only DATA frame delivers no bytes to the
application, so nothing ever calls back. The stream wedges; the connection window recovers **only if
something else on the connection is consumed or refused** — see the Change below, where that
qualifier turns out to matter more than this finding states.
*(Lane-reported.)*

**Change — BOTH windows.** An earlier draft flushed only the stream, which would have fixed the
stream and left the **connection** wedged in the same scenario. (Found by the reviewer in round 1;
verified at the source by the author before the change was made.)

Call **both** `flushStreamWindowUpdate` and `flushConnectionWindowUpdate( false )` at the
post-registry site, after the padding has been credited. The existing `shouldSendWindowUpdate`
threshold inside each prevents frame spam.

**The connection half is a no-op, and the justification below it was FALSE.** Corrected 2026-09-22
by the implementing lane, and settled by its own red run.

The paragraph here used to read: *"The connection padding credit has no flush either, and none of
the four existing `flushConnectionWindowUpdate` sites — `consumed( )`, refused frame,
judge-rejected, reap-closed-streams — is reachable from an accepted padding-only frame … the whole
connection wedges, which is worse than the stream-level defect this finding names. Verified at the
source by the author."*

**It is reachable.** `handleData( )` ends in `reapClosedStreams( )`, which ends in
`flushConnectionWindowUpdate( false )`. So the connection window is advertised on every DATA frame,
padding-only included, and was never wedged. The red run confirms it: the **connection** assertion
passed and only the **stream** assertion failed.

**How the error was made, since it is the instructive part.** The author enumerated the four call
sites and concluded none was reachable, without tracing whether `handleData( )`'s own path reaches
one. Reading call sites in isolation rather than following the function to its end is the same
failure this project has recorded repeatedly — and here it survived a reviewer who proposed it and an
author who believed he had verified it.

**The connection flush is kept anyway**, deliberately: it is harmless, threshold-gated, and keeps
credit and advertisement at a single site. It is not load-bearing, and no future reader should treat
it as fixing anything.

**Boundary, restated more precisely than the earlier draft.** A padded frame carrying even one real
byte already unwedges the stream, because `consumed( )` flushes the single `pendingCredit` counter
including all accumulated padding. So gating the new flushes on `dataSize == 0` is *permissible* but
not required — an unconditional flush at that site is threshold-gated and harmless. Prefer the
unconditional form: fewer branches, same behaviour.

**Self-guarding on END_STREAM — corrected 2026-09-22 by the implementation review.** This paragraph
used to say: *"If the frame carried END_STREAM, `canSend( WINDOW_UPDATE )` fails and the stream
flush returns without emitting."* That is true only when the END_STREAM **closes** the stream — the
local half already ended, which is every request without a body and every request whose body was
fully sent. While the local half is still open (a body still uploading when the peer answers early)
the stream becomes half-closed (remote), where `canSend( )` returns true
(`StreamStateMachine.h:358-361`), and the flush emits a threshold-gated WINDOW_UPDATE that is legal
(RFC 9113 5.1 and 6.9) and useless. No behaviour defect — `consumed( )` already emits the same frame
in the same state, and the connection window took its own padding credit at the top of
`handleData( )` — but the claim was stated without its condition, and the in-tree comment repeats
it (§11b.5, item 2). Either way `reapClosedStreams` credits the still-outstanding octets of a closed
stream to the connection. No special case needed.

**Placement constraint — the flushes must go BEFORE `reapClosedStreams( )`.** The stream flush takes
a `StreamContext&`, and `reapClosedStreams` erases contexts. A lane that appends the flushes at the
end of the function would pass a dangling reference on every END_STREAM frame.

**A comment in the tree becomes false and the lane owes its amendment.** A session test states "a
WINDOW_UPDATE is sent when the CONSUMER took the bytes, never when they arrived". After this change,
padding credit is advertised on arrival. That test uses unpadded frames and stays green, so nothing
fails — which is exactly how a false comment survives. Amend it, or state the exception at the new
test.

**Test.** Red before is deterministic — zero WINDOW_UPDATE frames today. The existing frame counter
is by type only, so assert **per stream id**: a WINDOW_UPDATE for stream N *and* for stream 0.

## 5. H17 — reject nonempty DATA on a bodyless response

**Defect.** `judgeDataFrame` has no arm for "this response may not carry content". A `204`, `304` or
response to `HEAD` carrying DATA is accepted and delivered. *(Lane-reported.)*

**Change.** One more arm in `judgeDataFrame`: `expectsNoContent && dataSize != 0` → malformed.

**Two boundaries the fix must respect.**
1. **Zero-length DATA with END_STREAM stays legal** on these responses — it is how a bodyless
   message completes.
2. The arm must run **before** the registry is told, so the RST_STREAM remains sendable and the
   engine's contract 1 holds.

**Test — the control does NOT already exist, contrary to an earlier draft.** No test feeds a
zero-length DATA with END_STREAM after a 204/304/HEAD response, and there is **no HEAD request in the
h2 session tests at all** — the request helper is GET-only. So the change-set owes: three rejection
cases (HEAD, 204, 304), the legal empty-completion control written from scratch, and a HEAD request
path in the test helper. Cheap, but not free, and it must be budgeted rather than assumed.

## 6. H26 — bracket an IPv6 literal when rendering CONNECT

**Defect.** `HttpConnectNegotiationT` concatenates `originHost << ":" << originPort` for both the
request target and `Host:`. `ConnectionKey::fromUri` stores `uri.host()`, which has already had its
brackets stripped. So `https://[::1]:443` through a proxy emits `CONNECT ::1:443`. *(Read by the
author.)*

**Change.** A small render helper that brackets when the host contains `:`, used at both
concatenations.

**What must not change.** The **unbracketed** form is what the resolver takes, what the SNI
decision is computed from, and what certificate verification matches. Bracket at render time only —
do not normalise the stored host.

*(Reviewer's precision, raised in round 1 and applied in round 3. The sentence used to say the
unbracketed form is "correct for SNI". More exactly: SNI is **not sent** for a host that parses as an
address — `TcpSslBaseTasks.h` tests `make_address( hostName )` before `SSL_set_tlsext_host_name` —
and that decision is computed from the bare form; a bracketed host would fail to parse and SNI would
go out *with* brackets, which is worse than a bare literal. Verification uses the iPAddress SAN
entries. So the bare stored form is what makes the no-SNI-for-literals rule fire — a stronger reason
not to normalise it than "correct for SNI" conveyed.)*

**Test.** Nearly free: the existing tunnel test already asserts exact CONNECT bytes for a DNS name;
add an `"::1"` sibling. Pure unit, no network.

## 7. H04b — test `isReady` before reading `negotiated()`

**Defect.** `effectiveMaxConnectionsPerKey` evaluates `connection->negotiated().protocol()` before
`entry->isReady`, so it can read a value a Connecting driver is still writing.

**Change.** Swap the `&&` operands so `entry->isReady` is tested first. One line.

**Why only half.** H04a — the unsynchronised publication of the driver pointer itself — is a
genuinely different size and is deferred to S6R.3. Half B reads an enum; half A is smart-pointer
publication. This change-set takes the free half and says so.

**Test.** Only under TSan with a concurrent case. Justified by construction.

## 8. H27 — give the cookie merge its own byte-exact comparison

**Defect.** `mergeCookieValues` drops a jar cookie whose name matches a caller's **case-insensitively**.
RFC 6265 cookie names are case-sensitive, so a caller's `sid` suppresses the jar's `SID`. *(Read by
the author.)*

**Change.** A `containsExact` helper used by `mergeCookieValues` only.

**The trap this must avoid.** The shared `contains()` has another caller where case-insensitivity is
**load-bearing**: the Accept-Encoding intersection, where a profile's token may be cased differently
from the registry's lower-cased key. **Do not make `contains()` exact** — the obvious fix breaks a
profile listing `GZIP`. (A third caller, header placement, compares already-lower-cased names, so
case-insensitivity is redundant there rather than load-bearing; an earlier draft called it
load-bearing. The conclusion is unchanged.)

**Supporting consistency argument.** The jar's own identity comparison is already byte-exact, so it
stores `sid` and `SID` as two cookies and emits both. Only the merge collapses them — which makes
this a defect in the merge alone, not a disagreement between two policies.

**Test.** `buildRequestHeaders` is static and unit-testable: assert `sid` and `SID` coexist, and
that a true same-name override still wins.

## 9. H28 — refuse a non-HTTP write that would replace an HttpOnly cookie

**Defect.** `setCookie` rejects an incoming cookie carrying HttpOnly in non-HTTP mode, but never
inspects whether the **existing** cookie it would replace or delete is HttpOnly. RFC 6265 §5.3
step 11, sub-step 2 requires the opposite. The read side is already correct.

**Change.** Check the existing entry's `isHttpOnly` in the replacement and deletion branches.

**Reachability, stated honestly.** The session always passes `isHttpApi = true`, so this is
unreachable through the session and is a public-API correctness fix. It was recorded against S2.7 and
not done; this closes it.

**Test.** Pure unit on the jar.

---

## 10. Why these nine are one change-set

They touch disjoint functions across **six** source files (an earlier draft said seven). The only
interactions are sequencing, not conflict: H13 edits `queueHeaderBlock`, which S6R.3's H10 will later
restructure; and H02's lines will be revisited by S6R.2's H01 barrier. Neither is a reason to split.

**One shared invariant, checked:** H14 and H17 both concern a DATA frame's window accounting. With
H14's flushes at the post-registry site, a frame H17 rejects never reaches them, and H17's reject
path already flushes. No conflict.

**Three hazards are recorded above rather than discovered during implementation:** H27's shared
`contains()`; H03a buying only the crash, with the residual batch's task actually *running*; and
H14's connection-window half, without which the fix makes the stream case better and leaves the
connection case wedged.

## 11. Acceptance

- Focused modules, clang debug, in the lane; **one module at a time**.
- Then clang **and** gcc release plus the whole-suite G1 gate, by the orchestrator.
- Every fix that claims a test above must ship that test, shown **red before and green after** —
  nothing in this change-set may rest on "the suite still passes", which pins nothing about a fix.
- H03a and H04b have no deterministic test; they are justified by construction and that is stated
  rather than papered over.
- **A TSan run over the pool module, with a racing `dispose( )`, is REQUIRED** — not opportunistic.
  Build with `BL_CLANG_ENABLE_RA_TSAN=1`. The **recipe** is in
  `http2-driver-timer-cancel-cross-thread-race-record.md` §5; the **mandatory positive control** —
  `utf_baselib_basictask`, its known report at `TestBaselibBasicTask.h:127`, exit 66 — is in that
  record's §1 and closing tables, not in §5. An earlier draft cited §5 for both. Record the result
  with the sentence
  that **a clean run is not a proof** — detection is interleaving-dependent, and what carries H03a
  and H04b is the construction argument. Under H03a's one-line shape the run should be clean by
  construction, which is what it corroborates.

## 11a. Readiness

**Agreed ready for implementation, 2026-09-22**, by the orchestrator and by the independent design
review, contingent on exactly the four edits that review named — the H03a retitle, the two-faced
residual, the "already the path" precision, and the positive-control citation — all of which are
applied above.

The design took **three shapes for H03a** before this one. The first fixed only the reader; the
second added a move-out under the lock which was mechanically correct and **still crashed**, moving
the segfault from shutdown to the orphan task's completion. Both are recorded in §2 with what each
got wrong. That history is the reason this section exists: the gate is that two readers agree, and
neither reader got H03a right alone.

**What agreement does and does not mean.** It means the intended change is correct, complete, and
bounded, and that a lane may start. It does **not** mean anything has been demonstrated — nothing in
this design has been built or run, and §11 lists what each fix owes before it can be called done.

**Reviewer's confirmation, 2026-09-22 (Claude Fable 5.1), on commit `7389c7b` read against the
source.** The four contingent edits are applied and correct. On top of them the reviewer added, under
the maintainer's process change of the same date and uncommitted for the orchestrator's review: three
provenance notes (H02's test wording, H03a's residual trace, H14's connection half), two tightenings
inside face 2 of the H03a residual (the policy field's full name and the factory clause; the crash
faces recounted), and the H26 SNI precision carried from round 1. None changes a decision. With those
in, **the reviewer agrees the design is ready for implementation.**

What the final round verified at the source, so this agreement is checkable rather than taken on
trust:

- the block shape (A) replaces is exactly `ConnectionPool.h:2118-2123`, and after the change `:841`
  is the only write to `m_eqConnections`;
- `forceFlushNoThrow( true )` (`ExecutionQueueImpl.h:1420-1432`) and `dispose( )` (`:1540-1555`)
  pass flag-for-flag identical arguments to `flushInternal`;
- `flushInternal`'s wait is a predicate loop on `! hasPendingOrExecuting( )` that releases the queue
  lock and is woken only by `onReady`'s `notify_all( )` (`:569`), with the `cancelExecuting` sweep
  run once before it — which is both why `forceFlushNoThrow`'s `BL_ASSERT( false == wait ||
  isEmptyInternal( ) )` cannot trip on a residual push and why face 2 of the residual exists;
- the crash chain of the rejected second draft: `dispose( )` nulls `m_observerThis` (`:1557-1560`);
  `pushInternalNoLock` (`:717`) has no disposed gate and ends in `padExecutingQueueNothrow( )`
  (`:778`), which binds the null proxy (`:697-698`); `om::copyAs( nullptr )` returns null without
  throwing (`ObjModel.h:144-156`); `notifyReadyImpl` invokes the callback unconditionally
  (`TaskBase.h:730`); `onReadyObserver` (`:426-448`) dereferences it through `std::unique_ptr`'s
  unguarded arrow (`SafeUniquePtr` overrides nothing there);
- the pool's member order behind the destructor-frame precision (`ConnectionPool.h:801-810`);
- `ConnectionPoolPolicy::idleTimeout` defaults to 300 s (`:175`, `:302`), and both drivers arm it
  while holding no stream (`Http1ConnectionTask.h:1356-1361`, `Http2ConnectionTask.h:2534`);
- the H02 probe's premises: `plain_stream_t` is `TcpSocketAsyncStrandedBase`
  (`TestHttp1ConnectionTask.h:743`) and `submit( )` posts `onStartRequest` through the stream's
  executor (`Http1ConnectionTask.h:1484-1530`);
- the H14 placement premise: `reapClosedStreams( )` erases contexts (`Session.h:3304`).

---

## 11b. Implementation review, 2026-09-22

**Reviewer: Claude Fable 5.1, on the uncommitted working tree of `lazari2` @ `c90f84e` — the same
change as the twelve commits `5114f20..568d97f` on `s6r1` over `4aeced3` — read against the design
above including the §4 correction.** Every changed function was opened at its signature and read to
its end; every line reference below is to the working tree. Nothing was built or run: the release
passes and the whole-suite gate were compiling during this review and are §11's, not this section's.

**Verdict: the code matches the design on all nine fixes, boundaries included, and the reviewer
agrees it may land** — contingent on one two-word correction to a false comment in a test
(§11b.5, item 1), which the orchestrator applies since the reviewer may not touch `src/`. Everything
else below is a verification, a precision the reviewer has already applied to this document, or a
proposal, and each is marked which.

### 11b.1 The code against the design, fix by fix

**H02 — matches, on the boundary that matters.** `m_requestMayHaveBeenSent = true` is at
`Http1ConnectionTask.h:687`, after both paths that legitimately claim "provably unwritten" have
returned — the `isClosing( )` check (`:580-596`) and the render `catch` (`:636-658`) — and before
`beginOperation( )` (`:689`) and the `async_write` `try` (`:692-694`). A throwing initiator, whose
`catch` completes the operation and fails the task (`:712`), therefore reaches
`onTaskStoppedNothrow( )` (`:1490`) with the flag set. The redundant assignment in
`onWriteCompleted( )` is kept (`:727-732`). Both `finishStream` calls in `onPeerClosed( )`
(`:1004-1007`, `:1013-1019`) and the terminal path carry the negation; `finishStream` resets it
(`:1147`). The rename is complete: no `m_requestBytesWritten` remains under `src/`.

**H03a — exactly one line, no reset.** The block at `ConnectionPool.h:2154-2157` is
`if( m_eqConnections ) { m_eqConnections -> forceFlushNoThrow( true /* wait */ ); }`. The member is
written once, at `:841`; its only other reader is `runActions( )` (`:1769`). The flush is outside the
pool lock — the `BL_MUTEX_GUARD( m_lock )` scope of `disposeInternal( )` runs `:2038-2100` and closes
before the answers are posted, the timer is cancelled, the attempts are cancelled and the queue is
flushed. The two claims the shape rests on hold at the source: `forceFlushNoThrow( true )`
(`ExecutionQueueImpl.h:1420-1436`) and `dispose( )` (`:1540-1562`) pass the same five `true` flags
to `flushInternal( )`; and `~ObjPtrDisposable( )` (`ObjModel.h:939-960`) calls `dispose( )` through
`tryQI< Disposable >` before releasing, which is what satisfies the queue destructor's
`BL_ASSERT( ! m_observerThis )` (`ExecutionQueueImpl.h:293`) for a disposed-then-destroyed pool.
`forceFlushNoThrow`'s own `BL_ASSERT( false == wait || isEmptyInternal( ) )` cannot trip: the
predicate wait returns holding the queue lock, `discardReady` clears `m_ready` under it, and the
assert runs before the lock is released — no push can land between.

**H04b — matches.** `entry -> isReady` is the first operand at `ConnectionPool.h:970`, then the
pointer, then `negotiated( ).protocol( )`.

**H13 — keyed off the parameter.** `queueHeaderBlock( )` computes
`firstMaxFragment = maxFragment - FrameCodec::prioritySize( priority )` from its own `priority`
parameter (`Session.h:3667`), takes `first = min( block.size( ), firstMaxFragment )` (`:3672`), and
hands the same `priority` to `serializeHeaders( )` (`:3678-3687`), whose `Length` adds
`PRIORITY_FIELDS_SIZE` exactly when `priority.isSet` (`FrameCodec.h:1146-1150`) — so the two cannot
disagree. CONTINUATION fragments keep the full `maxFragment` (`:3697`), which is right: they carry no
priority fields. The two callers pass what the design says: the request path the profile's priority
(`:951-956`), the plain header path a default-constructed `noPriority` (`:990-992`). No underflow:
`applyPeerSettings( )` refuses `SETTINGS_MAX_FRAME_SIZE < 16384` (`:2555-2568`).

**H14 — both windows, before `reapClosedStreams( )`, unconditional.** The two flushes are at
`Session.h:1898-1900`, immediately after the padding credit (`:1876-1880`) and before the Data event
(`:1902-1916`), `onPeerEndStream( )` (`:1920`) and `reapClosedStreams( )` (`:1925`), which erases
contexts (`:3343`). `context` is `it -> second` (`:1873`) from a lookup no code between disturbs.
Neither flush is gated on `dataSize == 0`, as the design preferred. A frame the judge rejects returns
at `:1842` and never reaches this site, so the §10 invariant with H17 holds.

**H17 — before the registry, on `dataSize`.** The arm is at `Session.h:2204-2222`, inside
`judgeDataFrame( )`, which `handleData( )` runs (`:1836-1842`) before `m_registry.onFrameReceived( )`
(`:1844-1850`). It tests `context.expectsNoContent && dataSize != 0`, and `dataSize` is the payload
net of padding (`:1803`), so a zero-length DATA with END_STREAM — and a padding-only one — passes to
the ordinary completion. `expectsNoContent` is set for HEAD at submit (`:949`) and for 204 and 304
when the final block is delivered (`:3211-3214`), which covers the three shapes. The reject path
credits the connection window and flushes it before `rejectStream( )` (`:2243-2247`), so contract 1
holds on this arm as on the others.

**H26 — render time only.** `renderAuthority( )` (`TcpTunnelStage.h:633-651`) is a private static
used for both the request target and `Host` (`:664-673`); `originHost` arrives as a
`const std::string&` this class only reads and does not store. The SOCKS5 sibling (`:1256-1267`)
still sends the bare host in the domain-name address form, which for a literal is a different defect
under a different RFC and is not in the nine; noted in §11b.5 rather than fixed.

**H27 — the shared helper untouched.** `contains( )` (`ClientSession.h:719-733`) still calls
`equalsIgnoreCase( )`; its two other callers, the accept-encoding intersection (`:401`) and header
placement (`:620`), are unchanged. `containsExact( )` (`:740-753`) is used by `mergeCookieValues( )`
alone (`:512`).

**H28 — the existing entry's flag, before both branches.** `CookieJar.h:897-900` tests
`m_cookies[ existing ].isHttpOnly && ! isHttpApi` inside `if( existing != m_cookies.size( ) )`, ahead
of the expiry erase (`:902-906`) and the replacement (`:914-918`). The incoming-cookie refusal of
step 10 stays where it was (`:807-809`).

### 11b.2 The four things the lane says the design got wrong — each verified at the source

1. **§4's connection-wedge claim was false, and the correction at `c90f84e` is right.**
   `handleData( )` ends in `reapClosedStreams( )` (`Session.h:1925`), which ends in
   `flushConnectionWindowUpdate( false )` (`:3346`). The claim was the reviewer's in round 1 and the
   author's to verify; it was enumerated from the four call sites without following `handleData( )`
   to its last line. The reviewer records that against himself as well as against the author.
2. **`PRIORITY_FIELDS_SIZE` is private, so H13 as written was unreachable.** It sits in the
   anonymous enum under `private:` at `FrameCodec.h:204-212`. The lane's `prioritySize( )`
   (`:1119-1122`) is in the `public:` section that opens at `:505` and closes at `:1400`, and takes
   the very `Http2HeadersPriority` the serializer takes — the right door, and additive API.
3. **Beast asserts on a virgin parser, so the H02 probe needed a partial status line.**
   `basic_parser.ipp:202-224` (Boost 1.90.0, the dist this machine builds against) opens
   `put_eof( )` with `BOOST_ASSERT( got_some( ) )`; `got_some( )` is `state_ != state::nothing_yet`
   (`basic_parser.hpp:166-171`). The wrapper `putEof( )` (`Http1CodecBeastImpl.h:208-216`) guards
   only on `is_done( )`. `NDEBUG` is defined only by `gcc-default-release.mk:2` and
   `msvc-default-release.mk:1`, and neither `BOOST_DISABLE_ASSERTS` nor `BOOST_ENABLE_ASSERT_HANDLER`
   is set anywhere under `projects/make/` or `baselib/core/`. Verified — and see §11b.3 for what it
   means beyond the probe.
4. **The comment above the H03a flush was false and the lane amended it.** It read *"cancel
   everything, do not wait for it here, and let the queue's own disposal join"* — false once the
   flush waits, and the design that changed the flush did not say so. The amendment fixes that half.
   The other half — *"flushed the way TcpServerBase flushes its own"* — is now itself inexact,
   because `TcpServerBase` flushes with `wait = false` (`TcpBaseTasks.h:1919-1927`) and leaves the
   join to its members' `~ObjPtrDisposable( )`; after this change the pool differs from it precisely
   in taking the join here. Proposal in §11b.5, item 3.

### 11b.3 The latent defect: the analysis is right, the routing is right, and the record must say more

**Verified.** `onPeerClosed( )` (`Http1ConnectionTask.h:988`) returns only for a null parser; with a
request in flight the parser exists from `onStartRequest( )` (`:602`) until `finishStream( )`
(`:1142`), and a read that completes with EOF before one response octet reaches `parseEof( )`
(`Http1Codec.h:538`), `putEof( )`, and `put_eof( )` on `state::nothing_yet`. Debug: the assert
aborts the process. Release: `put_eof( )` falls past both of its guards and sets
`state_ = complete`, so `isDone( )` is true, `m_statusCode` is still its initial 0,
`isInterimStatus( 0 )` is false and `m_isComplete = true` (`Http1Codec.h:563-566`). Then
`deliverHeaders( )` (`Http1ConnectionTask.h:801-838`) finds `isHeaderComplete( )` true — Beast's
`is_header_done( )` is `state_ > fields` — and delivers `onHeaders( handle, 0, {} )`; the request
task copies the 0 into the response with no floor (`HttpClientRequestTask.h:866`); and
`finishStream` is reached with `error_code( )` (`:1013-1019`). **The caller receives a status-0,
header-less, body-less response reported as a success.** The lane's phrase "a zero-byte response
reported as success" is right; the status-0 header delivery is the part it did not say.

**It is not latent in the sense of hard to reach.** The shape is a peer that closes after reading
the request and before answering — a server that drops a request it will not serve, and the stale
keep-alive race whose window is exactly the one H01 and H02 exist for. (The always-armed read
catches a FIN that arrived earlier, with the parser still null; only a FIN that lands after
`onStartRequest( )` has created the parser reaches this path.) Debug users of the pool will see the
abort on that race.

**What this means for H02, which neither the lane nor the design said.** On the zero-octet shape,
`isRetryable` is never consulted in release — the stream closes as a success — and never reached in
debug. H02 as landed therefore closes the duplicate POST for the shape where at least one response
octet arrived before the close (the probe's shape) and for the throwing-initiator and task-failure
paths, and does nothing for the zero-octet shape until S6R.2 converts that close into a failure —
at which point `! m_requestMayHaveBeenSent` is precisely the value that failure must carry. The
flag's name and its placement before the write are load-bearing for a fix that has not been written
yet. The routing to S6R.2 with H01 and N3 is right, and S6R.2 owes:

- the conversion itself — in `parseEof( )` or in the wrapper, a parser that has seen no octet
  reports a refusal rather than reaching `put_eof( )`, with `onPeerClosed( )` then finishing the
  stream as a failure carrying `! m_requestMayHaveBeenSent`;
- a test for the zero-octet shape, which cannot exist in this change-set because in a debug build it
  aborts the module before it can assert anything. The H02 probe's partial status line is therefore a
  workaround with a date on it, not a modelling choice; the probe comment's "also the realistic
  shape" is true and must not be read as "the only shape".

Both are recorded here so that S6R.2's design cannot start without them.

### 11b.4 Are the tests worth their green?

Each new case was traced through the source on both sides of its fix rather than taken from the
commit message's numbers, which the reviewer could not rerun.

- **H02** (`TestHttp1ConnectionTask.h:1951-2039`, probe `:821-940`). The ordering argument holds:
  `submit( )` posts through `postToStreamExecutor( )`, which is `asio::post( )` on the socket's
  executor (`Http1ConnectionTask.h:301-310`, `:1549-1560`) and never a dispatch; `m_started` is set
  synchronously in the base `scheduleTask( )` (`:1357`) before the probe's own post; so the strand
  holds `[ onStartRequest, peerClosedProbe ]` before either runs, and the write completion can only
  queue behind both. `m_probeParsed` is written on the strand before `sink -> onClosed( )` and read
  after `waitForClosed( )` returns under the sink's lock, so the read is ordered. The peer's
  `read:POST /inflight HTTP/1.1` record pins that the octets were on the wire, which is the premise
  of non-retryability. Red before: with the old flag unset until `onWriteCompleted( )`, the probe
  runs first and reports retryable. **Two limits, stated so nobody over-reads the green:** the case
  cannot tell marking before `async_write( )` from marking after it returns — the "before" is
  verified by reading (§11b.1), not by this test; and it pins only the partial-response shape
  (§11b.3).
- **H03a, H04b.** No deterministic case, as the design says. The TSan corroboration exists only in
  the lane's runbook outside the repo (`http2-l0-state/lane1.md`, S6R.1 section) and is transcribed
  here because §11 requires it recorded: instrumented tree parked outside the repo;
  `utf_baselib_h2client4` (15 cases) and `utf_baselib_h2client5` (1 case) three runs each, no
  report; the positive control `utf_baselib_basictask` fires in the same tree with the known
  `TestBaselibBasicTask.h:127` race, `reported 1 warnings`, exit 66, banner and suppressions file
  present. The modules run include cases which dispose a pool with acquisitions still pending
  (`TestConnectionPool.h:1195-1201`, `:1580-1585`), which is as close to §11's "racing `dispose( )`"
  as a deterministic case gets. **A clean run is not the proof** — detection is
  interleaving-dependent; what carries H03a and H04b is the construction argument in §2 and §7,
  verified at the source in §11b.1.
- **H13** (`TestSession.h:2735-2789`). `lengths[ 0 ] == peerMaxFrameSize( )` is the right pin: with a
  block far over the limit, `first = 16384 - 5` and the serializer adds 5, so the frame is exactly
  16384; the old code gave 16389, the lane's red number. The PRIORITY-flag assertion on `out[ 4 ]`
  closes the way the case could pass vacuously. `frameLengths( )` (`:503`) reads the 24-bit length
  correctly.
- **H14** (`TestSession.h:2499-2578`). Traced: a PADDED frame with Pad Length 60 and 60 octets of
  padding is 61 octets; both windows are charged 61 and credited 61; the profile's threshold of 40
  reaches both the connection window (`Session.h:536-540`) and the stream's (`:1453-1458`); so with
  the fix each emits one WINDOW_UPDATE and the trailing `reapClosedStreams( )` finds nothing left,
  which is why `== 1U` for stream 0 is exact rather than `>= 1U`. `takeWindowUpdate( )` grants the
  increment back (`FlowControlWindow.h:612-627`), so `streamReceiveWindow( ) == 65535` is a second,
  independent pin; the old code left 65474, the lane's red number. The stream-0 assertion is a
  control that passes on both sides, as the lane reported.
- **H17** (`TestSession.h:2178-2295`). The control passes on both sides, traced: before the fix
  there was no arm; after it `dataSize == 0` never enters the arm; the registry accepts; the H14
  flushes emit nothing (no pending credit, and `canSend` is false on the now-closed stream);
  `onPeerEndStream( )` sets `messageComplete` with no content-length to check; and
  `reapClosedStreams( )` emits `StreamClosed( NO_ERROR )` with no RST_STREAM — the three events the
  case requires. The rejections: `events.size( ) == 2` holds on both sides (Headers + Data before,
  Headers + StreamClosed after), so the red is in the three checks per script, nine in all, matching
  the lane. `rejectStream( )` (`Session.h:3290-3316`) records the code, sends RST_STREAM and reaps,
  which is where the StreamClosed event and the one RST_STREAM the case counts come from.
- **H26, H27, H28** are exact-value unit cases whose red values follow directly from the old code
  paths read above; nothing to add. H28's two checks after the two rejections are genuine controls:
  the HTTP-side replacement and the non-HttpOnly non-HTTP write both go through `:914-918`.

### 11b.5 What neither the design nor the lane noticed

1. **FINDING, and the one condition of this agreement — a comment amended to stay true is false.**
   `TestSession.h:2377` now reads *"ONE EXCEPTION, and the last case below is it: PADDING is
   credited on arrival"*. The padding-only case (`:2499`) is followed by two more cases in the same
   test — the stream-window overrun at `:2580` and the connection-window overrun at `:2615`. It is
   not the last case. Replace "the last case below" with **"the padding-only case below"**. Two
   words, in a comment the lane rewrote for the purpose of not being false after H14.
2. **FINDING on a comment, fix PROPOSED — the "self-guarding on END_STREAM" claim is overstated, in
   the tree and in §4.** The in-tree comment (`Session.h:1892-1896`) says *"if this frame carried
   END_STREAM, canSend( WINDOW_UPDATE ) fails and the stream flush emits nothing"*. `canSend( )`
   returns true in half-closed (remote) (`StreamStateMachine.h:358-361`) and false only in closed
   (`:375-381`). So the claim holds when the END_STREAM closes the stream — every request that
   carried no body, and every request whose body was fully sent — and fails while the local half is
   still open (a body still uploading when the peer answers early): the stream is then half-closed
   (remote), the flush emits a threshold-gated WINDOW_UPDATE that is legal (RFC 9113 5.1, a
   half-closed (remote) stream may send any frame; 6.9, the peer must not treat it as an error) and
   useless, and no credit is lost because the connection window took its own padding credit at
   `:1813-1818`. Not a behaviour defect — `consumed( )` (`:1142-1161`) already emits the same frame
   in the same state — which is why it is a proposal and not a condition. Proposed tree wording:
   *"and if this frame carried END_STREAM and closed the stream, canSend( WINDOW_UPDATE ) fails and
   the stream flush emits nothing; on a stream whose local half is still open it emits a legal
   WINDOW_UPDATE the peer will ignore"*. §4 above is corrected in place.
3. **PROPOSAL — the `TcpServerBase` analogy in the H03a comment.** After the amendment
   (`ConnectionPool.h:2113-2118`) the comment says the queue is *"flushed the way TcpServerBase
   flushes its own: cancel everything, and take the queue's own join"*. `TcpServerBase` cancels and
   does not join (`TcpBaseTasks.h:1919-1927`, `wait = false`, the join left to
   `~ObjPtrDisposable( )`). Proposed: *"The connection queue is cancelled the way TcpServerBase
   cancels its own — every task, no GOAWAY — but unlike TcpServerBase the pool takes the queue's join
   HERE rather than leaving it to the member's destructor, for the reason below."*
4. **Observation, no change.** `if( m_eqConnections )` at `ConnectionPool.h:2154` is now always
   true: the member is assigned in the constructor and never reset. Harmless; recorded so no reader
   takes the guard as evidence that the member can be null.
5. **Observation, outside the nine.** The SOCKS5 negotiation (`TcpTunnelStage.h:1256-1267`) sends
   the bare stored host in the domain-name address form (`ADDRESS_DOMAIN_NAME`), so an IPv6 literal
   goes out as the three-octet name `::1` rather than as RFC 1928's 16-octet `ATYP = 0x04`. Same root
   as H26 — the stored host is bare — different wire format, different fix. Read, not run against a
   proxy; belongs on the owed list, not in this change-set.

**Agreement.** With item 1 applied, the reviewer agrees that the S6R.1 implementation matches the
agreed design and may land, subject to §11's release passes and whole-suite gate, which were running
at the time of this review and which this section does not claim.

---

## 12. Deliberately out of scope

H01, H05, H07, H12, H15, H16, H18, H03b, N1, N2 (S6R.2); H06, H08, H09, H10, H11, H04a (S6R.3,
decisions first); H19, H20, H23, H29 (S6R.4); H24, H25 (deferred to the decoder programme); H21, H22
(on L6's owed list). **N2's fix additionally needs a Windows run to accept**, which this machine
cannot provide — the peer-close record states that a Linux-only run cannot catch a breach of that
rule.
