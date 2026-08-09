# Known Bugs

## Open

**5 open entries:** #107, #131, #132, #190, #191.
Next free number is **#192**.

### 190. An escaping named nested fun captures its enclosing locals by reference (dangling at -O0)

A named nested `fun` that is RETURNED (or otherwise escapes) and reads an
enclosing local gets that local by *reference* — the env stores the address of
the enclosing stack slot — so once the enclosing frame dies the capture dangles.
At -O2 the value often survives in a register by luck; at -O0 the slot is
clobbered and the closure reads garbage. Minimal repro:

```saffron
fun make_b() {
    var inner: String = "captured"
    fun collide_b() { return inner }
    return collide_b
}
var collide_b = make_b()
IO.println(collide_b())   // -O0: 5.21502e-310 (dangling), -O2: "captured"
```

Found by the differential oracle: `test/pass/segv_name_collision_fun_vs_var.sf`
disagrees native-O2 (exit 0) vs native-O0 (exit 1, the capturing-nested-fun
assertion prints a denormal). Verified in IR: `gen_func_ref`
(`src/compiler/codegen/expr_body.sf`, ~line 1560) builds the env with
`typed_ptr_to_val("%local", "i64*")` — the slot ADDRESS — and the trampoline
(~1601) forwards it; the callee `make_b$$collide_b(i64 %inner.arg)` does
`inttoptr`+`load`. So the nested-fun capture convention is by-reference, which is
fine for an in-place call (frame alive) but dangling on escape.

