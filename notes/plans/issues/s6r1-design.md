# S6R.1 — design for the nine contained fixes

**Status:** design, 2026-09-22. **Nothing implemented.** This is the artifact that must be agreed
before code is written, per the review loop.

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
strand**, which an earlier draft left unsaid and which is the whole of the determinism.

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
second added a move-out under the lock — mechanically correct, but I traced the residual to its end
and **it still crashes, later and elsewhere**: `dispose( )` does `m_observerThis -> disconnect( );
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
   `idleTimeout`, **300 s by default**, and unbounded if a caller disables it.

**Face 2 is pre-existing and this change does not introduce it.** Today's `reset( )` → `dispose( )`
→ `flushInternal( wait = true )` releases the lock inside the same predicate wait, and the observer
proxy is nulled only after that flush returns, so a push landing in today's wait produces exactly
the same delay. What shape (A) removes are the two **crash** faces — the null member and the second
draft's null-proxy completion. The delay is closed by S6R.3's admission protocol, with H04a.

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
stream and left the **connection** wedged in the same scenario.

Call **both** `flushStreamWindowUpdate` and `flushConnectionWindowUpdate( false )` at the
post-registry site, after the padding has been credited. The existing `shouldSendWindowUpdate`
threshold inside each prevents frame spam.

**Why the connection half is not optional.** The connection padding credit has no flush either, and
none of the four existing `flushConnectionWindowUpdate` sites — `consumed( )`, refused frame,
judge-rejected, reap-closed-streams — is reachable from an *accepted* padding-only frame. A stream
receiving only such frames drains the 65535-octet connection window with nobody to refill it: the
peer's send window is legitimately zero and the **whole connection** wedges, which is worse than the
stream-level defect this finding names. Verified at the source by the author.

**Boundary, restated more precisely than the earlier draft.** A padded frame carrying even one real
byte already unwedges the stream, because `consumed( )` flushes the single `pendingCredit` counter
including all accumulated padding. So gating the new flushes on `dataSize == 0` is *permissible* but
not required — an unconditional flush at that site is threshold-gated and harmless. Prefer the
unconditional form: fewer branches, same behaviour.

**Self-guarding on END_STREAM.** If the frame carried END_STREAM, `canSend( WINDOW_UPDATE )` fails
and the stream flush returns without emitting; `reapClosedStreams` then credits the leftover to the
connection. No special case needed.

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

**What must not change.** The **unbracketed** form is correct for the resolver, for SNI and for
certificate verification. Bracket at render time only — do not normalise the stored host.

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

---

## 12. Deliberately out of scope

H01, H05, H07, H12, H15, H16, H18, H03b, N1, N2 (S6R.2); H06, H08, H09, H10, H11, H04a (S6R.3,
decisions first); H19, H20, H23, H29 (S6R.4); H24, H25 (deferred to the decoder programme); H21, H22
(on L6's owed list). **N2's fix additionally needs a Windows run to accept**, which this machine
cannot provide — the peer-close record states that a Linux-only run cannot catch a breach of that
rule.
