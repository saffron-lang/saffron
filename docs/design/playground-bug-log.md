# Saffron / Turmeric bug log — playground build

Running log kept while implementing `saffron-playground`. One entry per bug hit.

Status legend: **FIXED** (patched in repo) / **WORKED AROUND** / **OPEN** (still blocking, avoided).

**Status of this document (2026-07-30).** Every `OPEN` entry was re-verified
against `build/saffronc` and the ones that still reproduce were filed in
`BUGS.md` as #50–#57 with the mapping below. A later pass the same day added
entries 31–34, revised Bug 28 with its actual root cause (the GC, not the request
parser), added entry 35, and filed the newly reproducing ones as #60–#64. A final
pass added entries 36–39, from getting the Run button working end to end; #75 is
the one new compiler bug out of that. This file is kept for the narrative — the
repros, the measurements, the workarounds each bug forced on the playground, and
the debugging notes — but `BUGS.md` is the tracker of record. The numbering here
is local to this log and does not match `BUGS.md`.

**The playground now works end to end** as of the last entry: page loads, examples
load, Run compiles and executes with correct output. Entries 36–39 are what stood
between the state the earlier entries describe and that.

| here | `BUGS.md` | note |
|---|---|---|
| Bug 2 | #66 | NUL truncation — filed as the general defect after it resurfaced serving `/app.wasm` |
| Bug 9 | #54 | Int literal in a `Float` position → `nan` |
| Bug 10 | #51 | mutable capture lost |
| Bug 12 | #50 | no virtual dispatch from an inherited method |
| Bug 16 | #52 | `list[float_var]` reads index 0 |
| Bug 17 | #53 | `UUID.v4()` constant — **fixed** in `b75689b` |
| Bug 18 | #55 | `@extern` used before declaration |
| Bug 19 | #57 | repeated `--lib-path` duplicates globals |
| Bug 23 | #56 | field access on an indirect call's result reads 0 |
| Bug 26, 27 | #49 | superseded — see the correction note below |
| Bug 28 | #63 | **root cause found after the migration** — GC vs coroutine frames, not the request parser |
| Bug 31 | #60 | `for (var i = 1; ...)` is a parse error |
| Bug 32 | #62 | `for (entry in map)` segfaults; iterator protocol never used |
| Bug 33 | #50 | same bug as Bug 12 — inherited `this.foo()` binds to the base |
| Bug 34 | #61 | `x is SomeClass` folded to `false` when the static type is `Any` |
| Bug 35 | #64 | >35 KB body kills the server; the source size cap is unreachable |
| Bug 36 | #75 | **new** — a value re-entering wasm from JS is untagged, so it never matches its own `Map` key |
| Bug 37 | — | not a compiler bug: string `+` in a loop is quadratic and blew the wasm heap on an 11 KB field |
| Bug 38 | #66 | third instance of the NUL truncation — worked around for the UI module too |
| Bug 39 | #63 | the GC bug measured at 23 requests, not 85; `__gc_disable()` applied after all |

Bugs 26 and 27 are **stale as written**: they describe the `Number` → `FloatType`
mapping as committed and actively regressing `@http/server` route dispatch. That
mapping was reverted; Bug 27's repro (`var i: Number` as a list index) now prints
`0 -> a` / `1 -> b` correctly. The durable finding — that `Number` is one surface
name for two representations and no single lattice entry is right for both uses —
is #49, which carries the measurements from both directions. The resolution taken
since is to deprecate `Number` outright in favour of the explicit `Int` and
`Float`; the entries below are kept as written, so they still spell `Number` where
the original repros did.

Bugs 24, 29 and 30 are not in `BUGS.md` yet: 24 needs a fresh repro (the
colliding stdlib name may have moved), and 29–30 are playground/turmeric build
issues rather than compiler bugs. Bug 28 *was* in that group until it was
root-caused — it turned out to be a GC bug, not a request-parser bug, and is now
filed as #63.

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

## Bug 31 — `for (var i = 1; ...)` is a parse error; a C-style loop variable cannot be inferred — **OPEN** (paper cut)