Contrast the LAMBDA path (`closures_body.sf:116-143`): it COPIES the value for a
read-only capture and only stores a cell pointer for a `boxed_locals` capture
(heap cells that survive the frame, BUGS #51). `gen_func_ref` never consults
`boxed_locals`. The comment at `output_body.sf:511` ("a capture that is merely
read keeps its alloca — the env snapshot is already correct for it") is true for
lambdas but FALSE for gen_func_ref, which snapshots the address rather than the
value.

Fix direction: make an escaping named nested fun capture read-only locals by
VALUE (as the lambda path does), or extend `boxed_locals` to include locals
captured by an escaping nested-fun ref so they become heap cells. Care needed:
this is the hottest codegen path and the in-place-call case must stay working;
needs solid regression coverage across in-place vs returned, read-only vs
written, and capturing-vs-plain nested funs. Compiler change → rebootstrap.
Related: BUGS #2 (nested-closure forward refs), #51 (boxed cells), #175
(nested-fun symbol qualification).

### 191. Truthy test on a union value tests bit 0 of the NaN box (address parity)

`if (x)` where `x` is a union like `String|Nil` holding a non-nil value gives an
answer that depends on the heap address's low bit, not on truthiness. `to_i1()`
(`src/compiler/codegen/stmts_body.sf:1568-1583`) emits `trunc i64 %v to i1` for a
condition — for a `TAG_PTR | addr` value that tests bit 0 of `addr`, so the
result flips with allocator parity. A non-nil String *should* be truthy; instead
it is address-parity, and native vs wasm allocators differ, so the same program
answers differently per target.

Found by the differential oracle: `test/nullable_narrowing.sf` prints an extra
line on wasm32 vs native — case 3's `if (result3)` (result3 = a non-nil String)
fires on one target and not the other. Both are wrong; it is nondeterministic in
principle. `checker.sf:1131` `check_condition` (BUGS #143) rejects provably-non-
Bool conditions but deliberately lets unions through (comment at ~1159 assumes
`if (maybe_box)` "is also always false" — but it is address-parity for the
non-nil pointer case, not always false).

Fix options: (a) checker — extend `check_condition` to reject a union condition
whose members are all provably non-Bool, forcing an explicit `!= nil` (matches
the #143 direction; the comment already names this as the deferred step); or
(b) codegen — make `to_i1` on a non-Bool value emit a real truthiness test
(nil/false → 0, else → 1) instead of a bare `trunc`, which needs a `__val_truthy`
helper added to all four runtime bases. Compiler change → rebootstrap.

#65 (runtime faults were fatal and uncatchable — `try`/`catch` couldn't catch
IndexError/DivisionError/NullError) is now FIXED: all five recoverable runtime
errors route through a new `__rt_raise` shim that, when a `try`/`catch` handler is
active, stores the (NaN-tagged) message string as the exception value and
longjmps into it — exactly what `throw` already does — so `catch (e)` binds the
message; with no handler it falls back to the original `__runtime_error_fatal`
(print + exit). Runtime-base shim in all four `.ll` bases + the five error
builders in `runtime.sf` now call `__rt_raise`. The old "setjmp is a no-op on
wasm" caveat is stale — wasm try/catch already emits setjmp (SjLj lowering), and
the wasm bases define longjmp. Regression test
`test/pass/catchable_runtime_errors.sf`; the exact docs repro now prints
`caught: IndexError: ...` then `still alive`; uncaught faults still exit fatally;
suite 355 passed / 0 failed. Lives in `BUGS_CLOSED.md`. Open set 4 → 3.

#187 (`Reflect.type_name` returned a subnormal double instead of the class name)
and #188 (`Reflect.module_doc()` always returned nil) are both now FIXED. #187:
`__reflect_class_name` returned its string pointer via a bare `ptrtoint`
(`ptr_to_val`) at both the matched-class arm and the `"Unknown"` fallback, so
`__any_to_string` read the untagged pointer as a denormal — switched both to
`emit_tag_ptr` (the #115/#154 family). #188: the parser collected the `//!`
module doc into a discarded local; it now publishes it via a `_module_doc` global
+ `module_doc()` accessor (mirroring `_had_error`/`had_error()`), main.sf captures
it right after the entry parse, and additive codegen wrappers
(`generate_with_target_opts4` / `generate_with_modules_flat_opts5`) thread it to
`gen.module_doc_text` — no existing signature changed. Both verified end-to-end
(`Point` / the `//!` text) and by the suite (353 passed / 0 failed); live in
`BUGS_CLOSED.md`. Open set 7 → 5.

#189 (json.sf diverged native-vs-wasm32 — garbage output then an out-of-bounds
trap) is now FULLY FIXED, in two independent wasm32 raw-pointer bugs found by
binary-searching the divergence:

**Primary — `__any_length`:** `.length()` on an Any-typed receiver returned 0 on
wasm32. An Any-typed value carries a list/map as a RAW (untagged) GC pointer
(upper bits != 0x7FF8), so `__val_is_ptr` returned false and `__any_length`
short-circuited to `ret 0`, while `is List` and indexing still worked — so a list
looked present but iterated zero times. Native's `__any_length` and wasm32's own
`__val_is_list` carry a `check_raw`/magic-sentinel fallback; the wasm32 port had
dropped it. Restored. Regression test `test/pass/any_length_wasm32.sf`.

**Residual — `__val_is_float`:** with the length fix in, json.sf still diverged.
Root-caused (this line) to `__val_is_float`, which returned true for a raw
untagged GC pointer — so `Json.to_string`'s leading `value is Float` guard fired
on a class instance, took the number path, and returned a corrupt value; the
roundtrip then read out of bounds. Same missing raw-pointer probe as the primary:
when `upper == 0` and `v != 0`, check the GC magic sentinel at `v-8` (a GC object
is not a float), exactly as `__val_is_list` does. Ported native's guard.

Both are `wasm_base_32.ll` runtime-only changes (no bootstrap, native unaffected).
`tools/differential.sh test/json.sf` now agrees `native-O0 == native-O2 == wasm32`
(`test/json.expected` was already the native-verified reference); full suite 353
passed / 0 failed. Both live in `BUGS_CLOSED.md`. Open set 5 → 4.

#186 (`Task.spawn` of a bare named-coroutine reference — `Task.spawn(worker)` or
`var fn = worker; Task.spawn(fn)` — traps `null function or function signature
mismatch` on wasm32; the `fun () => worker()` lambda form works) was found by the
#184 fix subagent and is the sibling of #184: same wasm32 call_indirect signature
mismatch, but via the `gen_func_ref` trampoline rather than a leaked coro flag.
Count 6 → 7, then 7 → 6 as #185 closed below.

#185 (`__float_to_string` formatted a float to a different precision on wasm32
than native — native's `%g` gives 6 significant digits, wasm32's hand-written
formatter gave 6 *fractional* digits, so `Math.pi` printed `3.14159` natively and
`3.141593` on wasm32) is now FIXED in the fixed-notation range: the wasm32
formatter shrinks its fractional-digit budget by the integer part's width
(`6 - intdigits`), matching `%g`'s 6-significant-digits rule. `test/pass/math.sf`
now agrees native == wasm32 and has an `.expected`; regression test
`test/pass/float_format_precision.sf`. The scientific-notation tail (`|f| >= 1e6`
or `< 1e-4`, where `%g` switches to `e` notation) is a known remaining divergence,
noted in the closed entry. Lives in `BUGS_CLOSED.md`. Open set 6 → 5.

#184 (a `Task.spawn`/coroutine closure trapped `null function or function
signature mismatch` on wasm32 when the module also had earlier closures) is now
FIXED — a stale `force_next_lambda_coro` flag leaked across lambdas, miscompiling
an unrelated closure as a coroutine; the handler now only sets it for a lambda
literal. Fixed in an isolated worktree off origin/main (to avoid a colleague's
then-uncommitted #181 WIP), verified the learnxinyminutes doc runs to completion
on wasm32, moved to `BUGS_CLOSED.md`. Open set 7 → 6.

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

**2026-08-07 — fourth pass over the remaining blind tail.** Backfilled the last
deterministic feature tests (`d0a7810`): assertions on `functions`, `async`,
`docstrings`, `test_reflect`, and `.expected` on `enum_cross_simple`, `imports`,
`test_package_import`, `deep_deserialize`, `visibility_internal_flat`,
`gc_deep_test`, `json`. Every output read against source first. The pass produced
**three more wrong-output finds**, each kept *visible* (bare `IO.println`, not
frozen) with an inline comment rather than enshrined: `Reflect.type_name` returns
a subnormal double instead of the class name (**#187**), `Reflect.module_doc()`
always returns nil (**#188**), and `json.sf` diverges native-vs-wasm32 exit
status (**#189**). Confirmed correctly-excluded and untouched: the `STALE_TESTS`
set (`builtin_types`, `for_in`, `types` — aspirational syntax), `NETWORK_TESTS`,
`NOT_A_TEST` (`hello_wasm`, `goals`), the import-only helpers, and the
address-printing `gc_roots_test`. This surface has now produced **six** bugs
(function-value printing, Bool match binding, #185, #187, #188, #189).

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

