# Saffron LLVM Compiler Plan

A self-hosted compiler for Saffron that targets LLVM IR, written entirely in Saffron and executed on the existing bytecode VM.

## Architecture

```
Source .sf ──► [Saffron Compiler on VM] ──► LLVM IR (.ll) ──► llc/clang ──► Native Binary
```

The compiler is a regular Saffron program. It reads source files, parses them into an AST, type-checks, and emits LLVM IR as text. The existing C VM is the host — it runs the compiler during development. The compiled native binary does not depend on the VM at all.

## Phases

### Phase 1 — Language Readiness

Prepare the Saffron language to be capable of expressing a compiler. These are features the compiler itself needs.

#### Done
- [x] Indexed assignment (`list[i] = x`)
- [x] StringBuilder (efficient IR generation without O(n²) string concat)
- [x] Recursive types (enums that reference themselves: `enum Expr { Binary(left: Expr, ...), Literal(value: Float) }`)

#### Remaining
- [ ] **Increase FRAMES_MAX** — 64 is too low for recursive AST traversal. Raise to 256 or 512.
- [ ] **Bitwise operators** — `&`, `|`, `^`, `<<`, `>>`, `~` on Int. Needed for flag manipulation and encoding in codegen.
- [ ] **Multi-line strings** — triple-quote `"""..."""` syntax for LLVM IR templates.
- [ ] **Exhaustive match checking** — warn when match arms don't cover all enum variants.

---

### Phase 2 — Saffron-mini Compiler

A working compiler for a minimal subset of Saffron. The goal is an end-to-end pipeline: source → LLVM IR → executable.

#### Saffron-mini Language Subset

The compiler initially targets only:
- Number literals, String literals, Bool, Nil
- Variables (`var`, `let`)
- Arithmetic (`+`, `-`, `*`, `/`, `%`)
- Comparison and logical operators
- `if`/`else`
- `while` loops
- Functions (no closures, no generics)
- `return`
- `IO.println` (mapped to `printf`)

No classes, no enums, no generics, no closures, no async. Just enough to compile real programs.

#### Compiler Structure

```
src/compiler/
  @ast.sf          — AST node definitions (recursive enums)
  @lexer.sf        — tokenizer
  @parser.sf       — recursive descent parser → AST
  @checker.sf      — type checker
  @codegen.sf      — AST → LLVM IR text emission
  @main.sf         — CLI orchestration: read file, compile, write .ll
```

#### Codegen Strategy

Emit LLVM IR as text (`.ll` files). Use StringBuilder for efficient construction. Invoke `clang` via `OS.exec()` to assemble into a binary.

Example target output for `fun main(): Int { return 42 }`:

```llvm
define i64 @main() {
entry:
  ret i64 42
}
```

#### Milestone: Hello World

```saffron
fun main() {
    IO.println("Hello, world!")
}
```

Compiles to LLVM IR that calls `puts("Hello, world!")` and produces a working native binary.

#### Type Mapping (Saffron-mini → LLVM)

All types are objects — no primitives. The compiler uses unboxed representations where escape analysis proves it safe, but semantically everything is an object with methods.

| Saffron Type | LLVM Representation | Notes |
|-------------|-----------|-------|
| Int | `i64` (unboxed) or `%Int*` (boxed) | Object with methods (`.abs()`, `.to_string()`, etc.) |
| Float | `double` (unboxed) or `%Float*` (boxed) | Object with methods |
| Bool | `i1` (unboxed) or `%Bool*` (boxed) | Object with methods |
| Nil | singleton `%Nil*` | The unit type |
| String | `%String*` | Always heap-allocated object |
| Function | `%Closure*` | Object wrapping function pointer + environment |

The compiler is free to unbox `Int`, `Float`, `Bool` in local variables and arithmetic, but they box automatically when passed as `Any`, stored in collections, or when methods are called on them. This is an optimization detail — from the programmer's perspective, `42.abs()` just works.

Note: The VM keeps `Number` (f64) internally — it only needs enough precision to run the compiler. The `Int`/`Float` distinction and object model exist only in the compiled output.

---

### Phase 3 — Saffron-core Compiler

