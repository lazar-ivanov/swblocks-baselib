# `utf_baselib_http` intermittent failures on Windows: root cause unknown

**Found:** 2026-09-09, while doing focused testing of the modules affected by commit `79488fa` on the
devenv7 Windows host (ARM64 Windows 11, `dist-devenv7-windows-hostarch-a64-targets-a64-x64-x86`,
MSVC 14.38.33130, clang-cl 16.0.5, Boost 1.90.0, OpenSSL 3.5.4), branch `lazari2`.

**Status:** **recorded, not fixed.** The root cause is **unknown**. An earlier IPv4/IPv6 explanation
was written into the plan and then **withdrawn** — see "The withdrawn explanation" below, which is
kept deliberately so the same wrong turn is not taken again.

**Related:** `notes/plans/issues/windows-path-normalization-and-flaky-tests-plan.md` (this is the
"Records to write" item of that plan) and its Fable 5.1 review, which refuted the first explanation;
`windows-blobtransfer-cancel-handle-and-http-reset-flakes-plan.md` (section B) carries the code
trace of 2026-09-09, the capture procedure and the fixes per mechanism.

---

## What was observed

`utf_baselib_http`, run directly from the built exe with
`--catch_system_errors=no --log_level=test_suite --report_level=detailed`:

| Toolchain / variant | Result |
|---|---|
| `vc143` debug, `ARCH=a64` | **52 of 56 cases passed, 4 failed** — 985 of 989 assertions |
| `ccl16` release, `ARCH=a64` | **56 of 56 passed** — 1103 of 1103 assertions |

Note the direction: 52 **passed**. An early draft of this record inverted that and reasoned from
"52 failed"; the four cases which actually failed were:

| Case | Failing assertion |
|---|---|
| `BaseLib_HttpServerPerfTest` | `HttpServerHelpers.h:262` — `UTF_REQUIRE( status == statusCodeExpected )` |
| `TlsHandshake_NameMismatchIsReportedThroughErrorInfo` | `TestTlsHandshakeVerification.h:349` — `attemptConnection( "localhost", control )` |
| `TlsHandshake_AllowUntrustedRecordsAndClearsEndpointInfo` | `TestTlsHandshakeVerification.h:530` — same |
| `TlsHandshake_SniOmittedForAddressLiterals` | `TestTlsHandshakeVerification.h:649` — `UTF_REQUIRE( ! acceptEc )` |

All four open sockets. The error carried on the connect failures is `system:10054`
(`WSAECONNRESET`, "An existing connection was forcibly closed by the remote host").

## The withdrawn explanation, and why it is wrong

The first diagnosis was: the test server binds `"0.0.0.0"` (IPv4 only) while Windows resolves
`localhost` to `::1` first, so the client connects to nothing. **Every load-bearing part of that is
refuted by the code:**

- **The `::1` in the message is not the endpoint that failed.**
  `TcpBaseTasks.h:727-735` (`enhanceException`) decorates *every* client exception with
  `errinfo_endpoint_address( m_endpoint.address().to_string() )`, and `m_endpoint` is assigned once
  from `getEndpoint( endpoints )` (~`:785`) — the **first resolved** endpoint. On a host where
  `localhost` resolves to `::1` first, every `localhost` failure is labelled `::1` whatever actually
  failed. This is the trap the whole explanation was built on. It is already a recorded finding at
  `notes/reviews/major/update_2026/whole-library-cxx-review-fable51.md:297`.
- **The client tries every resolved endpoint.** `TcpBaseTasks.h:1372-1381` uses the range form of
  `async_connect` through `BoostAsioCompat.h:389-411`, so a refused `::1` falls through to
  `127.0.0.1`.
- **The error code is wrong for the theory.** "Nothing listening" is `10061`
  (`WSAECONNREFUSED`). `10054` means a peer accepted and then reset, or a listen backlog overflowed.
- **The readiness probe would have caught it first.** `waitForAcceptorReady`
  (`TestTaskUtils.h:717-772`) connects with the same connector to the same `localhost` before any
  case runs; under the theory every case would fail there, deterministically, on both toolchains.
