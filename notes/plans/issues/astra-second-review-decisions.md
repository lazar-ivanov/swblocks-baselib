# Astra's second review — the decisions of 2026-09-27, as put and as taken

**Status:** decision document, 2026-09-27. **Nothing implemented; nothing under `src/` was touched
and nothing was built.** Eight decisions were put to the maintainer in AGENTS.md's shape and taken:
*"Yes, I accept D2 as revised, D4 widened to Astra's version, and the rest as recommended."*
Implementation waits for the maintainer's go-ahead.

**Scope:** R01–R08 of [astra's second review](../http2-l0-l6-remediation-review-2026-09-26.md), which
reviewed `db97372`. R09, the stale records, was done at `425802c`. Each finding was re-verified at the
source before it was put; the ledger rows are in [`astra-remediation-owed-work.md`](astra-remediation-owed-work.md),
under "Found by astra's second review".

**Provenance.** Everything below was read at the source on `lazari2` at `425802c`, whose `src/` is
`db97372`'s. The RFC passages were fetched rather than recalled, after one had been paraphrased wrongly
(§6). Nothing was built or run: every mechanism here is a static derivation, as the review's were.

---

## 1. The decisions at a glance

| # | Finding | Decided | Change-set |
|---|---|---|---|
| **D1** | R01 — a TLS truncation completes a close-delimited HTTP/1.1 body | **Strict**: a truncation never completes a message; no leniency setting | CS-1 |
| **D2** | R03 — the first TLS read and a request's first write can start concurrently; and a cancel before the driver starts is erased | **Astra's startup handler**: the first read, the switch that lets request starts be posted, and cancellation, in one accounted strand handler | CS-1 |
| **D3** | R02 — HTTP/1.1 body bytes a sink has not taken are unbounded | **Fail on overflow** of a **64 MiB** cap on bytes received and not yet taken, kept by the request task. *Refined in §9: each block is also charged an allowance, and nothing is delivered after a failure* | CS-2 |
| **D4** | R04 — H08's delivered count is lost when a later sink callback throws | **Astra's version**: record each block before the next callback; once a sink throws, no further delivery to it and no replay onto it | CS-2 |
| **D5** | R08 — H24 and H25 are reachable through public decoder registration | **Fix now, minimally**: decode only a single coding, and only a successful response that can carry content | CS-2 |
| **D6** | R05 — TE and the fields `Connection` names, across repeated fields | **Normalize**: canonicalize every TE; remove the fields every `Connection` names, except `te` — *and `host`, refined in §3* | CS-3 |
| **D7** | R06 — the Host agreement check | **Parse with `net::Uri`**: default a missing port from the scheme; refuse a malformed Host and more than one | CS-3 |
| **D8** | R07 — a case variant of HEAD is classified as HEAD | **Exact `"HEAD"`** | CS-3 |

---

## 2. The change-sets, their order and their validation

**Grouped by the files a lane touches**, per AGENTS.md: several findings in one file are one
change-set. The three touch disjoint files, so they can run as parallel lanes.

| CS | Decisions | Files |
|---|---|---|
| **CS-1** | D1, D2 | `httpclient/Http1ConnectionTask.h`; comments in `core/NetUtils.h` and `TestHttp1DriverTlsCancelClose.h`; new cases |
| **CS-2** | D3, D4, D5 | `httpclient/HttpClientRequestTask.h`, `httpclient/ClientSession.h`; new cases. *And, from §9, `httpclient/ConnectionPool.h` for the pool fix scheduled on sight* |
| **CS-3** | D6, D7, D8 | `http2/Http2ConnectionTask.h`, `http2/Session.h`; new cases. *And, from §9, the HTTP/2 DATA block sized to its payload* |

**Order, and why.** CS-1 first: D1 closes a live defect that hands a caller a wrong answer — a
truncated body reported complete — the class AGENTS.md schedules on sight, and D2 closes undefined
behaviour in the TLS engine; both are small and in one file. CS-2 second: it holds the one decision
with a number in it (D3) and a wrong answer behind an opt-in setting (D4). CS-3 last: every item in it
needs unusual caller input.

**Validation, per AGENTS.md.**

- Lanes: clang debug, the focused modules each slice affects, one module at a time.
- The orchestrator, after each merge: clang release and gcc debug.
- **CS-1 owes a Windows matrix run.** It changes how a TLS ending is classified — AGENTS.md's
  networking rule — and the order of the first I/O on every HTTP/1.1 connection.
- **Every red/green here is deterministic** — a barrier, a rendezvous or a pure boundary input — so
  each is shown red once and green once, with no 50- or 600-run budget.
- `core/NetUtils.h` is touched by comment only and kept line for line, so no object changes: baselib's
  macros bake `__LINE__` in.
- Test placement follows `src/utests/AGENTS.md`: a module comfortably under the 40 MB target, or a
  numbered sibling.
- **X1 stays deferred.** Nothing here replaces the x86-64 Linux matrix.

---

## 3. The decisions, as put and as taken

### D1 — R01: a TLS truncation must not complete a close-delimited body (CS-1)

*What it is.* `isCleanEndOfStream( )` (`Http1ConnectionTask.h`) admits `net::isCleanEndOfStreamErrorCode( )`
— eof — **or** `STREAM::isStreamTruncationError( )`, a TLS stream that ended without close_notify.
`onPeerClosed( )` then runs `parseEof( )`, which completes a body framed only by the close, and the
stream finishes successfully. Pre-existing: the driver's first version (`9ed2745`) classified the
ending the same way. The justification is written in three places — `NetUtils.h` above
`isCleanEndOfStreamErrorCode( )`, `isCleanEndOfStream( )`'s own comment, and face 2 of
`TestHttp1DriverTlsCancelClose.h` — as *"the ordinary shape of a close-delimited HTTPS response (RFC
2818 section 2.2.2)"*. §2.2.2 is server behaviour, and says servers MUST attempt to initiate the
exchange of closure alerts. RFC 9112 §9.8 is explicit: *"A response that has neither chunked transfer
coding nor Content-Length is complete only if a valid closure alert has been received."*

