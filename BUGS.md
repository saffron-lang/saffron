# Known Bugs

## Open

**7 open entries:** #49, #65, #107, #131, #132, #184, #185.
Next free number is **#186**.

#185 (`__float_to_string` formats a float to a DIFFERENT precision on wasm32 than
on native — native's `%g` gives 6 significant digits, wasm32's hand-written
formatter gives 6 *fractional* digits, so `Math.pi` prints `3.14159` natively and
`3.141593` on wasm32) was found by `tools/differential.sh --record` refusing to
freeze `test/pass/math.sf`: native-O0 and native-O2 agree, but native != wasm32.
Same program, different number — a real cross-target inconsistency. Filed from the
#107 surface (math.sf asserts nothing). Full entry below. Open set 6 → 7.

#184 (a `Task.spawn`/coroutine closure traps with `null function or function
signature mismatch` on wasm32 when the module also contains a large amount of
earlier closure/function-defining code — works natively, and works on wasm32 in
isolation) blocks the learnxinyminutes doc from running to completion in the
playground. #184 arrived as #181 was closed upstream in the same window, so the
open count nets to 6.

#183 (a `store8`/`load8` intrinsic called inside a nested/private function was
`$$`-qualified by #175's nested-fun naming and emitted as an undefined function
call — broke ALL of `@net`/`@ssl`) was found rebuilding the playground service and
is now FIXED: `nested_fun_symbol` no longer qualifies intrinsic names. Regression
from #175. Filed and closed in the same window; lives in `BUGS_CLOSED.md`. Open set
7 → 6.

#178 (an all-scalar overload set was rejected by the checker — every arm's first
parameter a scalar, so `scalar_mismatch` compared `show(42)` to the last-registered
`show(s: String)` and rejected a call codegen resolves fine) was fixed by teaching
the checker to recognise a genuine overload set. It counts free-function
definitions per `current_prefix + name` — scoped per file, exactly like codegen's
`detect_overload_sets` — and a bare name reaching 2+ in one module enters
`overload_names`; the free-function call site then skips the single-signature arg
check for those. The stdlib's `parse`/`from` (declared once per file across many
files) never enter, so the #155/#165 class-mismatch guard on that path stays intact
for every non-overloaded call. Regression test
`test/pass/overload_all_scalar.sf`. Lives in `BUGS_CLOSED.md`. Open set 7 → 6.

#175 (same-named nested funcs in different parent functions collided on an
unqualified symbol and silently returned the wrong body) was fixed by qualifying
a nested fun's emitted symbol by its top-level ancestor everywhere it is used —
definition, preregistration, sibling FunDecl metadata, call resolution,
nested-fun-as-value, undefined-var exemption, and (the transitive-capture path
that surfaced late) `closures_body.sf` free-var lookup — from one
`nested_fun_symbol` helper. Regression tests
(`test/pass/nested_fun_name_collision.sf`, `nested_forward_ref.sf`) pin the
collision + forward-ref + capture combinations. Lives in `BUGS_CLOSED.md`. Open
set 8 → 7; next-free 182 → 183 (183 not yet claimed).

#154 (an enum inside a printed collection printed a bit pattern) is now FULLY
FIXED — the fieldless half landed via a new NaN-box tag TAG_ENUM (0x7FFB), so a
fieldless variant is a runtime-identifiable immediate that `__val_to_string`
routes to `Enum__to_string`. Both halves closed; moved to `BUGS_CLOSED.md`. Open
set 9 → 8 upstream, then 8 → 7 here after #175 closed.

#182 (a match binding of a generic-type-parameter payload field was typed `Int`,
so a String payload printed a raw bit pattern under `"${msg}"`) was found running
the learnxinyminutes doc's `Result` example — the same example `main` had just
embedded in the playground, which would have shipped garbage output. Fixed at the
type source: `get_variant_field_type` now resolves a type-parameter field to `Any`
(runtime-dispatched) rather than the `Int` fallback, so every consumer is correct
at once. It was written as #181, which `main` had taken for the coroutine-linking
entry below, so it renumbered to #182. Lives in `BUGS_CLOSED.md`. Open set
unchanged; next-free 182 → 183.

#181 (a program that uses coroutines without importing `@async` fails to link —
the scheduler symbols codegen emits are undefined) is now FIXED via an ambient
scheduler, matching Elixir/BEAM (and Go/Swift): the coroutine runtime links like
`runtime.ll`/`gc.ll` rather than being a source module the user must import.
`main.sf` always collects `@scheduler` when the entry emits a program (gated by a
new `_entry_emits_program()` so `runtime.sf` compiling standalone doesn't
double-define the scheduler globals), and codegen emits `__mod_init_stdlib_scheduler`
only when `scheduler_used` is set — so a coroutine-free program is byte-identical
to before, zero cost. The two unsound attempts that preceded it (a `yield`/`Task.spawn`
text-scan that matched the compiler's own comments → dup symbols; collecting
`@scheduler` as an ordinary source module → prelude interfaces dragged into
`class_type_ids`, emitting reflect helpers the identity-mode `base.ll` can't link)
are why the fix waited for the lazy-init + entry-gate shape. `test/async.sf`
(top-level `yield`, no `import "@async"`) now links and runs; full suite 350
passed / 0 failed. Regenerated gen3+gen4 artifacts; STAGE 2 fixed-point green with
0 inference fallbacks. Never given a `### 181.` body (filed as header prose only),
so nothing moves to `BUGS_CLOSED.md`. Open set unchanged at 6.

