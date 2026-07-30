# Saffron / Turmeric bug log — playground build

Running log kept while implementing `saffron-playground`. One entry per bug hit.

Status legend: **FIXED** (patched in repo) / **WORKED AROUND** / **OPEN** (still blocking, avoided).

**Status of this document (2026-07-30).** Every `OPEN` entry was re-verified
against `build/saffronc` and the ones that still reproduce were filed in
`BUGS.md` as #50–#57 with the mapping below. This file is kept for the
narrative — the repros, the measurements, the workarounds each bug forced on the
playground, and the debugging notes — but `BUGS.md` is the tracker of record.
The numbering here is local to this log and does not match `BUGS.md`.

| here | `BUGS.md` | note |
|---|---|---|
| Bug 9 | #54 | Int literal in a `Float` position → `nan` |
| Bug 10 | #51 | mutable capture lost |
| Bug 12 | #50 | no virtual dispatch from an inherited method |
| Bug 16 | #52 | `list[float_var]` reads index 0 |
| Bug 17 | #53 | `UUID.v4()` constant |
| Bug 18 | #55 | `@extern` used before declaration |
| Bug 19 | #57 | repeated `--lib-path` duplicates globals |
| Bug 23 | #56 | field access on an indirect call's result reads 0 |
| Bug 26, 27 | #49 | superseded — see the correction note below |

Bugs 26 and 27 are **stale as written**: they describe the `Number` → `FloatType`
mapping as committed and actively regressing `@http/server` route dispatch. That
mapping was reverted; Bug 27's repro (`var i: Number` as a list index) now prints
`0 -> a` / `1 -> b` correctly. The durable finding — that `Number` is one surface
name for two representations and no single lattice entry is right for both uses —
is #49, which carries the measurements from both directions. The resolution taken
since is to deprecate `Number` outright in favour of the explicit `Int` and
`Float`; the entries below are kept as written, so they still spell `Number` where
the original repros did.

Bugs 24, 28, 29 and 30 are not in `BUGS.md` yet: 24 needs a fresh repro (the
colliding stdlib name may have moved), and 28–30 are playground/turmeric build
issues rather than compiler bugs.

---

## Bug 1 — `gen_logical` emits a malformed `phi` when the RHS opens new blocks — **FIXED**

**Repro:**
```saffron
fun f(a: Bool, b: Bool, c: Bool): Bool {
    if (a and (b or c)) { return true }
    return false
}
```
```
$ tools/saffron build --target wasm32 logic.sf -o logic.wasm
error: invalid LLVM IR input: PHI node entries do not match predecessors!
  %t11 = phi i1 [ %t2, %logic.entry2 ], [ %t10, %rhs3 ]
label %rhs3
label %end7
Instruction does not dominate all uses!
```

**Traced to:** `src/compiler/codegen/utils_body.sf:145-177` (`gen_logical`), mirrored in
`src/compiler/codegen/utils.sf:95-125`.

`gen_logical` opens `rhs_label`, calls `gen_arg_value(right)`, then emits a phi that
names `rhs_label` as the incoming predecessor. That is only correct when `right`
compiles to straight-line code. When `right` is itself a logical op (or a `match`,
or anything else that starts fresh blocks), `gen_arg_value` has already left
`rhs_label` behind, so control reaches `end_label` from some *other* block and the
phi's predecessor list no longer matches the CFG. The value `%t10` computed in that
other block also fails to dominate its use in the phi.

Latent, not new: Apple's system clang silently accepts the bad IR, so native builds
appeared fine while carrying 996 malformed `phi i1`s. Homebrew clang 22 — mandatory
for wasm32, since Apple clang cannot target wasm32 at all — hard-errors, which is
why this only surfaced as a wasm blocker.

**Fix:** track whether the RHS actually falls through, and funnel the fall-through
path via a dedicated single-instruction join block whose label is known at the point
the phi is emitted:

```saffron
var rhs_reaches_end: Bool = !this.block_terminated
var rhs_exit: String = this.fresh_label("rhs.exit")
if (rhs_reaches_end) {
    this.emit_terminator("br label %" + rhs_exit)
    this.start_block(rhs_exit)
    this.emit_terminator("br label %" + end_label)
}
this.start_block(end_label)
var phi_result: String = this.fresh_local()
if (rhs_reaches_end) {
    this.emit_indent(phi_result + " = phi i1 [" + lhs + ", %" + current_block + "], [" + rhs + ", %" + rhs_exit + "]")
} else {
    // RHS ended in its own terminator (e.g. throw) — end_label has one predecessor.
    this.emit_indent(phi_result + " = phi i1 [" + lhs + ", %" + current_block + "]")
}
```

The `rhs_reaches_end` branch matters: if the RHS ends in a `throw`, `end_label` has
exactly one predecessor and a two-entry phi would be wrong in the other direction.
`-O2` folds the extra block away, so there is no codegen cost.

**Verified:** `./bootstrap.sh` passes; gen3's output for `src/compiler/parser.sf`
contains 170 `phi i1`s and `llvm-as` verifies it with zero errors (gen2's checked-in
output still has the old malformed phis, as expected — gen2 was not promoted, which
is out of scope). Nested cases all compile and run correctly on both native and
wasm32:
`(a and b) or (c and d)`, `a or (b and (c or a))`, `x > 1 and (x < 10 or x == 100) and x != 5`.

This is a genuine repo-wide bug fix, not a playground workaround.

---

## Bug 2 — `IO.read_file` truncates at the first NUL byte — **WORKED AROUND**

**Repro:**
```bash
printf 'abc\x00def' > nul.bin       # 7 bytes on disk
```
```saffron
IO.println(IO.read_file("nul.bin").length().to_string())   // prints 3, want 7
IO.println(IO.file_size("nul.bin").to_string())            // prints 7 (correct)
```

Directly fatal for this project: a wasm module's magic number is `\0asm`, so the
very first byte is NUL and `IO.read_file("out.wasm")` returns the **empty string**
for every module. Observed on a real 1520-byte artifact:

```
read len=0
b64 len=0
```

**Cause:** `read_file` produces a NUL-terminated C string, so the length is
`strlen()` rather than the true byte count. `IO.file_size` uses `stat` and is
correct, which is what makes the mismatch visible. `IO.read_binary(path, buf,
max_size)` exists and takes a raw buffer, but there is no supported way to get from
that buffer to a length-carrying Saffron value — `Bytes.Buffer` has no
"adopt this pointer with this length" constructor.

**Impact beyond the playground:** any binary file read from Saffron is affected —
images, tarballs, sqlite files. `bazaar`'s publish flow sidesteps it by having the
*client* base64 the tarball, so the server only ever handles text.

**Worked around** by never letting the binary cross the language boundary: the
service shells out to `base64` and reads its stdout, which is pure ASCII.
```saffron
var r = Process.run("base64 -i " + out_path + " | tr -d '\\n'")
var encoded = r.stdout   // correct, 2028 chars for the 1520-byte module
```
Not fixed properly because the real fix is a length-carrying string/bytes
representation in the runtime — much larger than this task, and it would touch the
NaN-boxing layout.

**Follow-up worth filing:** give `Bytes.Buffer` a `from_file(path)` that uses
`file_size` + `read_binary` and records the length explicitly. That would fix the
whole class of bug without touching string representation.

---

## Bug 3 — subclasses forced to reimplement every inherited method — **FIXED**