- The bind is `"0.0.0.0"` at `src/utests/include/utests/baselib/HttpServerHelpers.h:363-382` (not
  under `src/include/`, as first cited). Being a numeric literal it resolves to exactly one
  endpoint, so `TcpBaseTasks.h:1194-1214` taking the first discards nothing.

## What is still true and worth keeping

- The default test-server certificate carries **DNS SANs only** (`localhost`), and
  `TestTlsHandshakeVerification.h:349-360` pins that connecting as `127.0.0.1` **must fail**. So the
  peer name in the positive control cannot simply be changed to an IP literal. The constraint is
  narrower than it first appeared, though: the module already ships a certificate with
  `IP:127.0.0.1` / `IP:::1` SANs (`UtfCrypto.h:139-155`).
- The failures are **intermittent and toolchain-correlated in the observed sample** (vc143 debug
  failed, ccl16 release passed) — one sample each, so the correlation is not established.

## What the code settles (trace of 2026-09-09; details in the fix plan, section B)

- **Why a `10054` fails a case at all.** `BL_TASKS_HANDLER_CHK_EC` fails the task for any error;
  `isExpectedException` only suppresses the log line (`TaskBase.h:139-156`). After the handshake,
  `TcpSslSocketAsyncBaseT::isExpectedException` (`TcpSslBaseTasks.h:399-424`) falls through to
  `TaskBase::isExpectedException` = false, so a reset during the client's TLS **shutdown** also
  fails the task (`onShutdownCompleted`, `:553-608`, line 600) — even though the whole response was
  already received. The plain client completes the body only on a clean `eof`
  (`SimpleHttpTask.h:832-835`); a reset that discards the receive buffer leaves
  `m_httpStatus == 0`, the `HttpServerHelpers.h:262` signature. A client-side 10054 is logged only
  at DEBUG (`TaskBase.h:846-853`) — which is the default level (`UtfArgsParser.h:318`), so a
  captured stdout already holds the phase and code of every failure; the runs above did not keep
  it.
- **The SNI case's `acceptCompleted` is also true on the 30 s timeout path** (the timer cancels
  the acceptor, the accept handler then runs with `operation_aborted`,
  `TestTlsHandshakeVerification.h:613-638`), so `acceptEc` may be a timeout rather than a reset.
  Note also that this record never stated the value of `acceptEc` at all — the `10054` above is
  established for the three *connect-side* failures only. Boost.Asio's IOCP accept completion
  remaps `ERROR_NETNAME_DELETED` to `connection_aborted` (10053, `socket_ops.ipp:193-195`), so a
  peer which resets before the accept completes would most likely surface as 10053 there; 995
  would mean the 30 s deadline fired and nothing connected. The assertion now prints the code
  (`UTF_REQUIRE_EQUAL( eh::error_code(), acceptEc )`), so the next failure settles it.
- **`linger( false, 0 )` verified against the built Boost headers**
  (`boost/asio/detail/socket_option.hpp:227-237`: `l_onoff = 0`), and accepted sockets inherit it
  via `SO_UPDATE_ACCEPT_CONTEXT` (`socket_ops.ipp:221-227`) — linger off in both places, so
  neither is an RST source.
- **`linger( false, 0 )`** (`TcpBaseTasks.h:1206`, `:318`) is linger *off* — a graceful close.
  The two comments describing it as "closed immediately" are wrong.

## Candidates after the trace

Refuted:

- The 3-second server-side handshake deadline (`TestHttpServer.h:2065`) is inside
  `TimeoutHttpSslServerT`, instantiated only at `TestHttpServer.h:2629` in
  `BaseLib_HttpSslServerProtocolHandshakeTimeoutTest`; none of the four cases uses it, the stock
  default is 60 s (`TcpSslBaseTasks.h:73`), and `setProtocolTimeout` has no production caller.
- Process-wide state as a *cascade*: `allowUntrustedCertificates`, `SuppressExpectedWarningsScope`,
  `RealVerifyCallbackScope` and the perf test's `LevelPusher` are RAII and unwind on `UTF_REQUIRE`
  throws. The manual seed of `localhost:28100` into the untrusted-endpoints map
  (`TestTlsHandshakeVerification.h:513-516`) is the one non-RAII item — it stays seeded after a
  failure at `:530`, but nothing later reads it (hygiene defect, not a cause).
