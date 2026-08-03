# Access modifiers for Saffron

Status: design proposal. Written 2026-08-02 against `915dd6a` (working tree has in-flight `stmts_body.sf` enum-encoding changes; see Phase 0).

Verified absent beforehand: no `private`/`public`/`protected`/`internal` in `lexer.sf`, `ast.sf`, `parser.sf`. The only occurrences repo-wide are LLVM's own `private` linkage in `src/runtime/*.ll` and prose in comments. This is greenfield.

## 1. The modifier set

**Decision (user, 2026-08-02): parity with Kotlin. Four modifiers — `public` (default), `private`, `internal`, `protected`.**

Kotlin's model is *position-dependent*: `private` does not mean one thing, it means "the narrowest enclosing declaration", which differs between top level and class body. Adopting it wholesale:

| Modifier | On a top-level declaration | On a class / actor / interface member |
|---|---|---|
| `public` (default) | visible everywhere it can be imported | visible wherever the class is |
| `internal` | visible inside the **package** | visible to clients in the package that can see the class |
| `protected` | **error** — Kotlin forbids it at top level | visible in the class **and its subclasses** |
| `private` | visible inside the **file** | visible inside **that class body only** |

Two consequences worth stating, because they contradict earlier drafts of this document:

**`private` is not "same file" everywhere.** The user's "private is same file" is precisely Kotlin's *top-level* rule. For a class member Kotlin's `private` is narrower — the class body, not the file. Both halves are adopted. This resolves former open decision #1: a same-file free function may **not** read a `private` field, because in Kotlin it cannot. The 24 stdlib underscore fields read by same-module free functions (§2) therefore become `internal`, not `private` — which is a better answer than the "permanently public" compromise the earlier draft settled for.

**File-scope and package-scope are two different boundaries and both already exist in the tree.** A module is a file: `module_prefix_from_file(file_path)` (`main.sf:630`). And a package is a `pantry.toml`: the compiler already reads `[package] name`/`entry` during import resolution (`main.sf:48-49`, `main.sf:475`), so `internal` keys off a boundary the compiler genuinely knows, not an invented one. Visibility identity must therefore be **structured** — an owning file *and* an owning package per declaration — not one opaque prefix string. Recording only the prefix would force later re-derivation of package membership, which is the M1 antipattern.

### `protected` is in scope and blocked; it is now the critical path

