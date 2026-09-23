# The astra L0–L6 review: verification record and remediation staging

**Date:** 2026-09-22. **Status:** verification complete; **no remediation implemented**. This record
says which of the 29 findings survive checking, which are rediscoveries, what each costs, and the
order they should be taken in.

**Source:** `notes/plans/http2-l0-l6-architecture-security-review-2026-09-21.md` — an architecture and
security review of L0–L6 by GPT-6 ("astra"), 29 findings on the implemented client (H01–H29) plus 11
on the parked decompression proposals (C01–C11).

**Method.** Every finding was re-derived at the source by a reader who did not write it: H01 and
spot-checks by the orchestrator, H02–H06/H09 in one worktree, H07/H08/H10–H18 in a second,
H19–H29 in a third. **Nothing was built or run** — astra's review was static and so was this
verification. Where a verdict needs a run to settle, it says so.

**Headline: 29 of 29 stand in some form. None was refuted outright.** Two were narrowed materially,
one had half of it refuted. **Eleven are genuinely new; the rest are rediscoveries of items already
on the L2–L6 ledgers**, which is itself a useful result — the ledgers are accurate.

---

## 1. What is new, and what is already recorded

| Finding | Verdict | New? | Cost |
|---|---|---|---|
| **H01** h1 buffers invalidated while `async_write` is pending | CONFIRMED | **new** | small, core |
| **H02** unfinished write read as "not sent" → **silent duplicate POST** | CONFIRMED | **new** | ~4 lines, core |
| **H03a** disposal null-derefs `m_eqConnections` → **segfault** | CONFIRMED | **new** | ~6 lines, core |
| **H03b** maintenance timer race | PARTIAL — real UB, benign outcome | new for pool | ~4 lines |
| **H04a/b** driver + negotiated published unsynchronised | CONFIRMED | pre-recorded (L5 f7) | 1 line / ~15 lines |
| **H05** unbounded aggregate 1xx | CONFIRMED | **new** | ~50–70 lines, additive |
| **H06** partial sink consumption loses bytes, reports success | CONFIRMED | half new | 100+, contract change |
| **H07** throwing sink callback becomes success | CONFIRMED | **new** | ~5–10 lines, core |
| **H08** retry reuses a `BodySink` after its terminal callback | CONFIRMED | **new** | ~20–40 lines, core |
| **H09** caller code + decoding under the queue lock | CONFIRMED | duplicate (L6 f6) | structural |
| **H10** queued header block follows a SETTINGS ACK with stale limits | CONFIRMED | **new**, contradicts L3 | large |
| **H11** decoder accepts a missing mandatory HPACK size update | CONFIRMED, deliberate | **new** | ~25–35 lines |
| **H12** encoder table capacity neither clamped nor announced | CONFIRMED (>4096 only) | **new**, contradicts L3 | ~5–10 lines |
| **H13** prioritized HEADERS exceed peer max frame size | CONFIRMED | **new** | 3–5 lines |
| **H14** padding-only DATA wedges a stream window | CONFIRMED | **new** | 3–5 lines |
| **H15** reduced initial window keeps an oversized update threshold | CONFIRMED | **new** | ~10–15 lines |
| **H16** outgoing headers unvalidated for HTTP/2 | CONFIRMED | **new** | ~20–30 lines |
| **H17** bodyless responses accept nonempty DATA | CONFIRMED | **new** | ~6 lines |
| **H18** no initial non-ACK SETTINGS enforcement | CONFIRMED | **new** | ~15 lines |
| **H19** plain client needs OpenSSL headers | PARTIAL, recorded & accepted | no | ~10 lines |
| **H20** URLs with userinfo in timeout exceptions | PARTIAL — logging half **refuted** | **new** | ~20 lines |
| **H21** h1 selection consumes a retry | CONFIRMED | duplicate (L6 4a) | ~15 lines |
| **H22** cancel between hops reports the 3xx as success | CONFIRMED | duplicate (L6 f9) | ~10 lines |
| **H23** one knob, two budgets | CONFIRMED | duplicate (L6 f10) | doc or ~20 lines |
| **H24** Content-Encoding as one value not a list | PARTIAL — **latent** | **new** | ~80–120 lines |
| **H25** decode on bodyless / failed responses | split: failed half duplicate (L6 f12); bodyless half **new**, latent | half | ~25 lines |
| **H26** CONNECT renders IPv6 without brackets | CONFIRMED, **live** | **new** | ~10 lines |
| **H27** cookie names merged case-insensitively | CONFIRMED | duplicate (L6 nit 13a) | ~8 lines |
| **H28** non-HTTP jar mode overwrites an HttpOnly cookie | CONFIRMED | duplicate (L2 S2.7) | ~8 lines |
| **H29** Accept-Encoding ≠ identity | PARTIAL — **code already correct**, docs wrong | docs | 2 sentences |

