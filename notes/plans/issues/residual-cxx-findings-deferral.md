# Asio Shim Scope, Filesystem Copy Policy and Immutable Loader Types: Deferral Record

This document records three deliberate decisions **not** to change code in this cycle, taken while
closing the residual findings of the deep-dive review, and the conditions under which each must be
revisited. Each is a risk acceptance, not an assessment that the risk is absent.

**Findings:** CXX-09, CXX-10 and CXX-11,
`notes/plans/issues/pr-review-deep-dive-residual-findings.md:142-185`

**Analysis and decision:** `notes/plans/issues/pr-review-residual-cxx-findings-plan.md`

---

## Decision

**Date:** 2026-09-02
**Status:** Each finding partly fixed and partly deferred; the deferred halves are recorded below

| # | Item | Disposition |
|---|---|---|
| 1 | Asio resolver shim is not a completion-token adapter (CXX-09, second half) | **Deferred** — documented as internal, not public API |
| 2 | Recursive copy has no symlink / special-file policy and no skipped-entry report (CXX-10, second half) | **Deferred** — behaviour documented precisely at the call site |
| 3 | Deleted move assignment on the public immutable loader types (CXX-11) | **Deferred** — documented in code; no release note |
| — | Handler lifetime hazard in the shim (CXX-09, first half) | **Fixed** — `std::decay` applied, with a regression test |
| — | Throwing `is_directory` inside the `error_code` overload (CXX-10, first half) | **Fixed** previously in `22ffa9b` |

None of the three is a defect which any in-repository caller can reach. All three exposures are to
downstream consumers only.

---

## Item 1 — the Asio resolver shim is not a completion-token adapter

**Location:** `src/include/baselib/core/detail/BoostAsioCompat.h`, `resolve_handler_wrapper` (`:298`)
and the `async_resolve` overload which instantiates it

```cpp
    template <typename Handler>
    struct resolve_handler_wrapper
    {
        Handler handler;

        void operator()(const boost::system::error_code& ec, const results_type& results)
        {
            handler(ec, results.begin());
        }
    };
```

The shim exists because Boost 1.89 changed `async_resolve` to deliver a
`basic_resolver_results<Protocol>` where this codebase's callbacks expect a
`basic_resolver_iterator<Protocol>`. It reproduces the shape of the old operation, but not its
contract:

- it accepts a plain callable only, not an Asio **completion token**, and `async_resolve` returns
  `void`, so `use_future`, `use_awaitable` and the deferred token do not work with it;
- it does not propagate the handler's **associated executor, allocator or cancellation slot**, so an
  operation started through it does not participate in cancellation and does not necessarily run on
  the executor the handler was associated with.

### Why this is deferred

A faithful adapter means implementing `async_initiate` with a proper initiation function, forwarding
`associated_executor` / `associated_allocator` / `associated_cancellation_slot` through the wrapper,
and giving the operation a real return type driven by the token. That is a larger and more delicate
piece of work than the shim it would replace, and it delivers nothing to any current caller.

### What limits the exposure

There are exactly **three** call sites of the query-based `async_resolve` in this repository, and all
three pass a bound callable which needs none of the missing behaviour:

- `src/include/baselib/tasks/TcpBaseTasks.h:815-823` — `cpp::bind( &this_type::onResolved, ... )`
- `src/include/baselib/tasks/utils/Pinger.h:582-590` — the same shape
- `src/utests/utf_baselib/TestBoostAsioCompat.h` — inline lambdas

The shim also lives in `boost::asio::ip`, a third-party namespace, which is a second reason not to
present it as an interface anyone outside this repository should bind to. It is now labelled
**internal, not public API** in a comment at the declaration.

### Review consensus

- `notes/plans/issues/pr-review-deep-dive-residual-findings.md:142-160` (CXX-09) — "high for a
  faithful completion-token shim ... only worth it if the shim is to be treated as public API —
  otherwise document it as internal"
- `notes/reviews/swblocks-baselib/major/update_2025/v1/pr_review_analysis_gpt56sol_deep_dive.md:505-519`
  — "a hand-written callable wrapper does not preserve the Asio completion-token contract"

Note that the related `BOOST_ASIO_DISABLE_STD_CHRONO` define is a separate, already-recorded
acceptance — `notes/plans/issues/public-header-hygiene-deferral.md`, item 2 — and is not restated
here.

### Conditions to revisit

- A caller inside this repository wants coroutines, futures or per-operation cancellation on name
  resolution.
- The shim is to be published as a supported compatibility layer rather than an internal detail.
- The project's minimum Boost version rises past the point where the shim is needed at all, in which
  case it should be **deleted** rather than completed: the `#else` branch already typedefs straight
  to the stock resolver, and the right end state is to migrate the two production call sites to the
  modern string-based `async_resolve` and drop the shim entirely.

---

## Item 2 — recursive copy has no link policy and reports nothing

**Location:** `src/include/baselib/core/FsUtils.h`, inside `copyDirectoryWithContents` (`:916`);
the filter is at `:944`

```cpp
                        if( fs::is_regular_file( sourcePath ) || fs::is_directory( sourcePath ) )
                        {
                            fs::copy(
                                sourcePath,
                                targetDir / fs::path( sourcePathStr ).relative_path()
                                );
                        }
```

There is no `else`. An entry which is neither a regular file nor a directory is dropped with no
counter, no callback, no log and no error, so a caller cannot distinguish a complete copy from a
partial one.

**The finding as filed describes this as silently skipping symlinks, which is not accurate and the
correction matters**, because it makes the gap considerably narrower than it sounds.
`fs::is_regular_file` and `fs::is_directory` **follow** links, so what actually happens is:

