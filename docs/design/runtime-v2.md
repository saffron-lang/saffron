# Runtime v2: LLVM Lib Codegen, First-Class Modules, First-Class Types

## Vision

Every value in Saffron is an object with methods. Every module is a namespace object. The compiler generates typed LLVM IR through a structured builder (not string concatenation). NaN-boxing enables runtime type dispatch for `Any`-typed values.

```saffron
// This just works — no magic, no hardcoding
var name = "hello"
IO.println(name.length().to_string())  // String.length() → method on String class
IO.println(name.to_upper())            // String.to_upper() → method on String class

var items = [1, 2, 3]
items.push(4)                          // List.push() → method on List class
IO.println(items.length().to_string()) // List.length() → method on List class

import "@os" as OS
IO.println(OS.cwd())                   // OS.cwd() → function in OS module
```

No special-cased dispatch for String vs List vs Map. No hardcoded IO/OS/GC blocks. The codegen just compiles method calls — the type system determines which implementation to call.

---

## Three Pillars

### 1. LLVM Lib Codegen

**Status:** Infrastructure wired, BlockBuilder working end-to-end.

Replace 620+ `this.emit_indent(...)` string-concat calls with typed `BlockBuilder` method calls:

```saffron
// Old (string concat):
this.emit_indent(local + " = add i64 0, " + val)

// New (typed builder):
var result = this.llvm_block_builder.add(zero_const, int_const)
```