---

## 2. Corrections to the review

Recorded so that the review is not treated as uniformly right, which would be as damaging as
treating it as wrong.

- **H20's logging half is refuted.** No `BL_LOG` in the new client emits a URL, and the framework's
  exception dump cannot fire for this task: it is gated on a task name `HttpClientRequestTaskT`
  never sets, on `isExpected` (true for timeouts), and on debug level. The URL reaches the caller
  **inside the exception only** — an API-contract gap, not a leak.
- **H29's code is already correct.** A no-decoder client receiving `Content-Encoding: br` returns the
  raw bytes with the header intact, deliberately. Two *sentences* are wrong — in the decoder deferral
  and in `ContentDecoder.h` — claiming servers send identity bodies. Documentation defect only.
  **Corrected 2026-09-22 while fixing them: it is three sentences, not two.** The deferral carries
  one in "Why there is a question at all" and a second in "What the deferral costs"; the third is in
  `ContentDecoder.h`'s `ContentDecoderRegistryT` class comment and not, as the R4 staging assumed,
  in its file note. The file note's account of the omitted `accept-encoding` is accurate.
- **H13 overshoots by "exactly five".** It is up to five: five only when the block is at least the
  frame limit, one to four for blocks just below it.
- **H12's second sentence overshoots.** A *smaller* unannounced capacity is harmless (the peer's
  table is a superset in the same order); only >4096 is a defect.
- **H24 is not silent.** For an unsupported coding the body and its header are handed back intact by
  design. The real hazard is a repeated identical coding, where one layer is stripped and both header
  fields are deleted.
