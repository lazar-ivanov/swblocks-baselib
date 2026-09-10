# Plan: document-only reconciliation of the review commit and the test-enhancement commits against their plans

**Date:** 2026-09-08
**Status:** plan only. Not executed. How and when to execute is a separate decision.
**Baseline:** `lazari2` at `7b4988e` (49 commits after `cb431f0`).

## Context

Two bodies of work landed on `lazari2` from the Fable 5.1 whole-library C++ review:

1. Commit `cb431f0` ("stage 3: all remaining review issues") implements the 62-row review
   (`whole-library-cxx-review-fable51.md`) per `whole-library-cxx-review-fable51-remaining-plan.md` and
   records each row in `whole-library-cxx-review-fable51-decisions.md`.
2. The 49 commits `cb431f0..7b4988e` implement the 383-task `whole-library-cxx-test-enhancement-plan.md`;
   `whole-library-cxx-test-enhancement-implementation-report.md` and
   `whole-library-cxx-test-enhancement-outstanding-issues.md` describe what was done and what is open.

The goal is an independent, **document-only** reconciliation of both: did the commits do what the plans say,
are the reports accurate, which residual gaps exist that the reports do not name, and how should the
outstanding issues be addressed. Building, running tests and delegating to agents are out of scope. An earlier
attempt did all three and was stopped; its partial findings are preserved in the appendix so they are not
re-derived.

## Constraints (binding)

- Read-only against the repository: `cat`/`sed`/`grep`, `git show cb431f0 -- <file>`, `git diff`, `git log`.
  No `make`, no test binaries, no subagents, no edits to anything except the two output documents named
  below, nothing committed.
- Single session, sequential batches. After every batch the ledger is appended to a working file so a
  context summarization cannot lose results; the two documents are written at the end from the ledger.
- Depth (decided 2026-09-08): presence-and-wiring check for all 383 tasks; assertion-level comparison only
  for the 21 P0 tasks and the tasks the two reports flag; reuse the four package groups already compared
  (95 tasks, appendix); residuals for the review commit by cross-reference only, no fresh code hunting.

## Inputs

All documents are in this directory. Two derived inputs are rebuilt at the start of execution:

- Task index (id, spec start line, spec end line, priority, package, test file, title) for all 383 tasks:

  ```
  awk '/^#### T[0-9][0-9][0-9] - / { if (id!="") print id "\t" start "\t" NR-1 "\t" prio "\t" pkg "\t" file "\t" title;
       id=$2; start=NR; title=substr($0, index($0," - ")+3); prio=""; pkg=""; file=""; next }
       id!="" && /\*\*Priority\*\*/ { match($0,/P[0-3]/); prio=substr($0,RSTART,RLENGTH); match($0,/PKG-[A-Z-]+/); pkg=substr($0,RSTART,RLENGTH) }
       id!="" && file=="" && /\*\*Test file\*\*/ { s=$0; gsub(/.*\*\*Test file\*\*:? */,"",s); file=s }
       END { print id "\t" start "\t" NR "\t" prio "\t" pkg "\t" file "\t" title }' \
       whole-library-cxx-test-enhancement-plan.md > task-index.tsv
  ```

  Expected: 383 lines; P0 21, P1 214, P2 127, P3 21; seven tasks (T376-T381, T383) have no test-file line.
- Review heading lines in `whole-library-cxx-review-fable51.md` (summary table 28-110, detailed findings
  from 159): A-1 165 .. A-14 235, N-1 243 .. N-10 293, M-1 301 .. M-11 356, R-1 366 .. R-8 405, T-1 410 ..
  T-10 462, O-1 467 .. O-10 509, S-1 521 .. S-8 553, W-1 569 .. W-4 578.

Facts already established from the documents and `git` (no re-derivation needed):

- `git diff --check 18cc4b1 HEAD -- src` is clean; `cb431f0` touches 107 files (6802 insertions).
- 31 new test files, all included by their module mains; 311 new `UTF_AUTO_TEST_CASE`s; `notes.txt` gained
  10 lines in 3 modules (`utf_baselib`, `utf_baselib_cmdline`, `utf_baselib_data`); 41 `UTF_SKIP_UNLESS` sites.