| Entry | Result |
|---|---|
| Link to a regular file | **Copied**, dereferenced, as a regular file — content, not link |
| Link to a directory | An **empty** directory is created; `fs::recursive_directory_iterator` does not descend into it |
| Dangling link | Dropped |
| FIFO, socket, device node | Dropped |

### Why this is deferred

A proper fix is not the `if` statement; it is a policy enum (`reject` / `copy the link` / `follow
within the source root` / `skip`) plumbed through the copy helpers, plus a report channel for
skipped entries, plus the escape-prevention logic that the follow-within-root option requires. It
also needs symlink, FIFO and permission tests, **none of which exist today** — the only coverage is
`src/utests/utf_baselib/TestBaselibDefault.h:7851`, a command-line-driven test whose only assertion
compares `recursive_directory_iterator` path counts and which creates no special entries.

Estimated at 2-3 days including that test matrix, against no known caller who needs it.

### What limits the exposure

The current behaviour is the **safe** default in the dimension that matters most: it never follows a
link out of the source tree, so it cannot be used to escape the intended root. The risk is
incompleteness, not escape.

The single production caller, `bl::fs::copyDirectoryWithContents`
(`src/include/baselib/core/FsUtils.h:1241-1247`), is used for copying build and test artefacts, not
for backups or deployments of arbitrary user trees.

### Review consensus

- `notes/plans/issues/pr-review-deep-dive-residual-findings.md:162-173` (CXX-10) — "the current
  silent-skip behavior is the risk — incomplete deployments/backups with no signal"
- `notes/reviews/swblocks-baselib/major/update_2025/v1/pr_review_analysis_gpt56sol_deep_dive.md:531-537`
  — "Expose an explicit policy (`reject`, `copy link`, `follow within root`, `skip`) and report
  skipped entries"

### Conditions to revisit

- Any caller starts using this to copy a tree it did not itself create — a user directory, a
  deployment payload, a backup source.
- A defect is reported which traces back to a silently incomplete copy.
- The cheapest first step, well short of the full policy, is to **count** skipped entries and log
  them at warning level; that turns a silent partial copy into a visible one for a fraction of the
  cost and needs no API change.

---

## Item 3 — deleted move assignment on the public immutable loader types

**Location:** `src/include/baselib/loader/Platform.h:90` and `src/include/baselib/loader/Manifest.h:101`

```cpp
            PlatformIdentityT& operator= ( SAA_in PlatformIdentityT&& other ) = delete;
            ManifestT& operator =( SAA_in ManifestT&& other ) = delete;
```

Both were added so the types compile under GCC 15, which enforces that a const member cannot be
assigned to inside a template body. Deleting the operator is a **source compatibility break** for
downstream code which assigned to one of these types.

### Why this is deferred

The break is accepted rather than repaired. No caller in this repository assigns either type; the
copy operations were already deleted for every `om::Object` implementation by
`BL_DECLARE_OBJECT_IMPL_DEFAULT` → `BL_CTR_COPY_DELETE`; and both types are value objects which are
constructed once and then only read, so the operation they lose is one they never usefully had.

Two observations which weaken the stated justification and are recorded so a future reader does not
have to rediscover them:

- `PlatformIdentityT`'s move constructor **does not move**. All three members are
  `const std::string`, so each `std::move()` in the member initializer list binds to the copy
  constructor. The type advertises a move it does not perform.
- `ManifestT` is only **partly** immutable — `m_classIds`, `m_pluginName`, `m_pluginDescription` and
  `m_platform` are not const. The const members are the reason the operation cannot be generated,
  not evidence that assigning one would be meaningless.

The redesign these point to — dropping `const` from the members and suppressing copy and move
through `BL_NO_COPY` / `BL_NO_COPY_OR_MOVE`, which is what the other 62 sites in this codebase do —
would make both types consistent with the rest of the codebase and restore a real move. It is
estimated at 2-3 days and is not worth spending in this cycle.

### What limits the exposure

These are the only two classes in the repository which use the const-members-plus-deleted-move
idiom; it is not a pattern that is spreading. A downstream break is a **compile** error, which is
the loud failure mode, not a silent behavioural change.

**No release note is produced for this item**, by explicit decision — the exposure is narrow enough
that the code comments are the appropriate record.

### Conditions to revisit

- A downstream consumer reports the break.
- Either type gains a caller inside this repository which wants to assign it.
- Any further class is about to adopt the same idiom — that is the point at which the redesign
  should be done instead, so the pattern does not spread.

---

## What was fixed

**CXX-09, the handler lifetime hazard.** `resolve_handler_wrapper` is now instantiated with
`typename std::decay< ResolveHandler >::type`, so an lvalue handler is stored by value rather than
becoming a reference into the caller's frame. Note this was **latent** — every in-repository call
site passes a temporary, so it was reachable only from downstream code. It is covered by
`BoostAsioCompat_ResolverAsyncHandlerIsStoredByValue`, which asserts that the handler is copied;
asserting on the dangling read instead would have been useless, because reading through a dangling
reference is undefined behaviour that in practice returns the expected value and the test passes
whether or not the bug is present.

**CXX-10, the throwing `is_directory`.** Fixed previously in `22ffa9b`; the `error_code` overload of
`fs::copy` now uses the `ec` overload and returns early
(`src/include/baselib/core/detail/OSImplPlatformCommon.h`).

**CXX-12, the `std::char_traits< unsigned char >` specialization.** Not deferred — removed. The
review estimated 3-4 days on the assumption that it would mean touching every
`std::basic_string< unsigned char >` use site; there turned out to be exactly two, both local
typedefs feeding a base64url decode, and `SerializationUtils::base64UrlDecodeVector` already
existed. Both now use `std::vector< unsigned char >` and the 100-line specialization is gone.