Every other binding form in the language accepts `var x = expr` and infers the
type. The C-style `for` header is the one place that does not: it demands an
explicit annotation *and* rejects the `var` keyword entirely.

Minimal repro:

```saffron
for (var i = 1; i <= 3; i = i + 1) { IO.println(i) }
```

```
[line 1, col 13] Error: expected ':' but found '='
  1 | for (var i = 1; i <= 3; i = i + 1) {
                  ^
```

The only accepted spelling drops `var` and annotates:

```saffron
for (i: Int = 1; i <= 3; i = i + 1) { IO.println(i) }   // works
```

Traced to `src/compiler/parser.sf:2151-2156`. After the loop variable's
identifier is consumed, the parser hard-`consume(":")`s:

```saffron
// C-style for without var keyword: for (i: Float = 0; i < 10; i = i + 1)
this.consume(":")
var vtype: String = this.parse_type()
this.consume("=")
```

There is no branch for `var`, and no branch for a missing annotation. Note the
comment already calls this "for without var keyword" — the `var` form was never
implemented, so the *natural* spelling is the unsupported one. Two small changes
fix it: accept and skip an optional leading `var`, and make the `":" type` part
optional, falling back to the same inference `Stmt.VarDecl` already performs for
`var x = expr` (pass `""` as the type, as the for-in desugaring does at
`parser.sf:2197`).

**Worked around** in `playground/examples/fizzbuzz.sf` by writing
`for (i: Int = 1; ...)` with a comment pointing here. Low severity, high
visibility: it is the first loop anyone writes.

---

## Bug 32 — `for (entry in someMap)` compiles to a *list* index loop and segfaults — **OPEN** (pre-existing, documented-feature-does-not-work)

`CLAUDE.md` documents map iteration as a supported feature:

```saffron
// Iteration yields [key, value] pairs
for (entry in m) {
    IO.println("${entry[0]} = ${entry[1]}")
}
```

That program crashes.

Minimal repro:

```saffron
var m: Map<String, Int> = {"one": 1}
for (e in m) { IO.println(e[0]) }
```

```
Segmentation fault: 11
```

Reproduced identically against the pre-session baseline compiler at
`/tmp/ovbase`, so this is **pre-existing, not a regression**.

Root cause: `for-in` is desugared in the parser with no knowledge of the
collection's type. `Parser.desugar_for_in` (`src/compiler/parser.sf:2193-2211`)
unconditionally emits an index-driven while loop:

```saffron
var init_list: AST.Stmt = AST.Stmt.VarDecl(list_var, "Any", iter_expr, "")
var cond: AST.Expr = ... MethodCall(Variable(list_var), "length", []) ...
var item_init: AST.Stmt = AST.Stmt.VarDecl(item_name, "Any",
    AST.Expr.IndexGet(AST.Expr.Variable(list_var), AST.Expr.Variable(idx_var)), "")
```

The temporary is typed `"Any"`, so by the time codegen sees `src[i]` it has lost
the fact that `src` is a Map. Two things then go wrong in the emitted IR
(confirmed by reading the generated `.ll`):

1. `src.length()` on a Map dispatches to `@__list_length` — it reads a map
   header as a list header.
2. `src[i]` dispatches to `@__map_get(map, i)` — because
   `gen_index_get` (`src/compiler/codegen/methods_body.sf:418`) *does* have a Map
   branch, but it is gated on `this.typed_vars` knowing the object is a Map.
   Here the variable is `Any`, so the Map branch is skipped for the *outer*
   lookup, and the integer cursor is used as a **map key**. `__map_get` returns
   nil/garbage, and `e[0]` then indexes that as a list.

So the loop is doubly wrong: it treats a Map as a List for the bound, and uses
the index as a key for the element. The crash is the `e[0]` list-read of a
non-list.

The same mechanism explains the companion symptom that the desugaring
*never* uses the documented iterator protocol at all:

```saffron
class Countdown {
    fun iter(): Countdown { return this }
    fun has_next(): Bool { ... }
    fun next(): Int { ... }
}
for (x in Countdown(3)) { IO.println(x) }
```
```
[codegen] Error: type 'Countdown' has no method 'length'
```

