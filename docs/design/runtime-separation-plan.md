# Runtime Separation Plan

## Overview

Extract all inline runtime functions from `src/compiler/codegen.sf` into a standalone `src/runtime.ll` file. The codegen will emit only `declare` statements for runtime functions, and user programs will link against the precompiled runtime.

---

## 1. Complete Inventory of Runtime Functions

### 1.1 List Runtime (`list_runtime()`)

| Function | Signature (LLVM IR) | Description |
|----------|---------------------|-------------|
| `@__list_new` | `() -> i64` | Allocate a new empty list (returns %List* as i64) |
| `@__list_push` | `(i64 %list, i64 %value) -> void` | Push element, grow if needed |
| `@__list_get` | `(i64 %list, i64 %index) -> i64` | Get element at index |
| `@__list_set` | `(i64 %list, i64 %index, i64 %value) -> void` | Set element at index |
| `@__list_length` | `(i64 %list) -> i64` | Return element count |
| `@__list_pop` | `(i64 %list) -> i64` | Remove and return last element |

### 1.2 Map Runtime (`map_runtime()`)

| Function | Signature (LLVM IR) | Description |
|----------|---------------------|-------------|
| `@__map_new` | `() -> i64` | Allocate a new empty map |
| `@__map_set` | `(i64 %map, i64 %key, i64 %value) -> void` | Set key-value (uses strcmp on keys) |
| `@__map_get` | `(i64 %map, i64 %key) -> i64` | Get value by key (0 if not found) |
| `@__map_has` | `(i64 %map, i64 %key) -> i64` | Check key existence (0 or 1) |
| `@__map_keys` | `(i64 %map) -> i64` | Return all keys as a List |

### 1.3 StringBuilder Runtime (`stringbuilder_runtime()`)

| Function | Signature (LLVM IR) | Description |
|----------|---------------------|-------------|
| `@StringBuilder` | `() -> i64` | Create a new StringBuilder (1024-byte initial buf) |
| `@__sb_append` | `(i64 %sb, i64 %str) -> void` | Append string (grows buffer if needed) |
| `@__sb_to_string` | `(i64 %sb) -> i64` | Return the buffer as a string pointer |

### 1.4 Extra Runtime (`extra_runtime()`)

| Function | Signature (LLVM IR) | Description |
|----------|---------------------|-------------|
| `@__str_ends_with` | `(i8* %str, i8* %suffix) -> i64` | Check string suffix (0 or 1) |
| `@__str_split` | `(i8* %str, i8* %delim) -> i64` | Split string into a List of strings |
| `@__str_replace` | `(i8* %str, i8* %old, i8* %new) -> i64` | Replace all occurrences |
| `@__int_to_string` | `(i64 %val) -> i64` | Convert integer to string |
| `@__list_join` | `(i64 %list, i8* %sep) -> i64` | Join list elements with separator |
| `@__list_contains` | `(i64 %list, i64 %val) -> i64` | Check if list contains value (strcmp-based) |
| `@__io_read_file` | `(i8* %path) -> i64` | Read entire file to string |
| `@__io_write_file` | `(i8* %path, i8* %content) -> void` | Write string to file |
| `@__os_exec` | `(i8* %cmd) -> i64` | Execute shell command, return output |
| `@__os_args` | `() -> i64` | Return argv as a List of strings |

---

## 2. LLVM Types

These struct types must be defined in `runtime.ll`:

```llvm
; List = { count, capacity, data* }
%List = type { i64, i64, i64* }

; Map = { count, capacity, keys*, values* }
%Map = type { i64, i64, i64*, i64* }

; StringBuilder = { length, capacity, buffer* }
%SB = type { i64, i64, i8* }
```

---

## 3. Global Variables

```llvm
; Format string for __int_to_string (snprintf)
@.fmt.ld = linkonce_odr unnamed_addr constant [4 x i8] c"%ld\00"

; Mode strings for file I/O
@.str.rb = linkonce_odr unnamed_addr constant [2 x i8] c"r\00"
@.str.wb = linkonce_odr unnamed_addr constant [2 x i8] c"w\00"
@.str.r  = linkonce_odr unnamed_addr constant [2 x i8] c"r\00"

; Command-line arguments (set by main wrapper)
@__argc = weak global i32 0
@__argv = weak global i8** null
```

