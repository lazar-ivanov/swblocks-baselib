# S6R.3 H10 — implementation record

**What this is.** The implementation of `s6r3-design.md` §3, in the house pattern of
`s6r1-design.md` §11b: what the code does against what the design specified, what the reading found
that the design did not say, and what the evidence does and does not establish. The design's own
status line and §9 are the status at design time and are left as written.

A separate file rather than a section of `s6r3-design.md`, because three lanes implement that
design's four change-sets and none of them should be editing the same paragraph.

---

## 1. The change, against §3.5

Two pieces and two comments, as §3.5 specifies, plus one the design did not name.

| Piece | Where | §3.5 |
|---|---|---|
| The acknowledgement becomes a `QueuedHeaderBlock` of `STREAM_ID_CONNECTION` | `handleSettings( )`, `Session.h:2455-2483` | item 1 |
| The bound counts the stream-0 entries with the control queue | `checkControlQueueBound( )`, `:1732-1751` | item 2 |
| What an entry of stream 0 is | the `QueuedHeaderBlock` comment, `:445-447` | the closing sentence |
| That one control frame is no longer in `produce( )`'s first group | `produce( )`'s comment, `:916-918` | **not named — see §2** |
| The same clause in the test peer | `Http2TestServer.h:1285` | **not named — see §2** |

`produce( )`, `wantsWrite( )`, `dropQueuedHeaderBlocks( )` and `raiseConnectionError( )` are
unchanged as code, which is what §3.5 says they should be. Verified rather than assumed: the four
readers of `m_headerBlockQueue` the design names are the four in the tree — `wantsWrite( )` `:901`,
`produce( )` `:935-942`, `raiseConnectionError( )`'s clear `:1594`, `dropQueuedHeaderBlocks( )`
`:3661-3676` — and the writer is `queueHeaderBlock( )` (`:3846`) plus this new site. The driver's
`onHeaderBlocksProduced( )` is per produce and iterates its own stream map, so a stream-0 entry is
invisible to it.

## 2. What the implementation found that the design did not say

**Two comments became inexact, and §3.5's list of what this touches named neither.** Both say the
same thing in the same words, and after this change it is true of every control frame *but* the
SETTINGS acknowledgement:

- `produce( )`'s own, *"Control frames first, then whole header blocks, then DATA within the
  windows"* (`Session.h:912-914`). §3.5's "`produce( )` writes the queue in order and is not
  touched" is right about the code and silent about the comment above it — the exact shape this
  feature has recorded seven times. One paragraph added; no code in `produce( )` changed.
- the test peer's, *"Session::produce( ) writes control frames, then header blocks, then DATA"*
  above `isStreamDrained( )` (`Http2TestServer.h:1285`). Its argument — a trailer section queued
  while a body is pending would be written in front of that body — rests only on *header blocks
  before DATA*, which is untouched, so the leading clause is dropped rather than qualified.

**And one behavioural consequence the design does not state.** An acknowledgement in the header
block queue is behind the *control queue* as well as behind the blocks that predate it, so a
control frame queued **after** it now leaves **before** it: a PING acknowledgement
(`handlePing( )`, into `m_controlQueue`), a RST_STREAM, or a graceful `goAway( )`'s GOAWAY. Nothing
in RFC 9113 orders a SETTINGS acknowledgement against a PING acknowledgement, and a SETTINGS
acknowledgement after a graceful GOAWAY is legal on a connection that is draining rather than
closed — header blocks already follow that GOAWAY today. It is written down because it is certain
from the code and **invisible to the suite**: the only outbound PING in the tree is the driver's own
keepalive (`Http2ConnectionTask.h:1896`), so nothing makes a peer PING us while an acknowledgement
of ours is pending.

Two others were checked and are still exact, so they are **not** touched: `applyCancel( )`'s
*"Session::produce( ) writes the control queue before the header block queue, so a RST_STREAM queued
while the stream's own HEADERS are still waiting would OVERTAKE them"*
(`Http2ConnectionTask.h:677-687`) — a RST_STREAM still goes to the control queue, so the sentence
and the hold-back it justifies are unaffected; and `Http2TestServer.h:162`, *"produce( ) writes
header blocks BEFORE data"*.