*If not done.* A caller can receive a truncated body reported as a complete 200, with no way to tell —
from a faulty peer, or from anyone able to end the transport.

*Risk, complexity, blast radius.* One predicate with one caller, `onPeerClosed( )`, plus the three
comment corrections. It reaches every HTTP/1.1-over-TLS response framed by the close. Length- and
chunk-framed responses are unaffected: a complete one finishes before the ending arrives, and an
incomplete one already fails — its code changes from the parser's partial-message to the TLS
truncation code. Third-party servers that send close-delimited responses **and** skip close_notify will
start failing. baselib's own server always sets Content-Length and refuses a custom one
(`httpserver/Response.h`), so nothing in-house changes.

*Undecided was:* strict; strict plus an opt-in leniency setting per session; or completing the
response and flagging it. The third needs a new field on a `ClientTypes.h` type — a frozen, published
interface — and was ruled out.

**Decided: strict, with no setting.** A truncation takes `onPeerClosed( )`'s unclean branch exactly as
a reset does: what parsed is delivered, the message is not completed, and the stream finishes with the
truncation's own code. **Reverses if** a real deployment meets a server that sends close-delimited
responses without close_notify; then an opt-in per session, defaulting to strict.

*Tests, all deterministic.* A peer-initiated missing close_notify on a close-delimited response, with
no local cancel: the request fails and the sink is not told complete — red today, green after. A
close_notify control that succeeds. Content-Length and chunked controls, ended by a truncation after a
complete message, that succeed. Each asserts the request's result and the sink's completion, not only
the driver task's terminal state.

*Rode with it until the fallout round.* The HTTP/1.1 driver's `consumed( )` comment — *"What
backpressure there is over HTTP/1.1 is TCP's own"* — is false while the read re-arms after every
chunk. It was to be corrected here because this change-set owns that file; it is a fact correction
that depends on no decision, so it moved to the bucket C round (§7) and was corrected at `d186d2a`,
without reference to D3's cap.

### D2 — R03: start the connection in one strand handler (CS-1)

*What it is.* Two defects in the driver's start.

1. **The race Astra found.** `scheduleTask( )` sets `m_started` under `m_stateLock`, releases it, and
   then calls `armRead( )` on the queue thread — and `ssl::stream::async_read_some( )` runs its first
   engine step on the thread that calls it. A `submit( )` in that window reads `m_started`, posts
   `onStartRequest( )`, and that handler, which takes no task lock, starts `async_write( )` on the same
   stream from the strand. It is reachable through the pool: a driver is born `Ready`, and
   `findDispatchable( )` asks only for `Ready` and free slots, never whether the driver is scheduled,
   so another thread's examine can hand it out before `runActions( )` pushes its schedule.
   Pre-existing: `c8e9be8` had the same shape, and A4 changed only how a throw leaves.
2. **Cancellation during startup — found while verifying D2's first form.** A request can be submitted
   before the driver starts. `cancel( handle )` posts `onCancelStream( )` without asking `m_started`,
   and that reaches `finishStream( )` → `closeConnection( )` → `beginClose( )`, which only sets
   `m_closing`. `MultiOperationTaskT::scheduleNothrow( )` zeroes `m_closing`, with the rest of the
   accounting, at the start of every run. So a cancel that lands before the run is erased: the driver
   starts as a `Draining` connection with a read armed and the idle timer running, and stays open until
   the idle timeout — 300 seconds by default — or until the peer closes. A cancel that lands during
   `scheduleTask( )` survives the reset, but `initiateClose( )` runs only on the first error or the
   first operation to complete after `beginClose( )`, so nothing wakes the read and the connection
   again waits on the peer. The cancelled request itself is answered correctly; it is the connection
   that lingers.

*If not done.* Rarely — a window of microseconds on a new connection, against a concurrent submit —
two threads drive one OpenSSL connection at once: undefined behaviour, anywhere from corrupt I/O to a
crash. And a request cancelled before its connection started holds that connection open for up to the
idle timeout.

*Risk, complexity, blast radius.* Small to moderate, all in `Http1ConnectionTask.h`. It reaches every
HTTP/1.1 connection's start, and adds one strand hop before the first read.

*Undecided was, first, the shape.* **As first put, the recommendation was a reorder** — arm the read,
then publish `m_started` — **and it was withdrawn during the round**, for two reasons.

