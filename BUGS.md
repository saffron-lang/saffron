# Known Bugs

## Critical

### 1. Functions in imported modules can't call other functions from the same module

**Reproduction:**
```saffron
// lib.sf
fun double(n: Number): Number { return n * 2 }
fun quad(n: Number): Number { return double(double(n)) }
```
```saffron
// main.sf
import "lib.sf" as Lib
IO.print(Lib.quad(5))  // Runtime error at line 0 in quad()
```

**Expected:** `quad` can call `double` since both are module-level functions.
**Actual:** `Runtime error. [line 0] in quad()` — `double` is not found.

**Root cause:** Module functions are compiled as closures with `OP_GET_GLOBAL` for
inter-function calls. `OP_GET_GLOBAL` looks up `module->obj.fields`. But the module's
fields table may not be properly linked during execution of imported code.

**Impact:** Blocks any stdlib module where functions call each other (iter, json, test).
**Location:** `OP_GET_GLOBAL` handler in `src/vm.c` and how `interpret()` / `executeModule()` sets up the module context.

**Note:** This also causes bug #10 below (test.sf calling its own functions).

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
**Note:** Design choice in many languages (Lua, Python). Compile-time local resolution.

## Fixed

### ~~3. Type checker doesn't recognize variables inside `if` blocks from outside~~
Not a bug — correct block scoping behavior.

### ~~4. Type checker doesn't resolve `@` import paths~~
**Fixed** — added `findModule` call in `types.c:parseFile`.

### ~~5. Type checker doesn't know about `Map()` as callable~~
**Fixed** — set `initType->returnType` on Map's type definition.

### ~~7. No string escape sequences~~
**Fixed** — `\"`, `\\`, `\n`, `\t`, `\r`, `\0` supported.

### ~~8. `list` vs `List` case~~
**Fixed**.

### ~~9. `anyType` not treated as universal supertype~~
**Fixed** — NODE_LIST and NODE_MAP now treat `anyType` like NULL (infer from contents). Also NODE_GET and NODE_CALL allow operations on Any.

### ~~Nested closures crash~~
**Fixed** — nested `fun` declarations now call `declareVariable` to register as locals.

## Minor

### 6. No `break`/`continue` keywords

Loops can only be exited via `return` or flag variables.

### 10. Imported module functions crash when calling module-level globals

Same root cause as #1. When an imported module's function calls another function from the same module, it fails because the global lookup doesn't find it in the module's scope.
