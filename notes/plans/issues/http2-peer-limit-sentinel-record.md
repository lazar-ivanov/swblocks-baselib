# One until the peer has spoken: the driver's half of L5 finding 5(c), and the end of the settle window

**Landed:** 2026-09-20, as one gated change-set (driver, pool, contract, cases). **Status:** DONE
and validated - the release pass and the G1 gate are below. The maintainer reviews the diff himself
before anything merges.

L5 finding 5(c) asked for one change - the driver reporting one free slot until the peer's limit is
known. Making it turned out to require three decisions, and the last of them found a defect.

## The validation

**Release pass:** twelve modules against `clang2010` and `gcc1520`, release - **24 of 24 green**,
rc=0, no leaks. Cases per toolchain: h2client 12, h2client2 13, h2client3 3, h2client4 15,
h2client5 1, h2profiles 14, httpclient 59, httpclient2 9, httpclient3 6, httpclient4 17,
httpclient5 2, httpclient6 1.

**G1 gate:** `gcc1520` debug, whole suite, baseline `1bcde00` - 42 modules, **1009 cases ran of 1020
registered**. (That registered set is not the same quantity as the 1030 the manifest carries, which
counts cases in the tree.) It was differenced against the timer-fix gate, which ran the same
baseline, so the delta isolates this change-set: **22 lines, every one accounted for.** The case
deltas are the three this change-set intends - `H2Pool_AssumptionIsTakenAfterTheSettleWindowTests`
gone with the mechanism it pinned, `H2Pool_TwoRequestsInFlightOnOneConnectionTests` renamed to
`H2Pool_RequestsInFlightTogetherOnOneConnectionTests`, and the count 1010 -> 1009 for that one net
deletion. The two assertion-count lines are **not** this change-set's:

- `BaseLib_Base64UrlTests` 16390 -> 16396, in `utf_baselib`, a module nothing here touches. It is a
  known false alarm: the case is driven by random data with conditional assertions seeded from
  `random_device`, and the L1 round ran the **unchanged baseline binary** eight times for
  16384 / 16384 / 16390 / 16396 / 16396 / 16390 / 16396 / 16366. The line records a different seed,
  not a different behaviour.
- `TlsHandshake_SniOmittedForAddressLiterals` 14 -> 21, pre-existing and identical in every earlier
  gate - S0.3's deliberate IPv6 extension (`d7f5ef0`).

**No pre-existing case changed outcome, disappeared, or moved its assertion count because of this
change-set.**

**The rounds of wording which followed the pass were checked against the gate's own toolchain**: the
orchestrator applied the final tree over the one the gate built and rebuilt both test objects at
`gcc1520` debug - `UtfBaselibH2Client4Main.o` `9d4ea6eec97185c01c6cf8c70158691d` and
`UtfBaselibH2Client5Main.o` `062715e9e1697968b29c49890e5902e9`, byte-identical - so the pass and the
gate stand for the text as it finally reads.

**And the defect in section 4 was shown rather than argued.** With the ordering hunk reverted alone
and nothing else changed, the composed case failed **4 of 40** runs under a concurrent compile; the
committed code failed **0 of 40** under the same load. With a 200 ms stall in place of the publish
it fails every time; with the same stall and the publish moved ahead of the sink it passes, which is
what isolates the ordering from the delay.

---

## 1. The predicate is "the peer has spoken", not "the peer limits concurrent streams"

5(c) is written as *"report `freeStreamSlots( )` as one until `peerLimitsConcurrentStreams( )` is
true"*. Implemented literally that is a permanent regression, because
`Session::m_peerLimitsConcurrentStreams` is set only when the peer's SETTINGS actually carries
`SETTINGS_MAX_CONCURRENT_STREAMS` (`Session.h:2506-2509`), and RFC 9113 6.5.2 gives that setting no
initial value - it is unlimited until a peer sends one. A peer which never sends it is legal and
ordinary, and such a connection would have been pinned at one stream for its whole life.

So the driver now answers three different facts, and only the third is new:

| what the driver knows | what it reports |
|---|---|
| the peer named a limit | the peer's number, minus what is open |
| the peer's SETTINGS arrived without that setting | `ASSUMED_MAX_CONCURRENT_STREAMS` (100) minus what is open - our own ceiling for a peer which has declared no limit |
| the peer has not spoken | `UNCONFIRMED_MAX_CONCURRENT_STREAMS` (1) minus what is open |

