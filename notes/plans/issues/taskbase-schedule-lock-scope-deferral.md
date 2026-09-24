# `TaskBase::scheduleNothrow( )` holds the task lock across `scheduleTask( )` — deferral

**Date:** 2026-09-23. **Status:** deferred, deliberately and with the question left **open**.
Requested by the maintainer when choosing the driver-local fix (A4) for the HTTP/1.1 driver:
*"write a deferral with the full detailed context to investigate later if fixing this in the core
would be appropriate and if this is a real design issue to be addressed in the library."*

This record exists so that question can be answered later **without re-deriving anything**. It is
not a proposal, and nothing here is scheduled.

**What was decided, and what was not.** A4 removes the HTTP/1.1 driver's exposure. It removes
nothing else. Whether the core itself should change is **not decided here, in either direction** —
and §5 is the honest case that it may well be correct as written.

---

## 1. The defect, as a chain

Four links. Each was re-derived at the source on `lazari2` @ `a723cee`; line numbers drift, the
function names are the anchors.

| # | Link | Where | Verified by |
|---|---|---|---|
| 1 | `TaskBase::scheduleNothrow( )` takes `BL_MUTEX_GUARD( m_lock )` (`:1167`) and calls `scheduleTask( eq )` (`:1208`) **under it** | `tasks/TaskBase.h:1160-1208` | author, at source |
| 2 | h1's `scheduleTask( )` override calls `scheduleRead( )` **directly** — not posted, and with no operation begun before it | `httpclient/Http1ConnectionTask.h:1584`, `:1605` | author, at source |
| 3 | `scheduleRead( )`'s `catch` calls `base_type::onOperationCompleted( std::current_exception(), false )` **inline** | `Http1ConnectionTask.h:869-871` | author, at source |
| 4 | `onOperationCompleted( )` is where the terminal `notifyReady( )` is reached, which `MultiOperationTask.h:62-67` says must run **outside** the task lock | `tasks/MultiOperationTask.h:62-67` | author, at source |

On this path the pending count reaches **zero** — nothing else is outstanding while the very first
read is being armed — so `notifyReady( )` runs with `m_lock` already held by link 1.

### 1.1 What it costs is a deadlock, not a rule violation

This is the part worth carrying forward, because "violates a documented invariant" understates it.

- `m_lock` is declared `mutable os::mutex` (`TaskBase.h:436`, and again at `:764`).
- `os::mutex` is **`std::mutex`** — `core/detail/OSBoostImports.h:95` reads `using std::mutex;`
  unconditionally — and `std::mutex` is **non-recursive**; re-locking it on the owning thread is
  undefined behaviour, and `std::recursive_mutex` is the separate type for when that is wanted.
  *Corrected 2026-09-23. This record first said `boost::mutex`, which was wrong: the author read the
  `lock_guard< mutex >` typedef and the `using boost::shared_mutex;` lines near it and inferred the
  import from the neighbourhood instead of reading it. A4's red backtrace settles it from the other
  direction — frame 4 is `std::__1::mutex::lock( )`. The conclusion is unchanged, because both types
  are non-recursive; the premise was not.*
- `notifyReadyImpl( )` takes `BL_MUTEX_GUARD( m_lock )` itself (`TaskBase.h:~563`).

So re-entering it on the same thread is undefined behaviour and in practice **a self-deadlock on the
thread that scheduled the task**. The 2026-09-23 review adds that this happens under the execution
queue's lock as well (`ExecutionQueueImpl.h:649`), which widens what is stalled from one task to the
queue. *That second point is the review's reading and was not independently re-derived here.*

**Reachability.** It needs `async_read_some( )` to throw out of its initiator, which is rare. No run
in this repository has produced it. It is a latent hang, not an observed one.

## 2. What A4 does, and what it leaves

**A4** (see [`driver-read-write-arms-design.md`](driver-read-write-arms-design.md) §4, §10.2): h1
propagates the throw on the schedule path — where `scheduleNothrow( )`'s own `catch` (`:1210`)
already completes it off every lock — and keeps its inline `catch` for the in-handler re-arm, where
the count cannot reach zero. Flag-free, one driver.

**What it leaves.** Every other task in the library that arms an operation synchronously from its
`scheduleTask( )` override keeps whatever exposure it has. A4 is a fix for one driver, not for the
shape. Establishing how many such tasks exist, and whether any of them can drive the count to zero
inside `scheduleTask( )` the way h1 can, is **the first thing an investigation should do** — and it
was deliberately not done here, because A4 does not depend on the answer.