#180 (plain assignment to a function-local `var` shadowing a module global wrote
the global, not the local — and a shadowed loop counter hung forever) was found
by a subagent writing #59's regression test: its reassignment draft hung, which
isolated the gap #59 left. Fixed on sight — the `Assign` arm now honours
`current_fn_locals` like the declaration store already did — and lives in
`BUGS_CLOSED.md`. Open set unchanged; next-free 180 → 181.

#179 (wasm32 `__sched_pump` returned a NaN-boxed value so the JS scheduler loop
never terminated — async programs produced correct output then hung at the
20000-step cap) was found via the playground and fixed on sight; it lives in
`BUGS_CLOSED.md`. Open set unchanged.

#178 (an all-scalar overload set is rejected by the checker) was filed while
adding a scalar-vs-scalar case to `test/pass/overloading.sf`: `main`'s
codegen-only overloading resolves the variant correctly, but the checker's
single-signature `scalar_mismatch` rejects the call first. A non-scalar arm
escapes it, so type-based overloading works otherwise. It was written as #176,
which `main` had already taken for the C-for-var bug below, so it renumbered to
#178. Count 9 → 10, next-free 178 → 179.

#176 (a C-style `for (i = 0; ...)` reusing an `i` declared earlier by `var i`
emits invalid IR — `use of undefined value '%i'`) and #177 (an unknown type name
in an annotation or signature is silently accepted, no checker error) were both
found running the learnxinyminutes doc through the playground. Count 7 → 9.

#2 (forward references in nested funcs) was in this list and is now closed as
**FIXED** — it was never the runtime error / design limitation it claimed. The
symbol always linked; the only defect was a false "calling undefined function"
warning, now suppressed by pre-registering sibling nested-fun names, and the
genuinely-undefined case became a hard compile error instead of a linker failure.
Moved to `BUGS_CLOSED.md`.

#75 (a value re-entering wasm32 from JS was untagged, so it never matched a Map
key) was closed upstream in the same window — an exported function's numeric
params are now re-tagged at the JS boundary — and arrives merged, already under
`BUGS_CLOSED.md`.

