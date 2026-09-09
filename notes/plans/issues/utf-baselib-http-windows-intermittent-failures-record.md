# `utf_baselib_http` intermittent failures on Windows: root cause unknown

**Found:** 2026-09-09, while doing focused testing of the modules affected by commit `79488fa` on the
devenv7 Windows host (ARM64 Windows 11, `dist-devenv7-windows-hostarch-a64-targets-a64-x64-x86`,
MSVC 14.38.33130, clang-cl 16.0.5, Boost 1.90.0, OpenSSL 3.5.4), branch `lazari2`.

**Status:** **recorded, not fixed.** The root cause is **unknown**. An earlier IPv4/IPv6 explanation
was written into the plan and then **withdrawn** — see "The withdrawn explanation" below, which is
kept deliberately so the same wrong turn is not taken again.

**Related:** `notes/plans/issues/windows-path-normalization-and-flaky-tests-plan.md` (this is the
"Records to write" item of that plan) and its Fable 5.1 review, which refuted the first explanation.

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

## Unconfirmed candidates, for whoever picks this up

1. **Process-wide state leaking across cases** — `allowUntrustedCertificates()`, the
   untrusted-endpoints map, or the global log level pushed by `SuppressExpectedWarningsScope`
   (`TestTlsHandshakeVerification.h:121-134`).
2. **Warning-as-failure amplification** — `UtfMain.h:124-126` fails a case on any `LL_WARNING`, and
   `tryCatchLog` defaults to `Logging::warning()` around per-connection setup at
   `TcpBaseTasks.h:1125-1131`. A benign transient would then present as a case failure.
3. **Short wall-clock deadlines against a slow debug build** — the 3-second server-side TLS
   handshake deadline at `TestHttpServer.h:2065` is a textbook producer of a client-side `10054`;
   `TestClientHttpTasks.h:2323/2339` has a one-second margin. This fits "debug fails, release
   passes" better than anything else on this list.
4. **Port 28100 hijack** under Windows `SO_REUSEADDR` (`TcpBaseTasks.h:1205`) by a leftover exe or a
   concurrently running module — every server module defaults to 28100 (`UtfArgsParser.h:46`) and
   the project rules permit five modules at once. The bind succeeds silently and every request
   resets.
5. `BL_ASSERT` is live only in debug (`BaseDefs.h:63`), so a debug-only assertion path is possible.

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
