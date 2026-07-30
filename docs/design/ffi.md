# FFI (Foreign Function Interface) — Current State & Roadmap

## Current Support

### Syntax

```saffron
@extern("double sin(double)")
fun _sin(x: Float): Float

@extern("i32 snprintf(i8*, i64, i8*, ...)")
fun snprintf(buf: Int, size: Int, fmt: Int): Int
```

### Supported C Types

| C Type | LLVM Type | Saffron Type | Conversion |
|--------|-----------|--------------|------------|
| `int` / `i32` | `i32` | `Int` | `trunc i64 → i32` / `sext i32 → i64` |
| `long` / `i64` | `i64` | `Int` | Pass-through |
| `double` | `double` | `Float` | `bitcast i64 ↔ double` |
| `void*` / `char*` | `i8*` | `Int` | `untag_ptr` / `tag_ptr` |
| `void**` | `i8**` | `Int` | `val_to_typed_ptr` |
| `void` | `void` | `Nil` | Returns 0 |
| `...` (variadic) | `...` | — | Printf-family only |

### What Works

- Calling any C function with primitive args/returns
- Variadic functions (printf, snprintf)
- Opaque pointer handles (FILE*, socket fds)
- 50+ libc functions wrapped in runtime.sf
- Stdlib modules: math.sf, time.sf, random.sf, socket.sf

### Codegen Pipeline

```
@extern("double sin(double)") fun _sin(x: Float): Float
    ↓ Parser: stores "@extern:double sin(double)" in docstring field
    ↓ Codegen: registers in extern_sigs map, adds to known_functions
    ↓ Output: emits `declare double @sin(double)` in LLVM IR
    ↓ Call: gen_extern_call() handles type conversions
    ↓ Linker: clang links with libc (implicit)
```

### Linking

Current command (tools/saffron):
```bash
clang -O2 -w -Wl,-stack_size,0x10000000 -o output \
    program.ll runtime.ll base.ll
```

Additional C files can be linked (async_native.c, socket_native.c).

---

## Gaps

### 1. Library Linking (`-l` flags) — EASY

**Problem:** Can't link external libraries (libcurl, libpng, sqlite3).

**Fix:** Add `--link` / `-l` flags to `tools/saffron` that pass through to clang:
```bash
saffron build app.sf -o app -l curl -l sqlite3 -L /opt/lib
```

**Effort:** ~20 lines in the shell driver.

### 2. Struct By Value — HARD

**Problem:** Can't pass/return C structs by value. Can't define C-compatible struct layouts.

**What's needed:**
- `@repr(C)` decorator on classes to guarantee field layout matches C ABI
- Codegen emits LLVM struct types matching the C layout
- Pass/return by value in extern calls (not just pointers)

**Example goal:**
```saffron
@repr(C)
class Point {
    var x: Float
    var y: Float
}

@extern("Point add_points(Point, Point)")
fun add(a: Point, b: Point): Point
```

**Effort:** Large — type system + codegen + ABI handling.

### 3. Function Pointers / Callbacks — HARD

**Problem:** Can't pass Saffron functions as C callbacks. Can't receive C function pointers.

**What's needed:**
- Function pointer type in extern signatures: `void (*callback)(int)`
- Trampoline layer: wrap Saffron closures into C-compatible function pointers
- Or: only allow non-capturing functions as callbacks

**Example goal:**
```saffron
@extern("void qsort(i8*, i64, i64, i8*)")
fun qsort(arr: Int, count: Int, size: Int, cmp: (Int, Int) => Int)
```

**Effort:** Large — needs codegen trampolines or restrict to non-capturing fns.

### 4. Typed Opaque Pointers — MEDIUM

> **Superseded by [ffi-pointer-discipline.md](ffi-pointer-discipline.md).** This is
> no longer only an ergonomics gap: because pointers and integers are both `Int`,
> `gen_extern_call` cannot decide whether to unbox an `i64` parameter and passes it
> raw, so `malloc(64)` receives `0x7FF9000000000040` and `@process` is entirely
> non-functional. See BUGS #24.

**Problem:** All C pointers are `Int` in Saffron — no type safety.

**What's needed:**
- `Ptr<T>` generic type for type-safe opaque handles
- Prevents accidentally passing a `FILE*` where a `sqlite3*` is expected

**Example goal:**
```saffron
type FileHandle = Ptr<File>
type DbConn = Ptr<Sqlite3>

@extern("i8* fopen(i8*, i8*)")
fun fopen(path: String, mode: String): Ptr<File>
```

**Effort:** Medium — mostly type system, minimal codegen.

### 5. Binding Generation — LARGE

**Problem:** Must manually write @extern for every C function.

**What's needed:**
- Tool that reads C headers and generates .sf files with @extern declarations
- Like Rust's `bindgen` or Zig's `@cImport`

**Effort:** Large — requires a C header parser (or shell out to `clang -dump-decl-json`).

---

## Key Files

| File | Role |
|------|------|
| `src/compiler/parser.sf:1098-1130` | @extern decorator parsing |
| `src/compiler/codegen.sf:46,96` | extern_sigs map storage |
| `src/compiler/codegen/stmts_body.sf:44-49` | @extern registration |
| `src/compiler/codegen/intrinsics_body.sf:96-142` | C signature parsing |
| `src/compiler/codegen/intrinsics_body.sf:144-224` | gen_extern_call (type conversion + call emission) |
| `src/compiler/codegen/output_body.sf:595-606` | LLVM declare emission |
| `src/lib/math.sf` | Example: wrapping libm |
| `src/lib/socket.sf` | Example: wrapping custom C (socket_native.c) |
| `src/runtime/runtime.sf:1-40` | 50+ libc @extern declarations |
| `tools/saffron:240` | Clang linking invocation |

---

## Comparison with Other Languages

| Feature | Saffron | Go (cgo) | Rust | Zig |
|---------|---------|----------|------|-----|
| Call C functions | ✓ (@extern) | ✓ | ✓ (unsafe extern) | ✓ (@extern) |
| Pass structs by value | ✗ | ✓ | ✓ (#[repr(C)]) | ✓ |
| Callbacks to C | ✗ | ✓ (with trampolines) | ✓ (extern fn) | ✓ |
| Import C headers | ✗ | ✗ (manual) | ✗ (bindgen separate) | ✓ (@cImport) |
| Library linking | Minimal | ✓ (#cgo LDFLAGS) | ✓ (build.rs) | ✓ |
| Variadic calls | ✓ | ✗ | ✗ | ✓ |

Saffron's FFI is closest to Go's manual approach but with Zig-like `@extern` syntax. The main gaps are struct layout control and library linking.
