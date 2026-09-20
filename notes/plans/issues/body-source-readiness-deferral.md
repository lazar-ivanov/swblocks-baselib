# A `BodySource` which is not ready has no way to say so

**Raised:** 2026-09-19, in S5.1, while closing the body-pull gap S4.2 found.
**Status:** DEFERRED — the narrower residue of a gap that is otherwise closed. Nothing in the HTTP/2
client work waits on it, and the buffered body mode, which is the default, is unaffected.

## What was closed, so that this is not mistaken for the same thing

The gap S4.2 recorded at `pumpBody()` in `http2/Http2ConnectionTask.h` is **closed**. S5.1 added
`ClientStreamEventSink::onBodyWanted( handle, bytes )`, the h2 driver raises it, and design §5.3 now
describes the pull that exists rather than one that did not. A streaming upload is no longer held
whole inside the driver: one pull is answered by exactly one `provideBody()`, so the driver holds at
most one un-placed chunk per stream and the rest of the body stays with the source.

## What is deferred

**A `BodySource` has no way to signal that it has become ready.** Two frozen facts in
`httpclient/ClientTypes.h` combine into it:

- `BodySource::read()` is **synchronous**. It is called, it returns, and there is no continuation.
- `BodyReadResult` separates `size` from `isEndOfStream` and its own comment says why: *"a source may
  legitimately produce zero bytes without being finished (it is waiting on something)"*. So the
  contract explicitly admits a source which is not ready.

A pull can serve a source that is always ready. It cannot serve one that is sometimes not, because
after an empty answer nothing tells the puller when to ask again. The request task answers such a
read honestly — `provideBody( handle, nullptr, false )` — and the driver re-raises the pull the next
time `pumpAllBodies()` runs, which is a read or a write completion. On a connection carrying other
streams, or one whose peer is sending `WINDOW_UPDATE`s, that arrives soon enough. **On a connection
whose only stream is this upload, and whose peer is silent, it does not arrive at all**, and the
upload stalls until the request's total timeout fires. The request still fails cleanly with
`TimeoutException` rather than hanging, which is why this is a deferral and not a defect.

The alternative to waiting — asking again immediately — was deliberately rejected: it makes the
driver and the request task ping-pong across two thread pools for as long as the source stays empty,
burning both. `applyProvideBody()` carries that reasoning at the point where the decision is taken.

## What would close it

A readiness signal on `BodySource`, so a source that answered empty can say when it has bytes. The
shape is not decided here; the obvious one is a callback the request task installs, which the source
invokes, and which causes the task to offer the driver the chunk it could not produce earlier. Note
that this needs **no** further change to `ClientConnection` — the driver's pull is already
outstanding at that moment, so the answer it is waiting for simply arrives late, which is exactly
what the one-pull-one-answer rule already allows.

**It is a change to `ClientTypes.h`, which is frozen** (see that file's own header: *"a change here
is a change to every consumer and is negotiated rather than made unilaterally"*). That is the whole
reason it is not done in S5.1: the body-pull gap was one landed-contract change, `ClientConnection`'s,
and taking a second one in the same slice for a case nothing exercises yet would be trading a real
fix for a speculative one.

## Who should reopen it

The first caller with a genuinely asynchronous upload source — a body produced by another task, read
from a socket, or generated on demand. Until then every source we have is a buffer or a file, both of
which are always ready, and for those the pull as built is complete.
