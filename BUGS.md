# Known Bugs

## Open

**7 open entries:** #2, #49, #65, #107, #131, #132, #154.
Next free number is **#175**.

#157 and #158 were in this list and are now closed as **already fixed** — both
were stale open entries, not new work. #157 (punctuation swallowed into an import
alias) was resolved by the `extract_alias_from_line` helper both scanner sites now
share, with `test/pass/import_alias_punctuation.sf` covering it. #158 (a `match`
above its enum's declaration compiled to a branchless first-arm destructure) was
resolved by the enum prepass in `codegen.sf` that registers every enum before any
body is generated; `test/pass/match_before_enum_decl.sf` now pins it, since the
entry noted no site in the tree exercised it. Both verified by probing the binary
before closing — see `BUGS_CLOSED.md`. Count 10 → 8, next-free unchanged.

#174 (`>=` maximal munch breaking `List<Int>=[...]` with no space: the lexer glues
the generic's closing `>` to the initializer's `=` into one `>=`, so the type
annotation never closes) was filed from the playground feature eval and fixed in
the same window — the parser's generic-close loop now splits a `>=` token into a
closing `>` plus an `=`, mirroring the existing `>>` split. Its full entry is in
`BUGS_CLOSED.md`.

#155 and #165 were in this list and are now fixed together — a class-typed
parameter or return that accepted an incompatible concrete type (a String where a
`Box` was declared) and segfaulted, at the argument site and the return site
respectively. One helper, `class_type_mismatch`, guarded on the class/enum
registries rather than on name spelling (the discipline #143's `check_condition`
already used), closed both; it turned out not to need the resolve pass #155's
narrative had gated it on. Count 12 → 10, next-free unchanged, nothing else moved.

#171 (a type parameter used only inside a generic parameter never resolves) was
filed as a two-layer defect and is now FIXED — both layers landed together, as its
entry said they had to. Moved to Resolved; count 14 → 13, next-free stays 174.

#173 (`try`/`catch`/`throw` trapping on wasm32) is fixed and moved to Resolved:
the wasm32 link path now enables LLVM WebAssembly SjLj (`-mllvm -wasm-enable-sjlj`)
with a small hand-written support file, so setjmp/longjmp actually jump. Count
13 → 12.

#172 (wasm32 `malloc` handing out pointers past memory when `memory.grow` is
capped/fails) is fixed and moved to Resolved: `@malloc` now honors the grow
result and the wasm32 max-memory cap was raised 16 MB → 2 GB. A browser eval of
8 real compile→run rounds went 2/8 → 8/8. Count 14 → 13.

#170 (a `for-in` list loop trapping on wasm32) is fixed and moved to Resolved:
the GC pointer-recognition floor in `__rt_gc_tag_of` was hardcoded to the native
4 GB value, rejecting every wasm32 heap pointer. Count 14 → 13, next-free
unchanged, nothing else moved.

#171 (a type parameter used only inside a generic parameter never resolves) is a
two-layer defect — filed but deliberately NOT half-fixed. Added to the set above;
count 13 → 14, next-free 171 → 172, nothing else moved.

#170 (a `for-in` list loop traps in wasm32 while working natively) was filed
from the playground: 3 of its 7 bundled examples crashed on it. Added to the set
above; count 12 → 13, next-free 170 → 171, nothing else moved.

#161 was in this list and is now fixed — a method call on a receiver from one
module bound to a same-named class in another, because codegen dispatch keyed on
a bare, last-writer-wins class name instead of the receiver's static type — so it
moved to Resolved. The count above went 13 → 12 for that reason and no other; the
set is otherwise unchanged, which is the check.

#160, #163, #164, #167, #168 and #169 are taken and not in the list above because
all six are fixed and live under Resolved: an `: Any` global that could not widen,
three malformed-`import` defects, a fatal `IndexError` on any incomplete source,
the GC constructor rule, two type declarations binding one name, and a generic
return type losing its type arguments. #165 arrives open from `main` (a
class-typed parameter accepting a String). The rule that made the old
placeholders necessary still holds: a number can be taken without its text being
in this file yet, so the next-free number is one past the highest *claimed*, not
one past the highest open or even the highest written.

#169 is the fourth entry in this file to be renumbered on landing, and the
cheapest one so far, which is worth one line on why. It was written as #166
against a tree whose header said "next free is #166" — true when read, stale by
the time it was pushed, because #166 had meanwhile been claimed upstream (and was
itself a renumber, of #164). Nothing detects that: the number is correct locally
and wrong globally, and the only thing that settles it is fetching before
claiming. The reason this one cost a single `sed` rather than an argument is that
the entry was self-contained — no other entry cross-referenced it yet — so there
was nothing to keep in step with the rename.

Worth recording how #162 and #163 avoided contesting each other, because it is
the first time the scheme resolved a three-way overlap with no renumber at all:
#163 was claimed *after* #161 and #162, saw both, and stepped over them to 163
rather than treating an unpushed claim as free. The "unpushed side moves" rule
would have let it take 162; it did not need to, and not exercising a tie-break
you are entitled to is cheaper than winning it.

**#162 is the sixth collision, and it was caught twice by the same grep.** It was
filed as #156, which `main` had already taken for the `var x = nil` fix; the
pre-merge re-read then found #161 claimed by `sfx`, so it landed on #162. Two
renumbers in one pass is a first, and it is the strongest case yet for the rule at
the end of the note below: had the grep been skipped, the number would have been
wrong in a commit message, a `KNOWN_FAIL` entry and a test file's header comment,
all of which are more annoying to fix than a header line. The tie-break was
unchanged — committed and cited keeps the number, uncommitted moves.

