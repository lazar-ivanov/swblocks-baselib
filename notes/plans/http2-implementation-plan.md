# HTTP/2 Client Library: Implementation Plan

**Status:** written 2026-09-17 as a plan with nothing built, run or probed. **Layer L0 was executed on
2026-09-17/18** - slices S0.1-S0.4 implemented in parallel worktrees, merged, release-validated, and
the G1 gate (S0.5) passed at `gcc1520` debug; see §2 for the result, its two stated limits and the
three follow-ups it left. **L1 and L2 were executed on 2026-09-18/19** (§3, §4); **L3 onwards is still
unexecuted.** Every probe outside L0-L2 remains folded
into the slice that needs it, to be run by the implementing agent at execution time.

**Spec.** This plan implements `notes/plans/http2-design.md` ("the design"). The design is the
specification; this plan is the work breakdown, the dependency graph, the parallelization schedule and
the verification method. Where a slice says "implements §X", read that section of the design for what to
build - the plan does not restate it. Decision ids `D1`-`D26` are the design's ledger (its section 0).

**Companion deferrals** (do not implement): `notes/plans/issues/http2-server-side-deferral.md`,
`notes/plans/issues/http-content-decoders-deferral.md`.

---

## 0. How to execute this plan

**A slice is one unit of work.** Each has a work order below: what it implements, its deliverables, what
it depends on, its earliest possible start, any probes, and its acceptance criteria and tests. A slice
is sized for one agent; three slices are large enough to want a sub-team but are still one integration
unit (S2.2 HPACK, S3.1 Session, S5.2 ConnectionPool).

**Layers and slices.** Work is grouped into layers L0-L8. A layer's slices are, with annotated
exceptions, parallel with each other. A layer depends only on layers below it. Every cross-slice edge is
named in the work order and summarized in §12, so the true schedule is explicit rather than implied by
layer order.

**Development parallelism is higher than integration parallelism.** Agents work in isolated worktrees.
A slice's *development* can begin as soon as the code it builds on exists in some branch, even before
that branch is integrated. A slice's *integration* follows the gating order of §12. The "earliest start"
field is for development; the wave table in §12 is for integration.

**Before you start any slice, read:** the root `AGENTS.md` (loaded automatically), `src/utests/AGENTS.md`,
the design's section 0 and the sections your work order names, and every existing header your work
order cites by path - the library's idioms are learned from those files, not from this plan.

**This plan governs sequencing.** Where its layering differs from the design's §10 "Lands in" column or
§11 phase table (it consolidates, for example, every additive change to `AsioSslStreamWrapper.h` into
one L1 slice with one owner), this plan is what is executed and §13 records the mapping. Everything
else - what to build and why - is the design's, and the plan never overrides a decision `D1`-`D26`.

**The build boundary.** Creating this plan ran no build. Executing it does: each slice is built and its
focused test module run. The default cadence, matching how this maintainer implements a plan, is
**gcc1520 debug build-and-run of the affected module while developing a slice, then one clang2010 release
pass over the affected modules at the end of a layer.** The exception is the G1 gate (L0), which is the
full suite - its breadth is the maintainer's call at execution time; do not assume it, ask. Do not
untar any dist, and do not widen a build beyond this without being asked: this repository forbids
unrequested full-repo builds, a build of more than one module is never parallelized (`-j1`), and tests
run at most five modules concurrently (`-j5`). Fuzz harnesses named below are optional: build them as a
standalone test app or a case gated behind an environment variable; do not add a makefile target for
them without asking.

**Git.** This maintainer commits his own work. Hand back working-tree changes per slice with the
evidence its acceptance asks for; do not `git add`/`commit`/`push` unless asked. The integration order of
§12 is a merge DAG for the maintainer (or a coordinating step) to drive, not a licence to commit.

**Test modules (D20, §8.1 of the design).** Four modules: `utf_baselib_h2core`, `utf_baselib_h2client`,
`utf_baselib_httpclient`, `utf_baselib_h2profiles`. `utf_baselib_http2` is **taken** (it is the numbered
sibling of `utf_baselib_http`) - do not use it. Conventions, so parallel agents do not collide:

- **S1.8 scaffolds all four module directories first** (a `…Main.cpp`, a `devenv7_only` marker, an empty
  `notes.txt`). A feature slice then adds its own `Test<Feature>.h` and appends **one** `#include` line
  to that module's `…Main.cpp`. Different slices append different lines; a collision there is a trivial
  merge. No slice edits another slice's `Test*.h`.
- **Never `#include` a test header across module directories** (`src/utests/AGENTS.md`).
- **Watch object size.** After a slice lands tests, run `make utests-sizes`. `h2core` (Session +
  HPACK conformance) is the likely first to approach the 40 MB target; if it does, split into
  `utf_baselib_h2core2` per `src/utests/AGENTS.md` - the plan budgets for this in S3.1's acceptance.

**Header-only and idioms.** Every production header follows the library idiom: `template< typename E =
void > class FooT`, `typedef FooT<> Foo`, `typedef om::ObjectImpl< Foo > FooImpl` where it is an object,
statics via `BL_DEFINE_STATIC_MEMBER`. New Boost usage is isolated the way the library already isolates
Boost (one `core/detail/*BoostImports.h` header, `using` declarations, a facade in library terms - see
S1.5). OpenSSL stays an optional dependency: headers needing it are separate and out of any
`PreCompiled.h` (design §1.5, §9).

**Verifying against the design.** Two mechanisms: every slice's acceptance names the design section and
the test that proves it; and §13 is a traceability matrix from every decision `D1`-`D26` and every design
section to the slice(s) and evidence. S8.3 is the slice that fills the matrix in at the end.

---

## 1. Layer and slice map

| Layer | Theme | Slices | Depends on |
|---|---|---|---|
| **L0** | Gated core change-set (G1) | S0.1-S0.5 | existing baselib |
| **L1** | Foundation leaves | S1.1-S1.9 | existing baselib |
| **L2** | Independent components | S2.1-S2.9 | L1 |
| **L3** | Session engine & transport seam | S3.1-S3.6 | L0, L1, L2 |
| **L4** | I/O shell & test peer | S4.1-S4.4 | L2, L3 |
| **L5** | Client orchestration | S5.1-S5.2 | L2, L4 |
| **L6** | Session | S6.1 | L1, L2, L5 |
| **L7** | Impersonation | S7.1-S7.5 | L2, L3, L6 |
| **L8** | Facade, tooling, verification | S8.1-S8.3 | all |

**The two long arms, and where they meet.** The single biggest parallelization win is that the slow,
risky arm (L0, gated on the whole suite) and the largest build arm (L1 → L2 → S3.1 Session) are
independent and run concurrently. They rejoin at L4, where the connection task needs both the gated core
(via the stranded TLS policy, the hook, the floor check, the multi-op task) and the Session engine. The
critical path then runs up the impersonation chain:

```
  L1.leaves ─► L2.primitives ─► S3.1 Session ──────────────┐
                                                           ▼
  L0.S1-4 ─► S0.5 GATE ─► S3.3/S3.4/S3.5 (transport) ─► S4.1 establish ─► S4.2 h2 driver
                                                                              │
                                          S5.2 pool ◄─── S4.3 h1 driver ◄─────┤
                                              │                               │
                                              ▼                               ▼
                                          S6.1 session ─► S7.3 apply ─► S7.4 report ─► S7.5 vectors
```

The TLS fidelity spike (S7.1) branches off after L3 and runs alongside L4-L6, because it needs only the
TLS context factory and the ClientHello capture, not the client stack.

### 1.1 Slice index

For assigning agents. Size is relative effort (S under a day of focused work, M a few days, L a week
or more, for one agent); "earliest" is the integration wave of §12 - development may start earlier.

| Slice | Name | Size | Depends on | Earliest |
|---|---|---|---|---|
| S0.1 | Handler macro + MultiOperationTask | M | - | 0 |
| S0.2 | Pre-handshake hook | S | - | 0 |
| S0.3 | configureClientStream extraction | S | - | 0 |
| S0.4 | initNativeSslContext split | M | - | 0 |
| S0.5 | The G1 gate | M | S0.1-S0.4 | 0 |
| S1.1 | net::Uri | M | - | 0 |
| S1.2 | http::HeaderList | S | - | 0 |
| S1.3 | Error info & exceptions | S | - | 0 |
| S1.4 | Profile shape types + data models | M | - | 0 |
| S1.5 | Beast import header | S | - | 0 |
| S1.6 | AsioSslStreamWrapper additive extensions | M | - | 0 |
| S1.7 | Build integration | S | - | 0 |
| S1.8 | Test-module scaffolding | S | S1.7 | 0 |
| S1.9 | HTTP/2 Globals | S | - | 0 |
| S2.1 | FrameCodec | M | S1.3, S1.9 | 0 |
| S2.2 | HPACK | L | S1.2, S1.3, S1.9 | 0 |
| S2.3 | FlowControlWindow | S | S1.3, S1.9 | 0 |
| S2.4 | StreamStateMachine | M | S1.3, S1.9 | 0 |
| S2.5 | HTTP/1.1 codec | L | S1.2, S1.3, S1.5 | 0 |
| S2.6 | Client contracts | M | S1.1, S1.2, S1.3 | 0 |
| S2.7 | Cookie jar | M | S1.1 | 0 |
| S2.8 | Redirect policy | S | S1.1 | 0 |
| S2.9 | Content-decoder seam | S | S1.2 | 0 |
| S3.1 | HTTP/2 Session engine | L | S2.1-S2.4, S1.2, S1.3, S1.9 | 1 |
| S3.2 | Stranded plain stream policy | S | - | 1 |
| S3.3 | Stranded TLS stream policy | M | S0.3, S1.6 | 1 (after S0.5) |
| S3.4 | TLS client profiles, contexts, floor | M | S0.4, S1.4 | 1 (after S0.5) |
| S3.5 | Tunnel stage | M | S0.2 | 1 (after S0.5) |
| S3.6 | ClientHello capture + JA3/JA4 | M | S1.6 | 1 |
| S4.1 | Connection establishment + ALPN dispatch | M | S3.2-S3.5, S0.1, S2.6 | 2 |
| S4.2 | HTTP/2 driver | L | S4.1, S3.1 | 2 |
| S4.3 | HTTP/1.1 driver | M | S4.1, S2.5 | 2 |
| S4.4 | Test peer | M | S3.1 | 2 |
| S5.1 | HttpClientRequestTask | M | S2.6, S0.1 | 3 |
| S5.2 | ConnectionPool | L | S4.1-S4.3, S2.6 | 3 |
| S6.1 | ClientSession | M | S5.1, S5.2, S1.1, S1.4, S2.7-S2.9 | 4 |
| S7.1 | TLS fidelity spike | M | S3.4, S3.6 | 2 |
| S7.2 | Profile content + loader | M | S1.4, S3.4, S7.1 | 5 |
| S7.3 | Apply profile across layers | M | S3.4, S3.1, S6.1, S2.9, S7.2 | 5 |
| S7.4 | Fidelity report + fingerprint | M | S3.6, S3.1, S7.3 | 5 |
| S7.5 | Capture procedure + vectors | M | S7.2, S7.4 | 5 |
| S8.1 | Compatibility facade | S | S6.1 | 6 |
| S8.2 | bl-tool command (optional) | S | S6.1 | 6 |
| S8.3 | Final verification | M | all | 6 |

---

## 2. Layer L0 — Gated core change-set (G1)

Implements design §3.8. This is the one change to code existing users run through. It is developed as
four independent commits and passes **one** gate on the whole suite (D19, D26). No feature code of any
later layer is in the tree for that gate. **Characterize before you change:** where existing behavior can
be observed without the change, commit the test that pins it first, and show it passing on both sides.

### S0.1 — Handler macro + MultiOperationTask (§3.2, §3.8 commit 1)
- Deliverables: in `tasks/TaskBase.h`, `BL_TASKS_HANDLER_END_IMPL_EX( onSuccess, onFailure )` with
  `BL_TASKS_HANDLER_END_IMPL` forwarding to it, and `BL_TASKS_HANDLER_END_MULTIOP`; new
  `tasks/MultiOperationTask.h` (`MultiOperationTaskT` mix-in: pending-op counter, first-error capture,
  `initiateClose()`, single terminal `notifyReady`).
- Depends on: existing baselib. Earliest: t0.
- Probe (**mechanical proof**): `scripts/utests/utf_ppstream.py`, a stdlib-only script beside
  `utf_inventory.py`, which preprocesses every TU the makefiles build, on the parent commit and on this
  change, strips line markers, tokenizes, and asserts the streams are equal except for integer literals
  from expansion sites inside `TaskBase.h`, which shift by one constant (from `BL_EXCEPTION`'s
  `__LINE__`, `core/ErrorHandling.h:50`). `BL_TASKS_HANDLER_END*` expands at 58 sites in 20 files
  under `src/`.
- Acceptance & tests: the mechanical proof passes; new cases in `utf_baselib_tasks` (or a numbered
  sibling) per the table in design §3.8 commit 1 (multi-op success, one/several failures, cancel,
  `initiateClose` throws, finish-continuation entered once, restart, TSan stress, single-threaded run).
- Pitfall: `notifyReadyImpl` runs finish-continuation and `onTaskStoppedNothrow` before its
  `m_notifyCalled` guard (`TaskBase.h:547`) - the mix-in must make the terminal path idempotent.

### S0.2 — Pre-handshake hook (§3.6, §3.8 commit 2)
- Deliverables: in `tasks/TcpBaseTasks.h`, protected virtual `beginPreHandshakeStage( continueCallback )`
  on `TcpConnectionEstablisherConnector`, default invokes the callback immediately;
  `onConnectionEstablished` (`:1315`) routed through it.
- Depends on: existing baselib. Earliest: t0.
- Acceptance & tests: contract in design §3.6/§3.8 commit 2 - with the default hook the call order and
  lock scope are unchanged; characterization of today's connect→handshake order via the existing
  `continueAfterConnected` virtual is committed first; new cases cover async stage, stage failure before
  handshake, cancel during stage, one-run-per-retry-attempt (`TcpBaseTasks.h:1389`).

### S0.3 — configureClientStream extraction (§3.1, §3.8 commit 3)
- Deliverables: in `tasks/TcpSslBaseTasks.h`, extract the SNI logic of `createSocket` (`:227-251`) into
  `configureClientStream()`; `createSocket` calls it. Pure refactor.
- Depends on: existing baselib. Earliest: t0.
- Acceptance & tests: `TlsHandshake_SniOmittedForAddressLiterals`
  (`utf_baselib_http/TestTlsHandshakeVerification.h:558`) passes unchanged (already characterizes both
  halves for a name and an IPv4 literal); **add first** the same assertion for an IPv6 literal.

### S0.4 — initNativeSslContext split (§3.3, §3.8 commit 4)
- Deliverables: in `crypto/CryptoBase.h`, split `initNativeSslContext` (`:257`) into three composable
  steps (floor/options/level; cipher policy; trust), with the global client context and every server
  context calling all three exactly as today. **Refactor only** - no new factory here.
- Depends on: existing baselib. Earliest: t0. Security-critical.
- Probe (**mechanical proof**): a probe dumping, for the global client context and a server context, the
  option bits, min/max protocol version, security level, the ordered cipher and TLS 1.3 suite lists, the
  session-cache mode, verify mode/depth and trust-anchor count; identical on the parent commit and this
  change. Run on **both OpenSSL flavors** (3.5.4 and 1.1.1w under `BL_USE_OPENSSL_1X`).
