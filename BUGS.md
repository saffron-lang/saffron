# Known Bugs

## Open

The block of bugs numbered 50–57 came out of building the playground; the full
narrative log for that work, including the ones that were fixed along the way and
the workarounds each one forced, is at `docs/design/playground-bug-log.md`. Every
entry below was re-verified against `build/saffronc` on 2026-07-30 before being
filed here — the log also contains entries that no longer reproduce, which are
noted there rather than carried forward.

### 50. An overridden method is not dispatched from an inherited method

**Reproduction:**

```saffron
class Animal {
    var name: String
    fun init(name: String) { this.name = name }
    fun speak(): String { return "..." }
    fun describe(): String { return "${this.name} says ${this.speak()}" }
}
class Dog extends Animal {
    fun speak(): String { return "Woof" }
}
var d = Dog("Rex")
IO.println(d.speak())      // "Woof"          — correct
IO.println(d.describe())   // "Rex says ..."  — wrong, want "Rex says Woof"
```

A direct call on a statically-known `Dog` is fine. The failure is specifically a
self-call from inside an *inherited* method: `Dog__describe` is emitted as a
forwarder to `Animal__describe`, whose body calls `Animal__speak` by static name,
so the override is invisible. Same through a `List<Animal>` holding `Dog`s.

Method calls lower to `<StaticType>__<method>` — there is no vtable and no runtime
dispatch on the object's class tag. This is a design limitation, not a patch:
fixing it means either re-emitting inherited method bodies per subclass (covers
the static-type case only) or real vtables (covers both, much larger).

**`CLAUDE.md` currently advertises polymorphic `speak()` overriding as supported,
so the documentation oversells this.**

### 51. Mutation of a captured variable is lost — captures are by value

**Reproduction:**

```saffron
fun main() {
    var count: Float = 0
    var bump = fun (): Float => {
        count = count + 1
        return count
    }
    IO.println(bump().to_string())    // 0, want 1
    IO.println(bump().to_string())    // 0, want 2
    IO.println("count = ${count}")    // 0, want 2
}
main()
```

*Reading* a captured variable works; only writes are lost, in both directions —
the closure never sees its own increment, and the enclosing scope never sees the
write.

`src/compiler/codegen/output_body.sf:26-85` hoists nested functions to top level
with their free variables appended as ordinary by-value `i64` parameters
(`find_free_vars_stmts` → `full_params`), so a write assigns to the callee's own
copy. Real mutable capture needs boxed cells — captures passed as pointers, with
the enclosing frame's variable promoted to a heap cell — which the comment at
line 26 hints at ("capture POINTERS") but the code does not do.

**`CLAUDE.md` shows a mutable-counter closure as a supported pattern, so the
documentation is wrong here too.**

### 52. Indexing a list with a `Float`-typed value silently reads element 0

**Reproduction:**

```saffron
var chars = ["a", "b", "c", "d"]
var f: Float = 2.0
IO.println(chars[f])   // "a" — want "c"
var i: Int = 2
IO.println(chars[i])   // "c" — correct
```

No warning, no error. The index path emits `__val_untag_int` on a float-tagged
value, so the double's bit pattern is *reinterpreted* as an integer rather than
converted; for small values the low bits are zero, which lands on index 0.
`.floor()` is the workaround and nothing tells you that you need it.

Same underlying confusion as the identity-mode float bug, but on the native
target and reachable from ordinary user code, which makes it more dangerous. #53
is a live instance of it in the shipped stdlib.

### 53. `UUID.v4()` always returns the all-zero UUID

**Reproduction:**

```saffron
import "@uuid" as UUID
IO.println(UUID.v4())   // 00000000-0000-4000-8000-000000000000, every time
```

`src/lib/uuid.sf` builds the string with `_to_hex(Random.int(0, 15))`, and
`_to_hex` indexes a 16-element list of hex digits. `Random.int` is declared to
return `Float` (`src/lib/random.sf:13`), so every one of those indexes hits #52
and returns `"0"`. Calling `Random.int(0, 15)` directly returns properly random
values — the corruption is entirely in the list-index step.

Beyond the obvious: any code trusting `UUID.v4()` for uniqueness — request IDs,
temp file names, database keys — is silently getting a constant. `Random.choice`
and `Random.shuffle` index by `Float` the same way and are presumably affected.

Fixing #52 fixes this; declaring `Random.int` as `Int` would also work and is the
honest signature.

### 54. An `Int` literal in a `Float` position yields `nan`

**Reproduction:**

```saffron
fun f(): Float { return 0 }
IO.println(f().to_string())     // nan, want 0
```

`return 0.0` works. There is no implicit Int→Float widening at a `Float`-typed
return (and presumably also at `Float` params and fields), so the integer NaN-box
tag reaches float-formatting code and reads as `nan`. Numeric literals are very
common in `Float` positions.

Belongs in the type checker's coercion rules rather than a codegen patch. Note
implicit Int→Float widening is the agreed direction for the language, so this is
the missing half of that rule rather than a new feature.

### 55. An `@extern` used before its declaration links against the wrong symbol

**Reproduction:**

```saffron
fun use_it(): Float { return later(1.0) }   // used here...
@extern("double sqrt(double)")
fun later(x: Float): Float                  // ...declared here
IO.println(use_it().to_string())
```

The call is emitted as `call i64 @later(...)` — the *Saffron-level* name, which
nothing defines — instead of the extern target `@sqrt`. The `declare` is emitted
correctly, so the file compiles without complaint and fails at link time with
`use of undefined value '@later'`. Worse, string arguments are lowered as `i64 0`
on that path, so even a coincidentally-matching symbol would be called with null
pointers.