`CLAUDE.md` states for-in "uses iterator protocol: `.iter()` -> object with
`.has_next()`, `.next()`" and that it "works over Lists, Strings (char by char),
Maps ([key, value] pairs), and custom types". **None of that is true** — the
desugaring only ever emits `length()` + `[i]`. Lists and Strings work because
those two happen to be implementable by index; Maps and custom types do not
work at all.

Real fix: desugar to the protocol the docs promise —
`var it = coll.iter(); while (it.has_next()) { var item = it.next(); ... }` —
and give List/String/Map runtime `iter()` methods. `Map.iter()` already exists
and works when driven by hand:

```saffron
var iter = m.iter()
while (iter.has_next()) { var e: List<Any> = iter.next(); IO.println(e[0]) }   // works
```

That makes the desugaring type-agnostic (which is what the parser needs, since
it has no types) *and* correct for every collection, and it removes the
`length()`/`[i]` special-casing from codegen. It cannot be done in the parser
alone if `iter()` must be resolved statically, but as a dynamic method dispatch
it can.

**Worked around** in `playground/examples/collections.sf` by iterating
`counts.keys()` and calling `.get(key)`, with a comment pointing here.

---

## Bug 33 — an inherited method's `this.foo()` call binds to the *base* implementation, so interface default methods and `List<Base>` polymorphism both read the base — **OPEN** (pre-existing; same root cause as the virtual-dispatch gap)

Three symptoms, one cause. Method calls resolve against the *static* type of the
receiver, so any call that should reach an override does not.

Symptom A — an interface default method calling an abstract method:

```saffron
interface Shape {
    fun area(): Float
    fun describe(): String { return "area ${this.area()}" }
}
class Square extends Shape {
    var side: Float
    fun init(side: Float) { this.side = side }
    fun area(): Float { return this.side * this.side }
}
var sq = Square(4.0)
IO.println(sq.area())        // 16   <- correct
IO.println(sq.describe())    // area 0   <- WRONG, should be "area 16"
```

`CLAUDE.md` advertises exactly this shape ("Default methods are inherited") under
"Interface Conformance", so the documented example is the broken one. The
inherited `describe` body was generated once against `Shape`, where `area()` is
abstract, and `this.area()` was bound there.

Symptom B — identical with a plain base class, so it is not interface-specific:

```saffron
class Base {
    fun area(): Float { return 0.0 }
    fun describe(): String { return "area ${this.area()}" }
}
class Sq extends Base { ... fun area(): Float { return this.side * this.side } }
IO.println(Sq(4.0).describe())   // area 0   <- WRONG
```

Symptom C — the already-known polymorphism gap, restated as the same bug:

```saffron
var pets: List<Animal> = [Dog("Rex"), Cat("Mia")]
for (p in pets) { IO.println("${p.name} says ${p.speak()}") }
```
```
Rex says ...
Mia says ...
```

Fixed on the virtual-dispatch branch (worktree `agent-aeeefba0cc72b6129`), which
produces `Rex says Woof` / `Mia says Meow`; **still broken on `main`** as of this
session — re-verified today.

**Worked around** in `playground/examples/classes.sf`: `speak()` is called on the
concrete `Dog`/`Cat` values rather than through a `List<Animal>`, and the
interface example declares only the abstract `area()` with no default method.
Both carry comments pointing here. Once the virtual-dispatch branch lands, both
workarounds should be reverted to the natural form — that is the single best
readability win available to these examples.

---

## Bug 34 — `x is SomeClass` is compiled to a constant `false` whenever the static type is `Any` — **OPEN** (pre-existing; silently wrong, no diagnostic)

A type test against a user-defined class is **silently constant-folded to
`false`** if the value's static type is `Any`. This is the worst failure mode of
the set: no error, no warning, just a wrong answer.

Minimal repro:

```saffron
class Dog { fun init() {} }
class Cat { fun init() {} }
fun check(a: Any): Bool { return a is Dog }
IO.println(check(Dog()))   // false   <- WRONG, should be true
IO.println(check(Cat()))   // false   <- correct by accident
```

