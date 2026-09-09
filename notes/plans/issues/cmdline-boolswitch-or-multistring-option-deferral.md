# `bl::cmdline::BoolSwitchOrMultiStringOption` cannot carry a value

**Origin:** task **T364** of the C++ test enhancement plan
(`notes/reviews/major/update_2026/whole-library-cxx-test-enhancement-plan.md`), while adding
`CmdLine_BoolSwitchOrMultiStringOption` to
`src/utests/utf_baselib_cmdline/TestCmdLine.h`.

**Decision date:** 2026-09-08. **Status:** ~~no production change; recorded only~~ -
**CLOSED, the typedef was withdrawn 2026-09-08.**

---

## Resolution (2026-09-08)

Decided as item 4 (decision D8) of
`notes/reviews/major/update_2026/whole-library-cxx-test-enhancement-outstanding-issues-plan.md`:
**option 2 below, delete it as dead API.**

`BoolSwitchOrMultiStringOption` was removed from `src/include/baselib/cmdline/Option.h`, and
`CmdLine_BoolSwitchOrMultiStringOption` together with its `BoolSwitchOrMultiStringCmdLine`
fixture was removed from `src/utests/utf_baselib_cmdline/TestCmdLine.h` - the typedef was the
only thing the case existed to characterise.

Why option 2 rather than option 1: the typedef has no consumer anywhere in the repository, and
what it tries to express - "a flag, optionally followed by values" - is modelled in
Boost.Program_options by `implicit_value`, not by the `zero_tokens` its `SwitchImpl` policy
applies. Making it work would mean specialising a public template for a container value type and
verifying the `implicit_value` / `multitoken` interaction against Boost 1.90, which is only worth
doing if a downstream consumer is known. A consumer naming the typedef now gets a compile error
and uses `BoolSwitch` plus `MultiStringOption` instead; this is a source-compatibility note for
the release notes.

The rest of this document is the original analysis and is kept for the record.

---

## What the typedef is

`src/include/baselib/cmdline/Option.h`:

```
typedef Option< std::vector< std::string >, detail::SwitchImpl< std::vector< std::string >, true > >  BoolSwitchOrMultiStringOption;
```

`Option< T, IMPL >::getSemantic()` builds the semantic, attaches the `m_hasValue` notifier,
optionally calls `multitoken()`, and then hands the result to `IMPL::decorateSemantic()`.
For `SwitchImpl` that last step is `return semantic -> zero_tokens()`, which sets
`min_tokens == max_tokens == 0` in Boost.Program_options.

Note that the `multitoken()` call is gated on `OptionBase::isMultiValue()`, which reads the
option **flags** (`MultiValue`), not the value type - so for a plain
`BoolSwitchOrMultiStringOption m_flag( "flag,f", "..." )` it never runs at all. The plan text
for T364 assumed the gate was type-based; it is not, and the outcome is the same either way
because `zero_tokens()` is applied last and wins.

## Observed behaviour

Measured by `CmdLine_BoolSwitchOrMultiStringOption` on ub24-a64, Boost 1.90.0, both
clang2010 release and gcc1520 debug:

| command line | result |
|---|---|
| (absent) | no throw; `hasValue() == false`; value vector empty |
| `--flag` | no throw; `hasValue() == true`; value vector **empty** |
| `--flag a` | throws `bl::po::too_many_positional_options_error` |
| `--flag a b` | throws `bl::po::too_many_positional_options_error` |

The `a` / `b` tokens are never consumed by `--flag` (max_tokens is 0), so they are left over
as positional arguments; the parser in the test declares none, and Boost rejects them.

## Why this matters

The name promises "a switch **or** a multi string option", but the second half is
unreachable: the option can never hold a value, and supplying one makes the whole command
line fail with a message that does not mention `--flag` at all. The typedef is therefore
broken as written.

It is also **unused**: the symbol is instantiated nowhere in the repository - not in
`src/include`, not in `src/local`, not in `bl-tool`, and (before T364) not in any test. Only
`BoolSwitch` (the `bool` sibling) and `MultiStringOption` (the plain vector) have consumers.

## The two possible fixes

1. **Make it work.** `decorateSemantic` would have to stop applying `zero_tokens()` when the
   value type is a container - i.e. become `isMultiValue()`-aware, or be specialised for
   `std::vector< T >` so it applies `implicit_value()` / `zero_tokens()` only for the scalar
   switch case. That is a change to the semantics of a public template in a public header.
2. **Delete it as dead API.** It has no consumer, so removal costs nothing inside this
   repository, but it is a published symbol and its removal is a breaking change for any
   external user.

## Why it is deferred

Both options are production changes to a public header, and T364 is classified as test
coverage only. Choosing between them is a design decision about a public API, not a test
task. The behaviour is now recorded by a test, so whichever way it is decided the evidence
is in place and the assertions in `CmdLine_BoolSwitchOrMultiStringOption` have to be edited
deliberately.

## Verification gap

The plan asked for the empirical step to be run on all four supported toolchain/variant
combinations. Only the two Linux ones were available (clang2010 release, gcc1520 debug) and
both agree. The behaviour follows from `zero_tokens()` and from Boost's positional handling,
neither of which is platform specific, but the Windows msvc/clang runs were not performed.