- Lane build trees contain only clang-release (current), clang-debug (last built 2026-09-07 20:44, before 47 of
  the 49 commits) and gcc-debug; no gcc-release tree exists anywhere. The report's own text states the lane
  gate was clang release and the integration check gcc debug.
- The decisions file has a "Decided" paragraph for every implemented row; M-4 and W-1..W-4 are recorded in
  `notes/plans/issues/broker-outbound-peer-identity-deferral.md` and
  `notes/plans/issues/windows-only-residual-findings-deferral.md` (items 10-14) rather than in the decisions
  file; T-2 is recorded inside T-3's entry; M-8 inside N-2's.
- Only six `notes/plans/issues/` records are dated 2026-09-08; none of the ~80 defects in test-plan section
  13.10 has its own record beyond those six.
- The production files changed by the 49 commits (10 files, 116 lines): `OSImplUNIX.h`, `OSImplWindows.h`,
  `HmacSha256.h`, `HttpServerPorts.h`, `JavaBridge.h`, `JvmHelpers.h`, `Version.h`,
  `FanoutTasksObservable.h`, `bl-tool/commands/HttpRequest.h`, `bl-tool/commands/ProcessFilesUtils.h`.

## Part A: `cb431f0` against the remaining-work plan (62 rows)

Deliverable: `whole-library-cxx-review-fable51-reconciliation.md` (this directory).

- **A1. Row accounting.** One line per row: disposition per the plan (implement / record-only / already done
  / excluded), the entry that records it, the stage. Every row accounted for exactly once.
- **A2. Claim-to-diff check.** For every implemented row, take the decisions entry's concrete claims
  (functions, members, constants, files) and locate each in `git show cb431f0 -- <file>`. For the bundled
  rows (A-10, A-11, A-14, M-11, N-4, N-5, N-9, N-10, O-6, O-10, R-7, S-8, T-9, W-4) check each sub-item of
  the review text separately. Record: present / present with recorded deviation / dropped without rationale /
  not found. Reading is limited to the commit's hunks and the review text; no call-path tracing at HEAD.
- **A3. Test-presence check.** For every test the decisions entry names, `grep` the case name under
  `src/utests`; record file and gating (`UTF_SKIP_UNLESS`, `--is-client`/`--is-server`/`--path`, root,
  `--unique-id`, `_WIN32`). No reading of the case body beyond confirming it references the fixed symbol.
- **A4. Deviation review.** For each recorded deviation (A-4, A-5, A-8, N-3, N-8, R-6, R-7, S-4, S-6, S-8,
  T-1, O-7, N-2's unwritten cases, the missing OpenSSL 1.1.1w build) restate the rationale and judge it
  against the plan's seven binding decisions and the review's stated design intent; flag any that changes a
  binding decision or silently narrows a row.
- **A5. Residuals by cross-reference.** Map every row of test-plan section 13.10 and every item of the
  outstanding-issues document to a review row or to "unrelated". For each residual of a review fix state the
  row, what the source document says, whether a record exists, and the severity the source gives. Candidates
  already identified from 13.10: N-9 (gateway error path not redacted), N-5 (gateway passes framing headers
  to `Response`, whose rejection throws inside the success arm), N-7 (`size_t(-1)` sentinel is a legal
  `Content-Length`), T-9 (`dataRateParser` upper bound rounds up), O-10 (`isEnabled()` reads the line logger
  unlocked), O-9 (copied hook can outlive its guard), S-7 (`SecurityException` thrown across OpenSSL C
  frames), A-6 (`cancelTask()` never reached from `requestCancel()`), A-14 (negative checking interval
  accepted), T-1 (`relPath` box mutable after validation), T-7 (symlink entries can set a directory mtime to
  the epoch), N-2 (client protocol timer never armed; no PROCESS-stage deadline), S-8 (`validitySeconds`
  LLP64 divergence). No new code reading.
- **A6. Records and release notes.** Confirm the stage-11 records match the plan's list; note the
  decisions-file gaps for M-4 / W-1..W-4; check release-notes section 11 covers every "downstream / release
  note" mention in the decisions entries (verify at least O-7 constructor signature, S-5 new helper, M-2
  checked setters, R-6 cap) and that nothing else with a contract change is missing.