- Acceptance & tests: `TestTlsProtocolPolicy.h` in `utf_baselib_http2` passes unchanged; **add first**
  the trust-anchor count and per-role session-cache-mode assertions (design §3.8 commit 4).

### S0.5 — The gate (§3.8 "The gate")
- Deliverables: the gate run and its evidence. One gate on the tip of S0.1-S0.4.
- Depends on: S0.1-S0.4. Earliest: after they exist.
- Method: the entire `utf_*` suite, baseline-relative via `scripts/utests/utf_runlog.py` (capture the
  parent commit twice for the nondeterministic-case list, capture the change, `--compare`); the two
  mechanical proofs of S0.1 and S0.4; TLS modules on both OpenSSL flavors. Matrix breadth is the
  maintainer's call. An intermittent failure is reproduced or explained against `notes/plans/issues/`,
  never re-run until green.
- Acceptance: for every pre-existing case the registered set, executed set, pass/fail and deterministic
  assertion counts are unchanged, and the new cases pass. **Nothing from L3+ that depends on G1 is
  integrated before this passes.**

**Executed 2026-09-18 - PASSED, at `gcc1520` debug only** (breadth narrowed by the author, as this
section allows). `utf_runlog --compare` exits 1 with 36 differences, all accounted for: 34 are the 17
new cases, each reported as both `REGISTRATION ADDED` and `NEWLY RUNS`, and all 17 verified `passed`;
one is `TlsHandshake_SniOmittedForAddressLiterals` 14 -> 21 assertions, which is S0.3's added
IPv6-literal pin; one is `BaseLib_Base64UrlTests` 16390 -> 16396 in an untouched module, proven a
false alarm by running the **unchanged baseline binary** eight times (16384, 16384, 16390, 16396,
16396, 16390, 16396, 16366 - the flagged value three times in eight). Verified beyond the tool's own
report: no pre-existing case changed outcome or disappeared, and no module exit code differs.

**Two limits of this gate, both to be stated wherever its result is cited:**

- **`utf_baselib_jni` is compared on two of the gate's four signals, not four.** All 11 of its cases
  are captured `passed` on both sides, so pass/fail and the registered set are real evidence. But its
  *executed* set is empty and every assertion count is 0, on both sides: the binary emits no
  `Entering test case` lines at all, which is what the tool reads for those two signals, so they are
  vacuous rather than verified. Its exit 200 is the known pre-existing `Test setup error` that follows
  a clean pass of every case; it is identical on both sides and is a host property.

  **Why the progress log is missing is not known.** With identical flags
  (`--log_level=test_suite --report_level=detailed`) `utf_baselib_utils` emits 3 `Entering` lines and
  `utf_baselib_jni` emits 0 while still reporting 12 `has passed`. Ruled out: the cases are ordinary
  `UTF_AUTO_TEST_CASE` declarations, and `UTF_TEST_APP_INIT_DEACTIVATE_THREAD_POOLS` - the only thing
  distinguishing that module's `…Main.cpp` - is not the cause, because `utf_baselib_basictask` sets
  the same macro and logs normally. Whoever needs those two signals for JNI should start there rather
  than re-deriving it.
- **The 1.1.1w half was not produced**, and cannot be on this machine - see the D2 note in the
  design's section 0.1. The "both flavors for TLS" clause of this gate is unmet, not waived.

**Re-run 2026-09-18 after the L0 follow-ups and the review round - PASSED again**, same breadth, same
baseline (the parent commit has not moved, so only the change side was rebuilt: 20 minutes rather
than 1h48m). 40 differences, all accounted for: 38 are the **19** new cases counted twice - L0's 17
plus phase 2's two - all verified `passed`; one is S0.3's IPv6 pin again; one is the same
`BaseLib_Base64UrlTests` nondeterminism, whose flagged value the unchanged baseline binary is already
proven to produce. No pre-existing case changed outcome or disappeared, and no module exit code
differs. **F-L0-2's move of three cases between modules produced no difference at all**, because the
comparison keys on case name rather than path - worth knowing before the next module split is gated.

**Third run 2026-09-18, the gate of the two post-L0 core change-sets - PASSED.** The
retry-classifier widening (`9182fd0`) and the `ThreadPoolImpl` lock (`31f9463`) share this run, as
the four L0 core commits shared G1. It was made after `10c1031`, the last commit that changes any
source - the two above it are a record and the regenerated manifest - so the code at the tip is what
it tested. Same breadth, same baseline. 48 differences, all accounted for: 46 are the **23** new
cases counted twice - L0's 17, phase 2's two and this round's four
(`Tasks_ThreadPoolSizeContractTests`, `Tasks_ThreadPoolConcurrentSizeReadTests`,
`TlsHandshakeRetryClassifier_RetryableErrorSetTests`,
`TlsHandshakeRetryClassifier_TruncationAgreementTests`); the other two are the IPv6 pin and the
`BaseLib_Base64UrlTests` nondeterminism again. **No pre-existing case changed outcome or assertion
count**, which is the signal that matters for two behavioural changes on paths the whole suite
exercises. The totals read 774 ran / 785 registered against the 795 cases of the refreshed manifest:
the 11 not run are `utf_baselib_jni`'s, as before, and the 10 not registered are `utf_baselib` cases
compiled out on this host - nine under `#if defined( _WIN32 )` and one under
`BL_DEVENV_VERSION < 6` - not a missing module.

The false alarm above exposed a weakness in the gate method itself, recorded at
`notes/plans/issues/utf-runlog-nondeterministic-sampling-record.md`: `utf_runlog.py` derives its
nondeterministic-case list from two baseline runs, which misclassifies a case whose variation is a
rare event. Any future gate should read that record before trusting an assertion-count difference.

### L0 follow-ups - after the gate, before S4.1

Three items fell out of executing L0. None was gated (the first two touch a header with no consumer
and a test module; the third runs an existing case under a sanitizer). **All three are done, executed
2026-09-18 in three parallel worktrees plus one follow-on phase, and merged into `lazari2`.**

- **F-L0-1 (done, `da9a444`): `MultiOperationTaskT` is parameterized on its base.** It had been built
  as `template< typename E = void > : public TaskBase`, which cannot be combined with
  `TcpConnectionEstablisherConnector` - see design §3.2, which now states the required shape. It is now
  `template< typename BASE = TaskBase > : public BASE`, with `BL_VARIADIC_CTOR` forwarding to the
  base as `ProcessingUnit.h` does, and the accounting members initialized in class - the macro owns
  the constructor definition, so there is no init list to write (design §3.2).
  `typedef MultiOperationTaskT<> MultiOperationTask` is kept, so the existing tests compiled with no
  edit at all, and the public `isClosing()` that design §5.1's read loop needs is added. **S4.1 is
  unblocked.**
- **F-L0-2 (done, `e2a0830`): `TestMultiOperationTask.h` moved to `utf_baselib_tasks2`.** It had been
  added to `utf_baselib_tasks`, which is 67.7 MB on win-x86 debug - 90% of the enforced 75 MB ceiling
  and far past the 40 MB target - which `src/utests/AGENTS.md` forbids adding to, while S0.2 created
  `utf_baselib_tasks2` for exactly that reason in the same layer. A pure rename (git records `R100`,
  identical blob), one include line each side. No case lost: 115 before and after, three changing
  module, each keeping its exact assertion count.
- **F-L0-3 (done, `0760ad4`): the TSan stress case is run, and the accounting is clean.** **Zero data
  races** on a four-thread pool, so nothing touches the pending count, the closing and terminal flags
  or the first-error capture. Design §3.8 commit 1's stress row is satisfied. Two findings came with
  it, both recorded and neither fixed: a pre-existing production race in `core/ThreadPoolImpl.h`,
  where `createThreads` mutates `m_threads` under `m_lock` while `size()` and `resize()` read it with
  none (`tsan-baseline-and-threadpool-resize-race-record.md`), and a test-probe teardown defect where
  the probe's timers outlive the asio service they were built against
  (`multioperation-probe-timer-teardown-record.md`). **The number to watch on any re-run is the
  data-race count staying at 0**; the other reports are the probe's teardown, not the library's.

**Phase 2 (done, `51aa812`, `8501770`): the composition is pinned in the tree.** The two tests that
needed both F-L0-1 and F-L0-2 could not be written while those lanes ran in parallel, so they landed
after. `Tasks_MultiOperationTaskOverConnectionEstablisherTests` derives from
`MultiOperationTaskT< TcpConnectionEstablisherConnector< TcpSocketAsyncBase > >`, so a regression to
`public TaskBase` is now a **compile error rather than a silent pass**, with a `static_cast< TaskBase* >`
pinning that there is exactly one `TaskBase` subobject and the recorded event order proving the
terminal `notifyReady` runs through the establisher chain. Not having that check is what let the wrong
shape through the first time. `utf_baselib_tasks2` ends at 25.9 MB on `ub24-a64-clang2010-debug`,
where nothing is enforced; the 40 MB target and 75 MB ceiling are calibrated for `win-x86-*-debug`
(`src/utests/AGENTS.md`). Nothing has been built for win-x86 here, so the figure there is
**extrapolated, not measured** - scaling by the only ratio available, `utf_baselib_tasks` at 67.7
against 56.8 MB, puts it near 31 MB, roughly 77% of target. Comfortable either way, but treat 31 MB
as an estimate for a different module until someone builds it.

**Decisions still owed after L0**, kept here because each is a prerequisite for something later and
none belongs to a single slice:

1. ~~**The 1.1.1w flavor and D2**~~ - **closed 2026-09-18 as deferred**,
   `notes/plans/issues/openssl-1x-flavor-deferral.md`. It does not gate L1. Every slice that touches
   OpenSSL adds to the owed evidence rather than creating a new question; say so in the slice's
   acceptance instead of claiming both flavors.
2. ~~**Whether to widen the handshake retry classifier**~~ - **closed 2026-09-18: widened**, as its
   own gated change-set (`tls-handshake-retry-unreachable-record.md`, now CLOSED). It asks the STREAM
   policy's `isStreamTruncationError` rather than comparing codes, so the retry follows whatever the
   policy in use calls a truncation. The evidence is a deletion: S0.2's test-only stream policy, which
   existed only because production did not recognize the real error, is gone and its cases pass
   unchanged. **Note for §5.1:** `MAX_RETRY_COUNT` is 5, so a peer which *consistently* truncates now
   costs six full resolve/connect/handshake attempts where it cost one. That is the regime `eof`
   already had and what a pooled connection wants, but nothing in the suite times it.
3. ~~**The `ThreadPoolImpl` race**~~ - **closed 2026-09-18: fixed**, as its own gated change-set
   (`tsan-baseline-and-threadpool-resize-race-record.md`, §3 now FIXED). `size()` takes
   `BL_MUTEX_GUARD( m_lock )` and `resize()` reads through it, following the five `NOEXCEPT` getters
   of `ExecutionQueueImpl`; the claim that `NOEXCEPT` made this hard was wrong. Data-race reports on
   `utf_baselib_basictask` went 3 to 1 with `ThreadPoolImpl` named in none. **Still open in that
   record's §4:** an unrelated two-line race on a dead `bool` in `TestBaselibBasicTask.h:127`, left
   out deliberately rather than folded into a gated core change. `aioService()` is racy in the same
   family and a lock cannot fix it - it returns a reference - so that is a separate design question
   about the disposable lock.
