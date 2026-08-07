# FFI (`@extern`)

The `@extern` decorator lets you call C functions (or any function with C calling convention) directly from Saffron. This is how the standard library bridges to system calls, math functions, and runtime internals.

## Syntax

```saffron
@extern("C_SIGNATURE") fun saffron_name(params): ReturnType
```

The signature string is **required**. `@extern fun name(...)` without the
parenthesized C signature is a parse error — the compiler needs the C types to
know how to marshal each argument.

The string is a C-style declaration written with LLVM IR type names:
`RET c_name(PARAM, PARAM, ...)`. `void*` and `void**` are accepted as spellings
of `i8*` and `i8**` (`normalize_c_type`); everything else must already be an
LLVM type name. The `c_name` is the symbol that gets called and `declare`d.

## Supported C types

Only a fixed set of C types can cross the boundary. The parameter side and the
return side support **different** sets (`extern_param_type_supported`,
`extern_ret_type_supported` in `intrinsics_body.sf`):

| C type (LLVM) | Param | Return | Saffron equivalent |
|---------------|:-----:|:------:|--------------------|
| `i64` | yes | yes | `Int` (or `Any` — see marshalling) |
| `i32` | yes | yes | `Int` (also `Bool` on return) |
| `double` | yes | yes | `Float` |
| `void*` / `i8*` | yes | yes | `String` / raw pointer as `Int` |
| `void**` / `i8**` | yes | **no** | pointer-to-pointer as `Int` |
| `...` (varargs) | yes | n/a | trailing variadic C args |
| `void` | n/a | yes | (no return type) |

Notes:
- `void**`/`i8**` is param-only. A `void**` return is a compile error.
- `...` marks a variadic tail (as in `printf`); it is only meaningful as a
  parameter. When a signature contains `...`, the call is emitted with the full
  parenthesized parameter-type list so LLVM knows the fixed/variadic split.
- `void` is only a return type.

An unsupported/unconvertible C type is a **compile-time error at the signature**,
not a downstream LLVM assembler failure. The messages are exact:

- Parameter: `@extern "<sig>": parameter N has C type '<T>', which the FFI boundary cannot convert. Supported: i64, i32, double, void*/i8*, void**/i8**.`
- Return: `@extern "<sig>": return C type '<T>' cannot be boxed at the FFI boundary. Supported: void, i64, i32, double, void*/i8*.`