#162 was found by #154's own regression test rather than by looking for it, and it
is filed separately on purpose: it is an argument-temp rooting hole in codegen,
reproducible with plain classes, and #63 — which reads like the same bug — is
closed and was about the *moving* nursery. Retiring that nursery removed the move
without supplying the missing root, so the non-moving collector freed the temp
instead of relocating it. Same lesson as #161 one entry up: the non-regression half
of a fix is where the next bug is found. (Now fixed and under Resolved; the
separate filing is what let the fix be scoped to argument loops rather than
reopened against #63's nursery narrative.)

#154 stays open with its payload half fixed. An enum with at least one field
anywhere now carries a GC header and names itself on the no-static-type paths; a
fieldless variant is the immediate `tag << 56` with nothing to hang a header on, so
it needs an enum-bearing NaN-box tag rather than another formatter arm.

The count and the open set live in **one** place: the header at the top of this
file. A second copy used to sit here, and it went stale exactly as I10 predicts —
it still said 12 open with #160 listed after #160 was closed, because closing an
entry moves it under `## Resolved` and nothing makes a duplicated tally follow.
(#160 was open when the note below was written and closed on 2026-08-04; #162
arrived from the saffron_154 line.)

**This line's duplicate-declaration fix was renumbered three times: #160 → #164 →
#166 → #168.** Written as #160, with the cross-module dispatch bug as #161. By the
time the GC work below was finished, `main` had claimed **#160** for the `: Any`
widening entry and **#162** for the saffron_154 line's SSA-temp bug, so it became
**#164** and the GC fix took **#165**. Then, during the bootstrap and suite run
that verified the first merge, `main` landed its own **#164** (a fatal `IndexError`
on incomplete source), so mine moved to **#166**. Then, during the bootstrap and
suite run that verified the *second* merge, `main` landed its own **#165** (a
class-typed parameter accepting a String), so the GC fix moved to **#167** and the
duplicate-declaration fix to **#168**. Both times mine was the unpushed side, and
both times the collision was found by the same grep in the same window — after the
verification, before the push.

Three lessons, and the third is the one this second collision adds:

- A reservation note in `main` is worth writing even for a number you have not
  pushed yet. #161 survived all of this *only* because `main` had reserved it by
  name for this line. Every entry that had no reservation moved at least once; the
  one that had one never moved.
- **The re-read has to happen after the last long-running verification, not before
  it.** The rule as written says "immediately before committing", and that is what
  was done — the pre-commit grep found #164 free and it was. What it did not
  survive was the ~15 minutes of bootstrap-plus-suite that came *after* the commit
  and before the push, which is exactly long enough for `main` to move. The
  operative moment is the push, not the commit. This cost a renumber across four
  files and one `### ` heading; catching it required re-fetching after the suite
  finished, which was luck rather than procedure.
- **Fixing the timing of the re-read does not reduce the number of collisions; it
  only moves where you find them.** The second collision happened *after* the
  lesson above was written down and followed. The pre-push grep did its job — it
  is why the renumber was a five-file `sed` and not a wrong number in `main` — but
  the underlying race is structural: verification takes longer than the interval
  between `main`'s commits, so any number claimed before a bootstrap is a number
  held across a window where it can be taken. The procedural fix caps the *cost*
  of a collision at one mechanical rename. The only thing that would cap the
  *count* is claiming the number in `main` first (a one-line reservation commit,
  pushed before the work starts) — which is exactly what #161 did, and #161 is the
  only entry on this line that never moved. Two collisions in one session is the
  argument for making that reservation the default rather than the exception.

**#161 was found by writing #168's test, not by looking for it.** The
duplicate-declaration check had to prove it did *not* reject two modules declaring
one name, and the fixture that exercises that — two `Marker` classes — turned out
to dispatch both receivers' methods to the second module. The general lesson is
about where the non-regression half of a rejection fix points: a check that
tightens what compiles needs a test for the shapes it must still accept, and that
test walks straight into whatever else is wrong with those shapes.

#161 and #162 are claimed but not present in that header: #161 is the sfx line's
cross-module method-binding bug and #162 is the saffron_154 line's SSA-temp GC
bug, both committed there and unpushed when this entry was written. Per the
rule below, whoever is unpushed moves — but both were claimed *before* #163,
so #163 stepped over them rather than contesting either. #163 is resolved on
sight and is filed under Resolved.

(That paragraph is `main`'s, and this line is the reason half of it is now
stale: #161 *is* in the header list above, because this line pushed it. Left in
place rather than rewritten, because what it records is a state — two numbers
claimed in worktrees and invisible from `main` — that was true when written and
is the exact condition the next-free rule exists for. A narrative that describes
a moment does not need updating every time the moment passes; a *count* does,
which is the whole distinction I10 is drawing.)

#160 took its number without a collision — the first entry in a while to do so.
The pre-commit re-read (see the note below) was run against `origin/main` and
found #160 free, which is the whole procedure working as intended rather than
detecting a clash. Worth recording that the quiet case happens too; five
consecutive collision narratives make the scheme look worse than it is. (The
sixth, above, landed on the very next pair of numbers — so the quiet case is
still the exception, not the new normal.)