- **The reorder is complete for the race and not for the item.** After it, the first read's start is
  the only place the driver touches the TLS stream off the strand, and nothing that touches the stream
  can run on the strand before that start returns: `onStartRequest( )` is posted only once `m_started`
  is set; `cancelTask( )` already posts its shutdown to the strand (`shutdownOnStreamExecutor( )`);
  `onCancelStream( )` touches no socket; and the read's own continuation can only follow its start.
  But it does nothing for defect 2, which Astra's shape covers by construction — *"with operation
  accounting and cancellation handled there"*. And it holds only while every future post that can
  precede the start stays gated by `m_started`.
- **The reason given for preferring it was wrong.** It was put that a read started from a posted
  handler *"can't reach `scheduleNothrow( )`'s catch"*, as a conflict with A4. It is none. A4's defect
  was a failed start completed *inline* while `scheduleNothrow( )` holds the task lock, which took the
  terminal path into a lock its own caller held. Inside an accounted strand handler, the handler's own
  operation keeps the count above zero until its epilog runs, so `scheduleRead( )` with its catch is
  safe there — which is how every re-arm in this driver already works. The true reason was size, and
  it did not survive the comparison.

**Decided: Astra's startup handler, closing both defects.** The shape, in idioms this file already
has:

- **`scheduleTask( )`** begins an operation and posts the startup handler to the stream's executor,
  and does nothing else. A post that throws propagates to `scheduleNothrow( )`'s catch with the count
  left at one — A4's route exactly, which `armRead( )`'s own comment already describes.
- **The startup handler** runs on the strand, under the handler macros:
  - if the connection is no longer `Ready` — a cancel arrived first — it re-asserts
    `closeConnection( )`, because the run's reset erased the first one, and starts no read;
  - otherwise it arms the read through `scheduleRead( )`, then sets `m_started` and takes
    `m_startPending` under `m_stateLock`, then starts the pending request or arms the idle timer — both
    of which already expect to run on the strand.
- **Its epilog** completes the startup operation. After a re-asserted close, that completion is the
  first after `beginClose( )`, so `initiateClose( )` runs and the task takes its terminal path at once.
- From the first operation on, every touch of the TLS stream is on the strand, without depending on
  which posts can precede the start.

*Undecided left:* none. **Reverses if** implementing it turns up a test or invariant that depends on the
read starting inside `scheduleTask( )` itself; then the reorder closes the race, and the cancel gap is
recorded as an item of its own.

*Tests, all deterministic.* Astra's barrier case: hold the first read's start at a barrier — a test
hook — while a request is submitted from another thread, and assert that no write starts before the
read's start has returned; red today, green after. Cancel-before-start: submit, cancel, then schedule,
and assert that the task ends without waiting for the idle timeout or the peer; red today, green after.
Both under the TLS and the cleartext policies. A4's case, `Http1Driver_ScheduleReadInitiatorThrowEndsTheTaskTests`,
then exercises the handler's route rather than `scheduleTask( )`'s, where only the post can still
throw.

### D3 — R02: cap the HTTP/1.1 body bytes a sink has not taken (CS-2)

*What it is.* Over HTTP/1.1 nothing limits how many body bytes wait for the sink. The driver's
`consumed( )` is a no-op and its read re-arms after every chunk; the request task queues every block in
its mailbox and then in `m_pendingDownload`, and neither is capped. N1 (`9895df2`) removed the 64 MB
transfer cap on purpose, and with it the only bound on this. It had been written down twice without a
decision — S6R.2 §9, and S6R.3 §1.6, the second time on the premise that *"any number silently
truncates a body"*, which does not hold for a bound on *outstanding* bytes.

*If not done.* A streaming download whose sink falls behind — or returns 0, which the BodySink
contract allows — grows memory at network speed until memory or the 30-minute total timeout runs out.
`streamIdleTimeout` is off by default, and would not fire while data flows anyway.

*Risk, complexity, blast radius.* Low to moderate. The request task serves both protocols and the
session, so every response's data path passes the counter. Over HTTP/2 the stream window bounds the
same case first, unless a profile advertises a window above the cap.

*Undecided was, two choices.*

- **Fail or pause.** Pausing HTTP/1.1 reads would rework the driver's rule that a read is always in
  flight — its task ends when nothing is pending — and, with no sink readiness signal, a sink that
  returns 0 is never offered data again, so it stalls until the 30-minute total timeout, as HTTP/2 does
  today.
- **Where.** In the request task, counted as each event is posted — the layer that sees the buffering,
  which is N1's own principle, and one that covers the mailbox too — or in the HTTP/1.1 driver through
  `consumed( )`.

**Decided: fail, in the request task, with a default of 64 MiB.** A new cap beside
`maxResponseBodySize` in `HttpClientRequestConfig`, on body bytes received and not yet taken: counted as
each data event is posted, and released as the sink takes bytes or the buffered path appends them.
Past it the stream is cancelled and the request fails with `BufferTooSmallException`, as the buffered
cap already does, and nothing further is queued for that request. 64 MiB is the ceiling N1 removed,
now on the right quantity, so no download size is capped. **Reverses if** a consumer needs a slow sink
to stall without failing; then pause — but only together with a sink readiness signal, the deferred
`ClientTypes.h` work, since pause alone turns this failure into a 30-minute stall.

