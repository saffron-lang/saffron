# Bootstrap Compiler Bugs

Issues in the C runtime (bytecode VM) that are fixed or not present in the self-hosted LLVM compiler. These exist only because the C runtime's parser/type-checker has limitations that the bootstrapped version doesn't share.

## #1: `///` docstrings inside class bodies

**Status:** C runtime bug, works in bootstrapped compiler

The C parser treats `///` inside a class body as an attempt to parse a method declaration, failing with "Expect 'fun' keyword before method name."

```saffron
class Foo {
    /// This doc-comment causes a parse error in the C runtime.
    fun bar() {}
}
```

**Workaround:** Use `//` instead of `///` for method docs when targeting the C runtime.

## #2: Generic type annotations on class fields

**Status:** C runtime bug, works in bootstrapped compiler

The C parser cannot handle `Map<String, String>` or `List<SomeType>` as field type annotations.

```saffron
class Config {
    var headers: Map<String, String>  // Parse error in C runtime
}
```

**Workaround:** Omit the type annotation and use a default initializer:
```saffron
class Config {
    var headers = {}
}
```

## #3: `data` is a reserved keyword

**Status:** C runtime bug (maps to `TOKEN_DATACLASS`)

The identifier `data` cannot be used as a variable name.

```saffron
var data = parse(input)  // Error: Expect variable name
```

**Workaround:** Use a different name (`toml_data`, `result`, etc.)

## #4: Untyped function parameters

**Status:** C runtime limitation

The C type checker requires all function parameters to have type annotations. The bootstrapped compiler supports inference.

```saffron
fun process(items) { ... }  // Error in C runtime
fun process(items: List<String>) { ... }  // OK
```

**Workaround:** Always annotate params when targeting the C runtime. For truly generic code, leave it for the bootstrapped version.

## #5: Forward references to functions

**Status:** C runtime limitation

Functions must be defined before they're called. The bootstrapped compiler resolves all declarations in a first pass.

```saffron
fun main() { helper() }  // Error: Undefined variable 'helper'
fun helper() { ... }
```

**Workaround:** Define helper functions above the functions that call them.

## #6: String comparison operators (`<`, `>`, `<=`, `>=`)

**Status:** C runtime limitation

The VM only supports `<`/`>` on numbers, not strings. The bootstrapped compiler supports lexicographic string comparison.

**Workaround:** Use `.index_of()` against an ordered character set, or implement a `_strcmp` helper.

---

# Native Compiler (saffronc) Bugs

Issues in the self-hosted native compiler (`build/saffronc`).

## #7: Segfault on class fields with default initializers

**Status:** saffronc codegen bug

The native compiler segfaults when a class has fields with inline default values.

```saffron
class Config {
    var name = ""        // SEGFAULT in saffronc
    var port = 8080      // SEGFAULT
    fun init() {}
}
```

**Status:** FIXED (segfault resolved). Parser now accepts `var name = expr` syntax
in class bodies. The default expression is parsed but not yet auto-assigned —
use `init()` to set values until default field initialization is implemented.

**Workaround:** Use typed field declarations and assign defaults in `init()`:
```saffron
class Config {
    var name: String
    var port: Int
    fun init() {
        this.name = ""
        this.port = 8080
    }
}
```