## 3. Why the obvious core fix was not taken

The obvious fix is to move `scheduleTask( eq )` out of the guard. The 2026-09-23 review found an
unexamined cost, and it is the reason this is deferred rather than scheduled:

**The guard is load-bearing for cancellation.** It serialises the `m_state = Running` store
(`TaskBase.h:1194`) against `requestCancel( )` / `requestCancelInternal( )` (`:1026-1058`). Move
`scheduleTask( )` out and a cancel arriving in the new window could land **after** the state says
`Running` but **before** the first operation is armed — and be lost, in **every task whose cancel
path is a synchronous `socket.cancel( )`**, which is most of them. That would be a real regression
across the whole library, introduced to close a latent hang in one driver.

*This objection is the review's, derived from reading. It has not been demonstrated with a test, and
confirming or refuting it is itself part of the investigation.*

## 4. What would have to be established before taking the core fix

In order. Each is a reason to stop if it fails.

1. ~~**The blast radius.**~~ **CLASSIFIED 2026-09-23 by A4's review, and the answer is h1 alone.**
   All 16 `scheduleTask( )` overrides in `src/include/baselib` were read and every `catch` in those
   files mapped to its enclosing function. **None shares h1's shape** — h1 was the only
   inline-catch-to-completion. `AsyncExecutorImpl.h:560` is the only other that catches at all and it
   completes by a *post*; `HttpClientRequestTask.h:1826` posts inside `BL_NOEXCEPT`, so a throw there
   terminates the process (a harsher policy, noted rather than judged); the other twelve arm with no
   catch and propagate by construction; `SimpleCompletedTask` is never scheduled.

   **By §4.1's own terms this closes the lock-scope question as *not a library problem*** — the core
   fix would buy nothing A4 has not already bought. **Two qualifications keep it from closing
   outright**, and they are the reason this record stays open rather than being deleted:
   - the contract is stated only **negatively** (`TaskBase.h:880-883`: an override *"should never
     attempt to execute synchronously and call notifyReady( )"* — precisely what h1's catch did), and
     never positively, so the documentation defect of §5.1 is exactly two sentences: one at `:890`
     and one at `MultiOperationTask.h:62-67`;
   - the off-lock route carries a residual of its own, §6 below.
2. **The cancel window is real.** Demonstrate — with a test, not by reading — that a cancel can be
   lost in the window the core fix would open. If it cannot, §3's objection falls and the core fix
   becomes cheap.
3. **A shape that keeps both.** If 1 and 2 both hold, the fix is not "move the call" but something
   that keeps the cancel serialised while letting the terminal path out from under the lock — for
   instance publishing `Running` under the guard and arming outside it, with the cancel re-checked
   after arming. Not designed here.
4. **A whole-suite gate.** Any change to `TaskBase`'s lock scope is a core change under the
   project's rule that core code paths land as their own change-set gated on the entire suite —
   messaging, blobtransfer, io and everything else go through this function.

## 5. The case that the core is correct as written — **substantially weakened 2026-09-23**

Stated deliberately, so a later reader does not assume the defect is the core's:

`scheduleNothrow( )` **already provides an off-lock completion route for exactly this situation** —
its own `catch` at `:1210` completes a throwing `scheduleTask( )` outside every lock. On that reading
the contract is *"throw out of `scheduleTask( )` and the base will complete you correctly"*, h1
simply does not use it, and **#12 is a driver bug rather than a library one.**

### 5.1 Two of this section's premises are false — A4's finding, 2026-09-23

*Reported by A4's implementing lane, which had to read all of this to build the fix. Under review at
the time of writing; recorded now because it changes what the deferred question is about.*

- **"The HTTP/2 driver documents that sibling route", implying h2 uses it, is not supported.**
  `Http2ConnectionTask.h` has **no `scheduleTask( )` override at all**. h2 never arms anything from
  `scheduleTask( )`, so it cannot be using the off-lock route. The reason is structural rather than
  stylistic: `Http2ConnectionTaskT` derives from an establisher which connects, handshakes and
  negotiates, arming everything from handlers; `Http1ConnectionTaskT` derives from
  `MultiOperationTaskT` and is handed an already-connected stream, so it **must** arm its own first
  read.
- **The h2 comment cited documents *when the route fires*, not a contract.** Its words — *"The one
  off-strand route … is taken only before the handshake, where all five timers are null"* — are an
  observation inside an argument about which timers are armed.

**So §5's own test resolves against it.** The question it posed was whether the contract is stated
anywhere or only implied by the `catch` existing: **only implied.** `scheduleNothrow( )`'s catch
comment is descriptive, and `MultiOperationTask.h:62-67` states the rule in a form that *does not
contemplate the initiator catches at all* — they reach `onOperationCompleted( )` directly, not from a
handler epilog. The codebase does spell such contracts out when it means them:
`ExecutionQueueImpl::padExecutingQueueNothrow( )` warns in capitals that a task must never call back
into the queue under the task lock.

**Revised position: A4 is still the driver being fixed to use a facility the base offers — but
nobody was using it deliberately, and nothing says it is there to be used. That is a documentation
defect in the library at minimum**, and `:62-67`'s rule should name the initiator-catch call sites
whatever else is decided.

## 6. The residual in the off-lock route — found 2026-09-23, and it is the reason §4.1 does not close

**The base's off-lock route completes a task without waiting for operations armed before the throw.**
Stated generally because it is not h1's: `HttpServer.h:158-170` arms a timer then a read, and the
`tasks2` probe arms N timers.

Concretely in h1: `scheduleTask( )` calls `postToStreamExecutor( )` **directly, under the task lock**,
immediately after arming the read — **on both of its branches**, `onStartRequest` when a request is
pending and `chkArmIdleTimer` when none is. If *that* call throws, the base's catch completes the
task while the read armed a moment earlier is still pending.

**The route, traced properly — and the first write-up of it here was wrong about the mechanism.**
*Corrected 2026-09-23 by A4's lane, which followed it to the end rather than restating the
orchestrator's one-line framing.*

The base's catch posts `notifyReadyImpl( false /* allowFinishContinuations */, … )`, and
`scheduleTaskFinishContinuation( )` sits **inside** that gate — so it does **not** run on the catch's
own route. What runs outside the gate is `onTaskStoppedNothrow( )`, and the stream policy's override
(`TcpBaseTasks.h:628-644`) calls `shutdownSocket( )`. **That shutdown wakes the pending read**, whose
handler reaches `END_MULTIOP` → `onOperationCompleted( )` → count 1→0 → `takeTerminalNoLock( )` →
`notifyReady( )` → a **second** `notifyReadyImpl( )` — and *that* one carries
`allowFinishContinuations = true`.

So the TLS exposure is real but arrives **one hop later**, from the late read handler's terminal
rather than from the catch, gated only by
`m_wasSocketShutdownForcefully || m_scheduledForShutdown || ! m_isCloseStreamOnTaskFinish ||
! isShutdownNeeded( )` (`TcpSslBaseTasks.h:475-479`). **Still traced by nobody past that point.**
`onTaskStoppedNothrow( )` runs **twice**, both times before `m_notifyCalled` is consulted — verbatim
the failure `MultiOperationTask.h:37-42` names, with `:46-49` naming the TLS shutdown as what the
accounting is supposed to keep from starting while an operation is pending.

**And the distinction that keeps A4 out of this entirely.** When `armRead( )` throws,
`beginOperation( )` has run but `async_read_some( )` **never started** — the count is one and
**nothing is pending**, so no handler is woken and there is no second `notifyReadyImpl( )`. A4's
route completes a task with a **phantom** in the count; this route completes one with a **live
operation in flight**. Only the second is the defect, and A4 neither creates nor widens it.

Pre-existing, untouched by A4, and absent from the design.

### 6.1 The h2 establishment route is the same defect, and is folded in here — **closed 2026-09-24**

Carried in from the owed list as item **13d**, which is now **closed** rather than left pending: it is
an instance of this section's general statement, its fix is this section's to make, and it changes
nothing an L0–L6 caller can observe.

**The route.** A throw out of `onProtocolNegotiated( )` after the first `beginOperation( )` reaches
the establisher's catch with operations pending and calls `notifyReady( eptr )` outside the lock.
`notifyReadyImpl( )` then runs `scheduleTaskFinishContinuation( )` and `onTaskStoppedNothrow( )`,
which cancels the timers and, through the policy, shuts the socket. The pending write or read fails,
its handler finds `isClosing( )` false — the accounting never saw an error — so the code goes through
`CHK_EC( )` into `END_MULTIOP( )`, becomes the first error, initiates the close and, at count zero,
takes the terminal: a **second** `notifyReadyImpl( )`, stopped at `m_notifyCalled`.

**Its entrances are several, not one** — every throw in `onProtocolNegotiated( )` after the write is
begun: the `async_write( )` itself with the settings timer pending, `scheduleRead( )`,
`chkArmKeepAlive( )`, `chkArmIdleTimer( )`, and anything in that stretch that allocates. **That is
why no initiator-level guard closes it**, and why A3's uniform shape was right to leave it: an inline
catch at one initiator would close one entrance and look complete.

**Why closing it here costs nothing to give up.** The consequence is bounded by three properties,
each verified at the source: the establisher's retry is gated on
`! hasHandshakeCompletedSuccessfully( )` and does not fire; the TLS continuation's second call sees
`m_scheduledForShutdown` and rethrows the original; and h2's `onTaskStoppedNothrow( )` is idempotent
by construction — monotone publish, `closeSubmissions( )` on a closed mailbox, an empty stream table.
**So it is redundant work and possibly a second log line. The pool sees one completion.** Derived by
reading, never observed.

**If this section is ever picked up, this is where it is fixed** — at the boundary of
`onProtocolNegotiated( )` rather than at an initiator: catch there, and when operations are pending
record the error and initiate the close *without completing anything*, returning true so the
establisher's epilog is not reached and the pending handlers take the one terminal; rethrow when
nothing is pending. That needs the additive *"fail while operations are pending"* primitive, which is
the same accounting change §4's step 3 would need.

## 7. Two adjacent facts, recorded so they are not rediscovered

**The in-handler call site is safe, and not for an obvious reason.** h1's *other* call of
`scheduleRead( )` — from inside `onReadCompleted( )`'s handler body — reaches the same inline
`onOperationCompleted( )` under the task lock, because a handler body runs inside
`BL_TASKS_HANDLER_BEGIN( )`'s guard. It is nevertheless not this defect: the completing operation
is still outstanding until `END_MULTIOP` runs, so the count cannot reach zero and `notifyReady( )`
is not reached. **Latent by an accounting property, not by luck** — and that property is what A4
preserves by keeping the catch there.

**`initiateClose( )` *is* reached inline under the task lock**, at every in-handler catch, per the
2026-09-23 review — benign today only because neither driver's `initiateClose( )` takes the task
lock. That is a second implicit invariant nothing enforces, and a driver that took the lock there
would deadlock the same way. *Review's finding; recorded, not re-derived.*

## 7. What is measured, and what is not

**Measured, 2026-09-23 — this section is superseded in its most important part.** A4 built the
throw, and the defect is **no longer derived**. A debugger backtrace of the deadlock
(`logs/lane1-a4/red-run2.bt`; gdb had to be the *parent*, since `ptrace_scope = 1` refuses a sibling)
shows the whole chain on one thread:

```
#6  notifyReadyImpl        TaskBase.h:564            <- blocked on m_lock
#8  applyDecision          MultiOperationTask.h:194    close=true, terminal=true
#9  onOperationCompleted   MultiOperationTask.h:384
#10 scheduleRead           Http1ConnectionTask.h:973
#11 scheduleTask           Http1ConnectionTask.h:2038
#12 scheduleNothrow        TaskBase.h:1208           <- holds m_lock
#14 padExecutingQueueNothrow  ExecutionQueueImpl.h:694  <- holds the QUEUE lock
```

Three things §1 and §7 could only infer are now facts: **`terminal=true` in frame 8** proves the
count really does reach zero; **frame 11** proves the throw is on the `scheduleTask( )` path and not
a later re-arm; and **frame 14** confirms §7's execution-queue-lock claim, which was a reviewer's
reading nobody had re-derived. The queue lock is held as well as the task lock.

**Still not measured:** the cancel window of §3, and whether any of the other 16 overrides can reach
this.

**Verified by reading, at the source, by the author:** links 1–4 of §1; `m_lock`'s type and
non-recursiveness; `notifyReadyImpl( )`'s re-acquisition.

**From the 2026-09-23 review, not independently re-derived:** the execution-queue lock at
`ExecutionQueueImpl.h:649`; `takeTerminalNoLock( )` being true at count zero; the cancel-serialisation
objection of §3; h2's documented sibling route; the `initiateClose( )` finding of §6.

**Not established at all:** the blast radius (§4.1), whether the cancel window is real (§4.2), and
whether the implied contract of §5 is written down anywhere.