---

## 4. Declare Statements for codegen.sf

Replace the runtime function definitions with these declares in the generated `.ll` output:

```llvm
; --- Runtime type declarations (opaque in user files) ---
%List = type { i64, i64, i64* }
%Map = type { i64, i64, i64*, i64* }
%SB = type { i64, i64, i8* }

; --- List runtime ---
declare i64 @__list_new()
declare void @__list_push(i64, i64)
declare i64 @__list_get(i64, i64)
declare void @__list_set(i64, i64, i64)
declare i64 @__list_length(i64)
declare i64 @__list_pop(i64)

; --- Map runtime ---
declare i64 @__map_new()
declare void @__map_set(i64, i64, i64)
declare i64 @__map_get(i64, i64)
declare i64 @__map_has(i64, i64)
declare i64 @__map_keys(i64)

; --- StringBuilder runtime ---
declare i64 @StringBuilder()
declare void @__sb_append(i64, i64)
declare i64 @__sb_to_string(i64)

; --- String operations ---
declare i64 @__str_ends_with(i8*, i8*)
declare i64 @__str_split(i8*, i8*)
declare i64 @__str_replace(i8*, i8*, i8*)
declare i64 @__int_to_string(i64)

; --- List operations (extended) ---
declare i64 @__list_join(i64, i8*)
declare i64 @__list_contains(i64, i64)

; --- I/O ---
declare i64 @__io_read_file(i8*)
declare void @__io_write_file(i8*, i8*)

; --- OS ---
declare i64 @__os_exec(i8*)
declare i64 @__os_args()

; --- Globals (extern in user files) ---
@__argc = external global i32
@__argv = external global i8**
@.fmt.ld = external unnamed_addr constant [4 x i8]
@.str.rb = external unnamed_addr constant [2 x i8]
@.str.wb = external unnamed_addr constant [2 x i8]
@.str.r  = external unnamed_addr constant [2 x i8]
```

---

## 5. Migration Plan

### Phase 1: Create `src/runtime.ll` (hand-written)

1. Create a new file `src/runtime.ll` containing:
   - The target triple
   - All type definitions (%List, %Map, %SB)
   - All global variables (@__argc, @__argv, format strings)
   - External C function declarations (malloc, realloc, free, strlen, strcmp, etc.)
   - All 20 runtime function definitions (copied from the current inline emission)
   - The `@main` wrapper that stores argc/argv and calls `@__saffron_entry` or `@__saffron_main`

2. The `@main` wrapper moves from codegen output to runtime.ll, but needs a forward declaration of the entry point. Use `declare i64 @__saffron_entry()` and `declare i64 @__saffron_main()` (whichever exists).

   **Alternative**: Keep the `@main` wrapper in codegen output (since it knows whether `__saffron_entry` or `__saffron_main` is the entry point). Runtime only provides the functions.

   **Recommendation**: Keep `@main` in codegen output. It is small and context-dependent.

### Phase 2: Modify `codegen.sf`

Changes to the `generate()` method (lines 3198-3370):

1. **Remove** the calls to:
   - `this.list_runtime()`
   - `this.map_runtime()`
   - `this.stringbuilder_runtime()`
   - `this.extra_runtime()`

2. **Remove** the inline type definitions for %List, %Map (currently emitted at lines 3320-3323).

3. **Remove** the inline format string globals (lines 3310-3317).

4. **Replace** with a new method `this.runtime_declares()` that returns all the `declare` statements and `external` global references listed in Section 4 above.

5. **Keep** the type definitions emitted as part of user structs (%SB is currently emitted by `stringbuilder_runtime()` -- move the type decl into the declares section).

6. **Remove** the four runtime methods entirely:
   - `list_runtime()` (lines 1968-2065)
   - `map_runtime()` (lines 2135-2289)
   - `stringbuilder_runtime()` (lines 2067-2133)
   - `extra_runtime()` (lines 2291-2559)

   This removes approximately 590 lines of code from codegen.sf.