**Refined 2026-09-27, in the run's first decision round (§9), both as recommended:**
- **The count is payload plus a fixed per-block allowance.** Over HTTP/1.1 one read is one block, so a
  peer trickling single bytes at a stalled sink would reach over 10 GiB of blocks before a cap on
  payload alone fired. The allowance is derived from the types — `sizeof( Event ) + sizeof( DataBlock )`
  plus an allocator constant — and the knob keeps its name and its unit, bytes.
- **`applyData( )` delivers nothing once the request has failed.** A timeout or a cancel still
  offered late blocks to the caller's sink; the block is now dropped and its charge released.

The mechanism is [`astra2-cs2-d3-unread-bytes-cap-design.md`](astra2-cs2-d3-unread-bytes-cap-design.md),
agreed after two review rounds.

*Tests, all deterministic.* A real HTTP/1.1 driver and a sink returning 0 under a small configured cap:
the request fails with the overflow, with no large allocation. A mailbox case: the sink's callback held
at a rendezvous while data arrives, failing at the cap. A control: a consuming sink and a body larger
than the cap, which succeeds — proving that the cap is on outstanding bytes and not on the total.

### D4 — R04: a sink's count survives a later throw, and a sink that threw is left alone (CS-2)

*What it is.* `offerToSink( )` adds up the accepted bytes in a local and records them in
`m_sinkDelivered` only after its loop. When a later callback in the same offer throws, the record is
skipped: `runDeferred( )` catches the throw, `failWith( )` keeps an earlier network failure first
(H07), and `chkPrepareRetry( )` sees zero. Residual of H08 (`0f8d9bd`).

*If not done.* With `retryIdempotentOnConnectionLoss` on — it defaults off — and a connection lost in
the same batch, a replay hands the sink the bytes it has already taken a second time, whether or not
the replay then succeeds. And the same batch offers a sink that has just thrown its block again.

*Risk, complexity, blast radius.* Trivial; it reaches the accounting of every streaming download.

*Undecided was:* whether a sink that threw — even having taken nothing — also stops the request's
further deliveries. **As first put, only replay was blocked.**

**Decided: Astra's version, wider than first put.** Each block's accepted bytes are recorded before the
next callback runs; and once a sink throws, **no further delivery reaches it and no replay is made
onto it** for that logical request. **Reverses if** sinks gain `reset( )` — B4, deferred in
`body-sink-terminal-callback-and-reset-deferral.md` — when a sink that can reset could be replayed.

*Tests, all deterministic.* A same-offer multi-block case — the first block taken, the second throwing
— followed by a connection loss in the same batch with `retryIdempotentOnConnectionLoss` on: one
network attempt, no duplicated prefix, and the sink not called after it threw. H07's first-failure
precedence and the release of the pool slot are both preserved. `BatchThrowingSinkT` does not cover
this: its first block commits in an earlier batch.

### D5 — R08: fix H24 and H25 now that applications can reach them (CS-2)

*What it is.* `ClientSession::decoders( )` and `registerDecoder( )` are public, and
`utf_baselib_httpclient4` registers through them. For an application that does the same, two defects
the decoder deferral had called latent are live:

- **H24:** `decodeBody( )` decodes only the first Content-Encoding field, and then removes all of them.
- **H25:** `continuationTask( )` runs `absorbResponse( )` — and so `decodeBody( )` — before it checks
  the hop's exception, so failed responses and responses with no body are decoded.

*If not done.* Such an application can receive a body decoded in the wrong order and labelled as
uncoded, or a decoder's error in place of the network failure that actually happened.

*Risk, complexity, blast radius.* Small, in `decodeBody( )` and `absorbResponse( )`, and reached only
when a registered decoder matches a response's coding; the default path is untouched.

*Undecided was:* fix now; restrict registration until the decoder programme lands — a public API
change, which breaks the test that registers; or document only.

**Decided: fix now, in the minimal form.**

- **H24:** the Content-Encoding list is read across every field, and a response is decoded only when it
  carries exactly one coding in total. Anything else is handed back with its body and all its headers
  untouched — already the documented behaviour for a coding the client cannot decode. Decoding several
  layers stays with the decoder programme.
- **H25:** decoding happens only when the hop succeeded and the response can carry content — not a
  response to HEAD, and not a 204 or a 304.
- **P1 — the decode under the queue lock, astra H09 — stays deferred as decided**, and the records now
  say that it too is reachable by an application that registers a decoder. Codec supply stays parked.

**Reverses if** the decoder API is to be frozen until the programme instead; then registration is
restricted, with the restriction stated at `registerDecoder( )`.

*Tests, all deterministic, with the existing test transform.* Two Content-Encoding fields, and one
field listing two codings: both handed back untouched. A response to HEAD and a 304 carrying
Content-Encoding: not decoded. A failed coded partial response: the caller sees the network error and
not a decoder's.

### D6 — R05: TE and the fields `Connection` names, across repeated fields (CS-3)

*What it is.* `normalizeHeaders( )` (`Http2ConnectionTask.h`) checks only the first TE field —
`tryGet( )` returns the first — and removes `Connection` in its fixed-name loop without reading the
fields its tokens name. Residual of H16 (`38c6f3d`).

