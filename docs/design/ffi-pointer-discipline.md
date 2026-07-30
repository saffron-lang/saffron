# A Consistent FFI: `Ptr<T>` as a Class, with Auto-Boxing

## Status

- **Stage:** Design / Proposal
- **Date:** 2026-07-29
- **Supersedes:** gap #4 ("Typed Opaque Pointers") in [ffi.md](ffi.md)
- **Related:** [binary-data.md](binary-data.md) (`@bytes` needs this to become
  pointer-backed), BUGS #24 (the concrete defect this closes)
- **Decision:** `Ptr<T>` is a **real class**, not a primitive type. Raw addresses
  crossing the FFI boundary are **auto-boxed** into it.

---

## 1. The Problem Is Not a Bug, It Is a Missing Type

Saffron's FFI currently spells "integer" and "pointer" the same way: `Int`. C spells
them the same way too (`i64`). Since neither side can tell them apart, the compiler
must guess — and it guesses differently in each direction.

**Returns are boxed.** `gen_extern_call` boxes every return path:
`i8*` → `tag_ptr`, `i32`/`i64` → `tag_int` (per the Saffron annotation),
`double` → `tag_float`.

**Parameters are not.** Declared C parameter types are unboxed *except* `i64`,
which passes through raw (`src/compiler/codegen/intrinsics_body.sf:185-189`):

```saffron
} else {
    // Pass i64 values directly — no untagging needed.
    // Saffron uses identity mode for pointer-as-int values (coro handles, etc.)
    call_args.push("i64 " + val)
}
```

So a NaN-boxed integer goes *in* and a correct integer comes *out*. The C function
receives garbage:

```llvm
%t2 = call i64 @__val_tag_int(i64 64)
%t3 = call i8* @malloc(i64 %t2)     ; malloc receives 0x7FF9000000000040
```

**This is not theoretical.** `@process` is entirely non-functional:
`Process.run("echo hi")` returns `code=-1, stdout=""` because `sf_process_spawn`
receives a tagged `flags`. The same applies to `sf_process_poll`, `_write_stdin`, and
every `i64`-parameter extern in `ssl.sf`, `watch.sf`, `socket.sf`.

Nor is it limited to `@process`: `sf_tcp_connect(i8*, i64)` received a tagged
`port`, so **no network connection could ever succeed** — not even to a local
`python3 -m http.server`. Everything above the socket layer was dead code.

### Why the raw pass-through exists

Of 224 `@extern` declarations, **114 take an `i64` parameter**. Roughly **49** of
those parameters are pointers-disguised-as-integers — coroutine handles, `malloc`
results, buffer addresses — which legitimately travel untagged. Untagging them all
would corrupt those 49; tagging them all would corrupt the other 65. The signature
cannot disambiguate, so the current code picks the option that keeps coroutines alive
and silently breaks arithmetic.

**The fix is not a better guess. It is making the guess unnecessary.**

### Why a runtime tag test is the wrong fix

Sniffing the top 16 bits for `0x7FF8..0x7FFA` is tempting and unsound: a genuine
integer whose payload coincides takes the wrong branch, a heap address above 2^48
takes the wrong branch, and the cost lands on *every* FFI call forever. Worse, it
converts a loud reproducible failure into a silent memory-corruption class.

---

## 2. Decision: `Ptr<T>` Is a Class

Two shapes were considered. The measurements below drove the choice.

### 2.1 What a class costs

```saffron
class Ptr<T> {
    var _addr: Int
}
```

Each pointer becomes a heap object: 24-byte GC header + 8-byte field = **32 bytes and
one allocation per pointer**, versus zero for a primitive. In a tight `while` loop
over a byte buffer that is real GC pressure.

**But that cost is avoidable where it matters and irrelevant where it does not.** The
hot paths are inside `Buffer` (§4), which holds *one* `Ptr` for the whole buffer and
indexes it with an ordinary `Int` offset. Allocation happens per-buffer, not
per-access. Outside `Buffer`, pointers are handles — one `Ptr` per open file, socket,
or TLS context — where 32 bytes is noise.

### 2.2 What a class buys

A primitive can only be passed and compared. A class can carry the API that makes
pointers *safe to use*, which is the actual goal:

```saffron
class Ptr<T> {
    var _addr: Int

    fun is_null(): Bool { return this._addr == 0 }
    fun read(): T
    fun write(value: T)
    fun offset(n: Int): Ptr<T>      // bounds-checkable, returns a new Ptr
    fun as_buffer(len: Int): Buffer // hands off to the safe byte API
}
```

None of this is expressible on a primitive without inventing free-function
namespacing for what are obviously methods. Longer-term this is also where
destructors/`defer`-style cleanup, `Deref`-like traits, and per-pointer-kind
validation would live. A primitive would have to grow into a class eventually;
starting there avoids the migration.

### 2.3 Verified: it already works

Class-typed extern signatures compile and run **today, with no compiler change**, for
pass-through:

```saffron
class Handle { var _a: Int
  fun init(a: Int) { this._a = a } }
@extern("void* malloc(i64)") fun m(size: Int): Handle
@extern("void free(void*)") fun fr(p: Handle)
var h: Handle = m(64)
fr(h)                                // works
```

Codegen emits `%Handle = type { i64 }`, tags the `malloc` result with
`__val_tag_ptr`, and untags it for the `void*` parameter. And once bug #24's parameter
defect is avoided, field access through a hand-boxed `Ptr` works correctly end to end:

```saffron
class Ptr { var _addr: Int
  fun init(a: Int) { this._addr = a } }
@extern("void* calloc(i32, i32)") fun _raw_calloc(n: Int, size: Int): Int
@extern("i64 strlen(void*)") fun _raw_strlen(p: Int): Int
fun alloc_zeroed(size: Int): Ptr { return Ptr(_raw_calloc(size, 1)) }

var p: Ptr = alloc_zeroed(64)
_raw_strlen(p._addr)                 // => 0, correct
```

The field load emits `load volatile i64` and yields a *tagged* `Int`, which the
`void*` parameter path correctly untags via `__val_untag_ptr`. The class adds no new
conversion problem — it participates in the existing discipline.

**The GC is already safe.** `__gc_is_heap_ptr` (`src/runtime/gc.ll:593`) validates a
magic number `0x5AFFC0DEDEADBEEF` at `header+16` before tracing. A raw `malloc`
address stored in `Ptr._addr` has no such header, so the collector skips it — no false
tracing, no attempt to mark foreign memory. The `Ptr` *instance* is GC-managed
normally; the address it holds is correctly treated as opaque data.

### 2.4 Verified: the naive version is a trap

The same class used *directly* as an extern return type, then dereferenced,
segfaults:

```saffron
@extern("void* malloc(i64)") fun m(size: Int): Handle
var h: Handle = m(64)
IO.println(h._a.to_string())         // SEGFAULT
```

`malloc` returns a bare address. Codegen tags it as a pointer, so `h` *looks* like a
`Handle` instance, and `getelementptr %Handle` reads whatever the first 8 bytes of
that allocation happen to be. There is no class layout there and no GC header.

**This is precisely why auto-boxing is required rather than mere annotation.** The
compiler must *construct* a real `Ptr` instance wrapping the address, not relabel the
address as one.

---

## 3. Auto-Boxing at the FFI Boundary

When an extern is declared to return `Ptr<T>`, codegen emits the wrapper rather than
a bare tag:

```saffron
@extern("void* malloc(i64)") fun malloc(size: Int): Ptr<Byte>
```

```llvm
%raw  = call i8* @malloc(i64 %size_untagged)   ; raw address
%inst = call i64 @Ptr()                        ; allocate a real Ptr instance
%addr = ptrtoint i8* %raw to i64
%tagd = call i64 @__val_tag_int(i64 %addr)     ; field holds a tagged Int
; store %tagd into %inst._addr
```

Symmetrically, passing a `Ptr<T>` to a C parameter unboxes: load `_addr`, untag,
pass. Both directions are mechanical and mirror what `Buffer`/`String` boxing already
does elsewhere in codegen.

### 3.1 The dispatch table — the whole fix

`gen_extern_call` dispatches on the **Saffron** type, not only the C type:

| Saffron type | C type | Parameter | Return |
|---|---|---|---|
| `Int` | `i64` | `emit_untag_int` ← **the fix** | `emit_tag_int` |
| `Int` | `i32` | `emit_untag_int` + `trunc` | `sext` + `emit_tag_int` |
| `Ptr<T>` | `i64` / `void*` / `i8*` | load `_addr`, untag, pass | **auto-box** |
| `String` | `void*` / `i8*` | `emit_untag_ptr` | `emit_tag_ptr` |
| `Float` | `double` | `emit_untag_float` | `emit_tag_float` |

