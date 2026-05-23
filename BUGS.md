# Known Bugs

## Critical

### 1. Functions in imported modules can't call other functions from the same module

**Reproduction:**
```saffron
// lib.sf
fun double(n) { return n * 2 }
fun quad(n) { return double(double(n)) }
```
```saffron
// main.sf
import "lib.sf" as Lib
IO.print(Lib.quad(5))  // Runtime error at line 0 in quad()
```

**Expected:** `quad` can call `double` since both are module-level functions.
**Actual:** `Runtime error. [line 0] in quad()` — `double` is not found.

**Note:** Nested closures (upvalues within a single function) DO work in imports.
The issue is specifically module-level globals referencing each other.

**Root cause:** Module functions are compiled as closures with `OP_GET_GLOBAL` for
inter-function calls. `OP_GET_GLOBAL` looks up `module->obj.fields`. But at the time
`quad` runs, `double` may not yet be in the module's fields table — OR the function's
global scope doesn't point back to the same module fields table.

**Impact:** Blocks any stdlib module where functions call each other (iter, json, etc.).
**Location:** `OP_GET_GLOBAL` handler in `src/vm.c` and how `interpret()` sets up
the module context for imported files.

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
**Note:** This is a design choice in many languages (Lua, Python have it), but limits expressiveness.

## Type Checker

### 3. Type checker doesn't recognize variables inside `if` blocks from outside

The type checker uses static scoping that doesn't allow variables declared in if-branches
to be referenced later (even though the runtime handles it). This is actually correct behavior
for block scoping — not really a bug but worth noting since Saffron has function-level scoping
at runtime but block-level in the type checker.

### 4. Type checker doesn't resolve `@` import paths

**Fixed** (added `findModule` call in `types.c:parseFile`), but previously the type checker
would try to `readFile("@iter")` directly without resolving the `@` prefix.

### 5. Type checker doesn't know about `Map()` as callable

`Map` is registered as a builtin type but calling it (`Map()`) isn't recognized by the
type checker as returning a map type. The type checker reports "Undefined variable" for
variables assigned from `Map()`.

### 9. `anyType` not treated as universal supertype in `isSubType`

**Reproduction:**
```saffron
IO.print([1, 2, 3])           // [line X] Error at '[': Type mismatch
Reflect.type_of([1, 2, 3])    // same
```

**Expected:** Any value (including list literals) should be assignable to a parameter typed `Any`.
**Actual:** Type checker reports "Type mismatch" when passing a list literal to a function
that accepts `anyType`.

**Root cause:** `isSubType()` in `src/types.c` doesn't short-circuit with `true` when the
target type is `anyType`. Needs a check like `if (superType == anyType) return true;` at
the top of the function.

**Impact:** Spurious type errors on any function call that accepts `Any` and receives a
list, map, or other compound type.

## Minor

### 6. No `break`/`continue` keywords

Loops can only be exited via `return` (from a wrapping function) or flag variables.
Makes iterative code verbose.

### 7. No string escape sequences were supported

**Fixed** — added `\"`, `\\`, `\n`, `\t`, `\r`, `\0` support in both scanner and parser.

### 8. `list` vs `List` — test used wrong case

**Fixed** — `test/builtin_types.sf` referenced lowercase `list` which doesn't exist.
