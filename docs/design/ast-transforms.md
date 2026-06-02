# Compile-Time AST Transformations for Saffron

## Status: Proposal
## Date: 2026-06-01

---

## 1. Current State

Saffron already has several relevant primitives:

| Primitive | Where | What it does |
|-----------|-------|--------------|
| `@extern("sig")` decorator | Parser (self-hosted) | Stores metadata in `docstring` field; codegen emits extern declaration |
| `@intrinsic` decorator | Parser (self-hosted) | Same mechanism; codegen skips body emission |
| `@Test.test` decorator | CVM runtime | Function wrapping via `name = decorator(name)` at runtime |
| `@extend` keyword | Parser (self-hosted) | Extension methods: docstring encodes target class |
| `@parser` / `@ast` / `@compile` stdlib | Library | User-accessible parse/traverse/codegen APIs |
| `sfx` preprocessor | Turmeric tools | Source-to-source `.sfx` to `.sf` via text manipulation |
| `[scripts.bin]` in pantry.toml | Pantry | Packages export CLI commands available to consumers |
| Pantry script expansion | Pantry `run.sf` | Scripts can reference other scripts by name; auto-expands |

### How decorators work today

**In the self-hosted compiler (native):** Decorators are parsed as `@name` or `@name("arg")` and converted to a string stored in the `docstring` field of the following `FunDecl` or `ClassDecl`. The codegen pattern-matches on known prefixes (`@intrinsic`, `@extern:`, `@extend:`, `@import:`). Unknown decorators are ignored by codegen.

**In the CVM (bytecode VM):** Decorators are full expressions stored in an `ExprArray`. After emitting the function, the compiler wraps it: `name = decorator(name)`. This is a true higher-order function application at runtime.

---

## 2. Approach Comparison

### 2.1 Build-Time Macros (Rust/Zig Style)

**Concept:** `@derive(Serialize)` or `@component` triggers the compiler to invoke a macro function at compile time that produces additional AST nodes.

| Criterion | Assessment |
|-----------|-----------|
| Implementation effort | **Very High.** Requires a compile-time execution environment, new compiler phases, macro hygiene, and error reporting through macro expansion. |
| User experience | Excellent for consumers (`@derive(Serialize)` just works). Complex for authors. |
| Composability | Possible but ordering semantics get complex. |
| Debuggability | Hard. Need `--expand-macros` flag. Errors in generated code are confusing. |
| Performance | Depends on macro complexity. Adds a full pass. |
| Type safety | Type checker runs after expansion — good semantics but macro errors appear in generated code. |
| Ecosystem fit | Requires new infrastructure. Does not leverage existing `@parser`/`@ast`. |

### 2.2 Compiler Plugins / AST Transform Hooks (Babel/TypeScript Style)

**Concept:** Users write transform functions `fun transform(stmts: List<AST.Stmt>): List<AST.Stmt>` that the compiler loads and runs during compilation.

| Criterion | Assessment |
|-----------|-----------|
| Implementation effort | **High.** Compiler must dynamically load and execute Saffron code, pass AST between host and plugin. |
| User experience | Library authors write standard Saffron. Consumers declare in pantry.toml. |
| Composability | Ordered by declaration in pantry.toml. Straightforward piping. |
| Debuggability | Good: `--emit-expanded` dumps transformed source. |
| Performance | Each plugin requires loading/executing code. |
| Type safety | Type checker runs after all transforms — standard and correct. |
| Ecosystem fit | Leverages existing `@parser`, `@ast` modules directly. |

### 2.3 Preprocessing Scripts (Current sfx Approach)

**Concept:** Separate tool runs before compilation: `sfx input.sfx output.sf`. Pantry orchestrates via `[scripts]`.

| Criterion | Assessment |
|-----------|-----------|
| Implementation effort | **Already done.** Only needs formalization. |
| User experience | Manual: users must configure scripts, file extensions differ. |
| Composability | Multiple tools chain via `&&`. Fragile. |
| Debuggability | Excellent: intermediate files are visible on disk. |
| Performance | Process spawning overhead per file. |
| Type safety | Type checker sees only the final `.sf` output. |
| Ecosystem fit | Already works. But source mapping is lost. |

### 2.4 Gradle/Bazel-Style Build Scripts

**Concept:** A `build.sf` file defines compilation phases, each can transform code.

| Criterion | Assessment |
|-----------|-----------|
| Implementation effort | **Very High.** Requires build system DSL, task graph, caching. |
| User experience | Powerful but heavyweight. Overkill for most projects. |
| Composability | Excellent (explicit task graph). |
| Debuggability | Good: each phase can dump output. |
| Performance | Good if caching is implemented. |
| Type safety | Phases run before the compiler. |
| Ecosystem fit | Pantry would need fundamental redesign. |

### 2.5 Decorator-Driven Transforms (Python/TypeScript Decorators)

**Concept:** Decorators on functions/classes trigger codegen changes. `@observable class State { ... }` generates getter/setter wrappers.

| Criterion | Assessment |
|-----------|-----------|
| Implementation effort | **Medium.** Extend existing decorator infrastructure. |
| User experience | Very natural: `@observable class State {}`. |
| Composability | Multiple decorators stack naturally. |
| Debuggability | Good: can dump expanded forms. |
| Performance | Low overhead: only processes decorated items. |
| Type safety | Configurable — before or after type checking. |
| Ecosystem fit | Decorators already exist. Natural extension. |