**Benefits:**
- Type safety (can't emit malformed IR)
- NaN-boxing via `NanBox` wrapper (tag/untag at value creation)
- Eliminates string parsing bugs (the #1 source of codegen issues this session)
- Structured output enables optimization passes

**Migration:** Function by function, dual-path with `use_llvm_lib` flag. Both paths produce the same IR. Once validated, remove string path.

### 2. First-Class Modules

**Status:** Universal dispatch working for non-builtins. IO hardcoded dispatch remains.

Every `import` creates a module namespace. Accessing `Mod.func()` is a direct call through the module's prefix — no object evaluation, no variable loading.

```saffron
import "@os" as OS     // OS is a namespace, not a value
OS.cwd()               // → direct call to @stdlib_os_cwd()

import "@http" as Http // Same mechanism
Http.serve(8080)       // → direct call to @stdlib_http_serve(8080)
```

**End state:**
- IO is auto-imported (only builtin)
- OS, GC, Reflect, and everything else requires explicit `import`
- No hardcoded method tables in the codegen
- `gen_method_call` is ONE universal dispatch path for all modules

**What removes the IO hardcoded block:**
- `println(value: Any)` needs runtime type dispatch → requires NaN-boxing
- OR: the codegen coerces at call sites (knows type at compile time) and calls `stdlib_io_println(string)`
- Second option works TODAY once io.sf is auto-imported with real prefix

### 3. First-Class Built-in Types

**Status:** Not started. Design below.

Every primitive type is a Saffron class with methods backed by extern/intrinsic implementations:

```saffron
// src/lib/string.sf
class String {
    var _ptr: Int  // raw pointer to null-terminated bytes

    @extern("i64 strlen(void*)") fun _strlen(ptr: Int): Int

    fun length(): Int {
        return _strlen(this._ptr)
    }

    fun to_upper(): String {
        return _to_upper(this._ptr)
    }

    fun contains(sub: String): Bool {
        return _strstr(this._ptr, sub._ptr) != 0
    }

    fun split(delim: String): List<String> {
        return _split(this._ptr, delim._ptr)
    }

    fun to_string(): String {
        return this  // already a string
    }

    fun slice(start: Int, end: Int): String {
        // allocate, memcpy, null-terminate
    }
}
```

```saffron
// src/lib/number.sf
class Number {
    var _val: Int  // raw i64 value (or NaN-boxed tagged int)

    fun to_string(): String {
        return _int_to_string(this._val)
    }

    fun abs(): Number {
        if (this._val < 0) { return Number(0 - this._val) }
        return this
    }

    fun floor(): Number { return this }  // already integer

    fun to_float(): Float {
        return _sitofp(this._val)
    }
}
```

```saffron
// src/lib/list.sf
class List<T> {
    var _ptr: Int  // pointer to { count, capacity, data_ptr }

    fun length(): Int { return _load64(this._ptr) }
    fun push(item: T) { _list_push(this._ptr, item) }
    fun pop(): T { return _list_pop(this._ptr) }
    fun get(index: Int): T { return _list_get(this._ptr, index) }
}
```

**Benefits:**
- `value.to_string()` works for ANY type (method on the object)
- `println(value: Any)` just calls `value.to_string()` — no compile-time coercion needed
- No more `coerce_to_string` in the codegen
- No more type-specific dispatch tables (`builtin_methods` map disappears)
- Method resolution is uniform: look up method on the object's class
- User classes and built-in types use identical dispatch

**How it works with NaN-boxing:**
- A tagged i64 value encodes BOTH the data and the type
- Method calls on Any-typed values: check tag → determine class → call method
- Method calls on known-typed values: compile-time dispatch (no tag check needed)
- `"hello".length()` → compiler knows it's String → calls `String__length` directly
- `any_var.length()` → runtime checks tag → dispatches to String__length or List__length

---

## Execution Order

### Phase A: LLVM Lib Codegen (in progress)

1. ✅ Wire Module/FunctionBuilder/BlockBuilder into Codegen class
2. ✅ Fix cross-module class field access
3. ✅ Fix BlockBuilder method calls
4. 🔄 Fix emit_ir param emission
5. 🔄 Create FunctionBuilder per gen_function, parallel emission for IntLit
6. Convert remaining gen_expr paths (BoolLit, StringLit, Binary, Call, etc.)
7. Convert gen_stmt paths (VarDecl, If, While, Return)
8. Convert gen_method_call (the biggest one)
9. Remove string emit path, `use_llvm_lib` flag, `sb` field

### Phase B: First-Class Modules (partially done)

1. ✅ Register IO/OS/GC/Task/Reflect in module_prefixes
2. ✅ Universal module dispatch at top of gen_method_call
3. ✅ OS/GC require explicit import
4. ✅ io.sf/os.sf/gc.sf self-contained with own externs
5. Auto-import io.sf with real prefix (blocked by duplicate symbol issue)
6. Remove IO hardcoded dispatch (needs println(Any) or compile-time coercion routing)
7. Remove OS/GC hardcoded dispatch (fallback dispatch handles it)
8. Delete 200+ lines of dead hardcoded dispatch code

### Phase C: NaN-Boxing (via LLVM lib)

1. Complete base_nanbox.ll (copy missing symbols from base.ll)
2. LLVM lib codegen uses NanBox wrapper: `nb.tag_int()`, `nb.tag_ptr()`
3. All literals tagged at creation, arithmetic untags/retags
4. Compile runtime without --identity-mode
5. Add --nanbox flag to driver (or make it default)
6. Implement `__any_to_string` using tag checks

### Phase D: First-Class Built-in Types

1. Define `src/lib/string.sf` with String class wrapping `_ptr`
2. Define `src/lib/number.sf` with Number class wrapping `_val`
3. Define `src/lib/list.sf` with List<T> class wrapping `_ptr`
4. Define `src/lib/map.sf` with Map<K,V> class wrapping `_ptr`
5. Define `src/lib/bool.sf` with Bool class
6. Method dispatch: compiler looks up method on the value's TYPE class
7. Remove `builtin_methods` map from Codegen
8. Remove `emit_builtin_dispatch` from methods_body.sf
9. `println(value: Any)` → `IO.println(value.to_string())` — just works

### Phase E: Cleanup

1. Remove `coerce_to_string` from codegen
2. Remove string-encoded data structures (class_fields string, etc.)
3. Remove `identity_mode` (everything uses NaN-boxing)
4. Remove `base.ll` (only base_nanbox.ll linked)
5. Single dispatch path in gen_method_call (no special cases)

---

## Dependencies

```
Phase A (LLVM lib) ──────────────────────────────────────────┐
                                                              ▼
Phase B (modules) ──── needs io.sf auto-import ──── Phase C (NaN-boxing)
                                                              │
                                                              ▼
                                                    Phase D (first-class types)
                                                              │
                                                              ▼
                                                    Phase E (cleanup)
```

Phase A and B are mostly independent and can proceed in parallel.
Phase C requires Phase A (NanBox wrapper in LLVM lib).
Phase D requires Phase C (runtime type dispatch for Any).
Phase E is the final cleanup after everything works.

---

## Success Criteria

When done:
- `IO.println(42)` works without any hardcoded dispatch
- `"hello".length()` is a method call on a String object
- `[1,2,3].push(4)` is a method call on a List object
- `import "@os" as OS; OS.cwd()` goes through the same dispatch as user modules
- The codegen has ZERO type-specific if-chains
- The LLVM lib produces identical IR to the current string path
- NaN-boxing is the default (identity-mode only for bootstrap)
- Adding a new method to String = adding a `fun` to `src/lib/string.sf` (no codegen changes)
