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

### 13. Bare `return` without semicolon consumes next `}`

**Reproduction:**
```saffron
fun test() {
    if (true) {
        return
    }
    IO.println("after")
}
```

**Expected:** `return` exits the function.
**Actual:** Parser error `Expect expression` at `}` — the `return` statement tries to
parse an expression (the `}`) because there's no semicolon or recognized terminator.

**Fix:** `returnStatement()` should check for `}` and `EOF` the same way `yield` does.
**Workaround:** Always use `return;` with a semicolon, or `return nil`.

### 11. Flow narrowing doesn't work for primitives in union types

**Reproduction:**
```saffron
fun test(x: Number | String) {
    if (x is Number) {
        var y = x.to_string()  // Error: "Attempting to get from invalid type"
    }
}
```

**Expected:** `x` narrows to `Number` in the then-branch.
**Actual:** Narrowing creates the scope entry but variable resolution finds the original union type from the enclosing scope.

**Workaround:** Narrowing works for class types, just not primitives.

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
