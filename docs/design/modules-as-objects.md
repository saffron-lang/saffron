# Design: Modules as Objects

## Problem Statement

The current module system uses string-prefixed function names (`stdlib_http_server_serve`), magic-cased builtins (`IO`, `OS`, `GC`), and multiple lookup tables (`module_prefixes`, `module_globals`, `known_functions`, `func_prefix_map`) to simulate namespaces. This causes:

1. **Namespace leaks** — local variables in imported modules shadow the importer's variables
2. **Magic builtins** — `IO`, `OS`, `GC`, `Task` need special-case checks everywhere in codegen
3. **Cross-module resolution failures** — field/method lookups fail when prefixed names don't match
4. **Function dedup issues** — `defined_funcs.contains()` fails across compilation units
5. **Named import dispatch** — `import { effect } from "@signal"` requires special routing to avoid indirect calls

Every bug fixed this session traces back to the lack of a real module concept.

## Proposed Design: Modules are Objects

Like Python, every module is a first-class object. Importing a module gives you a reference to that object. Accessing module members is regular attribute access.

### Syntax (unchanged for users)

```saffron
import "@http/server" as Http
import "@signal" as Signal
import { effect, computed } from "@signal"

Http.serve(8080)
var s = Signal.signal(0)
effect(fun () => IO.println(s.get().to_string()))
```

### Semantics (new)

```
import "@http/server" as Http
```
Becomes: `Http` is a local variable holding a pointer to a module object. The module object is a struct with fields for each exported name (functions, classes, enums, variables).

```
Http.serve(8080)
```
Becomes: load field `serve` from the module object, call it. Same dispatch as any class method call — no special prefixing.

```
import { effect } from "@signal"
```
Becomes: load field `effect` from the module object, bind it to local `effect`. Direct call.

### Module Object Layout

Each module compiles to:
1. A struct type with one field per export
2. A single global instance of that struct
3. An init function that populates the fields

```llvm
; Module: @signal
%__mod_signal = type { i64, i64, i64 }  ; signal, computed, effect (function pointers)
@__mod_signal_instance = global %__mod_signal zeroinitializer

define void @__mod_signal_init() {
  ; Store function pointers / class constructors into the module struct
  store i64 ptrtoint(@signal_signal), %__mod_signal_instance.field0
  store i64 ptrtoint(@signal_computed), %__mod_signal_instance.field1
  store i64 ptrtoint(@signal_effect), %__mod_signal_instance.field2
}
```

### Accessing Module Members

```saffron
Http.serve(8080)
```

Compiles to:
```llvm
; Load the function pointer from the module object
%mod = load i64, i64* @__mod_http_server_instance_serve
; Call it directly (not indirect — we know the target at compile time)
call i64 @http_server_serve(i64 8080)
```

At compile time, we KNOW which function `Http.serve` refers to. So the actual codegen can still emit direct calls — the "object" model is the CONCEPTUAL model for scoping and name resolution, not necessarily the runtime representation.

### Key Insight: Compile-Time Resolution, Object-Model Scoping

We don't need actual runtime module objects for most cases. The module-as-object model is for:
- **Scoping**: module internals don't leak
- **Dispatch**: `mod.func()` resolves like any attribute access
- **Uniformity**: IO, OS, GC are just pre-imported module objects (no magic)

At compile time, `Http.serve` resolves to `@http_server_serve` — a direct call. No indirection. The object model just determines HOW the resolution happens (attribute lookup on a known module type).

### Builtins Become Modules

```saffron
// These are implicitly imported in every file (like Python's builtins):
// import "@io" as IO
// import "@os" as OS
// import "@gc" as GC
```

`IO.println(x)` → resolve `println` on the `IO` module object → direct call to `@__io_println_str`. Same dispatch path as any other module. No special-casing in codegen.

### Module Exports

Only top-level declarations are exported:
```saffron
// src/lib/signal.sf

// Exported (top-level):
fun signal(initial: Any): Signal { ... }
fun computed(fn: () => Any): Computed { ... }
fun effect(fn: () => Nil) { ... }
class Signal { ... }
class Computed { ... }

// NOT exported (inside a function):
fun signal(initial: Any): Signal {
    var internal_helper = ...  // private to this function
}
```

### Named Imports as Destructuring

```saffron
import { signal, effect } from "@signal"
```

Equivalent to:
```saffron
import "@signal" as __signal_tmp
var signal = __signal_tmp.signal
var effect = __signal_tmp.effect
```

At compile time, these resolve to direct function references — no runtime cost.

### Module-Level State

Modules can have mutable state:
```saffron
// counter.sf
var count: Number = 0
fun increment() { count = count + 1 }
fun get(): Number { return count }
```

This state lives in module globals (as today), but scoped to the module object:
```saffron
import "@counter" as Counter
Counter.increment()
IO.println(Counter.get().to_string())  // 1
```

### Implementation Phases

#### Phase 1: Uniform Dispatch (minimal change)
- Remove special-casing of `IO`, `OS`, `GC`, `Task` from `gen_method_call`
- Register them as module aliases in `module_prefixes` at program start
- All module member access goes through the same resolution path
- No runtime module objects yet — just unified name resolution

#### Phase 2: Module Scoping
- Module internals (function-local variables) never register in global tables
- Only exported names (top-level functions, classes, enums) are visible
- `current_prefix` still used for codegen naming, but lookup is via module type info

#### Phase 3: Module Type Info
- Each module gets a "type descriptor" (list of exported names + types)
- `get_var_type_str("Http")` returns the module type
- Field access on a module-typed variable resolves via the descriptor

#### Phase 4: Runtime Module Objects (optional, for dynamic imports)
- Actual module struct at runtime
- Enables `var m = if (cond) import("a.sf") else import("b.sf")`
- Not needed for static imports (everything resolved at compile time)

## Migration

The current system works for bootstrap. Migration is incremental:
1. Phase 1 eliminates the IO/OS/GC special cases (biggest source of bugs)
2. Phase 2 fixes namespace leaks (the `var app` collision bug)
3. Phase 3 makes cross-module type resolution reliable
4. Phase 4 is optional luxury

Each phase is independently useful and doesn't break existing code.

## Comparison

| | Current | Proposed |
|---|---------|----------|
| `IO.println` | Magic keyword check in 3 places | Module attribute access (same as Http.serve) |
| Module internals | Leak into global namespace | Scoped to module |
| Cross-module fields | Prefix guessing + reverse lookup | Type descriptor lookup |
| Named imports | Special routing in gen_call | Variable binding from module field |
| `defined_funcs` dedup | String comparison bugs | Module-scoped, no global list |
| Adding new builtins | Edit codegen in 5 places | Add to prelude imports |
