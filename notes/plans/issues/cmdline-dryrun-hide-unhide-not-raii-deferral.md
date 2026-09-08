# `CommandBase::getOptionsHelp()` hide / unhide pair is not RAII protected

**Origin:** task **T281** of the C++ test enhancement plan
(`CPP_TEST_ENHANCEMENT_PLAN.md`), while adding
`CmdLine_DryRunNotApplicableHidesParentOption` to
`src/utests/utf_baselib_cmdline/TestCmdLine.h`.

**Decision date:** 2026-09-08. **Status:** no production change; recorded only. The new test
case asserts the non-throwing path only.

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
