# `CommandBase::getOptionsHelp()` hide / unhide pair is not RAII protected

**Origin:** task **T281** of the C++ test enhancement plan
(`notes/reviews/major/update_2026/whole-library-cxx-test-enhancement-plan.md`), while adding
`CmdLine_DryRunNotApplicableHidesParentOption` to
`src/utests/utf_baselib_cmdline/TestCmdLine.h`.

**Decision date:** 2026-09-08. **Status:** ~~no production change; recorded only~~ -
**CLOSED, fixed 2026-09-08**.

---

## Resolution (2026-09-08)

Fixed as item 8.1 of
`notes/reviews/major/update_2026/whole-library-cxx-test-enhancement-outstanding-issues-plan.md`.
`CommandBaseT::getOptionsHelp()` now wraps the unhide in `BL_SCOPE_EXIT`, so the root's
`dryrun,n` option is restored even when rendering the root's option block throws.
`CmdLine_DryRunNotApplicableHidesParentOption` gained a sub-case which makes that rendering
throw (through a root whose `formatMessage()` override throws on demand) and asserts that
`--dryrun` is neither hidden afterwards nor missing from the next command's help.

The rest of this document is the original analysis and is kept for the record.

---

## What the code does

`bl::cmdline::CommandBaseT::getOptionsHelp()`
(`src/include/baselib/cmdline/CommandBase.h`) ends, for a leaf command, with:

```
hideNonApplicableParentOptions();
oss << rootCmd -> getOptionsHelp();
unhideNonApplicableParentOptions();
```

`hideNonApplicableParentOptions()` looks up `"dryrun,n"` with `lookupParent = true` and calls
`setFlags( Hidden )` on it when `isDryRunApplicable()` returns false; the unhide half clears
the flag again. Both are `const` methods which mutate a **shared, parent-owned** option
through a non-const `OptionBase*`.

## Why it is a robustness defect

The middle call allocates, formats and streams. If it throws, `unhideNonApplicableParentOptions()`
never runs and the root's `--dryrun` option stays `Hidden` **for the rest of the process** -
it silently disappears from every subsequent help screen of every command, including the ones
for which dry run *is* applicable. There is no path which restores it.

A `BL_SCOPE_EXIT` around the pair would fix it:

```
hideNonApplicableParentOptions();
BL_SCOPE_EXIT( { unhideNonApplicableParentOptions(); } );
oss << rootCmd -> getOptionsHelp();
```

## Why it is deferred

The whole branch is dead in this repository - `isDryRunApplicable()` is not overridden
anywhere outside the new test case, so no in-repo caller can reach the leak today. The fix is
a production change to a public header and the plan classifies T281 as test coverage only, so
the branch is now covered by a test and the fix is left for a change which is allowed to touch
production.

## What is covered instead

`CmdLine_DryRunNotApplicableHidesParentOption` pins the non-throwing path end to end: the
control rendering shows `--dryrun`, the non-applicable command's rendering does not, only
`dryrun` is suppressed (`--verbose` survives), the flag is restored afterwards, and a
subsequent rendering shows `--dryrun` again. If the fix above is made, none of those
assertions change.