Note the shape: apart from the boxing constructor call, **no new conversion logic is
introduced.** Every emit path already exists. The change is that the *type* selects
the path instead of the compiler guessing from the C signature alone.

### 3.2 Type safety from the class

`Ptr<A>` and `Ptr<B>` are distinct class instantiations, so the type checker rejects
passing a `FILE*` where a `sqlite3*` is expected — for free, via the existing generic
machinery. The `T` is a **phantom tag**: `Ptr<File>`, `Ptr<Sqlite3>`, `Ptr<Byte>` need
no layout and need never be defined. Generic classes already work today
(`Ptr<Byte>(...)` compiles and runs).

Because `_addr` is `Int` and `Ptr` exposes no arithmetic, `ptr + 1` is a type error
rather than a silent miscalculation.

---

## 4. Address Arithmetic Moves Behind `Buffer`

77 call sites in `src/lib/` currently do raw arithmetic
(`load8(this._ptr + i)`, `store8(buf + pos, …)`). That arithmetic does not disappear
— it moves into one audited place:

```saffron
class Buffer {
    var _ptr: Ptr<Byte>
    var _len: Int

    fun get(i: Int): Int {
        if (i < 0 or i >= this._len) { throw "Buffer index out of bounds" }
        return _load8(this._ptr, i)          // bounds-checked; offset is an Int
    }
}

@intrinsic fun _load8(base: Ptr<Byte>, offset: Int): Int
@intrinsic fun _store8(base: Ptr<Byte>, offset: Int, val: Int)
```

The intrinsics take `(base, offset)` separately rather than a pre-computed address.
That is the key move: the offset stays an ordinary `Int` with ordinary arithmetic and
ordinary bounds checks, while the base stays an opaque `Ptr`. **Nobody needs pointer
arithmetic once the offset is a first-class parameter** — and one `Ptr` allocation
serves the whole buffer, so §2.1's cost never lands in a loop.

This also **completes `@bytes`**. `src/lib/bytes.sf` today backs `Buffer` with
`List<Int>` — safe, but with no bridge to FFI, so `socket.sf` and `dns.sf` cannot use
it and hand-roll `malloc` + `store8` instead. A pointer-backed `Buffer` is the single
type both worlds share, which is what [binary-data.md](binary-data.md) §1 asked for.

## 5. `@unsafe` Escape Hatch

Some code legitimately needs raw addresses:

- **`src/runtime/runtime.sf` cannot use `Ptr` at all.** It is compiled with
  `--identity-mode` and contains **zero classes** (verified); its allocator
  bootstraps the very machinery `Ptr` depends on. It must stay on raw `Int`.
- WASM linear-memory access, `load_argv`, and the GC's own internals.

Rather than leaving a hole in the type, name it:

```saffron
@unsafe fun ptr_from_int(addr: Int): Ptr<Byte>
@unsafe fun ptr_to_int(p: Ptr<Byte>): Int
```

`@unsafe` functions may perform the conversions the type system otherwise forbids.
The value is that `grep -rn '@unsafe' src/` becomes a complete audit list of where
pointer discipline is suspended. Today that list is "everywhere".

---

## 6. Also Inconsistent: Three FFI Mechanisms, Three Behaviours

`Ptr<T>` fixes the type dimension. Two other inconsistencies belong in the same
effort, because they produce the same class of silent wrongness.

### 6.1 `OS.*` bypasses the stdlib entirely

`OS.foo(...)` mangles directly to extern `@__os_foo`, never entering the function body
in `src/lib/os.sf`. This was discovered the hard way: a wait-status decode added to
`OS.system` in the stdlib had **no effect at all**, because the body is dead code.

Worse, this path keeps its own hand-maintained tagging allowlist
(`methods_body.sf:1154-1165`) naming eight string-returning and two bool-returning
builtins. Anything absent — such as `__os_system` — returns **untagged** and compares
unequal to every literal while printing correctly. That is BUGS #23's
silent-comparison failure, caused purely by a name missing from a hardcoded list.

