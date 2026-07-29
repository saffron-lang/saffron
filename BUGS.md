# Known Bugs

## Open

### 2. Forward references in nested closures

**Reproduction:**
```saffron
fun test() {
    fun a() { return b() }
    fun b() { return 42 }
    IO.print(a())
}
test()
```

**Expected:** Works (b is defined before a is called).
**Actual:** `Runtime error` — b is undefined when a's closure is compiled.

**Impact:** Can't write mutually recursive helper functions inside a parent scope.
**Note:** Design choice — compile-time local resolution. Same as Lua/Python.

### 6. No `break`/`continue` type checking

The compiler has break/continue infrastructure (breakJumps, continueJumps arrays) but the type checker doesn't handle NODE_BREAK/NODE_CONTINUE. Runtime works; type checker just ignores them.

### 12. Type checker segfaults on Any-typed closures in imported modules

**Reproduction:**
```saffron
// src/lib/test.sf contains:
fun mock(name: String) {
    var ret = nil
    var fn = fun (a: Any) => { return ret }
    ...
}
```
```saffron
import "@test" as T
T.mock("x")  // segfault
```

**Expected:** Module imports and runs.
**Actual:** Type checker crashes (segfault) when evaluating closures that capture
variables later assigned to different types (nil → Any), or when `Task.spawn(body)`
is called with an `Any`-typed param inside an imported module.

**Root cause:** The type checker dereferences NULL type pointers when resolving closure
captured variables whose initial type is nil. The cascading "Undefined variable" errors
from the type checker then trigger `runtimeError()` which corrupts VM state during import.

**Impact:** Blocks `@test` import with mock/async features. Inline usage works.
**Workaround:** Inline test functions or avoid nil-initialized captured variables.



### 21. `type` is a reserved keyword — can't use as parameter/variable name

`type` is tokenized as `TOKEN_TYPE` for type alias declarations. Code that uses `type` as a parameter name (e.g. `fun define(name: String, type: String)`) gets a parse error. Consider making it a contextual keyword (only reserved at statement start).


### 22. Cross-module global variable access emits undefined LLVM local

**Reproduction:**
```saffron
// cache.sf
var store: CacheStore = CacheStore()

// query.sf
import "./cache.sf" as Cache
Cache.store.get(key)  // <-- generates %Cache instead of @__g_basil_src_cache_store
```

**Expected:** `Cache.store` loads from the LLVM global `@__g_<prefix>store`.
**Actual:** Codegen emits `load i64, i64* %Cache` — `%Cache` is undefined.

**Root cause:** In `codegen/expr_body.sf` MemberAccess handler (~line 208), module-prefixed
access only checks if the resolved name is a known function. If it's a module-level `var`
(global), the check falls through and tries to load from a nonexistent local variable.

**Fix:** After the known_functions check, add a `module_globals.has(mp_resolved)` check
that emits `load i64, i64* @__g_<prefix><field>` for module-level variables.

**Impact:** Blocks any library that uses module-level globals accessed cross-module
(e.g. basil's `Cache.store` singleton). Workaround: inline the global or avoid module-qualified access.

### 23. Runtime functions return untagged i64 into NaN-boxed value space

`src/runtime/runtime.sf` is compiled with `--identity-mode`, where the tag/untag
helpers are no-ops. A runtime function that returns an integer therefore hands
back a raw i64 into value space, where every other value is NaN-boxed.

```saffron
import "@os" as OS
IO.println(OS.system("exit 1"))   // prints 4.94066e-324, not 1
```

The raw `1` is reinterpreted as a subnormal double. Two distinct symptoms:

- **Printing** goes through `__any_to_string`, which dispatches on the tag,
  finds no int tag, and formats the bits as `%g`.
- **Comparison** is worse because it is silent: `OS.system(cmd) == 0` compares a
  raw `0` against a NaN-boxed `0`, so it is *always false* even though the value
  prints as `0`.

Callers that need a correct integer can bypass the runtime and declare the libc
function directly — `@extern` results are tagged properly (see
`pantry/src/commands/run.sf` `_libc_system`).

**Root cause:** `bootstrap.sh` passes `--identity-mode` when compiling
`runtime.sf`, and `typed_ptr_to_val`/`ptr_to_val` in `codegen.sf` are pure width
converters that do no tagging. Production and consumption are both untagged, so
indexing, field access and calls all work; only tag-inspecting boundaries break.

**Fix:** tag at the boundary in `runtime.sf` via `__rt_tag_ptr` (`__list_join`
is the correct precedent), never in codegen — doing both double-tags. Groups
must be converted atomically or a printing bug becomes a segfault: (a) enum
construction plus the field loads in `match_body.sf`, (b) closures plus
`gen_indirect_call`, (c) class instances plus `gen_get_field`.


## Fixed

- ~~#1: Functions in imported modules can't call each other~~ — Fixed: ObjFunction now stores owning module, OP_GET_GLOBAL uses correct module context.
- ~~#3: Type checker if-block scoping~~ — Not a bug, correct behavior.
- ~~#4: @import path resolution~~ — Fixed: findModule call in parseFile.
- ~~#5: Map() not callable~~ — Fixed: set returnType on Map's init FunctorType.
- ~~#7: No string escape sequences~~ — Fixed.
- ~~#8: list vs List case~~ — Fixed.
- ~~#9: anyType not universal supertype~~ — Fixed: NODE_LIST/NODE_MAP/NODE_GET/NODE_CALL all handle anyType.
- ~~#10: Imported module functions crash~~ — Fixed: same as #1.
- ~~Nested closures crash~~ — Fixed: declareVariable for nested fun declarations.
- ~~#14: break/continue rejected in while/for loops~~ — Fixed: added loop depth tracking for NODE_WHILE and NODE_FOR.
- ~~#15: Qualified type references (`Module.Type`)~~ — Fixed: parser handles dotted names in type annotations, type checker resolves from module.
- ~~#16: Empty list not assignable to typed `List<T>` fields~~ — Fixed: NODE_SET propagates field type as currentAssignmentType; raw `List` accepted for list literals.
- ~~#17: Type checker NULL file path for relative imports~~ — Fixed: `setTypecheckFile(path)` called before `evaluateTree` in checkFile mode.
- ~~#18: Cross-module enum pattern matching~~ — Fixed: OP_IMPORT now pops the path string (was leaking on stack); match arms support dotted qualified names.
- ~~#19: GC not marking suspended call frame locals~~ — Fixed: OBJ_CALL_FRAME marking now traces `frame->stack`, `frame->stored`, `frame->result`.
- ~~#11: Flow narrowing for primitives in unions~~ — Fixed: narrowing check used `Expr.type` (TypeNode pointer) instead of `Expr.self.type` (NodeType enum) — always read as non-NODE_BINARY.
- ~~#13: Bare return without semicolon~~ — Fixed: `returnStatement()` now checks for `}` and EOF as implicit void return.
- ~~#20: List.pop() removes first element~~ — Fixed: pop now removes from `count - 1` instead of index 0.
