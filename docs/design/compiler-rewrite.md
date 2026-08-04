# Rewriting the Saffron compiler: a bug-prevention-first design

Status: design proposal, not a plan of record.
Written 2026-07-30 against `11c9638`.

This document answers one question: **if we rebuilt the core compiler from
scratch, what would we do differently so that the bugs we actually have could
not have been written?**

It is deliberately grounded. Every design rule below is justified by a specific
entry in `BUGS.md` or a specific measurable property of the current tree, not by
general compiler folklore. Where a rule would require a language feature Saffron
does not yet have, that is stated instead of hidden.

---

## Part 0 — What went wrong, mechanically

Before proposing anything, the failure modes. I grouped every open and recently
fixed bug in `BUGS.md` by *mechanism*, not by symptom. Five mechanisms account
for essentially all of them.

### M1. Codegen re-derives types the checker already computed

The checker runs and is then **thrown away**. `src/compiler/main.sf:992-1001`
calls `Checker.check_errors_with_imports(...)`, inspects the returned error
string, and discards everything else. It is a linter, not an elaborator. Grep
confirms codegen never references `Checker`.

Codegen therefore rebuilds inference from scratch, badly, in two places: a
mutable `last_type` field (`codegen.sf:41`) written and read at **265 sites**
across the `*_body.sf` files, and a second independent `get_expr_type`
(`methods_body.sf:113`). Neither has the checker's scope information, its import
tables, or its generic instantiation.

Consequences, all filed: **#32** (`tasks[i].await()` — `get_variable_name`
returns `""` for a non-`Variable`, fallback applied, `__list_length` got a
string), **#33** (`func_ret_types` not populated by the pre-scan, so a
caller-above-callee call inferred `Int`), **#36** (module global `var _items = []`
never inferred, `.push` misdispatched), **#37** (dispatch that matches no branch
returns literal `"0"`), **#25**, **#38**. Six bugs, one mechanism.

### M2. `Int` is the bottom type

`codegen.sf:108` initialises `this.last_type = AST.Type.IntType`, and that is
also the fallback at every resolution site. So "I don't know" is spelled
identically to "I know it's an integer."

This is what converts an inference *gap* into memory corruption rather than a
diagnostic. #32's write-up says it precisely: the fallback "was a *claim* the
value is an integer, when in fact the type is unknown." The fix was to fall back
to `AnyType` — honest, and the runtime dispatch machinery handles it. #33 calls
it "the same dishonest 'unknown means Int' fallback as #32."

#### Measured 2026-08-03: 9 sites, not "every resolution site"

"The fallback at every resolution site" was the right instinct and the wrong
count, and the difference decides how stage 1 is done. There are **23**
`return "Int"` in the compiler, and only **9** of them mean "I don't know". The
other 14 are correct answers that happen to be `Int` — and rewriting those to
`Unknown` would be a regression, not progress:

| legitimate `Int` | why |
|---|---|
| `ek == "int"` | it is an integer literal |
| `length` / `index_of` / `to_number` | those builtins do return `Int` |
| `&` `\|` `^` `<<` `>>` | bitwise operators are integer-typed |
| `ftype == "Bool"` | `Bool` is represented as `i64` |
| the six `identity_mode and ... == "Float"` collapses | `Float` and `Int` *are* the same type there |

The nine that are M2, each with what it actually means:

| site | "I don't know" because |
|---|---|
| `match_body.sf:602` | the enum variant is not registered |
| `match_body.sf:604` | the variant has no recorded fields |
| `match_body.sf:606` | the field index is out of bounds |
| `match_body.sf:609` | the field definition has no `name:type` split |
| `match_body.sf:619` | the type string matched none of the known spellings |
| `methods_body.sf:293` | `.pop()` on a receiver that is not a `List<T>` |
| `methods_body.sf:332` | a binary op where neither side resolved |
| `methods_body.sf:3642` | the class has no registered fields |
| `methods_body.sf:3655` | the field is not on the class |

Five of the nine are in one function (`get_variant_field_type`), which is the
useful part: the mechanism is concentrated, not diffuse. Each of the five is a
*distinct* failure to read enum metadata, so each wants its own diagnostic rather
than one shared "cannot infer" — the message should say which lookup missed.

Practical consequence for the stage-1 ordering: converting these nine cannot be
a `replace_all`. The two families are textually identical and semantically
opposite, so the sweep has to be site-by-site with the classification above in
hand. That is also why "make the `Int` fallback an error" is cheaper than the
migration table implies — nine call sites, one hot function — while being no
less valuable.

#### Landed 2026-08-03: all nine report, and the count on the compiler is zero

The nine sites now call `Diag.record_unresolved(site, what, fallback)`
(`diag.sf`), which records a `SevInternal` into the shared sink and counts it.
They still return `Int`. That ordering is deliberate and is the part worth
keeping: the compiler self-hosts, so a fallback that failed the build today
would fail the bootstrap, and nobody could measure anything. Flipping
`record_unresolved` to `record_error` is a one-line change *once the count is
zero* — which is the ordering BUGS #37 says was missing when 96 silent
fall-throughs went unnoticed.

Measuring it: `--report-unresolved` on `saffronc` prints one line per site plus a
total, and `SAFFRONC_FLAGS=--report-unresolved` threads it through
`tools/saffron` so a sweep sets it once for a whole corpus.