Output: summary verdict; 62-row matrix (row, sub-item, status, diff evidence, test, note); residual list with
source references; claims not matching the diff; record gaps.

## Part B: the 49 commits against the test plan (383 tasks)

Deliverable: `whole-library-cxx-test-enhancement-reconciliation.md` (this directory).

- **B1. Presence and wiring, all 383.** Per task: extract case / fixture names from the spec
  (`sed -n '<start>,<end>p'` on the index range; `UTF_AUTO_TEST_CASE(` and backticked identifiers); `grep`
  under `src/utests`; record file, module-main inclusion, gating, `_WIN32` guards. Mark FOUND /
  FOUND-ELSEWHERE / NOT FOUND. Batches of about 40 tasks by package; ledger appended after each.
- **B2. Assertion-level comparison, P0 and flagged set.** The 13 P0 tasks not yet covered (T001, T002,
  T016-T025, T031); the 13.1 red tasks (T016, T017, T031, T087, T272, T315); the tasks the reports flag
  (T006, T010, T012, T028, T082, T378 in the outstanding-issues erratum; T002, T061, T121, T180, T203, T225,
  T237, T294, T363 in the report's section 6). Compare the spec's assertion list with the case body;
  classify FULL / PARTIAL / DEVIATED-REPORTED / DEVIATED-UNREPORTED. For the red tasks confirm the corrected
  behaviour is asserted and that `git diff cb431f0..HEAD -- src/include src/local` matches the spec's stated
  fix.
- **B3. Fold in the appendix (95 tasks).** Re-check with one `grep`/`sed` any appendix claim the final
  document relies on.
- **B4. Completion-criteria audit (plan section 14), from documents and `git` only.** Toolchain/variant
  matrix (two of four Linux combinations not run; evidence: lane trees, report text); the six frozen shared
  headers modified in P1/P2 commits (`git log cb431f0..HEAD -- src/utests/include/utests/baselib/<file>`);
  `notes.txt` wiring; skip-list baseline (36 -> 35, T138); T005 leak check and its negative control; 13.1
  dispositions; 13.9 decisions answered or not (items 3, 4, 5, 7, 9, 10, 13 largely, 14, 15 have no recorded
  answer); 13.10 filed or not; json-spirit arm (14.3) never mentioned in either report; Windows-only tasks
  never compiled here (13.4.1: T054 half, T058, T060, T061, T062, T253, T357 plus any `_WIN32` block found in
  B1); 14.6 exclusions respected.
- **B5. Report accuracy.** Check each concrete claim of the two report documents against the tree: per-module
  case counts, "35 skipped", the 12 production fixes against the 10-file diff, the six plan errata plus the
  errata already found (T041 callback precondition, T193 path literal, T334 X.509 v1 root, T376 placeholder
  literal, T181 non-existent guard), the "count guard" attributed to T029, the 755/756 case totals.
- **B6. Remediation plan for the outstanding issues** (last section of the Part B document). One entry each
  with options, recommended option, blast radius and the tests that must flip:
  - D-01 `RetryableWrapperTask` cancel: a wrapper-level cancel flag set in `ForwarderTaskBaseT::requestCancel`
    (`tasks/TaskBase.h:380`) and checked in `RetryableWrapperTaskT::continuationTask()` (`:2040+`), with an
    `operation_aborted` result so a cancelled operation does not report success; alternative:
    `SimpleTimerTaskT::run()` throwing on cancel (library-wide). T203 must be flipped.
  - Windows session: extend `notes/plans/issues/whole-library-windows-residuals-instructions.md` with the two
    self-containment fixes (`ComUtils.h`, `WindowsShellShortcut.h`), the two inspection-only fixes, the seven
    13.4.1 tasks and the S-5 DACL question.
  - Server-error fidelity (`data/eh/ServerErrorHelpers.h`): arms for the five unmapped types; rehydrate an
    unknown category name (e.g. `"OpenSSL"`) as `UnexpectedException` carrying the name and code instead of
    throwing `ArgumentException` (`:275-301`); decide per field for the eight dropped `errinfo_*`
    (`original_type`, `original_thread_name`, `original_stack_trace`, `hint`, `service_status`,
    `service_status_category`, `service_status_message`, `error_uuid`). T130, T292, T381 must flip.
  - Redaction: `exceptionMessage` policy in `getRedactedServerErrorAsJson` (redact unless user-friendly);
    escaper for non-JSON content types in `security/AuthorizationServiceRest.h:436-446` (percent-encode for
    form-urlencoded, XML-escape for xml, reject otherwise); the gateway's unredacted path (13.9 item 5,
    `TestRestDefault.h` line ~495 pins it).
  - `BoolSwitchOrMultiStringOption` (`cmdline/Option.h:329`, `SwitchImpl::decorateSemantic` applies
    `zero_tokens()` over `multitoken()`): withdraw the typedef, or implement with `implicit_value` and the
    documented `--flag=a` syntax. T364 must flip.
  - Plan-document errata: prepend an erratum section to the test plan listing the six recorded and the five
    additional errors plus the stale-line-number note, rather than editing specs in place.
  - Section-7 pinned behaviours: per-item keep / fix decision, each with the pinning case named.
  - New gaps from B4: run the two missing toolchain/variant combinations once (a separate, explicitly
    scheduled step), add the `notes.txt` lines, file the 13.10 items as records, answer the open 13.9
    decisions, run the json-spirit arm once.

