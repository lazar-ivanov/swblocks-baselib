# A `BodySink` is told nothing when the request fails, and cannot be replayed onto

**Raised:** 2026-09-23, by S6R.3's H06/H08 change-set, which is what makes both of these observable.
**Status:** DEFERRED — both are changes to `httpclient/ClientTypes.h`, which is frozen, and neither
is needed to stop losing bytes or to stop lying about completion. `s6r3-decisions.md` §2 recommends
recording them rather than building them, and the maintainer accepted that; this file is the record.

## What changed, so that this is not read as a pre-existing gap

Before H06/H08, `applyClosed( )` pushed `sink -> onComplete( )` on **any** close whatever its
outcome. So a sink was told "the body is complete" after a truncation, after a timeout the caller
had already been failed for, and on the ALPN bounce that precedes every first request to an origin
which does not speak h2 — and was then handed the body again by the retried hop. It was delivered in
four of five terminal situations and was true in one.

Now it is delivered **only when the body arrived in full on a close which is the request's answer**,
and `HttpClientRequestTaskT::sinkDelivered( )` refuses a retry onto a sink which has already seen
bytes. `onComplete( )`'s declared meaning — *"the body is complete; no further onData( ... ) will
follow"* — became true of every call, with no word of the contract changed. The two residues below
are what that costs, and both are the cost the decisions document names.

## Residue 1 — a sink which will not see the body gets no terminal callback at all

A caller whose request fails, is cancelled or times out learns of it from the **task**, which fails
with the reason. Its sink learns nothing: it is told `onComplete( )` only on success, and there is no
other method to tell it with.

That is right for a callback whose name and comment say the body is complete, and it is not enough
for a caller who wants to release a file handle or a buffer from the sink itself rather than from
around the task.

**What would close it:** `onComplete( outcome )`, or a second terminal method — a signature change to
a published IID, which `ClientTypes.h`'s own header says *"is negotiated rather than made
unilaterally"*.

**Who should reopen it:** the first caller whose sink owns a resource it cannot release from the
scope that owns the task. Every sink in and out of the tree today appends to a buffer.

## Residue 2 — a hop which reached the sink may not be replayed, so such a request just fails

`chkPrepareRetry( )` refuses a retry once `sinkDelivered( )` is non-zero, because a `BodySink` cannot
be rewound: `BodySource` has `canRewind( )`/`rewind( )` and `BodySink` deliberately has no
counterpart. Replaying would **append** a second, complete copy of the body behind the prefix the
caller's sink already holds — silent corruption, where a failed request is merely a failure.

The configuration this refuses in is `retryIdempotentOnConnectionLoss`, which is **off by default**,
so the default path is unchanged; the ALPN bounce delivers zero bytes and still retries exactly as
before. `ClientSession_SinkIsToldCompleteOnceAcrossTheFallbackRetryTests` is the control for that and
would fail outright, not merely lose a callback, if the refusal reached the bounce.

**What would close it** is option B4 of `s6r3-decisions.md` §2: `canReset( )` / `reset( )` on
`BodySink`, so a sink which can discard what it has taken may be replayed onto. It buys nothing
today — no sink in or out of the tree implements a reset, and the only case it unlocks is behind a
knob which is off by default — and it changes every implementer of a published IID.

**Who should reopen it:** the first caller who wants a streamed download to survive a connection
dying mid-body. Until then that is the caller's own problem and `Range` is where it belongs, which is
what every client without a resettable sink does.

## What this is NOT

It is **not** the readiness gap. A sink which returns zero because it is momentarily full has no way
to say when it has room; that is the sibling of the `BodySource` gap and is recorded with it, in
`body-source-readiness-deferral.md`. Three deferrals on the same frozen file, and if any two are ever
taken up they should be taken together — one idiom for the seam rather than three.
