# Directive-only members leaving `split_members( )` — deferred, 2026-09-24

**Status:** deferred by the maintainer on 2026-09-24, with the wrong answer it used to cause already
fixed by other means. **Nothing is broken while this is deferred.** The condition that reverses it is
at the end.

---

## 1. What the thing is

`split_members( )` divides a namespace block into *members* on blank lines. A run of preprocessor
directives separated from its neighbours by blank lines therefore becomes a member of its own,
carrying no code at all. Three exist in the tree today:

| file | member |
|---|---|
| `include/utests/baselib/UtfPluginFixture.h` | `#endif` |
| `utf_baselib/TestBaselibDefault5.h` | `#if ! defined( _WIN32 )` |
| `utf_baselib/TestBaselibDefault5.h` | `#endif // ! defined( _WIN32 )` |

They are members because of where the blank lines fall, not because anything decided they should be.

**Three other members begin with a directive and are NOT this case** — `UtfPluginFixture.h:91` and
`:98`, which are six-line `g_libExt` definitions under `#if defined( _WIN32 )` / `#else`, and
`TestMessagingDefault.h:491`, a 25-line `#if 0` block. They carry code and are ordinary members. *An
earlier count said six one-line members; that was wrong and is corrected here, because the predicate
below turns on exactly this distinction.*

## 2. What it cost, and why that is already closed

C6's duplication half asks *"is this text duplicated within a module, and therefore an ODR risk?"* A
bare `#endif` is duplicated all over any module and is never an ODR risk. So the ordinary operation
of **moving a guarded helper into a sibling header of the same module, repeating the guard** — a
normal split — reported two ODR risks, on the `#if` and `#endif` lines themselves.

That was a live wrong answer: a red gate on a legal operation, whose tempting resolution is to
refresh it away, which would disarm something real.

**It was fixed on 2026-09-24 in `35fbb51`,** by excluding from the duplication half alone any member
whose every non-blank line is a conditional directive. The members stay in C6's no-loss and guard
halves, where their text still carries information. The predicate cannot be evaluated from the
manifest — a label alone would wrongly exempt `UtfPluginFixture.h:91` — so members gained a
`conditional_only` field: **717 members, 714 false, 3 true.**

## 3. What is still owed, and what it costs to leave

The root question the fix did not answer: **should `split_members( )` emit these at all?**

Leaving them costs **double reporting**. A change to a guarded region reports both the directive
member's own text moving *and* the guard stack changing on the members inside it — measured at
**7 lines for 5 facts** in two of the guard controls. That is noise in a gate whose whole value is
that a reader can act on every line it prints.

It is **not** a correctness problem. Nothing is judged wrongly, no operation reds that should not,
and no change passes that should not. It is the reason this is a deferral and not a schedule.

## 4. Why it was deferred rather than taken

- **The wrong answer is already gone.** What remains is cosmetic, and the project's own rule is that
  a live defect handing a caller a wrong answer is scheduled on sight while everything else waits for
  a batch. This stopped being the former when `35fbb51` landed.
- **It changes the population, 717 → 714.** That needs a manifest refresh, and a refresh is the one
  operation this project has repeatedly used to bless something nobody re-read.
- **It touches the extractor**, which every invariant reads — the widest blast radius in the tool.
  `scan_file( )` and `split_members( )` are upstream of C1 through C13 alike.
- **It would ride nearly free with the bracketed-span work**, which touches the same function and is
  scheduled. Taking it separately pays the extractor's blast radius twice.

## 5. What reverses this

**Any change that opens `split_members( )` for another reason** — the bracketed-span fix is the one
already scheduled, and it is the natural home. At that point the marginal cost is a predicate and a
refresh that is happening anyway.

**Or** a second observation of the double reporting confusing a reader in practice. Under the
project's rule an item recorded twice is a decision waiting rather than a note to write a third time,
and this record is the first writing.

**What does NOT reverse it:** finding more directive-only members. Three or thirty, the cost is the
same kind and the duplication half no longer fires on any of them.

## 6. Where the reasoning lives

- `scripts/utests/utf_inventory.py` — `conditional_only`, and the duplication half's exclusion
- `notes/plans/issues/astra-remediation-owed-work.md` — the two reviews that raised it, 2026-09-24;
  it was a *"could not settle"* item in the first and a decision in the second, which is the rule
  working as intended
- `35fbb51` — the fix that closed the wrong answer and left this behind