Output: summary verdict; 383-row ledger (task, priority, status, case, file, gated, reported-where);
unreported gaps; completion-criteria audit; report-accuracy findings; remediation plan.

## Execution estimate

| Step | Reading | Batches |
|---|---|---|
| A1-A6 | commit hunks per row (~6.8k diff lines) + decisions entries + 13.10 cross-reference | 3-4 |
| B1 | 383 spec headers (~10 lines each) + greps | 4-5 |
| B2 | ~30 specs (~60 lines each) + ~30 case bodies | 3-4 |
| B3-B6 | documents and greps listed above; the production sites for B6 are named above | 2 |

About 12-15 sequential batches in one session, no parallelism, no machine resources beyond reading.

## Verification (document-only)

- Every review row appears exactly once in the Part A matrix; every task id T001-T383 appears exactly once in
  the Part B ledger (arithmetic check on the files).
- Every "not found" or "unreported" claim in either document carries the `grep`/`git` command that
  reproduces it.
- Both documents cite paths and line numbers at HEAD `7b4988e`; nothing from the appendix is asserted without
  a re-check.
- `git status` shows only the new untracked documents in this directory.

## Out of scope

Building, running any binary, TSan or OpenSSL 1.1.1w builds, Windows verification, subagents, fixing any
defect, editing the plan or report documents in place, commits.

---

## Appendix: findings already available from the interrupted session (2026-09-08)

Produced by reading the plan specs and the test sources at `7b4988e`; nothing was built or run. To be
re-checked with a single `grep`/`sed` before any of it is relied on.

### PKG-CORE-DEFAULT part 1 (T013-T015, T034-T055): all 25 FULL

- T041: passes `cbOnErrorMinusOne` where the spec omitted it (`Utils.h` `BL_ASSERT` aborts debug otherwise);
  in the report's section 8, absent from the outstanding-issues erratum table.
- T055: the "exception or failed stream" outcome is logged, not asserted (non-discriminating disjunction).
- `src/utests/utf_baselib/notes.txt` has no `--run_test=` line for any of the 15 new cases.
- `TestBaselibDefault5.h:649` `BaseLib_OSCreateProcessArgvQuotingWindowsTests` is hard-disabled
  (`productionArgvQuotingIsFixed = false`) and documents the W-1 defect; in neither report. T054's Windows arm
  never compiled here. `BaseLib_NamedMutexTests` still leaks its fixed-name SysV semaphore (pre-existing).

### PKG-CORE-DEFAULT part 3 (T242-T257, T352-T359): 21 FULL, T248 PARTIAL, T253 DEVIATED-UNREPORTED

- T253 (`TestBaselibDefault.h:10444-10495`): the HKLM wrong-hive assertion was dropped (comment only), so the
  `OSImplWindows.h:3590` hive-text defect is neither fixed nor red-covered; the 1023-character limit is pinned
  as a throw without the owner decision the spec required; gated by a bare `if( ! onWindows() ) return;`
  (reports as passed on Linux, not `UTF_SKIP_UNLESS`).