*If not done.* `TE: trailers` followed by `TE: gzip` sends a request an HTTP/2 server must treat as
malformed (RFC 9113 §8.2.2). A field the caller marked hop-by-hop travels beyond the first hop.

*Risk, complexity, blast radius.* Small, in `normalizeHeaders( )`; only requests carrying these unusual
fields change.

*Undecided was:* for TE, canonicalize or be strict. And a trap in the obvious fix, which the review did
not name: stripping every field `Connection` names would remove `TE: trailers` from the form RFC 9110
§10.1.4 requires — a sender of TE also sends a `TE` connection option — and `trailers` is the one TE
value HTTP/2 permits.

**Decided: normalize.** Tokens are collected from every `Connection` field before anything is removed;
the fields they name are removed **except `te`**, which its own rule governs; then `Connection` and the
fixed connection-specific names go. TE is canonicalized across every occurrence: if any lists
`trailers` among its codings, exactly one `te: trailers` is sent, and otherwise none. `HeaderList` keeps
repeated fields, as its contract says. **Reverses if** a stricter request API is wanted, one that
rejects these inputs before they reach the driver.

**Refined 2026-09-27, at CS-3's checkpoint review: `host` is exempt too.** Both the review and the
lane found the same gap. Removing a field a `Connection` token names ran before D7's check, so
`Connection: host` with a disagreeing Host dropped the Host unread where D7 refuses it. It was a
silent change of the request's origin, and it made the two protocols diverge. The exemption holds
because D7's own check decides `host`, as TE's own rule decides `te`. **Reverses if** the maintainer
rules that `Connection: host` asks for RFC 9110 §7.6.1's drop; then only the comments change.

*Tests, pure boundary inputs.* Repeated TE; `TE: gzip, trailers`; TE without `trailers`; several
`Connection` fields with token lists, keeping the end-to-end fields they do not name; and
`Connection: TE` with `TE: trailers`. Each asserts the emitted HPACK fields, not the request object.

### D7 — R06: the Host agreement check (CS-3)

*What it is.* `isSameAuthority( )` defaults a Host with no port from `url.effectivePort( )`, which
returns the URL's *explicit* port when it has one — although its comment says the scheme's. Its port
parser returns 0 for a missing port, a non-digit port and `:0` alike, and accumulates unchecked, so a
large port wraps. And only the first Host is checked before every Host is removed. New in the H16 fix
(`38c6f3d`).

*If not done.* Some disagreeing or malformed Host values are silently dropped instead of refused —
`https://example.com:8443/` with `Host: example.com` among them. Nothing is misrouted: the URL stays the
connection target, as the review says.

*Risk, complexity, blast radius.* Small. `net::Uri`'s own parser — `tryParse( )` of `"//"` followed by
the field — already validates the host, rejects a non-digit port and one above 65535, and treats an
empty port as none.

*Undecided was:* for several Host fields, refuse them, or accept them if they all agree. RFC 9112 §3.2
has a server answer 400 to more than one, so there is no legitimate use.

**Decided: parse with `net::Uri`.** A Host that does not parse, or that carries anything beyond a host
and an optional port — userinfo, a path — is malformed and refused. A missing port is defaulted from
the scheme, never from the URL's port. More than one Host field is refused. The accepted equivalence of
`https://example.com/` and `Host: EXAMPLE.com:443` is kept. **Reverses if** a real caller sends
duplicates that agree.

*Tests, pure boundary inputs.* The inverse of the existing case — `https://example.com:8443/` with
`Host: example.com`; `:garbage`, `:0`, `:99999` and `:4294967739`; two Host fields; and controls for the
accepted equivalence, an empty port, and an IPv6 literal.

### D8 — R07: HEAD, matched exactly (CS-3)

*What it is.* The HTTP/2 engine's `isHeadMethod( )` (`Session.h`) folds case, so a request whose method
is `head` sets `expectsNoContent`, and H17 (`9e5cb11`) now rejects a non-empty DATA frame on its
response. The helper dates from `8e2a787`; H17 made it bite. RFC 9110 §9.1: *"The method token is
case-sensitive."* The HTTP/1.1 driver and the pool already match `"HEAD"` exactly.

*If not done.* A request with a case-variant method cannot receive a response body — a 501's
explanation, say.

*Risk, complexity, blast radius.* One line, consistent with the rest of the library. *Undecided was:*
nothing.

**Decided: exact `"HEAD"`.** No reversal condition was found.

*Tests.* HEAD with DATA still rejected; `head` with a 501's body delivered; a mixed-case control.

---

## 4. Astra's recommendations and the decisions

| | Astra recommended | Decided | Difference |
|---|---|---|---|
| R01 | Do not pass a TLS truncation into the close-delimited success path; leniency is reasonable only for a message that delimits itself | Strict, no setting | Same |
| R02 | Bound outstanding bytes, not the total, at ingress; fail on overflow **or** pause HTTP/1.1 reads, left open; the driver's `consumed( )` seam is available | Fail, in the request task, 64 MiB | Same direction; the choices she left open were made |
| R03 | One serialized strand bootstrap — the first read and the transition allowing posted request starts — with operation accounting and cancellation handled there | Her bootstrap | Same, after the first recommendation was withdrawn |
| R04 | Record each callback's count before another can throw; a sink exception stops further automatic delivery and replay | Her version | Same, after the first recommendation was widened |
| R05 | Normalize: collect `Connection` tokens from every occurrence first, remove the named fields, canonicalize every TE; do not make HeaderList drop repeats | The same | Same, plus the `te` exemption |
| R06 | Reuse the URI authority idiom; absent is not invalid; the scheme's default for an omitted port; validate every Host **or** reject duplicates | Refuse duplicates | Same; one of her two options |
| R07 | Exact HEAD only, as on HTTP/1.1; do not uppercase outgoing methods | Exact `"HEAD"` | Same |
| R08 | Fix H24/H25's integration now, independent of codec supply; reverses only on an explicit restriction of registration | The minimal fix, now | Same; the minimal form, leaving multi-layer decoding to the programme |

