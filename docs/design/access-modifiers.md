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

Kotlin parity means `protected` ships. It is currently **unimplementable soundly**, so the blocker below is promoted from "deferred Phase 8" into a prerequisite of the `protected` slice.

### Why not `protected`

`protected` is not merely unattractive here; it is **currently unimplementable soundly**, and that is a fact about the tree, not a preference.

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
- class-body loop (`parse_class_decl_with_doc`, `parser.sf:1985-2041`): the loop advances only inside its `var` / `@` / `fun` branches. **A token it recognizes in none of them does not advance the cursor — the loop spins forever.** So `private` must be handled in that loop before anything else, or a `private var` inside a class hangs the compiler. Needs verification against gen2's behavior; do not discover it by compiling.

## 6. Coverage by declaration kind, and the leakage check

### 6a. Per-kind decisions

**Top-level `fun`, `var`/`let`, `class`, `enum`, `interface`, `actor`, `type` alias.** All support `private` = module-private. Three access paths must be checked, not one:
1. named import — `import { name } from "@mod"`. Resolved in `main.sf` into the `named_imports` map (bare name → prefixed name). A private target must be rejected at the import, naming the module.
2. alias-qualified — `Mod.name` after `import "mod" as Mod`. Reaches the checker as `MemberAccess`/`Ref` and is handled in `infer_member_access`.
3. bare cross-module reference — resolve.sf's prescan registers *every* module's globals and funcs into one `Resolver`, so a bare `helper()` in the main program can bind to a module's function. This is the path most likely to be forgotten; it must be checked via `Ref.slot` (which carries the defining module's prefix) against the use site's prefix.

All three require Phase 2's module plumbing.

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

**L7, and its coverage hole.** Only the *first* parent is recorded (§1), so for `public class C extends PrivateA, PrivateB` this check catches `PrivateA` and silently misses `PrivateB`. That is the same parser truncation that blocks `protected`, and it must be recorded as a known hole with the same fix.

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

**Phase 2 — module plumbing into the checker (no new syntax, pre-promotion). ✅ DONE, `85bfb13`, pending merge.** Threaded `module_boundaries` + `prefixes_joined` into the checker via `check_errors_with_modules(...)`, decoding them exactly as `Resolve.resolve_imports` does rather than inventing a second scheme; `check_errors_with_imports` is now a thin wrapper that degrades to the old flat behaviour on an empty boundary list. Class tables re-keyed by prefix + name, with the missing `ambiguous_classes` guard added — an ambiguous bare name answers `Any` (widening) rather than inventing a mismatch, matching `get_variant_fields`' posture.

Two findings worth carrying forward:

- **Locality cannot be inferred from the key.** The module under check is unprefixed, so its qualified and bare keys are the *same string*. A first implementation therefore widened a program's own class to `Any` whenever any import declared that name. Locality is now recorded explicitly and a local declaration wins its bare name. Any later phase keying on qualified names must not re-derive locality from the key shape.
- **`ambiguous_enums` was NOT retired**, though Phase 2 makes it possible. `enum_fields`' key is already compound (`"EnumName.Variant"`), so adding a prefix makes it three-part and all ~10 read sites must agree simultaneously. Deferred with a comment recording that it is now possible.

Verified: bootstrap both stages; failure sets byte-identical to base (28 failures, same names, no swap; passing 147 → 148); `build/stage2/saffronc` provably unchanged. `test/pass/visibility_response_collision.sf` was confirmed to **fail on base** — the unchanged gen2 rejects it with `ERROR: client_payload: cannot assign Int to String`, the misattribution itself — so it proves what it claims. Helpers live in `test/support/`, since both `test/*.sf` and `test/fail/*.sf` are globbed as tests.