4. ~~**Refreshing the frozen `notes/reviews/major/update_2026/baseline/inventory.json`**~~ -
   **done 2026-09-18.** It was not rotten, merely out of date by exactly this work: at the pre-L0
   commit `1bcde00` it compared clean, and every violation since was L0's own legitimate change - 19
   `C1 case ADDED`, one `C2 case BODY CHANGED` (S0.3's deliberate IPv6 pin) and one `C6 helper member
   LOST`. Re-captured at 795 cases, 124 helper blocks, 30 modules; `check_split.sh --tier1` now passes
   end to end, which is what §13's tier-1 check depends on.

   **Keep the C6 behaviour in mind rather than re-deriving it.** `utf_inventory.py` cuts a helper
   block into members at bracket depth zero, so a nested namespace is one member, and the no-loss half
   of C6 keys each member on its content hash - an *edited* helper namespace therefore reports as
   LOST. Expect that from every slice that touches one, and do not "fix" it: the tool exists to prove
   a split moved helpers unchanged, so content-hash keying is the invariant both halves of C6 stand
   on, including the duplicate half that catches real ODR violations.

   **The other baseline artifacts were not refreshed**, and are a separate question: `runlog.json`
   holds 17 modules and 741 cases against 30 and 795 today, and was built for the split work's
   family-scoped comparisons rather than as a whole-tree gate.

**All four are now closed, so nothing on this list gates L1.**

A fifth item was a decision and has been **settled**: the context-dump probe of S0.4 lives in the
repository at `scripts/utests/tls_context_dump.{cpp,sh}`, beside `utf_ppstream.py`, with **no makefile
target** - the two mechanical proofs of L0 are now treated alike, and S3.4 and the 1.1.1w debt both
inherit a working probe rather than a description to reimplement. Its build flags are a capture from
`make -n`, not a derivation, so they go stale when the devenv moves; the dist root, the tool versions
and the architecture tag are environment overrides, the target triplet and the clang resource
directory are not, and the header says to recapture rather than patch one flag at a time.

---

## 3. Layer L1 — Foundation leaves

All depend only on existing baselib and are mutually parallel. Earliest start t0 for all.

**Executed 2026-09-18/19 - all nine slices implemented, merged and focus-validated.** Three lanes,
two waves: S1.7 then S1.8 first because they gate the four new test modules that S1.2, S1.3, S1.4 and
S1.9 put their tests in, with S1.1 and S1.6 running alongside them and the rest following. Per the
maintainer's instruction this layer ran **focused testing only** - each slice built and ran the
modules its acceptance names, clang debug - with **no release pass and no whole-suite gate**. The
tree ends at 825 cases across 34 modules, tier 1 clean.

Four things the layer settled that later slices should not rediscover:

- **The Beast probe of S1.5 answered PRESENT** - complete and byte-identical across all four dist
  variants - so nothing needs vendoring and **S2.5 chooses its backend on §5.5's other criteria**
  (`notes/plans/issues/beast-availability-probe-record.md`, which also records that Beast's `put()`
  is not eager by default and that a response-only parser must still override `on_request_impl`).
- **Pseudo-headers are not representable in `http::HeaderList`**, and this is a constraint on S2.2
  rather than a question for it. A colon is not a token character, so `":method"` is rejected by
  design - correct for the *request* side, where the session derives the pseudo-headers and their
  order comes from the profile (§6.4). But the **decoder's output is a different type**: design §4.5
  validates "pseudo-headers first and only the defined ones" and "exactly one three-digit `:status`"
  on a decoded block, so what S2.2 produces must carry pseudo-headers **in wire order**, and
  `HeaderList` as delivered cannot be it. The shape is S2.2's to choose with the codec in front of
  it - a separate decoded-field sequence, or a `HeaderList` mode which admits them - but that it
  cannot simply reuse `HeaderList` is settled here. §1.1's dependency row `S2.2 → S1.2` should be
  read as "consumes the encoder side", not "reuses the type for output".
- **The decoded header-list limit is a parameter, not a constant.** It is what the active profile
  advertises, and RFC 9113 gives the setting no numeric initial value, so S1.9 deliberately defines
  none and the decoder takes it as an argument. `SETTINGS_MAX_CONCURRENT_STREAMS` is the same shape.
- **`MAX_FLOW_CONTROL_WINDOW_SIZE` and `INITIAL_WINDOW_SIZE_DEFAULT` are signed**, because a window
  may legally go negative and an unsigned maximum would make every comparison in S2.3 a
  `-Wsign-compare` failure under `-Werror`.

**Object sizes are toolchain-dependent by about a factor of two, and the numbers a lane reports
are the small ones.** Every size in this layer was measured on `clang2010 debug`; the same modules
under `gcc1520 release` are roughly twice as large - `utf_baselib_http2` 28.6 -> 57.4 MB,
`utf_baselib_h2profiles` 22.3 -> 42.2, `utf_baselib_http` 44.2 -> **101.3**. Nothing fails, because
the 40 MB target and 75 MB ceiling are calibrated on *debug* objects and the gate is off on Linux
(`src/utests/object-size-limits.json`, which already records `win-x86-vc143-release` at 96.77 MB
building fine against a 75 MB debug ceiling). But a slice reading "20 MB, plenty of headroom" off a
clang debug build is reading the generous number. **S2.5 in particular should know this**, since its
Beast-versus-in-house decision turns on an object-size delta.

**Gated 2026-09-18, and it passed.** The whole-suite baseline-relative comparison was run once after
the layer closed, which the round rules had excluded. 112 differences, all accounted for: 110 are the
55 new cases counted twice - L0's 23 plus this layer's 32, every one verified `passed` - and the
other two are S0.3's IPv6 pin and the `BaseLib_Base64UrlTests` nondeterminism, which reported yet
another of the values its unchanged baseline binary is proven to produce. **No pre-existing case
changed outcome, disappeared or moved its assertion count, and no module exit code differs** - which
is the evidence focused testing could not give for `ErrorHandling.h`, reaching every translation
unit, and `AsioSslStreamWrapper.h`, reaching nine modules.

**Two gaps this layer's round rules left, closed immediately after it.** Focused testing meant
`utf_baselib_http` - the module with the real TLS client and server tasks over the stream wrapper
S1.6 extended - was never rebuilt against it, and **nothing in L1 was built with gcc at all**. Both
are low risk because every production change is additive, but `BeastBoostImports.h` and the wrapper's
message callback are exactly the kind of code where gcc's warning set differs from clang's and
everything is `-Werror`. A targeted gcc pass over the affected modules is the cheap way to retire
that before L2 builds on them, and is not the "full release verification" the round rule excluded.

**Both came back negative**: six modules under gcc release with `-Werror`, zero warnings, 133 of 133
cases, and `utf_baselib_http` passing against the extended wrapper under both toolchains
(`notes/plans/issues/l1-gcc-toolchain-coverage-record.md`). Two details there are worth carrying into
L2 rather than rediscovering. `BeastBoostImports.h` is included by nothing yet, so **no module build
compiles it in either toolchain** - it needs its own probe, and since `basic_parser` is abstract that
probe has to *derive* from it to instantiate anything. And a toolchain pass taken while other lanes
are still merging measures the tree it started from: that one did, its `utf_baselib2` row was a
commit behind, and it was re-run rather than annotated.

**`utf_baselib_h2client` carries one placeholder case** so that `make testutf` and any whole-suite
run are not red for two whole layers. That deviates from S1.8's work order, which said empty modules,
and the case says so in its own text: **S4.1 replaces it** with the first real one. A suite that is
permanently red teaches people to ignore red, which costs more than the placeholder does.

### S1.1 — net::Uri (§3.4, D16, D24)
- Deliver: `core/Uri.h` (`bl::net::UriT`): parse; RFC 3986 §5 reference resolution; normalization;
  `origin()`, `authority()`, `pathAndQuery()`. Strict (control chars, whitespace, backslash are errors);
  no IDNA (non-ASCII host is an error). Complements `str::uriEncode`/`uriDecode`.
- Accept: RFC 3986 cases incl. IPv6 literals, dot-segment removal, resolution; strictness rejections.
  Tests → beside `TestNetUtils.h` in `utf_baselib2` (headroom permitting), else a new module.
- **Residual found by the L2 second pass (2026-09-19), CLOSED the same day (`cd01896`).** `Uri.h:735`
  (the IP literal) and `:850` (the scheme) folded through `str::to_lower_copy`, which takes
  `std::locale()`, while the reg-name host already went through the header's own ASCII fold. Both
  now call `toLowerAsciiCopy`, added beside `toLowerAscii` under the same name and signature the
  five sibling classes use. It was deferred at first as a core-path change needing its own gated
  change-set; that was wrong - `Uri.h` was added by this work in `b2c0d3e`, is named by neither
  `core/PreCompiled.h` nor `core/BaseIncludes.h`, and every includer is an `httpclient/` header or a
  test from this feature, so it was the same free fix the siblings got. `Uri_LocaleIndependenceTests`
  pins it with a negative control that fails exactly the four of six references touching a fold site
  when the change is reverted. 48 -> 49 cases in `utf_baselib2`, 27.0 MB, clean under clang and gcc
  release.

### S1.2 — http::HeaderList (§3.5)
- Deliver: `http/HeaderList.h` (`bl::http::HeaderListT`): ordered name/value pairs, original case,
  case-insensitive lookup, multi-value, conversion to/from `http::HeadersMap`; validation helpers
  (reject CR/LF/NUL in values, non-token names).
- Accept: order and case preserved; repeated names retained; round-trip with `HeadersMap`. Tests →
  `utf_baselib_httpclient/TestHeaderList.h`.

### S1.3 — Error info & exceptions (§3.7)
- Deliver: additive to `core/ErrorHandling.h` - the `errinfo_http2_*`, `errinfo_http_alpn_selected`,
  `errinfo_tls_negotiated_*` typedefs and `BL_DECLARE_EXCEPTION( Http2ProtocolException )` /
  `Http2StreamException` next to the existing `errinfo_http_*` (`:473`).
- Accept: compiles; each errinfo attaches/reads. No existing code path changes. Tested indirectly by
  consumers; a smoke test in `h2core`.

### S1.4 — Profile shape types (§6.2)
- Deliver: the pure-data config types - `crypto/TlsClientProfile.h` (`TlsClientProfile`), the
  `Http2Profile` struct in `http2/`, the `HeaderProfile` struct + request-kind enum in `httpclient/`,
  and `data/models/HttpClientProfiles.h` (`bl::dm::httpclient`, the `BL_DM_*` models incl.
  `BrowserProfile`). **Types and models only; no content, no loader** (those are S7.2).
- Accept: types compile and the DM round-trips through JSON. Tests → `h2profiles`.

### S1.5 — Beast import header (§5.5, D15)
- Deliver: `core/detail/BeastBoostImports.h` in the form of `TimeBoostImports.h` - the only file that
  includes `<boost/beast/...>` (narrow headers, guarded by `BoostIncludeGuardPush/Pop.h`), bringing the
  exact names the codec will use into `bl::beast`/`bl::beast::http` by individual `using`. Not reachable
  from `BaseIncludes.h` or any `PreCompiled.h`.
- Probe: confirm `<boost/beast/http/basic_parser.hpp>` is present in the dist include tree. Beast is
  header-only, so if absent it can be vendored; record the finding for S2.5's decision.
- Accept: a translation unit including only this header compiles on all four toolchains.

### S1.6 — AsioSslStreamWrapper additive extensions (§3.1, §3.3, §3.7)
- Deliver: additive to `tasks/AsioSslStreamWrapper.h` - a strand-taking constructor overload; ALPN offer
  (`SSL_set_alpn_protos`) and selected-protocol getter (`SSL_get0_alpn_selected`); negotiated version and
  cipher getters; a ClientHello capture hook (`SSL_set_msg_callback`); a client-context constructor
  distinct from the server-flag one. All overloads/additions; the existing constructor and its
  "non-null context ⇒ server" rule (`:341`) are untouched.
- Depends on: existing wrapper only (leaf). Earliest: t0.
- Pitfall: `SSL_set_alpn_protos` returns **0 on success** - do not route through `BL_CHK_CRYPTO_API_NM`.
- Accept: extends `TestAsioSslStreamWrapper.h` in `utf_baselib_http2` - new getters default sanely
  pre-handshake; ALPN offer sets without error; existing wrapper cases pass unchanged.

### S1.7 — Build integration (§9, D1)
- Deliver: the `devenv7_only` marker mechanism and one `filter-out` block in `projects/make/common.mk`
  using the negative filter (mirroring `jni_enabled` at `:290`). The **only** makefile change.
- Header guards: new headers do **not** test `BL_DEVENV_VERSION` - only the project makefiles define
  it, and the devenv7 release notes record that no public header may require it
  (`notes/plans/issues/devenv7-breaking-changes-release-notes.md:352`). They guard on the capability
  they need, with a clear `#error`: `BOOST_VERSION >= 107000` for executor-bound I/O objects (the stranded
  policies), `OPENSSL_VERSION_NUMBER >= 0x30500000L` for impersonation (D2). The design's §9 says the
  same after this review.
- Accept: a module directory carrying the marker builds on devenv7 and is filtered out on devenv2-6; a
  new header included on too old a Boost fails with the `#error`, not a cryptic template error.

### S1.8 — Test-module scaffolding (§8.1, D20)
- Deliver: the four module directories - `utf_baselib_h2core`, `utf_baselib_h2client`,
  `utf_baselib_httpclient`, `utf_baselib_h2profiles` - each with a `…Main.cpp` skeleton (defines
  `UTF_TEST_MODULE`, includes `<utests/baselib/UtfMain.h>`), a `devenv7_only` marker, an empty
  `notes.txt`. Document the append convention (§0) at the top of each `Main.cpp`. Also
  `http2/PreCompiled.h` and `httpclient/PreCompiled.h` in the form of `http/PreCompiled.h` (no OpenSSL
  header in either), included by these four modules only.
- **Do not add the new `PreCompiled.h` files to `src/utests/include/utests/baselib/UtfBaseLibCommon.h`.**
  That header is included by every test module on every devenv (`:23-29`); adding devenv7-only headers
  to it would break every module on devenv2-6 and pull the new code into every existing module's
  translation unit, which the size policy forbids.
- Depends on: S1.7 (marker). Accept: each empty module builds and runs zero cases on devenv7; no
  existing module's preprocessed output changes.

### S1.9 — HTTP/2 Globals (§4.1, §4.6, design §2.2 `http2/Globals.h`)
- Deliver: `http2/Globals.h` - the constants every HTTP/2 slice shares and none may redefine: frame types,
  frame flags, settings ids, error codes (with a `toString`), the connection preface bytes, protocol
  constants (`2^31-1`, `16384`, `2^24-1`, default window and table sizes) and the default values of the
  §4.6 limits table, in the `GlobalsT< E >`/`BL_DEFINE_STATIC_*` idiom of `http/Globals.h`.
- Why a slice of its own: S2.1-S2.4 and S3.1 are developed in parallel; without a single owner each
  would invent its own enums and the merge would be a conflict and an inconsistency. This is small and
  must land before the L2 primitives start.
- Depends on: existing baselib. Earliest: t0. Accept: compiles; every constant has the RFC 9113 value;
  a smoke test in `h2core`.

---

## 4. Layer L2 — Independent components

All depend only on L1 and are mutually parallel. The four HTTP/2 primitives (S2.1-S2.4) are deliberately
**not** templated on any stream and know nothing of Asio, OpenSSL, tasks or locks (design §2.1).

**Executed 2026-09-18/19 - all nine slices implemented, merged and focus-validated** (S2.1, S2.3 and
S2.4 in `d70d577`; S2.6 through S2.9 in `598b890`; S2.2 in `8040ec8`; S2.5 with the D15 verdict in
`a7a36a5`). **Reviewed 2026-09-19 against the design and these work orders**, read-only -
`notes/plans/issues/http2-l2-review-record.md` carries the verdict per slice, the findings ranked, and
what the review did not check. Every slice does what its work order says; the RFC values were checked
mechanically where they could be (all 257 rows of the Huffman table and every Appendix C vector
against the RFC text) and by transcription elsewhere (the section 5.1 transition tables, the static
table, the frame rules). **Three defects, fixed in the review's follow-up rather than carried:** the
S2.6 contract could not convey the response status (recorded under S2.6, with the fix); S2.5's
`Transfer-Encoding` gate and profile case-map lookup folded through `std::locale()`, which is the
hazard S1.2 documents and avoids, and one of the two gates a smuggling defence; and S2.7 rejected a
`Domain` identical to a single-label request host where RFC 6265 5.3 step 5 stores it host-only,
so a test server on `localhost` lost its cookies. The rest are notes carried into the work orders
they affect (S3.1, S4.3, S5.1, S5.2, S6.1). Nothing found blocks L3.

**Second pass, 2026-09-19, over the fix round `cfa9159..6b4c6d1`: clean.** Each fix does what was
agreed and nothing it introduced is a defect - the status parameter and the enforcing stub, the
ASCII-only gate with the hazard executed under a hostile `ctype` facet rather than argued, the merged
cookie predicate (checked at the boundary: it admits exactly the attribute-equals-host case and
nothing either original rule refused), and the `NegotiatedProtocol` replacement of `protocol()`,
which was the right call. What the pass left are notes, carried into S2.6, S4.1, S4.3 and S5.1, and
one L1 residual of the locale class recorded under S1.1. The record has the detail. **L3 may start.**

**Both notes that were incompletenesses of the fix round were then closed before L3** (`059658b`,
`cd01896`), while the files still have no consumers and the change was therefore free:
`ClientResponse` now holds one `NegotiatedProtocol` with both setters removed, and `Uri` folds the
scheme and the IP literal without the locale. 43 -> 44 cases in `utf_baselib_httpclient` and 48 -> 49
in `utf_baselib2`, both clean under clang release and gcc release. The two notes that belong to
later slices - the driver's `const` member (S4.1) and interim responses from the parser's list
(S4.3) - stay open there.

### S2.1 — HTTP/2 FrameCodec (§4.1)
- Deliver: `http2/FrameCodec.h` - incremental 9-byte header parse across read boundaries; all ten frame
  types parsed/validated with per-type length, stream-id and padding rules; serialization of what we
  send (incl. optional `PRIORITY` fields, padding).
- Dep: S1.3, S1.9. Accept: byte-level parse/serialize vectors; oversize-frame and bad-padding rejections.
  Tests → `h2core/TestFrameCodec.h`.

### S2.2 — HTTP/2 HPACK (§4.2)
- Deliver: `http2/HpackDecoder.h`, `HpackEncoder.h`, `HpackHuffman.h`, `HpackDynamicTable.h` - static +
  dynamic tables with RFC size accounting; integer/string codecs with overflow limits; Huffman with the
  **decode state machine generated from the code table at static-init** (not a second literal table),
  padding validated; decoder enforces decoded-size bound *during* decoding and keeps the table in sync
  on overflow; encoder policy (indexed / incremental / literal / never-indexed, Huffman-if-shorter,
  cookie crumbling switch).