Her batches were grouped by theme — R01–R04, R05–R07, R08, R09 — and these by the files a lane touches;
the priorities agree.

---

## 5. What stays deferred or carried

- **P1, astra H09** — the decode, and other caller code, under the queue lock — stays deferred as
  decided (`http-content-decoders-deferral.md`); its reach now includes an application that registers a
  decoder.
- **The sink readiness signal and `onComplete( outcome )`** stay deferred with the other `ClientTypes.h`
  changes; D3's reversal condition depends on the first.
- **B4**, `reset( )` on `BodySink`, stays deferred; D4's reversal condition depends on it.
- **Astra's carried risks** — what a caller can still meet as the consequence of decisions already
  recorded, listed in her review's *"Carried risks that are not new remediation requests"* — are
  pointed to from the design's security summary, `http2-design.md` §7. None is owed work: each is closed
  against the decision that produced it and recorded where that decision lives.
- **X1**, the x86-64 Linux matrix, stays deferred by the maintainer.

---

## 6. Corrections made during the round

Recorded because this project's recurring failure is a conclusion resting on a wrong premise, and each
of these was stated to the maintainer before it was caught.

- **H24 and H25 were reported as unreachable** because "no codec ships" — the decoder deferral's
  premise. `registerDecoder( )` is public. Astra's R08; the records were corrected at `425802c`.
- **"21 of the 29 fixed"** counted H08, H16 and H17 as closed; R04 to R07 are their residuals.
- **RFC 2818 §2.2.1 was paraphrased as calling this case "a truncation attack".** It does not use the
  words: it says a client MUST treat a premature close as an error and the data received as potentially
  truncated, and that for a response without Content-Length a premature close from the server *"cannot
  be distinguished from a spurious close generated by an attacker"*. RFC 9112 §9.8, quoted in D1, is the
  text D1 rests on.
- **D2's first recommendation rested on a false conflict with A4, and missed the cancel gap** — both set
  out in D2.

---

## 7. Bucket C, the fallout round — decided and done 2026-09-27, before the change-sets

**What bucket C was.** Items recorded before this review and never decided or scheduled: L6 review
finding 11 and nits 13(b), (c), (e) and (i) (`http2-l6-review-record.md`, whose second pass says
*"the rest stand"*), the comment pass on `TestHttp2DriverWriteBarrier.h` owed since
`initiate-close-teardown-design.md` §16.9, and an owed-list row still reading as scheduled after its
fix merged. Recorded twice without a decision is the signal AGENTS.md names, so they were put as
decisions.

**Taken by the maintainer:** *"I accept all five as recommended. I also accept the plan change to do
bucket C in its own round before the astra work starts. Since bucket C is only comments and docs
changing I don't think it needs the validation you propose. You can skip the build and testing part
[if] there is no real code touched."*

| # | Decision | Taken |
|---|---|---|
| **C1** | Nit 13(b): `SessionHeadersT::applyHttp1Casing( )` has no caller anywhere in `src/` and duplicates the codec's casing lookup. Delete it now, or keep it for L7's S7.3 | **Delete.** S7.3 can route through `Http1Codec`'s lookup when it comes; keeping it is a second unwired copy for whoever wires casing. Reverses if S7.3 were about to start and wanted it |
| **C2** | Nit 13(c): `profile( )` writes, and `redirectPolicy( )` hands out a mutable reference to, what `createRequestTask( )` snapshots with no lock. Document the contract, or add a lock | **Document**, as the decoder registry already does: configured before requests are made or between them, never concurrently with `createRequestTask( )`. A lock could not cover what a caller does through `redirectPolicy( )`'s reference without replacing it by a value setter, an interface change. Reverses if a caller needs to reconfigure a live session from another thread |
| **C3** | Finding 11 and nit 13(e): the text says both halves of the retry call `chkRequestMayBeReplayed( )`, and that `narrowToHttp2( )` serves both policies. Correct the text, or change the code | **The text.** The pool's queued half applies only the budget clause, inline in `examineKey( )`, because a request which never left the queue was never sent — calling the predicate there would add a replayability refusal to requests never sent. `narrowToHttp2( )`'s cleartext line is unreachable, since a cleartext session never negotiates, and harmless. Reverses if the pool ever replays what it has already dispatched |
| **B6** | ThreadSanitizer over the client modules: now and again after the change-sets, or once after | **Once, after CS-1 to CS-3 land**, so that it covers D2's startup handler too — owed, on the ledger |
| **R** | Implement bucket C in the change-sets, or as its own round before them | **Its own round, before CS-1** — the change-sets stay about astra's findings and start from corrected comments. CS-1's own comment corrections stay in CS-1: they explain D1's behaviour change and cannot precede it |

