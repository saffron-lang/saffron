# Import System for Compiled Output

## Goal

Enable multi-file Saffron programs to compile to native binaries via LLVM IR, using static linking. No runtime module loading -- all resolution happens at compile time.

## Design Overview

```
foo.sf  ──compile──►  foo.ll  ─┐
bar.sf  ──compile──►  bar.ll  ─┼──► clang ──► binary
baz.sf  ──compile──►  baz.ll  ─┘
```

Each `.sf` file compiles to one `.ll` file. Symbols exported by a module are prefixed with the module name to avoid collisions. The `import` statement in compiled code translates to: (1) ensure the dependency is compiled, (2) declare its symbols as `external` in the current `.ll` file.

## Symbol Naming Convention

All top-level symbols get a module prefix derived from the filename:

| Source | Symbol | LLVM Name |
|--------|--------|-----------|
| `codegen.sf` exports `fun generate(...)` | `generate` | `@codegen__generate` |
| `ast.sf` exports `enum Expr` | `Expr` | `@ast__Expr` (constructor functions) |
| `lexer.sf` exports `fun lex(...)` | `lex` | `@lexer__lex` |
| `codegen.sf` class `Codegen` method `gen_expr` | `Codegen.gen_expr` | `@codegen__Codegen__gen_expr` |

**Naming rules:**
- Module name = filename without `.sf` extension, sanitized (dots/hyphens become underscores)
- Separator = `__` (double underscore)
- Class methods: `module__Class__method`
- Enum constructors: `module__Enum__Variant` (function that returns the tagged value)
- Standalone functions: `module__function_name`

## Import Statement Compilation

### Source syntax:
```saffron
import "./ast.sf" as AST
```

### What the compiler does:

1. **Resolve path** -- relative to the importing file's directory
2. **Compile dependency** -- recursively compile `ast.sf` to `ast.ll` (if not already compiled)
3. **Read exports** -- parse the dependency to know its exported symbols and types
4. **Emit declarations** -- in the importing module's `.ll` file, emit `declare` for each used symbol

### Example generated IR:

In `codegen.ll`:
```llvm
; External symbols from ast.sf
declare i64 @ast__Expr__IntLit(i64 %value)
declare i64 @ast__Expr__Binary(i64 %left, i64 %op, i64 %right)
; ... etc for each variant constructor used

; External symbols from codegen_ctx.sf  
declare i64 @codegen_ctx__CodegenCtx__fresh_local(i64 %self)
declare void @codegen_ctx__CodegenCtx__emit_indent(i64 %self, i64 %line)
```

In `ast.ll`:
```llvm
; Exported: constructors for enum Expr
define i64 @ast__Expr__IntLit(i64 %value) {
  ; ... tag + pack ...
}
```

## Compilation Order and Dependency Graph

The compiler builds a dependency graph from imports and compiles in topological order:

```
main.sf
  -> lexer.sf        (no deps)
  -> parser.sf       (-> ast.sf, lexer.sf)
  -> codegen.sf      (-> ast.sf, codegen_ctx.sf, codegen_core.sf, codegen_runtime.sf)
  -> ast.sf          (no deps)
```

**Algorithm:**
1. Parse all imports from the entry file (recursive)
2. Topological sort
3. Compile each file exactly once, bottom-up
4. Link all `.ll` files together with clang

**Cycle detection:** Circular imports are a compile error. The codegen split specifically avoids cycles (see `codegen-split-plan.md`).

## Module Metadata

Each compiled module needs to communicate its exports to importers. Two options:

### Option A: Re-parse the source (chosen)

The compiler already has a parser. When module B imports module A:
1. Parse A's source to get its top-level declarations
2. Extract function signatures, class definitions, enum definitions
3. Use that info to generate `declare` statements and validate usage

This is simple, requires no new file format, and works with the existing toolchain.

### Option B: .sfi interface files (future optimization)

Generate a compact interface file (`ast.sfi`) alongside `ast.ll` containing just the type signatures. Faster for large projects. Not needed for self-hosting.

## Qualified Access

When user writes `AST.Expr.IntLit(42)`:
1. Compiler knows `AST` maps to module `ast`
2. Knows `Expr` is an enum in that module
3. Knows `IntLit` is a variant with one field
4. Emits: `call i64 @ast__Expr__IntLit(i64 42)`

When user writes `ctx.fresh_local()`:
1. Compiler knows `ctx` has type `CodegenCtx` from module `codegen_ctx`
2. Emits: `call i64 @codegen_ctx__CodegenCtx__fresh_local(i64 %ctx_val)`