**Phase 2b — file → owning package mapping (no new syntax, pre-promotion).** Required by `internal`, which is package-scoped. Build it alongside `collect_modules`' existing `path_to_prefix` machinery, reusing `main.sf`'s existing `pantry.toml` reading (now inside `8b5eadf`'s consolidated import helper) rather than a second manifest parser — I10, and BUGS #27 records what a duplicate parser costs. **"No owning package" must be an explicit distinct value**, never an empty string and never a synthetic name: a bare script and most of `test/*.sf` have no manifest above them, and spelling that unknown as a concrete package is I2's exact prohibition. Nested manifests exist (`bazaar/pantry.toml` and `bazaar/frontend/pantry.toml`), so nearest-above governs.

**Phase 3b — widen `ClassDecl.parent: String` → `parents: List<String>` (prerequisite of `protected`).** Independent of visibility itself: it adds no modifier and no keyword, and fixes a real existing defect — `parser.sf` parses `class Duck extends Flyable, Swimmable, Walkable` and discards every parent after the first. Propagate through `class_parents`, `class_parent_of`, `is_subtype_node`, conformance, and codegen's field-prepending and `__class_parent_tag` switch, **preserving single-parent layout exactly** (a layout change is an ABI break against the checked-in gen2). Note `test/pass/multi_inherit.sf` passes today, which means it exercises only the first parent; strengthening it must be verified to *fail* before the fix. Interfaces and actors route through `ClassDecl` too, so both are in blast radius. Promotion-gated for the same #96/#100 reason as Phase 3.

**Phase 3 — widen the AST, promotion-gated.** One commit per node, smallest-first, each with **all** its match arms updated in the same commit (`Param` 34, `FunDecl` 44, `ClassDecl` 67, then `EnumDecl`/`VarDecl`/`TypeAlias`). Extend `test/pass/enum_wide_payload.sf` to a seven-field variant, since it exists as the regression test for exactly this hazard. Bootstrap with stage 2 after each. Because gen2 must be able to *read* the widened payloads before compiler source relies on them, **this is where promotion happens.**

> ### ⟵ PROMOTION POINT
> After Phase 3, run the full ceremony:
> ```
> ./bootstrap.sh                              # both stages, no SKIP_GEN4
> tools/saffron run test/hello_bootstrap.sf
> # plus the #100 check: a program with a class AND an import
> cp build/saffronc build/stage2/saffronc
> ./bootstrap.sh                              # promoted gen2 still bootstraps
> git add build/stage2/saffronc && git commit -m "Promote gen2: widened declaration nodes carry visibility"
> ```
> This is a ceremony with a decision attached, not a build step. It also must not be done while someone else has `build/` in flight.

**Phase 4 — lexer + parser + class-member enforcement (post-promotion).** Soft keywords; statement dispatch; the class-body loop (watching the non-advancing-token hang from §5); enforcement for private fields, methods and `init`. Land the Phase 1 leak pass here, with tests. Class-member visibility needs no module plumbing, so it is the narrowest useful slice and ships first.

**Phase 5 — module-level enforcement (post-promotion, needs Phase 2).** Top-level `private`, all three access paths (named import, alias-qualified, bare cross-module). This is where Phase 2 pays off.

**Phase 6 — annotate the stdlib (post-promotion).** The 156 top-level underscore functions and the 66 class-private-safe underscore fields. Opt-in, incremental, one module per commit. This is the feature's real proving ground.

**Phase 7 — annotate the compiler's own source. Deferred, not part of this work.** See §9.

**Phase 8+ — deferred features.** ~~Widen `ClassDecl.parent` to `List<String>`, which unblocks `protected` and closes L7's coverage hole.~~ **Done — it became Phase 3b and landed 2026-08-02** (BUGS #111): it is independent of visibility, and multiple inheritance was not merely unsound for `protected` but broken outright, so it was worth doing on its own account. What remains deferred here is `protected` itself and L7's coverage hole.

Nothing in Phases 1, 2, 2b, 3 or 3b may use a visibility modifier in compiler source. Nothing at all may use one before the Phase 3 promotion.

### Ordering summary

Sequential spine, each step gated on the one above:

```
Phase 2   file identity in checker        DONE (85bfb13)   ─┐ parallel,
Phase 2b  file → package mapping          in progress      ─┤ independent
Phase 3b  ClassDecl.parents: List<String> in progress      ─┘ (3 worktrees)
                    │
Phase 3   widen declaration nodes for visibility
                    │
            ⟵ GEN2 PROMOTION ⟶
                    │
Phase 4   parser + class-member enforcement + leak pass (public/private)
Phase 5   package-level enforcement (internal)   ← needs 2b
Phase 5b  protected                              ← needs 3b
Phase 6   annotate the stdlib
```

**What can be parallelised, and what cannot.** Phases 2, 2b and 3b are genuinely independent — different files, no shared tables — and are being run concurrently in separate worktrees. Everything from Phase 3 onward is **serialised by the promotion gate**: a single checked-in gen2 is the root of trust, so two agents cannot both be mid-promotion, and no post-promotion phase can start before it. Parallelising across the gate would produce two incompatible gen2 candidates; do not attempt it.

## 8. Test plan

The fail suite is how visibility is proven to actually deny — a `test/fail/` file that compiles cleanly is itself a suite failure.

**Runner requirement, found while planning.** `tools/run_tests.sh` globs `for f in "$ROOT"/test/fail/*.sf` and applies **no** skip list in that loop (`NOT_A_TEST` / `STALE_TESTS` are only consulted in the `test/*.sf` loop). So a *helper* module placed in `test/fail/` would be executed as a test and would have to fail on its own, which is impossible for a helper that must compile. Cross-module fail tests therefore need helpers outside that glob — a `test/fail_support/` directory, with the runner left unchanged. This is a real plumbing prerequisite for Phase 5's tests, not a detail.

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
- `leak_inferred_private_global.sf` (L9, the anchored/error half)

**`test/pass/` — must compile and run.**

- `visibility_default_public.sf` — unannotated members remain accessible. This is the backward-compatibility guarantee, asserted rather than assumed.
- `private_underscore_coexist.sf` — an unannotated `_`-named member is public. Guards §2 against a future "let's make `_` mean private".
- `private_within_class.sf` — private field and method used from a sibling method, **and** via `other._x` in an operator overload (the `int.sf` / `float.sf` / `string.sf` shape, 39 such accesses today).
- `private_init_factory.sf` — `private init` plus same-module `fun new()`, the `stack.sf` shape.
- `private_module_internal.sf` — private top-level function called from inside its own module, public wrapper exported.
- `leak_private_to_private_ok.sf` — private function taking a private type: no diagnostic.
- `leak_public_member_of_private_class.sf` — L4's no-op.
- `visibility_response_collision.sf` — two same-named classes in two modules, one with a private field, the other's like-named field read publicly. This is the `http/server.Response` / `http/client.Response` regression, and it fails before Phase 2.
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

1. **`protected` is currently unimplementable soundly** — parser discards all but the first parent (`parser.sf:1977-1979`); `ClassDecl.parent` is a `String`; `class_parents` is `Map<String,String>`.
2. **Module-level visibility is blocked** until the checker receives `module_boundaries` / `prefixes_joined` (`main.sf:1104` passes neither). Phase 2.
3. **Bare-name class keying will produce a false denial today** on `http/server.Response` vs `http/client.Response`; there is an `ambiguous_enums` guard and no `ambiguous_classes`. Phase 2.
4. **Closure-mediated visibility is structurally uncheckable** — `Fun` carries no signature (#56, #86).
5. **Private enum variants cannot be made coherent** against exhaustiveness given #76/#73's indeterminate-value semantics.
6. **`ClassDecl` at 7 fields re-enters #96/#100 territory** — mitigated only by the promotion ordering in Phase 3, not by cleverness.

## 11. Open decisions for the repo owner

Recorded here rather than resolved, because they are judgment calls about what the language should mean.

**Settled by the owner on 2026-08-02:**

- ~~May a same-file free function read a `private` field?~~ **No** — Kotlin's rule. The 24 affected stdlib fields become `internal` (§2).
- ~~Is Phase 2 in scope?~~ **Yes.** The whole plan is in scope; Phase 2 is a confirmed prerequisite, not a candidate for splitting out.
- ~~Which modifiers?~~ **Kotlin parity**: `public`/`private`/`internal`/`protected` (§1).

**Still open:**

1. **`internal` in a file with no owning `pantry.toml`** — bare scripts and most of `test/`. Options: (a) degrade to file-scope with a warning; (b) hard error, "`internal` requires a package"; (c) treat the file as its own singleton package. (a) is the I2-consistent default and is what this document assumes, but (b) is defensible and stricter, and (c) silently makes `internal` mean `private` — which is spelling unknown as something concrete, so it is the weakest of the three.
2. **`protected` and multiple inheritance.** Once `ClassDecl.parents` is a list, `class Duck extends Flyable, Swimmable` inherits `protected` members from several parents at once. Kotlin has no multiple inheritance, so parity gives no answer here — a name declared `protected` in two parents is a genuine ambiguity Saffron has to decide (error at the collision? first-parent wins? require qualification?).
3. **L9's error/warn split** — error only when the initializer is syntactically a private constructor call, warn otherwise. Chosen to avoid #56's over-erroring; uniform error is the alternative, at the cost of some false rejections.
4. **Is `test/fail_support/` acceptable** as a new directory, versus changing `run_tests.sh` to skip designated helpers in the fail glob?