#175 (same-named nested funcs in different parents collide on an unqualified
symbol and silently return the wrong body) was found while fixing #2 and filed
separately: it is a nested-fun *naming* defect, not a forward-reference one, and
reproduces without any recursion. Added to the set above. Net: #2 and #75 left
(one fixed here, one upstream), #175 arrived, so 8 → 7; next-free 175 → 176.

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

### 185. OPEN — `__float_to_string` uses a different precision on wasm32 than native (6 fractional digits vs `%g`'s 6 significant digits)

```saffron
import "math" as Math
IO.println(Math.pi)   // native: 3.14159    wasm32: 3.141593
```

**Severity: medium** — the same program prints a different number depending on the
target. Not garbage, but a silent cross-target inconsistency: any float whose
integer part is nonzero renders with a different digit count on wasm32.

**Root cause.** The two runtime bases format a float differently:

- `base_nanbox.ll` (native) calls C's `snprintf(buf, 32, "%g", f)`. `%g` prints
  **6 significant digits total**, so `3.14159265…` → `3.14159` (1 integer digit +
  5 fractional).
- `wasm_base_32.ll` has a hand-written `__float_to_string` (there is no libc
  `snprintf` on wasm32 — its `snprintf` shim just `strcpy`s the format string).
  It emits the integer part in full via `__wasm_uint_to_str`, then scales the
  fraction by `1e6` and emits **6 fractional digits**, trimming trailing zeros.
  So `3.14159265…` → `3.141593` (6 fractional digits, independent of the integer
  part's width).

Found by `tools/differential.sh --record` refusing to freeze `test/pass/math.sf`:
`native-O2 == native-O0` but `native-O2 != wasm32` (`3.14159` vs `3.141593`). The
determinism/cross-config gate is exactly the mechanism meant to catch this — it is
why `math.sf` has no `.expected` while its siblings now do.

**Fix direction.** Make the wasm32 formatter match `%g`'s "6 significant digits"
convention (count the integer part's digits and reduce the fractional digit budget
accordingly, or reproduce `%g`'s shortest-round-trip-ish rule). Whichever base is
chosen as canonical, both must agree; the native `%g` output is the reference the
rest of the suite's `.expected` files were recorded against, so wasm32 should move
to match native. wasm64 (`wasm_base.ll`) prints nothing for floats at all (#131),
so it is out of scope until that is addressed.

---

### 184. OPEN — a `Task.spawn`/coroutine closure traps `null function or function signature mismatch` on wasm32 when the module also has many earlier closures

**Severity: high** for real programs — it silently kills execution partway through
(no diagnostic, no crash trace, just a wasm trap). Found running the
learnxinyminutes doc in the playground: it compiles, prints ~30 correct lines,
then traps the moment execution reaches the `Task.spawn`/`.await()` section.

**wasm32 only.** The identical program runs correctly and to completion **natively**.
Each part runs fine on wasm32 in isolation — the async section alone works, the
first ~560 lines alone work. The trap needs BOTH: a `Task.spawn`/coroutine closure
AND a large amount of earlier closure/function-defining code in the same module.

**Repro** (deterministic): take the async section (a `fetch` coroutine +
`Task.spawn(fun () => fetch(...))` + `.await()`) and prepend the first ~300 lines
of `docs/learnxinyminutes/learnsaffron.sf` (many lambdas, nested funs, methods).
Build `--target wasm32` and run: correct output through the prepended code, then
`THREW: null function or function signature mismatch` at the first `.await()`.
Standalone async section, or a smaller prefix, does not trap. Native never traps.

**Likely cause.** `null function or function signature mismatch` is a wasm
`call_indirect` against a function-table slot whose stored index is wrong or whose
signature does not match the call site. `Task.spawn(fun () => ...)` stores a
closure/coroutine function pointer into the wasm indirect-call table; something
about that index or its type signature goes wrong once the table is large / has
many closures registered ahead of it. Suspect the wasm32 codegen for closure /
coroutine function-pointer emission and the `call_indirect` type index — a
collision, an off-by-one in table slot assignment, or a signature-index mismatch
that only manifests past some table size. Native uses real function pointers (no
typed indirect-call table), which is why it is wasm32-specific.

**Investigation aids.** `/tmp/combo.sf` (head[:560] + async section) reproduces;
so does head[:300] + async. Compare the emitted wasm's `call_indirect` type index
and the `elem`/table entry for the spawned closure against a working standalone
build. The trap is at the `.await()` / coroutine-drive path, so also check how the
coroutine frame's resume function pointer is registered vs called.

**Severity: medium.** A program that uses a coroutine — a top-level `yield`, or
`Task.spawn` — but does not `import "@async"` (which transitively imports
`@scheduler`) fails to link: codegen emits calls to `@stdlib_scheduler_enqueue`
and `@stdlib_scheduler_scheduler_run` whenever the entry is a coroutine, but with
no scheduler module in the link those symbols are undefined. `test/async.sf` is
the standing repro — it `yield`s at top level with no async import and fails as
`invalid-ir` (opt rejects the undefined symbol). Workaround: add `import "@async"
as Async`.

**Target design: Elixir/BEAM parity — the scheduler is ambient runtime, always
linked, never opted into.** That removes the whole detection question (which is a
nest of edge cases: the coroutine use can be in the ENTRY or ANY imported lib,
directly or via an aliased `Task` — `import "@async" as A` then `A.spawn` — and a
naive `source.contains("yield")` also matches the word in a comment).

**Why the obvious fixes fail (measured, not assumed):**

1. *Text-scan the source for `yield`/`Task.spawn` and conditionally collect
   `@scheduler`.* Rejected: the compiler's OWN source mentions `Task.spawn` in
   comments, so the scan fires while compiling the compiler, injects `@scheduler`
   into its build, and the bootstrap fails with duplicate `stdlib_scheduler_*`
   symbols. A text scan cannot tell code from a comment.

2. *Always collect `@scheduler` into the module set (unconditionally, via
   `collect_modules`).* Rejected empirically: collecting it as a source module has
   a second-order effect — it drags the prelude's interface types into
   `class_type_ids`, which makes codegen emit the full reflect-helper suite
   (`__reflect_class_name`/`is_class`/`get_fields`/`construct_from_map`), and those
   reference `__val_class_tag`. The `[TEST]` example in `bootstrap.sh` links with
   `base.ll` (the identity-mode base), which does not define `__val_class_tag`, so
   the link fails. The coupling is: module-collection → class registration →
   reflect emission → a runtime symbol the bootstrap base lacks.

**The clean fix is a build-system change, not a source patch.** Treat the
scheduler the way `runtime.ll`/`gc.ll`/`base_nanbox.ll` are treated: compile
`src/lib/scheduler.sf` once to `build/stage3/scheduler.ll` in `bootstrap.sh`, and
add that `.ll` to every link line (native + wasm32 + wasm64 in `tools/saffron`,
and the bootstrap's own links). Then `main.sf` must STOP collecting `@scheduler`
as a source module — when a program `import`s `@async`→`@scheduler`, the import
resolves to symbols already present in the linked `.ll`, so collecting the source
too would double-define them. That short-circuit in import resolution is the
subtle half: 9 files import `@scheduler` today (`async.sf`, `net.sf`, and 7 tests)
and every one becomes a validation surface.

Scope: `bootstrap.sh` (compile + link scheduler.ll, four bases), `tools/saffron`
(three target link lines), `main.sf` (exclude `@scheduler` from collection). Each
of the 9 importers must be re-verified, plus the bootstrap's own build (which must
NOT pull the reflect-emission coupling described above). Left open deliberately:
a half-finished version breaks the bootstrap, and the investigation that produced
this entry is the argument for doing it as one planned build-system change rather
than a source hack.

**Progress 2026-08-06: the FOUNDATION landed (commit `bf95a54`).** Dedup-safe
symbols — `Class__method` (including the prelude's interface methods like
`Comparable__lt`) and the reflect helpers — now emit `linkonce_odr` on native, so
two units that both pull the auto-imported prelude no longer collide on them.
Verified: bootstrap green, suite 342/2 unchanged, and a two-unit link of those
symbols succeeds where strong `define` failed. This resolves the *first* class of
duplicate the always-link approach hit (the prelude/reflect dup in analysis point
2 above). See `docs/design/build-and-linking.md`.

**What still blocks the always-link, discovered by that work:** three
whole-program hierarchy helpers are emitted unconditionally with bodies that
switch on THIS unit's entire class set — `__class_parent_tag`, `__class_is_a`
(`stmts_body.sf:2009/2093`) and `__val_to_string` (`:2203`) — plus per-module
globals and `__mod_init_*`. These are NOT `linkonce_odr` candidates the way
`Class__method` is: two units emit *different* bodies under the same name, so
ODR-merging would silently drop one unit's classes. The scheduler unit declares
no classes, so its versions are empty switches — the open question is whether an
empty-switch `__val_to_string`/`__class_is_a` from `scheduler.ll` can coexist with
a user program's full one (needs either: emit these only in the entry unit, or
give the runtime-linked scheduler.ll a mode that suppresses them). That decision
is the next step, and it is genuinely separate-compilation ABI design, not a patch.

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

**2026-08-07 — a real bug surfaced by this surface and fixed.** Running the
assertion-free feature tests and eyeballing their output found `test/functions.sf`
printing `5.21502e-310...` for `IO.print(areWeHavingItYet)` — a *function value*
reaching a formatter printed its raw closure-pair pointer, the same #115/#154
family (a value with no runtime-identifiable type hitting `__any_to_string`). Now
fixed: closures allocate through `__gc_alloc(16, 4)` (GC tag 4, the tag `gc.ll`'s
`trace_closure` already expected), `gen_func_ref`/`gen_lambda` type a function
value as `Any`, and `__any_to_string` prints `<function>` for tag 4 — so lists and
maps recurse into it (`[<function>, <function>]`, `{f: <function>}`) for free.
Regression test `test/pass/function_value_printing.sf` asserts the exact rendered
strings across named refs, stored vars, capturing lambdas, lists and maps.

**2026-08-07 — a second find from the same surface.** `test/oracle_stringify.sf`
(no assertions) printed `matched OB 0` for `match (OneBool.OB(false)) { OB(v) =>
"matched OB ${v}" }` — a `Bool` payload bound in a match arm formatted through
`__int_to_string` and rendered `0`/`1` instead of `false`/`true`. Root cause:
`get_variant_field_type` had an explicit `if (ftype == "Bool") return "Int"`, the
same Int-collapse family as #182 (generic-type-param fields). Now returns `"Bool"`
so the binding routes through `__bool_to_string`; the value is a NaN-boxed bool in
both modes. Regression test `test/pass/match_binding_bool.sf` (bound Bool renders
false/true, is usable in a condition, and formats correctly beside Int/String
fields). This entry stays open: the broader hygiene problem (positive tests
asserting nothing) is what let both hide, and the tail of assertion-free tests
remains.

**2026-08-07 — third find, plus hygiene.** Recorded `.expected` files (via
`tools/differential.sh --record`) for five verified-clean assertion-free tests
(`oracle_stringify`, `iterators`, `inheritance`, `varargs`, `data_equality`), so
their output is now frozen against silent rot — the actual repair this entry
tracks. The recording gate ALSO surfaced a real bug: it refused `test/pass/math.sf`
because `Math.pi` prints `3.14159` on native but `3.141593` on wasm32 — a float
formatter precision divergence, filed as #185. So this surface produced three
codegen/runtime bugs (function-value printing, Bool match binding, #185's float
formatting) plus five newly-frozen oracles.

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