Extend the compiler to handle the features needed to compile itself.

#### Features to Add

- [ ] Closures (lambda lifting or heap-allocated environments)
- [ ] Enums with data (tagged unions in LLVM)
- [ ] Pattern matching (lower to switches + GEPs)
- [ ] Classes and methods (vtable dispatch)
- [ ] Generics (monomorphization)
- [ ] Lists and Maps (runtime data structures)
- [ ] String interpolation (lower to concat calls)
- [ ] For-in loops (desugar to iterator protocol)
- [ ] Try/catch (LLVM invoke + landingpad, or setjmp/longjmp)
- [ ] Interfaces (vtable with abstract method slots)
- [ ] Int and Float types (replacing Number — `i64` and `double` in LLVM)

#### Runtime (in Saffron)

The compiled binary needs a runtime. This runtime is written in Saffron and compiled by our own compiler:

- **GC** — mark-sweep or reference counting, operating on tagged pointers
- **Object model** — class instances, vtable dispatch, field access
- **String** — immutable interned strings with methods
- **List / Map** — growable array and hash table
- **Task queue** — cooperative async (if targeting async support)

The runtime compiles to LLVM alongside user code and is linked into the final binary.

#### Bootstrap Sequence

1. Saffron-core compiler runs on the C VM
2. It compiles the Saffron runtime → `runtime.ll`
3. It compiles user code → `program.ll`
4. `clang runtime.ll program.ll -o program`
5. The resulting binary is standalone (no VM dependency)

---

### Phase 4 — Self-Hosting

The compiler compiles itself.

1. The Saffron-core compiler (running on VM) compiles its own source → native compiler binary
2. The native compiler binary re-compiles itself → identical binary (bootstrap verification)
3. The C VM is no longer needed for compilation

At this point Saffron is fully self-hosted: the compiler, runtime, and standard library are all Saffron.

---

## Design Decisions

### Why textual LLVM IR (not libLLVM bindings)?

- No FFI needed — the compiler is pure Saffron
- Simpler to implement and debug
- `clang -S -emit-llvm` can verify our output against reference
- We get LLVM optimizations for free via `opt` and `llc`

### Why not compile to C first?

- LLVM IR gives us direct access to LLVM's optimizer
- Proper tail calls, stack maps for GC, exception handling via landingpad
- No C compiler quirks to work around
- Industry standard backend

### Why run the compiler on the VM?

- The VM already exists and works
- We can develop the compiler incrementally, testing as we go
- No chicken-and-egg problem: the host is the C VM, the target is LLVM
- Performance doesn't matter much for the compiler itself (it's a dev tool)

---

## VM Adjustments (minimal)

These changes keep the VM viable as a compiler host without redesigning it:

| Change | Why |
|--------|-----|
| `FRAMES_MAX` → 256 or 512 | Deep AST recursion |
| GC threshold → 8 MB or 16 MB | Avoid constant collection during compilation |
| GC heap growth factor → 4x | Same — reduce GC frequency for large programs |

The VM does **not** need: Int type, JIT, tail-call optimization, or FFI. It just needs to run the compiler without hitting resource limits.

---

## File Layout (Final)

```
saffron/
  src/                    — C VM (host, unchanged)
  src/lib/                — Saffron stdlib (.sf)
  src/libc/               — Native C modules for VM
  src/compiler/           — Saffron LLVM compiler (runs on VM)
    @ast.sf
    @lexer.sf
    @parser.sf
    @checker.sf
    @codegen.sf
    @main.sf
  src/runtime/            — Saffron runtime (compiled by our compiler)
    @gc.sf
    @object.sf
    @string.sf
    @list.sf
    @map.sf
  docs/
  test/
```

---

## Success Criteria

| Milestone | Definition of Done |
|-----------|-------------------|
| Phase 1 complete | Recursive enums work, FRAMES_MAX raised, bitwise ops exist |
| Phase 2 complete | `./saffron src/compiler/@main.sf test/hello.sf` produces a working native binary |
| Phase 3 complete | The compiler can compile programs using closures, classes, enums, generics |
| Phase 4 complete | The compiler compiles itself and produces an identical binary on re-compilation |