#160 was also the second half of #147, which is the more useful thing about it: the
checker stopped conflating `: Any` with silence, and codegen never did. A fix
recorded as landed had reached one of two consumers, and nothing in the tree said
so. That is the argument for invariant I10 (one source of truth per fact) stated as
a measurement rather than a principle — the inference in question exists in five
pasted copies, and closing #160 on 2026-08-04 meant editing all five by hand. The
argument survives the fix: what made the hand-edit safe was not deduplication but
the type migration splitting the overloaded guard into two askable questions.

**The fifth collision, and the first one the pre-commit re-read actually caught.**
The rule at the end of the note below — re-read the next-free number from `main`
*immediately before committing* — was added in response to #156's collision, which
no amount of reading-at-start could have prevented. Running it here found that
`#158` had been claimed and **committed** by the lsp worktree (for the `match`
textually above its enum declaration, listed above) while this line's
condition-type tightening was still uncommitted. So the tightening became **#159**.
The tie-break was the same as every one before it, and for once cheap: the
committed, cited side keeps the number, the uncommitted side moves. Worth noting
that the lsp entry had already been renumbered four times by then, so honouring
"committed wins" also avoided forcing a sixth rename on the more-settled entry.
Five collisions in, the pattern is not that the numbering scheme is fragile but
that a single counter shared by four concurrent worktrees has no owner; the
pre-commit re-read is the cheapest thing that makes it converge.

#154 and #155 were filed on the same day on two lines of work that had not yet
met — origin/main's "an enum inside a printed collection prints a bit pattern"
took #154, and this line's "no `return` is checked against its declared return
type" took #155 after `grep -h "Next free number"` across worktrees showed #154
already claimed (by the lsp-symbol-payload worktree, for a `match` above its enum
declaration). This time the cross-worktree grep did its job: the two picked
different numbers, so the merge only had to place both entries rather than
renumber either. That is the note two collisions up working as intended.

**And then #155 collided anyway, on the very next merge — the fourth collision.**
The nil-initializer fix (`var x = nil` typed the variable `Nil` forever) was
written on a worktree that read "next free is #156" from a header where #155 was
still unclaimed, filed itself as #155, and was committed before the `no return`
entry above landed on `main`. It is now **#156**, under `## Resolved`. The tie-break
was the same as #152's and cost about as little: the `no return` entry was already
pushed and cited from `main`, the nil entry existed only in one unpushed worktree,
so the unpushed one moved. What makes this one worth recording separately is that
the cross-worktree grep the note above credits **would not have caught it** — the
number was genuinely free when it was read. The rule that does catch it is the one
below: re-read from `main` immediately *before committing*, not when you start.
#149 is the alias/type-re-export fix (`var X = Module.SomeType`), under
`## Resolved`. It was written on the ide-stage0-spans worktree and renumbered
five times at merge — #142 → #143 → #145 → #146 → #149 — each time origin/main
turned out to already own the number it had reached. The lesson below about
reading the next-free number from `main` is exactly why.

#144, #145, #146, #150, #151, #152 and #153 were each filed and fixed in the same
sitting — the unused-variable warning firing on compiler-mandated match-arm
bindings, call-site argument types never being checked at all, an inferred
global read from inside a function being typed `Int`, interface-typed dispatch
binding to the empty abstract stub, a cross-module subclass emitting an
unprefixed inherited-method forwarder, the lexer silently discarding `\xNN`
escapes, and an unresolved-type subscript falling through to the list path — and
all seven are under `## Resolved`.

**#152 collided the same way #146 did, and the subscript fix moved to #153.** ph5
filed the unresolved-subscript crash as #152 while reading "next free is #153" from
its own out-of-date header; origin/main had meanwhile given #152 to the lexer-escape
entry above. The tie-break resolved it in seconds this time: the escape fix was
already merged and cited by number in `lexer.sf` and `test/FAILURE_BASELINE.txt`,
while the subscript entry existed only in one unpushed worktree, so the subscript
entry moved. Its five in-source citations (`runtime.sf`, `glob.sf`, `find.sf`,
`methods_body.sf`, `test/pass/unresolved_index.sf`) were rewritten in the merge.
That is three collisions in this file's history now, all with the same cause:
**re-read this header from `main` immediately before filing, not from the worktree
you have been in all day.**

**#146 collided across two unpushed lines of work, and the escape fix moved to
#152.** Two different bugs were both filed as #146: locally "an inferred global
read from inside a function was typed `Int`" (commit `f0d52ab` plus a
rebootstrap, and three test/fixture files naming the number in their headers),
and on origin/main "the lexer silently dropped the backslash of every escape it
did not know" (commit `1800720`, one comment reference in `lexer.sf`; the test
that shipped alongside it, `test/pass/iface_dispatch.sf`, belongs to #150). Per
the tie-break below — the side with fewer commits, comments and test names
carrying the number moves — the local entry keeps #146 and the lexer-escape
entry is renumbered #152. Both were already FIXED and committed when they met,
so neither could simply be renamed in place; the renumbering is recorded here
because the escape bug is cited by number in prose (`lexer.sf`, updated to #152
in this merge) rather than only in a heading. origin/main's own #151
(cross-module subclass forwarder) did not collide — it landed after both #150
and the escape fix, and keeps its number here.

#140 was never filed — the count was bumped past it in the same commit that
closed five entries, so the number is burnt rather than in use. Do not reuse it;
a gap is cheaper than two entries sharing a number in the git history.