- T248 (`TestBaselibDefault.h:6244`): Windows LFN assertion weakened to a case-insensitive suffix match.
- T257 (`TestBaselibDefault9.h:673-690`): pooled-copy variant lacks the `capacity() == 64` post-condition.
- `notes.txt`: 10 of the 11 new cases have no line.
- Observed: `ThreadPool.h:141-145` `disposeGlobalThreadPool` disposes twice per call (idempotent; pinned by
  T255, not in the outstanding-issues section 7); T255's fixture expands `BL_QITBL_DECLARE_DISPOSABLE`
  (13.9 item 14 keep-or-delete); T242's diversity gate keys on `hardware_concurrency()` while the pool size
  comes from `--threads-count` (fails with `--threads-count 1` on a multi-core host).

### PKG-MESSAGING (T026-T028, T166-T181, T321-T326, T376): 23 FULL, T028/T180 DEVIATED-REPORTED, T170 PARTIAL

- T170: spec step 3 (`isConnected()` / `! autoBlockDispatching()` on `backend1`/`backend2` in
  `TestMessagingUtils.h:1154-1318` `forwardingBackendTests`) never landed; the forwarding backend's delegating
  `isConnected()` is unasserted anywhere. Unreported.
- T179 (`TestMessagingDefault.h:5641-5697`): second sub-block cancels before `push_back` instead of with an
  outbound send pending; justified in a comment, unreported.
- Plan errata unreported: T376's placeholder literal is the `dm::Payload` operator's, not the
  `AsyncRpcPayload` one actually streamed; T181's "present token on the acknowledgment path" guard does not
  exist in `BrokerBackendProcessing.h`.
- The six gated cases: `BrokerFacadeTests`, `ProxyBrokerFacadeTests` (`--is-server`);
  `ProxyBrokerClientBasicTests`, `BrokerClientTests`, `IO_MessagingPerfTests`,
  `IO_ConnectionEstablisherBasicTests` (`--is-client`).
- T376 pushes the global level to trace and swaps the process line logger for a full broker round trip
  (RAII-restored).

### PKG-SECURITY (T029, T030, T189-T198, T330-T335, T368, T381): 19 FULL, T334 DEVIATED-UNREPORTED

- T334 (`TestCryptoUtils.h:936-944`): the spec's per-root `X509_get_version == 2` is unattainable (the first
  bundled root, VeriSign Class 3 PCA, is X.509 v1); relaxed to `0 || 2` in-source only. A seventh plan
  defect.
- T193 (`TestAuthorizationServiceRest.h:623-631`): the spec's path `/authorize?t=opaque-token-value` is
  unattainable (`uriEncode` encodes `-`); corrected in the implementation, unrecorded.
- `src/utests/utf_baselib_security/notes.txt`: no line for any of the 19 new cases.
- Outstanding-issues section 4.2 says T029 has "a count guard": none exists and the spec asks for none.
- Section 7 of the outstanding-issues document omits three security pins: T331 (encode-of-zero throws), T335
  (`tryVerify` throws `ArgumentException` on malformed base64url), T368 (keep-first reason per endpoint).
- The T011 conversion turned the former `UTF_FAIL` for `--is-client` without `--path`/`--password`
  (`AuthorizationCacheRestImplManualInvoke`) into a skip; a misconfigured manual run now reads as skipped.
- No gated and no `_WIN32` cases in the group; T196/T334 register roots permanently (documented).

### Cross-cutting (already established)

- Two of the four mandatory Linux toolchain/variant combinations (clang debug on the final tree, gcc release
  at all) were never run on this work.
- `notes.txt` wiring skipped for about 300 of the 311 new cases.
- None of the section 13.10 production defects was filed as a record; several 13.9 decisions have no recorded
  answer; the json-spirit arm is not mentioned in either report.
- Report-document detail: the implementation report was revised on 2026-09-08 to fill in the "cases before"
  column (471 -> 756 over 18 modules; the checkpoint table still reads 755 for the 17-module aggregate).