- Dep: S1.2, S1.3, S1.9. Accept: **RFC 7541 Appendix C vectors** pass; `COMPRESSION_ERROR` and mid-decode
  bound cases; a libFuzzer harness (optional). Tests → `h2core/TestHpack.h`.

### S2.3 — HTTP/2 FlowControlWindow (§4.4)
- Deliver: `http2/FlowControlWindow.h` - signed 32-bit windows both levels/directions;
  `INITIAL_WINDOW_SIZE` re-adjustment (may go negative); zero-increment and overflow errors; consumer-
  driven `WINDOW_UPDATE` at the half-window threshold.
- Dep: S1.3, S1.9. Accept: adjustment, negative-window, overflow, threshold cases. Tests →
  `h2core/TestFlowControl.h`.

### S2.4 — HTTP/2 StreamStateMachine (§4.3)
- Deliver: `http2/StreamStateMachine.h` - RFC 9113 §5.1 machine parameterized by role; reserved states
  present but unreachable (D11); recently-closed set (bounded time); **DATA on a closed stream still
  credits the connection window**; id exhaustion drains the connection.
- Dep: S1.3, S1.9. Accept: legal/illegal transition tables per role; closed-stream DATA accounting. Tests
  → `h2core/TestStreamStates.h`.

### S2.5 — HTTP/1.1 codec (§5.5, D15)
- Deliver: `httpclient/Http1Codec.h` (facade in library terms, no Beast type in any signature) +
  `httpclient/detail/Http1CodecBeastImpl.h` (backend deriving from Beast's `http::basic_parser`, sans-
  I/O) + an in-house request serializer honoring profile header order/case. Response defenses: conflicting
  `Content-Length` = error; `Transfer-Encoding` + `Content-Length` per RFC 9112 §6.3; reject obsolete
  folding; 64 KB header cap.