- **H11's justification is not stale — it was written against the wrong RFC.** The comment cites
  RFC 7541 §4.2 (the encoder's duty). The decoder-side MUST is RFC 9113 §4.3.1, which it never
  engages.
- **H04's two halves are not the same size.** Half B reads an enum — a formal race with no plausible
  misbehaviour. Half A is unsynchronised smart-pointer publication, a genuine hazard on a64.
- **H19 is a boundary this project already recorded and accepted, and here is where** (added
  2026-09-22, so that the finding leads to its answer instead of being re-argued). The include is
  `crypto/CryptoBase.h` at `ClientConnectionTaskBase.h:29` with the `OPENSSL_VERSION_NUMBER` guard
  at `:48`, and the comment above the guard says why it is keyed on the capability rather than on a
  devenv version. The consequence — that this header reaches OpenSSL, so it is kept out of
  `httpclient/PreCompiled.h` — is written into `PreCompiled.h` itself, and
  `http2-l4-review-record.md` records that placement as checked and accepted in its verdict on the
  L4 range. Astra's remedy (move the specialization behind a TLS-specific header) remains a
  reasonable future shape; it is owed work, not an unrecorded defect.

## 3. What the review found that our own ledgers missed

- **A validation trap.** `ClientSession_FallbackRiderNeedsTheDispatchedRetryTests` asserts *failure*
  with retries disabled. When L6 4a lands, that test **keeps passing silently** unless it is
  inverted. L6's 4a condition does not mention it. **Recorded against 4a in the L6 record on
  2026-09-22, and narrowed while recording it: "keeps passing silently" holds for one of 4a's two
  candidate shapes, not for both.** The case builds a *default* `ClientSessionConfig`, so
  `cleartextProtocol` is `Http11` while the ALPN offer still names `h2`. A per-key decision —
  astra's own wording under H21, *"avoid dispatching a rider when the selected protocol is already
  known to be h1"* — makes the request succeed and the case fail **loudly** on `isFailed()`. A
  per-session flag, which is what 4a's own wording and a pool-wide `ConnectionPoolPolicy` imply,
  leaves a default session still able to produce h2 over TLS, so the rider still rides and the case
  **passes unchanged** — reading as evidence for a fix that never touched this path. Either way it
  must be revisited deliberately; the trap is real and its shape is conditional.
- **H09's deadline point.** The completed hop has already cancelled its timers and the chain budget
  is a timestamp checked synchronously, so decoding between hops is **both uncancellable and
  undeadlined**. L6 f6 did not say this.
- **H21's alternative remedy.** Hand the untransmitted request to the selected driver instead of
  bouncing it. This also fixes the TLS ALPN-fallback case, which 4a's policy flag cannot, because
  ALPN is not known at construction.
- **Sequencing.** H24 and H25 are prerequisites of the decoder programme, not standalone defects.
  L6 f12 reads as a small nit and is in fact a gate on that whole body of work.

## 4. Where this contradicts a prior verdict

**H10 contradicts `http2-l3-review-record.md:280-287`**, which recorded the HPACK encoder mirror as
conforming "including the smallest-then-final rule". That reading examined `encode()` in isolation
and never considered a block **already serialized** into the queue. **H07 partially contradicts
`http2-l5-review-record.md:143-150`**, which examined the deferred-callback path and cleared it,
without opening `failWith`'s guard.

**H12 is a gap L3 did not consider, not a verdict it got wrong** - an earlier draft of this section
said "contradicts" for both. L3 has no mention of construction-time encoder capacity at all; its
verdict covered the mirror only. The distinction matters because it changes what the L3 record is
worth: it was right about what it looked at.

These are the most valuable findings in the set, because they are where a previous reviewer looked
and concluded wrongly — the one thing an independent reviewer buys that another pass by the same
reader does not.

## 5. Three that need a design decision before any fix

1. **H06.** `HttpClientRequestTask_StreamingSinkCreditsOnlyWhatItTookTests` delivers 10 bytes,
   asserts the sink received 6, and asserts success. The current behaviour is **pinned by a test**,
   so fixing it overturns a decision. Amend the design record first.
2. **H08.** Either own sink completion at the logical-request level, or forbid transparent retry once
   response bytes have escaped — the latter is a change to the frozen `BodySink` contract.
3. **H11.** Being lenient where the RFC says MUST error is a conformance choice. It has no safety
   consequence and no interop break; decide whether conformance is wanted before spending the lines.

---

## 5a. Two defects a fifth pass found that astra and all three readers missed

Found 2026-09-22 by an independent reviewer over this record and the four passes behind it.

**N1 — the h1 driver caps *streamed* bodies at 64 MB; h2 does not.**
`Http1ResponseLimits::DEFAULT_MAX_BODY_SIZE` (`Http1Codec.h:212`) is handed to Beast's `body_limit`
for **every** h1 response, with no knowledge of whether a `BodySink` is installed; the request task's
own cap applies only to the buffered path. The design's limits table scopes that 64 MB to
**buffered mode** (`http2-design.md:748`). So a streamed download over 64 MB **fails on HTTP/1.1 and
succeeds on HTTP/2**. A session can raise `http1Limits.maxBodySize`, but nothing is per-request and
nothing makes the two protocols agree. Small fix; belongs with the sink-semantics work.

**N2 — the h1 driver compares peer-close error codes by hand, breaching a rule now written down.**
`onReadCompleted` (`Http1ConnectionTask.h:1003`) tests `asio::error::eof == ec` directly.
`issues/windows-peer-close-error-codes-record.md` now states the rule in terms: *"Do not compare an
asio transport error code by hand in networking code. Not `connection_reset`, not
`connection_aborted`, not `eof`. Ask `net::isPeerClosedErrorCode()` or
`net::isOrderlyPeerCloseErrorCode()`."* The h2 driver complies at `Http2ConnectionTask.h:1501`; h1
does not, and the h1 read is full-duplex, which is the shape that produces `connection_aborted` on
Windows. **Upgraded 2026-09-22** by commit `2b4c61b`, which measured the mechanism rather than
hypothesising it. The breach is certain and the fix shape is one file away; the manifestation
remains inferred. Note the record's own warning: **a Linux-only run cannot catch a breach of this
rule**, so this fix's acceptance needs the Windows matrix.

**N3 — OPEN QUESTION, not yet a finding: is a truncated body reported as complete?**
Commit `2b4c61b` measured that a Windows reset discards **all** unread bytes (16 KB sent, none
delivered) and its message infers *"a response body can be short while the task reports success"*.
The byte loss is measured; **the success half is not established**, and one link argues against it:
`onPeerClosed` routes through `closeAllStreamsUnwrittenRetryable( )`, which closes each stream with
an error code rather than a success.

What **is** established by reading: the declared-content-length completeness check is gated on
`endStream` (`Session.h:2188`, `endStream && lengthIsChecked && total != declaredContentLength`), and
a peer close carries no `END_STREAM`. So nothing compares a short body against its declared length on
that path. Whether that reaches the caller as a failure or as a short success depends on the stream
close code and on what the request task does with it.

**Settle this before S6R.2**, by reading the close path end to end and, if needed, one Windows run.
Do not carry the commit message's phrasing into a document as fact — it is an inference in a commit
message, and this project has been bitten four times by exactly that.

---

## 6. Proposed staging

Grouped so each change-set has one theme and can be gated on its own, per the project's rule that
core-path changes gate separately.

### R1 — the cheap, contained, high-value set (recommended first)
`H02`, `H03a`, `H13`, `H14`, `H17`, `H26`, `H04b`, `H27`, `H28`. Roughly 60 lines total across
seven files, every one deterministically testable, three of them one-liners. Removes a silent
duplicate POST, a shutdown segfault, two protocol violations, a wedged stream window and an invalid
CONNECT authority. The nine touch disjoint functions, so one change-set is safe — with two caveats:
**H03a buys the segfault only** (a batch collected before disposal can still schedule after the
sweep), and **H27 must not touch the shared `contains( )`**, whose case-insensitivity is
load-bearing for the Accept-Encoding intersection; give the cookie merge its own exact compare.

### R2 — the correctness set that needs more care
`H01`, `H07`, `H05`, `H12`, `H15`, `H16`, `H18`, `H03b`, `N1`, `N2`. Larger, still bounded, each with
a clear test. `H01` is the one with real design content (a write-completion barrier) — and a proper
barrier is also what would let `H02` be answered exactly rather than conservatively, so expect S6R.2
to revisit S6R.1's H02 lines.

### R3 — the ones gated on a decision
`H06`, `H08`, `H11`, `H09`, `H10`, `H04a`. Decide first, then implement. `H10` is the most expensive
item in the review and its cheap fix collides with an existing GOAWAY test. `H06` and `H08` are one
concept — "response bytes have escaped to the sink" — tracked once in the request task, and should be
decided together.

### R4 — documentation and ledger hygiene
`H29` (two sentences), `H20` (redacted renderer), `H19`, `H23`, the 4a test-inversion trap, and the
stale "Windows has not run any of it" line in the L4 record.

### Deferred to the decoder programme
`H24`, `H25` — latent until a codec ships, and prerequisites of it.

---

## 7. What this does not establish

No finding here was **demonstrated by running anything**. Both astra's review and this verification
were static. The races (H03, H04) are certain from the source and probabilistic in consequence; the
protocol violations (H10, H12, H13, H16, H17, H18) are derived from RFC MUSTs, not from a run
against a real peer or h2spec. Before R1 and R2 are called done, the cheap demonstrations are:
`produceText` byte assertions in `utf_baselib_h2core` for the frame-shape findings, `feedText` with
illegal frames for H17/H18, a throwing sink for H07, and TSan over a concurrent case plus a racing
`dispose()` for H03/H04 — the recipe in
`notes/plans/issues/http2-driver-timer-cancel-cross-thread-race-record.md` §5.
