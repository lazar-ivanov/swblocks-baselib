# The two HTTP/2 exceptions are not in createExceptionFromObject(), deliberately

**Decision:** 2026-09-18, while implementing plan slice S1.3. **Status:** DEFERRED - a seam, not an
omission. Nothing in the HTTP/2 work waits on it.

## What was not done, and why the question arises

`core/ErrorHandling.h` carries an instruction above its list of well known exceptions:

> When adding a new exception below remember to add a relevant code in `createExceptionFromObject()`
> in `baselib/data/eh/ServerErrorHelpers.h`

S1.3 adds `BL_DECLARE_EXCEPTION( Http2ProtocolException )` and `Http2StreamException` to that list
and **does not** touch `ServerErrorHelpers.h`. That is not an oversight of the instruction: S1.3's
work order says in as many words that **no existing code path changes**, and
`createExceptionFromObject()` is an existing code path on the async-RPC error route
(`messaging/asyncrpc/ConversationProcessingBaseImpl.h:69`). Under the rule the design's §3.8 set for
this work, a modification of an existing path is its own change-set, tested and gated separately;
it does not ride along with an additive slice.

`HttpException` and `HttpServerException` are both in that chain, so the symmetry argument for
adding the two HTTP/2 ones is real, and this record exists so that the next person does not have to
rediscover the question.

## What the behavior is today, exactly

The two directions are not symmetrical, and only one of them has a gap:

- **Serializing works already.** `createServerErrorResultObject` writes `e.fullTypeName()` into the
  document (`ServerErrorHelpers.h:164`) with no whitelist, so a `bl::Http2ProtocolException` crosses
  the wire as `"bl::Http2ProtocolException"` carrying all of its `errinfo_*` properties.
- **Rebuilding falls back.** `createExceptionFromObject()` ends in an `else` which returns
  `exceptionFromProperties( …, UnexpectedException() )`, so the document is rebuilt as a
  `bl::UnexpectedException` with every property preserved - the message, the error code, the
  category and the diagnostic dump. Nothing is lost but the C++ type, and nothing throws.

## When it would matter, and what to do then

Only when a **baselib server** raises an HTTP/2 exception and a baselib client rebuilds it from a
server error document, and then discriminates on the type rather than on the properties. That
cannot happen yet: the HTTP/2 code this design adds is a **client**, and production HTTP/2 in
`HttpServer` is itself deferred (D8, `notes/plans/issues/http2-server-side-deferral.md`). The client
catches its own exceptions in process, where the type is intact.

If it does become needed, the change is two `else if` arms beside the `HttpException` one, plus a
round-trip case in `src/utests/utf_baselib_data/TestServerErrorHelpers.h`, which already covers this
function - and it lands as its own change-set, gated on the whole suite, per design §3.8.