**Reconciling two open lists at a merge is not a union.** The collisions above are
all about the same number meaning two things; this is the opposite failure and it
bites in the other direction. #143 was fixed in `2c34dc6`, and a merge on
2026-08-03 put it back into the open count because the two sides' header lists
disagreed and the union looked like the cautious resolution. It is not cautious —
an entry in `## Open` that is already fixed sends the next reader to re-diagnose
solved work, and the entry's own prescription still reads as a to-do list. Before
adding a number to this header at a merge, check whether it has an entry under
`## Resolved`, and when the lists disagree about a number, **probe the shipped
compiler** rather than trusting either list. That is how #143 was caught: `if (42)`,
`while ("y")`, `if (true and 42)`, `if ([1])` and the if-expression form all
already errored. The count is bookkeeping and it drifts; the binary is the ground
truth.

**Read the next-free number from `main`, never from a worktree.** On 2026-08-03
three separate worktrees each numbered a different bug #137, and two of them also
claimed #138: the `as`-as-expression bug (this file's #137), ph5's List/Map `==`
bug (re-filed as #142), and ide-stage0-spans' `var X = Module.Type` alias bug
(still unmerged and still mis-numbered). ph5's had *already* been renumbered once,
from an internal task ID that collided with the resolved #28. The alias bug has
since merged and been renumbered to #143 to settle the collision. A worktree branched
before a filing carries the pre-filing note forward and reads it as authoritative,
so the collision is the default outcome rather than an accident, and it is only
visible at merge time — by which point the number is in commit messages, code
comments and test names. If you are filing from a worktree, `git show
main:BUGS.md | head -10` first.

**The rule cuts both ways, and `main` is not automatically right.** Later the same
day this file filed #143/#144 from `main` while three worktrees had *already*
agreed on #143 (non-`Bool` condition lowering) and #144 (unused-variable noise),
and one had taken #145 — none of them merged yet, so `main` was the stale copy.
These two entries were renumbered to #146/#147 rather than making three worktrees
renumber, which is the general tie-break: the side with fewer commits, comments and
test names carrying the number moves. Before filing, check the worktrees too:
`grep -h "Next free number" .claude/worktrees/*/BUGS.md BUGS.md`.

The same tie-break settled the ide-stage0-spans alias fix when that worktree was
finally merged: it had reached #146 through prior renumberings but had zero commits
or test files citing any final number, while `main`'s #146 (inferred-global typed
`Int`) had two commits and two test files carrying it, and `main` had since also
filed #148 (nonexistent class-member read) from another session. Ide's entry moved
to #149; the ambient rule did not change.