- Dep: S1.2, S1.3, S1.5. **Probe/decision (D15):** confirm `basic_parser` (sans-I/O) covers chunked +
  trailers, read-until-close, bodiless (1xx/204/304/HEAD), limits; and measure the object-size delta of a
  test module with and without Beast against the 40 MB target (`make utests-sizes`). Keep Beast if all
  four D15 criteria hold; otherwise write the backend in-house behind the same facade - **nothing above
  the facade changes either way.** Record the verdict (for S8.3 and the design's §5.5).
- Accept: status line, headers, chunked+trailers, read-until-close, bodiless, smuggling defenses. Tests →
  `httpclient/TestHttp1Codec.h`.

### S2.6 — Client contracts (§5.2, §5.3, §5.4)
- Why this slice exists: S4.1-S4.3, S5.1 and S5.2 are built in parallel against these interfaces. If
  they are vague, those five slices drift apart and meet in an integration failure. So this slice
  publishes the contracts **first** and they are then frozen; a change to them is a change to every
  consumer and is negotiated, not made unilaterally.
- Deliver, all decoupled from `Session` (plain parameters, never Session event types):
  - `httpclient/ClientTypes.h` - `ClientRequest` (method, `net::Uri`, `HeaderList`, body or
    `BodySource`, request kind, per-request timeouts/priority) and `ClientResponse` (status, `HeaderList`,
    body or `BodySink`, trailers, HTTP version, negotiated ALPN, the impersonation report handle);
    `BodySource` (pull: `read( DataBlock& ) -> size/eof`, `canRewind()`, `rewind()`) and `BodySink`
    (push: `onData( DataBlock ) -> consumed`, `onComplete`), per design §5.3.
  - `httpclient/ClientConnection.h` - the role interface both drivers implement: `submit( request,
    eventSink ) -> streamHandle`, `cancel( handle, errorCode )`, `consumed( handle, bytes )`,
    `provideBody( handle, DataBlock, endStream )`, and the queries the pool needs (`freeStreamSlots()`,
    `state()`: Connecting/Ready/Draining/Closed, `protocol()`). Every call is asynchronous - it posts
    to the connection's strand and returns (design §5.2 L3); none may block or call back synchronously.
  - The **stream event sink** the request task implements and the driver feeds via the mailbox:
    `onHeaders( HeaderList, isInterim )`, `onData( DataBlock )`, `onTrailers( HeaderList )`,
    `onClosed( errorCode, isRetryable )`. Delivered in order, never under the connection's lock.
  - The **pool interface** the request task consumes: `acquire( key, request, onReady )` where `onReady`
    is posted with a `ClientConnection` or an error; `release( connection, handle, outcome )`; the
    replayability rule (design §5.4) is a query on `ClientRequest`, not on the pool.
  - The **driver factory** S4.1 uses after ALPN: `createDriver( negotiated, connectedStream, ... )` -
    the `NegotiatedProtocol`, not the bare enum; see the second defect note below.
- Dep: S1.1, S1.2, S1.3. Earliest: t0; **integrate before any of S4.x/S5.x starts.**
- Accept: interfaces compile against a stub implementation; value objects round-trip; a stub
  `ClientConnection` + sink pair exists in `httpclient` tests for S5.1 to develop against.
- **DEFECT, FIXED AFTER THE L2 REVIEW (2026-09-19): the contract as first published had no path for
  the response status code.** `ClientStreamEventSink::onHeaders( handle, http::HeaderList&&,
  isInterim )` was the only header event, and `http::HeaderList` rejects `:status` by design - a
  colon is not a token character (S1.2) - so no driver could put the status in the list and the
  request task had nothing to fill `ClientResponse::status()` from; the stub sink in
  `TestClientContracts.h` recorded a header *count*. The line above,
  "`onHeaders( HeaderList, isInterim )`", is where it was lost, and design §5.3 did not say how the
  status crosses (it does now). This was a defect and not a decision: the contract could not work
  as written, and the option space was already closed - a `HeaderList` mode admitting pseudo-headers
  was rejected in S2.2 for reasons that still hold, and a separate `onStatus` event would add a
  fourth ordering guarantee to a sink whose comment exists to make the order a guarantee rather than
  a hope. **The fix: `onHeaders` gains a `status` parameter** - an `unsigned`, the three-digit
  `:status` for h2 and the status-line code for h1 - with the stub and its case updated in the same
  change. S4.2 and S4.3 deliver it; S5.1 consumes it. Nothing in L3 touches it.
- **As landed:** `onHeaders( handle, status, HeaderList&&, isInterim )`. **Every header block carries
  its own status**, interim ones included, so 103 Early Hints arrives as `( 103, hints, true )` and
  the response after it as `( 200, headers, false )`; the consumer takes the FINAL block's status as
  the response's and an interim one never overwrites it. `isInterim` **stays** although the status
  makes it derivable (true exactly when the status is in `[100, 199]` and is not 101): it states the
  structural fact the sink's ordering guarantee is written in terms of, and a consumer must not have
  to re-derive that from a number. A driver states both and the two must agree - the stub connection
  refuses a delivery in which they contradict, which is what pins 101 as a *final* response.
- **SECOND DEFECT OF THE SAME CLASS, FIXED IN THE SAME FOLLOW-UP (2026-09-19):
  `ClientResponse::negotiatedAlpn()` was unfillable too.** The field promises the identifier the peer
  selected *verbatim*, and `ClientConnection.h` named ALPN nowhere: `protocol()` returned the
  `HttpProtocol` enum and `createDriver( protocol, ... )` dropped the identifier at the factory
  boundary, where it cannot be recovered because a creator is registered once per session and cannot
  capture a per-connection value. Deriving `"h2"`/`"http/1.1"` from the protocol and the URL scheme
  gets the common cases right and is wrong for **exactly** the case the field exists to distinguish -
  a TLS connection whose peer selected nothing, which must read empty and would derive as
  `"http/1.1"`. Reported by lane 1 with the status fix and widened into it by the coordinator, on the
  same argument: this is the one moment before the implementers exist.
  **The fix: a `NegotiatedProtocol` value in `ClientTypes.h`**, carrying the protocol and the
  identifier together, with `protocol()` on `ClientConnection` **replaced by**
  `negotiated()` returning it and `createDriver` taking it in place of the enum (it still dispatches
  on `negotiated.protocol()`). Two queries were not added beside each other on purpose:
  `fromAlpn( ... )` is the only door which sets a non-empty identifier and it derives the protocol
  *from* it, so the two **cannot disagree** and no authority has to be nominated between them.
  `withoutAlpn( protocol )` is the other door - cleartext, or a TLS peer which selected nothing -
  and leaves the identifier empty; a default value is `Unknown` with none. `protocolOfAlpn( ... )` is
  the single place the mapping lives, and refuses `h2c`, `h3` and anything else this build does not
  speak. S4.1 constructs the value and both drivers report it unchanged.
- **Second pass (L2 review): the "cannot disagree" property stopped at the connection - CLOSED**
  (`059658b`). `ClientResponse` held `protocol()` and `negotiatedAlpn()` as two independently
  settable fields, so a response holding `Http2` beside `"http/1.1"` was representable there although
  no connection could produce it. It now holds one `NegotiatedProtocol` member and **both setters are
  gone**; the one door is `negotiated( ... )`, taking the pair whole. `protocol()` and
  `negotiatedAlpn()` survive as read-only forwarders, so no reader changed shape. **S5.1 therefore
  cannot set the two separately** - it fills the response with the value its connection already
  publishes, unchanged.

### S2.7 — Cookie jar (§5.6)
- Deliver: `httpclient/CookieJar.h` - RFC 6265 domain/path matching, `Secure`/`HttpOnly`/expiry/`Max-Age`,
  host-only cookies, per-domain and total caps; no public-suffix list (reject a `Domain` with no dot or a
  bare TLD; document the multi-label residual risk). Thread safe.
- Dep: S1.1. Accept: match/store/expiry/rejection cases. Tests → `httpclient/TestCookieJar.h`.

### S2.8 — Redirect policy (§5.6)
- Deliver: `httpclient/RedirectPolicy.h` - hop limit; 303 and (POST) 301/302 → GET without body; 307/308
  preserve method+body (require replayable body); drop `Authorization`+cookies cross-origin; refuse
  https→http downgrade unless allowed. Targets resolve via `net::Uri`. Off by default.
- Dep: S1.1. Accept: method rewrite, credential-drop, downgrade-refusal cases. Tests →
  `httpclient/TestRedirectPolicy.h`.

### S2.9 — Content-decoder seam (§5.6, D9)
- Deliver: `httpclient/ContentDecoder.h` - the streaming transform interface keyed by content-coding, a
  per-session registry, and the two bomb caps (absolute output, expansion ratio). **No decompressor
  ships.**
- Dep: S1.2. Accept: a test-only identity/echo decoder registers and streams; caps trip. Tests →
  `httpclient/TestContentDecoder.h`.

---

## 5. Layer L3 — Session engine & transport seam

Depends on L0 (gated), L1, L2. Slices are mutually parallel except as noted.

**Execution, 2026-09-19.** Three lanes from `65bd2a8`, split by **test module rather than by subject**
- two lanes appending to one `…Main.cpp` collide on every merge, which cost three manual resolutions
in L2. lane1 = S3.1 (`h2core`), lane2 = S3.2 then S3.3 (`h2client`), lane3 = S3.4 then S3.6
(`h2profiles`) and then S3.5 (`httpclient`, the only slice touching it).

**S3.4 and S3.6 landed first** (`f570d5e`, `f0bcbeb`, merged at `69a8f6b`). `utf_baselib_h2profiles`
5 → 13 cases, zero warnings under `-Werror -Wall -Wpedantic -Wextra`, object 22.3 → 24.9 MB clang
debug. Four things from that round are carried here rather than left in the lane journal:

- **Design §10 beats §2.2 on where the profile context lives.** §10 puts
  `createAsioSslClientContext` in `crypto/CryptoBase.h` while §2.2 puts "per-profile contexts, floor
  check" in `TlsClientProfile.h`. They conflict, and §10 wins because that second header's own
  comment makes *carrying no OpenSSL header* a property of the file, while the floor check takes an
  `SSL*`. All 407 lines are additive and nothing existing was touched, which is what D26 requires of
  a post-G1 slice.
- **The cipher allowlist had to depart from §3.3's literal wording, and the design is amended.** The
  original said "no `@`, `!`, `+`, `-`, `:` inside a name", which rejects every TLS 1.2 suite name
  OpenSSL knows and would leave every profile's TLS 1.2 list empty. The rule is now positive:
  non-empty, `[A-Za-z0-9_-]` throughout, first character alphanumeric. Strictly stronger than the
  original intent — it also refuses `,` and space.
- **`utf_baselib_h2profiles` is the module to watch for size.** 24.9 MB clang debug is comfortable
  against the 40 MB target, but the same object under gcc release **measures 49.9 MB** - not an
  estimate from the 2x rule of thumb, which predicted about 47; it was already past 40 there before
  this round. Nothing fails, because the target is calibrated on debug objects and the size gate is
  off on Linux, so it was not split. But it is the closest module in this feature to the 75 MB hard
  ceiling, and **S7.3 adds to this same module** - check `make utests-sizes` and the gcc release
  object before assuming room, and split to `utf_baselib_h2profiles2` rather than raising anything.
- **The context builder deliberately shapes a context by its cipher lists alone.** The group list and
  key-share marks, the signature algorithms, and the `status_request`/SCT/padding switches exist in
  `TlsClientProfile` but are not applied yet; that is **S7.3**, after the §6.3 spike, and the header
  says so.

**The debug-to-release ratio is not 2x, and should not be used as one.** Three modules measured on
the same day, same host, same toolchains: `h2profiles` 24.9 -> 49.9 MB (2.00), `h2client` 31.8 ->
59.1 (1.86), `h2core` 25.9 -> **69.2** (2.67). The layer protocol's "roughly 2x" rule of thumb
under-predicted `h2core` by 17 MB. **A slice deciding whether to split measures the gcc release
object; it does not convert the clang debug one.**

`h2core` at 69.2 MB gcc release is the largest object in this feature, and **it is not a violation of
anything.** The 40 MB target and the 75 MB ceiling are calibrated on *debug* objects, and the size
gate is enforced on `win-x86-*-debug` only - `src/utests/object-size-limits.json` is at rollout stage
0 and records `win-x86-vc143-release` at 96.77 MB building fine against that 75 MB debug ceiling.
The number that governs `h2core` is its **25.9 MB clang debug**, against a 40 MB target, which is
comfortable. The gcc release figure is recorded because it is the largest we have measured and
because S3.1's own acceptance asked the question, not because it fails a rule.

**S3.2 and S3.3 landed next** (`322f1bb`, `59e4885`, merged at `b66cc72`). `utf_baselib_h2client`
1 → 5 cases, 2 → 146 assertions, TSan clean over three runs, object 31.8 MB clang debug. The probe
and both case bodies are templates on the stream policy, so S3.3's test header adds only a TLS echo
peer - "S3.3 is S3.2 but over TLS" is a property of the code and not only of the prose. Four
negative controls, one per case, each exiting 201 with exactly the predicted failure, so no case is
vacuous. **That round also found the S4.1 blocker recorded above.**

**`utf_baselib_h2client` is now the largest module in this feature: 31.8 MB clang debug, 59.1 MB gcc
release measured** (the 2x rule of thumb predicted about 64). Most of that is a floor - the module
carries roughly 21 MB clang debug with no cases in it at all - so five cases did not buy it, but the
number is the number and **it is 16 MB from the 75 MB hard ceiling**. S4.1 adds the connection task,
the pool, the retry matrix and a stress suite to this same module. **Measure with gcc release before
adding to it, not with clang debug, and expect `utf_baselib_h2client2` to be the answer.**

Two smaller things from it worth keeping: `m_wasSocketShutdownForcefully` must be set synchronously
with only the socket call posted, because `isShutdownNeeded()` and `scheduleTaskFinishContinuation`
both read it to decide whether a TLS shutdown is owed and a stale read would start an
`async_shutdown` on a stream about to be shut down under it; and the plain policy installs its
socket through the public `attachStream()` because the base holds `m_socket` private, which costs
one extra `onStreamChanging` per socket creation that the base's own `createSocket` does not make.
The TLS stream is protected, so that policy mirrors the base exactly.

The JA3 and JA4 of a stock 3.5.4 client context, for the S7.1 spike to compare a browser against:

```
JA3      771,4865-4866-49195-49199-49200,65281-0-11-10-16-22-23-13-43-45-51,4588-29-23-30-24-25-256-257,0-1-2
JA3 hash 7f6ef6ebeba3cb0b7fe0b727b1fa8bba
JA4      t13d0511h2_1f640057409a_c3976d268853      (1503 bytes)
```

Five JA4 details that hand-working the vectors caught, and which a reimplementation will get wrong:
`supported_versions` is length-prefixed with **one** byte where every other list uses two; the
extension **count** includes SNI and ALPN while the extension **hash** excludes them; signature
algorithms are appended **unsorted** while everything else in a JA4 is sorted; with no sigalgs the
trailing underscore is not written; and an empty section hashes to **twelve zeros**, not to the hash
of the empty string.

### S3.1 — HTTP/2 Session engine (§4.5, §4.6)
- **Two contracts L2 imposes on this slice, neither of which the design pins.** Both were found while
  building the pieces this engine drives, and both are silent failures if missed.
  1. **Call the state machine once per completed header block, not once per frame.** CONTINUATION is
     deliberately not routable to it - a field block is one message and the `END_STREAM` transition
     belongs to the message, not to the frame carrying the last of it. Without this, a `HEADERS`
     carrying `END_STREAM` but not `END_HEADERS` closes the stream out from under its own
     CONTINUATION frames. The frame reader already owns block continuity (S2.1).
  2. **Decide how the HPACK decoder's capacity is set when a profile advertises below 4096.** S2.2's
     decoder takes one constructor parameter serving as both starting capacity and size-update
     ceiling, with no setter - correct for 4096 and for advertising *more*, but a profile advertising
     *less* leaves a window between our `SETTINGS` going out and the peer's ack in which the peer is
     still entitled to 4096 and we would reject a legal size update. Either add a setter there or
     construct the decoder after the ack; S2.2 flagged this as the item it would have stopped on had
     the default path not been correct.
  3. **Apply our own SETTINGS to local state when the peer's ACK arrives, not when they are sent**
     (RFC 9113 §6.5.3; L2 review). Item 2 is one instance of a general rule: until the ACK the peer
     is entitled to the old values, and every frame it sent under them precedes the ACK on the wire.
     The same holds for `SETTINGS_INITIAL_WINDOW_SIZE` on the *receive* side - S2.3's
     `applyInitialWindowSizeChange` applied to the receive windows at send time makes `consume`
     raise FLOW_CONTROL_ERROR on data the peer sent legally under the old window - and for
     `SETTINGS_MAX_FRAME_SIZE`, which is S2.1's `FrameReader::setMaxFrameSize`. The design says
     when the peer's settings apply and when ours time out, not when ours take effect; this is the
     answer, and it also decides where the SETTINGS_TIMEOUT timer stops.
  4. **A stream error reported on a closed stream cannot be answered through the registry** (L2
     review). S2.4 returns `FrameDisposition::StreamError( STREAM_CLOSED )` for a frame on a stream
     the peer reset, as §5.1 says, and §5.4.2 says a stream error is answered with RST_STREAM - but
     the same machine's `canSend( RST_STREAM )` is false once closed ("MUST NOT send frames other
     than PRIORITY on a closed stream"), so `StreamRegistry::onFrameSent( RST_STREAM, ... )` on it
     throws `UnexpectedException`. The RFC contradicts itself here and nghttp2 resolves it by
     ignoring the frame. Either treat StreamError on a stream that `isClosed()` as Ignored, or
     write the RST_STREAM without telling the registry - but decide it, because the naive path is
     an unexpected-exception crash against a peer that is merely late. The `HalfClosedRemote`
     case has no such problem: RST_STREAM may be sent there.
- Deliver: `http2/Session.h` (`SessionT`, role-neutral, single-threaded by contract): `feed(bytes)` →
  event queue (no callbacks out of `feed`); `wantsWrite()`/`produce(buffer)`; commands (submit, body,
  reset, consumed, ping, goaway, settings); message validation (RFC 9113 §8.1-8.3); write scheduling
  (control first, atomic header+CONTINUATION, DATA by RFC 9218 urgency within windows, bodies pulled);
  the limits table of §4.6; `ENABLE_PUSH=0` and `PUSH_PROMISE` = connection error (D11); the retryable
  flag on stream-closed events (feeds D6).
- Dep: S2.1, S2.2, S2.3, S2.4, S1.2, S1.3, S1.9. Earliest: after L2 primitives (can precede the G1
  gate).
- Accept: conformance as byte scripts incl. every §4.6 limit, message-validation rejections, early-
  response success, SETTINGS ack/timeout; a `feed` libFuzzer harness (optional). Tests → `h2core`;
  **run `make utests-sizes` and split to `utf_baselib_h2core2` if over target** (design §8.1).
- **Executed 2026-09-19.** `http2/Session.h` delivered; 15 cases in
  `utf_baselib_h2core/TestSession.h`, one per contract and one per §4.6 row — except row 6, the
  remembered closed streams, whose defaults are asserted in `Session_LimitsTests` and whose
  behaviour is pinned by S2.4's registry cases rather than here. Object 21.5 → **25.9
  MB** (a64 clang debug), 5th of nine and smaller than four unsplit modules, so **not split** — the
  next slice to add substantially to `h2core` re-measures. The four contracts, decided:
  1. **Header-block granularity.** Fragments are accumulated and exactly one
     `onFrameReceived( HEADERS, flags-of-the-opening-frame )` is made on END_HEADERS. The ordering
     inside that completion is a second, equally silent contract: **decode, then judge, then
     transition.** Decode first or the dynamic table desynchronises and the *next* stream's block
     dies of COMPRESSION_ERROR; judge before the transition because a malformed *final* response
     carries END_STREAM, and transitioning first closes the stream, after which §5.1 forbids the
     RST_STREAM §8.1 demands.
  2. **HPACK decoder capacity below 4096 — a setter was added** (`HpackDecoderT::
     setMaxDynamicTableSize`, additive, ceiling only). The decoder is constructed at
     `max( advertised, 4096 )` and the ceiling drops on the peer's ACK. "Construct after the ACK"
     was rejected as *wrong*, not merely awkward: at the ACK the peer's table is not empty, so a
     fresh decoder loses entries the peer still indexes. The setter never evicts — the table's
     capacity follows the peer's own size update (RFC 7541 §4.2), which is what keeps the two in
     step. **Corrected in the L3 fix round:** not evicting is not what saves a *conforming* peer,
     which §4.3 has evict on the reduction and §4.2 has signal it at the start of its next block,
     so it has already stopped naming what an eviction here would drop. What it buys is tolerance
     of a peer which reduced late or not at all.
  3. **Local SETTINGS apply on the peer's ACK.** Two sets, in-effect and in-flight, with a FIFO of
     unacknowledged frames; the ACK applies MAX_FRAME_SIZE (`FrameReader::setMaxFrameSize`),
     INITIAL_WINDOW_SIZE (receive windows), HEADER_TABLE_SIZE and MAX_HEADER_LIST_SIZE, and stops
     the SETTINGS_TIMEOUT timer. The receive side needed
     `ReceiveFlowControlWindowT::applyInitialWindowSizeChange` (additive, forwards to the inner
     window) — applied at send time instead, a window we shrank makes `onDataReceived` raise
     FLOW_CONTROL_ERROR on data the peer sent legally under the old value.
  4. **A stream error on a closed stream is Ignored** — no RST_STREAM, registry not told.
     `canSend( RST_STREAM )` is false once closed, so the naive path is an `UnexpectedException`
     against a peer that is merely late; §5.1 already requires tolerating frames on a stream the
     peer reset; nghttp2 does the same. (The "answering a reset with a reset invites a loop"
     argument this once carried was rhetoric and is withdrawn: §5.1 has a peer which sent
     RST_STREAM ignore ours, so there is no loop. The prohibition and the crash are the reasons.)
     The
     connection window is credited back whatever the disposition. `HalfClosedRemote` is not closed
     and answers normally. The same rule is applied consistently wherever a RST_STREAM would be
     illegal: the reason is recorded on the `StreamClosed` event even when no frame goes out.
- Decisions taken beyond the four, each written at its site in the header: PRIORITY received is
  dropped and never routed to the registry (§4.7, and it is legal on an *idle* stream, which the
  registry would report as a frame on an identifier we never opened); the profile's idle-stream
  PRIORITY frames are fingerprint shaping only and are not registered (their numbering versus the
  request stream ids is S7.3's); DATA padding is credited back at once on both windows so
  `consumed()` is about the octets the caller received; a closing stream credits everything still
  outstanding to the connection window and `consumed()` on a gone stream is a no-op; a SETTINGS ACK
  with nothing outstanding is a connection PROTOCOL_ERROR; `ENABLE_PUSH=0` is appended to a client
  profile that omits it and a client profile setting it non-zero is a `BL_CHK`; our own out-of-range
  SETTINGS are a `BL_CHK` at `applyLocalSettings()` rather than a protocol error when the ACK lands.
  Rows 7 and 8 of the §4.6 table (buffered response body, retry budget) are deliberately **not** the
  session's — they belong to S5.1 and S5.2; what the engine owes the second is the retryable flag.
- **L3 review fixes, 2026-09-19** (record: `notes/plans/issues/http2-l3-review-record.md`, findings
  2, 4, 5 and the S3.1 rows of 6). 17 cases now, the two new ones being
  `Session_DecodedHeaderListBoundTests` and `Session_DataPathValidationTests`:
  1. **The decoded header list is bounded from construction** (finding 2, Medium). Contract 3 was
     over-applied: §6.5.2 makes `SETTINGS_MAX_HEADER_LIST_SIZE` **advisory**, so there is no frame
     the peer sent legally under an older value to protect, and applying the bound only on the ACK
     left the default profile — which advertises none — unbounded forever and every profile
     unbounded until the ACK. HPACK expands, so that is the bomb §4.6 row 1 exists to close. Row 1
     now has a `SessionLimits::maxDecodedHeaderListSize` and a
     `Globals::MAX_DECODED_HEADER_LIST_SIZE_DEFAULT` of **64 KB**; the session starts at
     `max( row, advertised )` and the ACK applies the advertised value.
  2. **Contract 1's ordering holds on the DATA path** (finding 4). `handleData()` judges the frame
     — stream window fit, a header section before any DATA (§8.1), the content-length running
     total and, for END_STREAM, the final total — *before* `StreamRegistry::onFrameReceived`, so
     the RST_STREAM §8.1.1 demands is still sendable on a stream the frame is about to close.
     Judged only where `canReceive( DATA )` holds, so a frame the stream may not receive keeps the
     registry's own STREAM_CLOSED answer. Consequence pinned in the tests: a DATA frame which ends
     a message short is no longer delivered before the closure.
     **nghttp2 verified rather than recalled:** `session_on_data_received_fail_fast` refuses DATA
     before the response HEADERS ("DATA: stream not opened") but terminates the **connection**;
     §8.1.1 asks only for a stream error and that is what this engine sends.
  3. **The S3.1 nits of finding 6**, all fixed: the SETTINGS_TIMEOUT is measured from the oldest
     unacknowledged frame (the send time now travels with the frame); `isClosed()`'s comment no
     longer claims a GOAWAY of ours sets it; `submitHeaders()` reaps, so a server's closing answer
     emits its `StreamClosed` at once; `te` is compared case-insensitively (RFC 9110 token); a
     second GOAWAY cannot raise `lastStreamId` (§6.8) and is held to the first value; a connection
     error drops the header blocks queued before it rather than writing them after the GOAWAY.
  4. **Not changed:** `SessionLimits::settingsTimeoutInSeconds` stays at 10 s — see S4.2 below.

### S3.2 — Stranded plain stream policy (§3.1, D13)
- Deliver: `tasks/TcpStrandedStreams.h` (`TcpSocketAsyncStrandedBaseT`) deriving from the existing plain
  policy, hiding `createSocket` to build the socket on `asio::make_strand`; adds `getStrand()`,
  `createTimer()`, `postToStrand()`; `cancelTask` posts the shutdown to the strand.
- Dep: existing `TcpBaseTasks.h` only (no new dep). Earliest: t0.
- Accept: a task using it does concurrent read+write+timer on the plain socket with no data race under
  TSan. Tests → `h2client`.

### S3.3 — Stranded TLS stream policy (§3.1, D13)
- Deliver: `tasks/TcpSslStrandedStreams.h` (`TcpSslSocketAsyncStrandedBaseT`) - as S3.2 but over the TLS
  stream, constructing the `AsioSslStreamWrapper` on the strand via its strand constructor and using
  `configureClientStream()`.
- Dep: **L0.S0.3** (configureClientStream), S1.6 (strand ctor). Integrate after the G1 gate.
- Accept: concurrent read+write+timer over TLS with Asio's internal SSL handlers serialized; TSan clean.
  Tests → `h2client`.

### S3.4 — TLS client profiles, contexts, floor (§3.3, D4, D22)
- Deliver: in `crypto/` (extending the S0.4 split), `createAsioSslClientContext( profile )` (step 1
  common, profile step 2, shared trust via `SSL_CTX_set1_cert_store`); the cipher-name **allowlist**
  validator (no `@`,`!`,`+`,`-`,`:`; assert level still 2 after applying); the post-handshake floor check
  `chkNegotiatedParametersMeetFloor(...)` (≥ TLS 1.2 and TLS 1.3-suite-or-ECDHE/DHE+AEAD); advertise
  `session_ticket` when the profile does, never resume (cache stays off).
- Dep: **L0.S0.4**, S1.4. **Probe:** verify the API availability assumed on **both** flavors (3.5.4 and
  1.1.1w); gate impersonation-only APIs to `OPENSSL_VERSION_NUMBER >= 0x30500000L` (D2).
- Accept: a profile context reports level 2 and TLS 1.2 min and shares the global trust store; a
  `@SECLEVEL`-bearing cipher string is refused; the floor check rejects a below-floor negotiated suite;
  on 1.1.1w the impersonation entry point throws `NotSupportedException`. Tests → `h2profiles`.
- **The floor is three axes, not two, since the L3 review round.** `SSL_CIPHER_get_auth_nid` was added
  beside the key exchange and the AEAD check, accepting `{ NID_auth_rsa, NID_auth_ecdsa, NID_auth_dss,
  NID_auth_any }`, and `createAsioSslClientContext` appends `!aNULL:!eNULL` after the profile's names in
  the **TLS 1.2** list only. Finding 1 of `notes/plans/issues/http2-l3-review-record.md` is the account;
  it is hardening rather than a fix - security level 2 already refused every unauthenticated suite, as
  measured on the dist's own 3.5.4 - and what it buys is that §3.3's "strictly stronger than RFC 9113
  Appendix A" is now true of the check D4 names rather than of an OpenSSL behaviour nothing named.
- **Nit carried from the review:** `CryptoBase.h:22` includes `crypto/TlsClientProfile.h`, so every
  OpenSSL user in the library now compiles it. That header is `BaseIncludes.h` plus `<string>` and
  `<vector>`, so the compile cost is nil - but it is an **include edge added to core**, not merely a new
  name, and S7.3 should not widen it further without saying so.

### S3.5 — Tunnel stage: CONNECT + SOCKS5 (§3.6, D5)
- Deliver: `tasks/TcpTunnelStage.h` - HTTP `CONNECT` (optional Basic auth, bounded status/header read) and
  SOCKS5 (RFC 1928/1929, no-auth and user/pass, `DOMAINNAME`), run via `beginPreHandshakeStage`; resolver
  targets the proxy while SNI/verification stay with the origin. HTTPS proxies excluded.
- Dep: **L0.S0.2** (hook). Integrate after the G1 gate.
- Accept: against in-process fake proxies - success, auth, failure, cancel; origin SNI preserved. Tests →
  `httpclient` (fake-proxy fixtures).
- **Two obligations inherited from S0.2**, found there by the suite's own leak check and documented on
  the hook itself (commit `fea3a38`) - neither is optional and both are easy to miss:
  1. A stage that parks the continuation across an async operation forms a **reference cycle** with the
     task, because the continuation holds an `ObjPtrCopyable` reference to it. It must release the
     continuation when the task stops, or the task leaks.
  2. A stage with no socket I/O in flight is **never woken by the base `cancelTask()`**, which cancels
     socket operations. It must cancel its own async objects - timers, resolvers - itself.
- **The handshake retry this stage must be re-entrant against is REACHABLE.** An earlier version of
  this note said it was unreachable with a real peer; that was true when it was written and stopped
  being true on **2026-09-18**, when the classifier was widened as its own gated change-set
  (`notes/plans/issues/tls-handshake-retry-unreachable-record.md`, **CLOSED**). The note outlived
  the defect and was passed on to S3.5's work order before it was caught. Write the once-per-attempt
  behavior to the contract regardless - which is what S3.5 did, structurally, by constructing a
  fresh negotiation in `beginPreHandshakeStage` so nothing from the previous attempt is reachable -
  but do not reason from "the retry never fires", because it does. (The lane reported that S4.1 and
  S5.2 carry the same stale note; they do not - they do not reference the record at all, and the
  only other mention of it, under S0.3, already says CLOSED.)
- **Do not begin multi-operation work in the stage without resetting the accounting.** This is the one
  place the hazard becomes real: `MultiOperationTaskT` clears its accounting only in `scheduleNothrow`,
  while `scheduleTaskFinishContinuation` (`TcpBaseTasks.h:1437`) retries the transaction **in place**.
  A tunnel that calls `beginOperation()` or ends a handler with `BL_TASKS_HANDLER_END_MULTIOP` runs
  *before* the handshake, so a retry would restart it with a non-zero pending count - and if the
  terminal had been taken, the task would never complete. Either keep the accounting out of the stage
  or reset it per attempt. The hook's own doc comment already requires the stage to carry no state
  from one attempt to the next; this accounting is exactly such state.

### S3.6 — ClientHello capture + JA3/JA4 (§3.3, §6.3)
- Deliver: `crypto/TlsClientHello.h` - consume the S1.6 capture hook; parse the ClientHello bytes;
  compute JA3 and JA4 strings.
- Dep: S1.6. Accept: JA3/JA4 computed from a captured ClientHello match hand-worked values for a known
  input. Tests → `h2profiles`.

*(Note: the wrapper additive changes these three TLS slices rely on are all in S1.6, one owner of
`AsioSslStreamWrapper.h`, so S3.3/S3.6 have no intra-L3 edge to each other.)*

---

## 6. Layer L4 — I/O shell & test peer

Depends on L2, L3. S4.1 is the layer prerequisite for S4.2/S4.3 (one documented edge); S4.4 is parallel
with all of them.

### S4.1 — Connection establishment base + ALPN dispatch (§5.1, §5.5, §5.7)
- Deliver: a connection task base over `TcpConnectionEstablisherConnector< STREAM >` with the stranded
  policy and `MultiOperationTaskT`: resolve → connect → tunnel → handshake → floor check → read ALPN,
  then construct the h2 driver or hand the connected stream to the h1 driver (via a factory in S2.6);
  connect/handshake/SETTINGS-ack timers on the strand.
- Dep: S3.2, S3.3, S3.4 (floor), S3.5 (tunnel), S0.1 (multi-op), S2.6. Integrate after G1 gate.
- **F-L0-1 unblocked this slice** (§2). `MultiOperationTaskT` now takes its base as a template
  parameter, and the composition is pinned in the tree by
  `Tasks_MultiOperationTaskOverConnectionEstablisherTests`, which derives exactly the shape this slice
  needs. The mix-in needed nothing further: `isClosing()`, `beginOperation()`, `beginClose()`,
  `pendingOperations()` and the virtual `initiateClose()` are all reachable from the derived task, and
  `BL_VARIADIC_CTOR` forwards the establisher's three-argument constructor unchanged.
- Keep the task's **two paths out consistent** (design §3.2): `cancelTask()` for an external cancel and
  `initiateClose()` for the error path, both converging on the single terminal `notifyReady`.
- **BLOCKER for this slice, found in S3.2 and undecided: a deliberate `beginClose()` with operations
  in flight completes the task FAILED.** `beginClose()` records no error, so the first
  `operation_aborted` from what `initiateClose()` cancelled becomes the task's error and the task
  reports `isFailed()` on its own clean close. This slice's connection task closes deliberately as a
  matter of course - GOAWAY, idle deadline, last stream finishing - with a read and timers
  outstanding, so it hits this every time, and S5.2's pool would count a clean shutdown as a failed
  connection. `MultiOperationTask.h` is landed gated core, so the fix is **its own tested
  change-set** and the decision is the maintainer's.
  `notes/plans/issues/multioperation-deliberate-close-fails-task-record.md` has the diagnosis, why
  the existing S0.1 coverage does not reach it, and three candidate fixes with a recommendation.
  **Do not start S4.1 before this is settled.**
- **The accounting is reset per schedule, not per connect attempt.** The mix-in clears it only in its
  `scheduleNothrow` override (`MultiOperationTask.h:311-341`), while
  `TcpConnectionEstablisherConnector::scheduleTaskFinishContinuation` (`TcpBaseTasks.h:1437`) restarts
  the whole resolve/connect/handshake transaction **in place** and never passes through it. Harmless
  only because that branch is guarded on `! hasHandshakeCompletedSuccessfully()` and this slice's loops
  start at `continueAfterConnected()`. If anything here calls `beginOperation()` or ends a handler with
  `BL_TASKS_HANDLER_END_MULTIOP` **before** the handshake completes, a retry restarts with a non-zero
  pending count, and if the terminal was taken the task never completes. See also S3.5.
- **Declare your own `this_type`/`base_type`.** The mix-in's are public and hide the establisher's
  protected ones, so redeclare the pair - which is what every derived task here does anyway
  (`SimpleHttpTask.h:57-58`), so it is the house idiom rather than a workaround.
- **Build every timer on `getSocket().get_executor()`, never on a thread pool's `aioService()`.**
  Under the stranded policies of design §3.1 that executor *is* the strand, and the pool's
  `io_service` is not - so a timer built on the pool runs its handler off the strand, which is the
  race D13 exists to make unrepresentable. The two happen to be the same `io_context` for a TCP task
  today, because `continueAfterResolved` creates the socket on
  `ThreadPoolDefault::getDefault( getThreadPoolId() )` (`TcpBaseTasks.h:1414`) and, unlike
  `TimerTaskBaseT::resetTimer` (`TaskBase.h:1749`), does **not** honour a queue-local pool - so the
  mistake is invisible until a stranded policy is in play, which is precisely this slice.
  `Tasks_MultiOperationTaskOverConnectionEstablisherTests` models it correctly; copy from there.
  Use the `BOOST_VERSION` guard of `SimpleHttpTask.h:301`.
- **The §5.7 "resolve through preface" deadline is this slice's, and it must be armed BEFORE the
  tunnel stage rather than after it.** S3.5 owns no timer: a proxy which accepts the TCP connection
  and then never answers holds the task until an external cancel. That is deliberate and consistent
  - the establisher has no connect deadline of its own either - and the stage's class comment
  records why, since a timer inside it would acquire obligation 2 of `beginPreHandshakeStage` with
  it (`TcpTunnelStage.h`, the three obligations). So the 60 s connect timer is the only thing which
  bounds a silent proxy, and one armed after `beginPreHandshakeStage` has returned is never reached
  on that path. L3 review, `notes/plans/issues/http2-l3-review-record.md` §6 and §7.
- **Choose the retry budget; a consistently truncating peer now costs six attempts, not one.** The
  handshake retry is reachable with a real peer since the classifier was widened after L0 (§2,
  decision 2): `scheduleTaskFinishContinuation` restarts the whole resolve/connect/handshake
  transaction on `asio.ssl.stream:1` as well as on `eof`, immediately and with no delay between
  attempts, up to `MAX_RETRY_COUNT` = 5 (`TcpBaseTasks.h:1276`). A hard TLS rejection which closes
  the connection without an alert therefore fails after six handshakes where it failed after one,
  and nothing in the suite times it. `m_maxRetryCount` is the derived task's to set - the TLS probes
  of `TestTcpPreHandshakeStageTls.h` pass 1 - so decide the h2 task's budget here and record it in
  the acceptance.
- Accept: establishes plain and TLS; selects driver by ALPN; forced-http/1.1 path works; floor failure
  aborts before any HTTP byte. Tests → `h2client`.
- **Constructing the `NegotiatedProtocol`** (L2 second pass). `fromAlpn( "" )` throws by design, so
  an empty ALPN selection on a TLS connection - the peer completed the handshake without choosing -
  goes through `withoutAlpn( HttpProtocol::Http11 )`, as does every cleartext connection (`Http11`,
  or `Http2` by prior knowledge); only a non-empty `SSL_get0_alpn_selected` result goes through
  `fromAlpn`. Then hand the value to `createDriver`, which is the one moment it is in hand: the
  driver receives it at construction and stores it in a **`const`** member, because `negotiated()`
  returns a reference and is read by the pool and the request task off the strand - a member that
  is ever reassigned after construction would be a data race on a `std::string`. Constructed after
  ALPN, a driver never reports `Unknown`; that value belongs to the pool's `Connecting` placeholder.
- **Executed 2026-09-19, and one thing written here did not survive contact.** "S4.1 registers the
  h2 creator" (it was in `ClientConnection.h`'s own comment, now corrected) is **wrong**: the h2
  task does not go through the factory for itself at all. A driver which `attachStream()`s a stream
  created elsewhere **loses that policy's `m_strand`** - `TcpStrandedStreams.h` says so - so the h2
  task must BE the object which created the socket, not one handed a stream afterwards. The factory
  is what the **fallback** goes through, and what the session populates. `createDriver` above is
  therefore the fall-back path, not both paths.
- **The blocker S4.1 hit first was in S3.5, not here** (fixed in the same round, `ffcc805`):
  `TcpTunnelStageT` over a TLS stream policy had never compiled, because the stage's three socket
  operations went through `getSocket()`, which for a TLS policy forwards to the wrapper's
  `lowest_layer_type` - an `asio::basic_socket<tcp>` with no `async_read_some` and no
  `async_write_size`. The intent was right and its comment said so; the type could not carry it.
  `next_layer()` is the object which is both cleartext and an `AsyncStream`. **Nothing had
  instantiated that combination**, so S3.5's release validation under both toolchains passed over an
  uninstantiated template - the same class of gap as `BeastBoostImports.h`.
- **Two decisions worth carrying.** The retry budget is **1**, not the establisher's 5: six
  handshakes with no backoff against a consistently rejecting peer is not a retry policy, and the
  real one belongs to the pool (§5.4). And the deadline and the ALPN offer are taken in a
  `beginPreHandshakeStage` override **before** the base call - after it the tunnel's first write, or
  the TLS `async_handshake` the default hook starts synchronously, is in flight, so anything throwing
  then completes the task with an operation outstanding, in a phase outside the mix-in's accounting
  where the mix-in would not catch it. (This bullet said "an `async_connect` in flight" until the L4
  review. There is none: the connect completed before the hook was entered. The header at
  `ClientConnectionTaskBase.h:511-519` states the argument correctly - it is about arming in
  `continueAfterResolved`, where an `async_connect` really would be in flight - and the conclusion
  holds unchanged for the write and the handshake.) Resolve and TCP connect are therefore outside
  the deadline; both are OS-bounded, and the tunnel is the part bounded by nothing else. §5.7's row
  was amended to say "TCP connected through preface" in the L4 fix round, and records what the OS
  bound actually costs.
- **`utf_baselib_h2client` is CLOSED to new slices.** With S4.1 it is **37.1 MB clang debug and
  73.1 MB gcc release**, measured on the committed revision. It stays - the target and ceiling are
  calibrated on debug objects and 37.1 is under the 40 MB target - but the marginal ratio of this
  slice alone was **2.64**, so converting the debug delta would have under-predicted the release one
  by 8 MB. **S4.2 goes to `utf_baselib_h2client2`.**

### S4.2 — HTTP/2 driver (§5.1, §5.2, §5.7)
- Deliver: `http2/Http2ConnectionTask.h` - the opening coalesced write (preface+SETTINGS+WINDOW_UPDATE+
  PRIORITY+first HEADERS); the read loop into pooled `data::DataBlock`s (payloads copied once) →
  `Session::feed` → drain; the single-in-flight write pump; the connection-level timers of §5.7 on the
  strand - optional keepalive `PING` and its reply deadline, the connection idle timer (its lifetime
  value comes from the pool policy, S5.2); GOAWAY drain and graceful close (`GOAWAY( NO_ERROR )`
  best-effort, then the inherited TLS shutdown once no operation is pending). Concurrency rules L1-L4
  of §5.2 (own state on the strand; talk to request tasks by posting + mailbox).
- **Obligation carried from the L3 review (finding 6).** `SessionLimits::settingsTimeoutInSeconds`
  is **10** and design §5.7 lists the SETTINGS acknowledgement timeout as **30 s**. The engine's
  comment says the number is its own, and S3.1 deliberately left it alone; this slice owns the
  reconciliation — set it from §5.7 or amend §5.7 to record 10, but decide it here.
- Dep: S4.1, S3.1. Accept: request/response over h2 against the test peer; full-duplex upload+download;
  keepalive and idle close observed; TSan clean; **and h2 over TLS on the default hardened context
  passes on both OpenSSL flavors** - that is D2's promise that everything but impersonation works on
  1.1.1w. Tests → **`utf_baselib_h2client2` (cleartext) and `utf_baselib_h2client3` (TLS)**, not
  `h2client`, which S4.1 closed.
- **Executed 2026-09-19.** 11 cases, TSan clean over four runs (the known-open `ThreadPoolImpl`
  report did not appear, nothing here resizing a pool, so it is clean rather than merely unchanged),
  clean under clang and gcc at release. The 1.1.1w half of the acceptance is **owed, not claimed**.
- **The server half of ALPN was added here**, strictly additive - `setAlpnServerPreference`, 280
  insertions and zero deletions in `CryptoBase.h`. The **server's** preference order wins, and no
  overlap answers `SSL_TLSEXT_ERR_NOACK` rather than the fatal alert RFC 7301 §3.2 also permits,
  because design §5.5's fallback needs "handshake completed, nothing selected" to be representable.
- **SETTINGS timeout reconciled at 10 s** and design §5.7 amended. The engine's number wins on its
  merits: RFC 9113 gives `SETTINGS_TIMEOUT` no value, and the keepalive PING reply deadline in the
  row below is 15 s - so 30 would make the *first* control frame a new connection must answer twice
  as lenient as the steady-state liveness check.
- **An ordering hazard, handled in the driver and recorded in §5.7.** `Session::produce()` writes
  the control queue before the header-block queue, so a `RST_STREAM` queued while a stream's own
  `HEADERS` are still waiting **overtakes them**, and the peer sees a reset for a stream it never
  heard of - a connection error under §5.1. "Submit, then cancel" is the ordinary shape of a request
  whose deadline expired while queued. The driver holds such a cancel back; the engine's queue order
  is deliberately unchanged, since that order is what keeps a header block with its own CONTINUATION
  frames.
- **Two hazards worth carrying to any lane writing tests.** `UTF_FAIL( msg )` takes
  `UtfGlobals::g_lock` and **then** evaluates `msg`, and `bl::os::mutex` is not recursive - so a
  `UTF_REQUIRE` inside a `UTF_FAIL` message **self-deadlocks the binary** rather than failing it.
  And a wait whose predicate differs from the assertion's is a flake: `waitForRecordsOf( recorder, 1 )`
  was satisfied by the peer's own "connected" record, so a case asserted on a GOAWAY before it
  existed - passing three times, then failing. **Make the wait predicate the same predicate as the
  assertion**, and there is no count to guess.
- **Module sizes, measured.** Before the split 46.8 MB clang debug (117 % of target) / 98.4 MB gcc
  release; after, `h2client2` 37.0 / 75.0 and `h2client3` **39.4** / 77.7. The second is *at* the
  target with 0.6 MB of headroom holding two cases, so it is closed on arrival. The figure which
  decides splittability is `before - (a + b)` = **-29.6 MB**: a split relocates size and pays the
  TU floor plus the common instantiation weight twice, it does not reduce. This is the phenomenon
  `test-instantiation-weight-deferral.md` records. **S5.2 starts `utf_baselib_h2client4`.**

### S4.3 — HTTP/1.1 driver (§5.5)
- Deliver: `httpclient/Http1ConnectionTask.h` - one request at a time over `Http1Codec`; returns to the
  pool if fully consumed and neither side said close.
- Dep: S4.1, S2.5. Accept: request/response over h1; keep-alive reuse; against the library's own
  `HttpServer`. Tests → `httpclient`.
- **CORRECTION, found in execution: "keep-alive reuse against the library's own `HttpServer`" is not
  achievable**, and the library says so itself. `httpserver/Response.h` puts `Connection: close` on
  every response, with the comment *"Currently the HTTP server implementation does not support HTTP
  1.1 persistent connections (Keep-Alive) and pipelining, so we request that the connection is
  closed"*. The evidence is therefore split, and the split is **better** than the criterion it
  replaces because it exercises both directions: `HttpServer` proves the request/response path and
  the **negative** half of the derivation against a real peer with a real parser, while a scripted
  loopback peer proves reuse, HTTP/1.0 with and without keep-alive, read-until-close, interims and
  chunked trailers.
- **Executed 2026-09-19.** Tests went to a **new `utf_baselib_httpclient3`**, not `httpclient`:
  S4.3 alone takes that module 25.49 -> **44.54 MB** clang debug, past the 40 MB target, in a module
  design §8.1 still has the session and the pool landing in. The new module is **39.66 MB** and is
  declared CLOSED to new slices in its own main; a slice that would add there takes
  `utf_baselib_httpclient4`. Clean under clang and gcc at release, 6/6 and 44/44.
- **Three defects the tests found by being run**, the third being the one that matters most:
  trailers were never delivered at all; a request the driver refused to render left the connection
  draining, because reuse was derived from the absence of an error code rather than from whether the
  socket had been touched; and **a begun-but-never-started read would HANG the task rather than fail
  it** - `beginOperation()` followed by a throwing `async_read_some` leaves the pending count
  permanently above zero and the terminal is taken only at zero. Both read and write now balance the
  accounting in a catch.
- **Notes carried for L5.** The driver receives an **attached** stream, so the policy's `m_strand`
  is null and `getStrand()`/`createTimer()`/`postToStrand()` all assert - the strand survives as the
  **socket's executor**, and everything posts through `getSocket().get_executor()`. The always-armed
  idle read is load-bearing, because the mix-in takes its terminal from `onOperationCompleted()` so
  a pending count of zero while not closing can never be completed by `beginClose()`. State is
  settled **before** `onClosed` is delivered, since the request task calls `releaseStream` from its
  handling of `onClosed` and the pool then asks `state()`. Trailers are delivered from the two
  message-completion sites and **not** from inside `finishStream`, which is NOEXCEPT - a sink
  throwing out of `onTrailers` there would hit `BL_RIP_MSG`. And **`cancel()` ends the connection**:
  HTTP/1.1 has no stream reset, so this is the one place §5.7's "cancelling a request never closes
  the connection" cannot hold.
- **Reuse is derived here, not reported by the codec** (L2 review). `Http1ResponseParser` exposes
  `httpVersion()`, `needsEof()` and the header list but no keep-alive verdict, and Beast's own
  `keep_alive()` is deliberately not re-exported. An HTTP/1.0 response without
  `Connection: keep-alive`, a `Connection: close` **from either side - the request's own word binds
  too**, a body framed by the close (`needsEof()`), a 101, and any byte left unconsumed after the
  message all mean the connection is not returned to the pool. `statusCode()` goes to
  the sink through the `status` parameter of `onHeaders` (S2.6, fixed after the L2 review).
  **The request-side limb was claimed here, in the header comment, in design §5.5 and in a merge
  message while the code consulted only the response** - found by the L4 review, fixed in
  `ff8693c`. The enumeration above is now the code's, input for input.
- **Interim responses arrive after the fact** (L2 second pass). `Http1ResponseParser` files a 1xx
  into `interimResponses()` and restarts on the same buffer; it has no per-interim callback. So this
  driver delivers each filed interim as `onHeaders( handle, interim.statusCode, headers, true )`
  in order, *before* the final block, once the final header section completes - which keeps the
  sink's ordering guarantee and the stub's status/flag invariant (1xx except 101). A 103 Early
  Hints therefore reaches the request task later than it reached the wire; nothing in this client
  acts on hints, so that is accepted rather than fixed with a parser callback.

### S4.4 — Test peer (§8.2, D8)
- Deliver: in `src/utests/include/utests/baselib/` - `Http2TestServer` (`TcpServerBase< STREAM >` + a
  server-role `Session`, scriptable: delays, GOAWAY after N, `REFUSED_STREAM`, window stalls, trailers,
  1xx) and `RawFrameScriptPeer` (byte-exact malformed input). No production hardening (that is the server
  deferral). **Never included from `src/include/`.**
- Dep: S3.1 (server role). Earliest: after S3.1 - parallel with S4.1. Accept: drives each scripted
  behavior; used by S4.2 tests.
- **Executed 2026-09-19.** `utf_baselib_h2core` 61 -> 66 cases, clean under clang and gcc at
  release. **S3.1's role-neutrality holds** - nothing in the engine needed changing to build a
  server on it, which is the claim the slice existed to test.
- **The port is ephemeral and it cost no core change.** `continueAfterResolved()` is virtual and
  `m_acceptor` is protected, so the bound port is read back there and signalled. No fixed port, no
  `MachineGlobalTestLock`, several peers at once, **no readiness probe** - which at this peer would
  arrive as a connection - and no sleep, so `h2core` keeps its runs-in-parallel property.
- **Three defects it found by being run rather than reasoned about.** (1) **A window stall is an
  event, not a duration**: scripted as a delay plus a credit it hung 15 s on the first run, because
  the credit is an unpredictable number and a client still short of window re-stalls with the
  one-shot credit spent. The step now does not advance while the stream receive window is positive.
  (2) **An opening delay was undone by the first thing the client said**, because every read path
  ends in `pumpWrites()` - which matters exactly because a peer that accepts and says nothing is
  what S4.1's connect-through-preface deadline catches. (3) **The assertion count was unstable**
  (25/26/27) because a helper asserted once per write; tier 3 compares per-case counts, so that was
  a red gate waiting rather than a cosmetic wobble.
- **BLOCKING FOR S4.2's TLS ACCEPTANCE: the server half of ALPN does not exist.** The tree has
  `SSL_set_alpn_protos` and `SSL_get0_alpn_selected` - the client half - and **no**
  `SSL_CTX_set_alpn_select_cb` anywhere in `src/include/`, which is the callback a server needs to
  choose `h2`. So **no TLS test peer is possible** until it is added and S4.4's peer is
  cleartext-only by necessity, not by choice. **Assigned to S4.2**, riding with that slice rather
  than taking its own gated change-set, because it is *additive API* rather than a modification of
  an existing core path - the standing distinction in this project. If it cannot be kept additive,
  that changes the answer and it becomes its own change-set.
- **Cost to whoever includes it:** about **+8.6 MB clang debug and +19 MB gcc release**, measured.
  `RawFrameScriptPeer.h` includes `Http2TestServer.h` - one rendezvous implementation, deliberately -
  so a module wanting only the raw peer pays the same. `utf_baselib_h2core` itself went 26.1 -> 34.68
  MB clang debug and 69.2 -> **88.21 MB gcc release** (whole-object ratio 2.54). Nothing is
  violated: 34.68 is under the 40 MB target, and the size gate is `win-x86-*-debug` only. It is not
  the largest object in the tree either - `utf_baselib_http` is 101.3 MB gcc release - but it is the
  largest in this feature.

---

## 7. Layer L5 — Client orchestration

Depends on L2, L4. S5.1 and S5.2 are parallel via the S2.6 contracts.

### S5.1 — HttpClientRequestTask (§5.3, §5.7)
- **A GAP IN THE S2.6 CONTRACT, found by S4.2 and left for this slice to resolve. RESOLVED IN S5.1
  BY ADDING THE EVENT**, as its own change-set. `ClientConnection` had **no "the connection wants
  more body" event**, so a streaming upload could not be *pulled*: the driver held whatever
  `provideBody()` handed it until the windows took it. Documented at `pumpBody()` in
  `http2/Http2ConnectionTask.h`, and deliberately not fixed in L4 because closing it changes a
  landed contract.
  - **Why the event and not an amendment saying buffering is acceptable.** With the contract as
    published a request task holding a `BodySource` had exactly two possible behaviours, and only
    one of them was an implementation: hand the whole source over, which buffers the entire upload
    inside the driver and is *worse* than not streaming, or hand over a bounded amount and then
    stall, because nothing on that interface reports stream progress upwards - `onData` is response
    side, `consumed()` goes the other way, `freeStreamSlots()` is connection level. The contract
    therefore **forced** the first. An amendment recording that as acceptable would have left
    `BodySource` with no reason to exist over a buffered body; it would have deleted the feature
    rather than documented it. The event also cost least when taken here: nothing consumes streaming
    uploads yet, and the three sink implementers are all test code.
  - **As landed:** `ClientStreamEventSink::onBodyWanted( handle, bytes )`. Raised only for a stream
    submitted with a `BodySource`; **outside** the sink's response ordering, since it concerns the
    request body and interleaves freely, and bound only by never following `onClosed`; answered by
    **exactly one** `provideBody()`, which is what bounds the driver to one un-placed chunk per
    stream. A null block with `endStream` false is the legal "nothing right now" answer. The h1
    driver never raises it - it refuses a `BodySource` at `submit()` - and that is stated at its own
    `provideBody()` rather than left to be rediscovered.
  - **What it does not close**, recorded rather than glossed: `BodySource::read()` is synchronous and
    `BodyReadResult` admits a source which yields nothing without being finished, so such a source
    can still stall an otherwise idle connection until the total timeout. Closing that needs a
    readiness signal on `BodySource`, a change to the frozen `ClientTypes.h`. See
    `issues/body-source-readiness-deferral.md`.
- Deliver: `httpclient/HttpClientRequestTask.h` - one per request, protocol-agnostic against
  `ClientConnection`; `scheduleTask` only posts a start handler (honors `TaskBase.h:857`); a mailbox with
  ordered drain under the task lock; buffered (default, 64 MB cap) and streaming body modes; backpressure
  = report-consumed drives `WINDOW_UPDATE`; the request-level timers of §5.7 (total incl. pool wait,
  default 30 min; response-headers and stream-idle, off by default) → on expiry `RST_STREAM(CANCEL)` +
  `TimeoutException` with the existing message shape, connection untouched. Handlers run on
  `ThreadPoolId::GeneralPurpose` (design §5.2).
- Dep: S2.6, S0.1. Earliest: against the S2.6 interface (integration needs S5.2/L4). Accept: completion,
  timeout, cancel, backpressure against a stub connection then the real drivers. Tests → `h2client`.
- **Two things S2.6 did not carry as first published** (L2 review): the response status, which
  `onHeaders` now carries as a `status` parameter (fixed after the review - see S2.6), and the
  stream-idle timeout of design §5.7, which is not on `ClientRequest` (only `totalTimeout` and
  `responseHeadersTimeout` are). Fill `ClientResponse::protocol()` and `negotiatedAlpn()` from the
  one `negotiated()` value and never separately - see the second-pass note under S2.6 - and take
  the response's status from the final `onHeaders` only; an interim's status is that interim's own. It is off by
  default, so a session-level knob read here is enough; putting it on the frozen request type is a
  negotiated change like any other.

### S5.2 — ConnectionPool (§5.4, D6, D21)
- Deliver: `httpclient/ConnectionPool.h` - key (scheme/host/port/proxy/TLS-profile/h2-profile/verify);
  `Connecting` placeholder inserted under the pool lock before release so concurrent requests queue;
  dispatch to a `Ready` connection with a free stream slot (peer `MAX_CONCURRENT_STREAMS`, assume 100
  until SETTINGS); retry of provably-unprocessed + replayable requests (GOAWAY last-id, `REFUSED_STREAM`,
  pre-write failure), bounded; GOAWAY draining incl. the common double-GOAWAY; coalescing **designed,
  default off** (D21); disposal fails queued requests, GOAWAYs, flushes. Leaf-lock discipline of §5.2 L4.
- Dep: S4.1, S4.2, S4.3, S2.6. Accept: queueing behind a placeholder, slot limiting, retry matrix, GOAWAY
  handling, disposal; TSan clean. Tests → `h2client`.
- **Set the draining reserve** (L2 review). S2.4's `StreamRegistry::isDraining()` fires only once the
  identifier space is spent unless `setDrainingReserve( ... )` is called: the margin of design
  §4.3's "approaching 2^31-1" was left to the pool on purpose, and the default is none. Choose it
  from what this pool queues ahead, and pin it.
- **As landed.** Tests → **`utf_baselib_h2client4`**, a new module (every sibling was at or near the
  40 MB target), plus one case in `h2core` where a `Session` is already instantiated. Both numbers
  this slice owned are chosen and pinned: the **draining reserve is 1024**, four times the pool's
  own per-connection dispatch ceiling, and it needed a wiring line to be settable at all -
  `SessionLimits::drainingReserve` is new, and the `Session` constructor passes it to the registry.
  The **establishment bound is 120 s and belongs to the pool**, which closes §5.7's ownerless
  deferral; §5.7's table has the row. Two further things the contract could not give the pool are
  recorded in §5.4 rather than left implicit: the retry **counter** is split between the pool and
  the request task because `acquire`/`releaseStream` carry no request identity, and there is no
  connection→pool readiness notification, so the pool polls on a backing-off maintenance tick which
  is the seam such a notification would plug into.

---

## 8. Layer L6 — Session

### S6.1 — ClientSession (§5.6, §5.8)
- Deliver: `httpclient/ClientSession.h` (`ClientSessionImpl`) - holds pool, active profile, proxy config,
  cookie jar, redirect policy, decoder registry; creates request tasks; applies the profile's header set
  by request kind and the `accept-encoding` = profile-list ∩ registered-decoders rule (strict mode returns
  raw bytes); the API of design §5.8.
- Dep: S5.1, S5.2, S1.1, S1.4, S2.7, S2.8, S2.9. Accept: end-to-end GET/POST over h2 and h1 through the
  session against the test peer and the library `HttpServer`; redirects, cookies, strict decode. Tests →
  `httpclient`.
- **Three things L2 leaves to this slice** (L2 review). (1) An HTTP/1.1 request carries ONE `Cookie`
  field (RFC 6265 §5.4): the jar's `cookieHeaderValue` and any caller-supplied `Cookie` header have
  to be merged, not both sent - and on a same-origin redirect the caller's header survives
  `dropCredentialHeaders` while the jar recomputes, so the merge is where the two meet. (2)
  `RedirectPolicy::dropCredentialHeaders` also drops `Proxy-Authorization` on a cross-origin hop,
  beyond the work order's "Authorization + cookies": harmless while proxy credentials are session
  configuration applied by the tunnel stage (S3.5), wrong if a caller-supplied header is ever what
  carries them - keep the two consistent. (3) `CookieJar`'s `isHttpApi` is always `true` from this
  client; the jar does not implement RFC 6265 §5.3 step 11's non-HTTP-API clause (a non-HTTP set
  must not replace an existing HttpOnly cookie), which matters only if something ever passes
  `false`. Design §5.6 now records what the jar settled on Secure cookies set over `http`; if this
  session speaks both schemes to one host, that is the item to take up.

---

## 9. Layer L7 — Impersonation

Depends on L2, L3, L6. This layer has a real internal DAG (spike → content → apply → report → vectors);
it is the least parallel layer, which is expected at the top. Requires OpenSSL 3.5+ (D2); on 1.1.1w the
entry point throws (asserted by S3.4). Illustrative values in the design are **not** to be trusted -
capture them (S7.5).

### S7.1 — TLS fidelity spike (§6.3, D3, D23)
- Deliver: recorded spike results per profile. Earliest: after S3.4 + S3.6 - **runs in parallel with
  L4-L6.**
- Probe (**the measured spike**): dump each profile's ClientHello (S3.6), compute JA3/JA4; verify every
  "yes" in the design §6.3 knob table against the real 3.5.4; evaluate - **default off** - whether
  `SSL_CTX_add_custom_ext` can add GREASE / ECH-GREASE / an inert ALPS decoy toward Chrome's JA4, and
  record each workaround's interop risk. **Never** emulate `delegated_credentials`. Adopt nothing without
  evidence.
- Accept: a written result table feeding profile grades and deviation lists; no adopted workaround lacks a
  recorded interop check.

### S7.2 — Profile content + loader + validation (§6.2, §6.3-6.5, D10)
- Deliver: the Chrome/Edge/Firefox/Safari shapes (TLS/HTTP/2/header), version strings separate from
  shape, JSON literals parsed at first use, each with its grade (from S7.1) and deviation list; the loader
  validating untrusted input (cipher allowlist via S3.4; setting ids/values in range; header names as
  tokens, values free of CR/LF/NUL; bounded lists).
- Dep: S1.4, S3.4, S7.1 (grades). Accept: each built-in profile loads and validates; malformed profiles
  are rejected. Tests → `h2profiles`.

### S7.3 — Apply profile across layers (§6.3-6.5)
- Deliver: the wiring - TLS profile → `createAsioSslClientContext` (S3.4); HTTP/2 profile → Session
  settings order / connection WINDOW_UPDATE / PRIORITY / pseudo-header order (S3.1); header profile →
  session header set by request kind (S6.1); `accept-encoding` intersection (S2.9/S6.1).
- Dep: S3.4, S3.1, S6.1, S2.9, S7.2. Accept: a request under a profile emits the profile's settings,
  pseudo-order and headers. Tests → `h2profiles`.

### S7.4 — Fidelity report + fingerprint renderer (§6.4, §6.6)
- Deliver: `http2/Fingerprint.h` (renders the frames a session actually produced in the
  `SETTINGS|WINDOW_UPDATE|PRIORITY|pseudo-order` form) and the report on the connection and each response
  (profile+grade, backend+version, per-knob Honored/Approximated/Unsupported, the JA3/JA4 actually sent,
  the HTTP/2 fingerprint sent, the `accept-encoding` sent, the deviation list).
- Dep: S3.6, S3.1, S7.3. Accept: the report reflects what was sent, not what was requested. Tests →
  `h2profiles`.

### S7.5 — Capture procedure + test vectors (§6.7)
- Deliver: the documented capture procedure and, per profile, the pinned vectors - exact opening bytes
  from `Session`, the fingerprint string, header order per request kind, and the effective JA3/JA4 pinned
  against OpenSSL 3.5.x with the browser's own values recorded beside them.
- Dep: S7.2, S7.4. Accept: vectors committed and asserted; the `NotSupportedException` contract on 1.1.1w
  re-asserted. Tests → `h2profiles`.

---

## 10. Layer L8 — Facade, tooling, verification

### S8.1 — Compatibility facade (§5.8)
- Deliver: `SimpleHttp2GetTaskImpl` etc. with the constructor shape of `BL_TASKS_DECLARE_HTTP_TASK_*` and
  the same getters (`getResponse`, `getHttpStatus`, `getResponseHeaders`, `isSecureMode`,
  `addExpectedHttpStatuses`) over a process-default session.
- Dep: S6.1. Accept: an existing-style call works by changing only a type name. Tests → `h2client`.

### S8.2 — bl-tool command (optional, §11 P7)
- Deliver: an `http2`/`httpclient` command in `bl-tool` for manual runs and interop, mirroring
  `HttpRequest.h`. Dep: S6.1. Accept: manual GET/POST; not a unit-test dependency.

### S8.3 — Final verification against the design
- Deliver: fill in the traceability matrix of §13 (every `D1`-`D26` and every design section → slice →
  evidence); confirm the security checklist (design §7) and the non-goals (D25, design §12); run the
  whole suite on the matrix breadth the maintainer approves; confirm no `boost::beast` name outside
  S1.5's layers and no OpenSSL include in a `PreCompiled.h`.
- Dep: all. Accept: the matrix has no empty cell; §7 and §12 hold; grep invariants pass.

---

## 11. Probes folded into the plan

Every probe is owned by a slice and drives a decision at execution time; none is run to write this plan.

| Probe | Slice | Decides | Fallback if it fails |
|---|---|---|---|
| Token-stream equality of the handler macro | S0.1 | macro refactor is behavior-preserving | fix the macro until equal |
| `SSL_CTX` config dump equality, both flavors | S0.4 | context split is behavior-preserving | fix the split until equal |
| Beast headers present in the dist | S1.5 | Beast is usable at all | vendor Beast headers (header-only) |
| `basic_parser` sans-I/O coverage + object-size delta | S2.5 | keep Beast vs in-house backend (D15) | in-house backend behind the same facade |
| OpenSSL knob availability on 3.5.4 and 1.1.1w | S3.4 | which APIs are guarded to 3.5+ | guard/omit the unavailable knob, record deviation |
| ClientHello dump + JA3/JA4 + knob-table verification | S7.1 | each profile's grade and deviations | grade `Approximate`, list the gap |
| GREASE / ECH-GREASE / ALPS custom-ext viability | S7.1 | adopt a workaround (default off, D23) | leave off, record interop risk |
| `make utests-sizes` on `h2core` | S3.1 | split to `h2core2` or not | split per `src/utests/AGENTS.md` |

**The two flavor-dependent probes above cannot run as written until D2 is resolved.** S0.4's dump was
produced on 3.5.4 only, and S3.4's knob-availability probe faces the same wall: `BL_USE_OPENSSL_1X=1`
does not build, and no dist here carries 1.1.1w - see the D2 note in the design's section 0.1. Treat
"both flavors" in those two rows as owed evidence, and do not let a slice report them as done on one
flavor without saying so. S0.4's probe source is currently outside the repository; see the fourth
item under "L0 follow-ups" in §2.

---

## 12. Integration and gating order

Development runs in parallel worktrees; this is the **integration** DAG. "Earliest start" in the work
orders is for development and is generally earlier.

| Wave | Integrates | Gate |
|---|---|---|
| 0 | L0.S1-S4 (four commits) | **S0.5: whole suite, baseline-relative, both flavors for TLS.** Nothing G1-dependent merges before this passes. **Done 2026-09-18: passed at `gcc1520` debug, with the two limits recorded in §2 - `utf_baselib_jni` is compared on two of the four signals, and the 1.1.1w half is unmet. Wave 1 and beyond are unblocked.** |
| 0 (concurrent) | L1 all; L2 all (developed against L1) | focused modules per slice |
| 1 | S3.1 Session; S3.2 stranded plain; S3.6 ClientHello | focused modules |
| 1 (after S0.5) | S3.3 stranded TLS; S3.4 TLS contexts/floor; S3.5 tunnel | focused modules, both flavors for TLS |
| 2 | S4.1 establish; S4.2 h2 driver; S4.3 h1 driver; S4.4 test peer | `h2client`/`httpclient`, TSan on the drivers |
| 3 | S5.1 request task; S5.2 pool | `h2client`, TSan on the pool |
| 4 | S6.1 session | `httpclient` end-to-end |
| 5 | S7.1 spike (may begin at wave 2); S7.2 content; S7.3 apply; S7.4 report; S7.5 vectors | `h2profiles`, both flavors |
| 6 | S8.1 facade; S8.2 bl-tool; S8.3 verification | whole suite, matrix breadth per maintainer |

**Critical path:** L1 → L2 primitives → S3.1 Session → S4.1 → S4.2 → S5.2 → S6.1 → S7.3 → S7.4 → S7.5,
with the G1 gate (wave 0) as a parallel arm that must land before wave 1's TLS slices and wave 2. The
spike (S7.1) is off the critical path if started at wave 2.

**Two whole-suite gates only:** S0.5 (G1) and S8.3 (final). Every other slice is verified by focused
modules per the cadence in §0, so parallel work is not serialized behind the suite.

---

## 13. Traceability matrix (to be completed by S8.3)

**Decisions.** Every decision maps to at least one slice; S8.3 records the proving evidence.

| D | Slice(s) | D | Slice(s) |
|---|---|---|---|
| D1 devenv7-only | S1.7, all headers | D14 layout | all file placements |
| D2 OpenSSL flavors | S3.4, S7.2 | D15 Beast isolated+criteria | S1.5, S2.5 |
| D3 seam+OpenSSL | S1.6, S3.3-S3.6 | D16 in-house URI | S1.1 |
| D4 advertise/verify/refuse | S3.4 | D17 handler macro | S0.1 |
| D5 client layers | S2.5,S3.5,S2.7-S2.9 | D18 pre-handshake hook | S0.2 |
| D6 retry unprocessed | S3.1, S5.2 | D19 gated change-set | S0.5 |
| D7 generic placement | S1.1,S1.2,S1.6,S3.5 | D20 module names | S1.8 |
| D8 role-neutral+peer | S3.1, S4.4 | D21 coalescing off | S5.2 |
| D9 decoder seam only | S2.9 | D22 resumption advertise-only | S3.4 |
| D10 four graded profiles | S7.2 | D23 custom-ext off | S7.1 |
| D11 no push/h2c | S2.4, S3.1 | D24 URI strict | S1.1 |
| D12 docs location | done | D25 non-goals | S8.3 |
| D13 executor-bound | S3.2, S3.3 | D26 refactors gated | S0.3,S0.4,S0.5 |

**Design sections.** §3.1→S3.2/S3.3(+S1.6); §3.2→S0.1; §3.3→S3.4(+S0.4); §3.4→S1.1; §3.5→S1.2;
§3.6→S3.5(+S0.2); §3.7→S1.3; §3.8→S0.1-S0.5; §4.1→S2.1; §4.2→S2.2; §4.3→S2.4; §4.4→S2.3; §4.5/§4.6→S3.1;
§5.1→S4.1/S4.2; §5.2→S4.2/S5.2 (TSan); §5.3→S5.1; §5.4→S5.2; §5.5→S4.3/S2.5; §5.6→S6.1/S2.7-S2.9;
§5.7→S4.1/S5.1; §5.8→S6.1/S8.1; §6.1-6.8→S7.1-S7.5; §7 security→S8.3; §8 testing→S1.8+each slice;
§9 build→S1.7; §10→S0.*+additive slices; §12 curl parity→S8.3.

**Verification protocol.** Verification is itself a task an independent agent can run, per slice and
then for the whole. For each slice:

1. **Placement** - every deliverable exists at the path the design's §2.2 gives it and nowhere else;
   nothing generic lives under `http2/` or `httpclient/` (D7); no `boost::beast` outside S1.5's layers;
   no OpenSSL header in any `PreCompiled.h`; no `BL_DEVENV_VERSION` in a new header.
2. **Idiom** - the `T< E = void >` / `ObjectImpl` / `BL_DEFINE_STATIC_*` forms; `SAA_*` annotations;
   `BL_*` error and logging macros; no `boost::` name outside an import header.
3. **Behavior** - the work order's acceptance cases exist as tests in the named module and pass under
   the cadence of §0; a probe's recorded result exists where the work order names one.
4. **Design conformance** - read the design sections the work order names and list every point the
   implementation deviates from, with the reason; an unexplained deviation fails the slice.
5. **Decisions** - the `D` ids the slice maps to in the matrix above are honored; in particular D11
   (no push, no `h2c` upgrade), D21 (coalescing off), D22 (no resumption) and D23 (workarounds off) are
   easy to violate by "improving" the code and must be checked by reading it.

**Final acceptance (S8.3):** every cell above has recorded evidence; the two whole-suite gates passed;
the security checklist (§7) and non-goals (§12/D25) hold; the grep invariants of step 1 pass across
the tree; the D15 verdict, the S7.1 spike results and any `h2core` split are recorded back into the
design.