"The peer has spoken" is `SessionEventType::SettingsReceived`, which `handleSettings( )` raises for
every non-ACK SETTINGS frame including an empty one (`Session.h:2287-2300`). The driver keeps the
flag itself (`m_isPeerSettingsSeen`, strand state) rather than the session exposing it: the fact the
driver needs is "have I applied the peer's SETTINGS", it learns it from an event it already handles,
and `Session.h` is included by far more translation units than this change needs to touch.

## 2. The pool's inference: `slots > UNCONFIRMED`, and why the band had to go rather than be patched

The old inference took a reading outside `[ assumed - slotsInUse, assumed ]` as the peer's, which
was sound because a silent driver could only report inside that band. **5(c) destroys that premise
completely, not just at the sentinel.** With the driver reporting `1 - used`, its readings while
silent are `{0, 1}` - and that is a *subset* of what a peer which has spoken can report (a peer
allowing two with one stream out reports exactly one). No inference can separate the two.

- Leaving the band alone makes it fire on the sentinel, marking the limit known at the moment it is
  precisely not known; what it then stores depends only on where the refresh lands. At
  `slotsInUse == 0` it stores `peerLimit = 1`. At `slotsInUse == 1` - the pool has answered the
  preface rider and the driver has not opened its stream yet, so the reading is still one -
  `1 + 1 < 100` stores the bound, **2**, and that is the dangerous one: the pool dispatches a second
  request against a number the peer never gave. S6.1's dispatched retry
  (`SessionRequestTaskT::chkPrepareRetry( )`) does replay what a `REFUSED_STREAM` bounced, so the
  ordinary price is a round trip and one of three attempts - and the request itself when its body
  cannot rewind, when the three are spent, or when a strict peer answers with a connection error
  instead. The witness
  case fails on exactly that, at its pre-submit negative control - which is how the shape was
  confirmed rather than argued: a control build with the band restored beside the sentinel was run,
  and that is the assertion it failed.
- Excluding only the sentinel from the lower arm (`slots > 1 && slots + inUse < assumed`) is sound
  but leaves the peer allowing about 100 undistinguished - and that peer is exactly the settle
  window's population, so the window could not then be retired. That is the option this change-set
  considered and rejected, and the reason is in §3.
- **What was chosen:** `slots > UNCONFIRMED_MAX_CONCURRENT_STREAMS` - two or more - and nothing
  else. A silent driver may not publish such a reading, so it is the peer's, whatever its size.
  `ASSUMED_MAX_CONCURRENT_STREAMS` is gone from `ConnectionPoolPolicy` because the pool no longer
  has any use for the driver's assumption; what the two layers must now agree on is the sentinel,
  and `H2Pool_PolicyDefaultsTests`'s `static_assert` was moved onto it.

**What it rests on, and where that is written down.** The rule is sound only if EVERY
`ClientConnection` caps its reading at the sentinel while the peer is silent. That is now a stated
requirement at `ClientConnection::freeStreamSlots( )` rather than a property of the one driver that
happens to honour it. HTTP/1.1 satisfies it by construction - `Http1ConnectionTask.h:1619-1629`
returns one or zero and never more.

**What it costs, named rather than discovered later.** A peer which allows one or two. While the
single request the pool allowed itself is out, such a peer reports one or zero, which is what a
silent driver reports; the pool cannot tell and carries one at a time until that request finishes.
It is under-dispatch and never over-dispatch, it is bounded by the first response, and RFC 9113
6.5.2 recommends no peer be that restrictive. Before this change such a peer was learned at the
first post-SETTINGS reading, so this is a real regression for it - a deliberate one, because the
alternative is believing a reading which a silent driver can also produce.

## 3. Retiring the settle window: what was established, including what it does NOT cover

The window (`DEFAULT_SETTINGS_SETTLE_IN_MILLISECONDS`, `settingsSettleTimeout`, `Entry::settleBy`,
and the branch in `learnPeerLimit( )`) existed for the peer whose limit is about the assumed number
and which therefore never distinguished itself. Three things were established before removing it:

1. **Its population is now covered by the inference, not by the completed-response route.** Under
   `slots > 1` a peer allowing 100 reports 99 with one stream out, and 99 is a reading no silent
   driver can publish - so it is marked known the moment it speaks. The maintainer's stated
   condition for this half was the completed-response route at `ConnectionPool.h:2213-2227`; that
   route is kept and is a real backstop, but it is NOT what covers this population, because the
   shape the window's own case named - a first request which is a download or an event stream -
   never completes a response at all. Had the band been kept and only the sentinel excluded, that
   shape would have been left uncovered, which is the gap this record would otherwise be reporting
   instead of a change.
