# FFI (`@extern`)

The `@extern` decorator lets you call C functions (or any function with C calling convention) directly from Saffron. This is how the standard library bridges to system calls, math functions, and runtime internals.

## Syntax

```saffron
@extern("C_SIGNATURE") fun saffron_name(params): ReturnType
```

The signature string is **required**. `@extern fun name(...)` without the
parenthesized C signature is a parse error — the compiler needs the C types to
know how to marshal each argument.

The string argument is the C function signature using LLVM IR type names:

| LLVM type | Saffron equivalent |
|-----------|-------------------|
| `i64` | `Int` |
| `double` | `Float` |
| `void*` | `String` (pointer to string data) |
| `void` | (no return type) |

## Examples

### Calling C math functions

```saffron
@extern("double sin(double)") fun sin(x: Float): Float
@extern("double cos(double)") fun cos(x: Float): Float
@extern("double sqrt(double)") fun sqrt(x: Float): Float
@extern("double pow(double, double)") fun pow(x: Float, y: Float): Float

IO.println(sin(3.14159 / 2))  // ~1.0
IO.println(sqrt(16.0))        // 4.0
```

### Calling runtime GC functions

```saffron
@extern("void __gc_enable()") fun gc_enable()
@extern("void __gc_collect()") fun gc_collect()
@extern("void __gc_disable()") fun gc_disable()
@extern("i64 __gc_stat_alloc_count()") fun gc_alloc_count(): Int
@extern("i64 __gc_stat_total_bytes()") fun gc_total_bytes(): Int
```

### String arguments

Strings are passed as `void*` (null-terminated C strings):

```saffron
@extern("i64 js_dom_create_element(void*)") fun create_element(tag: String): Int
```

## Intrinsics

The `@intrinsic` decorator marks functions that map to LLVM intrinsics or compiler-provided operations:

```saffron
@intrinsic fun load64(addr: Int): Int
@intrinsic fun store64(addr: Int, val: Int)
```

## Notes

- The C signature string is required — `@extern fun name(...)` without it is a parse error
- The declared function has no body — the compiler generates a direct call to the named C symbol
- You are responsible for type correctness — the compiler trusts your signature
- Link the object file or library that provides the symbol when building: `tools/saffron build app.sf -o app` (the runtime already links standard C library functions)
