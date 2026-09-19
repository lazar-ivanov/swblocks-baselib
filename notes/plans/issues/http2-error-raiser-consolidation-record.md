# The shared HTTP/2 error raiser goes in `Globals.h`

**Decided:** 2026-09-19, by the maintainer. **Status:** DECIDED, **not yet implemented.** It is its
own change-set, scheduled **after L4**. Nothing is blocked on it.

## What is duplicated

Five headers in `http2/` carry error raisers, and all of them render one of two message formats -
`"HTTP/2 connection error <code> - <reason>"` and the stream equivalent - each attaching
`Http2ProtocolException` or `Http2StreamException` plus the same `errinfo` pair:

| Header | Helpers |
|---|---|
| `FrameCodec.h:521` | `throwConnectionError`, `throwStreamError` - static, and public **only** because `FrameReaderT` in the same header uses them |
| `FlowControlWindow.h:84` | `throwConnectionError`, `throwStreamError`, `throwAtThisLevel` |
| `StreamStateMachine.h:169` | `throwConnectionError`, `throwUnexpectedStreamId` |
| `HpackHuffman.h:56` | `HpackErrorT::throwCompressionError`, already shared with `HpackDecoder.h` |
| `Session.h:1337` | `throwConnectionError` |

## They are not interchangeable, and that shapes the fix

This is **not** "delete four of five". `FrameCodec`'s helpers are `static` and take the stream id as
an argument; `FlowControlWindow`'s are instance methods binding `m_streamId`, and attach the stream
id **even to a connection error**, because a window knows which level it belongs to.

So: extract the common core - exception type, errinfo attachment, message format - and leave a thin
forwarder wherever a class supplies its own state.

**What must NOT be absorbed**, because it is class-specific logic and not duplication:

- `FlowControlWindow::throwAtThisLevel`, which encodes design §6.9's standing rule about which level
  a fault belongs to.
- `StreamStateMachine::throwUnexpectedStreamId`.

`FrameCodec`'s two become **private** once `FrameReaderT` calls the shared pair instead.

## Why `Globals.h` and not a new `Http2Errors.h`

1. **Zero new include edges.** `Globals.h` already defines `errorCodeToString` (`:338`) - the exact
   function every raiser calls - and already reaches both exception classes and the errinfo through
   `BaseIncludes.h` → `ErrorHandling.h:778`. **All six `http2/` headers already include it.** The
   consolidation is purely subtractive everywhere else.
2. **The cohesion is genuine.** The message format and the code-to-string table are one concern. A
   separate `Http2Errors.h` would have to include `Globals.h` anyway, so every consumer would end up
   including both - one idea split across two files for no gain.
3. **It matches the precedent already set inside this feature, and the reason given for it.**
   `HpackErrorT` went into `HpackHuffman.h` rather than a file of its own, and its comment says why:
   *"a fifth header carrying twelve lines would be harder to find than this comment."*

**The honest cost:** `Globals.h` stops being purely constants. It is 411 lines and this adds roughly
40. Nothing pays extra at compile time, since everything in `http2/` already includes it.

## Before starting

**Grep for tests asserting on exception message text.** The extracted strings must stay byte
identical, or cases that match on the message will fail for a reason that has nothing to do with the
refactor. Check this first rather than discovering it in the run.

## Why it was deferred rather than done when found

It was raised at the close of L2, when three lanes were writing into `http2/` concurrently -
creating a shared header mid-flight would have conflicted for no gain. That is the condition to
avoid repeating, so the clean window is after L4 lands, not during it.