**Fix:** delete the allowlist. `@extern` declarations in `src/lib/os.sf` already carry
Saffron return types; route module-namespace calls through `gen_extern_call` so one
code path decides tagging for all FFI. A hardcoded name list is a bug generator —
every new runtime function is untagged until someone edits two `or`-chains.

`utils_body.sf:5-6` holds a *third* copy: 38 builtin names paired **positionally**
with 38 signature strings, where one misplaced insertion silently mismatches every
subsequent pair.

### 6.2 `@intrinsic` has no signature discipline

`@extern` at least declares its C types. `@intrinsic` matches by
`name.ends_with("load64")`, takes whatever arguments it finds, and hardcodes its own
conversion. There is no signature, so there is nothing to check — `store8("hello", 3)`
is accepted and writes into string data.

**Fix:** once `Ptr<T>` exists, give intrinsics real parameter types and typecheck them
like any other call. The `(base, offset)` signature in §4 is what that looks like.

---

## 7. Migration

Additive and mechanical. Generic classes and `Ptr<T>` annotations **already parse** —
gen2 accepts `fun m_alloc(size: Int): Ptr<Byte>` today — so there is no bootstrap
constraint.

1. **Fix bug #24 first — DONE.** Untag `i64` params exactly as the `i32` path
   already does. Independently valuable: it makes `@process` *and all networking*
   work, and unblocks everything below.

   Existing handle sites keep working not because they are annotated `Int` — 194
   of the `i64` params are, handles included — but because `__val_untag_int`
   masks 48 bits and sign-extends, making it the **identity on a genuine
   address** while still stripping a tag. Verified for all four shapes: tagged
   positive int, tagged negative int, `tag_ptr`'d handle, raw address. They still
   move to `Ptr<T>` in step 3, which is what makes the property *declared*
   rather than merely true.

   Measured at `b5fd568`, 111 tests, exact exit codes: `gc_test` 139 → 0,
   `test_file` segfault → 19/20, `@process` `out=[]` → `out=[hi]`. `test_async_io`
   and `test_httpx` go 1 → 139 — not a regression: they used to die at connect
   and now reach BUGS #32, which step 4 fixes.
2. **Define `Ptr<T>` in the stdlib** with `is_null`, `offset`, `read`/`write`.
   Plain Saffron; no compiler change.
3. **Teach codegen to auto-box.** `Ptr<T>` return → construct an instance;
   `Ptr<T>` parameter → load `_addr`, untag, pass. Annotate the 49 handle-passing
   sites as `Ptr<T>`. Mechanical, and a wrong annotation now surfaces as a test
   failure rather than silent corruption.
4. **Delete the three hardcoded allowlists** (`methods_body.sf:1154`, `:1162`,
   `utils_body.sf:5-6`); route `OS.*` through `gen_extern_call`. Fixes BUGS #23.
5. **Add `@unsafe`**, annotate the residue (`runtime.sf`, WASM, GC internals), and
   forbid raw `Int` → `Ptr` conversion outside it. This is the breaking step; land it
   last.
6. **Re-back `Buffer` on `Ptr<Byte>`**, then convert `socket.sf`, `dns.sf`, `ssl.sf`,
   `regex.sf`, and `process.sf` off raw `malloc`/`store8`.

Step 1 fixes live bugs; 2–4 remove the bug *generators*; 5–6 are the ergonomic
payoff. Each step is independently shippable.

## 8. What This Buys

| Today | After |
|---|---|
| `malloc(64)` receives `0x7FF9…0040` | receives `64` |
| `Process.run` returns `code=-1` | works |
| `OS.system(c) == 0` always false | works |
| New runtime fn untagged until an `or`-chain is edited | correct by construction |
| Class-typed extern return segfaults on field access | auto-boxed into a real instance |
| `store8("hello", 3)` compiles | compile error |
| `FILE*` accepted where `sqlite3*` expected | compile error |
| Pointer arithmetic in 77 stdlib sites | one bounds-checked `Buffer` |
| `@bytes` cannot reach FFI | shared pointer-backed `Buffer` |
| Pointers have no API | `is_null`, `offset`, `read`, `as_buffer`, room to grow |

The through-line: every row is a case where the compiler currently *guesses* what a
value is. `Ptr<T>` replaces the guess with a declaration — and being a class, it has
somewhere to put the safety.
