# `utf_baselib_blobtransfer` leaked objects at teardown: reference cycle in the fault-injection fixture

**Found:** 2026-09-07, while verifying stage 10 of the whole-library C++ review
(`notes/reviews/major/update_2026/whole-library-cxx-review-fable51.md`). It is **out of scope** for
that review — no finding covers it — so it was first recorded here rather than fixed, per the
review session's rule for incidental discoveries.

**Status:** **fixed 2026-09-07** (`src/utests/include/utests/baselib/TestBlobTransferUtils.h`),
diagnosed and fixed on the user's instruction after the record was written. Test-support code only;
no shipped code changed. It was never a regression and never a test failure.

---

## What happened

Every run of `utf_baselib_blobtransfer` passed all 15 test cases ("No errors detected", exit 0) and
then logs, from `DefaultUtfConfigT<>::~DefaultUtfConfigT()`
(`src/utests/include/utests/baselib/UtfMain.h:345`):

```
ERROR: DefaultUtfConfig()::~DefaultUtfConfig() failed with the following exception:
  Dynamic exception type: bl::UnexpectedException
  std::exception::what: Objects leaked!
ERROR: Outstanding object references are 357
```

The count was not stable — 352, 354, 356, 357, 358 and 359 were all observed. That instability
initially suggested a teardown race rather than a structural leak; **it was in fact a structural
reference cycle**, and the count varied only because the number of connection tasks caught in it
varies with the run. The destructor swallows the exception (it is a destructor), so the process
still exits 0 and no harness treats the run as failed. **That is the reason this went unnoticed:**
a green exit code hides it.

## Not a regression — shown on the untouched baseline

| Tree | Count |
|---|---|
| Working tree with all of stages 3-10 applied, clang2010 debug | 357 |
| Same, clang2010 release | 357 |
| Same, gcc1520 debug / release | 354 / 359 |
| The three iterator-owner hunks of T-1 reverted (`FilesystemMetadataInMemoryImpl.h`), clang2010 debug | 359 |
| **A throwaway `git worktree` of the untouched `HEAD` (`2480954`), clang2010 debug** | **358** |

The last row is the decisive one: the leak is present with none of this work applied. The
fourth row additionally rules out the specific suspect — T-1 gave the UUID iterators a strong
reference to the metadata store (`UuidIteratorImpl`'s pre-existing optional `impl` owner
parameter), which was the obvious candidate for a new reference cycle; removing it does not change
the count.

## How it was diagnosed

Two steps, both in the throwaway `HEAD` worktree:

1. **Bisect by test case.** Running each of the 15 cases alone showed that exactly five leak, and
   that their counts sum to the suite total:

   | Case | Outstanding refs |
   |---|---|
   | `BlobTransfer_FilesPackagerInMemoryReauthAfterDropOnSaveTests` | 84 |
   | `BlobTransfer_FilesPackagerInMemoryReauthAfterDropOnLoadTests` | 84 |
   | `BlobTransfer_FilesPackagerInMemoryReauthAfterDropOnRemoveTests` | 83 |
   | `BlobTransfer_FilesPackagerInMemoryUploadFailureKeepsMetadataMutableTests` | 40 |
   | `BlobTransfer_FilesPackagerInMemoryDropWithSessionsFailsUploadTests` | 63 |
   | **total** | **354** |

   Those five are exactly the cases which request **server-side fault injection**; the other ten
   leak nothing.

2. **Name the objects.** `om::ObjectImpl` was given a temporary per-type live-object registry (a
   `typeid( T ).name()` counter incremented in a member's constructor and decremented in its
   destructor) and `UtfMain.h` was made to dump the non-zero entries next to the count. The
   smallest case reported the **whole blob server object graph** still alive: 16 ×
   `TcpBlockTransferServerConnection`, plus `FaultInjectingBlobServer`,
   `FaultInjectingDataChunkStorage`, `AsyncDataChunkStorage`, `BlockTransferServerState`,
   3 × `ExecutionQueueImpl`, 4 × `ProxyImpl`, `DataChunkStorageFilesystemMultiFiles`, the loader,
   the factory and the pools. That instrumentation was reverted; it is not in the tree.

## Root cause

A reference cycle in the test fixture, `TestBlobTransferUtils.h`
(`executeTransferTestsWithFaultsInternal`):

```
acceptor ──owns──> AsyncDataChunkStorage ──owns──> faultStorage
   ^                                                    │
   └──── m_dropCallback (strong ObjPtrCopyable) ─────────┘
```

`faultStorage -> dropCallback( ... )` binds
`om::ObjPtrCopyable< FaultInjectingBlobServer >::acquireRef( acceptor.get() )` so the fault
injector can drop every client connection on demand — a **strong** back-reference to the acceptor
which owns, transitively, the storage that holds the callback. Nothing ever broke it:
`FaultInjectingDataChunkStorageT::dispose()` is a deliberate no-op ("the wrapped storage is owned
and disposed by the caller") and `faultStorage` is not wrapped in `om::lockDisposable`. So the
acceptor outlived the test, and with it `FaultInjectingBlobServerT::m_connections`, which holds a
strong `om::ObjPtr< Task >` to every connection the server ever created.

## The fix

A `BL_SCOPE_EXIT` immediately after the callback is installed, clearing it when the fixture
returns, so the cycle is broken deterministically on both the normal and the throwing path:

```cpp
BL_SCOPE_EXIT(
    {
        faultStorage -> dropCallback( cpp::void_callback_t() );
    }
    );
```

Alternatives considered and rejected: clearing `m_dropCallback` in
`FaultInjectingDataChunkStorageT::dispose()` would also require putting `faultStorage` under
`om::lockDisposable` and would reverse that method's documented no-op contract; holding the
acceptor by a raw pointer instead would be unsafe, because the cycle is precisely what was keeping
the acceptor alive.

## Verification

All 15 cases pass and no leak is reported, on `clang2010` and `gcc1520`, debug and release. The
five cases above were also confirmed individually: `Outstanding object references` is zero where it
was 40-84 before.

## Conditions to revisit

- Any new `om::ObjPtr`/`ObjPtrCopyable` captured into a callback that is stored on an object the
  captured object owns, transitively, is the same defect. The `utf_baselib_blobtransfer` leak
  report is the only place in the suite where such a cycle currently becomes visible, and only
  because that module's fixture builds an in-process server.
- The leak check reports a **count only**. If it fires again, re-add the per-type registry
  described under "How it was diagnosed" rather than guessing — it turned a day of speculation
  into two runs.
- Consider making the leak report a hard test failure. Today `DefaultUtfConfigT<>::~DefaultUtfConfigT()`
  swallows the exception and the process still exits 0, which is why this survived unnoticed; that
  is a separate decision about the test harness and was **not** changed here.
