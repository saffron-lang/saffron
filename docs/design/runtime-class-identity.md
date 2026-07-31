# Runtime class identity: one mechanism for `is`, `match`, and virtual dispatch

Status: **design, not yet implemented.** Written 2026-07-30 after verifying BUGS
#50 and #61 against `build/saffronc`.

## Why these are one bug

`#61` (`x is SomeClass` silently answers false) and `#50` (an overridden method is
not dispatched from an inherited method) read as unrelated — one is a type test,
one is method dispatch. They are the same defect: **codegen has no model of the
class hierarchy at runtime.** Fix that once and both follow, along with two things
that are currently impossible rather than merely broken.

Everything a correct answer needs is *already computed*; it is thrown away:

| What exists | Where | What is missing |
|---|---|---|
| Per-class type tag, unique, ≥10 | `class_type_ids`, `codegen.sf:75` | nothing — already baked into every `__gc_alloc` (`stmts_body.sf:629`) |
| Tag read from an object header | `__gc_get_type_tag`, `gc.ll:1214` | nothing — but only `__reflect_*` ever calls it |
| The parent of the class being lowered | **parameter** `parent` of `gen_class_decl_with_parent`, `stmts_body.sf:313` | it is used for field/method inheritance and then **discarded** |
| Parent chain at type-check time | `class_parents`, `checker.sf:163` | no equivalent on `Codegen` |

That fourth row is the crux. It is tempting to conclude codegen cannot know the
hierarchy — the previous investigation of #50 nearly did, and `class_parents`
living only in the checker supports that reading. But `gen_class_decl_with_parent`
is *handed* the parent name. Codegen knows the full hierarchy at lowering time and
simply never writes it down.

## The two real constraints

Neither of these is about missing information.

**1. Emission order.** `Animal__intro` is emitted before `Dog__speak` (IR lines 202
and 234 in the #50 repro), so when a parent method body is lowered the compiler
does not yet know which subclasses will override what. Any fix that needs
whole-program knowledge cannot run during method-body lowering.

**2. Method ASTs are discarded.** `class_methods` (`codegen.sf:32`) stores method
*name strings*. So "re-emit the inherited body per subclass" is not available
without a larger change to what codegen retains.

The way past both: **emit the hierarchy as data, and dispatch through it at
runtime.** Data can be emitted after all classes are known; it needs no method
ASTs and no re-lowering. There is already a precedent for the timing —
`emit_reflect_helpers` (`stmts_body.sf:1430`) runs from `output_body.sf:693`, at
the very end, and switches over the complete `class_type_ids` table.

## Foundation: a parent table, recorded then emitted

Two pieces, both small.

```saffron
// codegen.sf, beside class_type_ids
var class_parent_of: Map<String, String>

// stmts_body.sf:313, in gen_class_decl_with_parent — the parent is already here
if (parent.length() > 0) { this.class_parent_of.set(name, parent) }
```

Then, alongside the reflect helpers so it sees every class, emit one runtime
predicate answering "is tag `t` this class or any ancestor of it":

```llvm
; __class_is_a(tag, target) -> i1
; A switch per class walking to its parent tag. Flattening the chain at emit
; time into a "tag -> set of ancestor tags" table also works and is O(1);
; the walk is written here because it is easier to read and depth is small.
```

Costs: one `Map` field, one `set` call, one emitted helper. No new runtime C, no
`gc.ll` change, no change to what codegen retains.

## What that foundation buys

### `is` on a class becomes a real check (#61)

Both branches of `gen_is_check` change, and the second is the one that matters:

- `expr_body.sf:486-489` — the undecidable branch (`Any`, a union) currently emits
  `add i64 0, 0` plus a warning. Emit `__class_is_a(__gc_get_type_tag(val), <tag>)`.
- `expr_body.sf:508` — the *static* branch currently folds via `is_class_type`.
  **This must also go.** Keeping it leaves the flow-insensitive wrong answers
  in #61: `var z: Any = pick(true); z is Dog` folds to false while `z` holds a
  `Dog`, because the checker cannot infer `z`. A fix confined to the `Any` branch
  looks complete and is not.

Folding is only safe when the static type is *exactly* the class and cannot be a
subclass. That is a narrow enough case that emitting the runtime check
unconditionally is the better default; let LLVM fold what it can prove.

`is`-pattern `match` inherits the fix, which is what `CLAUDE.md` already
advertises. Note the measured behaviour today is that *both* arms fall through to
`_` (BUGS #61 as corrected), not that the first arm wins.

### Virtual dispatch becomes possible (#50)

With `__class_is_a` and per-class tags, a method call site that cannot be resolved
statically can dispatch on the receiver's actual tag instead of its static type.
The thin forwarder at `stmts_body.sf:368-399` — today literally `%r = call i64
@Animal__intro` — becomes a tag switch over the classes that actually override the
method.

This is strictly more than #50 asks for and should be **staged separately**, after
`is` lands. Reasons to keep it apart: it changes every method call site, it
interacts with the `#70` dispatch-preamble hazard (`methods_body.sf:1633`
unconditionally evaluates the receiver before dispatch is decided), and `#50`'s
own repro is fixed by the narrower "self-call inside an inherited method" case.

### Two things that are currently impossible

- **`d is Animal` for a `Dog`** — needs the parent chain, so it cannot work at all
  today regardless of the fold. `__class_is_a` is exactly this.
- **`match` on a class hierarchy** — same mechanism.

## Order of work

1. **Foundation** — `class_parent_of` + `__class_is_a`. No behaviour change, so it
   lands safely on its own and is independently testable via `__reflect_*`.
2. **`is` fixed** (#61) — both branches. Fully fixes #61 including the `z` case.
3. **Virtual dispatch** (#50) — separate, larger, wants its own decision.

Steps 1–2 touch `codegen.sf`, `stmts_body.sf`, and `expr_body.sf`.

## What this does *not* fix

- Generic type parameters. `T` has no tag; `x is T` stays unanswerable.
- Interface conformance as a runtime test, unless interfaces are also given tags.
- `#50`'s `List<Animal>` case needs step 3, not step 2.

## The recurring shape

`#61`, `#69`, and `#22`/`#40` are all the same failure mode: **a lookup that cannot
say "I don't know" returns a plausible wrong answer instead.** `gen_is_check`
answers `false` where it means "not computable here"; `is_class_type` answers
`false` for a union; `_find_in_lib_paths` returns a guessed path. `false` is a
legitimate value for a type test, so nothing downstream can tell the difference —
which is why every symptom in this family is silent. The fix in each case is to
make the mechanism total rather than to widen a whitelist, and that is the reason
to prefer the hierarchy table over adding another special case to `gen_is_check`.