---

## 3. Summary Table

| | Macros | Plugins | Preprocessing | Build Scripts | Decorators |
|--|---|---|---|---|---|
| **Impl effort** | Very High | High | Done | Very High | Medium |
| **Consumer UX** | Excellent | Good | Fair | Good | Excellent |
| **Author UX** | Hard | Standard | Standard | Complex | Standard |
| **Composability** | Complex | Simple | Fragile | Excellent | Natural |
| **Debuggability** | Hard | Good | Excellent | Good | Good |
| **Performance** | Variable | Moderate | Slow | Good | Fast |
| **Type safety** | After expansion | After transforms | After output | Before/after | Configurable |
| **Ecosystem fit** | New infra | Leverages @ast | Already works | Redesign needed | Natural extension |
| **Power** | Maximum | High | High | Maximum | Item-level |

---

## 4. Recommendation: Hybrid Approach (Plugins + Decorators)

### Rationale

Given that Saffron already has `@parser`, `@ast` modules, decorators in the syntax, pantry with dependency resolution, and the `sfx` preprocessor as a proven pattern — the most idiomatic approach is:

**Pantry-orchestrated AST transform plugins, triggered by decorators.**

This combines:
- **Decorator syntax** for the consumer API (natural, already parsed)
- **Plugin functions** for the transform implementation (standard Saffron using existing `@ast` modules)
- **Pantry integration** for discovery and orchestration

The key insight: we do NOT need to change the compiler. Pantry runs transform plugins as a pre-compilation step that reads `.sf` files, transforms the AST, and writes back `.sf`.

---

## 5. User-Facing API

### 5.1 Consuming a Transform

```toml
# pantry.toml
[dependencies]
saffron-observable = "^0.1.0"

[transforms]
observable = "saffron-observable"
sfx = { package = "turmeric", extensions = [".sfx"] }
```

In source code:

```saffron
@observable
class AppState {
    var count: Int
    var name: String
}
```

### 5.2 Writing a Transform

```saffron
// saffron-observable/src/transform.sf
import "@parser" as Parser
import "@ast" as AST

fun transform(source: String, path: String): String {
    var stmts = Parser.parse(source)
    var result: List<String> = []

    for (stmt in stmts) {
        match (stmt) {
            ClassDecl(name, parent, fields, methods, doc) => {
                if (doc == "@observable") {
                    result.push(generate_observable_class(name, fields, methods))
                } else {
                    result.push(emit_stmt(stmt))
                }
            }
            _ => result.push(emit_stmt(stmt))
        }
    }

    return result.join("\n")
}
```

Transform package `pantry.toml`:

```toml
[package]
name = "saffron-observable"
version = "0.1.0"
type = "transform"

[transform]
entry = "src/transform.sf"
decorators = ["observable"]
```

### 5.3 sfx Migration

Today's sfx becomes a transform with `extensions = [".sfx"]`:

```toml
# turmeric/pantry.toml
[transform]
entry = "tools/sfx_transform.sf"
extensions = [".sfx"]
```

---

## 6. Implementation Plan

### Phase 1: Formalize the Transform Protocol (pantry changes only)

~200 lines of changes to `pantry/src/commands/build.sf` and `pantry/src/config.sf`:

1. Add `[transforms]` section parsing to `Config.Manifest`
2. Before invoking `saffronc`, iterate through registered transforms
3. For file-extension transforms: find matching files, run transform, write to `build/.transforms/`
4. For decorator transforms: scan source for matching `@decorator` annotations, run transform
5. Compile from the transformed output

Transform execution: `saffron run <transform_entry> <input_file> <output_file>`

### Phase 2: AST-Aware Transform API (stdlib addition)

New `@transform` module + add `emit_source(stmt): String` to `@ast` for round-tripping.

### Phase 3: In-Process Transforms (compiler integration, optional)

Add `--transform <path>` flag to `saffronc`. Pipeline becomes: Lex → Parse → **Transform** → Check → Codegen.

### Phase 4: Structured Decorator Metadata (compiler enhancement)

Replace docstring hack with proper `decorators: List<Decorator>` field on AST nodes.

---

## 7. Design Decisions

### Why not change the compiler?

Phase 1 requires **zero compiler changes**. Pantry already orchestrates builds. Adding a pre-compilation transform step avoids bootstrap complexity and compiler architecture changes.

### Why not pure text transforms?

Text transforms lose structure. With AST-level transforms: error messages reference original source, transforms compose cleanly, and the type checker can run on original code.

### Why decorators as the trigger?

Already parsed and familiar. Clear signal: "this item needs transformation." Composes naturally (multiple decorators stack).

### Order of transforms

Declaration order in `[transforms]`. Extension transforms first (change file "language"), then decorator transforms.

---

## 8. Open Questions

1. **Source maps?** Transformed `.sf` in temp dir — need `// #sourcemap original.sf:42` for error messages.
2. **Incremental transforms?** Cache outputs with file hashes.
3. **Transform dependencies?** Declaration order for now; future `depends_on` field.
4. **Type-aware transforms?** Phase 1 is pre-type-check. Phase 3 could support post-check.
5. **Debug mode?** `pantry build --show-transforms` writes intermediates to `build/.transforms/`.