Everything with a resolution lives in **`BUGS_CLOSED.md`**, full narrative intact
under `## Resolved` (with `## Fixed` at the end of that file as the older
one-line-bullet log). **An entry whose title says FIXED belongs in
`BUGS_CLOSED.md`** — if you find one in this file, move it. Closing a bug is a
move between files, not a retitle in place. Before the 2026-08-06 split the two
sections shared one file and drift recurred — for a while `## Open` held 81
entries of which only 14 were actually open, and on 2026-08-03 seven FIXED entries
(#110, #121, #122, #124–#127) sat in `## Open` while the count disagreed. A whole
file cannot strand a FIXED entry the way a section above a marker could, but the
mechanical check still costs nothing:

```bash
tools/bugs.sh --check          # nonzero exit if a FIXED entry is stranded here
grep -cE '^### [0-9]+\.' BUGS.md   # every heading here is open by definition
```

`tools/bugs.sh` reads this file alone; it never consults `BUGS_CLOSED.md`, because
the open set is exactly the headings that live here.

Two titles here are deliberately hedged rather than resolved. #107 says LARGELY
CLOSED because the assertion backfill is incomplete, and #12 was moved to
`## Resolved` as OBSOLETE rather than FIXED — it no longer reproduces, but its
repro targeted the dead C VM, so nothing was fixed for it.

The block numbered 50–57 came out of building the playground; the full narrative
log for that work, including the ones fixed along the way and the workarounds
each forced, is at `docs/design/playground-bug-log.md`. Those entries are under
`## Resolved` now. The log also contains entries that no longer reproduce, noted
there rather than carried forward.

### 107. LARGELY CLOSED — 43 positive tests asserted nothing and would pass on any output

**Severity: high — this is the reason the other entries survived.**

| | count |
|---|---|
| assertions **and** `.expected` | 0 |
| assertions only | 77 |
| `.expected` only | 33 |
| **neither — passes on ANY output** | **64** |

The 64 are graded solely on "exit 0 and no `Runtime Error:` on stderr". They
print, and nothing checks what. Includes all 11 `mini_*` tests,
`test/pass/enums.sf`, `test/pass/generics.sf`, `test/pass/closures.sf`,
`test/pass/interfaces.sf`, `test/pass/operators.sf`, `test/json.sf`,
`test/inheritance.sf`.

Not hypothetical: `test/pass/enums.sf` and `test/pass/generics.sf` print
subnormal doubles today (#105) and both pass. `run_tests.sh` already notes that
`test_async.sf` "was green for the entire life of BUGS #38 while emitting 2 of
its ~12 expected lines and garbage for the rest" — the mechanism was known, the
scale was not.

`tools/differential.sh --record` writes `.expected` from the reference run and
refuses when configurations disagree.

---

**LARGELY CLOSED (2026-08-02).** The blind set was re-derived independently rather
than trusting the "64" above, which predated several merges. At the time of the
work: **184 positive files — 0 with both mechanisms, 86 assertions-only, 55
`.expected`-only, 43 blind.**

After the pass: **23 still blind, and 21 of those already FAIL in the baseline**
(build-fail / timeout / segfault / runtime-error). A test that produces no output
cannot enshrine a wrong one, so those 21 are not the hazard this entry is about.
Only **2 are green-and-blind**, both deliberately.

Coverage added across 27 files, via three mechanisms: 21 `.expected`, 2 empty
`.expected` (which pin the two import helpers to printing *nothing*), 4 `.exit`
(a **new** mechanism), 5 files given assertions, and 1 carrying both. Every
recorded output was read against the source before acceptance, then cross-checked
through the oracle: **36 AGREE, 0 MISMATCH** across native-O2 / native-O0 /
wasm32.

**What was deliberately NOT recorded is the more important half.**
`test/pass/enums.sf` and `test/pass/generics.sf` print `0` and `7.29112e-304` from
`IO.println(Color.Red)` — that is #105. Recording it would have frozen a bug as
intended behaviour and made this entry worse, so each instead got 10 assertions
via *interpolation* (which inserts `.to_string()` and is correct), leaving every
`IO.println` untouched so the garbage stays visible.

`gc_coro_root_{depth,order,overpop}.sf` were also left unfrozen, and this one was
*proven* rather than argued: adding the three locals needed to capture shadow-stack
depths moved the reported value 18 → 24. The depth is frame-layout dependent, so
freezing it would flag unrelated codegen as a regression. Their *invariants* are
asserted instead — depths equal across rounds, each frame above its spawner.

**Three harness defects found and fixed, all of which were this entry's own
mechanism one layer up:**

1. **The oracle compared stdout only.** It set `RUN_EC` per config and never used
   it. The four `mini_*` tests print nothing, so all four "AGREEd" by matching two
   empty files — a wasm32 run returning 0 where native returned 55 was a unanimous
   pass. It now compares exit status too, verified by injecting `RUN_EC=7`.
2. **`output-mismatch` was missing from `run_tests.sh`'s category breakdown**, so a
   `.expected` regression counted in the total but vanished from the summary.
3. **The two grading mechanisms compose**, contrary to what "0 files with both"
   suggested: the assertion gate returns early *only on failure*, so passing
   assertions fall through to the diff. The 0 was an accident, not policy;
   `interfaces.sf` is now the first file carrying both.

Also: `test/gc_deep_test.sf` was stable over **25 consecutive runs**, so the
"nondeterministic" label it carries elsewhere in this file is doubtful and should
not be used to dismiss a failure there.

**Merge hazard worth recording here, because it makes a fix look like it did not
work:** `tools/saffron` links the **checked-in** `build/stage3/runtime.ll`
(`tools/saffron:16`), not `src/runtime/runtime.sf`. So a merge that brings a
`runtime.sf` fix without regenerating that artifact leaves the fix **inert** —
`test/pass/subnormal_not_a_pointer.sf` still segfaulted (exit 139) on a tree that
already contained its fix in source. Regenerating `build/stage3/runtime.ll` made
all 13 assertions pass. Any runtime change therefore needs the artifact rebuilt
before its tests mean anything, and a green test run against a stale artifact is
evidence of nothing.


### 65. Runtime errors are fatal, not catchable — four docs promised the opposite

`try`/`catch` works correctly for `throw`. It does **not** catch runtime faults:
`IndexError`, `DivisionError` and `NullError` all route through
`__runtime_error_fatal` (`src/runtime/runtime.sf:713`), which writes to fd 2 and
calls `rt_exit(1)`. No `catch` or `finally` block runs.

Repro — the exact example `CLAUDE.md` used to carry:

```saffron
fun main() {
    try {
        var list = [1, 2, 3]
        IO.println(list[99])
    } catch (e) {
        IO.println("caught: ${e}")
    }
    IO.println("still alive")
}
main()
```

Actual: prints `Runtime Error: IndexError: index 99 out of bounds (length 3)`,
exits 1. Neither `caught:` nor `still alive` is reached. Division by zero behaves
identically. An explicit `throw` in the same shape is caught and returns 0, which
is what made the wrong claim plausible for so long.

The fatal call sites are `runtime.sf:777` (`__index_error`), `:815`
(`__division_error`) and `:857` (`__null_pointer_error`).

Nil misuse mostly never gets this far: the checker rejects calling a method on a
nullable value at compile time.

**Docs fixed** (2026-07-30): `CLAUDE.md`, `docs/src/tutorial/error-handling.md`,
`docs/learnxinyminutes/learnsaffron.sf` and its `index.html` all claimed runtime
errors were catchable and showed output that never occurs. They now document the
fatal behaviour and show a guard-before-indexing pattern instead.

**Still open**: whether the *behaviour* should change. Making these catchable is
not a small fix — the error path itself allocates (`__runtime_error` calls
`rt_malloc`), and `setjmp`/`longjmp` are no-ops on both wasm bases
(`wasm_base_32.ll:592-597`, `wasm_base.ll:503-508`), so `try`/`catch` cannot work
on wasm at all. Decide whether fatal-by-design is the intended semantics before
investing there.

### 49. `Number` is one surface name for two representations

`str_to_type` maps `"Number"` to `IntType`, which is a lie in one direction:

```saffron
fun area(r: Number): Number { return 3.0 * r * r }
IO.println(area(2.0).to_string())   // prints 0, should be 12
```

The declared type is relabelled `Int` at the boundary, so the caller's
`.to_string()` untags a double as an integer and prints `0` (or garbage like
`37357358909038`). Enum payloads had the same defect from the other end, and that
half **is** fixed — see #45 and #43.

Mapping `"Number"` to `FloatType` instead is the honest reading of the surface
syntax and fixes the case above, but it is a worse lie in the other direction:
stdlib code writes `Number` for values it then uses as list indices and integer
counters. `src/lib/toml.sf:23`'s `fun peek_at(offset: Number)` is the clearest
case — as a double, that offset breaks indexing. Measured both ways
(2026-07-30):

| mapping | `pantry_config` | `test_sorted_set` | `area(): Number` |
|---|---|---|---|
| `Number` → `Int` | 29/29 | 33/33 | `0` (wrong) |
| `Number` → `Float` | 8 failed | `IndexError` | `12` (right) |

Neither mapping is right, because `Number` itself is the defect: one surface name
for two representations, so no single lattice entry can be correct for both uses.
This is M2 in `docs/design/compiler-rewrite.md` ("`Int` is the bottom type")
showing up as a genuine fork in the road rather than a one-line fix.

**Real fix:** retire `Number` in favour of explicit `Int` and `Float` — the
intended direction for the language. That means auditing every `Number` in
`src/lib/*.sf` and deciding per site which one is meant (`peek_at(offset: Int)`,
`number(key: String): Float`, etc.), then removing the surface spelling. Until
then `Int` is the mapping that keeps the stdlib working, and it stays, with the
trade-off documented at the mapping site.

**Correction note.** An earlier version of this file recorded `Number` → `Float`
as fixed and claimed the regression above did not exist ("`pantry_config` passes
29/29 with the Float mapping"). That measurement was taken against a `saffronc`
that did not yet contain the change, and was wrong; the regression is real and
reproduces. The mapping was reverted to `Int`.

**Progress (2026-07-30).** The retirement is underway; the mapping is unchanged
but its blast radius is shrinking:

- `src/lib/*.sf`: all 134 `Number` annotations converted. Bulk → `Int`
  (indices, counters, ports, handles, lengths, byte values, char codes);
  `scheduler.sleep_times` → `List<Float>`; `reflect.number_to_string`,
  `toml.number`/`number_or` → `Any` (genuinely int-OR-float pass-throughs that
  do no arithmetic). `toml.sf`'s `peek_at(offset: Number)` — the example cited
  above — is now `offset: Int`.

  **Correction (2026-08-01): "no `Number` annotation remains in `src/lib`" was
  false when written.** Fourteen live annotations were left in
  `src/lib/http/client.sf` and `src/lib/http/server.sf` — `var status`,
  `max_redirects`, `redirect_count`, `port`, `body_start`, `_parse_hex(): Number`,
  `_index_of_from(start: Number): Number`, `_is_redirect(status: Number)`, and
  five `var i`/`var k` loop counters. All are indices, counts or HTTP status
  codes, so all fourteen became `Int`. The five remaining hits in
  `src/lib/llvm/test_codegen.sf` are inside comments and are left alone. `src/lib`
  is now genuinely free of `Number` annotations.
- `test/*.sf`: all 152 annotations → `Int` across 49 files. The 9 `is Number`
  checks are kept deliberately: they cover the feature while it still exists.
- `checker.sf` no longer collapses `Float` into `IntType` when parsing type
  strings, so `Int` and `Float` are finally distinct in the checker. This alone
  turned two `test/fail/` cases from "compiled cleanly" (itself a failure) into
  correct rejections, taking the suite from 90/47 to 92/45.
- A latent runtime bug surfaced and was fixed on the way: `__val_is_float`
  called every raw untagged heap pointer a float, so rewriting `is Number` to
  `is Int or is Float` made `JSON.to_string({...})` serialize a Map as an
  integer. `is Number` had masked it by only ever checking the int tag — which
  also means **`is Number` returns false for every float**, so all seven stdlib
  `is Number` guards were already silently wrong. See the Fixed section.

**Remaining:** user-facing docs, then removing the surface spelling from the
lexer/parser/checker/codegen — which is a breaking change for user programs and
wants its own decision.

### 2. Forward references in nested closures

**Reproduction:**
```saffron
fun test() {
    fun a() { return b() }
    fun b() { return 42 }
    IO.print(a())
}
test()
```

**Expected:** Works (b is defined before a is called).
**Actual:** `Runtime error` — b is undefined when a's closure is compiled.

**Impact:** Can't write mutually recursive helper functions inside a parent scope.
**Note:** Design choice — compile-time local resolution. Same as Lua/Python.

### 131. wasm64's identity discipline makes every non-String value unprintable

**Severity: high**, and the remaining half of #125 — wasm64 output is no longer
*entirely* dropped, but only strings survive.

`src/runtime/values.spec:41-44` gives wasm64 `discipline=identity` (raw i64 bits) and
wasm32 `discipline=nanbox`. Under identity there is no tag to discriminate on, so
`__io_println_any` in `wasm_base.ll` can only treat its argument as a string pointer.

Measured with `scratch/w64/battery.sf` across all three targets. native and wasm32 both
print all six lines (`a string / 42 / true / false / 3 / hello world`); wasm64 prints
the two strings and **four blank lines** for `42`, `true`, `false` and `l.length()`.

Not a local patch. It needs either switching `wasm_base.ll` to the nanbox discipline
wholesale — which also means adding the GC object headers nanbox tagging assumes — or
giving identity-mode values a type-carrying header. Until then, **treat wasm64 as
string-output-only.**

Related and worth reading together: this is the same trap as #125's wrong premise. An
identity i64 bitcast to double is a denormal, and `fptosi` truncates every denormal to
0, so a nanbox helper dropped into an identity base fails silently rather than loudly.

---

### 132. wasm64's link line accepts every undefined symbol, and nothing in the tree tests wasm64 at all

**Severity: high as a process defect** — this is the mechanism that made #110, #124,
#125 and four further missing symbols silent instead of link errors. Filed as its own
entry because fixing the individual symbols does not stop the next one.

Two independent halves, both verified in the source:

**The flag divergence.** `tools/saffron:357` links wasm64 with
`-Wl,--allow-undefined`, while `tools/saffron:412` deliberately links wasm32 with
`-Wl,--import-undefined`, carrying a comment that `--allow-undefined` "silently accepts
EVERY missing symbol." `tools/build_wasm.sh:77` also passes `--allow-undefined`. So on
wasm64 an undefined symbol becomes a no-op host import: the module builds, runs, and
does nothing. Also noted while measuring: wasm32 passes `--identity-mode` for the
runtime compile (`tools/saffron:384`) and wasm64 (`:352`) does not.

Sweeping a linked wasm64 module's `*UND*` list — rather than reading `wasm_base.ll` —
found **four more** undefined symbols beyond #124's and #125's: `__string_intern`
(which alone made all string interpolation produce no output), `__print_debug_location`,
`__val_tag_ptr_nullable`, and `__builtin_trap`, the last `declare`d at
`wasm_base.ll:557` but never defined, so a trap was a silent return. All four are now
defined. **The only reliable audit of a wasm64 base's completeness is dumping the
linked module's undefined symbols, not reading the file.**

**The coverage hole.** `tools/run_tests.sh` never builds for wasm64 — its single
`wasm` mention is `NOT_A_TEST="goals hello_wasm gc_generational_test"` — and
`tools/differential.sh` sets `ALL_CONFIGS="native-O0 wasm32"`. No harness in the tree
builds or runs a wasm64 module, which is how six undefined symbols coexisted with a
green suite.

Compounding the two: **linking cannot serve as the check here.** A successful wasm64
build is compatible with the module doing nothing at all, so any wasm64 test must
execute the module and assert on stdout, treating empty output as failure.
`scratch/w64/regress_wasm64.sh` is such a check (3 cases, empty stdout is FAIL),
written to be adoptable by `run_tests.sh` unchanged.

The flag was left as-is deliberately: it may be load-bearing for legitimate host
imports (`js_log_str` is one), so tightening it is its own measured change rather than
a one-line edit.

---


### 154. An enum inside a printed collection still prints a bit pattern — PAYLOAD HALF FIXED, fieldless half open

**Severity: medium as filed; now low.** Silent wrong answer, but narrower than it
looks: only *inside* a collection. This is the residual of #105 and #115 — both
entries describe it, but it lived in a paragraph of a *resolved* entry, which is a
place nothing looks. Hence a number.

**Status 2026-08-04: the payload half is fixed** exactly as the plan below
prescribed, and the fieldless half is open and will stay open until the value
representation changes. The entry keeps its number rather than splitting, because
the two halves share one repro and one mechanism; what differs is only whether the
value is allocated. The fix is described after the original analysis, so the
reasoning that predicted it stays readable next to what it predicted.

Verified when filed (the payload lines are now correct — see the Fix section):

```saffron
enum Color { Red, Green }
enum Shape { Circle(r: Number), Rect(w: Number, h: Number) }
var c: Color = Color.Red
var s: Shape = Shape.Circle(2)
IO.println("${c}")                      // Red        — correct
IO.println("${s}")                      // Circle(2)  — correct
var cs: List<Color> = [Color.Red, Color.Green]
IO.println("${cs}")                     // [0, 7.29112e-304]        — WRONG
var ss: List<Shape> = [Shape.Circle(1), Shape.Rect(2, 3)]
IO.println("${ss}")                     // [5.21502e-310, ...]      — WRONG
```

`Red` renders as `0` rather than as garbage only because tag 0 `<< 56` *is* zero;
that is the same defect wearing a plausible-looking hat, which is worth knowing
before someone reads `0` as a working case.

**Why the direct case works and the element case does not.** #105 fixed direct
`println` by having codegen route through the enum's own `to_string()` when the
*static* type is a known enum, and interpolation was always correct because the
lexer inserts an explicit `.to_string()`. Elements are different in kind: they are
formatted by `__rt_elem_to_string` → `__any_to_string` at **runtime**, where no
static type exists. So no static-type-driven fix can ever reach them.

**The two halves are not equally fixable, and #115 is the reason we now know
what separates them.** #115 closed exactly this shape for *classes* by generating
a `__val_to_string` tag switch from codegen's tables and calling it from
`__any_to_string`. The prerequisite for joining that switch is that the value be
identifiable at runtime:

- **Payload enums can join.** They are allocated, so give them a GC header —
  `__gc_alloc` instead of `__sf_malloc` in `gen_enum_construct`
  (`expr_body.sf`) — with tags drawn from the same allocator as
  `next_class_type_id` so they cannot collide with class tags. Then
  `emit_val_to_string`'s switch gains arms calling `emit_enum_to_string`'s
  symbols. Note this is a **representation change**, not a formatting change: it
  touches every enum allocation and every place that assumes the current layout,
  which is why it did not ride along with #115.
- **Fieldless enums cannot.** `tag << 56` is a bare immediate — no allocation, no
  identity, nothing to key on. A bit-pattern heuristic was deliberately not
  written for #115 and should not be written here either: mistaking a real double
  for an enum is worse than visible garbage. Closing this half needs the value
  representation itself to change (a NaN-box tag for enums), which is a much
  larger decision than a bug fix.

So this entry is honest about being *partly* fixable. Doing the payload half alone
is legitimate and leaves the fieldless half visibly broken rather than subtly
wrong.

`test/oracle_enum_println.sf` records the current output and is the regression
test; it has shown these subnormals unchanged across both the #105 and #115 fixes,
which is how the residual stayed measured rather than assumed.

**The fix (payload half, 2026-08-04).** Three edits, in the order that matters:

1. `codegen.sf` gains `enum_type_ids: Map<String, Float>` beside `class_type_ids`,
   drawing from the **same** `next_class_type_id` counter. Sharing the counter is
   the point, not an economy: `__val_class_tag` answers with one number space, so
   an enum tag that duplicated a class tag would make `__val_to_string` call the
   wrong `to_string` — a silent wrong answer strictly worse than the bit pattern
   it replaced.
2. `emit_enum_payload_alloc` (`stmts_body.sf`) centralises the allocation choice
   that used to be three independent `__sf_malloc` call sites — two in
   `gen_enum_construct`, one in `emit_enum_constructor`. It emits `__gc_alloc`
   with the enum's tag when there is one, and falls back to the old `__sf_malloc`
   under `identity_mode` or for an unregistered enum. One helper rather than three
   edits, so the header decision cannot drift between construction paths.
3. `emit_val_to_string`'s switch gains an arm per registered enum, and
   `register_enum_tag` is called from the **prescan** (`output_body.sf`'s
   `EnumDecl` arm) as well as from `gen_enum_decl` and `register_enum_variants`.
   The prescan call is the one that is easy to omit and fatal to omit: without it
   a construction site lowered before its `enum` declaration gets no tag, which is
   the #33/#36/#37 ordering hazard.

Two asymmetries had to be respected, and each would have produced a plausible
wrong fix:

- **An enum `to_string` returns a RAW `char*`; a class `to_string` returns a
  NaN-TAGGED pointer.** The evidence is at the existing call sites — the two enum
  ones wrap the result in `__rt_tag_ptr` (`methods_body.sf:1787`, `:3653`) and the
  class ones do not. So the enum arms in `__val_to_string` must *not* apply
  `__val_untag_ptr`, which every class arm does. Applying it uniformly is the #102
  mistake mirrored.
- **The payload pointer must stay BARE.** `ensure_enum_eq` bails to "unequal"
  unless both operands' `lshr 48` is zero, so NaN-tagging the allocation would
  make every payload enum unequal to itself while printing perfectly.
  `__val_class_tag` accepts a bare pointer (`upper == 0`) precisely so a
  GC-headered value need not be tagged to be identified.

The tag is **8 bits** — `__gc_pack_info` stores `tag << 8` and `__gc_info_tag`
reads it back with `and 255` — so `register_enum_tag` refuses past 255 rather than
wrapping. Refusing degrades to the old bit-pattern print; wrapping would collide
with class tag 10 and call a wrong `to_string`. GC tracing needed no work:
`__gc_mark_drain` routes any `tag >= 10` to `trace_instance`, and slot 0 (the
small variant tag) is rejected by `__gc_is_heap_ptr`'s alignment and 4 GB floor,
so scanning `size/8` slots as values is already safe.

**wasm64 is excluded, not overlooked.** `wasm_base.ll`'s `__gc_alloc` discards its
`%type_tag` and its `__val_class_tag` returns 0 unconditionally. Native
(`base_nanbox.ll`, 24-byte header) and wasm32 (`wasm_base_32.ll`, 16-byte header)
both have real headers and both were verified end to end.

Verified: native and wasm32 both go from `[5.21502e-310, ...]` / `[0, 0, 0]` to
`[Circle(1), Rect(2, 3), Nothing]`; `==` and `match` unchanged; `./bootstrap.sh`
green through stage 2 gen4 including the zero-`Int`-fallback assertion; the suite's
failure *set* byte-identical to `test/FAILURE_BASELINE.txt`. `identity_mode` is
excluded exactly as `emit_reflect_helpers` is, so **the bootstrap never exercises
this path** — it was tested with gen3-compiled user programs, which is the only way
it could be.

`test/pass/enum_payload_any.sf` is the regression test. It asserts on the three
paths that have no static type — a list element, a map value, an `Any` parameter —
because those are the ones that converge on `__any_to_string`; asserting on
`"${Shape.Circle(1)}"` would have passed before the fix and proved nothing. Five of
its first sixteen assertions failed before the change and all sixteen pass after.

It has 23 assertions now, and the extra seven are the ones that earned their keep:
a 400-iteration GC stress loop and a recursive `Tree` formatted byte-exactly against
an independently built expected string. Those found the two rooting bugs listed
above, which the sixteen never reached — an enum whose to_string does not itself
allocate past the threshold never triggers a collection mid-format. They also found
#162, which was *not* fixed here: the recursive assertions hoist both operands into
locals, because passing them directly left argument 0 unrooted across argument 1's
allocation. That is an argument-temp rooting hole in codegen, reproducible with
classes, and it is filed separately rather than papered over silently. #162 has
since been fixed, so the hoisting in this test is no longer load-bearing — it is
left in place because a test for #154 should not depend on #162's fix holding.

**What remains open** is the fieldless half, unchanged from the analysis above, and
`test/oracle_enum_println.sf` still fails on exactly one line for it (line 22,
`[Color.Red, Color.Blue]` → `[0, 4.77831e-299]`). That test stays in the baseline
for that line, so the movement here is *inside* one name — two failing lines to one
— which an unchanged failure set does not show. Closing it needs an enum-bearing
NaN-box tag, not another formatter arm.

---