It works when the compiler can see the concrete type at the check site:

```saffron
var d: Any = Dog()
IO.println(d is Dog)   // true    <- the *declared* Any is refined by the initialiser
```

so the failure is specific to a value whose type is genuinely opaque — most
importantly a function parameter, which is exactly where a type test is useful.

Traced to `Codegen.gen_is_check`, `src/compiler/codegen/expr_body.sf:400-427`.
The `Any` path maps a fixed list of *builtin* names to runtime predicates
(`__val_is_int`, `__val_is_string`, `__val_is_list`, …) and then gives up:

```saffron
// Unknown class type with Any — cannot resolve at runtime yet, return false
var local: String = this.fresh_local()
this.emit_indent(local + " = add i64 0, 0")
return this.emit_tag_bool(local)
```

The comment concedes it. But the machinery it says is missing **already exists
and is already used**: the statically-typed path a few lines below emits a class
comparison via `@__gc_get_type_tag`, which is a purely runtime query. Compare the
IR for the two spellings of the same test:

```
$ build/saffronc /tmp/probe/s1.sf /tmp/probe/s1.ll   # `a is Dog`, a: Any
  %t3 = add i64 0, 0          <- folded to false

$ build/saffronc /tmp/probe/s2.sf /tmp/probe/s2.ll   # `d is Dog`, d refined to Dog
  %t1 = call i64 @__gc_get_type_tag(i64 %val)
  %t2 = icmp eq i64 %klass, %t1
```

Fix: in the `Any` branch, when `type_name` is not a builtin, fall through to the
same `__gc_get_type_tag` comparison the static path emits, resolving the class
tag by name. Nothing new is needed at runtime. Failing that, it should at
minimum emit a diagnostic instead of a silent `false` — "cannot test `Any`
against class X" would have saved the debugging time this cost.

Knock-on effect: `is`-pattern matching inherits it. `CLAUDE.md` documents

```saffron
var sound = match (animal) {
    is Dog(d) => d.bark(),
    is Cat(c) => c.meow()
}
```

and with `animal: Any` every arm tests false, so the first arm wins by default —
`sound(Cat())` returns `"Woof"`. (On the `/tmp/ovbase` baseline the same program
failed to compile at all with `undefined variable 'd'`, so the is-pattern path
has moved from "rejected" to "silently wrong", which is worse.)

---
## Bug 28 (revised) — root cause found: the GC collects live objects out of a spawned task's coroutine frame

My original entry described this as "segfaults on a large request body that
arrives after a large response". That characterisation was wrong — it described
one instance of a much broader and much more serious bug. **Every** Saffron HTTP
server dies after a fixed amount of served traffic, regardless of request size,
and the cause is the garbage collector, not the request parser.

### The bug in ten lines

```saffron
import "@http/server" as HttpServer

var app = HttpServer.server(8098)
app.get("/small", fun (req: Any): Any {
    return HttpServer.Response(200, "0123456789")
})
app.serve()
```

```
DIED at req 85, cumulative body bytes=840: Remote end closed connection without response
```

Eighty-four successful 10-byte responses, then the process is gone. No panic, no
stderr output, no log line — the last thing in the log is still `Listening on
http://0.0.0.0:8098`. It exits silently.

### It is the GC

Adding one line makes the bug vanish:

```saffron
@extern("i64 __gc_disable()") fun gc_disable(): Int
gc_disable()
```

```
GC-DISABLED: 399 requests all ok
```

399 requests versus 84 — with the *only* difference being whether automatic
collection runs. That is conclusive.

The RSS trace shows the collection happening right before the crash:

```
req  76 fds=10 rss=5376KB
req  77 fds=10 rss=5392KB
req  78 fds=10 rss=5408KB     <- peak
req  79 fds=10 rss=5168KB     <- GC ran, heap shrank
req  80 fds=10 rss=5136KB
...
req  84 fds=10 rss=4896KB
DIED at req 85
```

`@__gc_threshold` defaults to 65536 bytes (`src/runtime/gc.ll:985`), so the first
automatic collection lands a few requests before the death — the crash is not the
collection itself but the first use of something the collection freed.