## 3. The sequencing constraint, verified rather than assumed

§6 forbids H10 running beside S6R.2's H12, which writes the same encoder's construction. H12 is
**in the tree**: `Session.h:572-577` constructs `m_encoder` at
`min( encoderTableSize( profile ), HEADER_TABLE_SIZE_DEFAULT )`, under the H12 comment at `:556`.
So the constraint is satisfied by H12 having landed, not by scheduling.

## 4. S6R.1's H13 lines — untouched, as §3.6 says

`queueHeaderBlock( )` is not in the diff. `Session.h`'s four hunks are at `:444`, `:915`, `:1731`
and `:2454` of the pre-change file, where `queueHeaderBlock( )` begins at `:3789` (`:3846` after).
H13's comment block, its
`firstMaxFragment = maxFragment - FrameCodec::prioritySize( priority )` and its
`first = min( block.size(), firstMaxFragment )` are verbatim, and
`Session_WriteSchedulingTests`'s priority block still asserts `lengths[ 0 ] == peerMaxFrameSize( )`
and passes.

## 5. The third hazard of §3.7 — not fixed, not regressed

`encode( )` still commits its dynamic-table transaction at queue time, and a dropped block still
leaves our encoder holding entries the peer never saw. Nothing in this change touches `encode( )`,
`queueHeaderBlock( )` or any drop path. The four properties that keep it latent were re-read at the
source on this tip and all hold:

- `raiseConnectionError( )`'s clear (`:1594`, the only `m_headerBlockQueue.clear( )` in the file)
  sets `m_isClosed` first and nothing further is sent;
- `handleGoAway( )` calls `m_registry.markDraining( )` (`:2541`) **before** `closeStreamsAbove( )`
  (`:2566`), and `submitRequest( )` refuses on `canOpenLocalStream( )` (`:988`);
- `submitHeaders( )` has no client caller — the four in the tree are the test server's three and one
  session case;
- a local cancel never drops a block: `sendRstStream( )` (`:1660-1690`) clears the stream's
  `pendingBody` and never its block, and the driver's `applyCancel( )` holds a cancel back until
  `onHeaderBlocksProduced( )`.

`forceCloseStream( )` still has exactly two callers (`:3625`, `:3647`). **And the new entry cannot
reach any of this**: `dropQueuedHeaderBlocks( )` matches an exact stream id, which is never 0 —
`closeStreamsAbove( )` and `closeEveryStream( )` both iterate `m_streams`, whose keys are opened
stream identifiers.

## 6. The cases, and what each is worth

Three cases in `utf_baselib_h2core`, `TestSession.h`. **Three and not one block**, so that neither
half can hide behind the first `UTF_REQUIRE` the other trips.

| Case | Before | After |
|---|---|---|
| `Session_StaleHeaderBlockPrecedesTheSettingsAckTests` | **RED** — `EQUAL( types[ 0 ], FRAME_TYPE_HEADERS )` at `TestSession.h:4725` | green |
| `Session_StaleFragmentSizePrecedesTheSettingsAckTests` | **RED** — the same check at `:4806` | green |
| `Session_SettingsAckPrecedesABlockEncodedAfterItTests` | green | green |

The third is the control §3.8 says is owed, and it is the one that makes the shape a *position* for
the acknowledgement rather than a deferral of it: it is green on both sides of this change and would
be **red** under the second buffer §3.5 first specified. It asserts the order *and* that the block
behind the acknowledgement opens with the size update, so it cannot go green on a mechanism which
put that update on the wire first.

**Both halves also assert the substance, which §3.8 proposes and this takes.** The HPACK case pins
that the stale block opens with no Dynamic Table Size Update and that the first block encoded after
it does — order is what 6.5.3 settles, the update is what 4.3.1 demands, and a case pinning only the
first would go green on a mechanism which got the second wrong. The frame-size case pins that the
stale first fragment is 32768, the limit the peer could still rely on when the block was framed.

