# Known Bugs

## Critical

### 1. Nested closures crash in imported modules

**Reproduction:**
```saffron
// lib.sf
fun outer(x: String) {
    fun inner(): String {
        return x  // captures upvalue from outer
    }
    return inner()
}
```
```saffron
// main.sf
import "lib.sf" as Lib
IO.print(Lib.outer("hello"))  // Runtime error at line 0
```

**Expected:** Works like it does in the main script.
**Actual:** `Runtime error. [line 0] in outer()`

**Impact:** Blocks writing any non-trivial stdlib in Saffron (e.g., the JSON parser).
**Location:** Likely in `executeModule` / `interpret` in `src/vm.c` — the module's call frame
or upvalue handling differs from the main script path.

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