The file-descriptor count is flat at 10 for the whole run, so this is **not** an
fd leak, and RSS is under 6 MB, so it is not exhaustion. The earlier
"cumulative bytes" pattern I measured on the playground (dying at ~77-95 KB of
response body) is just the allocation threshold being reached sooner when each
response is bigger:

```
20000B responses -> died on request 4    (cumulative 60000)
10B    responses -> died on request 85   (cumulative 840)
```

Same collection, reached at different request counts. And critically, requests
that produce a *tiny* response never trip it — 29 consecutive
compile-error responses through the playground, all fine, because little is
allocated.

### Why spawned tasks are implicated

The GC is not broken in general. A pure allocation loop survives 200,000
iterations, allocating and dropping a fresh string each time:

```saffron
var total: Int = 0
for (i: Int = 0; i < 200000; i = i + 1) {
    var s: String = "chunk-" + i.to_string()
    total = total + s.length()
}
IO.println("survived: ${total}")     // survived: 2288890
```

So straight-line code roots correctly. The distinguishing feature of the server
is that every connection is handled in a **spawned task** —
`src/lib/http/server.sf:379-383`:

```saffron
while (true) {
    var conn: Net.TcpConnection = listener.accept()
    var app = this
    Task.spawn(fun () => app._handle(conn))
}
```

`_handle` allocates heavily (the request string, the parsed `Request`, the route
table walk, the `Response`, the serialised output) and it does so **inside a
coroutine frame**, not on the machine stack. The GC's shadow stack
(`__gc_push_root` / `__gc_pop_roots`, visible in every generated function
prologue) roots locals by taking the address of a stack `alloca`. When the
function is a coroutine, those allocas live in the coroutine frame on the heap,
and after a suspend/resume the frame can be at a different address than the one
that was pushed — so the collector either scans a stale address or never sees the
live object at all. Either way it frees something still in use, and the next
touch of it kills the process.

That is consistent with everything measured: straight-line code fine, coroutine
code fatal, GC-disabled fine, timing set by the allocation threshold rather than
by request count or size.

I could not get the smaller repro to fail on demand — 4000 spawn/await round
trips each allocating a string survive:

```saffron
for (i: Int = 0; i < 4000; i = i + 1) {
    var t = Task.spawn(fun (): Int { ... })
    total = total + t.await()
}
```

so the trigger needs the specific mix `_handle` produces (probably a task that
actually *suspends* on I/O, which `accept`/`read`/`write` do and a pure
computation does not). The server repro above is small enough to debug directly
and fails every time at the same request number, so it is the better starting
point.

### Impact

This is a hard blocker on writing any real server in Saffron, and the most
serious bug in this log:

- Every server built on `@http/server` dies within a couple of minutes of
  moderate traffic. The playground itself does — it died mid-way through
  compiling its own six bundled examples, which is how I found it.
- The failure is silent, so from the outside it looks like a network fault, not a
  crash. I initially misdiagnosed it as a request-parser bug for exactly this
  reason.
- It is trivially remotely triggerable: ~85 unauthenticated GETs of a 10-byte
  response. Any Saffron-served endpoint can be taken down by a browser refresh
  held down for a few seconds.

### Workaround

`__gc_disable()` at startup, as above, trades the crash for unbounded heap
growth. That is acceptable for a short-lived local dev server (the playground's
actual use) and unacceptable for anything long-running. I have **not** applied it
to `playground/src/main.sf` — it is a papering-over of a runtime bug and the fix
belongs in the shadow-stack/coroutine interaction, not in every server's
startup. Restart the playground if it goes quiet.

Suggested fix direction: make the shadow stack coroutine-aware — either re-push
roots on resume, or root coroutine-frame locals via the frame pointer rather than
a captured `alloca` address, or have `llvm.coro` frames themselves be traced
objects the collector walks.

---

## Bug 35 — a request body over ~35 KB silently kills the server, and that makes the playground's own size cap unreachable — **OPEN** (filed as `BUGS.md` #64)

Found while doing the final verification pass on the sandbox controls. I set out
to confirm that `MAX_SOURCE_BYTES = 64000` rejects an oversized submission, and
discovered the check can never run.