(A `float`, `i16`, `i1`, etc. each need their own conversion that does not exist
yet, so they are rejected rather than silently miscompiled — the general form of
BUGS #24's param/return asymmetry.)

## Argument marshalling and the NaN-box boundary

Saffron values are NaN-boxed `i64`s (tagging lives in `src/runtime/runtime.sf`).
C code expects
raw machine values, so **every `extern` argument is untagged before the call** —
the one exception is an `i64` parameter whose Saffron annotation is `Any`. Each C
type is marshalled by `gen_extern_call` as follows:

| Declared C param | Marshalling |
|------------------|-------------|
| `i64`, annotated `Any` | passed through **with its tag intact** — opaque box in transit |
| `i64`, any other annotation | `__val_untag_int`: mask 48 bits + sign-extend, then pass as `i64` |
| `i8*` (`void*`) | `emit_untag_ptr`: strip the pointer tag, pass the raw address |
| `i8**` (`void**`) | untagged to a typed `i8**` pointer |
| `i32` | untag, then `trunc i64 -> i32` |
| `double` | `emit_untag_float`: recover the unboxed double |

Why untag-by-default: a raw `i64` that reaches a C pointer must carry no tag, or
the pointee is garbage — `malloc(64)` once received `0x7FF9000000000040` and every
`i64`-param extern got junk (BUGS #24). The 48-bit mask + sign-extend is safe for
the three things an `Int` param can hold: a NaN-boxed `Int` (payload recovered,
negatives included), a `tag_ptr`'d handle (tag stripped, address recovered), and
an already-raw address (identity, since real addresses are < 2^47).

Why `Any` is the exception: a value the C side only **stores and hands back** must
keep its tag, because untagging destroys its type. The scheduler's
`store_result(handle, value: Any)` ferries an opaque result through C; untagging a
tagged `42` down to a raw `42` made `task.await() == 42` compare false, and a
`Float` result was truncated by `fptosi` (BUGS #38). Annotate such a passthrough
param `Any` to preserve the box.

### Return boxing

The return value is re-boxed by C type, and for `i64`/`i32` returns the **Saffron
return annotation** picks the box:

| Declared C return | Result |
|-------------------|--------|
| `void` | no value (Nil); call emitted as `call void` |
| `i8*` (`void*`) | tagged as a `String` pointer via `emit_tag_ptr_nullable` — a NULL result stays `0` so `== 0` / `!= nil` guards work (BUGS #84) |
| `double` | tagged `Float` |
| `i32` | `sext` to `i64`, then boxed by annotation: `Any` → raw int, `Bool` → tagged bool, else tagged `Int` |
| `i64` | boxed by annotation: `Any` → raw (untouched), `String` → `inttoptr` + tag as pointer, `Bool` → tagged bool, else tagged `Int` |

An `i64`/`i32` extern that returns an already-boxed Saffron value (a Float, a
task handle, a list) should be annotated `Any` so codegen returns the payload
untouched and lets the receiving context do the single conversion. Annotating it
`Int` re-tags the payload and corrupts a Float's exponent bits (BUGS #38).

## Examples

### C math functions

```saffron
@extern("double sin(double)") fun sin(x: Float): Float
@extern("double sqrt(double)") fun sqrt(x: Float): Float
@extern("double pow(double, double)") fun pow(x: Float, y: Float): Float

IO.println(sin(3.14159 / 2))  // ~1.0
IO.println(sqrt(16.0))        // 4.0
```

### Runtime GC functions

```saffron
@extern("void __gc_collect()") fun gc_collect()
@extern("i64 __gc_stat_alloc_count()") fun gc_alloc_count(): Int
@extern("i64 __gc_stat_total_bytes()") fun gc_total_bytes(): Int
```

### Pointers and strings

A `void*` param is untagged to a raw address; a `void*` return is tagged back into
a `String`/pointer value. This is how the stdlib wraps libc allocation and string
functions (`src/lib/string.sf`, `src/lib/net.sf`):

```saffron
@extern("void* malloc(i64)") private fun _malloc(size: Int): Int
@extern("void free(void*)")  private fun _free(ptr: Int)
@extern("i64 strlen(void*)") private fun _strlen(s: Int): Int
@extern("void* strstr(void*, void*)") private fun _strstr(haystack: Int, needle: Int): Int
```

Passing an `i8*` param a `String` value works because the string's data pointer is
untagged in place. Storing the returned address as an `Int` and handing it back to
another `void*` param round-trips through the same untag.

### An `Any` param and an `Any` return (opaque passthrough)

When C only stores and returns a Saffron value without inspecting it, annotate the
`i64` slot `Any` on both ends so the NaN-box tag survives
(`src/lib/scheduler.sf`):

```saffron
// value is stored verbatim into a C-side result table, tag and all
@extern("void __sched_store_result(i64, i64)") fun store_result(handle: Int, value: Any)

// the result read back is an opaque box; Any returns it untouched
@extern("i64 __sched_get_task_result()") fun get_task_result_global(): Any
```

### Passing a closure as a raw pointer

A closure box is a tagged pointer. Declared as a C `i64` param, it is untagged to
its raw address, which the C trampoline reads as `[fn_ptr, env]`
(`src/lib/thread.sf`):

```saffron
@extern("i64 sf_thread_spawn(i64)") fun _spawn(closure: Fun): Int
```

### Variadic and `i32`

```saffron
@extern("i32 puts(void*)") private fun _puts(s: Int): Int
@extern("void exit(i32)")  private fun _exit(code: Int)
@extern("void srand(i32)") private fun _srand(seed: Int)
```

## Intrinsics

The `@intrinsic` decorator marks functions that map to LLVM intrinsics or
compiler-provided operations (handled by `gen_intrinsic_call`), not to a named C
symbol. Examples include `load64`/`store64`, `load8`/`store8`, `tag_ptr`,
`untag_ptr`, and `__suspend`:

```saffron
@intrinsic fun load64(addr: Int): Int
@intrinsic fun store64(addr: Int, val: Int)
```

`untag_ptr`/`tag_ptr` are the escape hatch for moving a value across the tag
boundary explicitly (e.g. handing a raw address back to Saffron as a pointer).

## Notes

- The C signature string is required — `@extern fun name(...)` without it is a parse error.
- The declared function has no body — the compiler emits a `declare` and a direct call to the named C symbol.
- You are responsible for C type correctness — the compiler trusts your signature and only checks that each C type is one it can marshal.
- Link the object file or library that provides the symbol when building: `tools/saffron build app.sf -o app` (the runtime already links the standard C library).