The measurement immediately found real defects, which is the entire argument for
doing this before the rest of the rewrite. On the compiler's own source the count
went **8620 → 2350 → 0**:

- **6270 were not gaps at all.** Two callers use `get_field_type` as an
  *existence probe* — "is `method` a function-typed field rather than a method?"
  — where a miss is the ordinary answer for every method call in the program.
  Extracted `try_get_field_type` (returns `""`, no report) and pointed the probes
  at it. A metric that counts its own control flow measures nothing.
- **2350 were one real defect.** Every remaining report was
  `get_variant_field_type` giving up on a declared type it had in hand: `Expr`
  (989), `Span` (764), `Types.Value` (276), `Type` (191), and six more — *all*
  names the codegen tables already knew. It now resolves through `enum_defs`,
  `resolve_class_type`, and a prefix-stripped enum lookup before reporting.
- **Two more were found by sweeping `test/pass`**, and both were the honest-vs-
  dishonest `Int` distinction reappearing inside the conversion itself. `Int`,
  `Any` and `Nil` were never in `get_variant_field_type`'s spelling list, so an
  honestly-`Int` field reached the right answer *via the fallback* and got
  counted as a guess (45 reports across two files). And `get_expr_type`'s binary
  arm answered `Int` for `Vec2 + Vec2` while `gen_binary`, twenty lines away in
  another file, resolved the same expression to `Vec2__add`'s declared return
  type — two consumers of the same information, one of them guessing.

What is left is residue that is honestly unknown, and it is documented rather
than papered over: unresolved generic parameters (`T`, `E` in
`get_variant_field_type` — a monomorphisation question, not a lookup failure) and
`binary` over `Any` operands, where "Any" is genuinely how an unannotated operand
spells itself. Both belong to stage 3/4, which is where types stop being strings.

`test/pass/unresolved_inference_report.sf` pins the half a conversion like this
can break — that all nine still return `Int` and therefore still compile and run
as before. It deliberately does **not** assert the count: freezing it would make
every legitimate improvement look like a regression.

### M3. Representation is a convention, not a type

Every value is an `i64`. Whether a given `i64` is NaN-boxed, a raw integer, or a
raw address is tracked by programmer discipline spread across codegen, four
runtime `.ll` bases, and a `--identity-mode` flag that makes `Float` and `Int`
the same type in `runtime.sf`.

Filed: **#23** (runtime functions return untagged `i64` into boxed value space —
`OS.system(cmd) == 0` is *always false* while printing as `0`), **#24** (`i64`
extern params passed raw while returns are boxed; **114 of 224** extern
declarations take an `i64`; all networking was dead), **#25** (interpolation
concat leaks a raw `char*` as a method receiver), **#28** (`var f: Float = 1`
became a subnormal), **#26** (canonical quiet NaN is bit-identical to `TAG_PTR`
with a null payload). Plus the GC interaction noted under #23: tagging a
`__gc_alloc`'d pointer makes `__gc_is_heap_ptr` stop tracing it, so the
collector sweeps it while live.

The type system has no vocabulary for the single most safety-critical
distinction in the implementation.

### M4. Implicit, unrestored compiler state

`class Codegen` has **65 mutable fields**. Several encode "where am I" —
`current_function_name`, `current_prefix`, `current_class`, `in_function`,
`register_only`, `is_coroutine`, `block_terminated`, `last_type` — with no
save/restore discipline.