```
body   8532B  ok
body  16014B  ok
body  32014B  ok
body  40014B  server gone; every later request gets ECONNREFUSED
```

`_handle` reads the request in one fixed-size call and never checks how much came
back (`src/lib/http/server.sf:400-405`):

```saffron
var raw: String = ""
if (tls_conn != nil) {
    raw = tls_conn.read(8192)
} else {
    raw = conn.read(8192)
}
```

No drain loop, no `Content-Length`, and `raw.length() == 0` is the only guard
(`server.sf:407`) — a *short* read is indistinguishable from a complete request.
Bodies in the 8–32 KB range survive only because a loopback read happens to
return more than 8192 bytes in practice; past ~35 KB it does not, the parser gets
a truncated request, and the process exits silently in the same way as Bug 28.

The security consequence is the one that matters for the deliverable: **a
size cap enforced in the handler is not a size cap.** The playground checks 64000
bytes in `src/compile.sf` before spawning anything, which is the right place for
it in principle, but the server is already dead by ~35 KB, so the guard is
decorative. I have corrected the README's "What is contained" section — it
previously listed the source size cap as an in-place control, which was wrong.

Distinct from Bug 28 in mechanism and in cost to exploit: Bug 28 needs ~85
requests to reach a GC cycle, this needs one 40 KB POST.

A correct fix is a read loop: read until the headers are complete, parse
`Content-Length`, refuse anything over a server-level maximum *before* reading the
body, then read exactly that many bytes.

---
## Bug 36 — a value that re-enters wasm from JS is untagged, so it can never match a `Map` key it was stored under — **FIXED in the playground** (filed as `BUGS.md` #75)

The third and last of the three bugs stacked behind "the Run button does nothing".
With Bug 37 (below) and `BUGS.md` #71 out of the way, the request went out
correctly and the response came back correctly — and still nothing happened. No
console error, no status change, no trap.

`api.sf` handed JS an opaque callback id and expected it back on completion:

```saffron
var _next_id: Float = 0
var _callbacks: Map<Float, (String) => Nil> = {}

fun _register(callback: (String) => Nil): Float {
    _next_id = _next_id + 1
    var id: Float = _next_id
    _callbacks.set(id, callback)
    return id
}

fun _resolve(id: Float, payload: String) {
    if (_callbacks.has(id)) { ... }        // always false
}
```