Declaration order does not matter for an ordinary top-level `fun`, so this is an
ordering dependency specific to `@extern` resolution. Workaround: declare externs
above first use. Related to the `@extern` rework in flight (see #24).

### 56. A field access on the result of an indirect call reads 0

**Reproduction:**

```saffron
class Box { var v: Int
    fun init(v: Int) { this.v = v } }
fun mk(): Fun { return fun (x: Int): Box { return Box(7) } }

var f: Fun = mk()
var typed: Box = f(1)
IO.println(typed.v.to_string())   // 7 — correct
IO.println(f(1).v.to_string())    // 0 — wrong
```

Binding the result to a variable with an explicit `: Box` annotation is correct;
accessing the field directly on the call expression, or through an inferred
variable, reads 0. The object is constructed fine and the receiver is fine — field
*offset* resolution is what fails, because the static type of an indirect call's
result is not recovered (`Fun` carries no return type) and codegen falls back to
offset 0 rather than reporting that it does not know.

M1/M2 from `docs/design/compiler-rewrite.md`: codegen re-deriving a type it does
not have, and spelling "unknown" as something concrete. The real fix is to give
`Fun` a return type in the type system, which is a language change. Workaround:
always annotate a variable holding the result of an indirect call.

### 57. A repeated `--lib-path` duplicates every global in the output IR

**Reproduction:**

```
saffronc --target wasm32 --lib-path .pantry/packages \
                         --lib-path "$PWD/.pantry/packages" src/main.sf out.ll
# error: redefinition of global '@__g_turmeric_prelude__tc_event'
```

Passing the same directory twice — once relative, once absolute, as happens
naturally when a caller adds `--lib-path` and the driver's own auto-discovery in
`tools/saffron:149-158` adds it again — makes the compiler emit each module's
globals twice. The dedupe compares path strings literally, so two spellings of one
directory are two packages. The failure surfaces as an LLVM redefinition error
naming an internal symbol, with no hint that a duplicated flag is the cause.

Fix: canonicalise paths before comparing. Workaround: let the driver discover
`.pantry/packages` itself and do not pass `--lib-path` explicitly.

### 49. `Number` is one surface name for two representations

`str_to_type` maps `"Number"` to `IntType`, which is a lie in one direction:

```saffron
fun area(r: Number): Number { return 3.0 * r * r }
IO.println(area(2.0).to_string())   // prints 0, should be 12
```

The declared type is relabelled `Int` at the boundary, so the caller's
`.to_string()` untags a double as an integer and prints `0` (or garbage like
`37357358909038`). Enum payloads had the same defect from the other end, and that
half **is** fixed — see #45 and #43.

Mapping `"Number"` to `FloatType` instead is the honest reading of the surface
syntax and fixes the case above, but it is a worse lie in the other direction:
stdlib code writes `Number` for values it then uses as list indices and integer
counters. `src/lib/toml.sf:23`'s `fun peek_at(offset: Number)` is the clearest
case — as a double, that offset breaks indexing. Measured both ways
(2026-07-30):

| mapping | `pantry_config` | `test_sorted_set` | `area(): Number` |
|---|---|---|---|
| `Number` → `Int` | 29/29 | 33/33 | `0` (wrong) |
| `Number` → `Float` | 8 failed | `IndexError` | `12` (right) |

Neither mapping is right, because `Number` itself is the defect: one surface name
for two representations, so no single lattice entry can be correct for both uses.
This is M2 in `docs/design/compiler-rewrite.md` ("`Int` is the bottom type")
showing up as a genuine fork in the road rather than a one-line fix.

**Real fix:** retire `Number` in favour of explicit `Int` and `Float` — the
intended direction for the language. That means auditing every `Number` in
`src/lib/*.sf` and deciding per site which one is meant (`peek_at(offset: Int)`,
`number(key: String): Float`, etc.), then removing the surface spelling. Until
then `Int` is the mapping that keeps the stdlib working, and it stays, with the
trade-off documented at the mapping site.

**Correction note.** An earlier version of this file recorded `Number` → `Float`
as fixed and claimed the regression above did not exist ("`pantry_config` passes
29/29 with the Float mapping"). That measurement was taken against a `saffronc`
that did not yet contain the change, and was wrong; the regression is real and
reproduces. The mapping was reverted to `Int`.

### 41. A nested map literal overwrites its parent — silent wrong answer

**Reproduction:**

```saffron
var nest = {"outer": {"inner": 5}}
IO.println(nest.to_string())   // prints {inner: 5}, should be {outer: {inner: 5}}
```

The parser desugars a map literal into a `BlockExpr` holding a `VarDecl` of a
temporary plus one `.set()` per entry, and that temporary is always named
`__map` (`parser.sf`, both desugaring sites). A literal nested inside another
literal therefore declares a *second* `__map` in the same scope: the inner one
wins, the outer map is discarded, and the variable ends up bound to the inner
map. No diagnostic — the program just holds the wrong value.

Needs a unique temporary per literal (a counter, the way `fresh_local()` works
in codegen) rather than the fixed name. Note the two desugaring sites are
copies of each other, so both need it — an instance of the duplicated-logic
mechanism (M5) described in `docs/design/compiler-rewrite.md`.

### 40. A module global shadows a like-named parameter — silent wrong answer

**Reproduction:**

```saffron
import "math" as Math
var x = 42
IO.println(Math.sqrt(16))   // prints 6.48074 (= sqrt(42)), should be 4
```

Remove `var x` and it prints `4`. The input IR is correct — `16` is passed — but
the callee reads the global:

```llvm
define i64 @math_sqrt(i64 %x.arg) {
  %x = alloca i64
  store i64 %x.arg, i64* %x
  %t1 = load i64, i64* @__g_x   ; ← user's global, not the parameter
```

Variable resolution in `expr_body.sf` (three sites: the `Variable` read ~:105,
`Assign` ~:135, and the `gen_arg_value` variable branch ~:1416) checks
`module_globals` before locals, so any function parameter whose name collides
with a user global — `x`, `y`, `n`, `value` — loads the global. `math_pow`,
`math_abs`, etc. are all affected; trivially triggerable from user code.

**Attempted fix and why it was reverted.** An `is_current_param(name)` helper
that shadows the global when `name` is a parameter of the function being
compiled fixed the repro and the direct cases, but regressed `test/pantry_config`
into invalid IR (`use of undefined value '%p'`). Root cause of the regression:
`current_function_name` is not a reliable "am I inside this function's body"
signal. `gen_function` sets it *before* its `register_only` / already-defined
early returns (so the pre-scan leaks a lambda's name), `gen_closure_function`
never sets it at all, and neither restores it — so at the top level it still
points at a previously-compiled lambda whose parameter happens to be named `p`.
The whole attempt was reverted rather than shipped fragile.

**Real fix:** track the parameters of the function currently being *emitted*
explicitly — e.g. a `current_params` set pushed where param allocas are created
and popped at function end — instead of inferring scope from
`current_function_name`. Then have the three resolution sites prefer a local
parameter over a global. Was previously noted as the "global-shadows-param"
work item (task #4 area).

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
**Note:** Design choice — compile-time local resolution. Same as Lua/Python.

### 6. No `break`/`continue` type checking

The compiler has break/continue infrastructure (breakJumps, continueJumps arrays) but the type checker doesn't handle NODE_BREAK/NODE_CONTINUE. Runtime works; type checker just ignores them.

### 12. Type checker segfaults on Any-typed closures in imported modules

**Reproduction:**
```saffron
// src/lib/test.sf contains:
fun mock(name: String) {
    var ret = nil
    var fn = fun (a: Any) => { return ret }
    ...
}
```
```saffron
import "@test" as T
T.mock("x")  // segfault
```

**Expected:** Module imports and runs.
**Actual:** Type checker crashes (segfault) when evaluating closures that capture
variables later assigned to different types (nil → Any), or when `Task.spawn(body)`
is called with an `Any`-typed param inside an imported module.

**Root cause:** The type checker dereferences NULL type pointers when resolving closure
captured variables whose initial type is nil. The cascading "Undefined variable" errors
from the type checker then trigger `runtimeError()` which corrupts VM state during import.

**Impact:** Blocks `@test` import with mock/async features. Inline usage works.
**Workaround:** Inline test functions or avoid nil-initialized captured variables.



### 21. `type` is a reserved keyword — can't use as parameter/variable name

`type` is tokenized as `TOKEN_TYPE` for type alias declarations. Code that uses `type` as a parameter name (e.g. `fun define(name: String, type: String)`) gets a parse error. Consider making it a contextual keyword (only reserved at statement start).


### 22. Cross-module global variable access emits undefined LLVM local

**Reproduction:**
```saffron
// cache.sf
var store: CacheStore = CacheStore()

// query.sf
import "./cache.sf" as Cache
Cache.store.get(key)  // <-- generates %Cache instead of @__g_basil_src_cache_store
```

**Expected:** `Cache.store` loads from the LLVM global `@__g_<prefix>store`.
**Actual:** Codegen emits `load i64, i64* %Cache` — `%Cache` is undefined.

**Root cause:** In `codegen/expr_body.sf` MemberAccess handler (~line 208), module-prefixed
access only checks if the resolved name is a known function. If it's a module-level `var`
(global), the check falls through and tries to load from a nonexistent local variable.

**Fix:** After the known_functions check, add a `module_globals.has(mp_resolved)` check
that emits `load i64, i64* @__g_<prefix><field>` for module-level variables.

**Impact:** Blocks any library that uses module-level globals accessed cross-module
(e.g. basil's `Cache.store` singleton). Workaround: inline the global or avoid module-qualified access.

**Confirmed hitting turmeric (2026-07-30).** Not just basil: `turmeric/src/`
`router.sf`, `prelude.sf` and `index.sf` all reference `_tracking`, a real global
at `turmeric/src/signal.sf:20`, and codegen emits `load i64, i64*
%turmeric_signal__tracking` with no definition. `clang -x ir` rejects the
emitted IR, so **turmeric does not currently build** — this is the blocker, and
it was previously masked because the undefined-variable check only warned (see
#37). The same measurement found zero other occurrences across `test/*.sf`,
`src/lib/*.sf`, and the compiler's own source, so this one variable is the whole
remaining surface of #22 in-repo.

### 23. Runtime functions return untagged i64 into NaN-boxed value space

`src/runtime/runtime.sf` is compiled with `--identity-mode`, where the tag/untag
helpers are no-ops. A runtime function that returns an integer therefore hands
back a raw i64 into value space, where every other value is NaN-boxed.

```saffron
import "@os" as OS
IO.println(OS.system("exit 1"))   // prints 4.94066e-324, not 1
```

The raw `1` is reinterpreted as a subnormal double. Two distinct symptoms:

- **Printing** goes through `__any_to_string`, which dispatches on the tag,
  finds no int tag, and formats the bits as `%g`.
- **Comparison** is worse because it is silent: `OS.system(cmd) == 0` compares a
  raw `0` against a NaN-boxed `0`, so it is *always false* even though the value
  prints as `0`.

Callers that need a correct integer can bypass the runtime and declare the libc
function directly — `@extern` results are tagged properly (see
`pantry/src/commands/run.sf` `_libc_system`).

**Root cause:** `bootstrap.sh` passes `--identity-mode` when compiling
`runtime.sf`, and `typed_ptr_to_val`/`ptr_to_val` in `codegen.sf` are pure width
converters that do no tagging. Production and consumption are both untagged, so
indexing, field access and calls all work; only tag-inspecting boundaries break.

**Fix:** tag at the boundary in `runtime.sf` via `__rt_tag_ptr` (`__list_join`
is the correct precedent), never in codegen — doing both double-tags. Groups
must be converted atomically or a printing bug becomes a segfault: (a) enum
construction plus the field loads in `match_body.sf`, (b) closures plus
`gen_indirect_call`, (c) class instances plus `gen_get_field`.

**Caveat on blanket tagging:** `__gc_is_heap_ptr` (`src/runtime/gc.ll:615`)
rejects any NaN-tagged value as a heap pointer, and nothing in `gc.ll` masks
the tag before marking. So tagging a `__gc_alloc`'d pointer makes the GC stop
tracing it and sweep it while live — strictly worse than the cosmetic printing
bug. Demonstrable today: `"abcdefgh".repeat(4)` is the one function that tags a
GC-allocated buffer, and it prints correctly with no GC pressure but garbage
under it. The `rt_malloc`-based `join`/`replace` survive. Any tagging of
GC-allocated returns must land together with masking the payload in
`__gc_is_heap_ptr`. `__list_join` is safe only because it returns
`rt_malloc`'d memory.

**Related:** the `OS.*` half of this is a hardcoded tagging allowlist
(`methods_body.sf:1154-1165`) that `__os_system` is simply missing from; #24
proposes deleting the allowlist and routing all FFI through one path. Note that
`OS.foo(...)` mangles straight to `@__os_foo` and never enters the body in
`src/lib/os.sf`, so a fix must go in `src/runtime/runtime.sf`.

### 24. `@extern` boxes returns but not `i64` params — a `Ptr` type would close it

`gen_extern_call` (`src/compiler/codegen/intrinsics_body.sf:155`) unboxes every
declared C parameter type *except* `i64`, which it passes through raw:

```saffron
} else {
    // Pass i64 values directly — no untagging needed.
    // Saffron uses identity mode for pointer-as-int values (coro handles, etc.)
    call_args.push("i64 " + val)
}
```

Returns, by contrast, are boxed on every path (`i8*` → tag_ptr, `i32`/`i64` →
tag_int per the Saffron annotation, `double` → tag_float). So a NaN-boxed integer
goes *in* and a correct integer comes *out* — the C function sees garbage:

```saffron
@extern("void* malloc(i64)") fun m_alloc(size: Int): Ptr
var buf = m_alloc(64)   // malloc receives 0x7FF9000000000040, not 64
```

Confirmed live: `malloc(i64 %tagged)`, and `@process` is entirely non-functional
because `sf_process_spawn` gets a tagged `flags` — `Process.run("echo hi")`
returns `code=-1, stdout=""`. `sf_process_poll`, `_write_stdin` and every other
`i64`-param extern in `src/lib/process.sf`, `ssl.sf`, `watch.sf` are equally
affected. 114 of 224 extern declarations take an `i64` parameter.

**Wider than first written: all networking was dead.**
`@extern("i64 sf_tcp_connect(i8*, i64)")` received a tagged `port`, so the
baseline could not connect even to a local `python3 -m http.server`. Nothing
above the socket layer — `httpx`, `net.sf`, async I/O — could ever have worked.

**Why the raw passthrough exists:** roughly 49 of those 114 params are
pointer-as-int values (coroutine handles, `malloc` results, buffer addresses)
that legitimately travel untagged. Blanket untagging would corrupt them; blanket
tagging would corrupt the other 65. The signature cannot disambiguate because
both spell themselves `i64` in C and `Int` in Saffron.

**Immediate fix:** untag `i64` params exactly as the `i32` path already does.
This alone makes `@process` and all networking work, and is independently
shippable.

**Why unconditional untagging is safe here** — not, as first argued, because
handle sites "are annotated `Int`" (so are 194 of the `i64` params, handles
included; that reasoning is backwards). The real justification is arithmetic:
`__val_untag_int` masks 48 bits and sign-extends, so it is the *identity* on a
genuine address while correctly stripping a tag. All four shapes an `Int` param
can carry survive:

| value in | untag out |
|---|---|
| NaN-boxed positive int | payload |
| NaN-boxed negative int | payload (sign-extended) |
| `tag_ptr`'d handle | address, TAG_PTR stripped |
| raw address (coro handle) | unchanged — real addresses are < 2^47 |

Measured over 111 tests at `b5fd568`, comparing exact exit codes:
`gc_test` 139 → 0; `test_file` segfault → 19/20 passing; `@process`
`out=[]` → `out=[hi]`; `malloc`/`memset`/`free` correct. `test_async_io` and
`test_httpx` go 1 → 139, which is **not** a regression: they previously failed
at connect and now get far enough to hit #32.

**Not a runtime tag test.** Sniffing the top 16 bits for `0x7FF8..0x7FFA` looks
tempting but is unsound: a real integer whose payload happens to match, or a heap
address above 2^48, silently takes the wrong branch, and the cost lands in every
FFI call.

**Longer term**, the 49 pointer-as-int sites move to a `Ptr<T>` class with
auto-boxing at the FFI boundary, so the type *says* which discipline applies. Note
that annotating alone is not enough: a class used directly as an extern return type
segfaults on field access, because codegen relabels the bare address instead of
constructing an instance. The compiler must box.

**Do not** auto-box unconditionally in either direction: it trades a loud,
reproducible failure for a silent memory-corruption class.

Full design, including the `OS.*` allowlist removal and the `@intrinsic`
signature gap that share this root cause:
[docs/design/ffi-pointer-discipline.md](docs/design/ffi-pointer-discipline.md).


### 25. Method call directly on an interpolated string literal returns garbage

**Reproduction:**
```saffron
var n = 42
IO.println("${n}".length())      // 105553123967040, not 2
IO.println("x${n}y".length())    // garbage, not 4
```

**Expected:** the length of the interpolated result.
**Actual:** a raw heap address.

Binding to a variable first works, so only the direct-call form is affected:

```saffron
var s = "x${n}y"
IO.println(s.length())           // 4 — correct
```

**Root cause:** interpolation desugars in the lexer to
`"" + (expr).to_string() + ""`. The concatenation produces a raw `char*`;
assigning it to a variable goes through a path that tags it, but calling a
method on the concatenation expression directly passes the untagged pointer as
the receiver, so `length()` reads the address as if it were a value. Same
raw-pointer-leak class as #23, and the static `Ptr` distinction proposed in #24
is what would let codegen tell the two cases apart.

**Impact:** cosmetic but easy to hit — `"${x}".length()` and
`"${x}".to_upper()` silently produce nonsense rather than failing.


### 34. `bootstrap.sh` never builds a gen4, contradicting the promotion criteria

`CLAUDE.md` states, as a promotion criterion, "Gen3 can compile itself
(bootstrap a gen4 from gen3): the test stage in bootstrap.sh verifies this." The
TEST stage does not do this — it compiles and runs sample programs, never a
gen4.

The gap matters because a hand-built gen4 **does not work**: it segfaults on
`IO.println("hi")`. Verified pre-existing by building a gen4 from the unmodified
baseline, which crashes identically, so this is not a regression from any recent
change — but it does mean the documented criterion has never actually held.

Either the criterion or the script should change. Until then, promotion decisions
rest on the sample-program tests alone, and that should be stated honestly rather
than implying a self-hosting check that isn't run.

### 38. Forwarding an `await` through a wrapper function returns the handle, not the result

`Async.gather()` — shipped, documented, and used by `@promise` — returns garbage.
The awaited value comes back as the raw task handle rather than the task's
result:

```saffron
fun mk_int(): Int { Async.sleep(0.01); return 42 }
var t: Task<Int> = Task.spawn(fun () => mk_int())
IO.println(t.await().to_string())          // 42            — correct
// the same await one call frame deeper, inside a coroutine wrapper:
IO.println(Async.await(t).to_string())     // 105553180263168 — the handle
```

**Not a typing problem.** This was first mistaken for one, because a String
result printed as `5.21502e-310` (a tagged pointer read as a float) — the
signature of #32/#37. It isn't: an **`Int`** fails identically, and `Int` needs
no tag interpretation. Annotating `Task<Int>` / `Task<String>` changes the
garbage but does not fix it. A generic `await<T>(task: Task<T>): T` does not fix
it either. The value crossing the return boundary is simply the wrong value.

Narrowed by elimination — these all work, so the defect is specifically *an
await whose result is returned out of a suspending wrapper*:
- a coroutine returning a String it built itself (no await crossing)
- forwarding an awaited `Int` through a wrapper that suspends
- `Task<String>` as a parameter type across a function boundary
- `t.await()` inline at the call site, any result type

**Affected:** `Async.gather()` and `Async.race()` in `src/lib/async.sf`;
`Promise.all()` / `Promise.race()` in `src/lib/promise.sf`, which call
`Async.await`.

**Also missing:** `Async.await` does not exist in `src/lib/async.sf` at all,
though `CLAUDE.md:219` documents it and both `src/lib/promise.sf:10,26` and
`test/async_coop.sf:16-18` call it. Those fail to link
(`_stdlib_async_await` undefined) — which is why `async_coop` is at exit 1.
Adding the function is a two-line change but is **blocked on this bug**: it
would link and then silently return handles instead of results, so it must not
be added until the forwarding path is fixed. Verified pre-existing on both the
pre-#32 and pre-#37 baselines.

### 37. Method dispatch that matches no branch returns a silent zero

`gen_method_call` (`methods_body.sf`) ends with `this.last_type =
AST.Type.IntType; return "0"` and emits an error *only* when `obj_type` is a
known user class missing the method. For a builtin/`Int`/`Any`/empty `obj_type`
— exactly the "type inference failed" case — it falls through emitting **no
call at all** and hands back the literal `0`:

```saffron
fun make(): Int { return 0 }
var x = make()
x.push(5)          // no __list_push emitted; the push vanishes, x unchanged
```

This is the mechanism that turned #33 into a segfault and #36 into lost data:
whenever type inference lands on `Int` for something that isn't, the method
silently no-ops instead of failing the compile.

**Proposed fix:** make the terminal fall-through an error (`has_errors = true`)
for builtin/unknown `obj_type` too, symmetric with the user-class arm. Related
to #33's closing note.

**Progress (2026-07-30):** the `String.push` blocker is gone. It was *not* an
invalid call as originally filed — `map.get()` on an unannotated receiver
claimed `StringType` at both of its inference sites, so `toml.sf`'s
`ensure_array_table` inferred `arr: String` from `current.get(last_key)` and its
`arr.push(new_table)` fell through, silently discarding every array-of-tables
entry. Fixed by returning `Any` when the receiver isn't an annotated `Map<K, V>`
(`methods_body.sf:263` in `get_expr_type`, `:2066` in the dispatch arm).
Annotated maps still narrow to `V`, so this only widens what was already
unknown. `toml_test` 1 → 0; suite fall-throughs 8 → 7; regression test at
`test/test_map_get_types.sf` (fails on the parent commit, passes after).

**Progress (2026-07-30), part 2 — undefined variables are now errors.** The
remaining 7 fall-throughs were all `test/json.sf`, which calls `Json.parse` with
**no `import` statement at all**. Chasing that surfaced a bigger defect in the
same "silent" family: an undeclared variable produced a *warning*
(`expr_body.sf:75` and `:1403`) and then emitted `load i64, i64* %name` with no
matching alloca. The compile still failed — but as `use of undefined value
'%data'` from llc, with no source location, no recognisable name, and
`has_errors` left false, so the compiler reported success right up to the
assembler.

Both sites are now errors that set `has_errors`. Measured before changing
anything, so the "false positives in multi-module context" caveat in the old
comment could be checked rather than trusted:

| Corpus | Files | Files warning |
|---|---|---|
| `test/*.sf` | 111 | 1 (`json.sf`, genuinely broken) |
| `src/lib/*.sf` | 67 | 0 |
| `parsley` + `basil` + `turmeric` src | — | 3 (all one variable) |
| compiler's own source | ~30k lines | 0 (it self-bootstraps clean) |

The 3 app hits are all `turmeric_signal__tracking`, a real global at
`turmeric/src/signal.sf:20` unresolved across the import — i.e. **#22**, not a
false positive. Verified that turmeric's emitted IR *already* fails to assemble
(`clang -x ir` rejects `%turmeric_signal__tracking`), so the hardening does not
break anything that currently works; it converts an opaque IR failure into a
named diagnostic. Suite exit codes unchanged, `test/fail/*` output
byte-identical. Negative test at `test/fail/undefined_variable.sf` — errors and
exits 1 here, warns and exits **0** on the parent commit.

`test/json.sf` also gets its missing `import "@json" as Json`.

**Progress (2026-07-30), part 3 — the upstream causes are gone.** Fall-throughs
across `test/*.sf` + `test/pass/*.sf` + `src/lib/*.sf` went **13 → 1** by fixing
what fed bad types *into* the terminal rather than the terminal itself (see #43):
globals read inside a function no longer claim `Int`, unannotated main-program
globals are registered, and `Number` no longer enters the lattice as `Int` (#48).
The compiler's own source was 0 throughout. The one remaining event is
`method 'Cruller' on obj_type 'Int'` in `test/imports.sf` — a cross-module class
constructor, i.e. a different mechanism (#22 territory), not a type-inference
failure.

**Still open:** the terminal fall-through in `gen_method_call` itself. With
undefined variables now caught earlier, `test/json.sf`'s 7 hits are diagnosed at
their real cause, but the terminal still returns a silent zero for any *other*
receiver whose type inference lands on a builtin. The repro in this entry still
reproduces verbatim:

```saffron
fun make(): Int { return 0 }
var x = make()
x.push(5)          // still no __list_push emitted; prints "survived"
```

Hardening it is now unblocked in principle, and with the upstream causes fixed
the remaining fall-through population is small enough to enumerate. It still
needs its own measurement pass over the same four corpora before flipping, since
a fall-through can also be reached by valid-but-unhandled builtin methods rather
than only by bad types.

## Fixed

- ~~#47: `[1, 2, 3].join(", ")` segfaulted~~ — `__list_join` did a bare
  `__rt_untag_ptr` + `rt_strlen` on every element, which is valid only when the
  element really is a string pointer. For a boxed *number* it untagged the value
  and walked whatever address the mantissa happened to name. It now dispatches on
  the NaN-box tag: pointer-shaped elements keep the original path verbatim —
  interned and static string constants (which the compiler's own IR emission is
  built out of) carry no GC header, so `__rt_as_string_ptr` rejects them and
  `__any_to_string` would render the pointer as an integer — while non-pointer tags
  route through `__rt_elem_to_string`.

  The wasm32 half needed a build fix, not a codegen fix: `tools/saffron` compiled
  the wasm32 runtime **without** `--identity-mode`, which `bootstrap.sh` has always
  passed for the native runtime. `runtime.sf` is the layer that *implements* NaN
  boxing, so it must see values as raw i64 bits. Without the flag, `elem >> 48`
  went through `__val_untag_int`, which on wasm32 converts a float to its integer
  *value* (`fptosi`): `3.0` became `3`, `3 >> 48` became `0`, and `0` is exactly the
  "plausible bare heap pointer" case — so the float was dereferenced as a string
  and `[3.0, 6.0].join(", ")` yielded `", "`.

- ~~#46: A capturing closure returned integer `0` for `nil`~~ — `gen_lambda`
  clears `in_function` before calling `gen_closure_function`, which is deliberate:
  it stops `gen_function` from treating the lambda as a nested function and
  hoisting it. But the flag is overloaded — it also means "emitted code lives
  inside a function body", and `NilLit` is the load-bearing case, emitting the
  literal `"0"` outside a function (correct for a module-level initialiser, where
  no runtime call can be made) and `call i64 @__val_nil()` inside one.

  Under NaN boxing `nil` is `0x7FF8000000000002`, not `0`, so every `return nil` in
  a capturing closure returned integer `0` — neither `nil` nor a valid pointer.
  `x == nil` was then false for a value that *was* nil, and dereferencing it
  segfaulted. `gen_closure_function` now sets `in_function` true for the body and
  restores the saved value at the end, rather than hard-clearing it, so the outer
  emitter's notion of where it is survives.

- ~~#45: `match` had no arm for `is`-class patterns, and Float bindings became
  Int~~ — two defects in `gen_match`. First, `match (x) { is Dog(d) => ..., is
  Cat(c) => ... }` over *class* patterns fell through every branch: there is no
  payload to index into, because the binding **is** the subject. The arm is now
  picked at compile time from the subject's static type (or a wildcard fallback),
  the subject is stored into the binding, and the binding is registered in
  `typed_vars` — required, since an unresolved `Variable` is a hard error as of
  #37's hardening, so `d.bark()` would otherwise fail to resolve.

  Second, `get_variant_field_type` collapsed `Float`/`Number` to `Int`, so
  arithmetic on a match binding emitted integer ops against a NaN-boxed double and
  `Circle(r) => 3.14159 * r * r` evaluated to `nan`. `Float` now stays `Float`;
  identity mode still gets `Int` because `type_to_string_for_target` already
  applies that collapse.

- ~~#44: A variable initialised to `nil` was typed `Nil` for life~~ —
  `var parsed: Any = nil` (and the inferred `var x = nil`) narrowed to `Nil`, and
  nothing widened it on reassignment. After `parsed = JSON.parse(body)` the
  variable still claimed to be `Nil`, and because the `== nil` comparison in
  `gen_binary` keys off the *static* type it emitted `__val_is_nil` against the nil
  literal rather than against the variable — an unconditionally true comparison.
  `parsed == nil` reported nil for a live object, and the playground's
  `/api/compile` rejected every request as "missing 'source' field".

  A nil initialiser says nothing about the variable's type, so it no longer
  narrows: the declaration widens to `Any`, the honest type here, which routes
  through the runtime dispatch helpers that inspect the actual tag. This is I2
  ("`Unknown` is distinct from `Any`") from `docs/design/compiler-rewrite.md`
  applied to the one case the current tree spells unknown as a concrete type.

- ~~#43: Type dishonesty in expression codegen — four silent wrong answers~~ —
  a cluster sharing one mechanism (M1/M2: codegen re-infers types and spells
  unknown as `Int`), fixed together in `expr_body.sf` and the two global pre-scans
  in `codegen.sf`.

  **Globals read inside a function claimed to be `Int`.** Module-level globals are
  registered in `global_var_types` by the pre-scan, not in `typed_vars`, and the
  `Variable` arm fell straight to `IntType` without consulting that table. So
  `var order = "abc"` read at top level worked (`typed_vars` has it there) but
  `fun f() { order.index_of("b") }` inferred `Int`, took no dispatch branch, and
  returned the literal `0` (#37) — a "found at index 0" that ignored the string
  entirely. That asymmetry is what let it survive so long. The arm now consults
  `get_var_type_str`. Relatedly, the main-program pre-scan only ever read the
  *annotation*, so an unannotated `var _order = "abc"` was never registered at all;
  it now infers `List`/`Map`/`String` from the initialiser, matching what the
  module-level copy already did (#36).

  **Operator overloads with a non-variable left operand were skipped.**
  `Vec2(1,2) + Vec2(3,4)` — a constructor call, or a `this.pos`, or a method result
  — gave up when the left operand was not a plain variable, fell through to the
  primitive integer path, and emitted `add i64` on two *pointers*, printing a
  garbage address with no diagnostic. `get_expr_type` already resolves calls and
  field accesses, so it is asked; the answer is accepted **only** if it names a
  declared class, because letting primitives through made `a + b + c` (whose left
  operand is itself a `Binary`) resolve to a primitive that collided with a user
  function sharing the overload method name, rewriting the addition into a call to
  the user's own global `add` (`'add' expects 3 arguments, got 2`).

  **`x == nil` tested the wrong side.** The comparison keyed off the inferred type
  alone, which is wrong when both sides are typed `Nil`: `check_val` picked
  whichever side the type test happened to name, ending up testing the nil literal
  against itself — unconditionally true regardless of what the variable held. It
  now keys off which side is the *syntactic* `nil` literal, the only reliable
  signal for "test the other operand".

  **`Any == "str"` segfaulted in `strcmp`.** `__string_eq`/`__string_ne`
  dereference their operands as `char*` without inspecting the tag, so an `Any`
  operand holding nil (or an int, or a list) crashed. Static-String comparison now
  requires *both* sides to be statically non-`Any`; an `Any` operand falls through
  to the `__any_eq`/`__any_ne` branch, which unmasks and routes on the real tag and
  compares string contents when both sides do turn out to be strings.

  Also here: `Int`→`Float` widening at the enum payload store, so a `Float`/`Number`
  field always holds a double and `get_variant_field_type`'s `Float` is true for
  every construction (#28's fix applied to enum construction instead of variable
  declaration).

- ~~#42: The lexer rejected scientific-notation float literals~~ — `IO.println(1e300)`
  failed with `expected ')' but found 'ident'`: `read_number` stopped at the
  mantissa, so `1e300` lexed as the int `1` followed by an identifier `e300`.
  Every exponent form was unusable — there was no way to write a very large or
  very small literal. `read_number` now consumes `e`/`E`, an optional sign, and
  the exponent digits, but only when a digit actually follows, so `2 * e`
  (multiplication by a variable named `e`) is untouched and `1.e` leaves the
  letter to the identifier scanner. The exponent scan runs after the hex branch
  returns, so `0xe` / `0x1e` keep treating `e` as a hex digit.

  An exponent always produces a `TkFloat`. When the source omits the decimal
  point the mantissa gets a `.0` spliced in (`1e300` → `1.0e300`), because LLVM's
  IR parser reads a point-free mantissa as an *integer* constant and rejects it
  with "integer constant must have integer type" — `emit_tag_float` drops the
  literal text straight into `double` position. Regression test:
  `test/pass/scientific_notation.sf`.

- ~~#39: `IO.println` on lists/maps printed garbage on wasm32~~ — Fixed by
  unifying the wasm32 GC header with native's. The native half (`88297ca`) routes
  bare collection pointers through `__rt_as_list_ptr`/`__rt_as_map_ptr` before the
  pointer/float split in `__any_to_string`; those helpers read `load64(raw - 8)`
  and require the magic sentinel `0x5AFFC0DEDEADBEEF` before trusting the type
  tag. wasm32's header was 8 bytes holding *only* the type tag, so `raw - 8` read
  a small integer, the guard never matched, and collections fell through to
  `do_float` and printed as reinterpreted doubles. Applying the native fix without
  changing the header regressed string printing to `[]`, which is why it was
  reverted the first time.

  The header is now 16 bytes — `[type_tag: i64][magic: i64][user data]` — putting
  the sentinel at `ptr - 8` exactly where native has it. Native uses 24 bytes
  (`[next][info][magic]`) because it threads a real collector; wasm32 never
  collects, so it needs neither the free-list link nor the packed size word, and
  only the magic's *position* has to agree. Every header-touching function in
  `wasm_base_32.ll` was converted in one change (`__gc_alloc`,
  `__gc_alloc_zeroed`, `__gc_realloc`, `__gc_alloc_safe`, `__gc_get_type_tag`,
  `__gc_string_alloc`, `__gc_list_new`, `__gc_list_push`'s realloc path,
  `__gc_map_new`, `__gc_stringbuilder_new`, `__gc_closure_new`, `__gc_env_alloc`,
  `__gc_instance_alloc`) — a partial conversion would corrupt every heap read.
  Struct-field offsets that happen to be `+8` (list capacity, closure env slot)
  are deliberately unchanged; only header offsets moved.

  wasm32 and native now produce byte-identical output for collections, nested
  collections, empty collections, strings, floats and interpolation. This is the
  header-layout unification that stage 9 of `docs/design/compiler-rewrite.md`
  describes, done for one target ahead of the generated-runtime work.

  WasmGC was considered and rejected: it would mean abandoning LLVM for the wasm
  target (LLVM's wasm backend emits linear memory only, not reference types), and
  the existing GC is adequate. `compiler-rewrite.md` part 5 likewise keeps NaN
  boxing — the representation is fine, only its discipline is unmanaged.

- ~~#35: An await loop's body re-executes after coroutine resume~~ — **Filed on a
  wrong diagnosis; not a real loop-structure bug.** The report was written from
  observed duplicate output (`"iter i=0"` printed twice) and attributed to the
  resume path re-entering the loop body block. It was actually a *symptom of
  #32*: the same repro did `result.length()` on an awaited value whose type fell
  back to `Int`, so a String receiver reached `__list_length` and corrupted
  memory — the duplicate lines were garbage output from that corruption, not a
  second pass through the body.

  With #32 fixed (`0b7542a`), the original repro (`tasks[i].await()` over three
  network fetches) segfaults on a pre-#32 tree and prints correct output on the
  current one. Verified by *counting* side effects rather than eyeballing output
  shape, across both suspension paths — `Async.sleep` and real socket I/O: the
  pre-await statement runs exactly once per iteration and every awaited result
  accumulates.

  Regression test at `test/test_await_loop_once.sf`. Note it passes on a pre-#32
  baseline too, precisely because the duplication required #32's corruption to
  manifest; the discriminating case is the network repro, which belongs to #32.
  The test is kept because it pins the property directly by count, which no
  existing test did.

- ~~#36: `push`/`set` on an unannotated module-level list silently vanished~~ —
  Fixed. A module global declared `var _items = []` (no annotation) never had a
  type inferred, so `_items.push(x)` from any function in that module hit the
  #37 fall-through and emitted no `__list_push` — the element was dropped and
  the list stayed empty.

  ```saffron
  // lib.sf
  var _items = []
  fun add(x: Any) { _items.push(x) }
  fun count(): Int { return _items.length() }
  // main.sf:  Lib.add(5); Lib.add(10); Lib.count()  ==> was 0, now 2
  ```

  **Root cause:** the module-global pre-scan in `codegen.sf` registered a global's
  type only from its *annotation* (`get_var_type`), never from its initializer.
  Local `var`s infer through `gen_var_decl_with_name`, but a module global is
  registered by this earlier pass so functions compiled before the module init
  can resolve list/map/string dispatch — and that pass skipped inference
  entirely. So `_items` fell back to `Int`, and `.push` misdispatched.

  The fix infers from the initializer when the annotation is absent, in all
  **three** copies of the pre-scan (`generate_with_modules` and the two `flat`
  variants). Because this pass runs before function return types are registered,
  only literal shapes — list, map, string — are resolved here; that is exactly
  the `[]` / `{}` / `""` global case that misfires. `test.sf`'s `_tests`/
  `_test_errors`, `bytes.sf`'s buffers, and similar stdlib globals are covered.

  Cut the suite's silent fall-throughs from 96 to 8 (the remainder are #37's
  `Json.parse` namespace resolution and one invalid `String.push`), fixed
  `test_buffer` (17/20 → 20/20 — three assertions had been losing pushes to a
  module-global `Buffer`), and changed no other exit code across all 111 tests.


- ~~#33: `to_lower()` on a function's return value produced a null receiver~~ —
  Fixed. Filed as a String-dispatch bug; it was actually a declaration-order bug
  in return-type registration, one call frame earlier.

  The symptom was a segfault in `_platform_strstr` from `client.sf:352`
  (`transfer_enc.to_lower().contains("chunked")`). The IR skipped the
  `rt_str_to_lower` call entirely and passed a literal `0` as the receiver —
  `%t140` was allocated but never defined, the tell-tale of a `fresh_local()`
  whose branch emitted nothing.

  **Root cause:** `codegen.sf`'s pre-scan registers `func_defaults`,
  `func_param_count` and `func_param_names` for every function in every module
  before any IR generation — but *not* `func_ret_types`. That was left to
  `gen_function` (`output_body.sf:10`), which only runs when compilation reaches
  the declaration. `_recv_response` is defined *above* `_extract_header_value` in
  the same file, so at the call site the table had no entry and `last_type` fell
  back to `IntType` — the same dishonest "unknown means Int" fallback as #32.
  `transfer_enc` was then typed `Int`, `to_lower` matched neither the `String`
  nor the `Any` branch, and dispatch fell through returning nothing.

  Instrumenting the compiler was what settled it: printing `obj_type` at the
  String-method gate showed `objtype=[Int] objname=[transfer_enc]` among a dozen
  correct `[String]` dispatches, and printing registration order showed
  `_recv_response` compiled before `_extract_header_value` registered.

  The fix adds return types to the pre-scan, in all **three** copies of that loop
  (`generate_with_modules`, `generate_with_modules_flat_opts`, and
  `..._flat_opts3` — the two `flat` variants iterate `all_stmts`, not
  `df_stmts`, which the first attempt got wrong and the build caught).

  **Why it resisted isolation:** direct chains, function-returned strings,
  cross-module returns and the same chain inside a coroutine all worked, because
  each happened to have the callee registered first. Only same-module
  caller-above-callee ordering triggers it.

  `test_httpx` now passes (139 → 0) and full-suite exit codes are otherwise
  identical. Verified against a local server, since `httpbin.org` — the test's
  target — currently returns 503: `Status: 200` and a body length of exactly
  5000 bytes for a 5000-byte file, both sequentially and through two parallel
  `Task.spawn`ed requests. HTTPS reaches a real handshake (the 503 arrives over
  TLS).

  **Note:** the residual hazard is the silent fall-through, not this instance. A
  builtin-dispatch path that matches no branch returns a zero-valued register
  rather than failing the compile, so the next type-inference gap of this shape
  will also surface as a null-pointer segfault far from its cause.
- ~~#32: `__list_length` receives a tagged list pointer and segfaults~~ — Fixed,
  and the filed diagnosis was wrong in both its title and its root cause.

  The crash is not in `tasks.length()` and the tagged pointer is not a list. In
  the repro, `__list_length` receives a **String** — a NaN-tagged `char*` from
  `TAG_PTR`. Disassembling the caller settled it: the instructions immediately
  before the faulting `bl __list_length` are `strcpy`/`strcat`/`__string_intern`,
  i.e. the `i.to_string() + ": "` concatenation on the *next* line. The receiver
  is `result`, not `tasks`.

  Localizing it took a detour. Breakpoint commands (`br command add`) never fired
  before the fault, and a watchpoint on `@__g_tasks` showed only one write — the
  `__list_new()` from the declaration — proving nothing ever stored a tagged value
  into the list global. Only disassembling the return address (`__saffron_entry +
  776`) revealed the true call site.

  **Root cause:** the `await`/`getResult` branches read the result type from
  `typed_vars[get_variable_name(object)]`, and `get_variable_name` returns `""`
  for anything that is not a bare `Variable`. So `tasks[i].await()` — an
  `IndexGet` — learns nothing and the fallback applied. That fallback was
  `IntType`: a *claim* the value is an integer, when in fact the type is unknown.
  `result.length()` then took the non-String, non-Any branch and emitted
  `call i64 @__list_length(i64 %result)` on a tagged string pointer.

  The fix is to fall back to `AnyType` at all three sites. `Any` is the honest
  answer and the dispatch machinery already handles it: `__any_length`
  (`base_nanbox.ll:1186`) unmasks `TAG_PTR`, checks the GC magic sentinel, and
  routes to `strlen`/`__list_length`/map-count at runtime.

  Not the filed fix. #32 proposed untagging the receiver at
  `methods_body.sf:2201`, which would have masked the tag off a string and fed
  `__list_length` a valid-looking pointer to character data — a silent wrong
  answer in place of a loud crash. It also credited #24's `Ptr` work with fixing
  this; unrelated.

  `test_async_io` now passes end to end for the first time (all three sections:
  Sequential, Parallel, Fan-out). Verified across the full 111-test suite against
  a baseline built from the previous commit: the *only* changed exit code is
  `test_async_io` 139 → 0.

  One rough edge left deliberately: the `Any` path prints
  `[codegen] Warning: dispatching 'length' on untyped value` on stderr for code
  that is now perfectly correct. Silencing it would need real inference of a
  task's result type through a container, which is a larger change than this fix.
- ~~#28: an `Int` literal assigned to a `Float` annotation became NaN~~ — Fixed:
  `gen_var_decl_with_name` stored the literal with its `TAG_INT` intact while every
  downstream read treated the bits as a double, so `var f: Float = 1` came back as
  the subnormal `4.94e-324`, or NaN once it reached a tag-inspecting path.

  Only the declaration path was broken. Mixed arithmetic (`1 + 1.0`) always
  worked because `__val_untag_float` (`base_nanbox.ll:263`) already converts an
  int-tagged operand via `sitofp`; the fix reuses exactly that conversion as an
  untag_float/tag_float round trip rather than duplicating a `sitofp`.

  **Gated on `!identity_mode`, deliberately.** Identity mode has no tags and
  compiles `Float` arithmetic as `add i64`, so `Float` *is* `Int` there — and the
  compiler's own sources rely on that in 455 places (`var pos: Float = 0`,
  `var depth: Float = 1`, `var slash_idx: Float = -1`). Converting unconditionally
  would have corrupted every one of them.
- ~~#30: a builtin module function couldn't be passed as a value~~ — Fixed, and it
  was narrower than the write-up suggested. The MemberAccess handler in
  `expr_body.sf` *does* have a module-prefixed function-reference branch, and
  `module_prefixes` *does* map `IO -> __io_`; the branch was skipped only because
  it gates on `known_functions`, and `__io_println`/`__io_print` were the sole
  `IO.*` entries never registered there. They are special-cased earlier on the
  *call* path (`methods_body.sf:976`, `expr_body.sf:1748`), so nothing ever
  needed them in the table — until they appeared in value position, where the
  miss fell through to a load from the nonexistent local `%IO`.

  So this was not the same defect as #22: #22 is a genuinely missing branch for
  module-level globals, this was a missing table entry. `IO.read_file` and every
  `OS.*` function already worked as values.

  Registering the two names in `known_functions` and `runtime_declares()`, plus
  `func_param_count` entries (`gen_func_ref` sizes the trampoline from it and
  would otherwise emit a 0-arg `call` against a 1-param declaration), is the whole
  fix. The call path is unaffected because its special cases run first.

  This makes the `Iter.each(["a","b","c"], IO.println)` example in
  `src/lib/iter.sf`'s docstring — previously aspirational — actually run.
- ~~#29: indexing a String segfaulted~~ — Fixed: `gen_index_get` had a `Map`
  special case but nothing for `String`, so `s[0]` fell through to the list path
  and `__list_get` read a character as if it were a list header. It now
  dispatches on `last_type == "String"` — the same `last_type` that already made
  `s.length()` resolve correctly, including through an `Any` annotation — and
  calls a new `__str_get` runtime helper.

  This also makes **`for (c in "ab")` work**, since the `for-in` desugaring is
  index-based and went straight through `s[i]`.

  `__str_get` mirrors `__list_get`, not `char_at`: negative indices count from
  the end (`s[-1]`), and out of range raises a fatal IndexError instead of
  reading past the terminator. `char_at(5)` on a 2-char string still returns
  garbage silently — matching that would have been the wrong precedent. It
  allocates with `rt_malloc` rather than `__gc_alloc` because, per #23, tagging
  GC-allocated memory makes the collector sweep it while live.
- ~~#31: a nested `for-in` visited only the first element of the outer loop~~ —
  Fixed: `desugar_for_in` in `src/compiler/parser.sf` named its temporaries
  `__for_list` / `__for_i` unconditionally. Nested loops share a scope chain, so
  the inner loop redeclared the outer loop's cursor and left it past the end,
  making the outer `while` condition false on the first re-test. The names are now
  gensym'd through `Parser.next_for_uid()`, and `parse_for_in` — which carried a
  second, near-identical copy of the desugaring — delegates to the one
  implementation so the two cannot drift again.
- ~~#26: `0.0 / 0.0` printed `(null)` and faulted through a Map~~ — Fixed: canonical
  quiet NaN is `0x7FF8000000000000`, bit-identical to TAG_PTR with a null payload,
  so `__val_is_ptr` (`upper == 0x7FF8`) read every NaN as a null pointer.
  `__val_tag_float` now remaps the three colliding patterns (`0x7FF8`/`0x7FF9`/
  `0x7FFA`) to `0x7FFC000000000000`, still a quiet NaN but outside the tag range.
  Only an all-ones exponent with a set high mantissa bit can reach that range, so
  no finite double is affected. Applied to all four runtimes (`base.ll`,
  `base_nanbox.ll`, `wasm_base.ll`, `wasm_base_32.ll`).
- ~~#27: comparison operators were silently dropped inside string interpolation~~ —
  Fixed: `read_interpolation` in `src/compiler/lexer.sf` carried its own shorter
  copy of the operator table covering only `( ) + - * / . , [ ]` and discarded
  every other character, so `"${a != b}"` lexed as `a b`. Both lexers now call a
  shared `lex_operator`.
- ~~#1: Functions in imported modules can't call each other~~ — Fixed: ObjFunction now stores owning module, OP_GET_GLOBAL uses correct module context.
- ~~#3: Type checker if-block scoping~~ — Not a bug, correct behavior.
- ~~#4: @import path resolution~~ — Fixed: findModule call in parseFile.
- ~~#5: Map() not callable~~ — Fixed: set returnType on Map's init FunctorType.
- ~~#7: No string escape sequences~~ — Fixed.
- ~~#8: list vs List case~~ — Fixed.
- ~~#9: anyType not universal supertype~~ — Fixed: NODE_LIST/NODE_MAP/NODE_GET/NODE_CALL all handle anyType.
- ~~#10: Imported module functions crash~~ — Fixed: same as #1.
- ~~Nested closures crash~~ — Fixed: declareVariable for nested fun declarations.
- ~~#14: break/continue rejected in while/for loops~~ — Fixed: added loop depth tracking for NODE_WHILE and NODE_FOR.
- ~~#15: Qualified type references (`Module.Type`)~~ — Fixed: parser handles dotted names in type annotations, type checker resolves from module.
- ~~#16: Empty list not assignable to typed `List<T>` fields~~ — Fixed: NODE_SET propagates field type as currentAssignmentType; raw `List` accepted for list literals.
- ~~#17: Type checker NULL file path for relative imports~~ — Fixed: `setTypecheckFile(path)` called before `evaluateTree` in checkFile mode.
- ~~#18: Cross-module enum pattern matching~~ — Fixed: OP_IMPORT now pops the path string (was leaking on stack); match arms support dotted qualified names.
- ~~#19: GC not marking suspended call frame locals~~ — Fixed: OBJ_CALL_FRAME marking now traces `frame->stack`, `frame->stored`, `frame->result`.
- ~~#11: Flow narrowing for primitives in unions~~ — Fixed: narrowing check used `Expr.type` (TypeNode pointer) instead of `Expr.self.type` (NodeType enum) — always read as non-NODE_BINARY.
- ~~#13: Bare return without semicolon~~ — Fixed: `returnStatement()` now checks for `}` and EOF as implicit void return.
- ~~#20: List.pop() removes first element~~ — Fixed: pop now removes from `count - 1` instead of index 0.