**Done:**

| Commit | What | Checked by |
|---|---|---|
| `d186d2a` | Comments only: C2 at the interface and the snapshot; C3's corrections in `ClientSession.h` (three) and `ConnectionPool.h` (line for line) and at `narrowToHttp2( )`; nit (i), the `#h2-only` marker appended to an empty profile id; the HTTP/1.1 driver's `consumed( )` comment, moved here from CS-1 | Mechanically: every changed line a comment line or blank, no added line carrying a `*/` that could end a comment early, and `createRequestTask( )` read to confirm it takes no lock |
| `7330dd5` | C1: `applyHttp1Casing( )` deleted, 34 lines; `toLowerAsciiCopy( )` kept, which has three other callers | The one code change, so not left to reading: `ClientSession.h` parsed with the project's clang2010 debug flags and `-Werror`, `-fsyntax-only` — clean — with a positive control using a surviving member and a negative control calling the deleted one, which fails as it must. Probes and log in `http2-l0-state/logs/bucketc-syntax/` |
| `d6d1d82` | The write-barrier case: three comments and its two failure messages say one DATA frame per write, not a window's worth | Tier 1: PASS on the tree before; after it, exactly C2 BODY CHANGED on `H2Driver_PeerHalfClosesWithAWriteInFlightTests` and C6 on the edited enum member; PASS again after the refresh. Logs in `http2-l0-state/logs/bucketc-tier1/` |
| `badf642` | The companion tier-1 manifest refresh | Its diff is three entries, all in that file: the case, the member and the namespace block holding it |

**Nothing was built or run**, by the maintainer's decision. The one piece of real code, C1, was
parsed rather than built. Nothing in the round can change behaviour: comments, an uncalled function,
and test text whose assertions did not move.

---

## 8. The implementation run, as set up 2026-09-27

**The maintainer's instruction:** *"For the implementation of CS-1, CS-2 and CS-3 I want to use the
parallel setup with the 3 worktrees as before and do all the work as much in parallel as possible.
Each CS should be a checkpoint and should run fable agent review with effort max back and forth
until you both agree and then implement the feedback and continue the same loop if necessary. Then
run the gate before you declare the CS ready. The gate should be clang release + gcc debug for all
affected modules."* The procedure is [`../parallel-implementation-workflow.md`](../parallel-implementation-workflow.md)
(`dee7ef0`), written at the same request.

**Taken at set-up, all as recommended:**

- **E1 and E2 fold into CS-2**, and so do the L6 review third pass's two open decisions, rescued the
  same day (`138311e`): **E3**, the TLS fallback exchange with a counting sink — the only control for
  the sink refusal D4 changes — and **E4**, renaming the sink case whose fallback retry is gone.
- **D2 and D3 get a design note each, reviewed until agreed, before they are coded**; every
  change-set also gets its checkpoint review.
- **CS-1 is ready after its Linux gate**, with the Windows matrix handed to the Windows agent and
  recorded when it reports.
- **B6**, one ThreadSanitizer pass over the client modules, runs after all three are ready.
- **The session is restarted before kickoff**, so that the lanes (`opus-lane`) and the reviews
  (`fable-reviewer`) run at maximum effort: both definitions live in `~/.claude/agents/`, created
  mid-session, and a session loads definitions only from directories which existed when it started.

| Lane | Branch, from `7d21df3` | Change-set | Reserved new test modules |
|---|---|---|---|
| `swblocks-baselib-lane1` | `astra2-cs1` | CS-1: D1, D2 | `utf_baselib_httpclient8` |
| `swblocks-baselib-lane2` | `astra2-cs2` | CS-2: D3, D4, D5, E1, E2, E3, E4 | `utf_baselib_httpclient9` (cleartext session), `…10` (TLS session) |
| `swblocks-baselib-lane3` | `astra2-cs3` | CS-3: D6, D7, D8 | `utf_baselib_h2client8` |

Each change-set's section here gains its commits, review rounds and gate result when it is ready.

*Corrected 2026-09-27: the table said the branches start at `138311e`. They start at `7d21df3`, the
tip after the fast-forward. Lane 3 caught it, and CS-3's review recorded it.*

## 9. Decisions taken during the implementation run, 2026-09-27

**The design notes.** Both were reviewed until agreed, before any of their code:
- D2's, in two rounds: [`astra2-cs1-d2-startup-handler-design.md`](astra2-cs1-d2-startup-handler-design.md).
- D3's, in two rounds, after the maintainer decided its open point:
  [`astra2-cs2-d3-unread-bytes-cap-design.md`](astra2-cs2-d3-unread-bytes-cap-design.md).

**The first decision round.** The reviews found three things. They were put to the maintainer in the
shape AGENTS.md sets, and all three were taken as recommended.