### Phase 3: Update build commands

The compiler currently produces a single `.ll` file. After separation:

```bash
# Compiling a user program:
saffronc program.sf          # produces program.ll
clang -O2 -o program program.ll src/runtime.ll

# Building the compiler itself (self-hosting):
saffronc src/compiler/main.sf   # produces main.ll
saffronc src/compiler/lexer.sf  # produces lexer.ll
saffronc src/compiler/parser.sf # produces parser.ll
saffronc src/compiler/codegen.sf # produces codegen.ll
clang -O2 -o saffronc main.ll lexer.ll parser.ll codegen.ll src/runtime.ll
```

### Phase 4: Pre-compile runtime.ll to object file (optional optimization)

```bash
# One-time compilation of runtime:
clang -O2 -c -o runtime.o src/runtime.ll

# Link user programs faster:
clang -O2 -o program program.ll runtime.o
```

---

## 6. Can `src/runtime/*.sf` Be Used Instead of Hand-Written IR?

### Current State of `src/runtime/*.sf`

The files in `src/runtime/` (list.sf, map.sf, string.sf, io.sf, gc.sf) are **design documents**, not compilable code. They:

- Use `// EXTERNAL:` comments as placeholders for low-level operations (malloc, memcpy, pointer arithmetic)
- Define a GC-aware object model with headers at offset 0-23 (different from the current codegen which uses raw structs without GC headers)
- Use different function naming conventions (`List_new` vs `__list_new`)
- Assume a richer type system (GC-tracked objects with tag bytes)

### Verdict: Hand-Written IR for Now

**The `src/runtime/*.sf` files cannot produce the needed `runtime.ll` today.** Reasons:

1. **Bootstrap problem**: The Saffron compiler cannot compile code that does raw pointer arithmetic, `getelementptr`, or calls libc functions with typed signatures (i8*, i64*). The codegen emits everything as `i64` and the runtime needs precise pointer manipulation.

2. **Incompatible object model**: The `.sf` files assume GC headers; the current codegen uses bare structs. Switching to GC objects is a separate project.

3. **Placeholder code**: Every low-level operation is commented out as `// EXTERNAL:`. These files express intent, not implementation.

### Future Path

Once the self-hosted compiler gains:
- Inline LLVM IR blocks (like Rust's `asm!` or Zig's `@intToPtr`)
- Pointer type support in the type system
- Ability to call libc directly

...then `src/runtime/*.sf` could be made compilable and produce `runtime.ll`. For now, the hand-written IR (which already exists verbatim in codegen.sf) is the correct approach -- we are simply extracting it to a standalone file.

---

## 7. Summary of Changes

| File | Action |
|------|--------|
| `src/runtime.ll` (NEW) | Hand-written LLVM IR with all 20 runtime functions, types, and globals |
| `src/compiler/codegen.sf` | Remove ~590 lines (4 runtime methods), add ~30 lines (declare emissions) |
| `build_native.sh` | Update link commands to include `runtime.ll` |
| `CLAUDE.md` | Update build instructions to reflect two-file linking |

### Lines of code impact
- **Removed from codegen.sf**: ~590 lines
- **Added to codegen.sf**: ~30 lines (new `runtime_declares()` method)
- **New file `src/runtime.ll`**: ~500 lines (the same IR, properly structured as a standalone module)
- **Net reduction in codegen.sf**: ~560 lines

---

## 8. Risks and Mitigations

| Risk | Mitigation |
|------|-----------|
| Type definition conflicts at link time | Use `type { ... }` in both files identically; LLVM handles structural type matching |
| Global linkage conflicts for @__argc/@__argv | Use `weak` in runtime.ll, `external` in user .ll files |
| Format string globals duplicated | Use `linkonce_odr` in runtime.ll, `external` in user files |
| `@main` definition in two places | Keep `@main` only in codegen output (not in runtime.ll) |
| StringBuilder constructor name collision with user code | Name is `@StringBuilder` -- if user defines a class with same name, it will conflict. Consider renaming to `@__sb_new` during migration |