**Repro:**
```saffron
class Animal {
    var name: String
    fun init(name: String) { this.name = name }
    fun describe(): String { return "an animal named " + this.name }
}
class Dog extends Animal {
    fun speak(): String { return "Woof" }
}
```
```
ERROR: Dog does not implement Animal.describe()
```
Even the empty case failed:
```saffron
class Base { fun greet(): String { return "hi" } }
class Child extends Base {}      // ERROR: Child does not implement Base.greet()
```
And interface **default** methods — the entire point of which is to be inherited —
were demanded too: `Circle does not implement Drawable.describe()`.

**Traced to two independent sites:**

- `src/compiler/checker.sf:635` (`check_conformance`) — required *every* parent
  method name, and compared with `.contains()` against a comma-joined string, so a
  child method `draw_border` wrongly satisfied a required `draw`.
- `src/compiler/codegen/stmts_body.sf:366` (mirrored `codegen/stmts.sf:232`) — used
  "the parent declares no fields" as a proxy for "the parent is an interface", then
  required every parent method. Interfaces and classes share one `ClassDecl` AST
  node in this compiler, so there is no structural way to ask "is this an
  interface" — but that heuristic is not it: it rejects any fieldless base class.

**Fix:** the real question is not *interface vs class*, it is *abstract vs
concrete*. A method declared with an empty body is a requirement on subclasses; a
method with a body is inherited. Added `class_abstract_methods` to both the checker
and codegen, populated only from methods whose `FunDecl` body is empty, and required
only those. Also replaced substring matching with exact name comparison via a new
`list_has_string` helper.

**Verified:** all the positive cases above now compile, including interface default
methods, and the negative case still errors correctly:
`ERROR: Bad does not implement Drawable.draw()`. Note `test/fail/inheritance_errors`
is listed as still failing ("compiled cleanly but must be an error") — that was
already failing before this change and is a *different* missing check.

---

## Bug 4 — an inherited `init` silently drops all constructor arguments — **FIXED**

**Repro:**
```saffron
class Base {
    var name: String
    fun init(name: String) { this.name = name }
    fun describe(): String { return "I am " + this.name }
}
class Child extends Base {}
fun main() {
    var c = Child("Rex")
    IO.println(c.describe())     // Segmentation fault: 11
}
```

Not a crash in `describe` — the argument never arrives. Emitted IR:
```llvm
define i64 @__saffron_entry() {
  %t2 = call i64 @Child()          ; "Rex" dropped entirely
  ...
  %t7 = call i64 @Child__describe(i64 %t6)
```
`@Child()` is the zero-arg allocator; it stores 0 into `name`, and `describe`
dereferences the null.

**Traced to:** `src/compiler/codegen/stmts_body.sf:324` (mirrored `stmts.sf:194`).
The inherited-method forwarder loop in `gen_class_decl_with_parent` read
`if (!child_methods.contains(mname) and mname != "init")` — `init` was explicitly
excluded, so no `Child__init` was ever emitted or registered. The call site at
`codegen/expr_body.sf:2042` only emits the `__init` call when the name is a known
function (`if (this.str_in_list(this.known_functions, init_name))`), so it silently
emitted nothing at all rather than failing loudly. A missing constructor call is
about the worst possible failure mode: it compiles, links, and segfaults later at an
unrelated line.

**Fix:** forward `init` like any other inherited method, with two guards. Field
indices line up because `gen_class_decl_with_parent` *prepends* parent fields to the
child's, so parent field `i` is child field `i` and every field is `i64`. The two
exceptions are excluded explicitly:

1. An **actor** inheriting from a non-actor gets two hidden fields
   (`__actor_busy`, `__actor_queue`) prepended, shifting every inherited index by
   two — do not forward there.
2. If the parent's `init` arity is unknown, no forwarder is emitted: a forwarder
   with the wrong arity is worse than none.

```saffron
var forward_method: Bool = !this.str_in_list(child_methods, mname)
if (mname == "init") {
    if (this.actor_classes.has(name) and !this.actor_classes.has(parent)) {
        forward_method = false
    }
    if (!this.called_function_arity.has(this.current_prefix + parent + "__init")) {
        forward_method = false
    }
}
```

**Verified:** the repro prints `I am Rex`. Was confirmed pre-existing before being
fixed, by bootstrapping a pristine `HEAD` worktree and reproducing the identical
segfault there with a `Base` that has *only* an `init` (so the Bug 3 conformance
change could not be implicated).

---

## Bug 5 — `is ClassName(binding)` in `match` left the binding undeclared — **FIXED**