| # | What | Decided |
|---|---|---|
| 1 | **HTTP/2 reserves at least 1 MiB for every DATA frame.** `blockOf( )` asks `DataBlock::get( pool, max( payload, defaultCapacity( ) ) )`. No in-tree code sets a pool and nothing returns a block to one, so 65,535 one-byte frames at a stalled sink allocate 64 GiB — the stream window bounds payload, not allocation | **Fixed now, in CS-3**: the block is sized to the payload, and a configured pool is still used |
| 2 | **D3's counted quantity** — payload alone does not bound memory against a peer trickling single bytes | **Payload plus an allowance derived from the types** (§3, D3) |
| 3 | **After a timeout or a cancel, the sink still received body bytes** — `applyData( )` had no completion guard | **Folded into CS-2 with D3** (§3, D3) |

**A second decision, the same day, on item 1's premise.** CS-3's third review round found that the
decision's "a pooled block too small for the payload is replaced" did not hold. The replacement,
`DataBlock::createInstance( size )`, is born full, so the write that follows throws, and the frame
is never delivered. The defect is older than this run, and only an application configuring a
small-block pool meets it. **Decided: fixed now, in CS-3**, as recommended: the replacement is asked
for through `DataBlock::get( )`, which empties it. The pool half of item 1 gets its first test.

**Scheduled on sight — a live defect that hands a caller a wrong answer** (AGENTS.md: such defects
are not batched). E2's case found it, the CS-2 lane demonstrated it deterministically with the real
pool, and the fix went into CS-2.
- `refreshEntry( )` reported a retired entry's failure again on every examine while a rider still held
  its slot.
- At `maxTotalConnections` no replacement can start, so one failed establishment was charged twice.
  A queued request then failed without the retry it was owed, and was told "cancelled" in place of
  the establishment timeout.
- **The fix: a failed attempt is reported once per entry, by the check that retires it.** That also
  closes a second hole: a connection that served a request and then closed was charged as a failure,
  the case the pool's own comment forbids.
- The owed list's row I7 carries the evidence.

**Folded in as consequences of decisions already taken** — closed against those decisions, and
reported to the maintainer rather than asked:
- **D1** unpinned the two A1-tls gates. The three cases' peer ended on a truncation, which D1 now
  refuses by itself. The peer now answers our FIN with a close_notify, so the ending is clean, and
  removing each gate turns its own case red again. D1 also made four more comments false, and they
  are corrected.
- **D2's** cases go to a new module, `utf_baselib_httpclient11`, split into `…12` if it grows past
  its size bound. `…8` was already at the target with D1's four cases. The TLS HTTP/1.1 peer both
  modules need is lifted into `Http1DriverTlsTestUtils.h`.
- **D4** stopped a delivery that an existing case counted — a third offer to a sink which had thrown.
  The case now reads two, and still pins H07's property.
- **D6** exempts `host` as well as `te` (§3, D6). A premise in `Session.h` and `TestSession.h` that
  cited RFC 9110 §5.6.2 for case-insensitive `trailers` is corrected to RFC 5234 §2.3, comment only.

**Found and not decided** — recorded as rows I1 to I11 of the owed list, in
[`astra-remediation-owed-work.md`](astra-remediation-owed-work.md).

## 10. The change-sets, as they landed

### CS-3 — D6, D7, D8, and the DATA block sized to its payload — ready 2026-09-27

**Merged at `f48acd2`** from `astra2-cs3`. Tier 1 was re-captured once from the integrated tree
(`130fd87`), with the nine lines it reported read against the manifest diff.

**The commits, and what each red was:**
- **D6** — `d515497`, then the fix `e0cc4a4`. Red: 7 failures.
- **D7** — `07133fb`, then the fix `e3e431d`. Red: 12 failures.
- **D8** — `54df8d7`, then the fix `164a195`. Red: 2 failures.
- **Comment-only commits** — `f4a41cc` and `f8b62d4`.
- **Review round 1** — `host`'s exemption (`fe1a1af` red, then `fced53a`), the h2client8 note
  (`5a79bc3`), and the RFC 5234 premise in `Session.h` and `TestSession.h` (`50c9cd1`, objects
  identical).
- **Decision round 1, item 1** — the DATA block sized to its payload (`c892ae5` red, then `411f8d0`).
- **The pooled-block replacement** — `5025adb` red, then `6cbefba`. The test puts a read-sized block
  into the pool last, because the driver's read block draws first from the same LIFO pool.

**Every red was deterministic**, a pure boundary input or a scripted peer, and was shown red once and
green once.

**Reviewed in four rounds by fable, all agreed**, and closed with no open finding:
- Round 1 found `Connection: host`, which the orchestrator's own review also found.
- Round 3 found the pooled-block replacement.
- Round 4 accepted the test's pool adaptation, which corrected a premise of its own round-3 proposal.

**The gate:** clang release and gcc debug over the twelve affected modules — every module whose
`.d` names `Http2ConnectionTask.h` or `Session.h`, and the new `utf_baselib_h2client8`.
**Green, 24 of 24**, on `f48acd2`:
- `h2client` to `h2client8`, `h2core`, and `httpclient4` to `httpclient6`;
- each recompiled on the merged tree;
- each entered and left every case, printed "No errors detected", and printed no failure and no
  `leaked` line. That was read from each log, not from the gate's own summary.

The logs are kept with the run's evidence, in `logs/astra2/gate/cs3/`.

**New module:** `utf_baselib_h2client8`, 35.4 MB at a64 clang debug, with its reason in its `Main.cpp`.
Its x86 size is owed to the Windows handoff.