## Handling the `as` Alias

The `as` alias is purely compile-time. It creates a local name mapping:
- `import "./ast.sf" as AST` means: when the programmer writes `AST.Foo`, resolve it as `ast__Foo`
- The `.ll` output never contains the alias -- only the canonical prefixed names

## Extern Declarations Strategy

Rather than declaring every symbol from every imported module, the compiler only emits `declare` for symbols actually referenced in the current file. This keeps `.ll` files smaller and avoids needing full knowledge of a module's internals.

**Process during codegen:**
1. Maintain a set `used_externals: Set<String>`
2. When generating a call to an imported symbol, add it to the set
3. After codegen completes, emit all `declare` statements at the top of the `.ll` file

## Linking

Final assembly is a single `clang` invocation:

```bash
clang -O2 -o output \
  module1.ll module2.ll module3.ll ... \
  -lc  # libc for malloc, printf, etc.
```

LLVM's linker resolves all `declare`/`define` pairs across `.ll` files. Unresolved symbols are a link error (missing import or typo).

**Order doesn't matter** -- LLVM linking is not order-dependent like C object files with static libraries.

## Runtime Considerations

### String constants
Each module has its own `@.str.N` constants. Since they're `private unnamed_addr`, there's no collision across modules. LLVM may deduplicate identical strings during optimization.

### Type definitions
LLVM struct types (`%List`, `%Map`, `%SB`, class structs) must be consistent across modules. Solutions:
1. **Literal types everywhere** -- use `{ i64, i64, i64* }` instead of `%List` in cross-module signatures. Verbose but correct.
2. **Shared type definitions** -- each module that uses `%List` defines it identically. LLVM's type system is structural, so identical definitions unify at link time.
3. **Opaque pointers** -- use `ptr` (LLVM opaque pointer, available since LLVM 15). All pointers become `ptr`, no need for type coordination.

**Chosen approach:** Option 2 for now (repeat type defs in each module). Migrate to opaque pointers when targeting LLVM 15+.

### The runtime module

The embedded runtime (list, map, stringbuilder) compiles to `runtime.ll` and is always linked:

```bash
clang -O2 -o output runtime.ll module1.ll module2.ll ...
```

Only one copy of `@__list_new`, `@__map_set`, etc. exists in the final binary. All modules reference them via `declare`.

## Implementation Steps

1. **Add module prefix to all emitted symbols** in `codegen.sf`'s `gen_function()` and `gen_class_decl()`. This is a one-line change: prepend `module_name + "__"` to function names.

2. **Track current module name** -- pass it into the `Codegen` constructor (derived from filename).

3. **Parse imports in main.sf** -- collect all `import` paths, resolve to absolute paths, topological sort.

4. **Compile dependencies first** -- before compiling the entry file, compile all its transitive dependencies.

5. **Emit extern declarations** -- when codegen encounters a call to `ModuleAlias.symbol`, emit a `declare` referencing the prefixed name.

6. **Link all .ll files** -- update the `clang` invocation in `main.sf` to include all compiled `.ll` files.

## Example: Full Pipeline

Source (`main.sf`):
```saffron
import "./math.sf" as Math

fun main() {
    IO.println(Math.add(1, 2))
}
```

Source (`math.sf`):
```saffron
fun add(a: Int, b: Int): Int {
    return a + b
}
```

Compiled `math.ll`:
```llvm
define i64 @math__add(i64 %a.arg, i64 %b.arg) {
entry:
  %a = alloca i64
  store i64 %a.arg, i64* %a
  %b = alloca i64
  store i64 %b.arg, i64* %b
  %t1 = load i64, i64* %a
  %t2 = load i64, i64* %b
  %t3 = add i64 %t1, %t2
  ret i64 %t3
}
```

Compiled `main.ll`:
```llvm
declare i64 @math__add(i64, i64)
declare i32 @printf(i8*, ...)

@.str.0 = private unnamed_addr constant [5 x i8] c"%ld\0A\00"

define i64 @main() {
entry:
  %t1 = call i64 @math__add(i64 1, i64 2)
  %fmt = getelementptr [5 x i8], [5 x i8]* @.str.0, i64 0, i64 0
  call i32 (i8*, ...) @printf(i8* %fmt, i64 %t1)
  ret i64 0
}
```

Link:
```bash
clang -O2 -o main math.ll main.ll
```

Result: native binary that prints `3`.