Kotlin parity means `protected` ships. It **was** unimplementable soundly, so the blocker below was promoted from "deferred Phase 8" into a prerequisite of the `protected` slice — and then done, as Phase 3b (BUGS #111). The diagnosis is kept below because it is the reason for the sequencing, and because the fix is only half-landed: it waits at the gen2 promotion gate.

### Why `protected` was blocked (historical — resolved by Phase 3b)

`protected` was not merely unattractive here; it was **unimplementable soundly**, and that was a fact about the tree, not a preference.

`ClassDecl.parent` is a single `String` (`ast.sf:82`). The parser reads the first parent and then throws the rest away:

```
parent = this.expect_ident()
// Support multiple extends (interfaces): class Foo extends A, B, C
while (this.match_kind(",")) {
    this.expect_ident() // skip additional interfaces for now
}
```
`src/compiler/parser.sf:1975-1979`

So for `class Duck extends Flyable, Swimmable, Walkable` (the form CLAUDE.md documents and `test/pass/multi_inherit.sf` exercises), only `Flyable` survives parsing. The checker's `class_parents` is `Map<String, String>` — one parent per class — and `is_subtype_node` walks a single `X__parent` chain.

A `protected` member declared on `Swimmable` therefore could not be resolved from `Duck` at all: the checker does not know `Duck` derives from `Swimmable`. `protected` would enforce correctly on the first parent and silently allow on the second and third — a visibility check that permits access it cannot resolve, which is exactly what I2 of `docs/design/compiler-rewrite.md` forbids ("`Unknown` may exist during unification; it may not appear in HIR", and its generalization: unknown must never be spelled as something concrete).

Prerequisite for `protected`, stated rather than hidden: widen `ClassDecl.parent: String` → `parents: List<String>`, propagate through `class_parents`, `class_parent_of`, `is_subtype_node`, conformance checking, and codegen's field-prepending and `__class_parent_tag` switch. Under Kotlin parity this is no longer optional and no longer last — it is a **hard prerequisite of the `protected` slice**, and it is the single largest piece of work in this design. It carries its own promotion gate, because it widens a declaration node the compiler reads about its own AST (§5, #96/#100).

Sequencing consequence: `protected` is the **last** modifier to land, not because it is least wanted but because it is the only one gated on a structural change to inheritance representation. `public`/`private`/`internal` do not depend on it and ship first.

**Update (2026-08-02): the widening is done — see BUGS #111.** The diagnosis above was accurate, with one correction: `test/pass/multi_inherit.sf` did not "exercise" the form, it *failed* on it (`[codegen] Error: type 'Duck' has no method 'swim'`), as did `test/pass/interfaces.sf` — multiple inheritance was not merely unsound for `protected`, it did not work at all past the first base. `class_parents` is now `Map<String, List<String>>`, `is_subtype_node` searches the full inheritance DAG breadth-first, conformance consults every base, and `__class_is_a` is a flattened ancestor-set table rather than a chain walk (a `switch` arm returns one value, so no walk can traverse a DAG). Field layout is deliberately unchanged: only `parents[0]` contributes fields, because parent field index i == child field index i can hold for exactly one base.

So `protected` is no longer blocked on representation — the checker can now resolve `Duck` → `Swimmable` and a `protected` member on any base is answerable. The sequencing above still holds for a different reason: the widening changes how the compiler reads its own `ClassDecl`, so it has its own promotion gate (§5, #96/#100) which must close before `protected` work can begin on top of it.

### `internal`: package-scoped, on a boundary that already exists

Kotlin's `internal` is module-scoped, where a Kotlin "module" is a compilation unit (Gradle module / Maven project). Saffron's closest true analogue is the **package** — a `pantry.toml` — not the file, since Saffron already calls a file a module.

Terminology hazard worth flagging loudly, because it will confuse anyone reading both languages: in this codebase "module" already means *file* (`module_prefix_from_file`, `module_boundaries`, `module_prefixes_list`). Kotlin's `internal` is *not* file-scoped. So `internal` in Saffron = **package-scoped** = the `pantry.toml` that owns the file. The design must never use the bare word "module" to describe `internal`'s scope; say "package".

The boundary is real and already parsed: `main.sf:48-49` and `main.sf:475` read `<lib_dir>/<name>/pantry.toml` to resolve a bare import to a package entry point. What does **not** yet exist is a per-declaration mapping from file → owning package; that has to be built, and it belongs next to the file-prefix plumbing in Phase 2 rather than bolted on later.

Open sub-question, deliberately not decided here: a file with no `pantry.toml` above it (a bare script, or `test/*.sf`) has no package. `internal` in such a file should most likely degrade to file-scope with a warning rather than silently mean "public" — spelling unknown as a concrete answer is I2's exact prohibition — but the honest options are enumerated in §11 for the owner rather than settled unilaterally.

### Default

**Unannotated is `public`** (settled). Consequences worth stating explicitly, because they are load-bearing for the rest of the plan:

- Backward compatible by construction. No existing program in `src/lib/`, `test/`, `examples/`, `basil/`, `bazaar/`, `parsley/`, `turmeric/` or `playground/` changes behavior. Zero forced churn.
- The migration question collapses: annotating is opt-in.
- Every negative test must therefore *introduce* an annotation to have anything to reject. There is no "the suite got stricter" moment. That has a downside covered in §7 (a check with nothing to reject looks green for the wrong reason — the #76 failure shape).

## 2. The leading-underscore convention

**Verdict: formalize as documentation-only. Do not auto-treat `_`-prefixed names as private. `private` is an orthogonal, opt-in annotation.**

Measured over `src/lib/` + `src/compiler/`:

| | count |
|---|---|
| top-level `fun _name` | 156 |
| class fields `var _name:` | 90 |
| class methods `fun _name` | 20 |
| `this._x` accesses | 782 |
| non-`this` `recv._x` accesses | 84, across 28 files |

Auto-private would break, concretely:

- **42 module-scope accesses** where a same-module *free function* reads a class's underscore field. Concentrated in `src/lib/pantry_config.sf` (20 — `proj._deps`, `proj._scripts`, `ws._members` …), `src/lib/http/server.sf` (14 — `resp._is_stream`, `resp._headers`, `resp._body_bytes` in `_serialize_headers`/`_write_response`), `src/lib/sorted_set.sf` (8 — `result._items` in the module-level `union`/`intersection`/`difference`). Class-private would reject every one of these, because the reader is not inside the class body.
- Of the 90 underscore fields, **66 are touched only inside their declaring class** (safe as class-private) and **24 are touched from outside it** (not safe).

The good news, which shapes §8: for top-level `_` *functions*, I found **zero** genuine cross-module callers. The one apparent hit (`_strcmp` in `semver.sf` and `string.sf`) is two independent declarations — `string.sf:6` is an `@extern("i64 strcmp(void*, void*)")`, `semver.sf:51` is a Saffron function. Likewise `_escape_sq` appears three times as three separate local definitions, and `_pad2`, `_to_hex`, `_is_digit`, `_shell_escape`, `_parse_response`, `_retag` are all duplicated-per-module rather than shared. The only qualified `Alias._member` spellings in the tree (`Net._raw_read`, `ModuleAlias._field`) are both inside comments.

So: all 156 top-level underscore functions are safe to annotate `private` later; 66 of 90 underscore fields are safe as `private`.

**Kotlin parity changes the answer for the remaining 24.** The earlier draft left them permanently public for want of a middle scope. There is now one: the 24 fields read by same-file free functions become **`internal`** (package-scoped), or `public` where they are genuinely part of the API. That is the concrete payoff of the third modifier — `http/server.Response._is_stream`, `pantry_config.Project._deps`, `sorted_set.SortedSet._items` all get a real answer instead of an exemption. Their exact classification is Phase 6 work, per-module, and needs re-measuring then: the counts above are same-**file** counts, and `internal` asks a same-**package** question, which is a strictly wider net.

Coexistence rule, stated so it can be tested: **`_` has no semantic weight.** An unannotated `_foo` is public. `test/pass/private_underscore_coexist.sf` asserts this so a future "let's just make `_` mean private" change trips a test rather than a customer.

## 3. Where enforcement lives

**Checker-only. Codegen learns nothing.**

The argument is straightforward and this codebase has the scars to back it. Visibility is a purely static property of declarations: it changes no LLVM types, no struct layout, no field offsets, no name mangling, no dispatch. There is nothing for codegen to emit differently. Handing codegen a visibility table would be a fresh instance of M1 — codegen re-deriving information the checker already has — for zero benefit. Per I1: the checker decides; codegen consumes nothing it did not need already.

Consequence: **visibility is not a security boundary.** A `--no-check` build ignores it entirely, and reflection (`test/test_reflect.sf` reads fields dynamically) bypasses it by construction. That is intended and should be documented, not patched.

### The unresolvable-receiver decision

This is the hard case. For `recv._private_field`, the checker may be unable to resolve `recv`'s class at all. Real, non-hypothetical sources:

- an `Any`-typed value — pervasive, and the honest answer of the inferrer per I2
- the result of a `Fun` call — `Fun` carries no return type (**#56**), so `f(1).v` has no known class
- a block-syntax parameter `{ x => ... }` — untyped by construction (**#86**), which is the documented idiomatic HTTP handler form
- the `MemberAccess` fallback generally, which is *also* the normal working route for module constants like `Math.PI`

**Decision: allow the access, and emit a warning that names the unresolved receiver and the member.** Not silently allow; not deny.

Justification, in order of weight:

1. **Neither silent answer is honest.** Silently allowing spells unknown as "public"; silently denying spells it as "private". Both violate I2. The only I2-compliant option is to say so out loud, which is what the warning is. The compiler reports *"cannot determine the class of receiver `x`; visibility of `._body` not checked"* rather than pretending either way.
2. **Denying breaks working code, and there is direct precedent for how badly.** #56's write-up records exactly this experiment: erroring unconditionally on the `MemberAccess` fallback "broke `pass/math` and `test_reflect` — one silent wrong answer traded for a batch of false rejections." Denying on unresolved receivers would reject `app.post("/echo") { req => req.body }`, the form every example in `examples/http_server.sf` uses, the moment anything in that chain is annotated.
3. **Asymmetric cost.** Visibility is a hygiene feature. A missed denial is a missed lint. A false denial is a compiler that rejects correct programs. The first is recoverable; the second is how a feature gets reverted.

Discipline this imposes on the test suite: **every `test/fail/` visibility case must use a receiver whose class is statically known** (an annotated local, `this`, or a direct constructor call). A fail test that accidentally routes through the soft-fail path would pass the compiler and thereby fail the suite, which at least fails loudly — but it would be testing nothing. Noted in §7.

## 4. Blocker: the checker cannot see module boundaries

**Module-level `private` cannot be enforced by the checker as it is currently wired.** This is the most important finding in this document and it gates a whole phase.

`main.sf:1104`:

```
checker_errors = Checker.check_errors_with_imports(program, all_import_stmts)
```

`all_import_stmts` is every declaration from every transitively imported module, flattened into one list. `main.sf` computes `module_boundaries` (indices) and `prefixes_joined` (a `|`-joined prefix string) and passes both to `Resolve.resolve_imports(...)` — and passes **neither** to the checker. `check_errors_with_imports` just loops `register_decl` over the flat list. `NullChecker` has no `current_prefix` or module field at all.

The checker already documents the consequence in its own words, at `checker.sf:881-890`:

> `enum_variants` is keyed by the *bare* declared name, and the checker is handed a flat list of every imported declaration with no module boundaries and no aliases (`check_errors_with_imports`), so two modules declaring the same enum name are indistinguishable here.

To decide whether `private fun _helper` in `@iter` is visible at a use site, the checker must know (a) which module each declaration came from and (b) which module the use site is in. It has neither. So Phase 2 below is a real prerequisite, not scope creep.

### And bare-name keying will misattribute

Worse than missing information: the class tables are keyed by bare name with **no ambiguity guard**. `env.class_fields.set(name, ...)` uses the bare name in both `register_decl` (`checker.sf:768`) and `check_stmt` (`checker.sf:1070`). There is an `ambiguous_enums` list; there is **no `ambiguous_classes`**.

Measured: of 97 `class`/`actor`/`interface` declarations across `src/lib` + `src/compiler`, exactly one name collides — **`Response`, declared in both `src/lib/http/server.sf:219` and `src/lib/http/client.sf:26`**. A `private` field on `http/server.Response` would be registered under the key `Response` and then consulted for `http/client.Response` too, whichever registered last winning. That is a **false denial** on correct code, in the stdlib, today.

So: **visibility tables must be keyed by module-prefix + name, not bare name.** This is the strongest single argument that the module-plumbing phase comes before any module-level or cross-module visibility enforcement, and it is independently valuable — the same information would let `ambiguous_enums` be replaced by real qualified keys.

## 5. Syntax and AST representation

Surface syntax: a leading soft keyword, matching how `interface` and `actor` already work (`is_ident_named`, `parser.sf:1425-1426`) so no new reserved word is introduced and no existing identifier named `private` or `public` breaks. (Grep found none anyway.)

```saffron
private fun helper(): Int { ... }        // module-private
private class Internal { ... }
private var _cache: Map<String, Int>

class Response {
    private var _headers: List<String>   // class-private
    public var status: Int               // explicit, same as unannotated
    private fun _serialize(): String { ... }
    private fun init(s: Int) { ... }     // see below
}
```

### Representation: widen the AST enums. Do not use the docstring channel.

The tempting shortcut is the existing docstring side channel — `@actor`, `@inline`, `@intrinsic`, `@extern:`, `@extend:`, `@generic:`, `@type_alias`, `@import:` are all carried that way and read at 30-plus sites. It would avoid widening any enum payload.

**Reject it**, for a specific mechanical reason: every reader uses `docstring.starts_with(...)`, so the channel has a capacity of exactly **one** annotation, and it is already full in two places. `parser.sf:1426` prepends `"@actor\n"`; `parser.sf:1665` prepends `"@generic:"`. An actor with a generic method already has this collision latent. Adding `@private` as a third prepender would make `docstring.starts_with("@generic:")` fail for any generic private function — a silent miss, not an error. Stacking a third occupant onto a one-slot channel is a defect generator, and it violates I3 besides.

So: add a `visibility: String` field (`"public"` / `"private"`) to the declaration nodes. Which nodes, and the cost:

| Node | arity | match arms to update | notes |
|---|---|---|---|
| `Param` | 3 → 4 | 34 arms, 24 constructions | carries class **fields** and function **params** |
| `FunDecl` | 5 → 6 | 44 arms, 5 constructions | covers methods **and** top-level functions |
| `ClassDecl` | 6 → **7** | 67 arms | also covers `interface` and `actor` |
| `EnumDecl` | 3 → 4 | modest | |
| `VarDecl` | 4 → 5 | large | top-level `var`/`let` |
| `TypeAlias` | 2 → 3 | small | |

Two hazards, both with precedent in `BUGS.md`, both must be designed for:

**#96 / #100.** `ClassDecl` going to seven fields is precisely the shape that segfaulted the compiler twice. #96: a match arm bound at most five fields, so a sixth read as 0, and `0.starts_with(...)` became `strncmp(NULL, ...)`. #100: the fix was in source (so gen3 was fine) but `build/stage2/saffronc` was the old binary, and gen2 is what compiles the source — so every green bootstrap rebuilt a gen3 that crashed on any program with a class and an import. The rule #100 distills is the one to obey: **a codegen fix for how the compiler reads its own AST is not landed until gen2 is promoted.** That makes each widening its own promotion-gated step (§7).

**Wrapper-node alternative, also rejected.** A `Stmt.Visibility(vis, decl)` decorator node would avoid touching any existing arity. But then every existing `match (stmt) { ClassDecl(...) => ... }` stops matching an annotated class and falls through to `_`, silently dropping the declaration. That is #37's mechanism (dispatch matching no branch yields a silent value) applied to whole declarations. Not worth the saved arms.

Parser touch points:
- statement dispatch (`parser.sf:1420ff`): `is_ident_named("private")` / `("public")` before the `var`/`fun`/`class`/`enum`/`interface`/`actor` tests, consuming the modifier and threading it down.
- class-body loop (`parse_class_decl_with_doc`, `parser.sf:2020`; loop at `:2068`): **verified 2026-08-02 by reading the loop, not by compiling.** The condition tests only `}` and `eof`; the body has exactly three branches (`match_kind_check("var")`, `is_ident_named("@")`, `match_kind_check("fun")`), each of which advances, and **there is no `else` and no unconditional advance**. So a member token matching none of the three spins forever: `private var x: Int` inside a class body would **hang the compiler**, not produce a parse error. `private` must therefore be handled in that loop *before* the three existing tests.

  Systematic fix worth taking while in there, rather than just dodging this instance: add an unconditional `else` that emits a diagnostic. The hang is not specific to visibility — *any* future member syntax hits it, and a hang is the worst possible failure mode because it has no error message to grep for. Do it as its own commit so it is reviewable separately from the feature.

  Relevant sites for threading visibility through, verified at the post-#111 tree: fields constructed at `:2101`, methods at `:2119` / `:2122` via `parse_fun_decl_with_doc`, and the node returned at `:2129`. Note the `extends` list at `:2055-2062` is now correct (it collects every base) — do not "fix" it.

## 6. Coverage by declaration kind, and the leakage check

### 6a. Per-kind decisions

**Top-level `fun`, `var`/`let`, `class`, `enum`, `interface`, `actor`, `type` alias.** All support `private` = module-private. Three access paths must be checked, not one:
1. named import — `import { name } from "@mod"`. Resolved in `main.sf` into the `named_imports` map (bare name → prefixed name). A private target must be rejected at the import, naming the module.
2. alias-qualified — `Mod.name` after `import "mod" as Mod`. Reaches the checker as `MemberAccess`/`Ref` and is handled in `infer_member_access`.
3. bare cross-module reference — resolve.sf's prescan registers *every* module's globals and funcs into one `Resolver`, so a bare `helper()` in the main program can bind to a module's function. This is the path most likely to be forgotten; it must be checked via `Ref.slot` (which carries the defining module's prefix) against the use site's prefix.

All three require Phase 2's module plumbing.

**Verified against the built compiler at `7df04b9`, so Phase 5 starts from measurement rather than from this section's guesses:**

- All three paths currently compile clean (`import { exported } from "..."`, `ModA.exported()`, and a bare `exported()` after only an `as ModA` import). So all three are genuinely live and reachable, and none is already denied by some unrelated existing check — each needs its own arm and its own fail test.
- `Ref.slot` does carry what path 3 needs, and it is the *defining* module's prefix: `resolve.sf:469`/`:472` set it from `this.globals.get(name)` / `this.funcs.get(name)`, both populated with `current_prefix` at `:127`/`:130`/`:178`. Resolve runs **before** the checker (`main.sf:1438-1444`), so the checker sees `Ref`, not `Variable`, and the comparison the section describes is available. The checker's `Ref` arm (`checker.sf:1704`) currently discards `rslot` entirely — that is the hook.
- **The blocker is registration, not lookup.** `register_decl` records module identity for `ClassDecl` only. Its `FunDecl` and `EnumDecl` arms ignore the visibility binding, and its `VarDecl` and `TypeAlias` arms are `=> {}` outright. So there is no `decl_module_file` entry for a top-level `fun`, `var`, `enum` or `type` to compare a use site against. Phase 5's first commit is extending registration to those four kinds — reusing `register_class_identity`'s shape, not inventing a second one.
- **Phase 5's enforcement cannot land before Phase 4 merges**, and this is a hard ordering, not a preference: `private fun helper()` at top level is still a parse error on `main` (`[codegen] Error: undefined variable 'private'` — the modifier lexes as an expression). Phase 4 owns the parser half. Until it merges there is no way to write a Phase 5 fail test, which is the same "annotations nothing checks / checks with no inputs" ambiguity that kept Phase 1's leak pass from landing early. The package *plumbing* was parallelisable; the enforcement is not.
- `internal` already reports `'internal' is not implemented yet; use 'private' or 'public'` from Phase 4's parser — a real diagnostic with a span, not a silent accept. That is the right placeholder for Phase 5 to replace.

**Class fields and methods.** Support `private` = class-private. Note the deliberate asymmetry with §2: a same-module *free function* reading a private field is **rejected**, which is why the 24 underscore fields in `http/server.sf`/`pantry_config.sf`/`sorted_set.sf` must stay unannotated. Module-private *fields* are out of scope for v1 (§9).

**Constructors (`private init`).** Supported, meaning: the class cannot be constructed from outside its module. `register_decl` already registers the class constructor as a function returning the class type (`checker.sf:770`), and after resolve a class name in call position becomes `Ref("func", Name, prefix)` — `resolve_variable` checks `funcs` before `types` — so the prefix needed for the check is already in hand.

The factory pattern works with no language addition, and the stdlib already has the shape: `fun new(): Stack` in `stack.sf`, `queue.sf`, `sorted_set.sf`, `heap.sf`, `deque.sf`, `sorted_map.sf` (six modules, plus `from(...)` variants). Those are same-module free functions, so `private init` + `public fun new()` is a drop-in for all six. That is a genuine motivating win, not a hypothetical.

**Interface members.** `interface` parses through `parse_class_decl_with_doc`, so it is the same node. The rule keys off the distinction `class_abstract_methods` already draws (`checker.sf:172-176`: bodyless = abstract = a requirement on subclasses; has body = inherited default):

- `private` on a **bodyless** interface method → **error**. A private abstract method is a contradiction: an implementor in another module must be able to name it in order to implement it, and `check_conformance` would demand an implementation the implementor may not write. Diagnostic: *"`Drawable.draw` is declared private and has no body; a private abstract method cannot be implemented"*.
- `private` on a **default** interface method (has a body) → **allowed**, class-private to the interface. It is a helper. This is coherent and useful.

**Enum variants.** **Not supported in v1**, and this is a decision rather than an omission.

Work it through. Suppose `enum E { public A, private B }` in module M, and a `match (e)` in module N. Either:
- (a) exhaustiveness (`check_exhaustiveness`, `checker.sf:2236`) demands N name `B`, which N is forbidden to write — contradiction; or
- (b) exhaustiveness exempts private variants, so N's match needs a `_` arm to be total. But a non-exhaustive match in Saffron **yields an indeterminate value, not an error** — `BUGS.md` at #76: *"the only thing standing between this class of bug and a nondeterministic compiler"*, and #73 is that mechanism firing. So option (b) makes the *safe* form unwritable and the *unsafe* form silently indeterminate.

A visibility feature whose only expressible use is a footgun is not a feature. Additionally, `Variant(name, fields)` has no room for a modifier without widening it, and every wide-payload AST change in this repo has a #96/#100 history.

What *is* supported and is well-defined: **`private enum`** — the whole type. No exhaustiveness interaction, because a module that cannot name the type cannot match on it at all.

### 6b. Visibility leakage: a public declaration exposing a private type

Enumerated positions, each with a verdict. Every diagnostic names **both parties and the position**.

| # | Position | Verdict | Diagnostic shape |
|---|---|---|---|
| L1 | `public` field whose declared type is a private class/enum/interface | **error** | `public field 'Cache.entry' exposes private type 'Slot' (declared in this module) in its declared type` |
| L2 | `public` function/method parameter type | **error** | `public fun 'store' exposes private type 'Slot' in parameter 2 ('s')` |
| L3 | `public` function/method return type | **error** | `public fun 'lookup' exposes private type 'Slot' in its return type` |
| L4 | member of a **private** class, whatever its own modifier | **no-op** (not an error) | — |
| L5 | type arguments, transitively: `List<Private>`, `Map<String, List<Private>>` | **error**, at any depth | `... exposes private type 'Slot' in its return type (via List<Map<String, Slot>>)` — the path is part of the message |
| L6 | union / nullable: `Private\|Nil`, `Private?` | **error** | `... exposes private type 'Slot' in its return type (via Slot\|Nil)` |
| L7 | `public class C extends PrivateBase` | **error** | `public class 'C' extends private type 'Base'; 'Base's members become reachable through 'C'` |
| L8 | any `Fun`-typed position | **deliberately unchecked** — structural hole | none; documented |
| L9 | `public` top-level `var` with an **inferred** private type | **error** if the initializer is syntactically a constructor call of a private class; **warning** otherwise | `public var 'registry' exposes private type 'Slot' in its inferred type (from initializer 'Slot()')` |

Notes on the non-obvious ones:

**L4, nested visibility.** A public member of a private type is already unreachable — the enclosing private type gates every path to it. Erroring would force annotating every member of every private class as `private`, which is churn buying nothing. Same reasoning applies recursively: inside a private class, a public method exposing another private type is also a no-op. Rule: **run the leak check only for declarations that are actually reachable from outside their module.**

**L5, transitivity.** This must decompose to arbitrary depth, and it must **reuse** `extract_nth_generic_arg` / `split_type_args` / `generic_base` (`checker.sf:1535-1599`) rather than write a second `<>` parser — I10, and #27 is what a second parser costs.

There is a wart to consume rather than fix: `Param.type_ann` is an `AST.Type` node, but `FunDecl.ret_type` and `VarDecl.type_ann` are raw `String`s. That is I3's exact complaint, and this design does not fix it. The leak walker therefore needs one helper — `mentioned_type_names(spelling: String): List<String>`, splitting on `<`, `>`, `,`, `|` and returning every bare name — and node-typed inputs reach it via the existing `type_node_to_string`. One function, one representation internally. Flag it as I3 debt this feature consumes.

**L6, unions.** Purely lexical decomposition on `|`. **The leak check never emits a runtime type test and never lowers to `is`**, so #69 (`is` broken on unions) is untouched by design. Say this out loud in the doc so nobody later "improves" the check into a runtime one.

**L7, and its former coverage hole — now closed.** The earlier draft recorded that only the *first* parent was known, so `public class C extends PrivateA, PrivateB` would catch `PrivateA` and silently miss `PrivateB`. **Phase 3b / BUGS #111 closed that**: `class_parents` holds every base, so the L7 check must iterate the full list. Stated as a positive requirement rather than a hole: L7 loops `parents`, and the diagnostic names *which* base leaks.

**L8, `Fun`.** `AST.Type` has `FuncType(params, ret)`, but the surface `Fun` erases to a signature-free type: #56 says plainly *"the static type of an indirect call's result is not recovered (`Fun` carries no return type)"*, and #86 is the same root for block parameters. So `public fun subscribe(cb: Fun)` where `cb` is expected to receive a private type is **structurally unable to be checked**. Not "hard" — impossible with the information present. Closing it requires giving `Fun` a signature, which is a language change already tracked by #56. State the hole; do not pretend coverage.

**L9, inference.** An inferred leak has no syntactic anchor to point at, which is why the verdict splits. When the initializer is a direct constructor call of a private class, the anchor exists (the call) and the diagnostic can name it — error. When the type is inferred through several steps, the anchor is the compiler's guess, and this codebase's history is unambiguous about erroring on guesses: #56's over-erroring "traded one silent wrong answer for a batch of false rejections." So: warn. The asymmetry is deliberate and is exactly the I2 posture from §3 — say what you know, and say when you do not know.

### 6c. Where the leak check runs: its own pass

It needs **every** declaration's visibility resolved before it can validate **any** signature, including declarations in other modules. The existing walk is two passes (`register_decl` over all statements, then `check_stmt` over all statements) plus a third for conformance. The leak check is naturally a **fourth pass**, sibling to `check_conformance`, run after registration.

Cost: one linear walk over the flattened declaration list. No recursion into bodies — it reads signatures only. Complexity is O(declarations × type-mentions-per-signature). Estimated 150-250 lines: the pass, `mentioned_type_names`, a visibility lookup, and the diagnostic formatting. It needs a `Map<qualified_name, visibility>` populated during registration, which is the same table §4's module plumbing makes possible.

Two things it must **not** do: it must not consult `last_type` or anything codegen owns (M1), and it must not re-derive scope from side tables (I4/#40) — visibility comes from the declaration, and module identity from `Ref.slot`/the prefix table.

## 7. Bootstrap sequencing

`build/stage2/saffronc` is a checked-in binary and the sole root of trust. It is not to be replaced casually. Every promotion below means the **full documented ceremony** from CLAUDE.md, including stage 2 (gen3 → gen4, ~4 minutes, `SKIP_GEN4=1` forbidden when deciding a promotion), the `test/hello_bootstrap.sf` run, the post-promotion re-bootstrap, and — per #100 — a direct check compiling a program that has **both a class and an import**, since that is the combination `hello_bootstrap.sf` misses.

Phases, in order. The promotion point is marked.

**Phase 0 — rebase, do not race. Ongoing, not a one-off.** The original instance of this (uncommitted `stmts_body.sf` enum-encoding work) landed as `99ed527`/`febd0de`. It recurred immediately: a "spans" rewrite (`65f821a`, `5411058`) added `AST.Span` and spanned tokens — **+79 lines in `ast.sf`, +127 in `parser.sf`** — and `8b5eadf` folded BUGS #78's three import-resolution loops into one guarded helper, reshaping the area Phase 2 and the package mapping both work in.

Treat this as a standing condition of the repo, not an obstacle to clear once: **another person commits to `main` continuously.** Every phase below must rebase before finishing, and any phase touching `ast.sf` or `parser.sf` must re-count the arguments at every affected construction site *by hand* after merging. A clean auto-merge is not evidence of correctness there — a `ClassDecl(` site left with the wrong argument count is BUGS #96's exact segfault (an unbound field reads as 0, then `0.starts_with(...)` is `strncmp(NULL, ...)`).

Conflicts in `build/saffronc` and `build/stage3/*.ll` are expected and are **generated artifacts**. Never hand-merge them; take either side and let `./bootstrap.sh` regenerate. Observed empirically: merging Phase 2 into `main` conflicted *only* in those two files.

**Phase 1 — leak-check machinery (no new syntax).** *Can* it land before promotion? Technically yes: it is checker-only and needs no syntax, so gen2 compiles it fine. **But it should not.** With nothing annotatable, the pass has zero reachable inputs, and "a check that ran and found nothing" is indistinguishable from "a check that never ran" — the precise failure that hid #76 for as long as it did, and that a green bootstrap over a broken gen3 hid in #100. So: **build the pass in Phase 1, but land it in the same commit as Phase 4's parser support**, where it has real inputs and real tests. This refutes the guess that it can usefully go first.

**Phase 2 — module plumbing into the checker (no new syntax, pre-promotion). ✅ DONE and merged to `main` (`fcf0bc1`, source `85bfb13`).** Threaded `module_boundaries` + `prefixes_joined` into the checker via `check_errors_with_modules(...)`, decoding them exactly as `Resolve.resolve_imports` does rather than inventing a second scheme; `check_errors_with_imports` is now a thin wrapper that degrades to the old flat behaviour on an empty boundary list. Class tables re-keyed by prefix + name, with the missing `ambiguous_classes` guard added — an ambiguous bare name answers `Any` (widening) rather than inventing a mismatch, matching `get_variant_fields`' posture.

Two findings worth carrying forward:

- **Locality cannot be inferred from the key.** The module under check is unprefixed, so its qualified and bare keys are the *same string*. A first implementation therefore widened a program's own class to `Any` whenever any import declared that name. Locality is now recorded explicitly and a local declaration wins its bare name. Any later phase keying on qualified names must not re-derive locality from the key shape.
- **`ambiguous_enums` was NOT retired**, though Phase 2 makes it possible. `enum_fields`' key is already compound (`"EnumName.Variant"`), so adding a prefix makes it three-part and all ~10 read sites must agree simultaneously. Deferred with a comment recording that it is now possible.

Verified: bootstrap both stages; failure sets byte-identical to base (28 failures, same names, no swap; passing 147 → 148); `build/stage2/saffronc` provably unchanged. `test/pass/visibility_response_collision.sf` was confirmed to **fail on base** — the unchanged gen2 rejects it with `ERROR: client_payload: cannot assign Int to String`, the misattribution itself — so it proves what it claims. Helpers live in `test/support/`, since both `test/*.sf` and `test/fail/*.sf` are globbed as tests.

**Phase 2b — file → owning package mapping (no new syntax, pre-promotion). ✅ DONE and merged to `main` (`29b4b8f`, source `6a85fc9`).** `file_has_package`, `package_root_of_file`, `package_name_of_file`, `same_module_file`, `same_package`, `loaded_file_paths` in `main.sf`, with `test/package_map_test.sh` (8 assertions). Reused `main.sf`'s existing manifest scanner generalised to `_read_toml_value(source, section, key)` rather than adding a third TOML parser — I10, BUGS #27. `@toml` was tried and rejected: its `Map<String, Any>` is refused by the checker, and it would put `main.sf`'s own compilation behind a closure-heavy parse.

Three findings, all of which corrected premises in this document's earlier drafts:

- **Package *names* are not unique.** The repo root and `src/compiler/` both declare `name = "saffron"`, so identity is the manifest **directory**; the name is carried for diagnostics only.
- **"most of `test/*.sf` has no manifest above them" was wrong.** The repo root has a `pantry.toml`, so every file in the tree inherits it, `test/` included. Genuinely packageless files exist only outside the repo. This is what settles §11 decision 1.
- **Path spellings had to be canonicalised**, or root-package files reached via `--stdlib` vs `--lib-path` compared unequal — "different package" about one package.

"No owning package" is an explicit sentinel, never an empty string and never a synthetic name (I2), and `same_package` answers false when **either** side lacks a package rather than calling two packageless files siblings. Nested manifests exist (11 in the tree, including `bazaar/pantry.toml` and `bazaar/frontend/pantry.toml`), so nearest-above governs. Side fix found on the way: the old scanner matched any line *starting with* the key, so `entrypoint =` matched `entry`.

**Phase 3b — widen `ClassDecl.parent: String` → `parents: List<String>` (prerequisite of `protected`). ✅ DONE and merged to `main` (`dc42fbc`, source `0ada95a`), BUGS #111.** Independent of visibility itself: it adds no modifier and no keyword, and fixed a real existing defect. `class_parents` is now `Map<String, List<String>>`, `inherits_from` does a BFS over the inheritance DAG with a visited set, conformance consults every base, and `__class_is_a` is a flattened tag→ancestor-set nested switch rather than a chain walk.

Field layout is deliberately unchanged — only `parents[0]` contributes fields, and only its `init` is forwarded, because the invariant the lowering rests on (parent field index i == child field index i) can hold for exactly one base. Verified no ABI break: for a single-inheritance program the `getelementptr` sets are identical between old and new compilers, and the only IR diff outside the `__class_is_a` rewrite is the removed `@__class_parent_tag` call.

**The premise "`test/pass/multi_inherit.sf` passes today" was wrong.** It *failed* on base with `[codegen] Error: type 'Duck' has no method 'swim'`, as did `test/pass/interfaces.sf` — multiple inheritance was broken outright, not merely unsound for `protected`. Two further bugs surfaced only after the widening, both invisible in totals and caught only by diffing failure name sets: `test/fail/conformance.sf` stopped being rejected (a child's inherited-name set included abstract names, so a requirement satisfied itself), and `class Both extends Requires, Provides` compiled clean and returned 0 because first-wins forwarding aimed at the bodyless symbol. This is also where §11 decision 2's precedence rule comes from.

**This phase reached the promotion gate without crossing it.** Per #96/#100 it is not truly landed until gen2 is promoted; that is a separate, explicitly-approved ceremony.

**Phase 3 — widen the AST, promotion-gated. ✅ DONE and merged to `main` (`85f410b`, artifacts `1226958`).** One commit per node, smallest-first, each with **all** its match arms in the same commit: `TypeAlias` 2→3 (15 sites), `EnumDecl` 3→4 (39), `VarDecl` 4→5 (54), `FunDecl` 5→6 (52), `Param` 3→4 (65), `ClassDecl` 6→7 (71). `test/pass/enum_wide_payload.sf` gained a `SevenMixed` variant with `ClassDecl`'s exact payload shape (two interior `List`s, two trailing `String`s). Bootstrap with stage 2 after each, `SKIP_GEN4` never set.

The arm counts predicted above were low across the board — the real site counts are the ones just listed. Three findings, each of which corrected a premise in this document or in `CLAUDE.md`:

- **The field goes LAST on every node, and the position is the whole safety argument.** Appending leaves every pre-existing field at its old index, so an arm that gets missed merely lacks `visibility`. Inserting first or mid-node would shift `name` and `docstring` — both `String` — and a missed arm would read a *plausible wrong string*, which is #96's exact mechanism. Do not reorder these nodes later.
- **`src/compiler/codegen.sf` past line 557 is live source, not a skeleton.** The class closes there; everything after is top-level free functions that `sed` copies through verbatim, holding 42 `ClassDecl`, 12 `EnumDecl` and 4 `VarDecl` sites on its own. `CLAUDE.md` claimed only `*_body.sf` files affect the build; corrected in `a55f4ae`. Following the old claim would have skipped all 58 sites — the single highest-risk item in the phase.
- **`Param` names two unrelated nodes.** `src/lib/llvm/function.sf:7` declares an independent 2-field `class Param` for LLVM function parameters, spelled `Func.Param(...)` at 55 sites. It must not be widened. `tools/arity_check.py` (committed) excludes it by file *and* by qualifier.

Two construction sites needed judgement rather than a literal: `parser.sf`'s `inject_field_defaults` and `checker.sf`'s block-parameter inference both **rebuild** an existing node, so both carry the original's visibility across. A hardcoded `"public"` at either would silently strip `private init` the moment Phase 4's parser starts producing it. The rule for the remaining ~50 literals: am I creating a declaration the user never wrote, or reconstructing one they did?

Verified: failure name sets byte-identical, 26 = 26, 171 passed on both sides, both measured in detached `/tmp` worktrees (`11d4f94` vs `f7f9569`) per Phase 0; gen4 fixed point confirmed by grepping the log rather than trusting exit 0; `build/stage2/saffronc` unchanged. One real merge conflict, in `checker.sf`'s `register_fun_param_sigs`, where `main` had extracted a `fun_param_sig_of` helper on the same two lines this phase widened — both changes wanted, resolved by combination rather than choice. This is the second time the one hand-resolved hunk of a phase merge was in `checker.sf` and had to be *combined*.

Because gen2 must be able to *read* the widened payloads before compiler source relies on them, **this is where promotion happens.** The parser writes `"public"` at every site for now; there is no syntax yet.

> ### ⟵ PROMOTION POINT — ✅ CROSSED (`332aafb`, 2026-08-02)
> The checked-in gen2 can now read all six widened declaration nodes, which is what makes Phases 3 and 3b landed rather than merely merged. Every criterion was checked rather than assumed: both bootstrap stages with stage 2 confirmed *present in the log* (not inferred from exit 0), `hello_bootstrap.sf`, the #100 class-plus-import check, and a full suite whose failure **name set** is byte-identical to the pre-promotion baseline at `f7f9569` — 171 passed, 26 failed, 14 skipped.
>
> Recovery path, recorded because this binary is the sole root of trust and cannot be regenerated: the previous gen2 is blob `ab69bef8`, reachable as `1226958:build/stage2/saffronc`.
>
> Everything from Phase 4 on is now unblocked. Compiler source still uses no modifier — that is Phase 7 and stays deferred.
>
> The ceremony that was run:
> ```
> ./bootstrap.sh                              # both stages, no SKIP_GEN4
> tools/saffron run test/hello_bootstrap.sf
> # plus the #100 check: a program with a class AND an import
> cp build/saffronc build/stage2/saffronc
> ./bootstrap.sh                              # promoted gen2 still bootstraps
> git add build/stage2/saffronc && git commit -m "Promote gen2: widened declaration nodes carry visibility"
> ```
> This is a ceremony with a decision attached, not a build step. It also must not be done while someone else has `build/` in flight.

**Phase 4 — lexer + parser + class-member enforcement (post-promotion). ← in progress.** Soft keywords; statement dispatch; the class-body loop (watching the non-advancing-token hang from §5); enforcement for private fields, methods and `init`. Land the Phase 1 leak pass here, with tests. Class-member visibility needs no module plumbing, so it is the narrowest useful slice and ships first.

**It can be *built* before the promotion, but not *landed*.** The distinction matters and is worth stating, because "post-promotion" reads as "cannot start". Phase 4 makes the parser accept modifiers in *user programs*; it does not put a modifier in compiler source, which is Phase 7 and deferred. So gen2 compiles Phase 4's source fine and its work can proceed concurrently with the promotion decision. What it cannot do is *rely* on the widened payloads being readable by the checked-in gen2 — per #100 that is exactly what promotion buys, so Phase 4 merges after the gate, not before.

**Phase 5 — module-level enforcement (post-promotion, needs Phase 2).** Top-level `private`, all three access paths (named import, alias-qualified, bare cross-module). This is where Phase 2 pays off.

**Phase 5's plumbing slice is done and landed (`c8153d8`), ahead of the enforcement itself.** Phase 2 declared `decl_module_package` and deliberately never wrote to it, with a comment forbidding the shortcut: package membership must come from `main.sf`'s `pantry.toml` reading, and re-deriving it from the module prefix string is the M1 antipattern. That seam is now closed. `main.sf` gains `package_roots_joined(module_file_paths)` — a newline-joined list of owning package **roots**, indexed by the same `i` as `prefixes_joined` and `module_boundaries` — and `check_errors_with_module_packages` decodes it and sets `current_package` in exactly the place that already sets `current_prefix`. `register_class_identity` records it under the same qualified key as `decl_module_file`, so the two answers about one declaration cannot drift.

Four choices in it that a later reader should not undo:

- **`module_file_paths` is a parallel list, pushed in lockstep at all three push sites, not derived afterwards from `loaded_file_paths()`.** That map is in *load* order and holds files that are not modules, so recovering the correspondence later would re-derive what the walk already knew.
- **Newline, not `"|"`, as the separator.** These entries are filesystem paths and `"|"` is legal in one. It is safe for `prefixes_joined` only because prefixes are identifiers.
- **The value is a package root, never the declared name** — same reason `same_package` compares roots: two distinct packages in this tree both declare `name = "saffron"`.
- **The packageless marker has exactly one definition**, `Checker.no_package_marker()`, which `main.sf`'s `_no_package_marker()` delegates to. Two literals that must stay equal is a silent-drift hazard here: a mismatch compiles clean and simply makes every `internal` check answer "different package". Relatedly, a missing or short root list yields the marker, so the degraded path **denies** `internal` rather than granting it — which is what keeps `check_errors_with_imports` valid and conservative.

This was the piece of Phase 5 that needs no new syntax, which is why it could run concurrently with Phase 4 rather than waiting behind it. What remains for Phase 5 proper is the enforcement: the three access paths, and the same-file/same-package tests that read these two maps.

**Phase 6 — annotate the stdlib (post-promotion).** The 156 top-level underscore functions and the 66 class-private-safe underscore fields. Opt-in, incremental, one module per commit. This is the feature's real proving ground.

**Phase 7 — annotate the compiler's own source. Deferred, not part of this work.** See §9.

**Phase 8+ — deferred features.** ~~Widen `ClassDecl.parent` to `List<String>`, which unblocks `protected` and closes L7's coverage hole.~~ **Done — it became Phase 3b and landed 2026-08-02** (BUGS #111): it is independent of visibility, and multiple inheritance was not merely unsound for `protected` but broken outright, so it was worth doing on its own account. What remains deferred here is `protected` itself and L7's coverage hole.

Nothing in Phases 1, 2, 2b, 3 or 3b may use a visibility modifier in compiler source. Nothing at all may use one before the Phase 3 promotion.

### Ordering summary

Sequential spine, each step gated on the one above:

```
Phase 2   file identity in checker        DONE  merged fcf0bc1   ─┐ ran in
Phase 2b  file → package mapping          DONE  merged 29b4b8f   ─┤ parallel,
Phase 3b  ClassDecl.parents: List<String> DONE  merged dc42fbc   ─┘ 3 worktrees
                    │
Phase 3   widen declaration nodes for visibility  DONE  merged 85f410b
                    │
            ⟵ GEN2 PROMOTION ⟶   DONE  332aafb  (covered 3b and 3 together)
                    │
Phase 4   parser + class-member enforcement + leak pass (public/private)
          ← in progress, and now landable
Phase 5   package-level enforcement (internal)   ← needs 2b
Phase 5b  protected                              ← needs 3b
Phase 6   annotate the stdlib
```

**What can be parallelised, and what cannot.** Phases 2, 2b and 3b were genuinely independent — different files, no shared tables — and were run concurrently in three worktrees, then merged with one hand-resolved hunk (the `ClassDecl` arm of `check_stmt`, where Phase 2's prefix-keyed sets and Phase 3b's list-valued parents are complementary and had to be *combined*, not chosen between). Failure name sets went 32 → 30 with zero regressions.

Phase 3 was **serialised by the promotion gate**: a single checked-in gen2 is the root of trust, so two agents cannot both be mid-promotion, and no post-promotion phase could start before it. Parallelising across the gate would have produced two incompatible gen2 candidates. Phase 3 additionally could not be parallelised *with* anything that writes match arms on these nodes — an agent editing a `FunDecl(` arm while Phase 3 changes its arity is #96's exact mechanism.

Since 3b and 3 both widened declaration nodes and 3b stopped at the gate, **one promotion covered both.** Promoting after 3b alone would have spent the ceremony twice for no benefit.

**With the gate crossed (`332aafb`), the serialisation is lifted.** Phases 4, 5, 5b and 6 need no further promotion — none of them widens a declaration node, and none puts a modifier in compiler source (that is Phase 7, deferred). So they can run concurrently, subject to two real couplings rather than the gate:

- **5b (`protected`) reads what 4 writes.** Both touch the same class-member access check. Runnable in parallel, but expect the hand-resolved hunk to be exactly there, and expect it to need *combining* — that has now been the shape of the one conflict in three successive phase merges.
- **6 (annotate the stdlib) is the proving ground for 4 and 5, so it cannot precede them meaningfully.** Annotating before enforcement exists means annotations nothing checks, which is the same "did the check run or find nothing?" ambiguity that kept Phase 1's leak pass from landing early. Start 6 only once 4 and 5 enforce.

One caveat that outlives the gate: a phase that *does* end up widening a declaration node re-opens a promotion of its own. Nothing in 4, 5, 5b or 6 is expected to, but if one finds it needs to, that is a stop-and-report, not a judgement call to make mid-phase.

## 8. Test plan

The fail suite is how visibility is proven to actually deny — a `test/fail/` file that compiles cleanly is itself a suite failure.

**Runner requirement, found while planning — and already satisfied.** `tools/run_tests.sh` globs `for f in "$ROOT"/test/fail/*.sf` (`:353`) and applies **no** skip list in that loop (`NOT_A_TEST` / `STALE_TESTS` are only consulted in the `test/*.sf` loop, `:317-326`). So a *helper* module placed in `test/fail/` would be executed as a test and would have to fail on its own, which is impossible for a helper that must compile. Cross-module fail tests therefore need helpers outside that glob.

No new directory is needed: **`test/support/` already exists** and is globbed by none of the three loops, which read only `test/*.sf`, `test/pass/*.sf` and `test/fail/*.sf`. Phase 2 created it for `visibility_collision_client.sf` / `visibility_collision_server.sf`. Put Phase 5's cross-module fail helpers there and leave the runner unchanged (§11 decision 4).

**`test/fail/` — must be rejected. Every one uses a statically-known receiver, so none can escape down §3's soft-fail path.**

- `private_field_access.sf` — read a private field through a locally-annotated receiver
- `private_method_call.sf`
- `private_init_external.sf` — construct a `private init` class from another module
- `private_module_fun.sf` — named import of a private top-level function (helper in `test/fail_support/`)
- `private_module_bare_ref.sf` — the bare cross-module path, the one most likely to be missed
- `private_alias_access.sf` — `Mod._thing` after `import ... as Mod`
- `private_abstract_interface_method.sf` — the §6a contradiction
- `leak_public_fun_private_return.sf` (L3)
- `leak_public_fun_private_param.sf` (L2)
- `leak_public_field_private_type.sf` (L1)
- `leak_generic_private_nested.sf` — `Map<String, List<Private>>`, proving L5 is transitive and not top-level-only
- `leak_union_private.sf` (L6)
- `leak_public_extends_private.sf` (L7)
- `leak_public_extends_private_secondary.sf` — `public class C extends PublicA, PrivateB`, the base that Phase 3b made visible. Without it L7 would silently regress to first-base-only and nothing would notice (§6b).
- `leak_inferred_private_global.sf` (L9, the anchored/error half)
- `internal_cross_package.sf` — an `internal` declaration read from a *different* package. Needs a helper under a second `pantry.toml`; `test/testpkg/pantry.toml` already exists as a precedent (§11 decision 1).
- `protected_from_unrelated_class.sf` — a `protected` member read from a class that is not a subclass (§5b).

**`test/pass/` — must compile and run.**

- `visibility_default_public.sf` — unannotated members remain accessible. This is the backward-compatibility guarantee, asserted rather than assumed.
- `private_underscore_coexist.sf` — an unannotated `_`-named member is public. Guards §2 against a future "let's make `_` mean private".
- `private_within_class.sf` — private field and method used from a sibling method, **and** via `other._x` in an operator overload (the `int.sf` / `float.sf` / `string.sf` shape, 39 such accesses today).
- `private_init_factory.sf` — `private init` plus same-module `fun new()`, the `stack.sf` shape.
- `private_module_internal.sf` — private top-level function called from inside its own module, public wrapper exported.
- `leak_private_to_private_ok.sf` — private function taking a private type: no diagnostic.
- `leak_public_member_of_private_class.sf` — L4's no-op.
- `visibility_response_collision.sf` — two same-named classes in two modules, one with a private field, the other's like-named field read publicly. This is the `http/server.Response` / `http/client.Response` regression, and it fails before Phase 2. **Landed with Phase 2**, and verified to fail on base with the misattribution itself (`client_payload: cannot assign Int to String`) rather than passing on both sides.
- `internal_same_package.sf` — an `internal` declaration read from a *different file in the same package*. This is the case that distinguishes `internal` from `private`; without it, an `internal` implemented as a synonym for `private` would pass the whole suite.
- `internal_no_package_warns.sf` — `internal` in a file with no owning `pantry.toml` degrades to file-scope **and warns** (§11 decision 1). Needs a `.expected` for the warning, per the soft-fail note below. Note this test cannot live in the repo tree, since the root `pantry.toml` gives every file in it a package — it needs the runner to compile a file from a temp dir, as `test/package_map_test.sh` already does.
- `protected_from_subclass_of_secondary_base.sf` — a `protected` member on base 2 or 3, read from a subclass. This is the case Phase 3b unblocked and the one §11 decision 2's precedence rule governs.
- extend `enum_wide_payload.sf` to seven fields (Phase 3's guard).

**Soft-fail warning path.** Warnings do not block, so this cannot be a fail test. The runner diffs stdout against an optional `.expected` file (added in `e5b4a7b` for #90), and `error_report` prints warnings via `IO.println("[checker] Warning: ...")`. So `test/pass/private_unresolved_receiver.sf` plus a `.expected` capturing the warning text is the right instrument — and it is what stops the soft-fail from silently becoming a no-op.

## 9. Explicitly out of scope

**Superseded by the Kotlin-parity decision — these are now IN scope, and were out of scope only in this document's first draft:**

- ~~`protected`~~ — **in scope**, ships last of the four, as Phase 5b (§7). It was blocked on `ClassDecl.parent: String` and the parser discarding parents 2..n; **that block is removed** (BUGS #111), so the checker can now resolve a base at any position. It still waits on the widening's own promotion gate.
- ~~a distinct `internal`~~ — **in scope**, package-scoped, gated on Phase 2b (§7).
- ~~module-private fields~~ — **resolved**: the 24 underscore fields read by same-file free functions become `internal`, not "permanently public" (§2).

**Genuinely out of scope:**

- **Private enum variants** — incoherent against exhaustiveness plus #76's indeterminate-value semantics (§6a).
- **`Fun`-mediated leaks (L8)** — structurally impossible; blocked on #56.
- **`friend` / workspace-level visibility** — no boundary the checker can see. (`internal` is now in scope because the *package* boundary does exist — `pantry.toml`, already read at `main.sf:48-49` and `:475`.)
- **Visibility on `@extern` symbols** — C linkage is a different namespace.
- **Runtime enforcement.** `--no-check` ignores visibility entirely, and reflection reads fields dynamically by design (`test/test_reflect.sf`). Visibility is a compile-time hygiene feature, not a security boundary. Intended, documented, not to be "fixed".
- **Re-export control** (`public import`) — separate feature.
- **Anything in the emitted LLVM IR** — codegen learns nothing (§3).
- **Annotating the compiler's own source.** Deferred deliberately, for three reasons: (a) the compiler is the one program whose miscompilation is unrecoverable, since `build/stage2/saffronc` is the root of trust; (b) #76 landed the checker into class method bodies only a handful of commits ago, and its own closing note says *"treat a fresh diagnostic in compiler source as real: there is no longer a 'the checker doesn't look here' explanation available"* — introducing a new diagnostic class into compiler source simultaneously with a new checker feature makes a real regression indistinguishable from a pre-existing hole, which is precisely the confusion #76 documents; (c) it would need its own promotion. Annotate the stdlib first (Phase 6) and let it be the proving ground.

## 10. Blockers, stated plainly

**Cleared (2026-08-02):**

1. ~~**`protected` is currently unimplementable soundly** — parser discards all but the first parent; `ClassDecl.parent` is a `String`; `class_parents` is `Map<String,String>`.~~ **Cleared by Phase 3b / BUGS #111.** `parents` is a list, `class_parents` is `Map<String, List<String>>`, and `inherits_from` searches the whole DAG. `protected` still waits on the promotion gate, but nothing about the representation blocks it now.
2. ~~**Module-level visibility is blocked** until the checker receives `module_boundaries` / `prefixes_joined`.~~ **Cleared by Phase 2.** `check_errors_with_modules(...)` receives both.
3. ~~**Bare-name class keying will produce a false denial today** on `http/server.Response` vs `http/client.Response`.~~ **Cleared by Phase 2**, which keys the class tables by prefix + name and adds the missing `ambiguous_classes` guard. `test/pass/visibility_response_collision.sf` pins it, and was confirmed to fail on base with the misattribution itself.

**Still live:**

4. **Closure-mediated visibility is structurally uncheckable** — `Fun` carries no signature (#56, #86). L8. Not "hard" — impossible with the information present.
5. **Private enum variants cannot be made coherent** against exhaustiveness given #76/#73's indeterminate-value semantics.
6. **`ClassDecl` at 7 fields re-enters #96/#100 territory** — mitigated only by the promotion ordering in Phase 3, not by cleverness. Still live, and now the *only* thing on the critical path: 3b already widened this node once and is itself waiting at the gate, so one promotion has to carry both.
7. **`ambiguous_enums` was not retired**, though Phase 2 makes it possible: `enum_fields`' key is already compound (`"EnumName.Variant"`), so adding a prefix makes it three-part and all ~10 read sites must agree simultaneously.

## 11. Open decisions for the repo owner

Recorded here rather than resolved, because they are judgment calls about what the language should mean.

**Settled by the owner on 2026-08-02:**

- ~~May a same-file free function read a `private` field?~~ **No** — Kotlin's rule. The 24 affected stdlib fields become `internal` (§2).
- ~~Is Phase 2 in scope?~~ **Yes.** The whole plan is in scope; Phase 2 is a confirmed prerequisite, not a candidate for splitting out.
- ~~Which modifiers?~~ **Kotlin parity**: `public`/`private`/`internal`/`protected` (§1).

**Resolved 2026-08-02 by evidence in the tree, not by preference. All four are reversible — they are design-doc decisions, and nothing has been built on them yet.**

1. **`internal` in a file with no owning `pantry.toml`** → **(a) degrade to file-scope with a warning.**

   The options were (a) degrade with a warning; (b) hard error, "`internal` requires a package"; (c) treat the file as its own singleton package. (c) is out on principle: it silently makes `internal` mean `private`, which is spelling unknown as something concrete — I2's exact prohibition.

   Between (a) and (b), one measured fact settles it and it corrected a premise in this document's own earlier drafts: **the repo root has a `pantry.toml`, so every file in the tree inherits it, including all of `test/`.** Genuinely packageless files exist only *outside* the repo — a bare script someone runs from `/tmp`. So (b)'s strictness would buy nothing inside any real project and would reject exactly the throwaway-script case where a hard error is most annoying. (a) it is, and Phase 2b already built the I2-compliant substrate for it: `_no_package_marker()` is a distinct sentinel, never an empty string, and `same_package` answers false when *either* side lacks a package rather than calling two packageless files siblings.

2. **`protected` and multiple inheritance** → **follow the rule the tree already has: earlier bases win ties, in declaration order, and abstract names do not claim a tie.**

   Kotlin has no multiple inheritance so parity gives no answer, but Saffron does not need to invent one — BUGS #111 already had to settle this exact question for *method forwarding*, and it did (`codegen/stmts_body.sf:442-448`):

   > Register inherited methods: if child doesn't define a method, map it to parent's — for EVERY base, in declaration order, not just the first. […] Earlier bases win ties, since `str_in_list(child_methods, ...)` sees names pushed by previous iterations — the same first-wins rule an override already had.

   plus the refinement that a base's **bodyless** names are skipped, so a concrete method on a later base is not shadowed by an abstract declaration on an earlier one (`class Both extends Requires, Provides`).

   A `protected` member visible through two bases is the same shape of question, so it gets the same answer rather than a second, competing precedence rule. Erroring at the collision was the alternative and is rejected: it would make a `protected` annotation on a widely-inherited interface a breaking change for every existing multiple-inheritance user, and it contradicts a rule already shipped one layer down. Requiring qualification is rejected for the same reason plus there is no syntax for it.

3. **L9's error/warn split** → **keep it as designed** (error when the initializer is syntactically a private constructor call, warning otherwise).

   The precedent is direct and recorded in #56: erroring unconditionally on the `MemberAccess` fallback "broke `pass/math` and `test_reflect` — one silent wrong answer traded for a batch of false rejections." A uniform error here would repeat that trade on inferred types, where the anchor for the diagnostic is the compiler's own guess. This is the §3 posture applied to inference: say what you know, and say when you do not know.

4. **`test/fail_support/`** → **not needed. Use the existing `test/support/`, and leave `run_tests.sh` unchanged.**

   The premise behind the question was right — the fail loop (`run_tests.sh:353`) applies no skip list, so a helper placed in `test/fail/` would be run as a test and would have to fail on its own, which a helper cannot do. But the conclusion was wrong: **`test/support/` already exists** (Phase 2 created it for `visibility_collision_client.sf` / `visibility_collision_server.sf`) and is globbed by none of the three loops, which only read `test/*.sf`, `test/pass/*.sf` and `test/fail/*.sf`. So the plumbing prerequisite §8 flagged is already satisfied, no new directory is warranted, and the runner needs no change.
