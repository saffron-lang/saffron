# Codegen Split Plan

## Current State

`src/compiler/codegen.sf` is ~2627 lines containing:
- 1 import (`./ast.sf`)
- 1 `Codegen` class with ~60 methods and 20 fields
- 1 standalone `generate()` function (entry point)

The class handles: IR emission utilities, type mapping, string constants, expression codegen, statement codegen, control flow, functions/lambdas/closures, classes, enums/match, runtime embedding (list, map, stringbuilder), variable collection, and final output assembly.

## Proposed Split: 4 Files

```
src/compiler/
  codegen.sf          -- (new) orchestrator: Codegen class shell + generate() + output assembly
  codegen_expr.sf     -- expression codegen (gen_expr, gen_binary, gen_call, gen_method_call, ...)
  codegen_stmt.sf     -- statement codegen, control flow, functions, classes, enums
  codegen_runtime.sf  -- embedded LLVM IR for list/map/stringbuilder runtime
```

### File 1: `codegen.sf` (~400 lines) -- Orchestrator

**Responsibility:** Class definition, state, IR emission primitives, type mapping, output assembly.

**Contains:**
- `class Codegen` with all 20 fields
- `init()`
- Emission primitives: `emit()`, `emit_indent()`, `emit_terminator()`, `start_block()`
- Naming/counters: `sanitize_name()`, `fresh_label()`, `fresh_local()`, `reset_locals()`
- Type mapping: `llvm_type()`
- String constants: `add_string_constant()`, `emit_string_constants()`, `escape_llvm_string()`
- Format helpers: `get_format_ptr()`
- The top-level `generate()` method (output assembly logic)
- The standalone `fun generate(program)` entry point

**Imports:**
```saffron
import "./ast.sf" as AST
import "./codegen_expr.sf" as Expr
import "./codegen_stmt.sf" as Stmt
import "./codegen_runtime.sf" as Runtime
```

### File 2: `codegen_expr.sf` (~900 lines) -- Expression Codegen

**Responsibility:** All expression-level IR generation.

**Contains:**
- `gen_expr()`
- `gen_arg_value()`
- `gen_binary()`
- `gen_string_concat()`, `gen_runtime_string_concat()`, `flatten_string_expr()`
- `gen_string_ptr()`
- `gen_unary()`
- `gen_call()`, `gen_indirect_call()`
- `gen_method_call()`
- `gen_logical()`
- `gen_enum_construct()`, `gen_match()`
- `gen_list_lit()`, `gen_index_get()`, `gen_index_set()`
- `gen_get_field()`, `gen_set_field()`
- `gen_lambda()`, `gen_closure_function()`
- Classification helpers: `classify_expr()`, `get_variable_name()`, `resolve_callee()`
- Type inference helpers: `is_string_expr()`, `is_string_variable()`, `is_this_expr()`, `has_string_literal()`, `contains_string_lit()`, `get_string_lit_value()`
- Field helpers: `get_field_index()`, `get_field_type()`
- Enum helpers: `find_enum_for_variant()`, `get_max_fields()`, `find_class_for_method()`, `get_method_ret()`
- Free var analysis: `find_free_vars_stmts()`, `find_free_vars_stmt()`, `find_free_vars_expr()`
- `to_i1()`

### File 3: `codegen_stmt.sf` (~500 lines) -- Statement Codegen

**Responsibility:** Statement-level IR generation, control flow, function/class/enum declarations.

**Contains:**
- `gen_stmt()`
- `gen_stmts()`
- `gen_if()`
- `gen_while()`
- `gen_function()`
- `gen_class_decl()`, `gen_class_constructor()`
- `gen_enum_decl()`
- `classify_stmt()`, `is_if_stmt()`
- Variable collection: `collect_vars()`, `collect_vars_stmt()`, `collect_vars_expr()`, `add_var_unique()`, `is_match_expr()`, `get_match_arms()`

### File 4: `codegen_runtime.sf` (~350 lines) -- Embedded Runtime

**Responsibility:** LLVM IR text for the list, map, and stringbuilder runtime functions.

**Contains:**
- `list_runtime()` -> String
- `stringbuilder_runtime()` -> String
- `map_runtime()` -> String

## Communication Pattern: Single Class, Free Functions Calling on Context

The key constraint is that Saffron's self-hosted compiler does not yet support multi-file classes or partial class definitions. The split must work within the language's existing capabilities.

**Approach: Pass `Codegen` instance as first argument to free functions.**

Each satellite file exports free functions that take the `Codegen` instance as their first parameter:

```saffron
// codegen_expr.sf
import "./ast.sf" as AST
import "./codegen.sf" as CG

fun gen_expr(ctx: CG.Codegen, expr: AST.Expr): String {
    return match (expr) {
        IntLit(v) => {
            ctx.last_type = "Int"
            var local = ctx.fresh_local()
            ctx.emit_indent(local + " = add i64 0, " + v.floor().to_string())
            local
        }
        // ...
    }
}
```

The orchestrator class methods become thin wrappers:

```saffron
// codegen.sf
import "./codegen_expr.sf" as ExprGen

class Codegen {
    // ... fields ...

    fun gen_expr(expr: AST.Expr): String {
        return ExprGen.gen_expr(this, expr)
    }
}
```

**Why this works:**
- No circular imports: `codegen.sf` imports the satellites, the satellites import `codegen.sf` only for the `Codegen` type (the class definition without method bodies would need to be available, or we use `Any` type for the context parameter)
- All state stays in one object -- no global mutable state
- Each file is independently compilable to LLVM IR

**Circular import mitigation:**
The real problem is that `codegen_expr.sf` needs to call methods on `Codegen` (like `fresh_local()`, `emit_indent()`), and `Codegen` is defined in `codegen.sf` which imports `codegen_expr.sf`.

**Solution: Extract a `CodegenCtx` base into its own file.**

```
src/compiler/
  codegen_ctx.sf      -- CodegenCtx class: fields + emission primitives + type helpers
  codegen_expr.sf     -- free functions taking CodegenCtx
  codegen_stmt.sf     -- free functions taking CodegenCtx
  codegen_runtime.sf  -- pure functions returning String (no dependencies)
  codegen.sf          -- imports all above, exposes generate()
```

Dependency graph (acyclic):
```
codegen_runtime.sf  (no imports besides ast.sf)
         |
codegen_ctx.sf      (imports ast.sf only)
    /         \
codegen_expr.sf    codegen_stmt.sf   (both import codegen_ctx.sf + ast.sf)
    \         /
     codegen.sf    (imports all, wires together, exports generate())
```

Note: `codegen_expr.sf` and `codegen_stmt.sf` will need to call each other (e.g., `gen_expr` calls `gen_stmts` via lambdas, `gen_stmt` calls `gen_expr`). This is resolved by passing function references at construction time or by having the orchestrator set cross-references after import.

**Practical resolution for mutual recursion:**

Since `gen_expr` needs `gen_stmts` and `gen_stmt` needs `gen_expr`, we keep both in the `CodegenCtx` class as method pointers (lambdas stored as fields), or more practically: we accept that expr and stmt codegen must be in the same file if we cannot break the cycle. In that case, the split becomes:

```
src/compiler/
  codegen_ctx.sf      -- CodegenCtx class (fields, emit helpers, type mapping, string constants)
  codegen_core.sf     -- expression + statement codegen (they're mutually recursive)
  codegen_runtime.sf  -- runtime IR generation (pure functions)
  codegen.sf          -- thin orchestrator: imports above, exposes generate()
```

This gives us **4 files** with clean boundaries, no circular deps, and manageable sizes (~200, ~1700, ~350, ~150 lines).

## Linking Strategy

Each `.sf` file compiles independently to a `.ll` file. The final binary is produced by:

```bash
clang -O2 -o saffron_compiler \
  codegen_ctx.ll \
  codegen_core.ll \
  codegen_runtime.ll \
  codegen.ll \
  ast.ll \
  lexer.ll \
  parser.ll \
  main.ll
```

All symbols are global LLVM functions with module-prefixed names (see `docs/import-system-plan.md`). No dynamic linking or dlopen needed.

## Migration Path

1. **Step 1:** Create `codegen_runtime.sf` -- extract `list_runtime()`, `map_runtime()`, `stringbuilder_runtime()` as module-level functions. Zero risk, no mutual deps.

2. **Step 2:** Create `codegen_ctx.sf` -- extract the class fields, `init()`, and all emission/utility methods that don't call into expr/stmt codegen (about 15 methods).

3. **Step 3:** Create `codegen_core.sf` -- move all `gen_*` methods here as free functions taking `CodegenCtx`. This is the bulk of the work.

4. **Step 4:** Slim down `codegen.sf` to the orchestrator that imports the others and exports `generate()`.

5. **Step 5:** Update `main.sf` import -- no change needed since it already imports `./codegen.sf` and calls `Codegen.generate(program)`.

Each step can be verified by running the compiler on test programs before proceeding to the next.