**Two pins that stop a case passing vacuously**, both deliberate: the HPACK case produces one
request before the one it queues, so the encoder's dynamic table is not empty when the reduction
arrives — and asserts the second block is *shorter* than the first, which is what shows the table
really filled. The frame-size case asserts every frame between the HEADERS and the acknowledgement
is a CONTINUATION, because the acknowledgement now shares the queue the block's atomicity comes
from.

## 7. Evidence, and its limit

`ub24-a64-clang2010-debug`, clang debug only, one module at a time, through the lane's build slot.

- `utf_baselib_h2core`: **74 cases, exit 0, no failure** (71 before — the three added ones).
  Red-before was taken on the same module with the same three cases and the unfixed `Session.h`.
- `utf_baselib_h2client2`: **14 cases, exit 0** — the driver-level module, taken because this is a
  core-path change to the engine and that module is where a write-ordering regression would show.
- `utf_inventory.py` against a manifest of this tip's `HEAD`: **the only difference is the three
  added cases**. No case body changed, no helper lost, every `notes.txt` recipe resolves. (The
  repository's checked-in baseline is older than several landed slices, so `check_split.sh`'s tier 1
  is red against it for reasons that are not this change's; the `HEAD` comparison is the one that
  speaks about this change.) Line endings pass.
- Object size, `UtfBaselibH2CoreMain.o`: **36,789,912 → 36,840,600 octets**, +50,688 (+0.14%). The
  baseline was measured by compiling the pre-change sources with the identical command line, not
  inferred.

**The manifest refresh §3.8 owes is deliberately NOT taken here, and this says so rather than
leaving it looking done.** `notes/reviews/major/update_2026/baseline/inventory.json` is a whole-tree
capture, and it is already 19 cases behind `6a6e10c` — several landed slices did not refresh it. A
capture from this lane would bless those 19 as well as these 3, and two other lanes off the same
base would each produce a conflicting capture of a 1000-entry JSON file. It belongs to whoever
merges the three, after the merge.

**What this does not establish.** The governing size measurement is `x86` `debug` against the 40 MB
target, and this host cannot build it; the a64 number is the available signal and the delta is what
carries. Release, gcc and the whole suite are the orchestrator's, per §7. And the peer behaviour the
design argues from — that nghttp2, Netty and QUICHE each bound an incoming size update by the
setting they have applied — was read from their sources by the design review and is **not**
exercised here: no run in this repository speaks to it.

---

## 8. Against the design — what was found wrong, and what was only imprecise

Nothing that changes the decision or the shape. Recorded in the spirit of the design's own §8.

1. **§3.5's list of what this change touches is short by two comments** (§2 above). It names the
   `QueuedHeaderBlock` sentence and nothing else, and says "`produce( )` writes the queue in order
   and is not touched" — true of the code, silent about the comment above it and about the test
   peer's copy of the same sentence.
2. **§3.8's frame-size recipe says "a header block exceeds 16384", and the assertion it then asks
   for needs it to exceed 32768.** "Assert ... that its length is what the old limit allowed" reads
   as `== 32768` only when the block is larger than the old limit; between 16384 and 32768 the
   first fragment is the whole block, which is over the *default* but is not the old limit. The
   case uses a 100000-octet header value, so it is exact. The recipe's intent is right; the number
   in it is the wrong one of the two.
3. **§3.5's "the four readers" is correct, and this confirms what the design review left to the
   lane.** §11's "whether a stream-0 entry disturbs any reader of `m_headerBlockQueue` the reviewer
   did not find is the lane's to confirm": there is no fifth reader, and the driver's
   `onHeaderBlocksProduced( )` runs once per produce over its own stream map, so it cannot see one.
4. **Every line number in §3 has drifted**, as §0 warns; each was re-derived. The anchors as they
   now stand: the `QueuedHeaderBlock` comment `:437-444` (§3.5 cites `:416-426`),
   `raiseConnectionError( )`'s clear `:1586` before this change (`:1530`), `dropQueuedHeaderBlocks( )`
   `:3604` (`:3460`), `m_controlQueue` `:469` (`:445`), the `SETTINGS_MAX_FRAME_SIZE = 1024` case
   `TestSession.h:3602-3623` (`:3558-3577`).