**#40** is a pure instance of this, and the write-up is unusually explicit about
why the obvious fix failed: `current_function_name` "is not a reliable 'am I
inside this function's body' signal. `gen_function` sets it *before* its
`register_only` / already-defined early returns (so the pre-scan leaks a
lambda's name), `gen_closure_function` never sets it at all, and neither
restores it." The attempted fix regressed `pantry_config` into invalid IR and was
reverted.

### M5. Duplicated logic that drifts

- **11** `generate_*` entry points in `codegen.sf`. The module pre-scan loop
  exists in **three** copies. #33's fix and #36's fix each had to be applied to
  all three; #33's first attempt got two of them wrong and only the build caught
  it.
- **Two lexers**: `tokenize` and `read_interpolation`. #27 — comparison
  operators silently dropped inside `"${a != b}"` because the second lexer
  carried a shorter operator table. Partially remedied by a shared
  `lex_operator`.
- **Two copies of the for-in desugaring** (#31), both naming temporaries
  `__for_list` / `__for_i` unconditionally, so nested loops clobbered each other.
- **Four hand-written runtime IR bases** with **65 / 68 / 90 / 121** `define`s
  respectively. **#39** is drift: the wasm32 GC header has no magic sentinel, so
  the native collection-printing fix is unsound there.
- **A half-migrated shadow IR library**: `use_llvm_lib` builds a parallel
  `LLVMMod.Module` alongside string emission, `verify_load`/`verify_store`
  compare the two *as strings* and merely print a mismatch, and several branches
  are stubbed (`// placeholder: string constants need Module.add_global_string()`).
  Cost paid, safety not yet delivered.
- **The bootstrap `sed` assembly**: `codegen/*_body.sf` are the real inputs;
  `codegen/*.sf` are inactive mirrors that "do NOT affect bootstrap." A file that
  looks authoritative and isn't.

### Two cross-cutting absences

- **The AST has no source positions.** `Token` carries `line` and `col`
  (`lexer.sf:84`); `ast.sf` has zero position fields. So no checker or codegen
  diagnostic can point at source. #37's write-up describes the result: an
  undefined variable surfaced as `use of undefined value '%data'` from llc "with
  no source location, no recognisable name."
- **No LLVM verifier gate.** `tools/saffron` never runs `llvm-as` or
  `opt -verify`. Invalid IR is discovered by `clang`, at the end, in IR
  coordinates. `test/fail/` contains 7 files; there are no IR snapshot tests at
  all.

---

## Part 1 — Design invariants

Eleven rules. Each is stated as an invariant, with the mechanism it structurally
eliminates. "Structurally" means: violating it should fail to compile the
compiler, or fail a gate in CI — not rely on review.

### I1. One inference. The checker elaborates; codegen never infers.

The type checker's output is not a string of errors. It is a **typed tree**:

```
                 ┌─ diagnostics (spanned)
check(ast) ──────┤
                 └─ HIR:  every expression node carries a resolved Type
```

Codegen consumes HIR. It has **no** `last_type` field, **no** `get_expr_type`,
**no** `typed_vars` map. The type of an expression is a field on the node it came
from. There is nothing to fall back to because there is nothing to look up.

Enforcement, not convention: HIR expression nodes are constructed only by the
elaborator, and the type field is non-optional. Codegen's `gen_expr` takes
`HIR.Expr` and pattern-matches; every arm has the type in hand.

*Kills M1 entirely: #32, #33, #36, #37, #25, #38.*

### I2. `Unknown` is a distinct type, and it is a hard error at the codegen boundary.

The type lattice gets **three** distinct bottom-ish members, currently conflated:

| Type | Meaning | Where legal |
|---|---|---|
| `Any` | dynamically dispatched, runtime decides | anywhere; user-writable |
| `Unknown` | inference has not resolved this yet | inside inference only |
| `Never` | this expression does not return | anywhere |

`Unknown` may exist during unification. It may **not** appear in HIR. The
HIR-construction boundary asserts this, and an `Unknown` that survives to that
boundary is reported as *"cannot infer type of <expr> at <span>"* — a real
diagnostic with a location, not a silent `Int`.

`Any` remains fully supported and is the honest answer for genuinely dynamic
code; it lowers to the runtime dispatch helpers (`__any_length`, etc.) exactly as
today.

*Kills M2. The "unknown means Int" class becomes unrepresentable.*

### I3. Types are one interned enum. No type is ever a `String`.

Today there are three representations in flight:

- `AST.Type` — the enum in `ast.sf`
- `String` — `Stmt.VarDecl.type_ann`, `Stmt.FunDecl.ret_type`, and **41**
  `: String`-returning inference functions in `checker.sf`
- LLVM type strings — `"i64"`, `"i8*"`, built ad hoc

Rules:

1. Exactly one source-level type representation: the `Type` enum. `type_ann` and
   `ret_type` become `Type`, not `String`. (`Param.type_ann` already is — the
   inconsistency is the tell.)
2. Types are **interned** behind a `TypeId`. Equality is integer equality. This
   removes the whole class of "compared `"Map<String, Number>"` to
   `"Map<String,Number>"` and they differed by a space."
3. LLVM types are a separate, closed enum in the backend. String formatting
   happens once, in the printer.
4. `type_to_string` exists for diagnostics only, and is never re-parsed.

*Kills the parsing-types-out-of-strings hazard visible in
`resolve_type_params(ret_type: String, params_str: String, ...)`.*

### I4. Names are resolved once, into a `DefId` table, before typing.

A dedicated resolve pass sits between parse and check. It walks scopes and
rewrites every `Variable(name)` / `MemberAccess(mod, field)` into a
`Ref(DefId)`, where a `DefId` names exactly one of:

```
Local(slot)      Param(slot)      ModuleGlobal(module, slot)
Function(mid)    Method(class, mid)   Class(cid)   EnumVariant(eid, vid)
```

After this pass, **no later pass ever looks up a name by string**. The three
resolution sites in `expr_body.sf` that check `module_globals` before locals —
the direct cause of **#40** — do not exist, because by then locals and globals
are different `DefId` constructors and shadowing was decided in the one place
that has the scope chain.

Same for **#22** (`Cache.store` emitting an undefined `%Cache`): a
module-qualified global resolves to `ModuleGlobal`, and the backend's `Ref` arm
handles every `DefId` constructor exhaustively. There is no fall-through to
"assume it's a local."

Same for **#30** (`IO.println` missing from `known_functions`): the resolver owns
the builtin table, so a builtin is either resolvable in *all* positions or none.
A name registered for calls but not for value position is not expressible.

*Kills #40, #22, #30, and the `known_functions` string-list pattern generally.*

### I5. Representation is in the type system: `Val`, `Raw<T>`, `Ptr<T>`.

This is the highest-leverage change, and the one the current tree most obviously
lacks vocabulary for.

Introduce a **low-level IR (LIR)** between HIR and LLVM, whose value types are:

```
Val            -- a NaN-boxed Saffron value. Invariant: correctly tagged.
Raw<Int>       -- an untagged machine integer
Raw<Float>     -- an untagged double
Ptr<Heap>      -- a GC-managed address. Untagged. Traceable by the collector.
Ptr<Foreign>   -- a malloc/C address. Untagged. Not traced.
```

Boxing and unboxing are **explicit LIR instructions** (`Box`, `Unbox`,
`TagPtr`, `UntagPtr`), inserted by a single lowering pass. The LIR type checker —
a real pass, run always, not a debug flag — rejects any instruction whose operand
representation doesn't match. Feeding a `Ptr<Heap>` where a `Val` is expected is
a compile error *in the compiler's own IR*, found before a single line of LLVM is
printed.

This turns each of the following from "silent wrong answer" into "LIR type
error":

- **#23** — `OS.system` returns `Raw<Int>`; the caller wants `Val`; missing `Box`
  is a LIR error. The current failure mode (`== 0` silently always false) is
  gone.
- **#24** — extern signatures declare their parameter representations. The 49
  pointer-as-int sites are `Ptr<Foreign>`; the 65 integer sites are `Raw<Int>`.
  The signature *says* which discipline applies, which is exactly what #24
  concludes ("The signature cannot disambiguate because both spell themselves
  `i64` in C and `Int` in Saffron"). No blanket untagging, no allowlist.
- **#25** — string concat produces `Ptr<Heap>`; a method receiver needs `Val`;
  the missing `TagPtr` is caught.
- **#28** — `Float` annotation with an `Int` literal is a representation
  coercion, inserted by the lowering pass on all paths, not just declarations.
- **The GC hazard under #23** — `Ptr<Heap>` is by construction untagged, so
  `__gc_is_heap_ptr` never sees a tagged pointer, so the "collector sweeps it
  while live" trap is closed by typing rather than by remembering.

`--identity-mode` disappears. It exists because `runtime.sf` needs to manipulate
raw values; with `Raw<T>` and `Ptr<T>` in the language, the runtime says so in
its signatures. Removing a whole-program mode flag that silently changes `Float`
semantics (and that 455 sites in the compiler's own source depend on, per #28)
also removes a category of "works in one mode" bugs.

**Honest prerequisite:** this needs Saffron itself to grow either opaque/newtype
declarations or a generic `Raw<T>` class that codegen understands, so that the
compiler's own source can express the distinction. That is a language feature to
land *before* the rewrite, not during it.

### I6. LLVM IR is built as data, printed once.

No `emit(line: String)`. The backend constructs a typed structure:

```
Module   := [Global] × [FnDecl] × [FnDef]
FnDef    := name × [Param] × [Block]
Block    := Label × [Inst] × Terminator     -- terminator is NOT optional
Inst     := Add(dst: Reg, ty, Reg, Reg) | Load(dst, ty, Reg) | Call(...) | ...
Reg      := name × LlvmType
```

Four structural wins:

1. **A block has a terminator by construction.** `block_terminated: Bool` and
   the `if (this.block_terminated) return nil` guard in `emit_indent` — a
   silent-drop mechanism — are gone. You cannot build an unterminated block.
2. **Every register carries its LLVM type**, so a use-before-def or a type
   mismatch is caught when the module is assembled. The `use of undefined value
   '%t140'` signature that #33 and #37 both diagnosed by hand becomes a typed
   error naming the instruction that produced it.
3. **`fresh_local()` returns a `Reg`, not a `String`.** #33's tell — "`%t140` was
   allocated but never defined, the tell-tale of a `fresh_local()` whose branch
   emitted nothing" — is a def-use check on the built module.
4. **One printer**, so opaque-vs-typed-pointer syntax is decided once. The
   current `verify_load`/`verify_store` string comparison, which knows the two
   paths disagree and just prints about it, is deleted rather than fixed.

The existing `src/lib/llvm/*` library is the right idea executed as a shadow
path. Here it is the *only* path. No `use_llvm_lib` flag, no parallel emission,
no placeholders.

### I7. Compiler state is passed, not mutated.

Replace the 65-field `Codegen` god object with:

- `Session` — immutable per-run: target, options, interned types, diagnostics
  sink.
- `Program` — immutable after resolve: `DefId` tables, class layouts, enum tags.
- `FnCtx` — created per function, passed by value into the emitter for that
  function, and **dropped** at the end. Holds current params, register counter,
  block being built, loop targets, coroutine-ness.

There is no "restore the previous value" step to forget, because there is no
shared slot. #40's real fix as filed — "track the parameters of the function
currently being *emitted* explicitly … instead of inferring scope from
`current_function_name`" — is what `FnCtx` *is*, generalised to every one of the
eight implicit-context fields rather than just that one.

### I8. Spans on every node, from token to diagnostic.

`Token` already has `line` and `col`. Every AST node, HIR node, and LIR
instruction carries a `Span` (file, byte offset, length) propagated from the
tokens it was built from. Desugarings propagate the span of the construct they
replaced.

Consequence: a backend error can say *`example.sf:12:5: cannot infer type of
'result'`* instead of an llc message in IR coordinates. This does not prevent
bugs directly — it collapses the cost of diagnosing them, which is where most of
the effort in `BUGS.md` visibly went (#33 required instrumenting the compiler to
print `obj_type`; #32 required disassembling a return address).

### I9. Every dispatch in the compiler is exhaustive. No fall-throughs.

There are **19** `return "0"` sites across the `*_body.sf` files. #37 is one of
them, and it is the mechanism that turned #33 into a segfault and #36 into lost
data.

Rules:

1. The backend matches on HIR/LIR enums. Saffron's checker already verifies enum
   match exhaustiveness (`checker.sf:1822`, `test/fail/exhaustiveness.sf`) — so
   **build the compiler with exhaustiveness as an error**, and a new HIR variant
   cannot be added without every backend arm being updated.
2. Where a total match genuinely can't be written (builtin method tables), the
   fall-through is `internal_error(span, "no lowering for …")` — a loud,
   located, `has_errors`-setting failure. Never a value.
3. `internal_error` is itself covered by a test that asserts a nonzero exit,
   so the "compiler reported success right up to the assembler" failure
   (#37, verbatim) cannot recur.

### I9b. No hand-unrolled loop over a variable-length structure.

`extract_arm_bindings` bound the first **five** fields of an enum variant, as five
copy-pasted `if (bind_count >= n)` lines, because a bug in the long-dead C VM made
a loop over the index unreliable. Construction had no such limit. A sixth field was
therefore stored and never loaded, and the binding kept its uninitialised slot,
which reads as 0 — a null pointer wherever a String was declared (#96).

The cap is not the interesting part; the shape is. An unrolled loop encodes a
maximum in code that reads as if it had none, and nothing connects it to the
producer that has no maximum. It was invisible until an unrelated change — adding
`type_params` to `ClassDecl`, pushing `docstring` to sixth — made the compiler
segfault on every program containing a class. Both halves of the diagnosis were
wrong for a while: gen3 crashing where gen4 did not looked exactly like a gen2
miscompilation, and a separate `send`-dispatch failure looked like a logic bug in
the change under test. Both were this.

Rules:

1. Iterate. A `while` over `bindings.length()` is not slower in any way that
   matters, and it cannot disagree with the producer about how many there are.
2. Where an unrolled form is genuinely required (an IR shape that needs distinct
   register names per slot), assert the length first and `internal_error` above it,
   per I9. Silence is the defect, not the bound.
3. A workaround for a dead backend is a bug with a comment on it. `legacy/`'s VM
   has been unsupported for a long time; every "to avoid VM …" comment in the tree
   is a candidate for the same treatment as this one.

### I10. One source of truth per fact. Duplication is generated, never copied.

- **One `compile()` entry point**, taking an options record. The current 11
  `generate_*` overloads and the three copies of the module pre-scan collapse to
  one. This alone would have prevented the "apply the fix to all three copies"
  hazard that #33 and #36 both hit.
- **One lexer.** Interpolation re-enters the same tokenizer with a mode flag, so
  #27 cannot recur even in principle.
- **Interpolation desugars in the parser, over spanned AST nodes** — not in the
  lexer. #25 exists because the lexer rewrites `"${e}"` into `"" + (e).to_string()
  + ""` as *tokens*, which loses the information that the result is a fresh
  string needing a tag. In HIR it is an `Interp([parts])` node lowered once,
  correctly.
- **One desugaring per construct.** for-in exists once (#31 was two copies), and
  every generated temporary comes from a single monotonic `gensym()` — no
  hand-written `__for_i`.
- **One runtime, four targets.** The single biggest copy-paste liability is four
  hand-maintained `.ll` bases at 65/68/90/121 defines. Instead: write the runtime
  **once** in Saffron (or once in LIR), and *generate* the per-target IR, with
  target differences confined to a small, explicit table (pointer width, GC
  header layout, available syscalls). **#39** — wasm32's GC header lacking the
  magic sentinel that native's has — is a header layout difference that a shared
  header definition would have made impossible to get wrong silently.
- **No inactive mirror files.** `codegen/*.sf` (inactive) alongside
  `codegen/*_body.sf` (active) is a trap for humans and agents alike. One file
  per unit, and the build reads exactly the files that exist.

### I11. The build is a real build, not `sed`.

The `sed`-based assembly of `*_body.sf` into `codegen.sf` at `@codegen-split:`
markers exists to work around the import system's inability to split a class
across files. Fix the cause: give Saffron either `extend fun` working through
imports (the `docs/design/codegen-split-plan.md` direction) or plain multi-file
modules, then delete the sed.

Reason this matters for correctness, not just taste: a textual assembly step
means the file the compiler compiles is not a file anyone edits or reviews, line
numbers in diagnostics point into a generated artifact, and the `_body`/mirror
split silently discards edits.

---

## Part 2 — The pipeline

```
  source
    │  lex                       one lexer, interpolation is a mode
    ▼
  Token[]                        spanned
    │  parse                     no desugaring beyond pure syntax sugar
    ▼
  AST                            spanned, names are strings, types are Type
    │  resolve                   scopes, imports, builtins → DefId
    ▼
  RAST                           every reference is a DefId. no name lookup after here.
    │  elaborate (check)         unification; the ONLY inference in the compiler
    ▼
  HIR                            every expr has a TypeId. no Unknown. desugared.
    │  lower                     representation decisions: Box/Unbox/Tag inserted
    ▼
  LIR                            Val / Raw<T> / Ptr<T>. rep-checked. ← GATE
    │  emit                      structural LLVM module
    ▼
  LlvmModule                     typed regs, terminated blocks, def-use checked ← GATE
    │  print
    ▼
  .ll  ──► opt -verify  ◄──────── GATE (always, not just in CI)
    │
    ▼  clang / wasm-ld
  binary
```

Three internal gates, all cheap, all always-on:

1. **LIR rep-check** — catches the entire #23/#24/#25/#28 family.
2. **LLVM module def-use + type check** — catches #33/#37's `%t140` class before
   llc.
3. **`opt -verify`** on the printed IR — a last net, and free.

Each gate failure is an `internal_error` with a span, which per I9 is a nonzero
exit.

### Why a separate resolve pass, and a separate lower pass

The current tree fuses resolve+check+lower into "codegen, with a checker running
alongside for warnings." Splitting them buys specific things:

- Resolve makes **shadowing a decided fact** rather than an ordering accident
  (#40).
- Resolve runs over the whole program before typing, which makes declaration
  order irrelevant. **#33** — "caller-above-callee ordering in the same file" —
  and the three-copies pre-scan it fixed both disappear: return types are known
  because resolve saw every declaration before typing began.
- Lower is where representation is decided, in one pass, so "tag in codegen or
  in the runtime, never both" (CLAUDE.md's rule, currently enforced by comment)
  becomes "there is exactly one pass that can insert a tag."

### Forward references in nested closures (#2)

Filed as a design limitation, matching Lua/Python. With a real resolve pass it
becomes a *choice* rather than a constraint: resolve can hoist local function
declarations within a block before resolving bodies, making mutual recursion
work. I would take that — the current behaviour is an artifact of
compile-as-you-go, not a decision anyone made.

### break/continue (#6)

Trivially fixed by construction: `Break`/`Continue` are HIR nodes with a
`Never` type and a resolved loop target assigned by resolve. The checker cannot
"just ignore them" because the exhaustive match (I9) requires an arm.

---

## Part 3 — The verification harness

Design invariants prevent bug *classes*. Gates catch instances. This is the part
the current tree is thinnest on: 7 files in `test/fail/`, no IR snapshots, and a
promotion criterion (`CLAUDE.md`: "gen3 can compile itself") that **#34** shows
has never actually been checked — and a hand-built gen4 segfaults on
`IO.println("hi")`.

### Tier 1 — always on, in-process

- LIR rep-check, LLVM def-use check, `opt -verify`. Above.
- **Exhaustiveness as an error** when building the compiler.

### Tier 2 — per-commit

- **IR snapshot tests.** For ~100 small programs, check the emitted `.ll` into
  the repo. A diff is either an intended improvement or a regression, and it is
  *visible in review*. Today an IR change is invisible unless a test's exit code
  moves — which is exactly how #36's 96 silent fall-throughs went unnoticed.
- **A fall-through counter.** ✅ **Done for the `Int` fallback, 2026-08-04.** #36
  and #37 both used "count of silent fall-throughs across the suite" as the
  metric (96 → 8 → 7). It is now a first-class, asserted-zero build statistic:
  `bootstrap.sh`'s STAGE 2 passes `--report-unresolved` on the gen4 `main.sf`
  compile — which already compiles the whole compiler, so the measurement is
  free — and **fails the bootstrap if the count is not 0**.

  Two details worth copying when the next counter gets this treatment. It reads
  the compiler's own printed total rather than `grep -c`-ing the report lines, so
  the number cannot drift from what the channel recorded. And a *missing* total
  fails too: "the statistic disappeared" and "the statistic is zero" must not
  look the same, which is mechanism M2's mistake one level up. This is also what
  lets `record_unresolved` become `record_error` later without a flag day — the
  build already can't regress past zero.

  The remaining counter of this shape is the non-exhaustive `match` fall-through
  (~100 sites, BUGS #76): still measured by hand.
- **Differential testing against a reference interpreter.** Write a simple
  tree-walking interpreter over HIR — a few thousand lines, no codegen, no
  tagging, obviously correct. Every `test/pass/*.sf` runs both ways and outputs
  must match. This is the single highest-value test asset the project doesn't
  have: it catches wrong-answer bugs (#23's silent `== 0`, #36's dropped pushes,
  #38's returned handle) that exit-code testing structurally cannot. Note that
  #37's own write-up had to *measure* fall-throughs across four corpora by hand;
  a differential oracle does that automatically, forever.
- **Property tests on the NaN box.** `unbox(box(x)) == x` over generated ints,
  floats, specials, addresses; and `tag_float` never lands in the tag range
  (this is #26, and it is a one-line property test).
- **`test/fail/` grows a case per fixed bug.** Not just type errors — a case
  for every internal gate.

### Tier 3 — bootstrap integrity

- **Build gen4 and require gen3 ≡ gen4 byte-for-byte.** Fixpoint is the real
  self-hosting check. #34 documents that the criterion is claimed but not run,
  and that gen4 is currently *broken*. Make it a gate, and gen2 promotion becomes
  mechanical rather than a judgement call.
- **A frozen conformance corpus** the promoted compiler must reproduce
  byte-identically, so promotion cannot silently change behaviour.

### Tier 4 — periodic

- **Grammar-directed fuzzing** into the three internal gates. Fuzzers are very
  good at finding "type inference landed somewhere unexpected," which is the
  root of M1/M2.
- **GC stress mode** (collect on every allocation) over the whole suite. #23's
  note that `"abcdefgh".repeat(4)` "prints correctly with no GC pressure but
  garbage under it" is precisely what this finds automatically.

---

## Part 4 — Migration

A from-scratch rewrite of a self-hosted compiler is a trap: you need the old one
to build the new one, and the old one constrains what syntax the new source may
use. So: **incremental, in dependency order, each stage independently
shippable**, and each stage retires a named bug class.

The ordering is forced by two facts: I5 needs a language feature, and I1 needs
resolve to exist first.

| # | Stage | Retires | Notes |
|---|---|---|---|
| 0 | Spans on AST + one `compile()` entry + delete inactive mirrors | diagnosis cost; the 3-copies hazard | **Landed 2026-08-03** (`ide-stage0-spans`). `AST.Span` carries line/col/len with `span_none()` as the absent value; the non-`_body` copies in `codegen/` are deleted and `bootstrap.sh` now fails assembly if a `*_body.sf` has no marker. Unified diagnostics, LSP and formatter came with it. |
| 1 | Introduce `Unknown`/`Never`; make `Int` fallback an error | M2 | **Landed 2026-08-03.** `Unknown`/`Never` in `ast.sf`; all 9 fallbacks report via `Diag.record_unresolved`; `--report-unresolved` measures. Count on the compiler's own source is **0** (from 8620), so the flip to a hard error is now a one-line change. Residue is generic params and `Any` operands — stage 3/4 work. See M2 above. |
| 2 | Resolve pass → `DefId` | #40, #22, #30, #2, #6 | **Landed 2026-08-03.** `src/compiler/resolve.sf`, on by default (`--no-resolve` to opt out), load-bearing. Closed #40, #22, #30 and #6; #2's runtime half is gone, leaving a cosmetic warning. The backend keeps its string paths until stage 4 flips them. |
| 3 | Types as one interned enum; kill string types | the `resolve_type_params` string-surgery class | Touches checker and AST broadly. |
| 4 | Elaborate: checker emits HIR; delete `last_type` + `get_expr_type` | M1 → #32/#33/#36/#37/#38/#25 | The big one. 265 `last_type` sites go away because the field does. |
| 5 | Language: opaque/newtype or `Raw<T>`/`Ptr<T>` in Saffron; promote gen2 | — | Prerequisite for stage 6. A language feature, developed under the existing bootstrap rules. |
| 6 | LIR with `Val`/`Raw`/`Ptr` + rep-check; delete `--identity-mode` | M3 → #23/#24/#25/#28 + the GC-sweep hazard | Requires stage 5. Convert in the atomic groups #23 already identifies (enum construction with match field loads; closures with `gen_indirect_call`; instances with `gen_get_field`) — a partial conversion turns a printing bug into a segfault. |
| 7 | Structural LLVM emission; delete `emit(String)`, `block_terminated`, `use_llvm_lib` | the undefined-`%tN` class | Reuses `src/lib/llvm/*` as the only path. |
| 8 | `Session`/`Program`/`FnCtx`; delete the god object | M4 | Largely falls out of 4 and 7. |
| 9 | Generated runtime; one header definition, four targets | M5 → #39 | Independent of 0-8; can run in parallel. |
| 10 | Replace `sed` assembly with real modules | build integrity | Blocked on **one** compiler bug, not on the import system as a whole — see below. |

Stages 0-2 and 9 are independent and can proceed concurrently. Stage 4 is the
inflection point: after it, the "codegen guessed a type" bug class is gone.

**Where the plan stands (2026-08-03).** Stages 0, 1 and 2 are landed. The next
stage in the critical path is **3** (types as one interned enum), and it is the
right next one for a reason stage 1 just demonstrated: every piece of stage 1's
residue — `T`/`E` in `get_variant_field_type`, `Any` operands in `binary` — is a
question a string type cannot answer. Stage 1 made those visible and countable;
stage 3 is what makes them representable. Stage 9 (generated runtime) remains
independent of everything and can be picked up in parallel at any time; stage 10
is still blocked on the single `resolve_method_symbol` bug described below.

### What actually blocks stage 10 (measured 2026-08-02)

This row used to say "blocked on the import-system work already tracked in
`docs/design/`", which was too vague to act on and pointed at a larger project
than the evidence supports. The blocker was re-measured against the promoted
gen2 at `67c8cf3`; most of what the older notes described as blocking is done.

**Not the blocker (verified working).** `extend fun` reaches across module
boundaries in all the ways the split needs *except one*:

| shape | status |
|---|---|
| `extend fun Ctx.m()` in a file that imports `Ctx`'s module | works |
| an extension method reading and writing `this.field` | works (the field-set bug the old notes cite is fixed) |
| a **core** method in `core.sf` calling an extension defined in another file | works |
| an extension calling an extension defined **later in the same module** | works |
| two extensions in different files, called in dependency order | works, by luck — see below |
| two extensions calling **each other** across files | **fails**, in either import order |

**The blocker: extension-method calls are resolved by scanning already-emitted
symbols, so a forward reference across a module prefix emits an unprefixed
callee that nothing defines.** `gen_extend_method`
(`codegen/methods_body.sf:600`) registers `Ctx__m` → `core_Ctx__m` in
`func_prefix_map` *as it lowers that method*, and the call site
(`methods_body.sf:940`, same logic factored into `resolve_method_symbol` at
`:759`) resolves `Ctx__m` by consulting `func_prefix_map`, then
`current_prefix + name`, then a suffix scan of `known_functions` — and when all
three miss, **returns the unprefixed name unchanged**. So a call emitted before
the callee has been lowered becomes `@Ctx__m`, while the definition is
`@core_Ctx__m`, and LLVM rejects it:

```
error: use of undefined value '@Ctx__bx'
  %t20 = call i64 @Ctx__bx(i64 %t12, i64 %t19)
```

Two properties make this precisely a stage-10 blocker rather than a nuisance:

- **Prefix-dependent.** The identical forward reference in a *single* file
  compiles and runs, because there `current_prefix` is `""` and the unprefixed
  fallback happens to be the right answer. The bug appears the moment the
  extensions live in a module — i.e. exactly when you split a class across
  files.
- **Order cannot fix it.** For a one-directional dependency, importing the
  callee's file first works. The codegen split is *mutually* recursive
  (`gen_arg_value` lives in `expr_body.sf` and is called from
  `methods_body.sf`; `expr_body.sf` calls back into statement generation), and
  no topological order exists for a cycle. Both orders were tested; both fail,
  each naming the other direction's symbol.

This is a fifth instance of the pattern #22, #40 and #78 all share, and which
Part 0 calls out: **a resolution helper that cannot fail, so it returns a
plausible wrong answer instead of reporting "not found."** Here the wrong answer
is an unprefixed symbol, and the report comes from LLVM with no mention of the
extension method or the module.

**So: unfinished work plus one compiler bug — not a missing language feature.**
The fix has the same shape as the fix for the identical bug class elsewhere: a
pre-pass over *all* modules that registers every `@extend:` method's prefixed
symbol in `func_prefix_map` before any body is lowered, and then making the
unprefixed fallback in `resolve_method_symbol` a diagnostic rather than a
guess. The pre-pass hook already exists —
`codegen.sf:737` walks every module and fills `extend_map` with
`prefix + name → class` before compilation, and `gen_extend_method` recomputes
the same prefix from `func_prefix_map`'s `Ctx__init` entry — so this is a
matter of writing `func_prefix_map` in that existing loop, not of building new
machinery.

Scheduling note: the fix lives in `codegen/methods_body.sf` and `codegen.sf`,
not in the import system or in `main.sf`, and it does not need a gen2
promotion — extension methods already parse and lower under the current gen2.
Stage 10 is therefore schedulable as soon as that one resolution bug is fixed,
and it should be verified by a mutual-recursion test across two extension
files, which is the case every ordering-based workaround hides.

Throughout: **build the differential interpreter early** (it only needs HIR, so
right after stage 4) and let it grade every subsequent stage. Without an oracle,
stages 6 and 7 are large refactors of tagging and emission with no way to detect
a silent wrong answer — the exact conditions that produced #23 and #32.

---

## Part 5 — What I would keep

Not everything needs changing, and it's worth being explicit so a rewrite doesn't
churn what works.

- **LLVM IR as the target, via text, then clang.** Cheap, debuggable, portable.
  No reason for the C API.
- **NaN boxing.** The representation is fine; only its *discipline* is
  unmanaged. #26's fix (remapping the three colliding quiet-NaN patterns to
  `0x7FFC…`) is correct and stays.
- **Self-hosting.** It is the project's whole point, and it is a fierce test —
  ~30k lines that "self-bootstraps clean" per #37's measurement table.
- **`Any` with runtime dispatch.** `__any_length`'s unmask-check-route is the
  right answer for genuinely dynamic values, and #32's fix correctly leaned on
  it. `Any` is not the problem; `Int`-as-`Unknown` was.
- **Enums + exhaustive match as the AST vocabulary.** Already good, and I9 just
  turns the existing checker feature into a build gate.
- **The `BUGS.md` discipline.** The write-ups record wrong first diagnoses
  (#35 filed on a misdiagnosis, #32's title wrong, #33 "filed as a String-dispatch
  bug; it was actually a declaration-order bug"), measurement tables, and
  reverted attempts with reasons. That is unusually high-quality engineering
  record-keeping and it is why this design could be grounded rather than
  speculative. Keep it.

---

## Summary

The current compiler's bugs are not distributed. **Five mechanisms** produce
almost all of them, and each maps to one missing structural guarantee:

| Mechanism | Missing guarantee | Invariant |
|---|---|---|
| Codegen re-infers types | checker output is consumed | I1 |
| `Int` is the unknown type | `Unknown` is distinct and illegal in HIR | I2 |
| Tagging is a convention | representation is in the type system | I5 |
| 65 mutable context fields | context is passed and dropped | I7 |
| Copy-pasted logic drifts | one source of truth, generated duplication | I10 |

Supported by: types as one interned enum (I3), names resolved once (I4),
structural IR (I6), spans everywhere (I8), no fall-throughs (I9), a real build
(I11) — and three always-on gates plus a differential interpreter to catch what
invariants can't.

The single highest-value change is **I1 + I2 together**: make the checker
elaborate, and make "I don't know" unrepresentable in the backend. That one pair
retires six of the open and recently-closed bugs. The single highest-value
*test* asset is a **reference interpreter for differential testing**, because
every remaining bug class in `BUGS.md` is a silent wrong answer, and exit codes
cannot see those.