`has()` was false for every id. Narrowed with a 20-line program (in `BUGS.md` #75)
that stores one entry, hands the id out through an `@extern`, and takes it
straight back in through an exported function: `MISSING`. Passing the *bit pattern
of 1.0* instead prints `FOUND`, which pins it exactly:

| Path | Value reaching `__map_key_cmp` |
|---|---|
| stored by `_register` | `0x3FF0000000000000` — f64 bit pattern of 1.0 |
| arriving from JS | `0x0000000000000001` — bare machine integer |

Nothing re-tags an exported function's parameters, and `__map_key_cmp`
(`src/runtime/runtime.sf:287`) compares non-string keys by exact bit pattern. So
the two "1"s are different keys.

Worth recording how much this cost to find, because the search was misdirected
twice:

- `Map.has()` looked broken on wasm32 in general: a test printing
  `m.has(k).to_string()` returned `false` for all key types. It was **`Bool`
  printing** that was wrong — `true.to_string()` prints `"false"` on wasm32, while
  `if (t)` branches correctly. A version that branched instead of printing showed
  the Map was fine. That is a separate unfiled bug, and it will mislead anyone
  debugging in this area.
- `json_field` was suspected next, since the callback never completed and
  `json_field` was the first thing in it. It works correctly in isolation.

**Fix:** a `List` index instead of a `Map` key — an index only has to compare
numerically, which survives the representation change. Turmeric's own
`__dispatch_event` (`turmeric/src/prelude/03_callbacks.sf:27`) is List-based for
exactly this reason, but the reason was not written down anywhere, so the Map
version looks perfectly reasonable until you try it.

---

## Bug 37 — `json_field` exhausted the wasm heap on an 11 KB field, because string `+` in a loop is quadratic — **FIXED**

Immediately after Bug 36 was fixed, the callback finally ran and died:

```
RuntimeError: memory access out of bounds
    at app.wasm.strcpy
    at app.wasm.api_json_field
    at app.wasm.__lambda_1697
    at app.wasm.api__resolve
    at app.wasm.api___on_fetch_complete
```

`json_field` accumulated the value a character at a time (`out = out + ch`).
Saffron strings are immutable, so every `+` mallocs a fresh copy of everything so
far: for the 11 KB base64 `wasm` field that is ~11000 allocations averaging 5.5 KB,
about 60 MB of garbage. Native has the headroom to absorb it; wasm32 linear memory
does not.

Not a compiler bug — the semantics are what they say — but a sharp edge worth
logging, because the natural way to write a scanner in Saffron is accidentally
quadratic and the failure only shows up on wasm32 and only at size. A native test
with a short field passes happily.

**Fix:** scan for the closing quote to find the end index, then take a single
`slice`. Escapes are expanded in a second pass, only for fields that actually
contain a backslash — which in practice means `diagnostics`, never the base64
payload, whose alphabet has nothing escapable in it.

---

## Bug 38 — `IO.read_file` NUL truncation, again: it also makes the UI module unservable, not just user modules — **WORKED AROUND** (same defect as Bug 2 / `BUGS.md` #66)

Third appearance of the same underlying defect. Bug 2 hit it encoding *user*
modules and worked around it with `base64(1)`. `BUGS.md` #66 filed it as the
general defect after it resurfaced serving `/app.wasm`. This is the instance that
had to be resolved for the playground to load in a browser at all:
`Http.static_files` reads with `IO.read_file`, a wasm module's magic number is
`\0asm`, and so the very first byte terminates the string. `GET /app.wasm` is a
well-formed `200` with `Content-Length: 0`.

Confirmed the file itself is fine, so the loss is purely in the representation:

```saffron
var c: String = IO.read_file("playground/static/app.wasm")
IO.println("length = " + c.length().to_string())     // length = 0
var h: String = IO.read_file("playground/static/index.html")
IO.println("html length = " + h.length().to_string())  // html length = 387
```

**Worked around** by serving the UI module base64-encoded from the service's own
endpoint (`GET /api/app_wasm`), decoded with `atob` in the loader before
instantiating — the same dodge as Bug 2, now factored into
`Compile.encode_file_base64` and shared by both paths. Verified byte-identical:
47555 bytes out, 47555 bytes decoded, `cmp` clean. Costs a 33% larger transfer
once per page load. The loader falls back to `./app.wasm` so a plain static file
server still works for frontend-only development.

This does *not* fix the general defect, and `BUGS.md` #66 stays open: a proper fix
needs a byte-length-carrying body type through `Response`, since `Response.body`
is `String` and `Content-Length` is emitted from `.length()`.

---

## Bug 39 — the GC bug had to be worked around after all; measured at 23 requests, not 85 — **WORKED AROUND** (`BUGS.md` #63)

Bug 28 concluded that `__gc_disable()` "trades the crash for an unbounded heap, so
it is deliberately not applied". That position did not survive contact with actual
use. Measured on the playground service:

| Endpoint | Requests before the process died |
|---|---|
| `/style.css` | 23 |
| `/api/health` | 60 |

Bug 28's figure of ~85 was for a 10-byte response; anything that allocates more
reaches the 65536-byte collection threshold sooner. At 23 requests the server
cannot survive a single page load plus a couple of Run clicks — it died three
times mid-verification during this session, each time producing a "connection
refused" that reads as a client or routing fault rather than a crash.

`__gc_disable()` is now called at the top of `main()`, taking it from 23 to 400+
requests clean. The trade-off is real and is documented at the call site and in
both the "Known issues" and "What is NOT contained" sections of the README: the
heap is now unbounded, so sustained traffic alone is a denial-of-service. That is
acceptable for a local dev playground and is not acceptable for a public
deployment, which the README already says this must not be.

The fix still belongs in making the shadow stack coroutine-aware; nothing here
changes that analysis.

---