2. **Keeping it would have been worse than removing it.** The window's mechanism is "after a second,
   take the current reading as the peer's". Under 5(c) the current reading of a peer which has not
   spoken is the SENTINEL, so the window would store `peerLimit = 1` for any peer slower than a
   second and hold the connection at one stream until an idle moment - turning a slow link into a
   serial connection. The window did not merely become redundant; it became wrong.
3. **What remains uncovered is the §2 cost and nothing else:** the peer allowing one or two behind a
   long-lived first request. No window fixes that honestly - after expiry the reading is still
   ambiguous, and the window would have been right for that peer only by the accident that
   `slots + inUse` happens to equal its limit when the driver's count matches the pool's.

## 4. The defect the witness case had actually found

**The record this change-set inherited was wrong about its own evidence.**
`h2client5-concurrent-case-is-load-sensitive-record.md` says the case failed at *"the discriminating
assertion ... where capacity reading 2 while the first request is still in flight"*. Both preserved
failures name `TestConnectionPoolConcurrency.h(675)`, which is the LAST assertion, after all three
requests had completed and been released - not the in-flight one at `:595`. The mechanism the record
inferred (the maintenance tick racing the band inference) is therefore not what happened.

**What did happen.** `closeStream( )` erased the stream and then called `sink -> onClosed( )`, while
its caller published the free-slot count only after that returned. Between those two points the
driver reported a stream it had already closed out as open - stale LOW, which the pool's own comment
says cannot happen (`ConnectionPool.h:1390-1394`, "stale HIGH ... never stale low"). `onClosed( )` is
what gives the pool its slot back, and `releaseStream( )` drops `slotsInUse` to zero and examines in
the same breath - and `slotsInUse == 0` is the one moment `learnPeerLimit( )` takes a reading as the
peer's limit outright. A release landing in that window stores a limit one below the peer's, and
only another idle moment can raise it again.

**The fix is an ordering, in the driver:** `closeStream( )` publishes before it tells the sink. That
makes the pool's documented invariant true, makes `learnPeerLimit( )`'s premise (`limit <= slots +
slotsInUse`) true at the one boundary where it was false, and makes the case's last assertion
deterministic without a rendezvous, since a sink's `waitForClosed( )` returning now implies the
publish has already happened.

**In production the release is posted rather than synchronous** (`HttpClientRequestTaskT::onClosed( )`
posts an event, and `releaseConnectionSlot( )` defers the call), so the window was a race there too
and not a certainty. The consequence was under-dispatch, which is why it survived every green run.

## 5. The cases

- `H2Pool_AssumedLimitIsNotDispatchedAgainstTests`, `H2Pool_PeerLimitIsTakenFreshOnceItIsKnownTests`:
  the stub now publishes the sentinel where it published the assumption. Every assertion is
  unchanged, because the pool's answer to each is unchanged.
- `H2Pool_PeerLimitLearnedFromACompletedResponseTests`: rewritten. For a peer whose reading never
  rises above the sentinel the mark itself is not observable - `capacityOf( )` answers one either
  way - so the case now pins what the proof must NOT do: what stands afterwards is the reading and
  not the pool's ceiling. That is stated in the case rather than left as an apparently strong green.
- `H2Pool_AssumptionIsTakenAfterTheSettleWindowTests`: **deleted** with the mechanism.
- `H2Pool_TwoRequestsInFlightOnOneConnectionTests` -> `H2Pool_RequestsInFlightTogetherOnOneConnectionTests`:
  the peer's limit is now three and a fourth request is queued behind it. Two is unreachable by
  construction under §2 - a peer allowing two reports one with one stream out - so the case would
  have pinned nothing. Three is the smallest limit whose in-flight reading is above the sentinel.
  The negative control also moved BEFORE the first submit, which is the only window in which the
  driver offers a slot the pool declines to take; after the submit the driver reports zero and such
  a wait would be corroborated by the driver rather than discriminating the pool.

**Inventory deltas, deliberate:** one case removed (`…AfterTheSettleWindow…`), one renamed
(`H2Pool_TwoRequestsInFlight…` -> `H2Pool_RequestsInFlightTogether…`), both with their `notes.txt`
recipes.

## Related

- `notes/plans/issues/http2-l5-review-record.md` - finding 5(c), which this implements, and the
  "New, Low" wrinkle it promotes to load-bearing.
- `notes/plans/issues/h2client5-concurrent-case-is-load-sensitive-record.md` - closed by §4.