- Warning amplification: exactly four assertions failed (985/989), so no warning-driven
  `BOOST_ERROR` fired. `BL_ASSERT`: an assert aborts the process. Backlog / connection cap:
  `SOMAXCONN` and hundreds-to-4096 respectively, and the perf failure was in the sequential block.

Still live, to be separated by the capture:

1. **Windows `shutdown( SD_BOTH )` racing a peer that is still sending** — the only close helper,
   `shutdownSocket` (`TcpBaseTasks.h:241-335`), shuts down both directions; on Windows `SD_RECEIVE`
   resets the connection if data is queued or arrives afterwards. Several server paths reach it
   without a prior TLS `async_shutdown` (`TcpSslBaseTasks.h:627-635`, `:352-360`;
   `TcpBaseTasks.h:2434-2437`, `:1966-1978`).
2. **Port 28100 shared with a foreign listener** — every server module defaults to 28100
   (`UtfArgsParser.h:46`); `utf_baselib_tasks`/`_io` bind `"localhost"`, i.e. `[::1]:28100` on this
   host (`TestTaskUtils.h:838`), and the HTTP client tries `::1` **first**. A concurrently running
   or leftover process there receives the connection and resets it, the readiness probe passes
   against it, and the SNI raw acceptor never sees its connection (timeout). Windows
   `SO_REUSEADDR` (`TcpBaseTasks.h:1205`) also lets a second `0.0.0.0` bind succeed silently.
3. **Server responding to an unparsable request and closing with request bytes unread**
   (`HttpServer.h:183-235`, `:536-574`) — low prior on loopback (the client sends the request in
   one write, `SimpleHttpTask.h:312`), but specific to the 400 request in the perf loop.

## Evidence added 2026-09-09 (solo captures)

The full module was run alone on the box — no other `utf-*` process, no listener on 281xx before
any run, `tasklist` and `netstat` sampled before each run — with stdout saved per run:

| Binary | Runs | Failures | Per run |
|---|---|---|---|
| `win-x64-ccl16-debug`, unmodified | 6 | **0** | ~222 s |
| `win-a64-vc143-debug`, unmodified (the tree of the original observation) | 6 | **0** | ~222 s |

The only 281xx state left behind was the expected TIME_WAIT population on `127.0.0.1:28100`
(about 90 entries after a run). Twelve clean solo runs make an in-process race at the originally
observed rate improbable; the original failure most likely depended on the conditions of that
run (several modules in flight, or a leftover process). A lock audit found every server-starting
case in `utf_baselib_io`, `utf_baselib_tasks`, `utf_baselib_messaging` and `utf_baselib_http`
holding `MachineGlobalTestLock`, so a *properly running* concurrent module is serialized against
the HTTP server — candidate 2 still needs a leftover process or a hung lock holder to work.

Two test-only changes are in the tree so the next occurrence is more informative: the raw accept
assertion prints its error code (`TestTlsHandshakeVerification.h`, `UTF_REQUIRE_EQUAL(
eh::error_code(), acceptEc )`), and the manual `localhost:28100` seed of
`TlsHandshake_AllowUntrustedRecordsAndClearsEndpointInfo` is cleared by a `BL_SCOPE_EXIT`. The two
`linger( false, 0 )` comments in `TcpBaseTasks.h` now describe what the call does. No
mechanism-dependent production change was made.

## What to capture next time it reproduces

Do not theorise from the decorated endpoint again. Capture:

- the exact failing case names and the **first** failing message per case;
- whether the readiness probe passed;
- whether another `utf_*` process was running at the time;
- `netstat -ano -p TCP` for the 281xx range while the module runs;
- whether the same binary fails again on a second run (it is intermittent, so a single green run
  proves nothing).

## Conditions to revisit

- The failures become deterministic, or appear on `ccl16` as well.
- Anyone changes the TLS handshake deadlines, the acceptor, or the test port allocation.
- A dual-stack acceptor is wanted for its own sake — that is a production change to
  `TcpBaseTasks.h:1181-1214` affecting every server in the library (HTTP, blob, broker, messaging)
  and should not be undertaken to chase this symptom without evidence it is the cause.