**Repro** (`test/pass/is_match.sf`, which was passing and started failing mid-task
when someone else's commit landed):
```saffron
class Dog { fun init() {} fun bark(): String { return "Woof!" } }
class Cat { fun init() {} fun meow(): String { return "Meow!" } }
var animal = Dog()
var sound = match (animal) {
    is Dog(d) => d.bark(),
    is Cat(c) => c.meow()
}
```
```
[codegen] Error: undefined variable 'd'
```

**Traced to:** `src/compiler/codegen/match_body.sf:83` — the `enum_name == "Unknown"`
fallback. `is Dog(d)` parses to the same `VariantPattern("Dog", ["d"])` node as an
enum variant pattern, so `find_enum_for_variant("Dog")` returns `"Unknown"` and the
fallback treated the subject as a heap-allocated enum payload: it GEP'd
`subject[1]` into `d` and never registered `d` in `typed_vars`. For a class pattern
the binding *is* the subject, and there is no payload.

That produced a garbage load and an unresolvable `d`, but `d` was previously only a
*warning*, so the bad IR reached llc and the test passed by luck. Commit `a1a536e`
("Make undefined variables a codegen error, not a warning", BUGS #37) correctly
promoted it to an error, which exposed this. The underlying bug is older than that
commit — `a1a536e` is not at fault.

**Fix:** detect a class pattern with `this.class_fields.has(first_variant)` and
handle it separately — store the subject straight into the binding slot, register
the binding's type in `typed_vars`, and pick the arm whose pattern name equals the
subject's static type (falling back to a `_` wildcard arm, then to the first arm).
Compile-time arm selection is the honest choice here: the static type is all the
checker knows, and there is no runtime class-tag comparison intrinsic exposed to
codegen for this path.

**Verified:** `test/pass/is_match.sf` prints `Woof!` then `Meow!` — i.e. *both*
arms select correctly, not just the first. Applied to `match_body.sf` (the bootstrap
input) and mirrored into `match.sf`.

**Limitation worth noting:** because selection is static, this cannot yet
discriminate a genuinely dynamic subject (e.g. an `Any` holding either a `Dog` or a
`Cat`). Doing that properly needs the arm switch to compare `__gc_get_type_tag`
against `class_type_ids`, which the runtime already tracks for reflection —
a reasonable follow-up, but out of scope here.

---

## Note — two failures in the tree are NOT from this work

`test/pantry_config` and `test/pass/generics` fail in the current tree with
`use of undefined value '%x'` / `'%p'`. Both were passing at my baseline. They are
**not** caused by anything in this task — they come from another agent's in-flight,
uncommitted `is_current_param` parameter-shadowing change in
`src/compiler/codegen/expr_body.sf` (BUGS #40).

Mechanism: `generics.sf` has a module global `var x` *and* a function
`fun identity<T>(x: T)`. The new shadowing rule makes top-level code emit
`load i64, i64* %x` for the global, but at module scope there is no `%x` alloca —
only `@__g_x`, which the line immediately above correctly stores into:
```llvm
store i64 %t11, i64* @__g_x
%t12 = load i64, i64* %x      ; no such local at module scope
```
The `in_function` guard in `is_current_param` is meant to prevent exactly this, but
`in_function` is evidently still true (or `current_function_name` still resolves)
while compiling top-level statements.

Verified by isolation: a pristine `HEAD` worktree carrying **only** my two
`match*.sf` files bootstraps clean and both tests compile *and* run correctly. Left
alone deliberately — it is someone else's uncommitted work and not mine to touch.

**Net effect of this task on the suite:** 3 tests fixed
(`test_semver`, `test_sorted_collections`, `test_url` — all via the Bug 1 phi fix),
plus `pass/is_match` restored, and zero regressions attributable here.

---

## Bug 6 — `let Shape.Circle(r) = ...` was a parse error — **FIXED**

**Repro:**
```saffron
enum Shape { Circle(radius: Float), Rect(w: Float, h: Float), Point }
let Shape.Circle(radius) = Shape.Circle(10)
```
```
[line 22, col 15] Error: expected '=' but found '.'
  22 | let Shape.Circle(radius) = Shape.Circle(10)
                 ^
```
Unqualified `let Circle(radius) = ...` already worked. So the enum name had to be
omitted in the pattern while being mandatory in the expression on the right — an
asymmetry with no justification, and the qualified form is the one people reach for.

**Traced to:** `src/compiler/parser.sf:1428` (`parse_var_decl_with_doc`). It read a
single identifier and went straight to looking for `(`; a following `.` fell through
to the plain-variable path, which then demanded `=`.

**Fix:** consume any dotted prefix before the `(` test, keeping only the final
segment. Patterns resolve by *variant* name — `find_enum_for_variant` recovers the
enum — so the qualifier is redundant once parsed. A `while` loop rather than a single
step, so a module-qualified enum (`let Shapes.Shape.Circle(r) = ...`) also parses.
A dotted name not followed by `(` is now a clear diagnostic instead of silently
declaring a variable named after the last segment.

---

## Bug 7 — `[1, 2, 3].join(", ")` segfaults — **FIXED**

**Repro:**
```saffron
var d: List<Float> = [1, 2, 3]
IO.println(d.join(", "))     // Segmentation fault: 11
```
`List<String>` joined fine, which is why this survived: every existing caller
happened to join strings.

**Traced to:** `src/runtime/runtime.sf:902` (`__list_join`). The element loop did
```saffron
var re: Int = __rt_untag_ptr(elem)
if (re != 0) { __join_append(buf_p, len_p, cap_p, re, rt_strlen(re)) }
```
— valid only if the element really is a string pointer. For a boxed number it
untagged the mantissa and called `rt_strlen` on whatever address that named.
`__list_to_string` (used by `IO.println`) already formats elements correctly via
`__rt_elem_to_string`; `join` just never used it.

**Fix:** dispatch on the NaN-box tag. Only non-pointer tags route through
`__rt_elem_to_string`; anything pointer-shaped (`upper == 32760` or `upper == 0`)
keeps the original path verbatim.

**A first attempt at this fix was wrong and is worth recording.** Sending *every*
element through `__rt_elem_to_string` looks cleaner and passes the obvious tests,
but it broke the compiler's own output: interned and static string constants carry
no GC header, so `__rt_as_string_ptr` rejects them and `__any_to_string` rendered
the pointer as a decimal integer. The compiler builds IR with `join`, so gen3 began
emitting
```llvm
%t5 = call i64 @__list_push(105553161427056, 105553161427280)
```
— pointers where `i64 %t2` belonged, and every compile failed with
`error: expected type`. The tag check is what keeps headerless strings on the fast
path. Caught because the same broken build also mangled the checker's own
diagnostics: `missing variants 105553147709392, ...` instead of variant names.

---

## Bug 8 — enum `Float` payload bindings were typed as `Int`, giving `nan` — **FIXED**

**Repro:**
```saffron
enum Shape { Circle(radius: Float), Rect(w: Float, h: Float), Point }
fun area(s: Shape): Float {
    return match (s) {
        Circle(r) => 3.14159 * r * r
        Rect(w, h) => w * h
        Point => 0
    }
}
IO.println(area(Shape.Rect(3, 4)).to_string())   // nan, want 12
```
Silent wrong answers, not a crash — the worst kind. `Circle(2)` gave the correct
`12.5664` while `Rect(3, 4)` gave `nan`, so it looked variant-specific rather than
type-specific.

**Traced to:** `get_variant_field_type` in `src/compiler/codegen/match_body.sf:310`
(mirrored `match.sf:303`):
```saffron
if (ftype == "Float" or ftype == "Number") return "Int"
```
A `Float` payload field was reported as `Int`, so arithmetic on the binding emitted
integer ops against a NaN-boxed double.

**Fix:** return `"Float"`. Identity mode still needs `Int`, and
`type_to_string_for_target` already applies that lowering
(`types_body.sf:135`), so the special case here was redundant as well as wrong.

**Verified:** `Rect(3, 4)` now yields `12`.

---

## Bug 9 — an `Int` literal in a `Float` position yields `nan` — **OPEN** (pre-existing)

**Minimal repro — no enums or matching involved:**
```saffron
fun f(): Float { return 0 }
IO.println(f().to_string())     // nan, want 0
```
Writing `return 0.0` works. Found while fixing Bug 8: the `Point => 0` arm still
produced `nan` after Float bindings were corrected, and it reduces to the above.

There is no implicit Int→Float widening at a `Float`-typed return (and presumably
also at `Float` params and fields), so the integer NaN-box tag reaches
float-formatting code and reads as `nan`. Numeric literals are extremely common in
`Float` positions, so this likely misleads people well beyond this example.

**Confirmed pre-existing** at `HEAD` in a clean worktree. **Not fixed** — it belongs
in the type checker's coercion rules rather than in codegen patches, and it is wider
than this task. **Worked around** in `playground/examples/enums_match.sf` by writing
`0.0` explicitly.

---

## Bug 10 — mutation of a captured variable is lost; captures are by value — **OPEN** (pre-existing)

**Repro:**
```saffron
fun main() {
    var count: Float = 0
    var bump = fun (): Float => {
        count = count + 1
        return count
    }
    IO.println(bump().to_string())    // 0, want 1
    IO.println(bump().to_string())    // 0, want 2
    IO.println("count = ${count}")    // 0, want 2
}
```
Reading a captured variable works (`fun (n: Float) => n + base` is correct); only
*writes* are lost, in both directions — the closure never sees its own increment, and
the enclosing scope never sees the write.

**Traced to:** `src/compiler/codegen/output_body.sf:26-85`. Nested functions are
hoisted to top level with their free variables appended as ordinary `i64`
**by-value** parameters (`find_free_vars_stmts` → `full_params`). A write inside the
body therefore assigns to the callee's own copy. Real mutable capture needs boxed
cells — captures passed as pointers, with the enclosing frame's variable promoted to
a heap cell — which the comment at line 26 hints at ("capture POINTERS") but the
code does not do.

**Confirmed pre-existing** at `HEAD` in a clean worktree. Not fixed: mutable capture
is a design change to the closure ABI, not a patch, and it is far outside this task.

**Worked around** in `playground/examples/closures.sf`: the counter example was
replaced with by-value capture and higher-order functions, which work correctly. This
is the one place a bundled example had to be shaped around a language limitation
rather than showcasing what the docs describe — `CLAUDE.md` shows a mutable-counter
closure as a supported pattern, so **the documentation is currently wrong**.

---

## Bug 11 — operator overloading skipped when an operand is not a named variable — **FIXED**

**Repro:**
```saffron
class Vec2 {
    var x: Float
    var y: Float
    fun init(x: Float, y: Float) { this.x = x; this.y = y }
    fun add(other: Vec2): Vec2 { return Vec2(this.x + other.x, this.y + other.y) }
    fun to_string(): String { return "(${this.x}, ${this.y})" }
}

var sum = Vec2(1.0, 2.0) + Vec2(3.0, 4.0)
IO.println(sum.to_string())     // "10469048808", want "(4, 6)"
```
With an explicit `var sum: Vec2 = ...` annotation it segfaults instead. Naming the
operands first (`var a = Vec2(1.0, 2.0); var b = Vec2(3.0, 4.0); var s = a + b`)
works correctly — which is what made this look like a NaN-boxing problem rather than
a dispatch problem.

The IR is unambiguous: `Vec2__add` is emitted but never called, and the two
*pointers* are fed to an integer add.
```llvm
%t5  = call i64 @Vec2__init(i64 %t2, i64 %t3, i64 %t4)
%t9  = call i64 @Vec2__init(i64 %t6, i64 %t7, i64 %t8)
%t10 = call i64 @__val_untag_int(i64 %t2)
%t11 = call i64 @__val_untag_int(i64 %t6)
%t12 = add i64 %t10, %t11
```

**Traced to:** `src/compiler/codegen/expr_body.sf:602-612`, `gen_binary`. Overload
dispatch was gated on
```saffron
var left_name: String = this.get_variable_name(left)
if (left_name.length() > 0) { left_class = this.get_var_type_str(left_name) }
```
and `get_variable_name` (`src/compiler/codegen/methods_body.sf:72-82`) returns `""`
for anything that is not a bare `Variable` — including `Call(c, a)`. So a
constructor-call operand resolved no class, dispatch was skipped silently, and
control fell through to the primitive integer path. This also affected `this.pos + 1`
style field operands and any method result.

**Fixed** by falling back to `get_expr_type(left)` when `get_variable_name` yields
nothing. `get_expr_type` (`methods_body.sf:113`, `call` arm at line 284) already
resolves a call's static type through `func_ret_types`, returning `Vec2` for
`Vec2(...)`, so no new inference machinery was needed. Mirrored into the extend-fun
variant `src/compiler/codegen/expr.sf:249`.

Bootstrap clean; the repro now prints `(4, 6)`. **Confirmed pre-existing at HEAD**
before the fix.

---

## Bug 12 — overridden methods are not virtually dispatched from an inherited method — **OPEN**

**Repro:**
```saffron
class Animal {
    var name: String
    fun init(name: String) { this.name = name }
    fun speak(): String { return "..." }
    fun describe(): String { return "${this.name} says ${this.speak()}" }
}
class Dog extends Animal {
    fun speak(): String { return "Woof" }
}
var d = Dog("Rex")
IO.println(d.speak())      // "Woof"          -- correct
IO.println(d.describe())   // "Rex says ..."  -- WRONG, want "Rex says Woof"
```
A direct call on a statically-known `Dog` dispatches correctly. The failure is
specifically a *self-call from inside an inherited method*: `Dog__describe` is
emitted as a forwarder to `Animal__describe`, whose body calls `Animal__speak` by
static name, so `Dog`'s override is invisible. The same applies through a
`List<Animal>` holding `Dog`s.

Method calls are lowered to `<StaticType>__<method>` — there is no vtable and no
runtime dispatch on the object's class tag. This is a **design limitation, not a
patch-sized bug**: it is the classic dynamic-dispatch hole, and fixing it means
either re-emitting inherited method bodies per subclass (fixes the static-type case
only) or introducing real vtables (fixes both, much larger).

**Worked around** in `playground/examples/classes.sf`: the example calls the
overridden method directly rather than through an inherited wrapper, so it
demonstrates inheritance without depending on virtual dispatch. Note that `CLAUDE.md`
advertises polymorphic `speak()` overriding as a supported feature, so **the
documentation oversells what works** here too.

---
## Bug 13 — every float prints truncated on wasm32 — **FIXED**

```saffron
IO.println((3.5).to_string())     // wasm32: "3"       native: "3.5"
IO.println("${12.5664}")          // wasm32: "12"      native: "12.5664"
```

`__float_to_string` in `src/runtime/wasm_base_32.ll:891` was implemented as
`fptosi` followed by `__int_to_string` — i.e. it simply **truncated**. Every float
on a wasm target silently lost its fractional part, through `to_string()`, string
interpolation and `__any_to_string` alike. The native runtimes format with
`snprintf("%g")`, but `snprintf` is only a stub on wasm32
(`wasm_base_32.ll:392`), so nothing was producing the digits.

**Fixed** by hand-rolling a formatter in `wasm_base_32.ll`: integer part via the
existing `__wasm_uint_to_str`, then up to 6 fractional digits, extracted by
scaling the fraction to an integer **once** and dividing exactly. Generating the
digits one at a time by repeated multiply-and-truncate accumulates binary
representation error and turns `12.5664` into `12.566399`. Trailing zeros are
trimmed so whole numbers still print as `3`, and a carry case (`.9999995` and up)
rounds into the integer part.

Known divergence from `%g`, deliberately accepted and commented in the source:
this is 6 decimal *places*, not 6 significant digits, and there is no exponent
form. So `1.2345e-05` prints as `0.000012`, and `12.5664` as `12.56636` where
native says `12.5664`.

---

## Bug 14 — the wasm32 driver compiled the runtime without `--identity-mode` — **FIXED**

```saffron
IO.println([3.0, 6.0].join(", "))   // wasm32: ", "    native: "3, 6"
```

`runtime.sf` is the layer that *implements* NaN-boxing, so it must be compiled
with values treated as raw i64 bits — that is what `--identity-mode` is for, and
`bootstrap.sh` has always passed it for the native runtime. The wasm32 branch of
`tools/saffron` did not. Every tag inspection inside the runtime was therefore
operating on doubly-tagged values.

The mechanism behind the visible symptom is worth recording, because it is a
wasm-only divergence: `elem >> 48` in `__list_join` compiled to a call to
`__val_untag_int`, and on wasm32 (`wasm_base_32.ll:642`) that function has a
`from_float` path that converts a float to its integer **value** via `fptosi`. So
`3.0` became `3`, `3 >> 48` became `0`, and `upper == 0` is exactly the
"plausible bare heap pointer" case in the tag dispatch — the float was then
dereferenced as a string pointer.

**Fixed** in `tools/saffron` by adding `--identity-mode --stdlib ...` to the
wasm32 runtime compile, matching what bootstrap.sh does for native.

A first attempt at this fix was aimed at the wrong layer and is worth recording as
a dead end: adding a `__val_tag_bits` extern to the four base `.ll` runtimes and
calling it from `runtime.sf` does nothing, because the extern call boundary passes
arguments through `__val_untag_int` *first* — the emitted IR reads
`%t11 = call i64 @__val_untag_int(i64 %t10)` and only then
`%t12 = call i64 @__val_tag_bits(i64 %t11)`. Reverted.

---

## Bug 15 — the JS glue's Proxy catch-all returns a BigInt for f64 imports — **WORKED AROUND**

`turmeric/runtime/app_template.js:230` (and `bazaar/static/app.js:230`) install:

```js
{ get(t, p) { return t[p] || ((...args) => 0n); } }
```

The catch-all exists so that a missing import never breaks instantiation, which
is the right instinct — but it returns `0n`, a **BigInt**, for *every* missing
import. Any import declared to return `f64` therefore throws
`Cannot convert a BigInt value to a number` the moment it is called. In practice
this hits `fmod` (any float `%`, so fizzbuzz) and `js_time_now` (anything
scheduler-driven, so every async program) — and because the throw happens inside
`_start`, the symptom is a module that instantiates and then dies with a type
error, giving no hint that an import was missing.

**Worked around** in `playground/frontend/public/app.js` by supplying real typed
implementations for `fmod` and `js_time_now`, and — for the user's module — by
walking `WebAssembly.Module.imports(mod)` and installing a plain `() => 0` (a
Number, not a BigInt) for anything unanticipated. The shared template in
`turmeric/runtime/` still has the defect; it is not this task's file to change,
but it should get the same treatment.

---

## Bug 16 — `list[float_var]` silently reads index 0 — **OPEN** (pre-existing)

Minimal repro:

```saffron
var chars = ["a", "b", "c", "d"]
var f: Float = 2.0
IO.println(chars[f])   // prints "a" — want "c"
var i: Int = 2
IO.println(chars[i])   // prints "c" — correct
```

Indexing a list with a value whose static type is `Float` always yields element
0. No warning, no error. The index path emits `__val_untag_int` on a
float-tagged value, so the double's bit pattern is reinterpreted as an integer
rather than converted; for small values the low bits are all zero, which lands on
index 0.

This is the same underlying confusion as Bug 14 but on the *native* target and
reachable from ordinary user code, which makes it considerably more dangerous:
`.floor()` is the workaround, and nothing tells you that you need it.

Found via Bug 17 below, which is a live instance of it in the shipped stdlib.

---

## Bug 17 — `UUID.v4()` always returns the all-zero UUID — **OPEN** (pre-existing, stdlib)

```saffron
import "@uuid" as UUID
IO.println(UUID.v4())   // 00000000-0000-4000-8000-000000000000, every time
```

Not random at all. `src/lib/uuid.sf` builds the string with
`_to_hex(Random.int(0, 15))`, and `_to_hex` indexes a 16-element list of hex
digits. `Random.int` is declared to return `Float` (`src/lib/random.sf:13`), so
every one of those indexes hits Bug 16 and returns `"0"`. Calling
`Random.int(0, 15)` directly returns properly random values — the corruption is
entirely in the list-index step.

Worth flagging beyond the playground: any code trusting `UUID.v4()` for
uniqueness — request IDs, temp file names, database keys — is silently getting a
constant. `playground/src/compile.sf` used it to isolate per-request build
directories, which would have made concurrent compiles collide on the same path.
**Worked around** there by combining a monotonic counter with the clock instead.

`Random.choice` and `Random.shuffle` index by `Float` the same way and are
presumably also affected.

---

## Bug 18 — an `@extern` used before its declaration links against the wrong symbol — **WORKED AROUND**

```saffron
fun sync() { set_prop(el, "value", "x") }        // used here...

@extern("void js_dom_set_property(i64, void*, void*)")
fun set_prop(handle: Float, prop: String, value: String)   // ...declared here
```

The call is emitted as `call i64 @set_prop(...)` — the *Saffron-level* name, which
nothing defines — instead of the extern target `@js_dom_set_property`. The
`declare` for the extern is emitted correctly, so the file compiles without
complaint and fails only at link time with
`use of undefined value '@set_prop'`. Worse, the string arguments are lowered as
`i64 0` on that path, so even a coincidentally-matching symbol would be called
with null pointers.

Declaration order should not matter for a top-level `fun` — it does not for
ordinary functions — so this is an ordering dependency specific to `@extern`
resolution. **Worked around** by declaring all externs above first use in
`playground/frontend/src/main.sf`.

---

## Bug 19 — a repeated `--lib-path` duplicates every global in the output IR — **WORKED AROUND**

```
saffronc --target wasm32 --lib-path .pantry/packages \
                         --lib-path "$PWD/.pantry/packages" src/main.sf out.ll
# error: redefinition of global '@__g_turmeric_prelude__tc_event'
```

Passing the same package directory twice — once relative, once absolute, as
happens naturally when a caller adds `--lib-path` and the driver's own
auto-discovery in `tools/saffron:149-158` adds it again — makes the compiler emit
each module's globals twice. The dedupe compares the path strings literally, so
two spellings of one directory are treated as two packages. The failure surfaces
as an LLVM redefinition error mentioning an internal symbol, which gives no hint
that the real problem is a duplicated flag.

**Worked around** by letting the driver discover `.pantry/packages` on its own and
not passing `--lib-path` explicitly. A real fix should canonicalise paths before
the comparison.

---

## Bug 20 — `Async.await` does not exist, but the compiler accepts it — **NOT A BUG IN async.sf; DIAGNOSTIC BUG**

```saffron
import "@async" as Async
var task = Task.spawn(fun () => work())
var result = Async.await(task)   // compiles; fails at link
```

`src/lib/async.sf` has `sleep`, `gather` and `race`, but no `await` — awaiting is
the method `task.await()`. The interesting part is that referencing a
**nonexistent member of a known module** is not a compile error: it is deferred
all the way to `Undefined symbols: _stdlib_async_await` from the system linker.

Any typo in a qualified stdlib call therefore produces a mangled-symbol link
error instead of "module @async has no member 'await'". Note `CLAUDE.md` itself
documents `Async.await(task)` in its async example, so the docs are wrong here
too. **Fixed** in `playground/examples/async.sf` by using `task.await()`.

---
## Bug 21 — `return nil` from a *capturing* closure returns integer 0, not nil — **FIXED**

The one that cost the most to find, and the most severe: it silently corrupts a
value and then segfaults somewhere else entirely.

```saffron
class Box { var v: Int
    fun init(v: Int) { this.v = v } }

fun make_capturing(dir: String): Fun {
    return fun (x: Int): Box {
        if (dir == "never") { return Box(1) }
        return nil                       // <-- compiles to `ret i64 0`
    }
}
fun make_plain(): Fun {
    return fun (x: Int): Box { return nil }   // correct: `ret i64 @__val_nil()`
}

var r1 = make_capturing("static")(1)
IO.println((r1 == nil).to_string())   // false   <-- WRONG
IO.println((make_plain()(1) == nil).to_string())  // true
```

Under NaN boxing `nil` is `0x7FF8000000000002`, not `0`. So the returned value
was neither nil nor a valid pointer: `x == nil` was **false for a value that was
nil**, and the first field access on it dereferenced address `0x28`.

Traced to `src/compiler/codegen/closures_body.sf:213` (`gen_closure_function`).
The mechanism is an overloaded flag:

- `NilLit` in `expr_body.sf:57-64` emits the literal `"0"` when
  `this.identity_mode or !this.in_function`, and `call i64 @__val_nil()`
  otherwise. The `"0"` branch is correct for a module-level initialiser, where no
  runtime call can be emitted yet.
- `gen_lambda` (`closures_body.sf:73-74`) deliberately sets
  `in_function = false` before generating the lambda body — that is what stops
  `gen_function` from treating the lambda as a *nested* function and hoisting it.
- But `gen_function` sets `in_function = true` for its own body
  (`output_body.sf:179`), while **`gen_closure_function` never did**. So only the
  capturing path — the one that goes through `gen_closure_function` — generated
  its body with the flag still false, and every `nil` inside it became `0`.

That is exactly mechanism **M4** in `docs/design/compiler-rewrite.md` ("implicit,
unrestored compiler state"): one field encoding two different questions, with no
save/restore discipline.

**Fixed** by saving, setting `in_function = true`, and restoring it in
`gen_closure_function`, mirroring what `gen_function` already does.

### Why this mattered far beyond a nil check

This is the root cause of the `/api/*` segfault that blocked the playground
backend all session. `src/lib/http/server.sf:626` builds its `static_files`
middleware as a closure capturing `url_prefix` and `dir`, and returns `nil` to
mean "I did not handle this request, fall through to the routes". `App._handle`
(`server.sf:443-451`) then does:

```saffron
resp = this._run_middlewares(req)
if (resp == nil) { resp = this._route(req) }
if (!resp._is_stream) { ... }     // <-- dereferences 0x28
```

The `resp == nil` test was false, so the router never ran, and `resp._is_stream`
dereferenced integer `0`. Confirmed under lldb:

```
stop reason = EXC_BAD_ACCESS (code=1, address=0x28)
frame #0: stdlib_http_server_App___handle + 1964
    ldr x0, [x8, #0x28]
frame #1: stdlib_http_server___lambda_781 + 84
frame #2: stdlib_http_server_App__serve.resume + 88
```

So **any** server using `static_files` alongside API routes died on the first API
request. Note the failure is not specific to the playground: it affects every
`@http/server` user that combines static files with routes, and more generally
every closure in the tree that captures a variable and returns nil. `bazaar/`
uses the same pattern.

The bisection was long because the symptom pointed at the wrong layer. Sequence
of wrong theories, recorded because each cost real time: (1) `examples.sf` was at
fault — no, it worked standalone; (2) the handler pattern was at fault — no, an
isolated server with the same handler worked; (3) `/api/examples` specifically —
no, `/api/health` died identically, which is what finally showed it was not about
the payload at all; (4) `IO.file_exists` or `Path.join` misbehaving inside a
closure — no, both were fine, and the trace showed the middleware ran to
completion and returned. Only after reducing to "capturing closure returning nil"
outside HTTP entirely did the IR make it obvious.

---

## Bug 22 — `var x: Any = nil` types the variable `Nil` forever, so `x == nil` is always true — **FIXED**

```saffron
var a: Any = nil
a = 99
IO.println(a.to_string())        // 99      — the value IS there
IO.println((a == nil).to_string())   // true    <-- WRONG
IO.println((a == 99).to_string())    // false   <-- WRONG

var b: Any = nil
b = "hello"
IO.println(b.to_string())        // 4296082291  <-- raw pointer, not "hello"
```

Two defects compounding, both in codegen:

1. **`gen_var_decl_with_name` (`stmts_body.sf:142`)** overwrites an explicit
   `Any` annotation with the initialiser's inferred type. For `= nil` that means
   the variable is typed `Nil`, and nothing ever widens it on reassignment. The
   explicit annotation the programmer wrote is discarded.
2. **The `== nil` comparison (`expr_body.sf:655-678`)** decided *which operand to
   test* from the inferred types: `var check_val = lhs; if (left_type == "Nil") { check_val = rhs }`.
   When the variable is itself typed `Nil`, both sides are `Nil`, and it ended up
   emitting `__val_is_nil` against **the nil literal** rather than the variable —
   unconditionally true, whatever the variable actually held.

The `b = "hello"` line shows the second-order damage: with the variable typed
`Nil`, `to_string()` dispatched on the wrong static type and printed the pointer
as an integer.

**Fixed** both: `gen_var_decl_with_name` no longer lets a nil initialiser narrow
to `Nil` (falls back to `Any`, which routes through the runtime dispatch helpers
that inspect the real tag), and the nil comparison now keys off `is_nil_expr` —
which side is *syntactically* the literal `nil` — instead of the inferred type,
with the type test kept only as a fallback when neither side is a literal.

This is invariant **I2** in `docs/design/compiler-rewrite.md` ("`Unknown` is
distinct, and `Any` is the honest answer") applied to the one case where the
current tree spells "I don't know yet" as a concrete type.

### Impact

This is what broke `/api/compile`. `playground/src/main.sf:37` had the completely
ordinary shape:

```saffron
var parsed: Any = nil
try { parsed = JSON.parse(body) } catch (e) { ... }
if (parsed == nil or !parsed.has("source")) { ... }   // always taken
```

so every well-formed request was rejected as `missing 'source' field`. It looked
like a `try`-scoping bug for a while — the assignment appeared to be "lost" —
which is why `test/l2.sf`-style probes on `Int` and `String` (both fine) were
needed to show that the trigger was the `= nil` initialiser and not the `try` at
all. `declare-nil-then-assign` is a very common idiom, so this likely mis-compiled
in a lot of places quietly.

---

## Bug 23 — a field access on the result of an indirect call reads 0 — **OPEN** (pre-existing)

Found while verifying Bug 21; a genuinely separate defect, not fixed.

```saffron
class Box { var v: Int
    fun init(v: Int) { this.v = v } }
fun mk(): Fun { return fun (x: Int): Box { return Box(7) } }

var f: Fun = mk()
var typed: Box = f(1)
IO.println(typed.v.to_string())   // 7   — correct
IO.println(f(1).v.to_string())    // 0   <-- WRONG
var untyped = f(1)
IO.println(untyped.v.to_string()) // 0   <-- WRONG
```

Binding the call result to a variable with an explicit `: Box` annotation is
correct; accessing the field directly on the call expression, or through an
inferred variable, reads 0. So the object is constructed fine and the receiver is
fine — the field *offset* resolution is what fails, because the static type of an
indirect call's result is not recovered (`Fun` carries no return type), and
codegen falls back to offset 0 rather than reporting that it does not know.

Mechanism **M1**/**M2** from `docs/design/compiler-rewrite.md`: codegen
re-deriving a type it does not have, and spelling "unknown" as something
concrete. Not fixed here because the real fix is to give `Fun` a return type in
the type system, which is a language change, not a codegen patch. **Workaround:
always annotate a variable holding the result of an indirect call.**

---

## Bug 24 — a local function name collides with an imported module's function of the same name — **OPEN** (pre-existing)

Found while bisecting Bug 21.

```saffron
import "@http/server" as Http
fun mw(dir: String): Fun { ... }        // one parameter
fun main() {
    var app = Http.server(8080)
    app.use(mw("static"))               // one argument
}
```
```
[codegen] Error: 'mw' expects 1 arguments, got 2
```

`src/lib/http/server.sf` has its own internal function whose mangled name
collides with the local `mw`, and the arity table is keyed by unqualified name,
so the *other* `mw`'s arity is checked against this call. Renaming to `middleware`
makes it compile unchanged. The error is at least loud rather than silent, but it
names the user's function while describing a stdlib one.

This is invariant **I4** in `docs/design/compiler-rewrite.md` — names resolved
once into a `DefId` table instead of string-keyed lookups (`called_function_arity`
here). **Worked around** by renaming.

---
## Bug 25 — an enum `Number` payload stores whichever representation the *argument* had, so no static type for the extracted binding is correct — **FIXED**

Minimal repro:

```saffron
enum Option { Some(value: Number), None }
let Some(a) = Option.Some(7)
IO.println(a.to_string())     // printed: nan     expected: 7

let Some(b) = Option.Some(7.5)
IO.println(b.to_string())     // printed: 7.5     correct
```

The declared field type is `Number` in both cases, but the *bits* differ:
`Some(7)` stored a TAG_INT and `Some(7.5)` stored a raw double. The declared type
therefore did not describe the representation, which makes the binding produced by
`let Some(v) = ...` unreadable either way — the extraction site has exactly one
static type to choose and both choices are wrong half the time:

- typing it `Int` (the original behaviour) made `Circle(2.0) => 3.14159*r*r`
  emit integer ops against a NaN-boxed double, evaluating to `nan`;
- typing it `Float` fixed that but made `Option.Some(7)` print `nan`.

I hit the second form as a *self-inflicted* regression: an earlier edit of mine to
`get_variant_field_type` (`codegen/match_body.sf:314`, `Float or Number => "Float"`)
traded the first symptom for the second. Exit codes were identical, so the test
suite reported success — it was caught only by diffing the *stdout* of all 114
tests against a HEAD baseline, where `test/test_expr_features.sf` line 5 had
changed from `7` to `nan`. Worth recording: an exit-code-only regression gate would
have shipped this.

Fixed at the *construction* site instead, so the declared type becomes true:
`gen_enum_construct` (`codegen/expr_body.sf:2833`) now widens an Int-tagged
argument to a double when the field is declared `Float`/`Number`, reusing the
untag/tag round trip already used for BUGS #28 (`var f: Float = 1`). With
construction normalised, `get_variant_field_type`'s `Float` is correct for every
construction, and both repro forms print correctly. Gated on `!identity_mode`,
where Float genuinely *is* Int.

This also fixed two cases that were **already broken at HEAD**:
`area(Shape.Circle(2))` and `area(Shape.Rect(2.5, 4))`.

---

## Bug 26 — `Number` maps to `IntType`, so a `Number`-returning function relabels a double as an integer — **OPEN; a fix for this is currently COMMITTED AND BREAKING `@http/server`**

Minimal repro:

```saffron
enum Shape { Circle(r: Number), Rect(w: Number, h: Number) }
fun area(s: Shape): Number {
    return match (s) {
        Circle(r) => 3.14159 * r * r
        Rect(w, h) => w * h
    }
}
IO.println(area(Shape.Circle(2.0)).to_string())
// printed: 37357358909038     expected: 12.5664
```

Note the same expression evaluated *inline* (no function wrapper) prints
`12.5664` correctly. The corruption is introduced at the function boundary:
`str_to_type` (`src/compiler/codegen/types_body.sf:37`) maps the surface type
`Number` to `IntType`, so the declared return type says "Int" for a value that is
a double, and the caller's `.to_string()` untags a double as an integer.

This is mechanism **M2** in `docs/design/compiler-rewrite.md` — "`Int` is the
bottom type" — at the point where surface syntax enters the type lattice.

**Attempted fix, reverted.** Mapping `Number` to `FloatType` is the honest reading
and does fix this repro (all four shape cases print correctly), and it bootstraps
cleanly — identity mode is unaffected because `type_to_string` collapses Float
back to `"Int"` there, so the compiler's own 455 `Float`/`Number` sites keep the
exact strings they had. But it regresses three tests:

```
test/pantry_config.sf    exit 0 -> 1   (8 of 29 assertions fail)
test/test_sorted_set.sf  exit 0 -> 1   (IndexError: index 0 out of bounds)
test/test_base64.sf      exit 1 -> 139 (segfault)
```

The cause is that stdlib code writes `Number` for values it then uses as **list
indices and integer counters**, and those paths silently depend on the Int
mapping. So the one-line change is not viable on its own: fixing this properly
means distinguishing "integral" from "numeric" across the stdlib, which is a
larger change than this task should carry. **Reverted**, with the constraint
recorded as a comment at the mapping site so the next person does not repeat the
experiment blind.

---

## Bug 27 — `var i: Number` used as a list index yields a `nan` index and reads the wrong element — **OPEN, ACTIVE REGRESSION on `main`**

This is the fallout of the `Number` -> `FloatType` mapping that is currently
committed as `9f59563` ("Number is a Float, not an Int, at the surface-to-type
boundary"). It is the concrete mechanism behind the three test regressions listed
under Bug 26, and it breaks `@http/server` route dispatch.

Minimal repro:

```saffron
class Item { var name: String
  fun init(n: String) { this.name = n } }
var items: List<Item> = [Item("a"), Item("b")]
var i: Number = 0
while (i < items.length()) {
  var it: Item = items[i]
  IO.println(i.to_string() + " -> " + it.name)
  i = i + 1
}
```

Current `main`:
```
nan -> a
1 -> a
```
Expected (and what the pre-change baseline prints):
```
0 -> a
1 -> b
```

Two distinct symptoms: the index *prints* as `nan`, and `items[1]` reads element
`a` — the wrong element, silently. A loop written this way does not fail, it
quietly processes the wrong data.

**Impact — this is the important part.** `src/lib/http/server.sf:500` writes its
route-matching loop counter as `var i: Number = 0` and uses it to index
`this.routes`. So `App._route` cannot find routes any more. Observable effect:

```saffron
var app = Http.server(8123)
app.post("/api/compile", hc)
app.get("/api/health", h)
```
`POST /api/compile` returns `{"c":1}` correctly, but `GET /api/health` returns
`404 Not Found`. Whichever route the broken index happens to land on is the only
one reachable; the others are simply gone. Every Saffron HTTP server with more
than one route is affected, including the playground and `bazaar/`.

**Debugging note worth recording, because it cost real time.** While chasing this
I twice believed I had reproduced a 404 that was not actually the playground's:
`tools/saffron` execs a temp binary (`/tmp/saffron_build_*/program`), so
`pkill -f 'saffron run ...'` does not match it and old servers survive and keep
their ports. One probe was answered by a *different developer's* `node
tests/server.js` on port 8091. The tell was the capitalisation: `@http/server`
returns `"Not Found"` (`server.sf:175`), while the stray node server returned
lowercase `"not found"`. Always check `lsof -ti:<port>` and confirm the listener
is yours before trusting the response — and confirm the server logged
`Listening`, not `failed to bind`.

**Not fixed by me.** The mapping change is the user's commit, and reverting it is
their call — I had reverted it earlier in this session after measuring the same
three regressions, and the commit reinstated it. Options: revert `9f59563`, or
keep it and change the stdlib's integral loop counters (`server.sf:500`,
`server.sf:511`, and the `Number` counters in the sorted-set / pantry / base64
paths) from `Number` to `Int`.

---
## Bug 28 — the HTTP server segfaults on a large request body that arrives *after* a large response — **OPEN** (playground-visible, in `@http/server`)

Deterministic repro, against `playground/src/main.sf`:

```bash
# 1. a normal compile, which produces a LARGE response body (~9KB of base64 wasm)
curl -s -X POST -H 'Content-Type: application/json' \
     -d '{"source":"IO.println(\"hi\")"}' http://127.0.0.1:PORT/api/compile
# -> {"ok":true,"wasm":"AGFzbQ..."}   server alive

# 2. now a large REQUEST body (70KB, rejected by the size cap)
curl -s -X POST -H 'Content-Type: application/json' \
     -d @big.json http://127.0.0.1:PORT/api/compile
# -> empty response;  server is GONE
```

```
../tools/saffron: line 400: 16745 Segmentation fault: 11  "$TMPBIN" "$@"
```

The ordering is what matters, and it is why this took a few passes to pin down:

- three consecutive 70KB requests on a *fresh* server all succeed (the size cap
  returns `program too large: 70003 bytes (limit 64000)` each time, server stays
  up);
- a 70KB request immediately after a successful compile **always** crashes.

So it is not the request size alone and not the cap logic — the cap works and
returns the right JSON. It looks like buffer state left over from writing the
previous large response being reused for the next large read. I did not get as far
as a specific file:line in `src/lib/http/server.sf`.

Not a security hole by itself (it is a crash, not a code path an attacker
controls), but it is a trivially reachable remote denial-of-service for any Saffron
HTTP server: two curl commands take the process down. Worth noting alongside bug
27 as a reason `@http/server` is not yet ready for untrusted traffic.

**Not worked around.** The playground's own size cap is what surfaces it, but the
defect is in the server library, not the playground.

---
## Bug 29 — a relative `--lib-path` makes the Turmeric prelude compile twice: `redefinition of global` — **OPEN** (blocks the frontend build)

Repro, from `playground/frontend`:

```bash
# relative lib path — FAILS
saffron build --target wasm32 --lib-path .pantry/packages src/main.sf -o /tmp/a.wasm
#   opt: output.ll:1327:1: error: redefinition of global '@__g_turmeric_prelude__tc_event'
#   saffron: this is a compiler bug, not an error in your program.

# same command, same cwd, absolute lib path — SUCCEEDS
saffron build --target wasm32 --lib-path "$PWD/.pantry/packages" src/main.sf -o /tmp/b.wasm
```

The emitted IR contains the entire Turmeric prelude global block twice:

```llvm
@__g_turmeric_prelude__tc_event = global i64 0      ; line 1322
...
@__g_turmeric_prelude__tc_event = global i64 0      ; line 1327
```

Cause: `src/compiler/main.sf` dedupes modules with a `visited: Map<String, Bool>`
keyed on the **path string** (`main.sf:547`, `main.sf:853`). The auto-imported
package prelude is registered under `lib_dir + "/" + pkg_name + "/src/prelude.sf"`
(`main.sf:852`) — so with `--lib-path .pantry/packages` the key is the *relative*
`.pantry/packages/turmeric/src/prelude.sf`, while the explicit
`import { ... } from "turmeric/prelude"` resolves to the **absolute** realpath
(the packages entry is a symlink to `/Users/willemhs/personal/saffron/turmeric`).
Two different strings, same file, so `visited.has(...)` misses and the prelude is
parsed and emitted a second time.

Two things make this expensive to diagnose, both worth recording:

1. It is **cwd- and spelling-dependent, not source-dependent.** The identical
   `src/main.sf` builds cleanly from a scratch directory and fails from
   `frontend/`. I lost time bisecting the *source* and the import list when the
   variable was the lib-path spelling.
2. A **bogus** `--lib-path` also "succeeds", because then nothing resolves at all.
   So "it built" is not evidence the path was right — check that the wasm was
   actually produced.

I also wrongly suspected the stray `basil` symlink in
`frontend/.pantry/packages/` (basil's `pantry.toml` does depend on turmeric, so a
second resolution route was a plausible story). Removing it changed nothing; the
link is undeclared in the frontend's `pantry.toml`/`pantry.lock` but harmless. It
has been restored.

Correct fix: canonicalise paths (realpath) before using them as `visited` keys, so
one file has one identity regardless of how it was reached. This is invariant **I4**
in `docs/design/compiler-rewrite.md` — resolve names once into a stable identity
instead of string-keyed lookups — applied to module paths rather than value names.

**Workaround:** pass an absolute `--lib-path`. Note `turmeric/tools/build.sf:40`
hard-codes the *relative* form, so `turmeric-build` hits this for any app whose
prelude is reached through a symlink:

```saffron
var compile_cmd: String = "saffron build --target wasm32 --lib-path .pantry/packages " + entry + ...
```

---
## Bug 30 — the wasm32 link drops `js_*` extern calls that are only reachable from JS-invoked callbacks, so the UI renders but cannot fetch or run — **OPEN**

The playground frontend compiles and links cleanly, renders, and exports all its
callbacks — but the produced module does not *import* `js_fetch_post` or
`js_run_wasm`, so pressing Run can never reach the compile service.

Observed on `playground/static/app.wasm`:

```
IMPORTS (7):  js_log_str, js_dom_set_text, js_dom_set_attr, js_dom_add_event,
              js_dom_create_element, js_dom_append_child, js_dom_set_inner_html
EXPORTS:      memory, malloc, __sched_pump, _start,
              turmeric_prelude___dispatch_event, turmeric_prelude___on_timeout,
              api___on_fetch_complete, api___on_run_complete, __on_output_changed
```

`js_fetch_post`, `js_run_wasm`, `js_fetch_json`, `js_get_hash_source` and
`js_set_hash_source` are all missing from the imports. Both sides of the contract
are otherwise correct: `static/app.js:295` supplies `js_fetch_post` and resolves
the callback by suffix (`_findExport('__on_fetch_complete')`, `app.js:378`), and
the callbacks *are* exported.

The IR is fine — the calls are present before linking:

```bash
build/saffronc --target wasm32 --lib-path "$PWD/.pantry/packages" src/main.sf /tmp/fe_full.ll
grep -c 'call .*@js_fetch_post' /tmp/fe_full.ll      # 1
grep -n 'declare void @js_fetch_post' /tmp/fe_full.ll # 1315
```

with a live call chain: `run_program` (`:29862`) → `api_compile` (`:28669`) →
`call void @js_fetch_post` (`:28729`). But none of those functions survive into the
module — the wasm name section contains `_start`, `malloc`, `__sched_pump` and
`api___on_fetch_complete`, while `run_program`, `api_compile` and `api__register`
are all absent.

So the chain is dropped at link time. The entry points that reach it are
`__on_*`/`__dispatch_*` callbacks invoked *from JS*, and `tools/saffron:352-362`
exports those with `--export-if-defined` **after** `-O2` has already run over the
IR; anything reachable only from a not-yet-known-live export looks dead. The
`js_*` import disappears with it. `_start` alone does not reach `run_program`,
because that path only opens when the user clicks Run.

Consequence: any Turmeric app whose host FFI is reachable only from an event
handler loses that FFI silently — it links, loads and renders, and the feature is
simply inert. There is no diagnostic.

Likely fix: compute the export list *before* optimisation and pass it so the
optimiser treats those functions as roots (or link the callback-reachable set with
`--no-gc-sections` / an explicit `llvm.used`-style anchor). I did not implement
this — it is a change to the link pipeline in `tools/saffron`, and the frontend
needs it verified in a real browser, which is beyond what I can check here.

**Status: the playground's compile service is fully working and verified
end-to-end; the Turmeric UI renders but its Run button cannot reach the service
until this is fixed.** The service can be driven directly in the meantime:

```bash
curl -s -X POST -H 'Content-Type: application/json' \
  -d '{"source":"IO.println(\"hi\")"}' http://127.0.0.1:8080/api/compile
```

---
